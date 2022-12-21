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
// r_brush.c: brush model rendering. renamed from r_surf.c

#include "quakedef.h"

extern cvar_t gl_fullbrights, r_drawflat, r_gpulightmapupdate; // johnfitz

int gl_lightmap_format;

#define SHELF_HEIGHT 256
#define SHELVES		 (LMBLOCK_HEIGHT / SHELF_HEIGHT)

#define LM_BIN_E 8
#define LM_BINS	 49

static int SizeToBin (int size)
{
	size -= 1;
	if (size < LM_BIN_E * 2 + 1)
		return size;
	int bc = Q_log2 (size / LM_BIN_E);
	return (size >> bc) + LM_BIN_E * bc + 1;
}

static int BinToSize (int bin)
{
	if (bin < LM_BIN_E * 2 + 1)
		return bin + 1;
	bin -= 1;
	int bc = bin / LM_BIN_E - 1;
	return (bin % LM_BIN_E + LM_BIN_E + 1) << bc;
}

struct lightmap_s *lightmaps;
int				   lightmap_count;
int				   last_lightmap_allocated;
int				   used_columns[MAX_SANITY_LIGHTMAPS][SHELVES];
int				   lightmap_idx[LM_BINS];
int				   shelf_idx[LM_BINS];
int				   columns[LM_BINS];
int				   rows[LM_BINS];

/* Lightmap extents are usually <= 18 with the default qbsp -subdivide of 240. The check in CalcSurfaceExtents ()
   limits them to 126 x 126 on load. The lightmap packer and the blocklights array can handle up to 256 x 256. */

unsigned blocklights[256 * 256 * 3 + 1]; // johnfitz -- was 18*18, added lit support (*3) and loosened surface extents maximum

qboolean indirect = true;
qboolean indirect_ready = false;

typedef struct
{
	texture_t *texture;
	short	   lightmap_idx;
	short	   is_bmodel; // for gl_zfix
	int		   max_indices;
} indirectdraw_t;

#define MAX_INDIRECT_DRAWS 32768
static indirectdraw_t indirect_draws[MAX_INDIRECT_DRAWS];
static int			  used_indirect_draws = 0;
static uint32_t		  indirect_bmodel_start;

#define INDIRECT_ZBIAS 1 // suport gl_zfix for nontransformed models. Costs extra indirect drawcalls
extern cvar_t gl_zfix;

extern cvar_t vid_filter;
extern cvar_t vid_palettize;

static VkDrawIndexedIndirectCommand initial_indirect_buffer[MAX_INDIRECT_DRAWS];

typedef union
{
	struct // 1st element contains this
	{
		short has_sky;
		short water_count;
		int	  lm_count;
	};
	atomic_uint32_t *update_warp; // next water_count elements contain this
	struct						  // last lm_count elements contain this
	{
		int		 lightmap_num;
		uint32_t lightmap_styles;
	};
} combined_brush_deps;

static combined_brush_deps *brush_deps_data;
static int					used_deps_data = 0;
#define INITIAL_BRUSH_DEPS_SIZE 16384

static int alloc_deps_data (combined_brush_deps *items)
{
	static int last = 0;
	int		   item_count = items[0].water_count + items[0].lm_count;

	if (last < used_deps_data && !memcmp (items, &brush_deps_data[last], sizeof (combined_brush_deps)) &&
		!memcmp (items + 1, &brush_deps_data[last + 1], item_count * sizeof (combined_brush_deps)))
		return last;

	if (used_deps_data == 0)
		brush_deps_data = Mem_Alloc (INITIAL_BRUSH_DEPS_SIZE * sizeof (combined_brush_deps));

	for (int i = 0; i <= item_count; i++)
	{
		if (used_deps_data >= INITIAL_BRUSH_DEPS_SIZE && !(used_deps_data & (used_deps_data - 1)))
			brush_deps_data = Mem_Realloc (brush_deps_data, used_deps_data * 2 * sizeof (combined_brush_deps));
		brush_deps_data[used_deps_data] = items[i];
		++used_deps_data;
	}
	return (last = used_deps_data - item_count - 1);
}

static void calc_deps (qmodel_t *model, mleaf_t *leaf)
{
	combined_brush_deps deps[1 + 256 + MAX_SANITY_LIGHTMAPS];
	deps[0].has_sky = false;
	deps[0].water_count = 0;
	deps[0].lm_count = 0;
	const int num_surfaces = model ? model->nummodelsurfaces : leaf->nummarksurfaces;

	for (int i = 0; i < num_surfaces; i++)
	{
		msurface_t *psurf = model ? &model->surfaces[model->firstmodelsurface] + i : &cl.worldmodel->surfaces[leaf->firstmarksurface[i]];
		texture_t  *t = psurf->texinfo->texture;
		if (t->name[0] == '*')
		{
			qboolean found = false;
			for (int j = 1; j < 1 + deps[0].water_count; j++)
				if (deps[j].update_warp == &t->update_warp)
				{
					found = true;
					break;
				}
			if (!found)
			{
				++deps[0].water_count;
				if (deps[0].water_count > 256)
					Sys_Error ("A single bmodel / world leaf is using more than 256 different water textures");
				if (sizeof (atomic_uint32_t *) < sizeof (combined_brush_deps)) // make sure the padding is 0 in 32-bit builds
					memset (&deps[deps[0].water_count], 0, sizeof (combined_brush_deps));
				deps[deps[0].water_count].update_warp = &t->update_warp;
			}
		}
		if (model && psurf->flags & SURF_DRAWSKY)
			deps[0].has_sky = true;
	}

	for (int i = 0; i < num_surfaces; i++)
	{
		msurface_t *psurf = model ? &model->surfaces[model->firstmodelsurface] + i : &cl.worldmodel->surfaces[leaf->firstmarksurface[i]];
		if (psurf->lightmaptexturenum >= 0)
		{
			qboolean found = false;
			for (int j = 1 + deps[0].water_count; j < 1 + deps[0].water_count + deps[0].lm_count; j++)
				if (deps[j].lightmap_num == psurf->lightmaptexturenum)
				{
					deps[j].lightmap_styles |= psurf->styles_bitmap;
					found = true;
					break;
				}
			if (!found)
			{
				++deps[0].lm_count;
				deps[deps[0].water_count + deps[0].lm_count].lightmap_num = psurf->lightmaptexturenum;
				deps[deps[0].water_count + deps[0].lm_count].lightmap_styles = psurf->styles_bitmap;
			}
		}
	}

	if (model)
		model->combined_deps = alloc_deps_data (deps);
	else
		leaf->combined_deps = alloc_deps_data (deps);
}

void mark_deps (int combined_deps, int worker_index)
{
	combined_brush_deps *deps = &brush_deps_data[combined_deps];
	int					 water_count = deps->water_count;
	int					 lm_count = deps->lm_count;
	int					 i;
	for (i = 0; i < water_count; ++i)
		Atomic_StoreUInt32 ((++deps)->update_warp, true);
	for (i = 0, ++deps; i < lm_count; ++i, ++deps)
		lightmaps[deps->lightmap_num].modified[worker_index] |= deps->lightmap_styles;
}

static vulkan_memory_t bmodel_memory;
VkBuffer			   bmodel_vertex_buffer;

extern cvar_t r_showtris;
extern cvar_t r_simd;
typedef struct lm_compute_surface_data_s
{
	uint32_t packed_lightstyles;
	vec3_t	 normal;
	float	 dist;
	uint32_t packed_light_st_and_tex;
	uint32_t packed_vbo_offset_and_count;
	uint32_t packed_texturemins;
	vec4_t	 vecs[2];
} lm_compute_surface_data_t;
COMPILE_TIME_ASSERT (lm_compute_surface_data_t, sizeof (lm_compute_surface_data_t) == 64);

typedef struct lm_compute_light_s
{
	vec3_t origin;
	float  radius;
	vec3_t color;
	float  minlight;
} lm_compute_light_t;
COMPILE_TIME_ASSERT (lm_compute_light_t, sizeof (lm_compute_light_t) == 32);

#define WORKGROUP_BOUNDS_BUFFER_SIZE ((LMBLOCK_WIDTH / 8) * (LMBLOCK_HEIGHT / 8) * sizeof (lm_compute_workgroup_bounds_t))

static vulkan_memory_t	   lightstyles_scales_buffer_memory;
static vulkan_memory_t	   lights_buffer_memory;
static vulkan_memory_t	   surface_data_buffer_memory;
static vulkan_memory_t	   workgroup_bounds_buffer_memory;
static vulkan_memory_t	   indirect_buffer_memory;
static vulkan_memory_t	   indirect_index_buffer_memory;
static vulkan_memory_t	   dyn_visibility_buffer_memory;
static VkBuffer			   surface_data_buffer;
static int				   num_surfaces;
static VkBuffer			   indirect_buffer;
static VkBuffer			   indirect_index_buffer;
static VkBuffer			   dyn_visibility_buffer;
static uint32_t			   dyn_visibility_offset; // for double-buffering
static unsigned char	  *dyn_visibility_view;
static VkBuffer			   lightstyles_scales_buffer;
static VkBuffer			   lights_buffer;
static float			  *lightstyles_scales_buffer_mapped;
static lm_compute_light_t *lights_buffer_mapped;

static int current_compute_buffer_index;

/*
===============
R_TextureAnimation -- johnfitz -- added "frame" param to eliminate use of "currententity" global

Returns the proper texture for a given time and base texture
===============
*/
texture_t *R_TextureAnimation (texture_t *base, int frame)
{
	int relative;
	int count;

	if (frame)
		if (base->alternate_anims)
			base = base->alternate_anims;

	if (!base->anim_total)
		return base;

	relative = (int)(cl.time * 10) % base->anim_total;

	count = 0;
	while (base->anim_min > relative || base->anim_max <= relative)
	{
		base = base->anim_next;
		if (!base)
			Sys_Error ("R_TextureAnimation: broken cycle");
		if (++count > 100)
			Sys_Error ("R_TextureAnimation: infinite cycle");
	}

	return base;
}

/*
================
DrawGLPoly
================
*/
void DrawGLPoly (cb_context_t *cbx, glpoly_t *p, float color[3], float alpha)
{
	const int numverts = p->numverts;
	const int numtriangles = (numverts - 2);
	const int numindices = numtriangles * 3;

	VkBuffer	 vertex_buffer;
	VkDeviceSize vertex_buffer_offset;

	basicvertex_t *vertices = (basicvertex_t *)R_VertexAllocate (numverts * sizeof (basicvertex_t), &vertex_buffer, &vertex_buffer_offset);

	float *v;
	int	   i;
	int	   current_index = 0;

	v = p->verts[0];
	for (i = 0; i < numverts; ++i, v += VERTEXSIZE)
	{
		vertices[i].position[0] = v[0];
		vertices[i].position[1] = v[1];
		vertices[i].position[2] = v[2];
		vertices[i].texcoord[0] = v[3];
		vertices[i].texcoord[1] = v[4];
		vertices[i].color[0] = color[0] * 255.0f;
		vertices[i].color[1] = color[1] * 255.0f;
		vertices[i].color[2] = color[2] * 255.0f;
		vertices[i].color[3] = alpha * 255.0f;
	}

	// I don't know the maximum poly size quake maps can have, so just in case fall back to dynamic allocations
	// TODO: Find out if it's necessary
	if (numindices > FAN_INDEX_BUFFER_SIZE)
	{
		VkBuffer	 index_buffer;
		VkDeviceSize index_buffer_offset;

		uint16_t *indices = (uint16_t *)R_IndexAllocate (numindices * sizeof (uint16_t), &index_buffer, &index_buffer_offset);
		for (i = 0; i < numtriangles; ++i)
		{
			indices[current_index++] = 0;
			indices[current_index++] = 1 + i;
			indices[current_index++] = 2 + i;
		}
		vulkan_globals.vk_cmd_bind_index_buffer (cbx->cb, index_buffer, index_buffer_offset, VK_INDEX_TYPE_UINT16);
	}
	else
		vulkan_globals.vk_cmd_bind_index_buffer (cbx->cb, vulkan_globals.fan_index_buffer, 0, VK_INDEX_TYPE_UINT16);

	vulkan_globals.vk_cmd_bind_vertex_buffers (cbx->cb, 0, 1, &vertex_buffer, &vertex_buffer_offset);
	vulkan_globals.vk_cmd_draw_indexed (cbx->cb, numindices, 1, 0, 0, 0);
}

/*
=============================================================

	BRUSH MODELS

=============================================================
*/

/*
=================
R_HasSky
=================
*/
qboolean R_HasSky (entity_t *e)
{
	assert (e->model->type == mod_brush && e->model->name[0] == '*');
	return brush_deps_data[e->model->combined_deps].has_sky;
}

/*
=================
R_IndirectBrush
=================
*/
qboolean R_IndirectBrush (entity_t *e)
{
	assert (e->model->type == mod_brush);
	return indirect && !(e->origin[0] || e->origin[1] || e->origin[2] || e->angles[0] || e->angles[1] || e->angles[2] ||
						 ENTSCALE_DECODE (e->netstate.scale) != 1.0f || ENTALPHA_DECODE (e->alpha) != 1.0f || e->frame != 0 || e->model->name[0] != '*' ||
						 (WATER_FIXED_ORDER && brush_deps_data[e->model->combined_deps].water_count != 0));
}

/*
=================
R_DrawBrushModel
=================
*/
void R_DrawBrushModel (cb_context_t *cbx, entity_t *e, int chain, int *brushpolys)
{
	int			i, k;
	msurface_t *psurf;
	float		dot;
	mplane_t   *pplane;
	qmodel_t   *clmodel;
	vec3_t		modelorg;

	if (R_CullModelForEntity (e))
		return;

	clmodel = e->model;

	if (R_IndirectBrush (e))
	{
		// indirect mark
		int				 start = clmodel->firstmodelsurface;
		int				 end = start + clmodel->nummodelsurfaces;
		int				 startword = start / 32;
		int				 endword = end / 32;
		atomic_uint32_t *surfvis = (atomic_uint32_t *)cl.worldmodel->surfvis;
		if (startword == endword)
			Atomic_OrUInt32 (&surfvis[startword], (1u << end % 32) - (1u << start % 32));
		else
		{
			uint32_t supress_warning = (1u << start % 32);
			Atomic_OrUInt32 (&surfvis[startword], (1ull << 32) - supress_warning);
			for (i = startword + 1; i < endword; i++)
				Atomic_StoreUInt32 (&surfvis[i], 0xFFFFFFFF);
			Atomic_OrUInt32 (&surfvis[endword], (1u << end % 32) - 1);
		}
		mark_deps (clmodel->combined_deps, Tasks_GetWorkerIndex ());
		return;
	}

	VectorSubtract (r_refdef.vieworg, e->origin, modelorg);
	if (e->angles[0] || e->angles[1] || e->angles[2])
	{
		vec3_t temp;
		vec3_t forward, right, up;

		VectorCopy (modelorg, temp);
		AngleVectors (e->angles, forward, right, up);
		modelorg[0] = DotProduct (temp, forward);
		modelorg[1] = -DotProduct (temp, right);
		modelorg[2] = DotProduct (temp, up);
	}

	psurf = &clmodel->surfaces[clmodel->firstmodelsurface];

	// calculate dynamic lighting for bmodel if it's not an
	// instanced model
	if (!r_gpulightmapupdate.value && clmodel->firstmodelsurface != 0)
	{
		for (k = 0; k < MAX_DLIGHTS; k++)
		{
			if ((cl_dlights[k].die < cl.time) || (!cl_dlights[k].radius))
				continue;

			R_MarkLights (&cl_dlights[k], k, clmodel->nodes + clmodel->hulls[0].firstclipnode);
		}
	}

	vec3_t e_angles;
	VectorCopy (e->angles, e_angles);
	e_angles[0] = -e_angles[0]; // stupid quake bug
	float model_matrix[16];
	IdentityMatrix (model_matrix);
	R_RotateForEntity (model_matrix, e->origin, e_angles, e->netstate.scale);

	float mvp[16];
	memcpy (mvp, vulkan_globals.view_projection_matrix, 16 * sizeof (float));
	MatrixMultiply (mvp, model_matrix);

	R_PushConstants (cbx, VK_SHADER_STAGE_ALL_GRAPHICS, 0, 16 * sizeof (float), mvp);
	R_ClearTextureChains (clmodel, chain);
	const int worker_index = Tasks_GetWorkerIndex ();
	for (i = 0; i < clmodel->nummodelsurfaces; i++, psurf++)
	{
		pplane = psurf->plane;
		dot = DotProduct (modelorg, pplane->normal) - pplane->dist;
		if (((psurf->flags & SURF_PLANEBACK) && (dot < -BACKFACE_EPSILON)) || (!(psurf->flags & SURF_PLANEBACK) && (dot > BACKFACE_EPSILON)))
		{
			R_ChainSurface (psurf, chain);
			++(*brushpolys);
			if (!r_gpulightmapupdate.value)
				R_RenderDynamicLightmaps (psurf);
			else if (psurf->lightmaptexturenum >= 0)
				lightmaps[psurf->lightmaptexturenum].modified[worker_index] |= psurf->styles_bitmap;
		}
	}

	R_DrawTextureChains (cbx, clmodel, e, chain);
	R_DrawTextureChains_Water (cbx, clmodel, e, chain);
	R_PushConstants (cbx, VK_SHADER_STAGE_ALL_GRAPHICS, 0, 16 * sizeof (float), vulkan_globals.view_projection_matrix);
}

/*
=================
R_DrawBrushModel_ShowTris -- johnfitz
=================
*/
void R_DrawBrushModel_ShowTris (cb_context_t *cbx, entity_t *e)
{
	int			i;
	msurface_t *psurf;
	float		dot;
	mplane_t   *pplane;
	qmodel_t   *clmodel;
	float		color[] = {1.0f, 1.0f, 1.0f};
	const float alpha = 1.0f;
	vec3_t		modelorg;

	if (R_CullModelForEntity (e) || R_IndirectBrush (e))
		return;

	clmodel = e->model;

	VectorSubtract (r_refdef.vieworg, e->origin, modelorg);
	if (e->angles[0] || e->angles[1] || e->angles[2])
	{
		vec3_t temp;
		vec3_t forward, right, up;

		VectorCopy (modelorg, temp);
		AngleVectors (e->angles, forward, right, up);
		modelorg[0] = DotProduct (temp, forward);
		modelorg[1] = -DotProduct (temp, right);
		modelorg[2] = DotProduct (temp, up);
	}

	psurf = &clmodel->surfaces[clmodel->firstmodelsurface];

	e->angles[0] = -e->angles[0]; // stupid quake bug
	float model_matrix[16];
	IdentityMatrix (model_matrix);
	R_RotateForEntity (model_matrix, e->origin, e->angles, e->netstate.scale);
	e->angles[0] = -e->angles[0]; // stupid quake bug

	float mvp[16];
	memcpy (mvp, vulkan_globals.view_projection_matrix, 16 * sizeof (float));
	MatrixMultiply (mvp, model_matrix);

	if (r_showtris.value == 1)
		R_BindPipeline (cbx, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_globals.showtris_pipeline);
	else
		R_BindPipeline (cbx, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_globals.showtris_depth_test_pipeline);
	R_PushConstants (cbx, VK_SHADER_STAGE_ALL_GRAPHICS, 0, 16 * sizeof (float), mvp);

	//
	// draw it
	//
	for (i = 0; i < clmodel->nummodelsurfaces; i++, psurf++)
	{
		pplane = psurf->plane;
		dot = DotProduct (modelorg, pplane->normal) - pplane->dist;
		if (((psurf->flags & SURF_PLANEBACK) && (dot < -BACKFACE_EPSILON)) || (!(psurf->flags & SURF_PLANEBACK) && (dot > BACKFACE_EPSILON)))
		{
			DrawGLPoly (cbx, psurf->polys, color, alpha);
		}
	}

	R_PushConstants (cbx, VK_SHADER_STAGE_ALL_GRAPHICS, 0, 16 * sizeof (float), vulkan_globals.view_projection_matrix);
}

/*
=============
R_DrawIndirectBrushes
=============
*/
void R_DrawIndirectBrushes (cb_context_t *cbx, qboolean draw_water, qboolean draw_sky, int index)
{
	assert (!draw_water || !draw_sky);

	R_BeginDebugUtilsLabel (cbx, "Indirect Brushes");

	VkDeviceSize offset = 0;
	vulkan_globals.vk_cmd_bind_vertex_buffers (cbx->cb, 0, 1, &bmodel_vertex_buffer, &offset);
	vulkan_globals.vk_cmd_bind_index_buffer (cbx->cb, indirect_index_buffer, 0, VK_INDEX_TYPE_UINT32);

	if (!draw_sky)
	{
		vulkan_globals.vk_cmd_bind_descriptor_sets (
			cbx->cb, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_globals.world_pipeline_layout.handle, 2, 1, &nulltexture->descriptor_set, 0, NULL);
		if (r_lightmap_cheatsafe)
			vulkan_globals.vk_cmd_bind_descriptor_sets (
				cbx->cb, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_globals.world_pipeline_layout.handle, 0, 1, &greytexture->descriptor_set, 0, NULL);
	}

	gltexture_t *lastfullbright = NULL;
	gltexture_t *lastlightmap = NULL;
	gltexture_t *lasttexture = NULL;
	float		 last_alpha = FLT_MAX;
	float		 last_constant_factor = FLT_MAX;

	int part_size = (used_indirect_draws + NUM_WORLD_CBX - 1) / NUM_WORLD_CBX;
	int start = index < 0 ? 0 : part_size * index;
	int end = index < 0 ? used_indirect_draws : q_min (part_size * (index + 1), used_indirect_draws);

	for (int i = start; i < end; i++)
	{
		texture_t	*t = indirect_draws[i].texture;
		texture_t	*texture = R_TextureAnimation (t, 0);
		gltexture_t *gl_texture = draw_water ? texture->warpimage : texture->gltexture;

		if (!draw_sky && !gl_texture)
			continue;
		if (draw_water != (texture->name[0] == '*')) // SURF_DRAWTURB is in surfaces only, but it's derived from the texture name
			continue;
		if (draw_sky != (!q_strncasecmp (texture->name, "sky", 3))) // SURF_DRAWSKY is in surfaces only, but it's derived from the texture name
			continue;

		if (!draw_sky && !r_lightmap_cheatsafe && lasttexture != gl_texture)
		{
			vulkan_globals.vk_cmd_bind_descriptor_sets (
				cbx->cb, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_globals.world_pipeline_layout.handle, 0, 1, &gl_texture->descriptor_set, 0, NULL);
			lasttexture = gl_texture;
		}

		qboolean	 fullbright_enabled = false;
		gltexture_t *fullbright;
		if (!draw_sky && gl_fullbrights.value && (fullbright = R_TextureAnimation (t, 0)->fullbright) && !r_lightmap_cheatsafe)
		{
			fullbright_enabled = true;
			if (lastfullbright != fullbright)
			{
				vulkan_globals.vk_cmd_bind_descriptor_sets (
					cbx->cb, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_globals.world_pipeline_layout.handle, 2, 1, &fullbright->descriptor_set, 0, NULL);
				lastfullbright = fullbright;
			}
		}

		float alpha = 1.0f;
		if (draw_water)
		{
			if (!(q_strncasecmp (texture->name, "*lava", 5))) // SURF_DRAWLAVA is in surfaces only, but it's derived from the texture name
				alpha = map_lavaalpha > 0 ? map_lavaalpha : map_fallbackalpha;
			else if (!(q_strncasecmp (texture->name, "*slime", 6))) // SURF_DRAWSLIME is in surfaces only, but it's derived from the texture name
				alpha = map_slimealpha > 0 ? map_slimealpha : map_fallbackalpha;
			else if (!(q_strncasecmp (texture->name, "*tele", 5))) // SURF_DRAWTELE is in surfaces only, but it's derived from the texture name
				alpha = map_telealpha > 0 ? map_telealpha : map_fallbackalpha;
			else
				alpha = map_wateralpha;

			if (alpha != last_alpha)
			{
				R_PushConstants (cbx, VK_SHADER_STAGE_ALL_GRAPHICS, 20 * sizeof (float), 1 * sizeof (float), &alpha);
				last_alpha = alpha;
			}
		}

		if (!draw_sky)
		{
			const qboolean alpha_test = texture->name[0] == '{'; // SURF_DRAWFENCE is in surfaces only, but it's derived from the texture name
			const qboolean alpha_blend = alpha < 1.0f;
			int			   pipeline_index =
				(fullbright_enabled ? 1 : 0) + (alpha_test ? 2 : 0) + (alpha_blend ? 4 : 0) + (vid_filter.value != 0 && vid_palettize.value != 0 ? 8 : 0);
			R_BindPipeline (cbx, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_globals.world_pipelines[pipeline_index]);

			qboolean use_zbias = INDIRECT_ZBIAS && gl_zfix.value && indirect_draws[i].is_bmodel;
			float	 constant_factor = 0.0f, slope_factor = 0.0f;
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
			if (last_constant_factor != constant_factor)
			{
				vkCmdSetDepthBias (cbx->cb, constant_factor, 0.0f, slope_factor);
				last_constant_factor = constant_factor;
			}

			const int	 lm_idx = indirect_draws[i].lightmap_idx;
			gltexture_t *lightmap_texture = (r_fullbright_cheatsafe || lm_idx < 0) ? greylightmap : lightmaps[lm_idx].texture;
			if (lastlightmap != lightmap_texture)
			{
				vulkan_globals.vk_cmd_bind_descriptor_sets (
					cbx->cb, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_globals.world_pipeline_layout.handle, 1, 1, &lightmap_texture->descriptor_set, 0, NULL);
				lastlightmap = lightmap_texture;
			}
		}

		vulkan_globals.vk_cmd_draw_indexed_indirect (cbx->cb, indirect_buffer, i * sizeof (VkDrawIndexedIndirectCommand), 1, 0);
	}

	R_EndDebugUtilsLabel (cbx);
}

/*
=============
R_DrawIndirectBrushes_ShowTris
=============
*/
void R_DrawIndirectBrushes_ShowTris (cb_context_t *cbx)
{
	R_BindPipeline (
		cbx, VK_PIPELINE_BIND_POINT_GRAPHICS,
		r_showtris.value == 1 ? vulkan_globals.showtris_indirect_pipeline : vulkan_globals.showtris_indirect_depth_test_pipeline);

	VkDeviceSize offset = 0;
	vulkan_globals.vk_cmd_bind_vertex_buffers (cbx->cb, 0, 1, &bmodel_vertex_buffer, &offset);
	vulkan_globals.vk_cmd_bind_index_buffer (cbx->cb, indirect_index_buffer, 0, VK_INDEX_TYPE_UINT32);

	if (vulkan_globals.multi_draw_indirect)
		vulkan_globals.vk_cmd_draw_indexed_indirect (cbx->cb, indirect_buffer, 0, used_indirect_draws, sizeof (VkDrawIndexedIndirectCommand));
	else
		for (int i = 0; i < used_indirect_draws; i++)
			vulkan_globals.vk_cmd_draw_indexed_indirect (cbx->cb, indirect_buffer, i * sizeof (VkDrawIndexedIndirectCommand), 1, 0);
}

/*
=============================================================

	LIGHTMAPS

=============================================================
*/

/*
================
R_RenderDynamicLightmaps
called during rendering
================
*/
void R_RenderDynamicLightmaps (msurface_t *fa)
{
	byte	 *base;
	int		  maps;
	glRect_t *theRect;
	int		  smax, tmax;

	if (fa->flags & SURF_DRAWTILED) // johnfitz -- not a lightmapped surface
		return;

	// check for lightmap modification
	for (maps = 0; maps < MAXLIGHTMAPS && fa->styles[maps] != 255; maps++)
		if (d_lightstylevalue[fa->styles[maps]] != fa->cached_light[maps])
			goto dynamic;

	if (fa->dlightframe == r_framecount // dynamic this frame
		|| fa->cached_dlight)			// dynamic previously
	{
	dynamic:
		if (r_dynamic.value)
		{
			struct lightmap_s *lm = &lightmaps[fa->lightmaptexturenum];
			lm->modified[Tasks_GetWorkerIndex ()] = true;
			theRect = &lm->rectchange;
			if (fa->light_t < theRect->t)
			{
				if (theRect->h)
					theRect->h += theRect->t - fa->light_t;
				theRect->t = fa->light_t;
			}
			if (fa->light_s < theRect->l)
			{
				if (theRect->w)
					theRect->w += theRect->l - fa->light_s;
				theRect->l = fa->light_s;
			}
			smax = (fa->extents[0] >> 4) + 1;
			tmax = (fa->extents[1] >> 4) + 1;
			if ((theRect->w + theRect->l) < (fa->light_s + smax))
				theRect->w = (fa->light_s - theRect->l) + smax;
			if ((theRect->h + theRect->t) < (fa->light_t + tmax))
				theRect->h = (fa->light_t - theRect->t) + tmax;
			base = lm->data;
			base += fa->light_t * LMBLOCK_WIDTH * LIGHTMAP_BYTES + fa->light_s * LIGHTMAP_BYTES;
			R_BuildLightMap (fa, base, LMBLOCK_WIDTH * LIGHTMAP_BYTES);
		}
	}
}

/*
========================
AllocBlock -- returns a texture number and the position inside it
========================
*/
static int AllocBlock (int w, int h, int *x, int *y)
{
	int i, j;
	int texnum;

	for (texnum = last_lightmap_allocated; texnum < MAX_SANITY_LIGHTMAPS; texnum++)
	{
		if (texnum == lightmap_count)
		{
			lightmap_count++;
			lightmaps = (struct lightmap_s *)Mem_Realloc (lightmaps, sizeof (*lightmaps) * lightmap_count);
			memset (&lightmaps[texnum], 0, sizeof (lightmaps[texnum]));
			lightmaps[texnum].data = (byte *)Mem_Alloc (LIGHTMAP_BYTES * LMBLOCK_WIDTH * LMBLOCK_HEIGHT);
			for (i = 0; i < MAXLIGHTMAPS * 3 / 4; ++i)
				lightmaps[texnum].lightstyle_data[i] = (byte *)Mem_Alloc (LIGHTMAP_BYTES * LMBLOCK_WIDTH * LMBLOCK_HEIGHT);
			lightmaps[texnum].surface_indices = (uint32_t *)Mem_Alloc (sizeof (uint32_t) * LMBLOCK_WIDTH * LMBLOCK_HEIGHT);
			memset (lightmaps[texnum].surface_indices, 0xFF, 4 * LMBLOCK_WIDTH * LMBLOCK_HEIGHT);
			lightmaps[texnum].workgroup_bounds = (lm_compute_workgroup_bounds_t *)Mem_Alloc (WORKGROUP_BOUNDS_BUFFER_SIZE);
			for (i = 0; i < (LMBLOCK_WIDTH / 8) * (LMBLOCK_HEIGHT / 8); ++i)
			{
				for (j = 0; j < 3; ++j)
				{
					lightmaps[texnum].workgroup_bounds[i].mins[j] = FLT_MAX;
					lightmaps[texnum].workgroup_bounds[i].maxs[j] = -FLT_MAX;
				}
			}
			for (j = 0; j < 3; ++j)
			{
				lightmaps[texnum].global_bounds.mins[j] = FLT_MAX;
				lightmaps[texnum].global_bounds.maxs[j] = -FLT_MAX;
			}
			memset (lightmaps[texnum].cached_light, -1, sizeof (lightmaps[texnum].cached_light));
			memset (used_columns[texnum], 0, sizeof (used_columns[texnum]));
			last_lightmap_allocated = texnum;
		}

		i = SizeToBin (w);
		if (columns[i] < 0 || rows[i] + h - shelf_idx[i] * SHELF_HEIGHT > SHELF_HEIGHT) // need another shelf
		{
			while (used_columns[lightmap_idx[i]][shelf_idx[i]] + BinToSize (i) > LMBLOCK_WIDTH)
			{
				if (++shelf_idx[i] < SHELVES)
					continue;
				shelf_idx[i] = 0;
				if (++lightmap_idx[i] == lightmap_count)
					break;
			}
			if (lightmap_idx[i] == lightmap_count) // need another lightmap
				continue;

			columns[i] = used_columns[lightmap_idx[i]][shelf_idx[i]];
			used_columns[lightmap_idx[i]][shelf_idx[i]] += BinToSize (i);
			rows[i] = shelf_idx[i] * SHELF_HEIGHT;
		}
		*x = columns[i];
		*y = rows[i];
		rows[i] += h;
		return lightmap_idx[i];
	}

	Sys_Error ("AllocBlock: full");
	return 0; // johnfitz -- shut up compiler
}

mvertex_t *r_pcurrentvertbase;
qmodel_t  *currentmodel;

int nColinElim;

/*
===============
R_AssignSurfaceIndex
===============
*/
static void R_AssignSurfaceIndex (msurface_t *surf, uint32_t index, uint32_t *surface_indices, int stride)
{
	int width = (surf->extents[0] >> 4) + 1;
	int height = (surf->extents[1] >> 4) + 1;

	stride -= width;
	while (height-- > 0)
	{
		int i;
		for (i = 0; i < width; i++)
		{
			*surface_indices++ = index;
		}
		surface_indices += stride;
	}
}

/*
===============
R_FillLightstyleTexture
===============
*/
static void R_FillLightstyleTextures (msurface_t *surf, byte **lightstyles, int stride)
{
	int	  smax, tmax;
	byte *lightmap;
	int	  maps;

	smax = (surf->extents[0] >> 4) + 1;
	tmax = (surf->extents[1] >> 4) + 1;
	lightmap = surf->samples;

	// add all the lightmaps
	if (lightmap)
	{
		for (maps = 0; maps < MAXLIGHTMAPS && surf->styles[maps] != 255; ++maps)
		{
			lightmaps[surf->lightmaptexturenum].used_lightstyles[surf->styles[maps]] = true;
			if (maps % 4 != 3)
			{
				byte *outptr = lightstyles[maps / 4 * 3 + maps % 4];
				int	  height = tmax;
				while (height-- > 0)
				{
					int i;
					for (i = 0; i < smax; i++)
					{
						*outptr++ = *lightmap++;
						*outptr++ = *lightmap++;
						*outptr++ = *lightmap++;
						*outptr++ = 0;
					}
					outptr += stride - smax * LIGHTMAP_BYTES;
				}
			}
			else
			{
				for (int height = 0; height < tmax; ++height)
					for (int i = 0; i < smax; i++)
					{
						lightstyles[maps / 4 + 0][i * 4 + 3 + stride * height] = *lightmap++;
						lightstyles[maps / 4 + 1][i * 4 + 3 + stride * height] = *lightmap++;
						lightstyles[maps / 4 + 2][i * 4 + 3 + stride * height] = *lightmap++;
					}
			}
		}
	}
}

/*
===============
R_AssignWorkgroupBounds

FIXME: This doesn't account for moving bmodels
===============
*/
static void R_AssignWorkgroupBounds (msurface_t *surf)
{
	lm_compute_workgroup_bounds_t *bounds = lightmaps[surf->lightmaptexturenum].workgroup_bounds;
	lm_compute_workgroup_bounds_t *global_bounds = &lightmaps[surf->lightmaptexturenum].global_bounds;
	const int					   smax = (surf->extents[0] >> 4) + 1;
	const int					   tmax = (surf->extents[1] >> 4) + 1;

	lm_compute_workgroup_bounds_t surf_bounds;
	for (int i = 0; i < 3; ++i)
	{
		surf_bounds.mins[i] = FLT_MAX;
		surf_bounds.maxs[i] = -FLT_MAX;
	}

	float *v = surf->polys->verts[0];
	for (int i = 0; i < surf->polys->numverts; ++i, v += VERTEXSIZE)
	{
		for (int j = 0; j < 3; ++j)
		{
			if (v[j] < surf_bounds.mins[j])
				surf_bounds.mins[j] = v[j];
			if (v[j] > surf_bounds.maxs[j])
				surf_bounds.maxs[j] = v[j];
		}
	}

	for (int s = 0; s < smax; ++s)
	{
		for (int t = 0; t < tmax; ++t)
		{
			const int					   workgroup_x = (surf->light_s + s) / 8;
			const int					   workgroup_y = (surf->light_t + t) / 8;
			lm_compute_workgroup_bounds_t *workgroup_bounds = bounds + workgroup_x + (workgroup_y * (LMBLOCK_WIDTH / 8));
			for (int i = 0; i < 3; ++i)
			{
				if (surf_bounds.mins[i] < workgroup_bounds->mins[i])
					workgroup_bounds->mins[i] = surf_bounds.mins[i];
				if (surf_bounds.maxs[i] > workgroup_bounds->maxs[i])
					workgroup_bounds->maxs[i] = surf_bounds.maxs[i];
				if (surf_bounds.mins[i] < global_bounds->mins[i])
					global_bounds->mins[i] = surf_bounds.mins[i];
				if (surf_bounds.maxs[i] > global_bounds->maxs[i])
					global_bounds->maxs[i] = surf_bounds.maxs[i];
			}
		}
	}
}

static void UpdateIndirectStructs (msurface_t *surf, qboolean is_bmodel)
{
	static int last;
	int		   i;
	if (last < used_indirect_draws && indirect_draws[last].lightmap_idx == surf->lightmaptexturenum && indirect_draws[last].texture == surf->texinfo->texture &&
		indirect_draws[last].is_bmodel == is_bmodel)
	{
		surf->indirect_idx = last;
		indirect_draws[last].max_indices += 3 * (surf->numedges - 2);
		return;
	}
	for (i = 0; i < used_indirect_draws; i++)
	{
		if (indirect_draws[i].lightmap_idx == surf->lightmaptexturenum && indirect_draws[i].texture == surf->texinfo->texture &&
			indirect_draws[i].is_bmodel == is_bmodel)
		{
			surf->indirect_idx = last = i;
			indirect_draws[i].max_indices += 3 * (surf->numedges - 2);
			return;
		}
	}
	if (i == MAX_INDIRECT_DRAWS - 1)
	{
		indirect_ready = false;
		return;
	}
	++used_indirect_draws;
	surf->indirect_idx = last = i;
	indirect_draws[i].texture = surf->texinfo->texture;
	indirect_draws[i].lightmap_idx = surf->lightmaptexturenum;
	indirect_draws[i].is_bmodel = is_bmodel;
	indirect_draws[i].max_indices = 3 * (surf->numedges - 2);
}

static void PrepareIndirectDraws ()
{
	int total_indices = 0;
	for (int i = 0; i < used_indirect_draws; i++)
	{
		initial_indirect_buffer[i].indexCount = 0;
		initial_indirect_buffer[i].instanceCount = 1;
		initial_indirect_buffer[i].firstIndex = total_indices;
		initial_indirect_buffer[i].vertexOffset = 0;
		initial_indirect_buffer[i].firstInstance = 0;
		total_indices += indirect_draws[i].max_indices;
	}
}

/*
========================
GL_CreateSurfaceLightmap
========================
*/
static void GL_CreateSurfaceLightmap (msurface_t *surf, uint32_t surface_index)
{
	int		  i;
	int		  smax, tmax;
	byte	 *base;
	byte	 *lightstyles[MAXLIGHTMAPS * 3 / 4];
	uint32_t *surface_indices;

	if (surf->flags & SURF_DRAWTILED)
	{
		surf->lightmaptexturenum = -1;
		return;
	}

	smax = (surf->extents[0] >> 4) + 1;
	tmax = (surf->extents[1] >> 4) + 1;

	surf->lightmaptexturenum = AllocBlock (smax, tmax, &surf->light_s, &surf->light_t);
	base = lightmaps[surf->lightmaptexturenum].data;
	base += (surf->light_t * LMBLOCK_WIDTH + surf->light_s) * LIGHTMAP_BYTES;
	R_BuildLightMap (surf, base, LMBLOCK_WIDTH * LIGHTMAP_BYTES);

	surface_indices = lightmaps[surf->lightmaptexturenum].surface_indices;
	surface_indices += (surf->light_t * LMBLOCK_WIDTH + surf->light_s);
	R_AssignSurfaceIndex (surf, surface_index, surface_indices, LMBLOCK_WIDTH);

	for (i = 0; i < MAXLIGHTMAPS * 3 / 4; ++i)
	{
		lightstyles[i] = lightmaps[surf->lightmaptexturenum].lightstyle_data[i];
		lightstyles[i] += (surf->light_t * LMBLOCK_WIDTH + surf->light_s) * LIGHTMAP_BYTES;
	}
	R_FillLightstyleTextures (surf, lightstyles, LMBLOCK_WIDTH * LIGHTMAP_BYTES);
}

/*
================
BuildSurfaceDisplayList -- called at level load time
================
*/
void BuildSurfaceDisplayList (msurface_t *fa)
{
	int		  i, lindex, lnumverts;
	medge_t	 *pedges, *r_pedge;
	float	 *vec;
	float	  s, t, s0, t0, sdiv, tdiv;
	glpoly_t *poly;
	float	 *poly_vert;

	// reconstruct the polygon
	pedges = currentmodel->edges;
	lnumverts = fa->numedges;

	//
	// draw texture
	//
	poly = (glpoly_t *)Mem_Alloc (sizeof (glpoly_t) + (lnumverts - 4) * VERTEXSIZE * sizeof (float));
	poly->next = fa->polys;
	fa->polys = poly;
	poly->numverts = lnumverts;

	if (fa->flags & SURF_DRAWTURB)
	{
		// match Mod_PolyForUnlitSurface
		s0 = t0 = 0.f;
		sdiv = tdiv = 128.f;
	}
	else
	{
		s0 = fa->texinfo->vecs[0][3];
		t0 = fa->texinfo->vecs[1][3];
		sdiv = fa->texinfo->texture->width;
		tdiv = fa->texinfo->texture->height;
	}

	for (i = 0; i < lnumverts; i++)
	{
		lindex = currentmodel->surfedges[fa->firstedge + i];

		if (lindex > 0)
		{
			r_pedge = &pedges[lindex];
			vec = r_pcurrentvertbase[r_pedge->v[0]].position;
		}
		else
		{
			r_pedge = &pedges[-lindex];
			vec = r_pcurrentvertbase[r_pedge->v[1]].position;
		}
		s = DotProduct (vec, fa->texinfo->vecs[0]) + s0;
		s /= sdiv;

		t = DotProduct (vec, fa->texinfo->vecs[1]) + t0;
		t /= tdiv;

		poly_vert = &poly->verts[0][0] + (i * VERTEXSIZE);
		VectorCopy (vec, poly_vert);
		poly_vert[3] = s;
		poly_vert[4] = t;

		// Q64 RERELEASE texture shift
		if (fa->texinfo->texture->shift > 0)
		{
			poly_vert[3] /= (2 * fa->texinfo->texture->shift);
			poly_vert[4] /= (2 * fa->texinfo->texture->shift);
		}

		//
		// lightmap texture coordinates
		//
		s = DotProduct (vec, fa->texinfo->vecs[0]) + fa->texinfo->vecs[0][3];
		s -= fa->texturemins[0];
		s += fa->light_s * 16;
		s += 8;
		s /= LMBLOCK_WIDTH * 16; // fa->texinfo->texture->width;

		t = DotProduct (vec, fa->texinfo->vecs[1]) + fa->texinfo->vecs[1][3];
		t -= fa->texturemins[1];
		t += fa->light_t * 16;
		t += 8;
		t /= LMBLOCK_HEIGHT * 16; // fa->texinfo->texture->height;

		poly_vert[5] = s;
		poly_vert[6] = t;
	}

	// johnfitz -- removed gl_keeptjunctions code

	poly->numverts = lnumverts;
}

/*
==================
R_AllocateLightmapComputeBuffers
==================
*/
void R_AllocateLightmapComputeBuffers ()
{
	VkResult err;
	{
		size_t buffer_size = MAX_LIGHTSTYLES * sizeof (float) * 2;
		COMPILE_TIME_ASSERT (lightstyles_buffer_alignment, ((MAX_LIGHTSTYLES * sizeof (float)) % 256) == 0);

		Sys_Printf ("Allocating lightstyles buffer (%u KB)\n", (int)buffer_size / 1024);

		VkBufferCreateInfo buffer_create_info;
		memset (&buffer_create_info, 0, sizeof (buffer_create_info));
		buffer_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		buffer_create_info.size = buffer_size;
		buffer_create_info.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;

		err = vkCreateBuffer (vulkan_globals.device, &buffer_create_info, NULL, &lightstyles_scales_buffer);
		if (err != VK_SUCCESS)
			Sys_Error ("vkCreateBuffer failed");
		GL_SetObjectName ((uint64_t)lightstyles_scales_buffer, VK_OBJECT_TYPE_BUFFER, "Lightstyles Buffer");

		VkMemoryRequirements memory_requirements;
		vkGetBufferMemoryRequirements (vulkan_globals.device, lightstyles_scales_buffer, &memory_requirements);

		VkMemoryAllocateInfo memory_allocate_info;
		memset (&memory_allocate_info, 0, sizeof (memory_allocate_info));
		memory_allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		memory_allocate_info.allocationSize = memory_requirements.size;
		memory_allocate_info.memoryTypeIndex =
			GL_MemoryTypeFromProperties (memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, VK_MEMORY_PROPERTY_HOST_CACHED_BIT);

		Atomic_IncrementUInt32 (&num_vulkan_misc_allocations);
		R_AllocateVulkanMemory (&lightstyles_scales_buffer_memory, &memory_allocate_info, VULKAN_MEMORY_TYPE_HOST);
		GL_SetObjectName ((uint64_t)lightstyles_scales_buffer_memory.handle, VK_OBJECT_TYPE_DEVICE_MEMORY, "Lightstyles Buffer");

		err = vkBindBufferMemory (vulkan_globals.device, lightstyles_scales_buffer, lightstyles_scales_buffer_memory.handle, 0);
		if (err != VK_SUCCESS)
			Sys_Error ("vkBindBufferMemory failed");

		err = vkMapMemory (vulkan_globals.device, lightstyles_scales_buffer_memory.handle, 0, buffer_size, 0, (void **)&lightstyles_scales_buffer_mapped);
		if (err != VK_SUCCESS)
			Sys_Error ("vkMapMemory failed");
	}

	{
		size_t buffer_size = MAX_DLIGHTS * sizeof (lm_compute_light_t) * 2;
		COMPILE_TIME_ASSERT (light_buffer_alignment, ((MAX_DLIGHTS * sizeof (lm_compute_light_t)) % 256) == 0);

		Sys_Printf ("Allocating lights buffer (%u KB)\n", (int)buffer_size / 1024);

		VkBufferCreateInfo buffer_create_info;
		memset (&buffer_create_info, 0, sizeof (buffer_create_info));
		buffer_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		buffer_create_info.size = buffer_size;
		buffer_create_info.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;

		err = vkCreateBuffer (vulkan_globals.device, &buffer_create_info, NULL, &lights_buffer);
		if (err != VK_SUCCESS)
			Sys_Error ("vkCreateBuffer failed");
		GL_SetObjectName ((uint64_t)lights_buffer, VK_OBJECT_TYPE_BUFFER, "Lights Buffer");

		VkMemoryRequirements memory_requirements;
		vkGetBufferMemoryRequirements (vulkan_globals.device, lights_buffer, &memory_requirements);

		VkMemoryAllocateInfo memory_allocate_info;
		memset (&memory_allocate_info, 0, sizeof (memory_allocate_info));
		memory_allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		memory_allocate_info.allocationSize = memory_requirements.size;
		memory_allocate_info.memoryTypeIndex =
			GL_MemoryTypeFromProperties (memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, VK_MEMORY_PROPERTY_HOST_CACHED_BIT);

		Atomic_IncrementUInt32 (&num_vulkan_misc_allocations);
		R_AllocateVulkanMemory (&lights_buffer_memory, &memory_allocate_info, VULKAN_MEMORY_TYPE_HOST);
		GL_SetObjectName ((uint64_t)lights_buffer_memory.handle, VK_OBJECT_TYPE_DEVICE_MEMORY, "Lights Buffer");

		err = vkBindBufferMemory (vulkan_globals.device, lights_buffer, lights_buffer_memory.handle, 0);
		if (err != VK_SUCCESS)
			Sys_Error ("vkBindBufferMemory failed");

		err = vkMapMemory (vulkan_globals.device, lights_buffer_memory.handle, 0, buffer_size, 0, (void **)&lights_buffer_mapped);
		if (err != VK_SUCCESS)
			Sys_Error ("vkMapMemory failed");
	}
}

/*
==================
GL_AllocateSurfaceDataBuffer
==================
*/
static lm_compute_surface_data_t *GL_AllocateSurfaceDataBuffer ()
{
	VkResult err;
	size_t	 buffer_size = num_surfaces * sizeof (lm_compute_surface_data_t);

	if (surface_data_buffer != VK_NULL_HANDLE)
	{
		vkDestroyBuffer (vulkan_globals.device, surface_data_buffer, NULL);
		R_FreeVulkanMemory (&surface_data_buffer_memory);
		Atomic_DecrementUInt32 (&num_vulkan_misc_allocations);
	}

	Sys_Printf ("Allocating lightmap compute surface data (%u KB)\n", (int)buffer_size / 1024);

	VkBufferCreateInfo buffer_create_info;
	memset (&buffer_create_info, 0, sizeof (buffer_create_info));
	buffer_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	buffer_create_info.size = buffer_size;
	buffer_create_info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

	err = vkCreateBuffer (vulkan_globals.device, &buffer_create_info, NULL, &surface_data_buffer);
	if (err != VK_SUCCESS)
		Sys_Error ("vkCreateBuffer failed");
	GL_SetObjectName ((uint64_t)surface_data_buffer, VK_OBJECT_TYPE_BUFFER, "Lightmap Compute Surface Data Buffer");

	VkMemoryRequirements memory_requirements;
	vkGetBufferMemoryRequirements (vulkan_globals.device, surface_data_buffer, &memory_requirements);

	VkMemoryAllocateInfo memory_allocate_info;
	memset (&memory_allocate_info, 0, sizeof (memory_allocate_info));
	memory_allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	memory_allocate_info.allocationSize = memory_requirements.size;
	memory_allocate_info.memoryTypeIndex = GL_MemoryTypeFromProperties (memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0);

	Atomic_IncrementUInt32 (&num_vulkan_misc_allocations);
	R_AllocateVulkanMemory (&surface_data_buffer_memory, &memory_allocate_info, VULKAN_MEMORY_TYPE_DEVICE);
	GL_SetObjectName ((uint64_t)surface_data_buffer_memory.handle, VK_OBJECT_TYPE_DEVICE_MEMORY, "Lightmap Compute Surface Data Buffer");

	err = vkBindBufferMemory (vulkan_globals.device, surface_data_buffer, surface_data_buffer_memory.handle, 0);
	if (err != VK_SUCCESS)
		Sys_Error ("vkBindBufferMemory failed");

	VkCommandBuffer			   command_buffer;
	VkBuffer				   staging_buffer;
	int						   staging_offset;
	lm_compute_surface_data_t *staging_mem = (lm_compute_surface_data_t *)R_StagingAllocate (buffer_size, 1, &command_buffer, &staging_buffer, &staging_offset);

	VkBufferCopy region;
	region.srcOffset = staging_offset;
	region.dstOffset = 0;
	region.size = buffer_size;
	vkCmdCopyBuffer (command_buffer, staging_buffer, surface_data_buffer, 1, &region);

	return staging_mem;
}

/*
==================
GL_AllocateIndirectBuffer
==================
*/
static VkDrawIndexedIndirectCommand *GL_AllocateIndirectBuffer (int num_used_indirect_draws)
{
	VkResult err;
	size_t	 buffer_size = num_used_indirect_draws * sizeof (VkDrawIndexedIndirectCommand);

	if (indirect_buffer != VK_NULL_HANDLE)
	{
		vkDestroyBuffer (vulkan_globals.device, indirect_buffer, NULL);
		R_FreeVulkanMemory (&indirect_buffer_memory);
		Atomic_DecrementUInt32 (&num_vulkan_misc_allocations);
	}

	Sys_Printf ("Allocating indirect draw data (%u KB, %d draws)\n", (int)buffer_size / 1024, num_used_indirect_draws);

	VkBufferCreateInfo buffer_create_info;
	memset (&buffer_create_info, 0, sizeof (buffer_create_info));
	buffer_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	buffer_create_info.size = buffer_size;
	buffer_create_info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;

	err = vkCreateBuffer (vulkan_globals.device, &buffer_create_info, NULL, &indirect_buffer);
	if (err != VK_SUCCESS)
		Sys_Error ("vkCreateBuffer failed");
	GL_SetObjectName ((uint64_t)indirect_buffer, VK_OBJECT_TYPE_BUFFER, "Indirect Draw Data Buffer");

	VkMemoryRequirements memory_requirements;
	vkGetBufferMemoryRequirements (vulkan_globals.device, indirect_buffer, &memory_requirements);

	VkMemoryAllocateInfo memory_allocate_info;
	memset (&memory_allocate_info, 0, sizeof (memory_allocate_info));
	memory_allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	memory_allocate_info.allocationSize = memory_requirements.size;
	memory_allocate_info.memoryTypeIndex = GL_MemoryTypeFromProperties (memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0);

	Atomic_IncrementUInt32 (&num_vulkan_misc_allocations);
	R_AllocateVulkanMemory (&indirect_buffer_memory, &memory_allocate_info, VULKAN_MEMORY_TYPE_DEVICE);
	GL_SetObjectName ((uint64_t)indirect_buffer_memory.handle, VK_OBJECT_TYPE_DEVICE_MEMORY, "Indirect Draw Data Buffer");

	err = vkBindBufferMemory (vulkan_globals.device, indirect_buffer, indirect_buffer_memory.handle, 0);
	if (err != VK_SUCCESS)
		Sys_Error ("vkBindBufferMemory failed");

	VkCommandBuffer				  command_buffer;
	VkBuffer					  staging_buffer;
	int							  staging_offset;
	VkDrawIndexedIndirectCommand *staging_mem =
		(VkDrawIndexedIndirectCommand *)R_StagingAllocate (buffer_size, 1, &command_buffer, &staging_buffer, &staging_offset);

	VkBufferCopy region;
	region.srcOffset = staging_offset;
	region.dstOffset = 0;
	region.size = buffer_size;
	vkCmdCopyBuffer (command_buffer, staging_buffer, indirect_buffer, 1, &region);

	return staging_mem;
}

/*
==================
GL_AllocateWorkgroupBoundsBuffers
==================
*/
static void GL_AllocateWorkgroupBoundsBuffers ()
{
	VkResult err;

	if (workgroup_bounds_buffer_memory.handle != VK_NULL_HANDLE)
	{
		R_FreeVulkanMemory (&workgroup_bounds_buffer_memory);
		Atomic_DecrementUInt32 (&num_vulkan_misc_allocations);
	}

	for (int i = 0; i < lightmap_count; i++)
	{
		VkBufferCreateInfo buffer_create_info;
		memset (&buffer_create_info, 0, sizeof (buffer_create_info));
		buffer_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		buffer_create_info.size = WORKGROUP_BOUNDS_BUFFER_SIZE;
		buffer_create_info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

		err = vkCreateBuffer (vulkan_globals.device, &buffer_create_info, NULL, &lightmaps[i].workgroup_bounds_buffer);
		if (err != VK_SUCCESS)
			Sys_Error ("vkCreateBuffer failed");
		GL_SetObjectName ((uint64_t)lightmaps[i].workgroup_bounds_buffer, VK_OBJECT_TYPE_BUFFER, "Workgroup Bounds Buffer");
	}

	int aligned_size = 0;
	if (lightmap_count > 0)
	{
		VkMemoryRequirements memory_requirements;
		vkGetBufferMemoryRequirements (vulkan_globals.device, lightmaps[0].workgroup_bounds_buffer, &memory_requirements);

		const int align_mod = memory_requirements.size % memory_requirements.alignment;
		aligned_size = ((memory_requirements.size % memory_requirements.alignment) == 0)
						   ? memory_requirements.size
						   : (memory_requirements.size + memory_requirements.alignment - align_mod);

		VkMemoryAllocateInfo memory_allocate_info;
		memset (&memory_allocate_info, 0, sizeof (memory_allocate_info));
		memory_allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		memory_allocate_info.allocationSize = lightmap_count * aligned_size;
		memory_allocate_info.memoryTypeIndex = GL_MemoryTypeFromProperties (memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0);

		Atomic_IncrementUInt32 (&num_vulkan_misc_allocations);
		R_AllocateVulkanMemory (&workgroup_bounds_buffer_memory, &memory_allocate_info, VULKAN_MEMORY_TYPE_DEVICE);
		GL_SetObjectName ((uint64_t)workgroup_bounds_buffer_memory.handle, VK_OBJECT_TYPE_DEVICE_MEMORY, "Workgroup Bounds Buffer Memory");
	}

	for (int i = 0; i < lightmap_count; i++)
	{
		err = vkBindBufferMemory (vulkan_globals.device, lightmaps[i].workgroup_bounds_buffer, workgroup_bounds_buffer_memory.handle, aligned_size * i);
		if (err != VK_SUCCESS)
			Sys_Error ("vkBindBufferMemory failed");
	}
}

/*
===============
R_InitIndirectIndexBuffer
===============
*/
static void R_InitIndirectIndexBuffer (uint32_t size)
{
	if (indirect_index_buffer != VK_NULL_HANDLE)
	{
		vkDestroyBuffer (vulkan_globals.device, indirect_index_buffer, NULL);
		R_FreeVulkanMemory (&indirect_index_buffer_memory);
		Atomic_DecrementUInt32 (&num_vulkan_misc_allocations);
	}

	Sys_Printf ("Allocating indirect IBs (%u KB)\n", size / 1024);

	VkResult err;

	VkBufferCreateInfo buffer_create_info;
	memset (&buffer_create_info, 0, sizeof (buffer_create_info));
	buffer_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	buffer_create_info.size = size;
	buffer_create_info.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

	err = vkCreateBuffer (vulkan_globals.device, &buffer_create_info, NULL, &indirect_index_buffer);
	if (err != VK_SUCCESS)
		Sys_Error ("vkCreateBuffer failed");

	GL_SetObjectName ((uint64_t)indirect_index_buffer, VK_OBJECT_TYPE_BUFFER, "Indirect Index Buffer");

	VkMemoryRequirements memory_requirements;
	vkGetBufferMemoryRequirements (vulkan_globals.device, indirect_index_buffer, &memory_requirements);

	VkMemoryAllocateInfo memory_allocate_info;
	memset (&memory_allocate_info, 0, sizeof (memory_allocate_info));
	memory_allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	memory_allocate_info.allocationSize = memory_requirements.size;
	memory_allocate_info.memoryTypeIndex = GL_MemoryTypeFromProperties (memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0);

	Atomic_IncrementUInt32 (&num_vulkan_misc_allocations);
	R_AllocateVulkanMemory (&indirect_index_buffer_memory, &memory_allocate_info, VULKAN_MEMORY_TYPE_DEVICE);
	GL_SetObjectName ((uint64_t)indirect_index_buffer_memory.handle, VK_OBJECT_TYPE_DEVICE_MEMORY, "Indirect Index Buffer");

	err = vkBindBufferMemory (vulkan_globals.device, indirect_index_buffer, indirect_index_buffer_memory.handle, 0);
	if (err != VK_SUCCESS)
		Sys_Error ("vkBindBufferMemory failed");
}

/*
===============
R_InitVisibilityBuffers (one bit per surface)
===============
*/
static void R_InitVisibilityBuffers (uint32_t size)
{
	if (dyn_visibility_buffer != VK_NULL_HANDLE)
	{
		vkDestroyBuffer (vulkan_globals.device, dyn_visibility_buffer, NULL);
		R_FreeVulkanMemory (&dyn_visibility_buffer_memory);
		Atomic_DecrementUInt32 (&num_vulkan_misc_allocations);
	}

	size = (size + 255) / 256 * 256 * 2;

	Sys_Printf ("Allocating visibility buffers (%u KB)\n", size / 1024);

	VkResult err;

	VkBufferCreateInfo buffer_create_info;
	memset (&buffer_create_info, 0, sizeof (buffer_create_info));
	buffer_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	buffer_create_info.size = size;
	buffer_create_info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

	err = vkCreateBuffer (vulkan_globals.device, &buffer_create_info, NULL, &dyn_visibility_buffer);
	if (err != VK_SUCCESS)
		Sys_Error ("vkCreateBuffer failed");

	GL_SetObjectName ((uint64_t)dyn_visibility_buffer, VK_OBJECT_TYPE_BUFFER, "Dynamic Visibility Buffer");

	VkMemoryRequirements memory_requirements;
	vkGetBufferMemoryRequirements (vulkan_globals.device, dyn_visibility_buffer, &memory_requirements);

	VkMemoryAllocateInfo memory_allocate_info;
	memset (&memory_allocate_info, 0, sizeof (memory_allocate_info));
	memory_allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	memory_allocate_info.allocationSize = memory_requirements.size;
	memory_allocate_info.memoryTypeIndex =
		GL_MemoryTypeFromProperties (memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
	Atomic_IncrementUInt32 (&num_vulkan_misc_allocations);
	R_AllocateVulkanMemory (&dyn_visibility_buffer_memory, &memory_allocate_info, VULKAN_MEMORY_TYPE_HOST);
	GL_SetObjectName ((uint64_t)dyn_visibility_buffer_memory.handle, VK_OBJECT_TYPE_DEVICE_MEMORY, "Dynamic Visibility Buffer");

	err = vkBindBufferMemory (vulkan_globals.device, dyn_visibility_buffer, dyn_visibility_buffer_memory.handle, 0);
	if (err != VK_SUCCESS)
		Sys_Error ("vkBindBufferMemory failed");

	void *data;
	err = vkMapMemory (vulkan_globals.device, dyn_visibility_buffer_memory.handle, 0, memory_requirements.size, 0, &data);
	if (err != VK_SUCCESS)
		Sys_Error ("vkMapMemory failed");

	dyn_visibility_view = (unsigned char *)data;
	dyn_visibility_offset = size / 2;
}

/*
===============
R_UploadVisibility
===============
*/
static void R_UploadVisibility (byte *data, uint32_t size)
{
	memcpy (dyn_visibility_view + current_compute_buffer_index * dyn_visibility_offset, data, size);
}

/*
==================
GL_BuildLightmaps -- called at level load time

Builds the lightmap texture
with all the surfaces from all brush models
==================
*/
void GL_BuildLightmaps (void)
{
	char					   name[32];
	int						   i, j;
	uint32_t				   surface_index = 0;
	struct lightmap_s		  *lm;
	qmodel_t				  *m;
	lm_compute_surface_data_t *surface_data;
	msurface_t				  *surf;

	GL_WaitForDeviceIdle ();

	r_framecount = 1; // no dlightcache

	// Spike -- wipe out all the lightmap data (johnfitz -- the gltexture objects were already freed by Mod_ClearAll)
	for (i = 0; i < lightmap_count; i++)
	{
		Mem_Free (lightmaps[i].data);
		R_FreeDescriptorSet (lightmaps[i].descriptor_set, &vulkan_globals.lightmap_compute_set_layout);
		if (lightmaps[i].workgroup_bounds_buffer != VK_NULL_HANDLE)
			vkDestroyBuffer (vulkan_globals.device, lightmaps[i].workgroup_bounds_buffer, NULL);
	}

	Mem_Free (lightmaps);
	lightmaps = NULL;
	last_lightmap_allocated = 0;
	lightmap_count = 0;
	num_surfaces = 0;
	memset (columns, -1, sizeof (columns));
	memset (lightmap_idx, 0, sizeof (lightmap_idx));
	memset (shelf_idx, 0, sizeof (shelf_idx));
	used_indirect_draws = 0;
	indirect_ready = true;
	indirect_bmodel_start = INT_MAX;
	used_deps_data = 0;
	Mem_Free (brush_deps_data);

	for (i = 1; i < MAX_MODELS; ++i)
	{
		m = cl.model_precache[i];
		if (!m)
			break;
		if (m->name[0] == '*')
		{
			indirect_bmodel_start = q_min (indirect_bmodel_start, m->firstmodelsurface);
			continue;
		}
		num_surfaces += m->numsurfaces;
	}

	surface_data = GL_AllocateSurfaceDataBuffer ();

	R_StagingBeginCopy ();
	unsigned int varray_index = 0;
	for (j = 1; j < MAX_MODELS; j++)
	{
		m = cl.model_precache[j];
		if (!m)
			break;
		if (m->name[0] == '*')
			continue;
		r_pcurrentvertbase = m->vertexes;
		currentmodel = m;
		for (i = 0; i < m->numsurfaces; i++)
		{
			surf = &m->surfaces[i];
			if (!(surf->flags & SURF_DRAWTILED))
			{
				GL_CreateSurfaceLightmap (surf, surface_index);
				BuildSurfaceDisplayList (surf);
				R_AssignWorkgroupBounds (surf);
			}
			if (indirect_ready)
				UpdateIndirectStructs (surf, INDIRECT_ZBIAS && surface_index >= indirect_bmodel_start);

			lm_compute_surface_data_t *surf_data = &surface_data[surface_index];
			surf_data->packed_lightstyles = ((uint32_t)(surf->styles[0]) << 0) | ((uint32_t)(surf->styles[1]) << 8) | ((uint32_t)(surf->styles[2]) << 16) |
											((uint32_t)(surf->styles[3]) << 24);
			for (int k = 0; k < 3; ++k)
				surf_data->normal[k] = surf->plane->normal[k];
			surf_data->dist = surf->plane->dist;
			surf_data->packed_light_st_and_tex =
				(((surf->light_s) & 0xFF) << 24) | (((surf->light_t) & 0xFF) << 16) | surf->indirect_idx | !!(surf->flags & SURF_PLANEBACK) << 15;
			surf->vbo_firstvert = varray_index;
			surf_data->packed_vbo_offset_and_count = (surf->numedges << 24) | surf->vbo_firstvert;
			if (surf->numedges > 255 || varray_index > (1 << 24) - 1)
				indirect_ready = false;
			varray_index += surf->numedges;

			surf_data->packed_texturemins = (uint32_t)(surf->texturemins[0] + 32768) | ((uint32_t)(surf->texturemins[1] + 32768) << 16);
			Vector4Copy (surf->texinfo->vecs[0], surf_data->vecs[0]);
			Vector4Copy (surf->texinfo->vecs[1], surf_data->vecs[1]);

			surface_index += 1;
		}
	}

	R_StagingEndCopy ();

	if (indirect_ready)
	{
		PrepareIndirectDraws ();
		VkDrawIndexedIndirectCommand *hw_indirect_buffer = GL_AllocateIndirectBuffer (used_indirect_draws);
		R_StagingBeginCopy ();
		memcpy (hw_indirect_buffer, initial_indirect_buffer, used_indirect_draws * sizeof (VkDrawIndexedIndirectCommand));
		R_StagingEndCopy ();

		R_InitIndirectIndexBuffer ((initial_indirect_buffer[used_indirect_draws - 1].firstIndex + indirect_draws[used_indirect_draws - 1].max_indices) * 4);
		R_InitVisibilityBuffers ((cl.worldmodel->numsurfaces + 31) / 8);

		if (vulkan_globals.indirect_compute_desc_set != VK_NULL_HANDLE)
			R_FreeDescriptorSet (vulkan_globals.indirect_compute_desc_set, &vulkan_globals.indirect_compute_set_layout);
		vulkan_globals.indirect_compute_desc_set = R_AllocateDescriptorSet (&vulkan_globals.indirect_compute_set_layout);

		VkDescriptorBufferInfo indirect_draw_buffer_info;
		memset (&indirect_draw_buffer_info, 0, sizeof (indirect_draw_buffer_info));
		indirect_draw_buffer_info.buffer = indirect_buffer;
		indirect_draw_buffer_info.offset = 0;
		indirect_draw_buffer_info.range = VK_WHOLE_SIZE;

		VkDescriptorBufferInfo surfaces_buffer_info;
		memset (&surfaces_buffer_info, 0, sizeof (surfaces_buffer_info));
		surfaces_buffer_info.buffer = surface_data_buffer;
		surfaces_buffer_info.offset = 0;
		surfaces_buffer_info.range = num_surfaces * sizeof (lm_compute_surface_data_t);

		VkDescriptorBufferInfo visibility_buffer_info;
		memset (&visibility_buffer_info, 0, sizeof (visibility_buffer_info));
		visibility_buffer_info.buffer = dyn_visibility_buffer;
		visibility_buffer_info.offset = 0;
		visibility_buffer_info.range = VK_WHOLE_SIZE;

		VkDescriptorBufferInfo index_buffer_info;
		memset (&index_buffer_info, 0, sizeof (index_buffer_info));
		index_buffer_info.buffer = indirect_index_buffer;
		index_buffer_info.offset = 0;
		index_buffer_info.range = VK_WHOLE_SIZE;

		VkWriteDescriptorSet indirect_d[4];
		memset (indirect_d, 0, sizeof (indirect_d));

		indirect_d[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		indirect_d[0].dstBinding = 0;
		indirect_d[0].dstArrayElement = 0;
		indirect_d[0].descriptorCount = 1;
		indirect_d[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		indirect_d[0].dstSet = vulkan_globals.indirect_compute_desc_set;
		indirect_d[0].pBufferInfo = &indirect_draw_buffer_info;

		indirect_d[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		indirect_d[1].dstBinding = 1;
		indirect_d[1].dstArrayElement = 0;
		indirect_d[1].descriptorCount = 1;
		indirect_d[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		indirect_d[1].dstSet = vulkan_globals.indirect_compute_desc_set;
		indirect_d[1].pBufferInfo = &surfaces_buffer_info;

		indirect_d[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		indirect_d[2].dstBinding = 2;
		indirect_d[2].dstArrayElement = 0;
		indirect_d[2].descriptorCount = 1;
		indirect_d[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		indirect_d[2].dstSet = vulkan_globals.indirect_compute_desc_set;
		indirect_d[2].pBufferInfo = &visibility_buffer_info;

		indirect_d[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		indirect_d[3].dstBinding = 3;
		indirect_d[3].dstArrayElement = 0;
		indirect_d[3].descriptorCount = 1;
		indirect_d[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		indirect_d[3].dstSet = vulkan_globals.indirect_compute_desc_set;
		indirect_d[3].pBufferInfo = &index_buffer_info;

		vkUpdateDescriptorSets (vulkan_globals.device, 4, indirect_d, 0, NULL);

		for (i = 0; i < cl.worldmodel->numleafs; i++)
			calc_deps (NULL, &cl.worldmodel->leafs[i]);

		if (WATER_FIXED_ORDER)
		{
			cl.worldmodel->water_surfs = Mem_Alloc (8192 * sizeof (int));
			for (i = 0; i < cl.worldmodel->numsurfaces; i++)
				if (cl.worldmodel->surfaces[i].flags & SURF_DRAWTURB)
				{
					if (cl.worldmodel->used_water_surfs >= 8192 && !(cl.worldmodel->used_water_surfs & (cl.worldmodel->used_water_surfs - 1)))
						cl.worldmodel->water_surfs = Mem_Realloc (cl.worldmodel->water_surfs, cl.worldmodel->used_water_surfs * 2 * sizeof (int));
					cl.worldmodel->water_surfs[cl.worldmodel->used_water_surfs] = i;
					++cl.worldmodel->used_water_surfs;
				}
		}
	}
	else
		Con_Warning ("map exceeds indirect dispatch limits\n");

	for (j = 1; j < MAX_MODELS; j++)
	{
		m = cl.model_precache[j];
		if (!m)
			break;
		if (m->name[0] != '*')
			continue;
		calc_deps (m, NULL);
	}

	GL_AllocateWorkgroupBoundsBuffers ();

	//
	// upload all lightmaps that were filled
	//
	for (i = 0; i < lightmap_count; i++)
	{
		lm = &lightmaps[i];
		for (j = 0; j < TASKS_MAX_WORKERS; ++j)
			lm->modified[j] = 0;
		lm->rectchange.l = LMBLOCK_WIDTH;
		lm->rectchange.t = LMBLOCK_HEIGHT;
		lm->rectchange.w = 0;
		lm->rectchange.h = 0;

		q_snprintf (name, sizeof (name), "lightmap_%07i", i);
		lm->texture = TexMgr_LoadImage (
			cl.worldmodel, name, LMBLOCK_WIDTH, LMBLOCK_HEIGHT, SRC_LIGHTMAP, lm->data, "", (src_offset_t)lm->data, TEXPREF_LINEAR | TEXPREF_NOPICMIP);
		for (j = 0; j < MAXLIGHTMAPS * 3 / 4; ++j)
		{
			lm->lightstyle_textures[j] = TexMgr_LoadImage (
				cl.worldmodel, name, LMBLOCK_WIDTH, LMBLOCK_HEIGHT, SRC_RGBA, lm->lightstyle_data[j], "", (src_offset_t)lm->data,
				TEXPREF_NEAREST | TEXPREF_NOPICMIP);
			q_snprintf (name, sizeof (name), "lightstyle%d_%07i", j, i);
			SAFE_FREE (lm->lightstyle_data[j]);
		}

		q_snprintf (name, sizeof (name), "surfindices_%07i", i);
		lm->surface_indices_texture = TexMgr_LoadImage (
			cl.worldmodel, name, LMBLOCK_WIDTH, LMBLOCK_HEIGHT, SRC_SURF_INDICES, (byte *)lm->surface_indices, "", (src_offset_t)lm->surface_indices,
			TEXPREF_NEAREST | TEXPREF_NOPICMIP);
		SAFE_FREE (lm->surface_indices);
	}

	for (i = 0; i < lightmap_count; i++)
	{
		lm = &lightmaps[i];
		lm->descriptor_set = R_AllocateDescriptorSet (&vulkan_globals.lightmap_compute_set_layout);
		GL_SetObjectName ((uint64_t)lm->descriptor_set, VK_OBJECT_TYPE_DESCRIPTOR_SET, va ("%s compute desc set", name));

		{
			VkCommandBuffer command_buffer;
			VkBuffer		staging_buffer;
			int				staging_offset;
			byte		   *bounds_staging = R_StagingAllocate (WORKGROUP_BOUNDS_BUFFER_SIZE, 1, &command_buffer, &staging_buffer, &staging_offset);

			VkBufferCopy region;
			region.srcOffset = staging_offset;
			region.dstOffset = 0;
			region.size = WORKGROUP_BOUNDS_BUFFER_SIZE;
			vkCmdCopyBuffer (command_buffer, staging_buffer, lm->workgroup_bounds_buffer, 1, &region);

			R_StagingBeginCopy ();
			memcpy (bounds_staging, lm->workgroup_bounds, WORKGROUP_BOUNDS_BUFFER_SIZE);
			R_StagingEndCopy ();
			SAFE_FREE (lm->workgroup_bounds);
		}

		VkDescriptorImageInfo output_image_info;
		memset (&output_image_info, 0, sizeof (output_image_info));
		output_image_info.imageView = lm->texture->target_image_view;
		output_image_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

		VkDescriptorImageInfo surface_indices_image_info;
		memset (&surface_indices_image_info, 0, sizeof (surface_indices_image_info));
		surface_indices_image_info.imageView = lm->surface_indices_texture->image_view;
		surface_indices_image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		VkDescriptorImageInfo lightmap_images_infos[MAXLIGHTMAPS * 3 / 4];
		memset (&lightmap_images_infos, 0, sizeof (lightmap_images_infos));
		for (j = 0; j < MAXLIGHTMAPS * 3 / 4; ++j)
		{
			lightmap_images_infos[j].imageView = lm->lightstyle_textures[j]->image_view;
			lightmap_images_infos[j].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		}

		VkDescriptorBufferInfo surfaces_data_buffer_info;
		memset (&surfaces_data_buffer_info, 0, sizeof (surfaces_data_buffer_info));
		surfaces_data_buffer_info.buffer = surface_data_buffer;
		surfaces_data_buffer_info.offset = 0;
		surfaces_data_buffer_info.range = num_surfaces * sizeof (lm_compute_surface_data_t);

		VkDescriptorBufferInfo workgroup_bounds_buffer_info;
		memset (&workgroup_bounds_buffer_info, 0, sizeof (workgroup_bounds_buffer_info));
		workgroup_bounds_buffer_info.buffer = lm->workgroup_bounds_buffer;
		workgroup_bounds_buffer_info.offset = 0;
		workgroup_bounds_buffer_info.range = (LMBLOCK_WIDTH / 8) * (LMBLOCK_HEIGHT / 8) * sizeof (lm_compute_workgroup_bounds_t);

		VkDescriptorBufferInfo lightstyle_scales_buffer_info;
		memset (&lightstyle_scales_buffer_info, 0, sizeof (lightstyle_scales_buffer_info));
		lightstyle_scales_buffer_info.buffer = lightstyles_scales_buffer;
		lightstyle_scales_buffer_info.offset = 0;
		lightstyle_scales_buffer_info.range = MAX_LIGHTSTYLES * sizeof (float);

		VkDescriptorBufferInfo lights_buffer_info;
		memset (&lights_buffer_info, 0, sizeof (lights_buffer_info));
		lights_buffer_info.buffer = lights_buffer;
		lights_buffer_info.offset = 0;
		lights_buffer_info.range = MAX_DLIGHTS * sizeof (lm_compute_light_t);

		VkWriteDescriptorSet writes[7];
		memset (&writes, 0, sizeof (writes));

		writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[0].dstBinding = 0;
		writes[0].dstArrayElement = 0;
		writes[0].descriptorCount = 1;
		writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		writes[0].dstSet = lm->descriptor_set;
		writes[0].pImageInfo = &output_image_info;

		writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[1].dstBinding = 1;
		writes[1].dstArrayElement = 0;
		writes[1].descriptorCount = 1;
		writes[1].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
		writes[1].dstSet = lm->descriptor_set;
		writes[1].pImageInfo = &surface_indices_image_info;

		writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[2].dstBinding = 2;
		writes[2].dstArrayElement = 0;
		writes[2].descriptorCount = MAXLIGHTMAPS * 3 / 4;
		writes[2].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
		writes[2].dstSet = lm->descriptor_set;
		writes[2].pImageInfo = lightmap_images_infos;

		writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[3].dstBinding = 3;
		writes[3].dstArrayElement = 0;
		writes[3].descriptorCount = 1;
		writes[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		writes[3].dstSet = lm->descriptor_set;
		writes[3].pBufferInfo = &surfaces_data_buffer_info;

		writes[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[4].dstBinding = 4;
		writes[4].dstArrayElement = 0;
		writes[4].descriptorCount = 1;
		writes[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		writes[4].dstSet = lm->descriptor_set;
		writes[4].pBufferInfo = &workgroup_bounds_buffer_info;

		writes[5].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[5].dstBinding = 5;
		writes[5].dstArrayElement = 0;
		writes[5].descriptorCount = 1;
		writes[5].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
		writes[5].dstSet = lm->descriptor_set;
		writes[5].pBufferInfo = &lightstyle_scales_buffer_info;

		writes[6].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[6].dstBinding = 6;
		writes[6].dstArrayElement = 0;
		writes[6].descriptorCount = 1;
		writes[6].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
		writes[6].dstSet = lm->descriptor_set;
		writes[6].pBufferInfo = &lights_buffer_info;

		vkUpdateDescriptorSets (vulkan_globals.device, 7, writes, 0, NULL);
	}

	// johnfitz -- warn about exceeding old limits
	// GLQuake limit was 64 textures of 128x128. Estimate how many 128x128 textures we would need
	// given that we are using lightmap_count of LMBLOCK_WIDTH x LMBLOCK_HEIGHT
	i = lightmap_count * ((LMBLOCK_WIDTH / 128) * (LMBLOCK_HEIGHT / 128));
	if (i > 64)
		Con_DWarning ("%i lightmaps exceeds standard limit of 64.\n", i);
	// johnfitz
}

/*
=============================================================

	VBO support

=============================================================
*/

void GL_DeleteBModelVertexBuffer (void)
{
	GL_WaitForDeviceIdle ();

	if (bmodel_vertex_buffer)
		vkDestroyBuffer (vulkan_globals.device, bmodel_vertex_buffer, NULL);

	if (bmodel_memory.handle != VK_NULL_HANDLE)
	{
		Atomic_DecrementUInt32 (&num_vulkan_bmodel_allocations);
		R_FreeVulkanMemory (&bmodel_memory);
	}
}

/*
==================
GL_BuildBModelVertexBuffer

Deletes gl_bmodel_vbo if it already exists, then rebuilds it with all
surfaces from world + all brush models
==================
*/
void GL_BuildBModelVertexBuffer (void)
{
	unsigned int numverts, varray_bytes;
	int			 i, j;
	qmodel_t	*m;
	float		*varray;
	int			 remaining_size;
	int			 copy_offset;

	// count all verts in all models
	numverts = 0;
	for (j = 1; j < MAX_MODELS; j++)
	{
		m = cl.model_precache[j];
		if (!m || m->name[0] == '*' || m->type != mod_brush)
			continue;

		for (i = 0; i < m->numsurfaces; i++)
		{
			numverts += m->surfaces[i].numedges;
		}
	}

	// build vertex array
	varray_bytes = VERTEXSIZE * sizeof (float) * numverts;
	varray = (float *)Mem_Alloc (varray_bytes);

	for (j = 1; j < MAX_MODELS; j++)
	{
		m = cl.model_precache[j];
		if (!m || m->name[0] == '*' || m->type != mod_brush)
			continue;

		for (i = 0; i < m->numsurfaces; i++)
		{
			msurface_t *s = &m->surfaces[i];
			memcpy (&varray[VERTEXSIZE * s->vbo_firstvert], s->polys->verts, VERTEXSIZE * sizeof (float) * s->numedges);
		}
	}

	// Allocate & upload to GPU
	VkResult err;

	VkBufferCreateInfo buffer_create_info;
	memset (&buffer_create_info, 0, sizeof (buffer_create_info));
	buffer_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	buffer_create_info.size = varray_bytes;
	buffer_create_info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	err = vkCreateBuffer (vulkan_globals.device, &buffer_create_info, NULL, &bmodel_vertex_buffer);
	if (err != VK_SUCCESS)
		Sys_Error ("vkCreateBuffer failed");

	GL_SetObjectName ((uint64_t)bmodel_vertex_buffer, VK_OBJECT_TYPE_BUFFER, "Brush Vertex Buffer");

	VkMemoryRequirements memory_requirements;
	vkGetBufferMemoryRequirements (vulkan_globals.device, bmodel_vertex_buffer, &memory_requirements);

	const int align_mod = memory_requirements.size % memory_requirements.alignment;
	const int aligned_size = ((memory_requirements.size % memory_requirements.alignment) == 0)
								 ? memory_requirements.size
								 : (memory_requirements.size + memory_requirements.alignment - align_mod);

	VkMemoryAllocateInfo memory_allocate_info;
	memset (&memory_allocate_info, 0, sizeof (memory_allocate_info));
	memory_allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	memory_allocate_info.allocationSize = aligned_size;
	memory_allocate_info.memoryTypeIndex = GL_MemoryTypeFromProperties (memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0);

	Atomic_IncrementUInt32 (&num_vulkan_bmodel_allocations);
	R_AllocateVulkanMemory (&bmodel_memory, &memory_allocate_info, VULKAN_MEMORY_TYPE_DEVICE);
	GL_SetObjectName ((uint64_t)bmodel_memory.handle, VK_OBJECT_TYPE_DEVICE_MEMORY, "Brush Memory");

	err = vkBindBufferMemory (vulkan_globals.device, bmodel_vertex_buffer, bmodel_memory.handle, 0);
	if (err != VK_SUCCESS)
		Sys_Error ("vkBindImageMemory failed");

	remaining_size = varray_bytes;
	copy_offset = 0;

	while (remaining_size > 0)
	{
		const int		size_to_copy = q_min (remaining_size, vulkan_globals.staging_buffer_size);
		VkBuffer		staging_buffer;
		VkCommandBuffer command_buffer;
		int				staging_offset;
		unsigned char  *staging_memory = R_StagingAllocate (size_to_copy, 1, &command_buffer, &staging_buffer, &staging_offset);

		VkBufferCopy region;
		region.srcOffset = staging_offset;
		region.dstOffset = copy_offset;
		region.size = size_to_copy;
		vkCmdCopyBuffer (command_buffer, staging_buffer, bmodel_vertex_buffer, 1, &region);

		R_StagingBeginCopy ();
		memcpy (staging_memory, (byte *)varray + copy_offset, size_to_copy);
		R_StagingEndCopy ();

		copy_offset += size_to_copy;
		remaining_size -= size_to_copy;
	}

	Mem_Free (varray);
}

/*
=================
SoA_FillBoxLane
=================
*/
void SoA_FillBoxLane (soa_aabb_t *boxes, int index, vec3_t mins, vec3_t maxs)
{
	float *dst = boxes[index >> 3];
	index &= 7;
	dst[index + 0] = mins[0];
	dst[index + 8] = maxs[0];
	dst[index + 16] = mins[1];
	dst[index + 24] = maxs[1];
	dst[index + 32] = mins[2];
	dst[index + 40] = maxs[2];
}

/*
=================
SoA_FillPlaneLane
=================
*/
void SoA_FillPlaneLane (soa_plane_t *planes, int index, mplane_t *src, qboolean flip)
{
	float  side = flip ? -1.0f : 1.0f;
	float *dst = planes[index >> 3];
	index &= 7;
	dst[index + 0] = side * src->normal[0];
	dst[index + 8] = side * src->normal[1];
	dst[index + 16] = side * src->normal[2];
	dst[index + 24] = side * src->dist;
}

/*
===============
GL_PrepareSIMDData
===============
*/
void GL_PrepareSIMDAndParallelData (void)
{
	cl.worldmodel->surfvis = Mem_Alloc (((cl.worldmodel->numsurfaces + 31) / 8));
#ifdef USE_SIMD
	int i;

	cl.worldmodel->soa_leafbounds = Mem_Alloc (6 * sizeof (float) * ((cl.worldmodel->numleafs + 31) & ~7));
	cl.worldmodel->soa_surfplanes = Mem_Alloc (4 * sizeof (float) * ((cl.worldmodel->numsurfaces + 31) & ~7));

	for (i = 0; i < cl.worldmodel->numleafs; ++i)
	{
		mleaf_t *leaf = &cl.worldmodel->leafs[i + 1];
		SoA_FillBoxLane (cl.worldmodel->soa_leafbounds, i, leaf->minmaxs, leaf->minmaxs + 3);
	}

	for (i = 0; i < cl.worldmodel->numsurfaces; ++i)
	{
		msurface_t *surf = &cl.worldmodel->surfaces[i];
		SoA_FillPlaneLane (cl.worldmodel->soa_surfplanes, i, surf->plane, surf->flags & SURF_PLANEBACK);
	}
#endif // def USE_SIMD
}

/*
===============
R_AddDynamicLights
===============
*/
void R_AddDynamicLights (msurface_t *surf)
{
	int			lnum;
	int			sd, td;
	float		dist, rad, minlight;
	vec3_t		impact, local;
	int			s, t;
	int			i;
	int			smax, tmax;
	mtexinfo_t *tex;
	// johnfitz -- lit support via lordhavoc
	float		cred, cgreen, cblue, brightness;
	unsigned   *bl;
	// johnfitz

	smax = (surf->extents[0] >> 4) + 1;
	tmax = (surf->extents[1] >> 4) + 1;
	tex = surf->texinfo;

	for (lnum = 0; lnum < MAX_DLIGHTS; lnum++)
	{
		if (!(surf->dlightbits[lnum >> 5] & (1U << (lnum & 31))))
			continue; // not lit by this light

		rad = cl_dlights[lnum].radius;
		dist = DotProduct (cl_dlights[lnum].origin, surf->plane->normal) - surf->plane->dist;
		rad -= fabs (dist);
		minlight = cl_dlights[lnum].minlight;
		if (rad < minlight)
			continue;
		minlight = rad - minlight;

		for (i = 0; i < 3; i++)
		{
			impact[i] = cl_dlights[lnum].origin[i] - surf->plane->normal[i] * dist;
		}

		local[0] = DotProduct (impact, tex->vecs[0]) + tex->vecs[0][3];
		local[1] = DotProduct (impact, tex->vecs[1]) + tex->vecs[1][3];

		local[0] -= surf->texturemins[0];
		local[1] -= surf->texturemins[1];

		// johnfitz -- lit support via lordhavoc
		bl = blocklights;
		cred = cl_dlights[lnum].color[0] * 256.0f;
		cgreen = cl_dlights[lnum].color[1] * 256.0f;
		cblue = cl_dlights[lnum].color[2] * 256.0f;
		// johnfitz
		for (t = 0; t < tmax; t++)
		{
			td = local[1] - t * 16;
			if (td < 0)
				td = -td;
			for (s = 0; s < smax; s++)
			{
				sd = local[0] - s * 16;
				if (sd < 0)
					sd = -sd;
				if (sd > td)
					dist = sd + (td >> 1);
				else
					dist = td + (sd >> 1);
				if (dist < minlight)
				// johnfitz -- lit support via lordhavoc
				{
					brightness = rad - dist;
					bl[0] += (int)(brightness * cred);
					bl[1] += (int)(brightness * cgreen);
					bl[2] += (int)(brightness * cblue);
				}
				bl += 3;
				// johnfitz
			}
		}
	}
}

/*
===============
R_AccumulateLightmap

Scales 'lightmap' contents (RGB8) by 'scale' and accumulates
the result in the 'blocklights' array (RGB32)
===============
*/
void R_AccumulateLightmap (byte *lightmap, unsigned scale, int texels)
{
	unsigned *bl = blocklights;
	int		  size = texels * 3;

#if defined(USE_SIMD)
	if (use_simd && size >= 8)
	{
#if defined(USE_SSE2)
		__m128i vscale = _mm_set1_epi16 (scale);
		__m128i vlo, vhi, vdst, vsrc, v;

		while (size >= 8)
		{
			vsrc = _mm_loadl_epi64 ((const __m128i *)lightmap);

			v = _mm_unpacklo_epi8 (vsrc, _mm_setzero_si128 ());
			vlo = _mm_mullo_epi16 (v, vscale);
			vhi = _mm_mulhi_epu16 (v, vscale);

			vdst = _mm_loadu_si128 ((const __m128i *)bl);
			vdst = _mm_add_epi32 (vdst, _mm_unpacklo_epi16 (vlo, vhi));
			_mm_storeu_si128 ((__m128i *)bl, vdst);
			bl += 4;

			vdst = _mm_loadu_si128 ((const __m128i *)bl);
			vdst = _mm_add_epi32 (vdst, _mm_unpackhi_epi16 (vlo, vhi));
			_mm_storeu_si128 ((__m128i *)bl, vdst);
			bl += 4;

			lightmap += 8;
			size -= 8;
		}
#elif defined(USE_NEON)
		while (size >= 8)
		{
			uint8x8_t  lm_uint_8x8 = vld1_u8 (lightmap);
			uint16x8_t lm_uint_16x8 = vmovl_u8 (lm_uint_8x8);

			uint32x4_t lm_old_low_4x32bit = vld1q_u32 (bl);
			uint16x4_t lm_uint_low_16x4 = vget_low_u16 (lm_uint_16x8);
			uint32x4_t lm_scaled_accum_low_4x32bit = vmlal_n_u16 (lm_old_low_4x32bit, lm_uint_low_16x4, scale);
			vst1q_u32 (bl, lm_scaled_accum_low_4x32bit);
			bl += 4;

			uint32x4_t lm_old_high_4x32bit = vld1q_u32 (bl);
			uint16x4_t lm_uint_high_16x4 = vget_high_u16 (lm_uint_16x8);
			uint32x4_t lm_scaled_accum_high_4x32bit = vmlal_n_u16 (lm_old_high_4x32bit, lm_uint_high_16x4, scale);
			vst1q_u32 (bl, lm_scaled_accum_high_4x32bit);
			bl += 4;

			lightmap += 8;
			size -= 8;
		}
#endif
	}
#endif

	while (size-- > 0)
		*bl++ += *lightmap++ * scale;
}

/*
===============
R_StoreLightmap

Converts contiguous lightmap info accumulated in 'blocklights'
from RGB32 (with 8 fractional bits) to RGBA8, saturates and
stores the result in 'dest'
===============
*/
void R_StoreLightmap (byte *dest, int width, int height, int stride)
{
	unsigned *src = blocklights;

#if defined(USE_SIMD)
	if (use_simd)
	{
#if defined(USE_SSE2)
		__m128i vzero = _mm_setzero_si128 ();

		while (height-- > 0)
		{
			int i;
			for (i = 0; i < width; i++)
			{
				__m128i v = _mm_srli_epi32 (_mm_loadu_si128 ((const __m128i *)src), 8);
				v = _mm_packs_epi32 (v, vzero);
				v = _mm_packus_epi16 (v, vzero);
				((uint32_t *)dest)[i] = _mm_cvtsi128_si32 (v) | 0xff000000;
				src += 3;
			}
			dest += stride;
		}
#elif defined(USE_NEON)
		while (height-- > 0)
		{
			int i;
			for (i = 0; i < width; i++)
			{
				uint32x4_t lm_32x4 = vld1q_u32 (src);
				uint16x4_t lm_shifted_16x4 = vshrn_n_u32 (lm_32x4, 8);
				uint16x4_t lm_shifted_16x4_masked = vset_lane_u16 (0xFF, lm_shifted_16x4, 3);
				uint16x8_t lm_shifted_16x8 = vcombine_u16 (lm_shifted_16x4_masked, vcreate_u16 (0));
				uint8x8_t  lm_shifted_saturated_8x8 = vqmovn_u16 (lm_shifted_16x8);
				uint32x2_t lm_shifted_saturated_32x2 = vreinterpret_u32_u8 (lm_shifted_saturated_8x8);
				((uint32_t *)dest)[i] = vget_lane_u32 (lm_shifted_saturated_32x2, 0);
				src += 3;
			}
			dest += stride;
		}
#endif
	}
	else
#endif
	{
		stride -= width * 4;
		while (height-- > 0)
		{
			int i;
			for (i = 0; i < width; i++)
			{
				unsigned c;
				c = *src++ >> 8;
				*dest++ = q_min (c, 255);
				c = *src++ >> 8;
				*dest++ = q_min (c, 255);
				c = *src++ >> 8;
				*dest++ = q_min (c, 255);
				*dest++ = 255;
			}
			dest += stride;
		}
	}
}

/*
===============
R_BuildLightMap -- johnfitz -- revised for lit support via lordhavoc

Combine and scale multiple lightmaps into the 8.8 format in blocklights
===============
*/
void R_BuildLightMap (msurface_t *surf, byte *dest, int stride)
{
	int		 smax, tmax;
	int		 size;
	byte	*lightmap;
	unsigned scale;
	int		 maps;

	surf->cached_dlight = (surf->dlightframe == r_framecount);

	smax = (surf->extents[0] >> 4) + 1;
	tmax = (surf->extents[1] >> 4) + 1;
	size = smax * tmax;
	lightmap = surf->samples;

	if (cl.worldmodel->lightdata)
	{
		// clear to no light
		memset (&blocklights[0], 0, size * 3 * sizeof (unsigned int)); // johnfitz -- lit support via lordhavoc

		// add all the lightmaps
		if (lightmap)
		{
			for (maps = 0; maps < MAXLIGHTMAPS && surf->styles[maps] != 255; maps++)
			{
				scale = d_lightstylevalue[surf->styles[maps]];
				surf->cached_light[maps] = scale; // 8.8 fraction
				// johnfitz -- lit support via lordhavoc
				R_AccumulateLightmap (lightmap, scale, size);
				lightmap += size * 3;
				// johnfitz
			}
		}

		// add all the dynamic lights
		if (surf->dlightframe == r_framecount)
			R_AddDynamicLights (surf);
	}
	else
	{
		// set to full bright if no light data
		memset (&blocklights[0], 255, size * 3 * sizeof (unsigned int)); // johnfitz -- lit support via lordhavoc
	}

	R_StoreLightmap (dest, smax, tmax, stride);
}

/*
===============
R_UploadLightmap -- johnfitz -- uploads the modified lightmap to opengl if necessary

assumes lightmap texture is already bound
===============
*/
static void R_UploadLightmap (int lmap, gltexture_t *lightmap_tex)
{
	struct lightmap_s *lm = &lightmaps[lmap];
	qboolean		   modified = false;
	for (int i = 0; i < TASKS_MAX_WORKERS; ++i)
	{
		if (lm->modified[i])
			modified = true;
		lm->modified[i] = 0;
	}
	if (!modified)
		return;

	const int staging_size = LMBLOCK_WIDTH * lm->rectchange.h * 4;

	if (staging_size == 0) // Empty copies are not valid. This can happen for a single frame when toggling r_gpulightmapupdate from 1 to 0
		return;

	VkBuffer		staging_buffer;
	VkCommandBuffer command_buffer;
	int				staging_offset;
	unsigned char  *staging_memory = R_StagingAllocate (staging_size, 4, &command_buffer, &staging_buffer, &staging_offset);

	VkBufferImageCopy region;
	memset (&region, 0, sizeof (region));
	region.bufferOffset = staging_offset;
	region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	region.imageSubresource.layerCount = 1;
	region.imageSubresource.mipLevel = 0;
	region.imageExtent.width = LMBLOCK_WIDTH;
	region.imageExtent.height = lm->rectchange.h;
	region.imageExtent.depth = 1;
	region.imageOffset.y = lm->rectchange.t;

	VkImageMemoryBarrier image_memory_barrier;
	memset (&image_memory_barrier, 0, sizeof (image_memory_barrier));
	image_memory_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	image_memory_barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
	image_memory_barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	image_memory_barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	image_memory_barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	image_memory_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	image_memory_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	image_memory_barrier.image = lightmap_tex->image;
	image_memory_barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	image_memory_barrier.subresourceRange.baseMipLevel = 0;
	image_memory_barrier.subresourceRange.levelCount = 1;
	image_memory_barrier.subresourceRange.baseArrayLayer = 0;
	image_memory_barrier.subresourceRange.layerCount = 1;

	vulkan_globals.vk_cmd_pipeline_barrier (
		command_buffer, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &image_memory_barrier);

	vulkan_globals.vk_cmd_copy_buffer_to_image (command_buffer, staging_buffer, lightmap_tex->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

	image_memory_barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	image_memory_barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	image_memory_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	image_memory_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	vulkan_globals.vk_cmd_pipeline_barrier (
		command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, NULL, 0, NULL, 1, &image_memory_barrier);

	R_StagingBeginCopy ();
	byte *data = lm->data + lm->rectchange.t * LMBLOCK_WIDTH * LIGHTMAP_BYTES;
	if (vulkan_globals.color_format == VK_FORMAT_A2B10G10R10_UNORM_PACK32)
		for (byte *p = data; p < data + staging_size; p += 4, staging_memory += 4)
			*(unsigned *)staging_memory = p[0] | p[1] << 10 | p[2] << 20;
	else
		memcpy (staging_memory, data, staging_size);
	R_StagingEndCopy ();

	lm->rectchange.l = LMBLOCK_WIDTH;
	lm->rectchange.t = LMBLOCK_HEIGHT;
	lm->rectchange.h = 0;
	lm->rectchange.w = 0;
}

/*
=============
R_FlushUpdateLightmaps
=============
*/
void R_FlushUpdateLightmaps (
	cb_context_t *cbx, int num_batch_lightmaps, VkImageMemoryBarrier *pre_barriers, VkImageMemoryBarrier *post_barriers, int *lightmap_indexes,
	int num_used_dlights)
{
	vkCmdPipelineBarrier (
		cbx->cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, NULL, 0, NULL, num_batch_lightmaps, pre_barriers);
	R_BindPipeline (cbx, VK_PIPELINE_BIND_POINT_COMPUTE, vulkan_globals.update_lightmap_pipeline);
	uint32_t push_constants[2] = {num_used_dlights, LMBLOCK_WIDTH};
	uint32_t offsets[2] = {
		current_compute_buffer_index * MAX_LIGHTSTYLES * sizeof (float), current_compute_buffer_index * MAX_DLIGHTS * sizeof (lm_compute_light_t)};
	R_PushConstants (cbx, VK_SHADER_STAGE_COMPUTE_BIT, 0, 2 * sizeof (uint32_t), push_constants);
	for (int j = 0; j < num_batch_lightmaps; ++j)
	{
		VkDescriptorSet sets[1] = {lightmaps[lightmap_indexes[j]].descriptor_set};
		vkCmdBindDescriptorSets (cbx->cb, VK_PIPELINE_BIND_POINT_COMPUTE, vulkan_globals.update_lightmap_pipeline.layout.handle, 0, 1, sets, 2, offsets);
		vkCmdDispatch (cbx->cb, LMBLOCK_WIDTH / 8, LMBLOCK_HEIGHT / 8, 1);
	}

	vkCmdPipelineBarrier (
		cbx->cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, NULL, 0, NULL, num_batch_lightmaps, post_barriers);
}

/*
=============
R_IndirectComputeDispatch
=============
*/
static void R_IndirectComputeDispatch (cb_context_t *cbx)
{
	if (!indirect)
		return;

	R_BeginDebugUtilsLabel (cbx, "Indirect Compute");

	R_UploadVisibility (cl.worldmodel->surfvis, (cl.worldmodel->numsurfaces + 31) / 8);

	R_BindPipeline (cbx, VK_PIPELINE_BIND_POINT_COMPUTE, vulkan_globals.indirect_clear_pipeline);
	VkDescriptorSet sets[1] = {vulkan_globals.indirect_compute_desc_set};
	vkCmdBindDescriptorSets (cbx->cb, VK_PIPELINE_BIND_POINT_COMPUTE, vulkan_globals.indirect_clear_pipeline.layout.handle, 0, 1, sets, 0, NULL);
	R_PushConstants (cbx, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof (uint32_t), &used_indirect_draws);

	vkCmdDispatch (cbx->cb, (used_indirect_draws + 63) / 64, 1, 1);

	VkMemoryBarrier memory_barrier;
	memory_barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
	memory_barrier.pNext = NULL;
	memory_barrier.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
	memory_barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
	vkCmdPipelineBarrier (cbx->cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &memory_barrier, 0, NULL, 0, NULL);

	R_BindPipeline (cbx, VK_PIPELINE_BIND_POINT_COMPUTE, vulkan_globals.indirect_draw_pipeline);
	char push_constants[5 * 4];
	memcpy (push_constants, &cl.model_precache[1]->numsurfaces, sizeof (int));
	uint32_t offset = current_compute_buffer_index * dyn_visibility_offset / 4;
	memcpy (push_constants + 4, &offset, sizeof (uint32_t));
	memcpy (push_constants + 8, r_refdef.vieworg, sizeof (vec3_t));
	R_PushConstants (cbx, VK_SHADER_STAGE_COMPUTE_BIT, 0, 5 * 4, push_constants);
	vkCmdDispatch (cbx->cb, (cl.worldmodel->numsurfaces + 63) / 64, 1, 1);

	memory_barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
	memory_barrier.pNext = NULL;
	memory_barrier.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
	memory_barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
	vkCmdPipelineBarrier (
		cbx->cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, 0, 1, &memory_barrier, 0, NULL,
		0, NULL);

	R_EndDebugUtilsLabel (cbx);
}

/*
=============
R_UpdateLightmapsAndIndirect
=============
*/
void R_UpdateLightmapsAndIndirect (void *unused)
{
#define UPDATE_LIGHTMAP_BATCH_SIZE 64

	cb_context_t *cbx = &vulkan_globals.secondary_cb_contexts[CBX_UPDATE_LIGHTMAPS];
	R_BeginDebugUtilsLabel (cbx, "Update Lightmaps");

	for (int i = 0; i < MAX_LIGHTSTYLES; ++i)
	{
		float *style = lightstyles_scales_buffer_mapped + i + (current_compute_buffer_index * MAX_LIGHTSTYLES);
		*style = (float)d_lightstylevalue[i] / 256.0f;
	}

	for (int i = 0; i < MAX_DLIGHTS; ++i)
	{
		lm_compute_light_t *light = lights_buffer_mapped + i + (current_compute_buffer_index * MAX_DLIGHTS);
		VectorCopy (cl_dlights[i].origin, light->origin);
		light->radius = (cl_dlights[i].die < cl.time) ? 0.0f : cl_dlights[i].radius;
		VectorCopy (cl_dlights[i].color, light->color);
		light->minlight = cl_dlights[i].minlight;
	}

	int					 num_lightmaps = 0;
	int					 num_batch_lightmaps = 0;
	VkImageMemoryBarrier pre_lm_image_barriers[UPDATE_LIGHTMAP_BATCH_SIZE];
	VkImageMemoryBarrier post_lm_image_barriers[UPDATE_LIGHTMAP_BATCH_SIZE];
	int					 lightmap_indexes[UPDATE_LIGHTMAP_BATCH_SIZE];
	int					 num_used_dlights = 0;

	for (int lightmap_index = 0; lightmap_index < lightmap_count; ++lightmap_index)
	{
		struct lightmap_s *lm = &lightmaps[lightmap_index];
		uint32_t		   modified = 0;
		for (int i = 0; i < TASKS_MAX_WORKERS; ++i)
		{
			modified |= lm->modified[i];
			lm->modified[i] = 0;
		}
		if (modified == 0)
			continue;

		qboolean needs_update = false;
		for (int i = 0; i < MAX_LIGHTSTYLES; i++)
			if (lm->used_lightstyles[i] && modified & 1 << (i < 16 ? i : i % 16 + 16) && lm->cached_light[i] != d_lightstylevalue[i])
			{
				needs_update = true;
				break;
			}
		for (int i = 0; i < MAX_DLIGHTS; i++)
		{
			qboolean hit = true;

			if (!r_dynamic.value || cl_dlights[i].die < cl.time || cl_dlights[i].radius == 0.0f)
				hit = false;
			else
			{
				num_used_dlights = i + 1;
				float sq_dist = 0.0f;
				for (int j = 0; j < 3; j++)
				{
					float v = cl_dlights[i].origin[j];
					float mins = lm->global_bounds.mins[j];
					float maxs = lm->global_bounds.maxs[j];

					if (v < mins)
						sq_dist += (mins - v) * (mins - v);
					if (v > maxs)
						sq_dist += (v - maxs) * (v - maxs);
				}

				if (sq_dist > cl_dlights[i].radius * cl_dlights[i].radius)
					hit = false;
			}
			if (hit)
			{
				needs_update = true;
				lm->active_dlights[i] = true;
			}
			else if (lm->active_dlights[i])
			{
				needs_update = true;
				lm->active_dlights[i] = false;
			}
		}
		if (!needs_update)
			continue;
		else
			for (int i = 0; i < MAX_LIGHTSTYLES; i++)
				lm->cached_light[i] = d_lightstylevalue[i];

		int batch_index = num_batch_lightmaps++;
		lightmap_indexes[batch_index] = lightmap_index;

		VkImageMemoryBarrier *pre_barrier = &pre_lm_image_barriers[batch_index];
		pre_barrier->sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		pre_barrier->pNext = NULL;
		pre_barrier->srcAccessMask = 0;
		pre_barrier->dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		pre_barrier->oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		pre_barrier->newLayout = VK_IMAGE_LAYOUT_GENERAL;
		pre_barrier->srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		pre_barrier->dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		pre_barrier->image = lm->texture->image;
		pre_barrier->subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		pre_barrier->subresourceRange.baseMipLevel = 0;
		pre_barrier->subresourceRange.levelCount = 1;
		pre_barrier->subresourceRange.baseArrayLayer = 0;
		pre_barrier->subresourceRange.layerCount = 1;

		VkImageMemoryBarrier *post_barrier = &post_lm_image_barriers[batch_index];
		post_barrier->sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		post_barrier->pNext = NULL;
		post_barrier->srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		post_barrier->dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		post_barrier->oldLayout = VK_IMAGE_LAYOUT_GENERAL;
		post_barrier->newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		post_barrier->srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		post_barrier->dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		post_barrier->image = lm->texture->image;
		post_barrier->subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		post_barrier->subresourceRange.baseMipLevel = 0;
		post_barrier->subresourceRange.levelCount = 1;
		post_barrier->subresourceRange.baseArrayLayer = 0;
		post_barrier->subresourceRange.layerCount = 1;

		if (num_batch_lightmaps == UPDATE_LIGHTMAP_BATCH_SIZE)
		{
			R_FlushUpdateLightmaps (cbx, num_batch_lightmaps, pre_lm_image_barriers, post_lm_image_barriers, lightmap_indexes, num_used_dlights);
			num_lightmaps += num_batch_lightmaps;
			num_batch_lightmaps = 0;
		}
	}

	if (num_batch_lightmaps > 0)
	{
		R_FlushUpdateLightmaps (cbx, num_batch_lightmaps, pre_lm_image_barriers, post_lm_image_barriers, lightmap_indexes, num_used_dlights);
		num_lightmaps += num_batch_lightmaps;
	}

	Atomic_AddUInt32 (&rs_dynamiclightmaps, num_lightmaps);

	R_EndDebugUtilsLabel (cbx);

	R_IndirectComputeDispatch (cbx);

	current_compute_buffer_index = (current_compute_buffer_index + 1) % 2;
}

void R_UploadLightmaps (void)
{
	int lmap;
	int num_uploads = 0;

	for (lmap = 0; lmap < lightmap_count; lmap++)
	{
		qboolean modified = false;
		for (int i = 0; i < TASKS_MAX_WORKERS; ++i)
			modified = modified || lightmaps[lmap].modified[i];
		if (!modified)
			continue;

		++num_uploads;
		R_UploadLightmap (lmap, lightmaps[lmap].texture);
	}

	Atomic_AddUInt32 (&rs_dynamiclightmaps, num_uploads);
}
