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
//gl_warp.c -- warping animation support

#include "quakedef.h"

extern cvar_t r_drawflat;

cvar_t r_waterquality = {"r_waterquality", "8", CVAR_NONE};
cvar_t r_waterwarp = {"r_waterwarp", "1", CVAR_NONE};

float load_subdivide_size; //johnfitz -- remember what subdivide_size value was when this map was loaded

float	turbsin[] =
{
#include "gl_warp_sin.h"
};

#define WARPCALC(s,t) ((s + turbsin[(int)((t*2)+(cl.time*(128.0/M_PI))) & 255]) * (1.0/64)) //johnfitz -- correct warp

//==============================================================================
//
//  OLD-STYLE WATER
//
//==============================================================================

extern	qmodel_t	*loadmodel;

msurface_t	*warpface;

cvar_t gl_subdivide_size = {"gl_subdivide_size", "128", CVAR_ARCHIVE};

void BoundPoly (int numverts, float *verts, vec3_t mins, vec3_t maxs)
{
	int		i, j;
	float	*v;

	mins[0] = mins[1] = mins[2] = 9999;
	maxs[0] = maxs[1] = maxs[2] = -9999;
	v = verts;
	for (i=0 ; i<numverts ; i++)
		for (j=0 ; j<3 ; j++, v++)
		{
			if (*v < mins[j])
				mins[j] = *v;
			if (*v > maxs[j])
				maxs[j] = *v;
		}
}

void SubdividePolygon (int numverts, float *verts)
{
	int		i, j, k;
	vec3_t	mins, maxs;
	float	m;
	float	*v;
	vec3_t	front[64], back[64];
	int		f, b;
	float	dist[64];
	float	frac;
	glpoly_t	*poly;
	float	s, t;

	if (numverts > 60)
		Sys_Error ("numverts = %i", numverts);

	BoundPoly (numverts, verts, mins, maxs);

	for (i=0 ; i<3 ; i++)
	{
		m = (mins[i] + maxs[i]) * 0.5;
		m = gl_subdivide_size.value * floor (m/gl_subdivide_size.value + 0.5);
		if (maxs[i] - m < 8)
			continue;
		if (m - mins[i] < 8)
			continue;

		// cut it
		v = verts + i;
		for (j=0 ; j<numverts ; j++, v+= 3)
			dist[j] = *v - m;

		// wrap cases
		dist[j] = dist[0];
		v-=i;
		VectorCopy (verts, v);

		f = b = 0;
		v = verts;
		for (j=0 ; j<numverts ; j++, v+= 3)
		{
			if (dist[j] >= 0)
			{
				VectorCopy (v, front[f]);
				f++;
			}
			if (dist[j] <= 0)
			{
				VectorCopy (v, back[b]);
				b++;
			}
			if (dist[j] == 0 || dist[j+1] == 0)
				continue;
			if ( (dist[j] > 0) != (dist[j+1] > 0) )
			{
				// clip point
				frac = dist[j] / (dist[j] - dist[j+1]);
				for (k=0 ; k<3 ; k++)
					front[f][k] = back[b][k] = v[k] + frac*(v[3+k] - v[k]);
				f++;
				b++;
			}
		}

		SubdividePolygon (f, front[0]);
		SubdividePolygon (b, back[0]);
		return;
	}

	poly = (glpoly_t *) Hunk_Alloc (sizeof(glpoly_t) + (numverts-4) * VERTEXSIZE*sizeof(float));
	poly->next = warpface->polys->next;
	warpface->polys->next = poly;
	poly->numverts = numverts;
	for (i=0 ; i<numverts ; i++, verts+= 3)
	{
		VectorCopy (verts, poly->verts[i]);
		s = DotProduct (verts, warpface->texinfo->vecs[0]);
		t = DotProduct (verts, warpface->texinfo->vecs[1]);
		poly->verts[i][3] = s;
		poly->verts[i][4] = t;
	}
}

/*
================
GL_SubdivideSurface
================
*/
void GL_SubdivideSurface (msurface_t *fa)
{
	vec3_t	verts[64];
	int		i;

	warpface = fa;

	//the first poly in the chain is the undivided poly for newwater rendering.
	//grab the verts from that.
	for (i=0; i<fa->polys->numverts; i++)
		VectorCopy (fa->polys->verts[i], verts[i]);

	SubdividePolygon (fa->polys->numverts, verts[0]);
}

//==============================================================================
//
//  RENDER-TO-FRAMEBUFFER WATER
//
//==============================================================================

/*
=============
R_UpdateWarpTextures -- johnfitz -- each frame, update warping textures
=============
*/
static texture_t * warp_textures[MAX_GLTEXTURES];
static VkImageMemoryBarrier warp_image_barriers[MAX_GLTEXTURES];

void R_UpdateWarpTextures (void)
{
	texture_t *tx;
	int i;
	float x, y, x2, warptess;

	if (cl.paused || r_drawflat_cheatsafe || r_lightmap_cheatsafe)
		return;

	warptess = 128.0/CLAMP (3.0, floor(r_waterquality.value), 64.0);

	int num_textures = cl.worldmodel->numtextures;
	int num_warp_textures = 0;

	// Render warp to top mips
	for (i = 0; i < num_textures; ++i)
	{
		if (!(tx = cl.worldmodel->textures[i]))
			continue;

		if (!tx->update_warp)
			continue;

		VkRect2D render_area;
		render_area.offset.x = 0;
		render_area.offset.y = 0;
		render_area.extent.width = WARPIMAGESIZE;
		render_area.extent.height = WARPIMAGESIZE;

		VkRenderPassBeginInfo render_pass_begin_info;
		memset(&render_pass_begin_info, 0, sizeof(render_pass_begin_info));
		render_pass_begin_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		render_pass_begin_info.renderArea = render_area;
		render_pass_begin_info.renderPass = vulkan_globals.warp_render_pass;
		render_pass_begin_info.framebuffer = tx->warpimage->frame_buffer;

		vkCmdBeginRenderPass(vulkan_globals.command_buffer, &render_pass_begin_info, VK_SUBPASS_CONTENTS_INLINE);

		//render warp
		GL_SetCanvas (CANVAS_WARPIMAGE);
		vkCmdBindPipeline(vulkan_globals.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_globals.warp_pipeline);
		vkCmdBindDescriptorSets(vulkan_globals.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_globals.basic_pipeline_layout, 0, 1, &tx->gltexture->descriptor_set, 0, NULL);

		int num_verts = 0;
		for (y=0.0; y<128.01; y+=warptess) // .01 for rounding errors
			num_verts += 2;

		for (x=0.0; x<128.0; x=x2)
		{	
			VkBuffer buffer;
			VkDeviceSize buffer_offset;
			basicvertex_t * vertices = (basicvertex_t*)R_VertexAllocate(num_verts * sizeof(basicvertex_t), &buffer, &buffer_offset);

			int i = 0;
			x2 = x + warptess;
			for (y=0.0; y<128.01; y+=warptess) // .01 for rounding errors
			{
				vertices[i].position[0] = x;
				vertices[i].position[1] = y;
				vertices[i].position[2] = 0.0f;
				vertices[i].texcoord[0] = WARPCALC(x,y);
				vertices[i].texcoord[1] = WARPCALC(y,x);
				vertices[i].color[0] = 255;
				vertices[i].color[1] = 255;
				vertices[i].color[2] = 255;
				vertices[i].color[3] = 255;
				i += 1;
				vertices[i].position[0] = x2;
				vertices[i].position[1] = y;
				vertices[i].position[2] = 0.0f;
				vertices[i].texcoord[0] = WARPCALC(x2,y);
				vertices[i].texcoord[1] = WARPCALC(y,x2);
				vertices[i].color[0] = 255;
				vertices[i].color[1] = 255;
				vertices[i].color[2] = 255;
				vertices[i].color[3] = 255;
				i += 1;
			}

			vkCmdBindVertexBuffers(vulkan_globals.command_buffer, 0, 1, &buffer, &buffer_offset);
			vkCmdDraw(vulkan_globals.command_buffer, num_verts, 1, 0, 0);
		}

		vkCmdEndRenderPass(vulkan_globals.command_buffer);

		VkImageMemoryBarrier * image_barrier = &warp_image_barriers[num_warp_textures];
		image_barrier->sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		image_barrier->pNext = NULL;
		image_barrier->srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
		image_barrier->dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
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

		warp_textures[num_warp_textures] = tx;
		num_warp_textures += 1;
	}

	// Make sure that writes are done for top mips we just rendered to
	VkMemoryBarrier memory_barrier;
	memory_barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
	memory_barrier.pNext = NULL;
	memory_barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	memory_barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

	// Transfer all other mips from UNDEFINED to GENERAL layout
	vkCmdPipelineBarrier(vulkan_globals.command_buffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &memory_barrier, 0, NULL, num_warp_textures, warp_image_barriers);

	// Generate mip chains
	for (int mip = 1; mip < WARPIMAGEMIPS; ++mip)
	{
		int srcSize = WARPIMAGESIZE >> (mip - 1);
		int dstSize = WARPIMAGESIZE >> mip;

		for (i = 0; i < num_warp_textures; ++i)
		{
			tx = warp_textures[i];

			VkImageBlit region;
			memset(&region, 0, sizeof(region));
			region.srcOffsets[1].x = srcSize;
			region.srcOffsets[1].y = srcSize;
			region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			region.srcSubresource.layerCount = 1;
			region.srcSubresource.mipLevel = (mip - 1);
			region.dstOffsets[1].x = dstSize;
			region.dstOffsets[1].y = dstSize;
			region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			region.dstSubresource.layerCount = 1;
			region.dstSubresource.mipLevel = mip;

			vkCmdBlitImage(vulkan_globals.command_buffer, tx->warpimage->image, VK_IMAGE_LAYOUT_GENERAL, tx->warpimage->image, VK_IMAGE_LAYOUT_GENERAL, 1, &region, VK_FILTER_LINEAR);
		}

		if (mip < (WARPIMAGEMIPS - 1))
		{
			memory_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			memory_barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			vkCmdPipelineBarrier(vulkan_globals.command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &memory_barrier, 0, NULL, 0, NULL);
		}
	}

	// Transfer all warp texture mips from GENERAL to SHADER_READ_ONLY_OPTIMAL
	for (i = 0; i < num_warp_textures; ++i)
	{
		tx = warp_textures[i];
		
		VkImageMemoryBarrier * image_barrier = &warp_image_barriers[i];
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

		tx->update_warp = false;
	}

	vkCmdPipelineBarrier(vulkan_globals.command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, NULL, 0, NULL, num_warp_textures, warp_image_barriers);

	//if warp render went down into sbar territory, we need to be sure to refresh it next frame
	if (WARPIMAGESIZE + sb_lines > glheight)
		Sbar_Changed ();

	//if viewsize is less than 100, we need to redraw the frame around the viewport
	scr_tileclear_updates = 0;
}
