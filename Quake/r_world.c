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
#include "atomics.h"

extern cvar_t gl_fullbrights;
extern cvar_t r_drawflat;
extern cvar_t r_oldskyleaf;
extern cvar_t r_showtris;
extern cvar_t r_simd;
extern cvar_t gl_zfix;
extern cvar_t r_gpulightmapupdate;
extern cvar_t r_tasks;

cvar_t r_parallelmark = {"r_parallelmark", "1", CVAR_NONE};

byte *SV_FatPVS (vec3_t org, qmodel_t *worldmodel);

extern VkBuffer bmodel_vertex_buffer;
static int      world_texstart[NUM_WORLD_CBX];
static int      world_texend[NUM_WORLD_CBX];

/*
===============
mark_surfaces_state_t
===============
*/
typedef struct
{
	byte *vis;
} mark_surfaces_state_t;
mark_surfaces_state_t mark_surfaces_state;

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
	for (i = 0; i < mod->numtextures; i++)
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
	surf->texturechains[chain] = surf->texinfo->texture->texturechains[chain];
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

/*
===============
R_SetupWorldCBXTexRanges
===============
*/
void R_SetupWorldCBXTexRanges (qboolean use_tasks)
{
	const int num_textures = cl.worldmodel->numtextures;
	if (!use_tasks)
	{
		world_texstart[0] = 0;
		world_texend[0] = num_textures;
		return;
	}

	int       total_world_textures = 0;
	for (int i = 0; i < num_textures; ++i)
	{
		texture_t *t = cl.worldmodel->textures[i];
		if (!t || !t->texturechains[chain_world] || t->texturechains[chain_world]->flags & (SURF_DRAWTURB | SURF_DRAWTILED | SURF_NOTEXTURE))
			continue;
		total_world_textures += 1;
	}

	const int num_textures_per_cbx = (total_world_textures + NUM_WORLD_CBX - 1) / NUM_WORLD_CBX;
	memset(world_texstart, 0, sizeof(world_texstart));
	memset(world_texend, 0, sizeof(world_texend));
	int current_cbx = 0;
	int num_assigned_to_cbx = 0;
	for (int i = 0; i < num_textures; ++i)
	{
		texture_t *t = cl.worldmodel->textures[i];
		if (!t || !t->texturechains[chain_world] || t->texturechains[chain_world]->flags & (SURF_DRAWTURB | SURF_DRAWTILED | SURF_NOTEXTURE))
			continue;
		world_texend[current_cbx] = i + 1;
		num_assigned_to_cbx += 1;
		if (num_assigned_to_cbx == num_textures_per_cbx)
		{
			current_cbx += 1;
			if (current_cbx < NUM_WORLD_CBX)
			{
				world_texstart[current_cbx] = i + 1;
			}
			num_assigned_to_cbx = 0;
		}
	}
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
	__m128 pos = _mm_loadu_ps (r_refdef.vieworg);

	__m128 px = _mm_shuffle_ps (pos, pos, _MM_SHUFFLE (0, 0, 0, 0));
	__m128 v0 = _mm_mul_ps (_mm_loadu_ps (plane + 0), px);
	__m128 v1 = _mm_mul_ps (_mm_loadu_ps (plane + 4), px);

	__m128 py = _mm_shuffle_ps (pos, pos, _MM_SHUFFLE (1, 1, 1, 1));
	v0 = _mm_add_ps (v0, _mm_mul_ps (_mm_loadu_ps (plane + 8), py));
	v1 = _mm_add_ps (v1, _mm_mul_ps (_mm_loadu_ps (plane + 12), py));

	__m128 pz = _mm_shuffle_ps (pos, pos, _MM_SHUFFLE (2, 2, 2, 2));
	v0 = _mm_add_ps (v0, _mm_mul_ps (_mm_loadu_ps (plane + 16), pz));
	v1 = _mm_add_ps (v1, _mm_mul_ps (_mm_loadu_ps (plane + 20), pz));

	__m128 pd0 = _mm_loadu_ps (plane + 24);
	__m128 pd1 = _mm_loadu_ps (plane + 28);

	return _mm_movemask_ps (_mm_cmplt_ps (pd0, v0)) | (_mm_movemask_ps (_mm_cmplt_ps (pd1, v1)) << 4);
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
		byte      signbits;
		int       ofs;

		if (activelanes == 0)
			break;

		p = frustum + i;
		signbits = p->signbits;

		__m128 vplane = _mm_loadu_ps (p->normal);

		ofs = signbits & 1 ? 0 : 8; // x min/max
		__m128 px = _mm_shuffle_ps (vplane, vplane, _MM_SHUFFLE (0, 0, 0, 0));
		__m128 v0 = _mm_mul_ps (_mm_loadu_ps (box + ofs), px);
		__m128 v1 = _mm_mul_ps (_mm_loadu_ps (box + ofs + 4), px);

		ofs = signbits & 2 ? 16 : 24; // y min/max
		__m128 py = _mm_shuffle_ps (vplane, vplane, _MM_SHUFFLE (1, 1, 1, 1));
		v0 = _mm_add_ps (v0, _mm_mul_ps (_mm_loadu_ps (box + ofs), py));
		v1 = _mm_add_ps (v1, _mm_mul_ps (_mm_loadu_ps (box + ofs + 4), py));

		ofs = signbits & 4 ? 32 : 40; // z min/max
		__m128 pz = _mm_shuffle_ps (vplane, vplane, _MM_SHUFFLE (2, 2, 2, 2));
		v0 = _mm_add_ps (v0, _mm_mul_ps (_mm_loadu_ps (box + ofs), pz));
		v1 = _mm_add_ps (v1, _mm_mul_ps (_mm_loadu_ps (box + ofs + 4), pz));

		__m128 pd = _mm_shuffle_ps (vplane, vplane, _MM_SHUFFLE (3, 3, 3, 3));
		activelanes &= _mm_movemask_ps (_mm_cmplt_ps (pd, v0)) | (_mm_movemask_ps (_mm_cmplt_ps (pd, v1)) << 4);
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
void R_MarkVisSurfacesSIMD (qboolean *use_tasks)
{
	msurface_t  *surf;
	unsigned int i, k;
	unsigned int numleafs = cl.worldmodel->numleafs;
	unsigned int numsurfaces = cl.worldmodel->numsurfaces;
	byte        *surfvis = cl.worldmodel->surfvis;
	soa_aabb_t  *leafbounds = cl.worldmodel->soa_leafbounds;

	// iterate through leaves, marking surfaces
	for (i = 0; i < numleafs; i += 8)
	{
		byte mask = mark_surfaces_state.vis[i / 8];
		if (mask == 0)
			continue;

		mask = R_CullBoxSIMD (leafbounds[i / 8], mask);
		while (mask != 0)
		{
			const int j = FindFirstBitNonZero (mask);
			mask &= ~(1u << j);

			mleaf_t *leaf = &cl.worldmodel->leafs[1 + i + j];
			if (leaf->contents != CONTENTS_SKY || r_oldskyleaf.value)
			{
				unsigned int nummarksurfaces = leaf->nummarksurfaces;
				int         *marksurfaces = leaf->firstmarksurface;
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

		mask &= R_BackFaceCullSIMD (cl.worldmodel->soa_surfplanes[i / 8]);
		while (mask != 0)
		{
			const int j = FindFirstBitNonZero (mask);
			mask &= ~(1u << j);

			surf = &cl.worldmodel->surfaces[i + j];
			Atomic_IncrementUInt32 (&rs_brushpolys); // count wpolys here
			R_ChainSurface (surf, chain_world);
			if (!r_gpulightmapupdate.value)
				R_RenderDynamicLightmaps (surf);
			else if (surf->lightmaptexturenum >= 0)
				lightmaps[surf->lightmaptexturenum].modified = true;
			if (surf->texinfo->texture->warpimage)
				surf->texinfo->texture->update_warp = true;
		}
	}

	R_SetupWorldCBXTexRanges(*use_tasks);
}

/*
===============
R_MarkLeafsSIMD
===============
*/
void R_MarkLeafsSIMD (int index, void *unused)
{
	unsigned int    j;
	unsigned int    first_leaf = index * 8;
	atomic_uint8_t *surfvis = (atomic_uint8_t *)cl.worldmodel->surfvis;
	soa_aabb_t     *leafbounds = cl.worldmodel->soa_leafbounds;

	byte *mask = &mark_surfaces_state.vis[index];
	if (*mask == 0)
		return;

	*mask = R_CullBoxSIMD (leafbounds[index], *mask);

	uint8_t mask_iter = *mask;
	while (mask_iter != 0)
	{
		const int i = FindFirstBitNonZero (mask_iter);

		mleaf_t *leaf = &cl.worldmodel->leafs[1 + first_leaf + i];
		if (leaf->contents != CONTENTS_SKY || r_oldskyleaf.value)
		{
			unsigned int nummarksurfaces = leaf->nummarksurfaces;
			int         *marksurfaces = leaf->firstmarksurface;
			for (j = 0; j < nummarksurfaces; ++j)
			{
				unsigned int surf_index = marksurfaces[j];
				Atomic_OrUInt8 (&surfvis[surf_index / 8], 1u << (surf_index % 8));
			}
		}
		const uint8_t bit_mask = ~(1u << i);
		if (!leaf->efrags)
		{
			*mask &= bit_mask;
		}
		mask_iter &= bit_mask;
	}
}

/*
===============
R_BackfaceCullSurfacesSIMD
===============
*/
void R_BackfaceCullSurfacesSIMD (int index, void *unused)
{
	byte       *surfvis = cl.worldmodel->surfvis;
	msurface_t *surf;

	byte *mask = &surfvis[index];
	if (*mask == 0)
		return;

	*mask &= R_BackFaceCullSIMD (cl.worldmodel->soa_surfplanes[index]);

	uint8_t mask_iter = *mask;
	while (mask_iter != 0)
	{
		const int i = FindFirstBitNonZero (mask_iter);

		surf = &cl.worldmodel->surfaces[(index * 8) + i];
		if (surf->lightmaptexturenum >= 0)
			lightmaps[surf->lightmaptexturenum].modified = true;
		if (surf->texinfo->texture->warpimage)
			surf->texinfo->texture->update_warp = true;

		const uint8_t bit_mask = ~(1u << i);
		mask_iter &= bit_mask;
	}
}

/*
===============
R_StoreLeafEFrags
===============
*/
void R_StoreLeafEFrags (void *unused)
{
	unsigned int i;
	unsigned int numleafs = cl.worldmodel->numleafs;
	for (i = 0; i < numleafs; i += 8)
	{
		byte mask = mark_surfaces_state.vis[i / 8];
		while (mask != 0)
		{
			const int j = FindFirstBitNonZero (mask);
			mask &= ~(1u << j);
			mleaf_t *leaf = &cl.worldmodel->leafs[1 + i + j];
			R_StoreEfrags (&leaf->efrags);
		}
	}
}

/*
===============
R_ChainVisSurfaces
===============
*/
void R_ChainVisSurfaces (qboolean *use_tasks)
{
	unsigned int i;
	msurface_t  *surf;
	unsigned int numsurfaces = cl.worldmodel->numsurfaces;
	byte        *surfvis = cl.worldmodel->surfvis;
	for (i = 0; i < numsurfaces; i += 8)
	{
		byte mask = surfvis[i / 8];
		while (mask != 0)
		{
			const int j = FindFirstBitNonZero (mask);
			mask &= ~(1u << j);
			surf = &cl.worldmodel->surfaces[i + j];
			Atomic_IncrementUInt32 (&rs_brushpolys); // count wpolys here
			R_ChainSurface (surf, chain_world);
		}
	}

	R_SetupWorldCBXTexRanges(*use_tasks);
}
#endif // defined(USE_SIMD)

/*
===============
R_MarkVisSurfaces
===============
*/
void R_MarkVisSurfaces (qboolean *use_tasks)
{
	int         i, j;
	msurface_t *surf;
	mleaf_t    *leaf;

	leaf = &cl.worldmodel->leafs[1];
	for (i = 0; i < cl.worldmodel->numleafs; i++, leaf++)
	{
		if (mark_surfaces_state.vis[i >> 3] & (1 << (i & 7)))
		{
			if (R_CullBox (leaf->minmaxs, leaf->minmaxs + 3))
				continue;

			if (r_oldskyleaf.value || leaf->contents != CONTENTS_SKY)
			{
				for (j = 0; j < leaf->nummarksurfaces; j++)
				{
					surf = &cl.worldmodel->surfaces[leaf->firstmarksurface[j]];
					if (surf->visframe != r_visframecount)
					{
						surf->visframe = r_visframecount;
						if (!R_BackFaceCull (surf))
						{
							Atomic_IncrementUInt32 (&rs_brushpolys); // count wpolys here
							R_ChainSurface (surf, chain_world);
							if (!r_gpulightmapupdate.value)
								R_RenderDynamicLightmaps (surf);
							else if (surf->lightmaptexturenum >= 0)
								lightmaps[surf->lightmaptexturenum].modified = true;
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

	R_SetupWorldCBXTexRanges(*use_tasks);
}

/*
===============
R_MarkSurfacesPrepare
===============
*/
static void R_MarkSurfacesPrepare (void *unused)
{
	int      i;
	qboolean nearwaterportal;
	int      maxleafs = cl.worldmodel->numleafs;

	// check this leaf for water portals
	// TODO: loop through all water surfs and use distance to leaf cullbox
	nearwaterportal = false;
	for (i = 0; i < r_viewleaf->nummarksurfaces; i++)
		if (cl.worldmodel->surfaces[r_viewleaf->firstmarksurface[i]].flags & SURF_DRAWTURB)
			nearwaterportal = true;

	// choose vis data
	if (r_novis.value || r_viewleaf->contents == CONTENTS_SOLID || r_viewleaf->contents == CONTENTS_SKY)
		mark_surfaces_state.vis = Mod_NoVisPVS (cl.worldmodel);
	else if (nearwaterportal)
		mark_surfaces_state.vis = SV_FatPVS (r_origin, cl.worldmodel);
	else
		mark_surfaces_state.vis = Mod_LeafPVS (r_viewleaf, cl.worldmodel);

	if (maxleafs & 7)
		mark_surfaces_state.vis[maxleafs >> 3] &= (1 << (maxleafs & 7)) - 1;

	r_visframecount++;

	// set all chains to null
	for (i = 0; i < cl.worldmodel->numtextures; i++)
		if (cl.worldmodel->textures[i])
			cl.worldmodel->textures[i]->texturechains[chain_world] = NULL;

	memset (cl.worldmodel->surfvis, 0, (cl.worldmodel->numsurfaces + 7) / 8);
}

/*
===============
R_MarkSurfaces -- johnfitz -- mark surfaces based on PVS and rebuild texture chains
===============
*/
void R_MarkSurfaces (qboolean use_tasks, task_handle_t before_mark, task_handle_t *store_efrags, task_handle_t *cull_surfaces, task_handle_t *chain_surfaces)
{
	if (use_tasks)
	{
		task_handle_t prepare_mark = Task_AllocateAndAssignFunc (R_MarkSurfacesPrepare, NULL, 0);
		Task_AddDependency (before_mark, prepare_mark);
		Task_Submit (prepare_mark);
#if defined(USE_SIMD)
		if (use_simd)
		{
			if (r_parallelmark.value)
			{
				unsigned int  numleafs = cl.worldmodel->numleafs;
				task_handle_t mark_surfaces = Task_AllocateAndAssignIndexedFunc (R_MarkLeafsSIMD, (numleafs + 7) / 8, NULL, 0);
				Task_AddDependency (prepare_mark, mark_surfaces);
				Task_Submit (mark_surfaces);

				*store_efrags = Task_AllocateAndAssignFunc (R_StoreLeafEFrags, NULL, 0);
				Task_AddDependency (mark_surfaces, *store_efrags);

				unsigned int numsurfaces = cl.worldmodel->numsurfaces;
				*cull_surfaces = Task_AllocateAndAssignIndexedFunc (R_BackfaceCullSurfacesSIMD, (numsurfaces + 7) / 8, NULL, 0);
				Task_AddDependency (mark_surfaces, *cull_surfaces);

				*chain_surfaces = Task_AllocateAndAssignFunc ((task_func_t)R_ChainVisSurfaces, &use_tasks, sizeof(qboolean));
				Task_AddDependency (*cull_surfaces, *chain_surfaces);
			}
			else
			{
				task_handle_t mark_surfaces = Task_AllocateAndAssignFunc ((task_func_t)R_MarkVisSurfacesSIMD, &use_tasks, sizeof(qboolean));
				Task_AddDependency (prepare_mark, mark_surfaces);
				*store_efrags = mark_surfaces;
				*chain_surfaces = mark_surfaces;
				*cull_surfaces = mark_surfaces;
			}
		}
		else
#endif
		{
			task_handle_t mark_surfaces = Task_AllocateAndAssignFunc ((task_func_t)R_MarkVisSurfaces, &use_tasks, sizeof(qboolean));
			Task_AddDependency (prepare_mark, mark_surfaces);
			*store_efrags = mark_surfaces;
			*chain_surfaces = mark_surfaces;
			*cull_surfaces = mark_surfaces;
		}
	}
	else
	{
		R_MarkSurfacesPrepare (NULL);
		// iterate through leaves, marking surfaces
#if defined(USE_SIMD)
		if (use_simd)
		{
			R_MarkVisSurfacesSIMD (&use_tasks);
		}
		else
#endif
			R_MarkVisSurfaces (&use_tasks);
	}
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
	for (i = 2; i < s->numedges; i++)
	{
		*dest++ = s->vbo_firstvert;
		*dest++ = s->vbo_firstvert + i - 1;
		*dest++ = s->vbo_firstvert + i;
	}
}

/*
================
R_ClearBatch
================
*/
static void R_ClearBatch (cb_context_t *cbx)
{
	cbx->num_vbo_indices = 0;
}

/*
================
R_FlushBatch

Draw the current batch if non-empty and clears it, ready for more R_BatchSurface calls.
================
*/
static void
R_FlushBatch (cb_context_t *cbx, qboolean fullbright_enabled, qboolean alpha_test, qboolean alpha_blend, qboolean use_zbias, gltexture_t *lightmap_texture)
{
	if (cbx->num_vbo_indices > 0)
	{
		int pipeline_index = (fullbright_enabled ? 1 : 0) + (alpha_test ? 2 : 0) + (alpha_blend ? 4 : 0);
		R_BindPipeline (cbx, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_globals.world_pipelines[pipeline_index]);

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
		vkCmdSetDepthBias (cbx->cb, constant_factor, 0.0f, slope_factor);

		if (!r_fullbright_cheatsafe)
			vulkan_globals.vk_cmd_bind_descriptor_sets (
				cbx->cb, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_globals.world_pipeline_layout.handle, 1, 1, &lightmap_texture->descriptor_set, 0, NULL);
		else
			vulkan_globals.vk_cmd_bind_descriptor_sets (
				cbx->cb, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_globals.world_pipeline_layout.handle, 1, 1, &greytexture->descriptor_set, 0, NULL);

		VkBuffer     buffer;
		VkDeviceSize buffer_offset;
		byte        *indices = R_IndexAllocate (cbx->num_vbo_indices * sizeof (uint32_t), &buffer, &buffer_offset);
		memcpy (indices, cbx->vbo_indices, cbx->num_vbo_indices * sizeof (uint32_t));

		vulkan_globals.vk_cmd_bind_index_buffer (cbx->cb, buffer, buffer_offset, VK_INDEX_TYPE_UINT32);
		vulkan_globals.vk_cmd_draw_indexed (cbx->cb, cbx->num_vbo_indices, 1, 0, 0, 0);

		cbx->num_vbo_indices = 0;
	}
}

/*
================
R_BatchSurface

Add the surface to the current batch, or just draw it immediately if we're not
using VBOs.
================
*/
static void R_BatchSurface (
	cb_context_t *cbx, msurface_t *s, qboolean fullbright_enabled, qboolean alpha_test, qboolean alpha_blend, qboolean use_zbias, gltexture_t *lightmap_texture)
{
	int num_surf_indices;

	num_surf_indices = R_NumTriangleIndicesForSurf (s);

	if (cbx->num_vbo_indices + num_surf_indices > MAX_BATCH_SIZE)
		R_FlushBatch (cbx, fullbright_enabled, alpha_test, alpha_blend, use_zbias, lightmap_texture);

	R_TriangleIndicesForSurf (s, &cbx->vbo_indices[cbx->num_vbo_indices]);
	cbx->num_vbo_indices += num_surf_indices;
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
		entalpha = GL_WaterAlphaForSurface (s);
	else
		entalpha = ENTALPHA_DECODE (ent->alpha);
	return entalpha;
}

/*
================
R_DrawTextureChains_ShowTris -- johnfitz
================
*/
void R_DrawTextureChains_ShowTris (cb_context_t *cbx, qmodel_t *model, texchain_t chain)
{
	int         i;
	msurface_t *s;
	texture_t  *t;
	float       color[] = {1.0f, 1.0f, 1.0f};
	const float alpha = 1.0f;

	for (i = 0; i < model->numtextures; i++)
	{
		t = model->textures[i];
		if (!t)
			continue;

		for (s = t->texturechains[chain]; s; s = s->texturechains[chain])
			DrawGLPoly (cbx, s->polys, color, alpha);
	}
}

/*
================
R_DrawTextureChains_Water -- johnfitz
================
*/
void R_DrawTextureChains_Water (cb_context_t *cbx, qmodel_t *model, entity_t *ent, texchain_t chain)
{
	int         i;
	msurface_t *s;
	texture_t  *t;

	VkDeviceSize offset = 0;
	vulkan_globals.vk_cmd_bind_vertex_buffers (cbx->cb, 0, 1, &bmodel_vertex_buffer, &offset);

	vulkan_globals.vk_cmd_bind_descriptor_sets (
		cbx->cb, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_globals.world_pipeline_layout.handle, 2, 1, &nulltexture->descriptor_set, 0, NULL);
	if (r_lightmap_cheatsafe)
		vulkan_globals.vk_cmd_bind_descriptor_sets (
			cbx->cb, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_globals.world_pipeline_layout.handle, 0, 1, &greytexture->descriptor_set, 0, NULL);

	for (i = 0; i < model->numtextures; ++i)
	{
		t = model->textures[i];

		if (!t || !t->texturechains[chain] || !(t->texturechains[chain]->flags & SURF_DRAWTURB))
			continue;

		gltexture_t *lightmap_texture = NULL;
		R_ClearBatch (cbx);

		int   lastlightmap = -2; // avoid compiler warning
		float last_alpha = 0.0f;
		float alpha = 0.0f;

		gltexture_t *gl_texture = t->warpimage;
		if (!r_lightmap_cheatsafe)
			vulkan_globals.vk_cmd_bind_descriptor_sets (
				cbx->cb, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_globals.world_pipeline_layout.handle, 0, 1, &gl_texture->descriptor_set, 0, NULL);

		for (s = t->texturechains[chain]; s; s = s->texturechains[chain])
		{
			if (model != cl.worldmodel)
			{
				// ericw -- this is copied from R_DrawSequentialPoly.
				// If the poly is not part of the world we have to
				// set this flag
				t->update_warp = true; // FIXME: one frame too late!
			}

			alpha = GL_WaterAlphaForEntitySurface (ent, s);
			const qboolean alpha_blend = alpha < 1.0f;

			if ((s->lightmaptexturenum != lastlightmap) || (last_alpha != alpha))
			{
				if (alpha_blend)
					R_PushConstants (cbx, VK_SHADER_STAGE_ALL_GRAPHICS, 20 * sizeof (float), 1 * sizeof (float), &alpha);
				R_FlushBatch (cbx, false, false, alpha_blend, false, lightmap_texture);
				lightmap_texture = (s->lightmaptexturenum >= 0) ? lightmaps[s->lightmaptexturenum].texture : greytexture;
				last_alpha = alpha;
			}

			lastlightmap = s->lightmaptexturenum;
			R_BatchSurface (cbx, s, false, false, alpha_blend, false, lightmap_texture);

			Atomic_IncrementUInt32 (&rs_brushpasses);
		}

		const qboolean alpha_blend = alpha < 1.0f;
		if (alpha_blend)
			R_PushConstants (cbx, VK_SHADER_STAGE_ALL_GRAPHICS, 20 * sizeof (float), 1 * sizeof (float), &alpha);
		R_FlushBatch (cbx, false, false, alpha_blend, false, lightmap_texture);
	}
}

/*
================
R_DrawTextureChains_Multitexture
================
*/
void R_DrawTextureChains_Multitexture (cb_context_t *cbx, qmodel_t *model, entity_t *ent, texchain_t chain, const float alpha, int texstart, int texend)
{
	int          i;
	msurface_t  *s;
	texture_t   *t;
	qboolean     fullbright_enabled = false;
	qboolean     alpha_test = false;
	qboolean     alpha_blend = alpha < 1.0f;
	qboolean     use_zbias = (gl_zfix.value && model != cl.worldmodel);
	int          lastlightmap;
	int          ent_frame = ent != NULL ? ent->frame : 0;
	gltexture_t *fullbright = NULL;

	VkDeviceSize offset = 0;
	vulkan_globals.vk_cmd_bind_vertex_buffers (cbx->cb, 0, 1, &bmodel_vertex_buffer, &offset);

	vulkan_globals.vk_cmd_bind_descriptor_sets (
		cbx->cb, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_globals.world_pipeline_layout.handle, 2, 1, &nulltexture->descriptor_set, 0, NULL);
	if (r_lightmap_cheatsafe)
		vulkan_globals.vk_cmd_bind_descriptor_sets (
			cbx->cb, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_globals.world_pipeline_layout.handle, 0, 1, &greytexture->descriptor_set, 0, NULL);

	if (alpha_blend)
	{
		R_PushConstants (cbx, VK_SHADER_STAGE_ALL_GRAPHICS, 20 * sizeof (float), 1 * sizeof (float), &alpha);
	}

	uint32_t brushpasses = 0;
	for (i = texstart; i < texend; ++i)
	{
		t = model->textures[i];

		if (!t || !t->texturechains[chain] || t->texturechains[chain]->flags & (SURF_DRAWTURB | SURF_DRAWTILED | SURF_NOTEXTURE))
			continue;

		if (gl_fullbrights.value && (fullbright = R_TextureAnimation (t, ent_frame)->fullbright) && !r_lightmap_cheatsafe)
		{
			fullbright_enabled = true;
			vulkan_globals.vk_cmd_bind_descriptor_sets (
				cbx->cb, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_globals.world_pipeline_layout.handle, 2, 1, &fullbright->descriptor_set, 0, NULL);
		}
		else
			fullbright_enabled = false;

		gltexture_t *lightmap_texture = NULL;
		R_ClearBatch (cbx);

		lastlightmap = -1; // avoid compiler warning
		alpha_test = (t->texturechains[chain]->flags & SURF_DRAWFENCE) != 0;

		texture_t   *texture = R_TextureAnimation (t, ent_frame);
		gltexture_t *gl_texture = texture->gltexture;
		if (!r_lightmap_cheatsafe)
			vulkan_globals.vk_cmd_bind_descriptor_sets (
				cbx->cb, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_globals.world_pipeline_layout.handle, 0, 1, &gl_texture->descriptor_set, 0, NULL);

		for (s = t->texturechains[chain]; s; s = s->texturechains[chain])
		{
			if (s->lightmaptexturenum != lastlightmap)
			{
				R_FlushBatch (cbx, fullbright_enabled, alpha_test, alpha_blend, use_zbias, lightmap_texture);
				lightmap_texture = lightmaps[s->lightmaptexturenum].texture;
			}

			lastlightmap = s->lightmaptexturenum;
			R_BatchSurface (cbx, s, fullbright_enabled, alpha_test, alpha_blend, use_zbias, lightmap_texture);

			brushpasses += 1;
		}

		R_FlushBatch (cbx, fullbright_enabled, alpha_test, alpha_blend, use_zbias, lightmap_texture);
	}

	Atomic_AddUInt32 (&rs_brushpasses, brushpasses);
}

/*
=============
R_DrawWorld -- johnfitz -- rewritten
=============
*/
void R_DrawTextureChains (cb_context_t *cbx, qmodel_t *model, entity_t *ent, texchain_t chain)
{
	float entalpha;

	if (ent != NULL)
		entalpha = ENTALPHA_DECODE (ent->alpha);
	else
		entalpha = 1;

	if (!r_gpulightmapupdate.value)
		R_UploadLightmaps ();
	R_DrawTextureChains_Multitexture (cbx, model, ent, chain, entalpha, 0, model->numtextures);
}

/*
=============
R_DrawWorld -- ericw -- moved from R_DrawTextureChains, which is no longer specific to the world.
=============
*/
void R_DrawWorld (cb_context_t *cbx, int index)
{
	if (!r_drawworld_cheatsafe)
		return;

	R_BeginDebugUtilsLabel (cbx, "World");
	if (!r_gpulightmapupdate.value)
		R_UploadLightmaps ();
	R_DrawTextureChains_Multitexture (cbx, cl.worldmodel, NULL, chain_world, 1, world_texstart[index], world_texend[index]);
	R_EndDebugUtilsLabel (cbx);
}

/*
=============
R_DrawWorld_Water -- ericw -- moved from R_DrawTextureChains_Water, which is no longer specific to the world.
=============
*/
void R_DrawWorld_Water (cb_context_t *cbx)
{
	if (!r_drawworld_cheatsafe)
		return;

	R_BeginDebugUtilsLabel (cbx, "Water");
	R_DrawTextureChains_Water (cbx, cl.worldmodel, NULL, chain_world);
	R_EndDebugUtilsLabel (cbx);
}

/*
=============
R_DrawWorld_ShowTris -- ericw -- moved from R_DrawTextureChains_ShowTris, which is no longer specific to the world.
=============
*/
void R_DrawWorld_ShowTris (cb_context_t *cbx)
{
	if (r_showtris.value == 1)
		R_BindPipeline (cbx, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_globals.showtris_pipeline);
	else
		R_BindPipeline (cbx, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_globals.showtris_depth_test_pipeline);

	vkCmdBindIndexBuffer (cbx->cb, vulkan_globals.fan_index_buffer, 0, VK_INDEX_TYPE_UINT16);

	if (!r_drawworld_cheatsafe)
		return;

	R_DrawTextureChains_ShowTris (cbx, cl.worldmodel, chain_world);
}
