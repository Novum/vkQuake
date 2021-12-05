/*
Copyright (C) 1996-2001 Id Software, Inc.
Copyright (C) 2002-2009 John Fitzgibbons and others
Copyright (C) 2007-2008 Kristian Duske
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
// r_world.c: world model rendering

#include "quakedef.h"

extern cvar_t gl_fullbrights, r_drawflat, r_oldskyleaf, r_showtris, r_simd, gl_zfix; //johnfitz

byte *SV_FatPVS (vec3_t org, qmodel_t *worldmodel);

extern VkBuffer bmodel_vertex_buffer;

//==============================================================================
//
// SETUP CHAINS
//
//==============================================================================

/*
================
R_ClearTextureChains -- ericw 

clears texture chains for all textures used by the given model, and also
clears the lightmap chains
================
*/
void R_ClearTextureChains (qmodel_t *mod, texchain_t chain)
{
	int i;

	// set all chains to null
	for (i=0 ; i<mod->numtextures ; i++)
		if (mod->textures[i])
			mod->textures[i]->texturechains[chain] = NULL;
}

/*
================
R_ChainSurface -- ericw -- adds the given surface to its texture chain
================
*/
void R_ChainSurface (msurface_t *surf, texchain_t chain)
{
	surf->texturechain = surf->texinfo->texture->texturechains[chain];
	surf->texinfo->texture->texturechains[chain] = surf;
}

/*
================
R_BackFaceCull -- johnfitz -- returns true if the surface is facing away from vieworg
================
*/
qboolean R_BackFaceCull (msurface_t *surf)
{
	double dot;

	if (surf->plane->type < 3)
		dot = r_refdef.vieworg[surf->plane->type] - surf->plane->dist;
	else
		dot = DotProduct (r_refdef.vieworg, surf->plane->normal) - surf->plane->dist;

	if ((dot < 0) ^ !!(surf->flags & SURF_PLANEBACK))
		return true;

	return false;
}

#ifdef USE_SSE2
/*
===============
R_BackFaceCullSIMD

Performs backface culling for 8 planes
===============
*/
byte R_BackFaceCullSIMD (soa_plane_t plane)
{
	__m128 pos = _mm_loadu_ps(r_refdef.vieworg);

	__m128 px = _mm_shuffle_ps(pos, pos, _MM_SHUFFLE(0, 0, 0, 0));
	__m128 v0 = _mm_mul_ps(_mm_loadu_ps(plane + 0), px);
	__m128 v1 = _mm_mul_ps(_mm_loadu_ps(plane + 4), px);

	__m128 py = _mm_shuffle_ps(pos, pos, _MM_SHUFFLE(1, 1, 1, 1));
	v0 = _mm_add_ps(v0, _mm_mul_ps(_mm_loadu_ps(plane +  8), py));
	v1 = _mm_add_ps(v1, _mm_mul_ps(_mm_loadu_ps(plane + 12), py));

	__m128 pz = _mm_shuffle_ps(pos, pos, _MM_SHUFFLE(2, 2, 2, 2));
	v0 = _mm_add_ps(v0, _mm_mul_ps(_mm_loadu_ps(plane + 16), pz));
	v1 = _mm_add_ps(v1, _mm_mul_ps(_mm_loadu_ps(plane + 20), pz));

	__m128 pd0 = _mm_loadu_ps(plane + 24);
	__m128 pd1 = _mm_loadu_ps(plane + 28);

	return _mm_movemask_ps(_mm_cmplt_ps(pd0, v0)) | (_mm_movemask_ps(_mm_cmplt_ps(pd1, v1)) << 4);
}

/*
===============
R_CullBoxSIMD

Performs frustum culling for 8 bounding boxes
===============
*/
byte R_CullBoxSIMD (soa_aabb_t box, byte activelanes)
{
	int i;
	for (i = 0; i < 4; i++)
	{
		mplane_t *p;
		byte signbits;
		int ofs;

		if (activelanes == 0)
			break;

		p = frustum + i;
		signbits = p->signbits;

		__m128 vplane = _mm_loadu_ps(p->normal);

		ofs = signbits & 1 ? 0 : 8; // x min/max
		__m128 px = _mm_shuffle_ps(vplane, vplane, _MM_SHUFFLE(0, 0, 0, 0));
		__m128 v0 = _mm_mul_ps(_mm_loadu_ps(box + ofs), px);
		__m128 v1 = _mm_mul_ps(_mm_loadu_ps(box + ofs + 4), px);

		ofs = signbits & 2 ? 16 : 24; // y min/max
		__m128 py = _mm_shuffle_ps(vplane, vplane, _MM_SHUFFLE(1, 1, 1, 1));
		v0 = _mm_add_ps(v0, _mm_mul_ps(_mm_loadu_ps(box + ofs), py));
		v1 = _mm_add_ps(v1, _mm_mul_ps(_mm_loadu_ps(box + ofs + 4), py));

		ofs = signbits & 4 ? 32 : 40; // z min/max
		__m128 pz = _mm_shuffle_ps(vplane, vplane, _MM_SHUFFLE(2, 2, 2, 2));
		v0 = _mm_add_ps(v0, _mm_mul_ps(_mm_loadu_ps(box + ofs), pz));
		v1 = _mm_add_ps(v1, _mm_mul_ps(_mm_loadu_ps(box + ofs + 4), pz));

		__m128 pd = _mm_shuffle_ps(vplane, vplane, _MM_SHUFFLE(3, 3, 3, 3));
		activelanes &= _mm_movemask_ps(_mm_cmplt_ps(pd, v0)) | (_mm_movemask_ps(_mm_cmplt_ps(pd, v1)) << 4);
	}

	return activelanes;
}
#endif // defined(USE_SSE2)

#if defined(USE_SIMD)
/*
===============
R_MarkVisSurfacesSIMD
===============
*/
void R_MarkVisSurfacesSIMD (byte *vis)
{
	msurface_t		*surf;
	unsigned int	i, j, k;
	unsigned int	numleafs = cl.worldmodel->numleafs;
	unsigned int	numsurfaces = cl.worldmodel->numsurfaces;
	byte			*surfvis = cl.worldmodel->surfvis;
	soa_aabb_t		*leafbounds = cl.worldmodel->soa_leafbounds;

	memset(cl.worldmodel->surfvis, 0, (cl.worldmodel->numsurfaces + 7) / 8);

	// iterate through leaves, marking surfaces
	for (i = 0; i < numleafs; i += 8)
	{
		byte mask = vis[i / 8];
		if (mask == 0)
			continue;

		mask = R_CullBoxSIMD(leafbounds[i / 8], mask);
		if (mask == 0)
			continue;

		for (j = 0; (j < 8) && ((i + j) < numleafs); ++j)
		{
			if (!(mask & (1u << j)))
				continue;

			mleaf_t *leaf = &cl.worldmodel->leafs[1 + i + j];
			if (leaf->contents != CONTENTS_SKY || r_oldskyleaf.value)
			{
				unsigned int nummarksurfaces = leaf->nummarksurfaces;
				int *marksurfaces = leaf->firstmarksurface;
				for (k = 0; k < nummarksurfaces; ++k)
				{
					unsigned int index = marksurfaces[k];
					surfvis[index / 8] |= 1u << (index % 8);
				}
			}

			// add static models
			if (leaf->efrags)
				R_StoreEfrags (&leaf->efrags);
		}
	}

	for (i = 0; i < numsurfaces; i += 8)
	{
		byte mask = surfvis[i / 8];
		if (mask == 0)
			continue;

		mask &= R_BackFaceCullSIMD(cl.worldmodel->soa_surfplanes[i / 8]);
		if (mask == 0)
			continue;

		for (j = 0; j < 8; ++j)
		{
			if (!(mask & (1u << j)))
				continue;

			surf = &cl.worldmodel->surfaces[i + j];
			rs_brushpolys++; //count wpolys here
			R_ChainSurface(surf, chain_world);
			R_RenderDynamicLightmaps(surf);
			if (surf->texinfo->texture->warpimage)
				surf->texinfo->texture->update_warp = true;
		}
	}
}
#endif // defined(USE_SIMD)

/*
===============
R_MarkVisSurfaces
===============
*/
void R_MarkVisSurfaces (byte* vis)
{
	int			i, j;
	msurface_t	*surf;
	mleaf_t		*leaf;

	leaf = &cl.worldmodel->leafs[1];
	for (i=0 ; i<cl.worldmodel->numleafs ; i++, leaf++)
	{
		if (vis[i>>3] & (1<<(i&7)))
		{
			if (R_CullBox(leaf->minmaxs, leaf->minmaxs + 3))
				continue;

			if (r_oldskyleaf.value || leaf->contents != CONTENTS_SKY)
			{
				for (j=0; j<leaf->nummarksurfaces; j++)
				{
					surf = &cl.worldmodel->surfaces[leaf->firstmarksurface[j]];
					if (surf->visframe != r_visframecount)
					{
						surf->visframe = r_visframecount;
						if (!R_BackFaceCull (surf))
						{
							rs_brushpolys++; //count wpolys here
							R_ChainSurface(surf, chain_world);
							R_RenderDynamicLightmaps(surf);
							if (surf->texinfo->texture->warpimage)
								surf->texinfo->texture->update_warp = true;
						}
					}
				}
			}

			// add static models
			if (leaf->efrags)
				R_StoreEfrags (&leaf->efrags);
		}
	}
}

/*
===============
R_MarkSurfaces -- johnfitz -- mark surfaces based on PVS and rebuild texture chains
===============
*/
void R_MarkSurfaces (void)
{
	byte		*vis;
	int			i;
	qboolean	nearwaterportal;

	// check this leaf for water portals
	// TODO: loop through all water surfs and use distance to leaf cullbox
	nearwaterportal = false;
	for (i=0; i < r_viewleaf->nummarksurfaces; i++)
		if (cl.worldmodel->surfaces[r_viewleaf->firstmarksurface[i]].flags & SURF_DRAWTURB)
			nearwaterportal = true;

	// choose vis data
	if (r_novis.value || r_viewleaf->contents == CONTENTS_SOLID || r_viewleaf->contents == CONTENTS_SKY)
		vis = Mod_NoVisPVS (cl.worldmodel);
	else if (nearwaterportal)
		vis = SV_FatPVS (r_origin, cl.worldmodel);
	else
		vis = Mod_LeafPVS (r_viewleaf, cl.worldmodel);

	r_visframecount++;

	// set all chains to null
	for (i=0 ; i<cl.worldmodel->numtextures ; i++)
		if (cl.worldmodel->textures[i])
			cl.worldmodel->textures[i]->texturechains[chain_world] = NULL;

	// iterate through leaves, marking surfaces
#if defined(USE_SIMD)
	if (use_simd)
		R_MarkVisSurfacesSIMD(vis);
	else
#endif
	  R_MarkVisSurfaces(vis);
}

//==============================================================================
//
// VBO SUPPORT
//
//==============================================================================

static unsigned int R_NumTriangleIndicesForSurf (msurface_t *s)
{
	return 3 * (s->numedges - 2);
}

/*
================
R_TriangleIndicesForSurf

Writes out the triangle indices needed to draw s as a triangle list.
The number of indices it will write is given by R_NumTriangleIndicesForSurf.
================
*/
static void R_TriangleIndicesForSurf (msurface_t *s, uint32_t *dest)
{
	int i;
	for (i=2; i<s->numedges; i++)
	{
		*dest++ = s->vbo_firstvert;
		*dest++ = s->vbo_firstvert + i - 1;
		*dest++ = s->vbo_firstvert + i;
	}
}

#define MAX_BATCH_SIZE 65536

static uint32_t vbo_indices[MAX_BATCH_SIZE];
static unsigned int num_vbo_indices;

/*
================
R_ClearBatch
================
*/
static void R_ClearBatch ()
{
	num_vbo_indices = 0;
}

/*
================
R_FlushBatch

Draw the current batch if non-empty and clears it, ready for more R_BatchSurface calls.
================
*/
static void R_FlushBatch (qboolean fullbright_enabled, qboolean alpha_test, qboolean alpha_blend, qboolean use_zbias, gltexture_t * lightmap_texture)
{
	if (num_vbo_indices > 0)
	{
		int pipeline_index = (fullbright_enabled ? 1 : 0) + (alpha_test ? 2 : 0) + (alpha_blend ? 4 : 0);
		R_BindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_globals.world_pipelines[pipeline_index]);

		float constant_factor = 0.0f, slope_factor = 0.0f;
		if (use_zbias)
		{
			if (vulkan_globals.depth_format == VK_FORMAT_D32_SFLOAT_S8_UINT || vulkan_globals.depth_format == VK_FORMAT_D32_SFLOAT)
			{
				constant_factor = -4.f;
				slope_factor = -0.125f;
			}
			else
			{
				constant_factor = -1.f;
				slope_factor = -0.25f;
			}
		}
		vkCmdSetDepthBias(vulkan_globals.command_buffer, constant_factor, 0.0f, slope_factor);

		if (!r_fullbright_cheatsafe)
			vulkan_globals.vk_cmd_bind_descriptor_sets(vulkan_globals.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_globals.world_pipeline_layout.handle, 1, 1, &lightmap_texture->descriptor_set, 0, NULL);
		else
			vulkan_globals.vk_cmd_bind_descriptor_sets(vulkan_globals.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_globals.world_pipeline_layout.handle, 1, 1, &greytexture->descriptor_set, 0, NULL);

		VkBuffer buffer;
		VkDeviceSize buffer_offset;
		byte * indices = R_IndexAllocate(num_vbo_indices * sizeof(uint32_t), &buffer, &buffer_offset);
		memcpy(indices, vbo_indices, num_vbo_indices * sizeof(uint32_t));

		vulkan_globals.vk_cmd_bind_index_buffer(vulkan_globals.command_buffer, buffer, buffer_offset, VK_INDEX_TYPE_UINT32);
		vulkan_globals.vk_cmd_draw_indexed(vulkan_globals.command_buffer, num_vbo_indices, 1, 0, 0, 0);

		num_vbo_indices = 0;
	}
}

/*
================
R_BatchSurface

Add the surface to the current batch, or just draw it immediately if we're not
using VBOs.
================
*/
static void R_BatchSurface (msurface_t *s, qboolean fullbright_enabled, qboolean alpha_test, qboolean alpha_blend, qboolean use_zbias, gltexture_t * lightmap_texture)
{
	int num_surf_indices;

	num_surf_indices = R_NumTriangleIndicesForSurf (s);

	if (num_vbo_indices + num_surf_indices > MAX_BATCH_SIZE)
		R_FlushBatch(fullbright_enabled, alpha_test, alpha_blend, use_zbias, lightmap_texture);

	R_TriangleIndicesForSurf (s, &vbo_indices[num_vbo_indices]);
	num_vbo_indices += num_surf_indices;
}

/*
================
GL_WaterAlphaForEntitySurface -- ericw
 
Returns the water alpha to use for the entity and surface combination.
================
*/
float GL_WaterAlphaForEntitySurface (entity_t *ent, msurface_t *s)
{
	float entalpha;
	if (r_lightmap_cheatsafe)
		entalpha = 1;
	else if (ent == NULL || ent->alpha == ENTALPHA_DEFAULT)
		entalpha = GL_WaterAlphaForSurface(s);
	else
		entalpha = ENTALPHA_DECODE(ent->alpha);
	return entalpha;
}

/*
================
R_DrawTextureChains_ShowTris -- johnfitz
================
*/
void R_DrawTextureChains_ShowTris(qmodel_t *model, texchain_t chain)
{
	int			i;
	msurface_t	*s;
	texture_t	*t;
	float color[] = { 1.0f, 1.0f, 1.0f };
	const float alpha = 1.0f;

	for (i = 0; i<model->numtextures; i++)
	{
		t = model->textures[i];
		if (!t)
			continue;

		for (s = t->texturechains[chain]; s; s = s->texturechain)
			DrawGLPoly(s->polys, color, alpha);
	}
}

/*
================
R_DrawTextureChains_Water -- johnfitz
================
*/
void R_DrawTextureChains_Water (qmodel_t *model, entity_t *ent, texchain_t chain)
{
	int			i;
	msurface_t	*s;
	texture_t	*t;
	qboolean	bound;
	float entalpha;

	float color[3] = { 1.0f, 1.0f, 1.0f };

	R_BindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_globals.water_pipeline);

	for (i=0 ; i<model->numtextures ; i++)
	{
		t = model->textures[i];
		if (!t || !t->texturechains[chain] || !(t->texturechains[chain]->flags & SURF_DRAWTURB))
			continue;
		bound = false;
		entalpha = 1.0f;
		for (s = t->texturechains[chain]; s; s = s->texturechain)
		{
			if (!bound) //only bind once we are sure we need this texture
			{
				entalpha = GL_WaterAlphaForEntitySurface (ent, s);
				if (entalpha < 1.0f)
					R_BindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_globals.water_blend_pipeline);
				else
					R_BindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_globals.water_pipeline);

				vulkan_globals.vk_cmd_bind_descriptor_sets(vulkan_globals.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_globals.basic_pipeline_layout.handle, 0, 1, &t->warpimage->descriptor_set, 0, NULL);

				if (model != cl.worldmodel)
				{
					// ericw -- this is copied from R_DrawSequentialPoly.
					// If the poly is not part of the world we have to
					// set this flag
					t->update_warp = true; // FIXME: one frame too late!
				}

				bound = true;
			}
			DrawGLPoly (s->polys, color, entalpha);
			rs_brushpasses++;
		}
	}
}

/*
================
R_DrawTextureChains_Multitexture
================
*/
void R_DrawTextureChains_Multitexture (qmodel_t *model, entity_t *ent, texchain_t chain, const float alpha)
{
	int			i;
	msurface_t	*s;
	texture_t	*t;
	qboolean	fullbright_enabled = false;
	qboolean	alpha_test = false;
	qboolean	alpha_blend = alpha < 1.0f;
	qboolean	use_zbias = (gl_zfix.value && model != cl.worldmodel);
	int		lastlightmap;
	int		ent_frame = ent != NULL ? ent->frame : 0;
	gltexture_t	*fullbright = NULL;

	VkDeviceSize offset = 0;
	vulkan_globals.vk_cmd_bind_vertex_buffers(vulkan_globals.command_buffer, 0, 1, &bmodel_vertex_buffer, &offset);

	vulkan_globals.vk_cmd_bind_descriptor_sets(vulkan_globals.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_globals.world_pipeline_layout.handle, 2, 1, &nulltexture->descriptor_set, 0, NULL);
	if (r_lightmap_cheatsafe)
		vulkan_globals.vk_cmd_bind_descriptor_sets(vulkan_globals.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_globals.world_pipeline_layout.handle, 0, 1, &greytexture->descriptor_set, 0, NULL);

	if (alpha_blend) {
		R_PushConstants(VK_SHADER_STAGE_ALL_GRAPHICS, 20 * sizeof(float), 1 * sizeof(float), &alpha);
	}

	for (i = 0; i<model->numtextures; ++i)
	{
		t = model->textures[i];

		if (!t || !t->texturechains[chain] || t->texturechains[chain]->flags & (SURF_DRAWTILED | SURF_NOTEXTURE))
			continue;

		if (gl_fullbrights.value && (fullbright = R_TextureAnimation(t, ent_frame)->fullbright) && !r_lightmap_cheatsafe)
		{
			fullbright_enabled = true;
			vulkan_globals.vk_cmd_bind_descriptor_sets(vulkan_globals.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_globals.world_pipeline_layout.handle, 2, 1, &fullbright->descriptor_set, 0, NULL);
		}
		else
			fullbright_enabled = false;

		gltexture_t * lightmap_texture = NULL;
		R_ClearBatch ();

		lastlightmap = -1; // avoid compiler warning
		alpha_test = (t->texturechains[chain]->flags & SURF_DRAWFENCE) != 0;
		
		texture_t * texture = R_TextureAnimation(t, ent_frame);
		gltexture_t * gl_texture = texture->gltexture;
		if (!r_lightmap_cheatsafe)
			vulkan_globals.vk_cmd_bind_descriptor_sets(vulkan_globals.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_globals.world_pipeline_layout.handle, 0, 1, &gl_texture->descriptor_set, 0, NULL);

		for (s = t->texturechains[chain]; s; s = s->texturechain)
		{
			if (s->lightmaptexturenum != lastlightmap)
			{
				R_FlushBatch (fullbright_enabled, alpha_test, alpha_blend, use_zbias, lightmap_texture);
				lightmap_texture = lightmaps[s->lightmaptexturenum].texture;
			}

			lastlightmap = s->lightmaptexturenum;
			R_BatchSurface (s, fullbright_enabled, alpha_test, alpha_blend, use_zbias, lightmap_texture);

			rs_brushpasses++;
		}

		R_FlushBatch (fullbright_enabled, alpha_test, alpha_blend, use_zbias, lightmap_texture);
	}
}

/*
=============
R_DrawWorld -- johnfitz -- rewritten
=============
*/
void R_DrawTextureChains (qmodel_t *model, entity_t *ent, texchain_t chain)
{
	float entalpha;
	
	if (ent != NULL)
		entalpha = ENTALPHA_DECODE(ent->alpha);
	else
		entalpha = 1;

	R_UploadLightmaps ();
	R_DrawTextureChains_Multitexture (model, ent, chain, entalpha);
}

/*
=============
R_DrawWorld -- ericw -- moved from R_DrawTextureChains, which is no longer specific to the world.
=============
*/
void R_DrawWorld (void)
{
	if (!r_drawworld_cheatsafe)
		return;

	R_BeginDebugUtilsLabel ("World");
	R_DrawTextureChains (cl.worldmodel, NULL, chain_world);
	R_EndDebugUtilsLabel ();
}

/*
=============
R_DrawWorld_Water -- ericw -- moved from R_DrawTextureChains_Water, which is no longer specific to the world.
=============
*/
void R_DrawWorld_Water (void)
{
	if (!r_drawworld_cheatsafe)
		return;

	R_BeginDebugUtilsLabel ("Water");
	R_DrawTextureChains_Water (cl.worldmodel, NULL, chain_world);
	R_EndDebugUtilsLabel ();
}

/*
=============
R_DrawWorld_ShowTris -- ericw -- moved from R_DrawTextureChains_ShowTris, which is no longer specific to the world.
=============
*/
void R_DrawWorld_ShowTris (void)
{
	if (r_showtris.value == 1)
		R_BindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_globals.showtris_pipeline);
	else
		R_BindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_globals.showtris_depth_test_pipeline);

	vkCmdBindIndexBuffer(vulkan_globals.command_buffer, vulkan_globals.fan_index_buffer, 0, VK_INDEX_TYPE_UINT16);

	if (!r_drawworld_cheatsafe)
		return;

	R_DrawTextureChains_ShowTris (cl.worldmodel, chain_world);
}
