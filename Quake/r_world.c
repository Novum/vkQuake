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
extern cvar_t vid_filter;
extern cvar_t vid_palettize;

cvar_t r_parallelmark = {"r_parallelmark", "1", CVAR_NONE};

byte *SV_FatPVS (vec3_t org, qmodel_t *worldmodel);

extern VkBuffer bmodel_vertex_buffer;
static int		world_texstart[NUM_WORLD_CBX];
static int		world_texend[NUM_WORLD_CBX];

/*
===============
mark_surfaces_state_t
===============
*/
typedef struct
{
#if defined(USE_SIMD)
#if defined(USE_SSE2)
	__m128 frustum_px[4];
	__m128 frustum_py[4];
	__m128 frustum_pz[4];
	__m128 frustum_pd[4];
	__m128 vieworg_px;
	__m128 vieworg_py;
	__m128 vieworg_pz;
#elif defined(USE_NEON)
	float32x4_t frustum_px[4];
	float32x4_t frustum_py[4];
	float32x4_t frustum_pz[4];
	float32x4_t frustum_pd[4];
	float32x4_t vieworg_px;
	float32x4_t vieworg_py;
	float32x4_t vieworg_pz;
#endif
	int frustum_ofsx[4];
	int frustum_ofsy[4];
	int frustum_ofsz[4];
#endif
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
	{
		if (mod->textures[i])
		{
			mod->textures[i]->texturechains[chain] = NULL;
			mod->textures[i]->chain_size[chain] = 0;
		}
	}
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
	surf->texinfo->texture->chain_size[chain] += 1;
}

/*
================
R_BackFaceCull -- johnfitz -- returns true if the surface is facing away from vieworg
================
*/
static inline qboolean R_BackFaceCull (msurface_t *surf)
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
	memset (world_texstart, 0, sizeof (world_texstart));
	memset (world_texend, 0, sizeof (world_texend));

	const int num_textures = cl.worldmodel->numtextures;
	if (!use_tasks)
	{
		world_texstart[0] = 0;
		world_texend[0] = num_textures;
		return;
	}

	int total_world_surfs = 0;
	for (int i = 0; i < num_textures; ++i)
	{
		texture_t *t = cl.worldmodel->textures[i];
		if (!t || !t->texturechains[chain_world] || t->texturechains[chain_world]->flags & (SURF_DRAWTURB | SURF_DRAWTILED))
			continue;
		total_world_surfs += t->chain_size[chain_world];
	}

	const int num_surfs_per_cbx = (total_world_surfs + NUM_WORLD_CBX - 1) / NUM_WORLD_CBX;
	int		  current_cbx = 0;
	int		  num_assigned_to_cbx = 0;
	for (int i = 0; i < num_textures; ++i)
	{
		texture_t *t = cl.worldmodel->textures[i];
		if (!t || !t->texturechains[chain_world] || t->texturechains[chain_world]->flags & (SURF_DRAWTURB | SURF_DRAWTILED))
			continue;
		assert (current_cbx < NUM_WORLD_CBX);
		world_texend[current_cbx] = i + 1;
		num_assigned_to_cbx += t->chain_size[chain_world];
		if (num_assigned_to_cbx >= num_surfs_per_cbx)
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

#if defined(USE_SSE2)
/*
===============
R_BackFaceCullSIMD

Performs backface culling for 32 planes
===============
*/
static FORCE_INLINE uint32_t R_BackFaceCullSIMD (soa_plane_t *planes)
{
	__m128 px = mark_surfaces_state.vieworg_px;
	__m128 py = mark_surfaces_state.vieworg_py;
	__m128 pz = mark_surfaces_state.vieworg_pz;

	uint32_t activelanes = 0;
	for (int plane_index = 0; plane_index < 4; ++plane_index)
	{
		soa_plane_t *plane = planes + plane_index;

		__m128 v0 = _mm_mul_ps (_mm_loadu_ps ((*plane) + 0), px);
		__m128 v1 = _mm_mul_ps (_mm_loadu_ps ((*plane) + 4), px);

		v0 = _mm_add_ps (v0, _mm_mul_ps (_mm_loadu_ps ((*plane) + 8), py));
		v1 = _mm_add_ps (v1, _mm_mul_ps (_mm_loadu_ps ((*plane) + 12), py));

		v0 = _mm_add_ps (v0, _mm_mul_ps (_mm_loadu_ps ((*plane) + 16), pz));
		v1 = _mm_add_ps (v1, _mm_mul_ps (_mm_loadu_ps ((*plane) + 20), pz));

		__m128 pd0 = _mm_loadu_ps ((*plane) + 24);
		__m128 pd1 = _mm_loadu_ps ((*plane) + 28);

		uint32_t plane_lanes = (uint32_t)(_mm_movemask_ps (_mm_cmplt_ps (pd0, v0)) | (_mm_movemask_ps (_mm_cmplt_ps (pd1, v1)) << 4));
		activelanes |= plane_lanes << (plane_index * 8);
	}
	return activelanes;
}

/*
===============
R_CullBoxSIMD

Performs frustum culling for 32 bounding boxes
===============
*/
static FORCE_INLINE uint32_t R_CullBoxSIMD (soa_aabb_t *boxes, uint32_t activelanes)
{
	for (int frustum_index = 0; frustum_index < 4; ++frustum_index)
	{
		if (activelanes == 0)
			break;

		int	   ofsx = mark_surfaces_state.frustum_ofsx[frustum_index];
		int	   ofsy = mark_surfaces_state.frustum_ofsy[frustum_index];
		int	   ofsz = mark_surfaces_state.frustum_ofsz[frustum_index];
		__m128 px = mark_surfaces_state.frustum_px[frustum_index];
		__m128 py = mark_surfaces_state.frustum_py[frustum_index];
		__m128 pz = mark_surfaces_state.frustum_pz[frustum_index];
		__m128 pd = mark_surfaces_state.frustum_pd[frustum_index];

		uint32_t frustum_lanes = 0;
		for (int boxes_index = 0; boxes_index < 4; ++boxes_index)
		{
			soa_aabb_t *box = boxes + boxes_index;
			__m128		v0 = _mm_mul_ps (_mm_loadu_ps ((*box) + ofsx), px);
			__m128		v1 = _mm_mul_ps (_mm_loadu_ps ((*box) + ofsx + 4), px);
			v0 = _mm_add_ps (v0, _mm_mul_ps (_mm_loadu_ps ((*box) + ofsy), py));
			v1 = _mm_add_ps (v1, _mm_mul_ps (_mm_loadu_ps ((*box) + ofsy + 4), py));
			v0 = _mm_add_ps (v0, _mm_mul_ps (_mm_loadu_ps ((*box) + ofsz), pz));
			v1 = _mm_add_ps (v1, _mm_mul_ps (_mm_loadu_ps ((*box) + ofsz + 4), pz));
			frustum_lanes |= (uint32_t)(_mm_movemask_ps (_mm_cmplt_ps (pd, v0)) | (_mm_movemask_ps (_mm_cmplt_ps (pd, v1)) << 4)) << (boxes_index * 8);
		}
		activelanes &= frustum_lanes;
	}

	return activelanes;
}
#elif defined(USE_NEON)
static FORCE_INLINE uint32_t NeonMoveMask (uint32x4_t input)
{
	static const int32x4_t shift = {0, 1, 2, 3};
	return vaddvq_u32 (vshlq_u32 (vshrq_n_u32 (input, 31), shift));
}

/*
===============
R_BackFaceCullSIMD

Performs backface culling for 32 planes
===============
*/
static FORCE_INLINE uint32_t R_BackFaceCullSIMD (soa_plane_t *planes)
{
	float32x4_t px = mark_surfaces_state.vieworg_px;
	float32x4_t py = mark_surfaces_state.vieworg_py;
	float32x4_t pz = mark_surfaces_state.vieworg_pz;

	uint32_t activelanes = 0;
	for (int plane_index = 0; plane_index < 4; ++plane_index)
	{
		soa_plane_t *plane = planes + plane_index;

		float32x4_t v0 = vmulq_f32 (vld1q_f32 ((*plane) + 0), px);
		float32x4_t v1 = vmulq_f32 (vld1q_f32 ((*plane) + 4), px);

		v0 = vmlaq_f32 (v0, vld1q_f32 ((*plane) + 8), py);
		v1 = vmlaq_f32 (v1, vld1q_f32 ((*plane) + 12), py);

		v0 = vmlaq_f32 (v0, vld1q_f32 ((*plane) + 16), pz);
		v1 = vmlaq_f32 (v1, vld1q_f32 ((*plane) + 20), pz);

		float32x4_t pd0 = vld1q_f32 ((*plane) + 24);
		float32x4_t pd1 = vld1q_f32 ((*plane) + 28);

		uint32_t plane_lanes = (uint32_t)(NeonMoveMask (vcltq_f32 (pd0, v0)) | (NeonMoveMask (vcltq_f32 (pd1, v1)) << 4));
		activelanes |= plane_lanes << (plane_index * 8);
	}
	return activelanes;
}

/*
===============
R_CullBoxSIMD

Performs frustum culling for 32 bounding boxes
===============
*/
static FORCE_INLINE uint32_t R_CullBoxSIMD (soa_aabb_t *boxes, uint32_t activelanes)
{
	for (int frustum_index = 0; frustum_index < 4; ++frustum_index)
	{
		if (activelanes == 0)
			break;

		int			ofsx = mark_surfaces_state.frustum_ofsx[frustum_index];
		int			ofsy = mark_surfaces_state.frustum_ofsy[frustum_index];
		int			ofsz = mark_surfaces_state.frustum_ofsz[frustum_index];
		float32x4_t px = mark_surfaces_state.frustum_px[frustum_index];
		float32x4_t py = mark_surfaces_state.frustum_py[frustum_index];
		float32x4_t pz = mark_surfaces_state.frustum_pz[frustum_index];
		float32x4_t pd = mark_surfaces_state.frustum_pd[frustum_index];

		uint32_t frustum_lanes = 0;
		for (int boxes_index = 0; boxes_index < 4; ++boxes_index)
		{
			soa_aabb_t *box = boxes + boxes_index;
			float32x4_t v0 = vmulq_f32 (vld1q_f32 ((*box) + ofsx), px);
			float32x4_t v1 = vmulq_f32 (vld1q_f32 ((*box) + ofsx + 4), px);
			v0 = vmlaq_f32 (v0, vld1q_f32 ((*box) + ofsy), py);
			v1 = vmlaq_f32 (v1, vld1q_f32 ((*box) + ofsy + 4), py);
			v0 = vmlaq_f32 (v0, vld1q_f32 ((*box) + ofsz), pz);
			v1 = vmlaq_f32 (v1, vld1q_f32 ((*box) + ofsz + 4), pz);
			frustum_lanes |= (uint32_t)(NeonMoveMask (vcltq_f32 (pd, v0)) | (NeonMoveMask (vcltq_f32 (pd, v1)) << 4)) << (boxes_index * 8);
		}
		activelanes &= frustum_lanes;
	}

	return activelanes;
}
#endif

#if defined(USE_SIMD)
/*
===============
R_MarkVisSurfacesSIMD
===============
*/
void R_MarkVisSurfacesSIMD (qboolean *use_tasks)
{
	msurface_t	*surf;
	unsigned int i, k;
	unsigned int numleafs = cl.worldmodel->numleafs;
	unsigned int numsurfaces = cl.worldmodel->numsurfaces;
	uint32_t	*vis = (uint32_t *)mark_surfaces_state.vis;
	uint32_t	*surfvis = (uint32_t *)cl.worldmodel->surfvis;
	soa_aabb_t	*leafbounds = cl.worldmodel->soa_leafbounds;

	// iterate through leaves, marking surfaces
	for (i = 0; i < numleafs; i += 32)
	{
		uint32_t mask = vis[i / 32];
		if (mask == 0)
			continue;

		mask = R_CullBoxSIMD (&leafbounds[i / 8], mask);
		while (mask != 0)
		{
			const int j = FindFirstBitNonZero (mask);
			mask &= ~(1u << j);

			mleaf_t *leaf = &cl.worldmodel->leafs[1 + i + j];
			if (r_drawworld_cheatsafe && (leaf->contents != CONTENTS_SKY || r_oldskyleaf.value))
			{
				unsigned int nummarksurfaces = leaf->nummarksurfaces;
				int			*marksurfaces = leaf->firstmarksurface;
				for (k = 0; k < nummarksurfaces; ++k)
				{
					unsigned int index = marksurfaces[k];
					surfvis[index / 32] |= 1u << (index % 32);
				}
				if (indirect)
					R_MarkDeps (leaf->combined_deps, 0);
			}

			// add static models
			if (leaf->efrags)
				R_StoreEfrags (&leaf->efrags);
		}
	}

	if (indirect)
		return;

	uint32_t brushpolys = 0;
	for (i = 0; i < numsurfaces; i += 32)
	{
		uint32_t mask = surfvis[i / 32];
		if (mask == 0)
			continue;

		mask &= R_BackFaceCullSIMD (&cl.worldmodel->soa_surfplanes[i / 8]);
		while (mask != 0)
		{
			const int j = FindFirstBitNonZero (mask);
			mask &= ~(1u << j);

			surf = &cl.worldmodel->surfaces[i + j];
			++brushpolys;
			R_ChainSurface (surf, chain_world);
			if (!r_gpulightmapupdate.value)
				R_RenderDynamicLightmaps (surf);
			else if (surf->lightmaptexturenum >= 0)
				lightmaps[surf->lightmaptexturenum].modified[0] |= surf->styles_bitmap;
			if (surf->texinfo->texture->warpimage)
				Atomic_StoreUInt32 (&surf->texinfo->texture->update_warp, true);
		}
	}

	Atomic_AddUInt32 (&rs_brushpolys, brushpolys); // count wpolys here
	R_SetupWorldCBXTexRanges (*use_tasks);
}

/*
===============
R_MarkLeafsSIMD
===============
*/
void R_MarkLeafsSIMD (int index, void *unused)
{
	unsigned int	 j;
	unsigned int	 first_leaf = index * 32;
	atomic_uint32_t *surfvis = (atomic_uint32_t *)cl.worldmodel->surfvis;
	soa_aabb_t		*leafbounds = cl.worldmodel->soa_leafbounds;
	uint32_t		*vis = (uint32_t *)mark_surfaces_state.vis;

	uint32_t *mask = &vis[index];
	if (*mask == 0)
		return;

	*mask = R_CullBoxSIMD (&leafbounds[index * 4], *mask);

	uint32_t mask_iter = *mask;
	while (mask_iter != 0)
	{
		const int i = FindFirstBitNonZero (mask_iter);

		mleaf_t *leaf = &cl.worldmodel->leafs[1 + first_leaf + i];
		if (r_drawworld_cheatsafe && (leaf->contents != CONTENTS_SKY || r_oldskyleaf.value))
		{
			unsigned int nummarksurfaces = leaf->nummarksurfaces;
			int			*marksurfaces = leaf->firstmarksurface;
			for (j = 0; j < nummarksurfaces; ++j)
			{
				unsigned int surf_index = marksurfaces[j];
				Atomic_OrUInt32 (&surfvis[surf_index / 32], 1u << (surf_index % 32));
			}
			if (indirect)
				R_MarkDeps (leaf->combined_deps, Tasks_GetWorkerIndex ());
		}
		const uint32_t bit_mask = ~(1u << i);
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
	uint32_t   *surfvis = (uint32_t *)cl.worldmodel->surfvis;
	msurface_t *surf;

	uint32_t *mask = &surfvis[index];
	if (*mask == 0)
		return;

	*mask &= R_BackFaceCullSIMD (&cl.worldmodel->soa_surfplanes[index * 4]);

	const int worker_index = Tasks_GetWorkerIndex ();
	uint32_t  mask_iter = *mask;
	while (mask_iter != 0)
	{
		const int i = FindFirstBitNonZero (mask_iter);

		surf = &cl.worldmodel->surfaces[(index * 32) + i];
		if (surf->lightmaptexturenum >= 0)
			lightmaps[surf->lightmaptexturenum].modified[worker_index] |= surf->styles_bitmap;
		if (surf->texinfo->texture->warpimage)
			Atomic_StoreUInt32 (&surf->texinfo->texture->update_warp, true);

		const uint32_t bit_mask = ~(1u << i);
		mask_iter &= bit_mask;
	}
}
#endif // defined(USE_SIMD)

/*
===============
R_StoreLeafEFrags
===============
*/
void R_StoreLeafEFrags (void *unused)
{
	unsigned int i;
	unsigned int numleafs = cl.worldmodel->numleafs;
	uint32_t	*vis = (uint32_t *)mark_surfaces_state.vis;
	for (i = 0; i < numleafs; i += 32)
	{
		uint32_t mask = vis[i / 32];
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
	msurface_t	*surf;
	unsigned int numsurfaces = cl.worldmodel->numsurfaces;
	uint32_t	*surfvis = (uint32_t *)cl.worldmodel->surfvis;
	uint32_t	 brushpolys = 0;
	for (i = 0; i < numsurfaces; i += 32)
	{
		uint32_t mask = surfvis[i / 32];
		while (mask != 0)
		{
			const int j = FindFirstBitNonZero (mask);
			mask &= ~(1u << j);
			surf = &cl.worldmodel->surfaces[i + j];
			++brushpolys;
			R_ChainSurface (surf, chain_world);
		}
	}

	Atomic_AddUInt32 (&rs_brushpolys, brushpolys); // count wpolys here
	R_SetupWorldCBXTexRanges (*use_tasks);
}

/*
===============
R_GetTransparentWaterTypes
===============
*/
static int R_GetTransparentWaterTypes ()
{
	int types = 0;
	if ((map_lavaalpha > 0 ? map_lavaalpha : map_fallbackalpha) != 1)
		types |= SURF_DRAWLAVA;
	if ((map_telealpha > 0 ? map_telealpha : map_fallbackalpha) != 1)
		types |= SURF_DRAWTELE;
	if ((map_slimealpha > 0 ? map_slimealpha : map_fallbackalpha) != 1)
		types |= SURF_DRAWSLIME;
	if (map_wateralpha != 1)
		types |= SURF_DRAWWATER;
	return types;
}

/*
===============
R_PrepareTransparentWaterSurfList
===============
*/
static void R_PrepareTransparentWaterSurfList ()
{
	int types = R_GetTransparentWaterTypes ();
	if (cl.worldmodel->water_surfs_specials != types)
	{
		if (!cl.worldmodel->water_surfs)
			cl.worldmodel->water_surfs = Mem_Realloc (cl.worldmodel->water_surfs, 8192 * sizeof (int));
		cl.worldmodel->used_water_surfs = 0;

		for (int i = 0; i < cl.worldmodel->numsurfaces; i++)
			if (cl.worldmodel->surfaces[i].flags & types)
			{
				if (cl.worldmodel->used_water_surfs >= 8192 && !(cl.worldmodel->used_water_surfs & (cl.worldmodel->used_water_surfs - 1)))
					cl.worldmodel->water_surfs = Mem_Realloc (cl.worldmodel->water_surfs, cl.worldmodel->used_water_surfs * 2 * sizeof (int));
				cl.worldmodel->water_surfs[cl.worldmodel->used_water_surfs] = i;
				++cl.worldmodel->used_water_surfs;
			}

		cl.worldmodel->water_surfs_specials = types;
	}
}

/*
===============
R_ChainVisSurfaces_TransparentWater
===============
*/
static void R_ChainVisSurfaces_TransparentWater ()
{
	R_PrepareTransparentWaterSurfList ();
	uint32_t *surfvis = (uint32_t *)cl.worldmodel->surfvis;
	for (int i = 0; i < cl.worldmodel->used_water_surfs; i++)
	{
		int j = cl.worldmodel->water_surfs[i];
		if (surfvis[j / 32] & 1 << j % 32 && !R_BackFaceCull (&cl.worldmodel->surfaces[j]))
			R_ChainSurface (&cl.worldmodel->surfaces[j], chain_world);
	}
}

/*
===============
R_MarkLeafsParallel
===============
*/
void R_MarkLeafsParallel (int index, void *unused)
{
	mleaf_t			*leaf;
	uint32_t		*mask = (uint32_t *)mark_surfaces_state.vis + index;
	atomic_uint32_t *surfvis = (atomic_uint32_t *)cl.worldmodel->surfvis;
	unsigned int	 first_leaf = index * 32 + 1;

	uint32_t mask_iter = *mask;
	while (mask_iter != 0)
	{
		const int	   i = FindFirstBitNonZero (mask_iter);
		const uint32_t bit_mask = ~(1u << i);
		mask_iter &= bit_mask;
		leaf = &cl.worldmodel->leafs[first_leaf + i];
		if (R_CullBox (leaf->minmaxs, leaf->minmaxs + 3))
		{
			*mask &= bit_mask;
			continue;
		}
		if (!leaf->efrags)
			*mask &= bit_mask;
		if (r_drawworld_cheatsafe && (leaf->contents != CONTENTS_SKY || r_oldskyleaf.value))
		{
			unsigned int nummarksurfaces = leaf->nummarksurfaces;
			int			*marksurfaces = leaf->firstmarksurface;
			for (unsigned int j = 0; j < nummarksurfaces; ++j)
			{
				unsigned int surf_index = marksurfaces[j];
				Atomic_OrUInt32 (&surfvis[surf_index / 32], 1u << (surf_index % 32));
			}
			if (indirect)
				R_MarkDeps (leaf->combined_deps, Tasks_GetWorkerIndex ());
		}
	}
}

/*
===============
R_BackfaceCullSurfacesParallel
===============
*/
void R_BackfaceCullSurfacesParallel (int index, void *unused)
{
	uint32_t   *surfvis = (uint32_t *)cl.worldmodel->surfvis + index;
	msurface_t *surf;

	uint32_t mask_iter = *surfvis;
	if (mask_iter == 0)
		return;

	const int worker_index = Tasks_GetWorkerIndex ();
	while (mask_iter != 0)
	{
		const int	   i = FindFirstBitNonZero (mask_iter);
		const uint32_t bit_mask = ~(1u << i);
		mask_iter &= bit_mask;

		surf = &cl.worldmodel->surfaces[(index * 32) + i];

		if (R_BackFaceCull (surf))
			*surfvis &= bit_mask;
		else
		{
			if (surf->lightmaptexturenum >= 0)
				lightmaps[surf->lightmaptexturenum].modified[worker_index] |= surf->styles_bitmap;
			if (surf->texinfo->texture->warpimage)
				Atomic_StoreUInt32 (&surf->texinfo->texture->update_warp, true);
		}
	}
}

/*
===============
R_MarkVisSurfaces
===============
*/
void R_MarkVisSurfaces (qboolean *use_tasks)
{
	int			i, j;
	msurface_t *surf;
	mleaf_t	   *leaf;
	uint32_t	brushpolys = 0;
	uint32_t   *vis = (uint32_t *)mark_surfaces_state.vis;
	uint32_t   *surfvis = (uint32_t *)cl.worldmodel->surfvis;

	leaf = &cl.worldmodel->leafs[1];
	for (i = 0; i < cl.worldmodel->numleafs; i++, leaf++)
	{
		if (vis[i / 32] & (1u << (i % 32)))
		{
			if (R_CullBox (leaf->minmaxs, leaf->minmaxs + 3))
				continue;

			if (r_drawworld_cheatsafe && (leaf->contents != CONTENTS_SKY || r_oldskyleaf.value))
			{
				if (indirect)
					R_MarkDeps (leaf->combined_deps, 0);
				for (j = 0; j < leaf->nummarksurfaces; j++)
				{
					if (indirect)
					{
						unsigned int surf_index = leaf->firstmarksurface[j];
						surfvis[surf_index / 32] |= 1u << (surf_index % 32);
						continue;
					}
					surf = &cl.worldmodel->surfaces[leaf->firstmarksurface[j]];
					if (surf->visframe != r_visframecount)
					{
						surf->visframe = r_visframecount;
						if (!R_BackFaceCull (surf))
						{
							++brushpolys;
							R_ChainSurface (surf, chain_world);
							if (!r_gpulightmapupdate.value)
								R_RenderDynamicLightmaps (surf);
							else if (surf->lightmaptexturenum >= 0)
								lightmaps[surf->lightmaptexturenum].modified[0] |= surf->styles_bitmap;
							if (surf->texinfo->texture->warpimage)
								Atomic_StoreUInt32 (&surf->texinfo->texture->update_warp, true);
						}
					}
				}
			}

			// add static models
			if (leaf->efrags)
				R_StoreEfrags (&leaf->efrags);
		}
	}

	if (indirect)
		return;

	Atomic_AddUInt32 (&rs_brushpolys, brushpolys); // count wpolys here
	R_SetupWorldCBXTexRanges (*use_tasks);
}

/*
===============
R_MarkSurfacesPrepare
===============
*/
static void R_MarkSurfacesPrepare (void *unused)
{
	int		 i;
	qboolean nearwaterportal;
	int		 numleafs = cl.worldmodel->numleafs;

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

	uint32_t *vis = (uint32_t *)mark_surfaces_state.vis;
	if ((numleafs % 32) != 0)
		vis[numleafs / 32] &= (1u << (numleafs % 32)) - 1;

	r_visframecount++;

	// set all chains to null
	for (i = 0; i < cl.worldmodel->numtextures; i++)
		if (cl.worldmodel->textures[i])
		{
			cl.worldmodel->textures[i]->texturechains[chain_world] = NULL;
			cl.worldmodel->textures[i]->chain_size[chain_world] = 0;
		}

#if defined(USE_SIMD)
	if (use_simd)
	{
		memset (cl.worldmodel->surfvis, 0, (cl.worldmodel->numsurfaces + 31) / 8);
#if defined(USE_SSE2)
		for (int frustum_index = 0; frustum_index < 4; ++frustum_index)
		{
			mplane_t *p = frustum + frustum_index;
			byte	  signbits = p->signbits;
			__m128	  vplane = _mm_loadu_ps (p->normal);
			mark_surfaces_state.frustum_ofsx[frustum_index] = signbits & 1 ? 0 : 8;	  // x min/max
			mark_surfaces_state.frustum_ofsy[frustum_index] = signbits & 2 ? 16 : 24; // y min/max
			mark_surfaces_state.frustum_ofsz[frustum_index] = signbits & 4 ? 32 : 40; // z min/max
			mark_surfaces_state.frustum_px[frustum_index] = _mm_shuffle_ps (vplane, vplane, _MM_SHUFFLE (0, 0, 0, 0));
			mark_surfaces_state.frustum_py[frustum_index] = _mm_shuffle_ps (vplane, vplane, _MM_SHUFFLE (1, 1, 1, 1));
			mark_surfaces_state.frustum_pz[frustum_index] = _mm_shuffle_ps (vplane, vplane, _MM_SHUFFLE (2, 2, 2, 2));
			mark_surfaces_state.frustum_pd[frustum_index] = _mm_shuffle_ps (vplane, vplane, _MM_SHUFFLE (3, 3, 3, 3));
		}
		__m128 pos = _mm_loadu_ps (r_refdef.vieworg);
		mark_surfaces_state.vieworg_px = _mm_shuffle_ps (pos, pos, _MM_SHUFFLE (0, 0, 0, 0));
		mark_surfaces_state.vieworg_py = _mm_shuffle_ps (pos, pos, _MM_SHUFFLE (1, 1, 1, 1));
		mark_surfaces_state.vieworg_pz = _mm_shuffle_ps (pos, pos, _MM_SHUFFLE (2, 2, 2, 2));
#elif defined(USE_NEON)
		for (int frustum_index = 0; frustum_index < 4; ++frustum_index)
		{
			mplane_t *p = frustum + frustum_index;
			byte	  signbits = p->signbits;
			mark_surfaces_state.frustum_ofsx[frustum_index] = signbits & 1 ? 0 : 8;	  // x min/max
			mark_surfaces_state.frustum_ofsy[frustum_index] = signbits & 2 ? 16 : 24; // y min/max
			mark_surfaces_state.frustum_ofsz[frustum_index] = signbits & 4 ? 32 : 40; // z min/max
			mark_surfaces_state.frustum_px[frustum_index] = vdupq_n_f32 (p->normal[0]);
			mark_surfaces_state.frustum_py[frustum_index] = vdupq_n_f32 (p->normal[1]);
			mark_surfaces_state.frustum_pz[frustum_index] = vdupq_n_f32 (p->normal[2]);
			mark_surfaces_state.frustum_pd[frustum_index] = vdupq_n_f32 (p->dist);
		}
		mark_surfaces_state.vieworg_px = vdupq_n_f32 (r_refdef.vieworg[0]);
		mark_surfaces_state.vieworg_py = vdupq_n_f32 (r_refdef.vieworg[1]);
		mark_surfaces_state.vieworg_pz = vdupq_n_f32 (r_refdef.vieworg[2]);
#endif
	}
	else
#endif
		if (r_parallelmark.value || indirect)
		memset (cl.worldmodel->surfvis, 0, (cl.worldmodel->numsurfaces + 31) / 8);
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
		if (r_parallelmark.value)
		{
			unsigned int  numleafs = cl.worldmodel->numleafs;
			task_handle_t mark_surfaces;
#if defined(USE_SIMD)
			if (use_simd)
				mark_surfaces = Task_AllocateAndAssignIndexedFunc (R_MarkLeafsSIMD, (numleafs + 31) / 32, NULL, 0);
			else
#endif
				mark_surfaces = Task_AllocateAndAssignIndexedFunc (R_MarkLeafsParallel, (numleafs + 31) / 32, NULL, 0);
			Task_AddDependency (prepare_mark, mark_surfaces);
			Task_Submit (mark_surfaces);

			*store_efrags = Task_AllocateAndAssignFunc (R_StoreLeafEFrags, NULL, 0);
			Task_AddDependency (mark_surfaces, *store_efrags);

			if (!indirect && r_drawworld_cheatsafe)
			{
				unsigned int numsurfaces = cl.worldmodel->numsurfaces;
#if defined(USE_SIMD)
				if (use_simd)
					*cull_surfaces = Task_AllocateAndAssignIndexedFunc (R_BackfaceCullSurfacesSIMD, (numsurfaces + 31) / 32, NULL, 0);
				else
#endif
					*cull_surfaces = Task_AllocateAndAssignIndexedFunc (R_BackfaceCullSurfacesParallel, (numsurfaces + 31) / 32, NULL, 0);
				Task_AddDependency (mark_surfaces, *cull_surfaces);

				*chain_surfaces = Task_AllocateAndAssignFunc ((task_func_t)R_ChainVisSurfaces, &use_tasks, sizeof (qboolean));
				Task_AddDependency (*cull_surfaces, *chain_surfaces);
			}
			else // indirect
			{
				*cull_surfaces = mark_surfaces;
				*chain_surfaces = mark_surfaces;
			}
		}
		else
		{
			task_handle_t mark_surfaces;
#if defined(USE_SIMD)
			if (use_simd)
				mark_surfaces = Task_AllocateAndAssignFunc ((task_func_t)R_MarkVisSurfacesSIMD, &use_tasks, sizeof (qboolean));
			else
#endif
				mark_surfaces = Task_AllocateAndAssignFunc ((task_func_t)R_MarkVisSurfaces, &use_tasks, sizeof (qboolean));
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
static void R_FlushBatch (
	cb_context_t *cbx, qboolean fullbright_enabled, qboolean alpha_test, qboolean alpha_blend, qboolean use_zbias, gltexture_t *lightmap_texture,
	uint32_t *brushpasses)
{
	if (cbx->num_vbo_indices > 0)
	{
		int pipeline_index =
			(fullbright_enabled ? 1 : 0) + (alpha_test ? 2 : 0) + (alpha_blend ? 4 : 0) + (vid_filter.value != 0 && vid_palettize.value != 0 ? 8 : 0);
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
				cbx->cb, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_globals.world_pipeline_layout.handle, 1, 1, &greylightmap->descriptor_set, 0, NULL);

		VkBuffer	 buffer;
		VkDeviceSize buffer_offset;
		byte		*indices = R_IndexAllocate (cbx->num_vbo_indices * sizeof (uint32_t), &buffer, &buffer_offset);
		memcpy (indices, cbx->vbo_indices, cbx->num_vbo_indices * sizeof (uint32_t));

		vulkan_globals.vk_cmd_bind_index_buffer (cbx->cb, buffer, buffer_offset, VK_INDEX_TYPE_UINT32);
		vulkan_globals.vk_cmd_draw_indexed (cbx->cb, cbx->num_vbo_indices, 1, 0, 0, 0);

		R_ClearBatch (cbx);
		++(*brushpasses);
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
	cb_context_t *cbx, msurface_t *s, qboolean fullbright_enabled, qboolean alpha_test, qboolean alpha_blend, qboolean use_zbias, gltexture_t *lightmap_texture,
	uint32_t *brushpasses)
{
	int num_surf_indices;

	num_surf_indices = R_NumTriangleIndicesForSurf (s);

	if (cbx->num_vbo_indices + num_surf_indices > MAX_BATCH_SIZE)
		R_FlushBatch (cbx, fullbright_enabled, alpha_test, alpha_blend, use_zbias, lightmap_texture, brushpasses);

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
	int			i;
	msurface_t *s;
	texture_t  *t;
	float		color[] = {1.0f, 1.0f, 1.0f};
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
void R_DrawTextureChains_Water (cb_context_t *cbx, qmodel_t *model, entity_t *ent, texchain_t chain, qboolean opaque_only, qboolean transparent_only)
{
	int			i;
	msurface_t *s;
	texture_t  *t;

	VkDeviceSize offset = 0;
	vulkan_globals.vk_cmd_bind_vertex_buffers (cbx->cb, 0, 1, &bmodel_vertex_buffer, &offset);

	vulkan_globals.vk_cmd_bind_descriptor_sets (
		cbx->cb, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_globals.world_pipeline_layout.handle, 2, 1, &nulltexture->descriptor_set, 0, NULL);
	if (r_lightmap_cheatsafe)
		vulkan_globals.vk_cmd_bind_descriptor_sets (
			cbx->cb, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_globals.world_pipeline_layout.handle, 0, 1, &greytexture->descriptor_set, 0, NULL);

	uint32_t brushpasses = 0;
	for (i = 0; i < model->numtextures; ++i)
	{
		t = model->textures[i];

		if (!t || !t->texturechains[chain] || !(t->texturechains[chain]->flags & SURF_DRAWTURB))
			continue;

		gltexture_t *lightmap_texture = NULL;
		R_ClearBatch (cbx);

		int			   lastlightmap = -2;
		const float	   alpha = GL_WaterAlphaForEntitySurface (ent, t->texturechains[chain]);
		const qboolean alpha_blend = alpha < 1.0f;

		if (((opaque_only && alpha_blend) || (transparent_only && !alpha_blend)) && (!r_lightmap_cheatsafe || !indirect))
			continue;

		gltexture_t *gl_texture = t->warpimage;
		if (!r_lightmap_cheatsafe)
			vulkan_globals.vk_cmd_bind_descriptor_sets (
				cbx->cb, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_globals.world_pipeline_layout.handle, 0, 1, &gl_texture->descriptor_set, 0, NULL);

		if (model != cl.worldmodel)
			Atomic_StoreUInt32 (&t->update_warp, true); // FIXME: races against UpdateWarpTextures task, bmodel-only warps may end up updating at half frequency

		for (s = t->texturechains[chain]; s; s = s->texturechains[chain])
		{
			if (s->lightmaptexturenum != lastlightmap)
			{
				if (alpha_blend)
					R_PushConstants (cbx, VK_SHADER_STAGE_ALL_GRAPHICS, 20 * sizeof (float), 1 * sizeof (float), &alpha);
				R_FlushBatch (cbx, false, false, alpha_blend, false, lightmap_texture, &brushpasses);
				lightmap_texture = (s->lightmaptexturenum >= 0) ? lightmaps[s->lightmaptexturenum].texture : greylightmap;
				lastlightmap = s->lightmaptexturenum;
			}
			R_BatchSurface (cbx, s, false, false, alpha_blend, false, lightmap_texture, &brushpasses);
		}

		if (alpha_blend)
			R_PushConstants (cbx, VK_SHADER_STAGE_ALL_GRAPHICS, 20 * sizeof (float), 1 * sizeof (float), &alpha);
		R_FlushBatch (cbx, false, false, alpha_blend, false, lightmap_texture, &brushpasses);
	}

	Atomic_AddUInt32 (&rs_brushpasses, brushpasses);
}

/*
================
R_DrawTextureChains_Multitexture
================
*/
void R_DrawTextureChains_Multitexture (cb_context_t *cbx, qmodel_t *model, entity_t *ent, texchain_t chain, const float alpha, int texstart, int texend)
{
	int			 i;
	msurface_t	*s;
	texture_t	*t;
	qboolean	 fullbright_enabled = false;
	qboolean	 alpha_test = false;
	qboolean	 alpha_blend = alpha < 1.0f;
	qboolean	 use_zbias = (gl_zfix.value && model != cl.worldmodel);
	int			 lastlightmap;
	int			 ent_frame = ent != NULL ? ent->frame : 0;
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

		if (!t || !t->texturechains[chain] || t->texturechains[chain]->flags & (SURF_DRAWTURB | SURF_DRAWTILED))
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

		texture_t	*texture = R_TextureAnimation (t, ent_frame);
		gltexture_t *gl_texture = texture->gltexture;
		if (!r_lightmap_cheatsafe)
			vulkan_globals.vk_cmd_bind_descriptor_sets (
				cbx->cb, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_globals.world_pipeline_layout.handle, 0, 1, &gl_texture->descriptor_set, 0, NULL);

		for (s = t->texturechains[chain]; s; s = s->texturechains[chain])
		{
			if (s->lightmaptexturenum != lastlightmap)
			{
				R_FlushBatch (cbx, fullbright_enabled, alpha_test, alpha_blend, use_zbias, lightmap_texture, &brushpasses);
				lightmap_texture = lightmaps[s->lightmaptexturenum].texture;
			}

			lastlightmap = s->lightmaptexturenum;
			R_BatchSurface (cbx, s, fullbright_enabled, alpha_test, alpha_blend, use_zbias, lightmap_texture, &brushpasses);
		}

		R_FlushBatch (cbx, fullbright_enabled, alpha_test, alpha_blend, use_zbias, lightmap_texture, &brushpasses);
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
void R_DrawWorld_Water (cb_context_t *cbx, qboolean transparent)
{
	if (!r_drawworld_cheatsafe)
		return;

	R_BeginDebugUtilsLabel (cbx, transparent ? "Transparent World Water" : "Opaque World Water");
	if (indirect)
	{
		if (WATER_FIXED_ORDER && transparent)
		{
			R_ChainVisSurfaces_TransparentWater ();
			R_DrawTextureChains_Water (cbx, cl.worldmodel, NULL, chain_world, false, true);
		}
		else
			R_DrawIndirectBrushes (cbx, true, transparent, false, -1);
	}
	else
		R_DrawTextureChains_Water (cbx, cl.worldmodel, NULL, chain_world, !transparent, transparent);
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
