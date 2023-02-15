/*
Copyright (C) 1996-2001 Id Software, Inc.
Copyright (C) 2002-2009 John Fitzgibbons and others
Copyright (C) 2010-2014 QuakeSpasm developers
Copyright (C) 2016 Axel Gneiting

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
*/
// gl_warp.c -- warping animation support

#include "quakedef.h"

extern cvar_t r_drawflat;

cvar_t r_waterquality = {"r_waterquality", "8", CVAR_NONE};
cvar_t r_waterwarp = {"r_waterwarp", "1", CVAR_ARCHIVE};
cvar_t r_waterwarpcompute = {"r_waterwarpcompute", "1", CVAR_ARCHIVE};

float turbsin[] = {
#include "gl_warp_sin.h"
};

#define WARPCALC(s, t) ((s + turbsin[(int)((t * 2) + (cl.time * (128.0 / M_PI))) & 255]) * (1.0 / 64)) // johnfitz -- correct warp

//==============================================================================
//
//  RENDER-TO-FRAMEBUFFER WATER
//
//==============================================================================
static void R_RasterWarpTexture (cb_context_t *cbx, texture_t *tx, float warptess)
{
	float x, y, x2;

	VkRect2D render_area;
	render_area.offset.x = 0;
	render_area.offset.y = 0;
	render_area.extent.width = WARPIMAGESIZE;
	render_area.extent.height = WARPIMAGESIZE;

	ZEROED_STRUCT (VkRenderPassBeginInfo, render_pass_begin_info);
	render_pass_begin_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	render_pass_begin_info.renderArea = render_area;
	render_pass_begin_info.renderPass = vulkan_globals.warp_render_pass;
	render_pass_begin_info.framebuffer = tx->warpimage->frame_buffer;

	vkCmdBeginRenderPass (cbx->cb, &render_pass_begin_info, VK_SUBPASS_CONTENTS_INLINE);

	// render warp
	R_BindPipeline (cbx, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_globals.raster_tex_warp_pipeline);
	GL_SetCanvas (cbx, CANVAS_WARPIMAGE);
	vkCmdSetScissor (cbx->cb, 0, 1, &render_area);
	if (!r_lightmap_cheatsafe)
		vkCmdBindDescriptorSets (
			cbx->cb, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_globals.basic_pipeline_layout.handle, 0, 1, &tx->gltexture->descriptor_set, 0, NULL);
	else
		vkCmdBindDescriptorSets (
			cbx->cb, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_globals.basic_pipeline_layout.handle, 0, 1, &whitetexture->descriptor_set, 0, NULL);

	int num_verts = 0;
	for (y = 0.0; y < 128.01; y += warptess) // .01 for rounding errors
		num_verts += 2;

	for (x = 0.0; x < 128.0; x = x2)
	{
		VkBuffer	   buffer;
		VkDeviceSize   buffer_offset;
		basicvertex_t *vertices = (basicvertex_t *)R_VertexAllocate (num_verts * sizeof (basicvertex_t), &buffer, &buffer_offset);

		int i = 0;
		x2 = x + warptess;
		for (y = 0.0; y < 128.01; y += warptess) // .01 for rounding errors
		{
			vertices[i].position[0] = x;
			vertices[i].position[1] = y;
			vertices[i].position[2] = 0.0f;
			vertices[i].texcoord[0] = WARPCALC (x, y);
			vertices[i].texcoord[1] = 1.0f - WARPCALC (y, x);
			vertices[i].color[0] = 255;
			vertices[i].color[1] = 255;
			vertices[i].color[2] = 255;
			vertices[i].color[3] = 255;
			i += 1;
			vertices[i].position[0] = x2;
			vertices[i].position[1] = y;
			vertices[i].position[2] = 0.0f;
			vertices[i].texcoord[0] = WARPCALC (x2, y);
			vertices[i].texcoord[1] = 1.0f - WARPCALC (y, x2);
			vertices[i].color[0] = 255;
			vertices[i].color[1] = 255;
			vertices[i].color[2] = 255;
			vertices[i].color[3] = 255;
			i += 1;
		}

		vkCmdBindVertexBuffers (cbx->cb, 0, 1, &buffer, &buffer_offset);
		vkCmdDraw (cbx->cb, num_verts, 1, 0, 0);
	}

	vkCmdEndRenderPass (cbx->cb);
}

/*
=============
R_ComputeWarpTexture
=============
*/
static void R_ComputeWarpTexture (cb_context_t *cbx, texture_t *tx, float warptess)
{
	// render warp
	const float time = cl.time;
	R_BindPipeline (cbx, VK_PIPELINE_BIND_POINT_COMPUTE, vulkan_globals.cs_tex_warp_pipeline);
	VkDescriptorSet sets[2] = {tx->gltexture->descriptor_set, tx->warpimage->storage_descriptor_set};
	if (r_lightmap_cheatsafe)
		sets[0] = whitetexture->descriptor_set;
	vkCmdBindDescriptorSets (cbx->cb, VK_PIPELINE_BIND_POINT_COMPUTE, vulkan_globals.cs_tex_warp_pipeline.layout.handle, 0, 2, sets, 0, NULL);
	R_PushConstants (cbx, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof (float), &time);
	vkCmdDispatch (cbx->cb, WARPIMAGESIZE / 8, WARPIMAGESIZE / 8, 1);
}

/*
=============
R_UpdateWarpTextures -- johnfitz -- each frame, update warping textures
=============
*/
static texture_t		   *warp_textures[MAX_GLTEXTURES];
static VkImageMemoryBarrier warp_image_barriers[MAX_GLTEXTURES];

void R_UpdateWarpTextures (void *unused)
{
	cb_context_t *cbx = &vulkan_globals.primary_cb_contexts[PCBX_UPDATE_WARP];
	GL_SetCanvas (cbx, CANVAS_NONE); // Invalidate canvas so push constants get set later

	texture_t *tx;
	int		   i, mip;
	float	   warptess;

	if (cl.paused)
		return;

	R_BeginDebugUtilsLabel (cbx, "Update Warp Textures");

	warptess = 128.0 / CLAMP (3.0, floor (r_waterquality.value), 64.0);

	int num_warp_textures = 0;

	// Count warp texture & prepare barrier from undefined to GENERL if using compute warp
	for (int j = 1; j < MAX_MODELS; j++)
	{
		qmodel_t *m = cl.model_precache[j];

		if (!m)
			break;
		if (m->name[0] == '*')
			continue;
		if (j > 1 && !(m->used_specials & SURF_DRAWTURB))
			continue;

		for (i = 0; i < m->numtextures; ++i)
		{
			if (!(tx = m->textures[i]))
				continue;

			if (!Atomic_LoadUInt32 (&tx->update_warp))
				continue;

			if (r_waterwarpcompute.value)
			{
				VkImageMemoryBarrier *image_barrier = &warp_image_barriers[num_warp_textures];
				image_barrier->sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
				image_barrier->pNext = NULL;
				image_barrier->srcAccessMask = 0;
				image_barrier->dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
				image_barrier->oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
				image_barrier->newLayout = VK_IMAGE_LAYOUT_GENERAL;
				image_barrier->srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				image_barrier->dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				image_barrier->image = tx->warpimage->image;
				image_barrier->subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				image_barrier->subresourceRange.baseMipLevel = 0;
				image_barrier->subresourceRange.levelCount = WARPIMAGEMIPS;
				image_barrier->subresourceRange.baseArrayLayer = 0;
				image_barrier->subresourceRange.layerCount = 1;
			}

			warp_textures[num_warp_textures] = tx;
			num_warp_textures += 1;
		}
	}

	// Transfer mips from UNDEFINED to GENERAL layout
	if (r_waterwarpcompute.value)
		vkCmdPipelineBarrier (
			cbx->cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, NULL, 0, NULL, num_warp_textures, warp_image_barriers);

	// Render warp to top mips
	for (i = 0; i < num_warp_textures; ++i)
	{
		tx = warp_textures[i];

		if (r_waterwarpcompute.value)
			R_ComputeWarpTexture (cbx, tx, warptess);
		else
			R_RasterWarpTexture (cbx, tx, warptess);

		VkImageMemoryBarrier *image_barrier = &warp_image_barriers[i];
		image_barrier->sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		image_barrier->pNext = NULL;
		image_barrier->srcAccessMask = 0;
		image_barrier->dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		image_barrier->oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		image_barrier->newLayout = VK_IMAGE_LAYOUT_GENERAL;
		image_barrier->srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		image_barrier->dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		image_barrier->image = tx->warpimage->image;
		image_barrier->subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		image_barrier->subresourceRange.baseMipLevel = 1;
		image_barrier->subresourceRange.levelCount = WARPIMAGEMIPS - 1;
		image_barrier->subresourceRange.baseArrayLayer = 0;
		image_barrier->subresourceRange.layerCount = 1;
	}

	// Make sure that writes are done for top mips we just rendered to
	VkMemoryBarrier memory_barrier;
	memory_barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
	memory_barrier.pNext = NULL;
	memory_barrier.srcAccessMask = r_waterwarpcompute.value ? VK_ACCESS_SHADER_WRITE_BIT : VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	memory_barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

	// Transfer all other mips from UNDEFINED to GENERAL layout
	vkCmdPipelineBarrier (
		cbx->cb, r_waterwarpcompute.value ? VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT : VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &memory_barrier, 0, NULL, num_warp_textures, warp_image_barriers);

	// Generate mip chains
	for (mip = 1; mip < WARPIMAGEMIPS; ++mip)
	{
		int srcSize = WARPIMAGESIZE >> (mip - 1);
		int dstSize = WARPIMAGESIZE >> mip;

		for (i = 0; i < num_warp_textures; ++i)
		{
			tx = warp_textures[i];

			VkImageBlit region;
			memset (&region, 0, sizeof (region));
			region.srcOffsets[1].x = srcSize;
			region.srcOffsets[1].y = srcSize;
			region.srcOffsets[1].z = 1;
			region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			region.srcSubresource.layerCount = 1;
			region.srcSubresource.mipLevel = (mip - 1);
			region.dstOffsets[1].x = dstSize;
			region.dstOffsets[1].y = dstSize;
			region.dstOffsets[1].z = 1;
			region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			region.dstSubresource.layerCount = 1;
			region.dstSubresource.mipLevel = mip;

			vkCmdBlitImage (
				cbx->cb, tx->warpimage->image, VK_IMAGE_LAYOUT_GENERAL, tx->warpimage->image, VK_IMAGE_LAYOUT_GENERAL, 1, &region, VK_FILTER_LINEAR);
		}

		if (mip < (WARPIMAGEMIPS - 1))
		{
			memory_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			memory_barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			vkCmdPipelineBarrier (cbx->cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &memory_barrier, 0, NULL, 0, NULL);
		}
	}

	// Transfer all warp texture mips from GENERAL to SHADER_READ_ONLY_OPTIMAL
	for (i = 0; i < num_warp_textures; ++i)
	{
		tx = warp_textures[i];

		VkImageMemoryBarrier *image_barrier = &warp_image_barriers[i];
		image_barrier->sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		image_barrier->pNext = NULL;
		image_barrier->srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		image_barrier->dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		image_barrier->oldLayout = VK_IMAGE_LAYOUT_GENERAL;
		image_barrier->newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		image_barrier->srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		image_barrier->dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		image_barrier->image = tx->warpimage->image;
		image_barrier->subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		image_barrier->subresourceRange.baseMipLevel = 0;
		image_barrier->subresourceRange.levelCount = WARPIMAGEMIPS;
		image_barrier->subresourceRange.baseArrayLayer = 0;
		image_barrier->subresourceRange.layerCount = 1;

		Atomic_StoreUInt32 (&tx->update_warp, false);
	}

	vkCmdPipelineBarrier (
		cbx->cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, NULL, 0, NULL, num_warp_textures, warp_image_barriers);

	R_EndDebugUtilsLabel (cbx);
}
