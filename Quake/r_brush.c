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

extern cvar_t gl_fullbrights, r_drawflat; //johnfitz

int		gl_lightmap_format;

struct lightmap_s	*lightmaps;
int					lightmap_count;
int					last_lightmap_allocated;
int					allocated[LMBLOCK_WIDTH];

unsigned	blocklights[LMBLOCK_WIDTH*LMBLOCK_HEIGHT*3 + 1]; //johnfitz -- was 18*18, added lit support (*3) and loosened surface extents maximum (LMBLOCK_WIDTH*LMBLOCK_HEIGHT)

static vulkan_memory_t	bmodel_memory;
VkBuffer				bmodel_vertex_buffer;

extern cvar_t r_showtris;
extern cvar_t r_simd;
typedef struct lm_compute_surface_data_s
{
	uint32_t packed_lightstyles;
	vec3_t normal;
	float dist;
	uint32_t light_s;
	uint32_t light_t;
	uint32_t packed_texturemins;
	vec4_t vecs[2];
} lm_compute_surface_data_t;
COMPILE_TIME_ASSERT(lm_compute_surface_data_t, sizeof(lm_compute_surface_data_t) == 64);

typedef struct lm_compute_light_s
{
	vec3_t origin;
	float radius;
	vec3_t color;
	float minlight;
} lm_compute_light_t;
COMPILE_TIME_ASSERT(lm_compute_light_t, sizeof(lm_compute_light_t) == 32);

#define WORKGROUP_BOUNDS_BUFFER_SIZE ((LMBLOCK_WIDTH / 8) * (LMBLOCK_HEIGHT / 8) * sizeof(lm_compute_workgroup_bounds_t))

static vulkan_memory_t lightstyles_scales_buffer_memory;
static vulkan_memory_t lights_buffer_memory;
static vulkan_memory_t surface_data_buffer_memory;
static vulkan_memory_t workgroup_bounds_buffer_memory;
static VkBuffer surface_data_buffer;
static VkBuffer lightstyles_scales_buffer;
static VkBuffer lights_buffer;
static float *lightstyles_scales_buffer_mapped;
static lm_compute_light_t *lights_buffer_mapped;

static int current_compute_lightmap_buffer_index;

/*
===============
R_TextureAnimation -- johnfitz -- added "frame" param to eliminate use of "currententity" global

Returns the proper texture for a given time and base texture
===============
*/
texture_t *R_TextureAnimation (texture_t *base, int frame)
{
	int		relative;
	int		count;

	if (frame)
		if (base->alternate_anims)
			base = base->alternate_anims;

	if (!base->anim_total)
		return base;

	relative = (int)(cl.time*10) % base->anim_total;

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
void DrawGLPoly (glpoly_t *p, float color[3], float alpha)
{
	const int numverts = p->numverts;
	const int numtriangles = (numverts - 2);
	const int numindices = numtriangles * 3;

	VkBuffer vertex_buffer;
	VkDeviceSize vertex_buffer_offset;

	basicvertex_t * vertices = (basicvertex_t*)R_VertexAllocate(numverts * sizeof(basicvertex_t), &vertex_buffer, &vertex_buffer_offset);

	float	*v;
	int		i;
	int		current_index = 0;

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
		VkBuffer index_buffer;
		VkDeviceSize index_buffer_offset;

		uint16_t * indices = (uint16_t *)R_IndexAllocate(numindices * sizeof(uint16_t), &index_buffer, &index_buffer_offset);
		for (i = 0; i < numtriangles; ++i)
		{
			indices[current_index++] = 0;
			indices[current_index++] = 1 + i;
			indices[current_index++] = 2 + i;
		}
		vulkan_globals.vk_cmd_bind_index_buffer(vulkan_globals.command_buffer, index_buffer, index_buffer_offset, VK_INDEX_TYPE_UINT16);
	}
	else
		vulkan_globals.vk_cmd_bind_index_buffer(vulkan_globals.command_buffer, vulkan_globals.fan_index_buffer, 0, VK_INDEX_TYPE_UINT16);

	vulkan_globals.vk_cmd_bind_vertex_buffers(vulkan_globals.command_buffer, 0, 1, &vertex_buffer, &vertex_buffer_offset);
	vulkan_globals.vk_cmd_draw_indexed(vulkan_globals.command_buffer, numindices, 1, 0, 0, 0);
}

/*
=============================================================

	BRUSH MODELS

=============================================================
*/

/*
=================
R_DrawBrushModel
=================
*/
void R_DrawBrushModel (entity_t *e)
{
	int			i, k;
	msurface_t	*psurf;
	float		dot;
	mplane_t	*pplane;
	qmodel_t	*clmodel;

	if (R_CullModelForEntity(e))
		return;

	currententity = e;
	clmodel = e->model;

	VectorSubtract (r_refdef.vieworg, e->origin, modelorg);
	if (e->angles[0] || e->angles[1] || e->angles[2])
	{
		vec3_t	temp;
		vec3_t	forward, right, up;

		VectorCopy (modelorg, temp);
		AngleVectors (e->angles, forward, right, up);
		modelorg[0] = DotProduct (temp, forward);
		modelorg[1] = -DotProduct (temp, right);
		modelorg[2] = DotProduct (temp, up);
	}

	psurf = &clmodel->surfaces[clmodel->firstmodelsurface];

// calculate dynamic lighting for bmodel if it's not an
// instanced model
	if (clmodel->firstmodelsurface != 0)
	{
		for (k=0 ; k<MAX_DLIGHTS ; k++)
		{
			if ((cl_dlights[k].die < cl.time) ||
				(!cl_dlights[k].radius))
				continue;

			R_MarkLights (&cl_dlights[k], k,
				clmodel->nodes + clmodel->hulls[0].firstclipnode);
		}
	}

	e->angles[0] = -e->angles[0];	// stupid quake bug
	float model_matrix[16];
	IdentityMatrix(model_matrix);
	R_RotateForEntity (model_matrix, e->origin, e->angles);
	e->angles[0] = -e->angles[0];	// stupid quake bug

	float mvp[16];
	memcpy(mvp, vulkan_globals.view_projection_matrix, 16 * sizeof(float));
	MatrixMultiply(mvp, model_matrix);

	R_PushConstants(VK_SHADER_STAGE_ALL_GRAPHICS, 0, 16 * sizeof(float), mvp);
	R_ClearTextureChains (clmodel, chain_model);
	for (i=0 ; i<clmodel->nummodelsurfaces ; i++, psurf++)
	{
		pplane = psurf->plane;
		dot = DotProduct (modelorg, pplane->normal) - pplane->dist;
		if (((psurf->flags & SURF_PLANEBACK) && (dot < -BACKFACE_EPSILON)) ||
			(!(psurf->flags & SURF_PLANEBACK) && (dot > BACKFACE_EPSILON)))
		{
			R_ChainSurface (psurf, chain_model);
			R_RenderDynamicLightmaps(psurf);
			rs_brushpolys++;
		}
	}

	R_DrawTextureChains (clmodel, e, chain_model);
	R_DrawTextureChains_Water (clmodel, e, chain_model);
	R_PushConstants(VK_SHADER_STAGE_ALL_GRAPHICS, 0, 16 * sizeof(float), vulkan_globals.view_projection_matrix);
}

/*
=================
R_DrawBrushModel_ShowTris -- johnfitz
=================
*/
void R_DrawBrushModel_ShowTris(entity_t *e)
{
	int			i;
	msurface_t	*psurf;
	float		dot;
	mplane_t	*pplane;
	qmodel_t	*clmodel;
	float color[] = { 1.0f, 1.0f, 1.0f };
	const float alpha = 1.0f;

	if (R_CullModelForEntity(e))
		return;

	currententity = e;
	clmodel = e->model;

	VectorSubtract (r_refdef.vieworg, e->origin, modelorg);
	if (e->angles[0] || e->angles[1] || e->angles[2])
	{
		vec3_t	temp;
		vec3_t	forward, right, up;

		VectorCopy (modelorg, temp);
		AngleVectors (e->angles, forward, right, up);
		modelorg[0] = DotProduct (temp, forward);
		modelorg[1] = -DotProduct (temp, right);
		modelorg[2] = DotProduct (temp, up);
	}

	psurf = &clmodel->surfaces[clmodel->firstmodelsurface];

	e->angles[0] = -e->angles[0];	// stupid quake bug
	float model_matrix[16];
	IdentityMatrix(model_matrix);
	R_RotateForEntity (model_matrix, e->origin, e->angles);
	e->angles[0] = -e->angles[0];	// stupid quake bug

	float mvp[16];
	memcpy(mvp, vulkan_globals.view_projection_matrix, 16 * sizeof(float));
	MatrixMultiply(mvp, model_matrix);

	if (r_showtris.value == 1)
		R_BindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_globals.showtris_pipeline);
	else
		R_BindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_globals.showtris_depth_test_pipeline);
	R_PushConstants(VK_SHADER_STAGE_ALL_GRAPHICS, 0, 16 * sizeof(float), mvp);

	//
	// draw it
	//
	for (i=0 ; i<clmodel->nummodelsurfaces ; i++, psurf++)
	{
		pplane = psurf->plane;
		dot = DotProduct (modelorg, pplane->normal) - pplane->dist;
		if (((psurf->flags & SURF_PLANEBACK) && (dot < -BACKFACE_EPSILON)) ||
			(!(psurf->flags & SURF_PLANEBACK) && (dot > BACKFACE_EPSILON)))
		{
			DrawGLPoly (psurf->polys, color, alpha);
		}
	}

	R_PushConstants(VK_SHADER_STAGE_ALL_GRAPHICS, 0, 16 * sizeof(float), vulkan_globals.view_projection_matrix);
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
	byte		*base;
	int			maps;
	glRect_t    *theRect;
	int smax, tmax;

	if (fa->flags & SURF_DRAWTILED) //johnfitz -- not a lightmapped surface
		return;

	// check for lightmap modification
	for (maps=0; maps < MAXLIGHTMAPS && fa->styles[maps] != 255; maps++)
		if (d_lightstylevalue[fa->styles[maps]] != fa->cached_light[maps])
			goto dynamic;

	if (fa->dlightframe == r_framecount	// dynamic this frame
		|| fa->cached_dlight)			// dynamic previously
	{
dynamic:
		if (r_dynamic.value)
		{
			struct lightmap_s *lm = &lightmaps[fa->lightmaptexturenum];
			lm->modified = true;
			theRect = &lm->rectchange;
			if (fa->light_t < theRect->t) {
				if (theRect->h)
					theRect->h += theRect->t - fa->light_t;
				theRect->t = fa->light_t;
			}
			if (fa->light_s < theRect->l) {
				if (theRect->w)
					theRect->w += theRect->l - fa->light_s;
				theRect->l = fa->light_s;
			}
			smax = (fa->extents[0]>>4)+1;
			tmax = (fa->extents[1]>>4)+1;
			if ((theRect->w + theRect->l) < (fa->light_s + smax))
				theRect->w = (fa->light_s-theRect->l)+smax;
			if ((theRect->h + theRect->t) < (fa->light_t + tmax))
				theRect->h = (fa->light_t-theRect->t)+tmax;
			base = lm->data;
			base += fa->light_t * LMBLOCK_WIDTH * LIGHTMAP_BYTES + fa->light_s * LIGHTMAP_BYTES;
			R_BuildLightMap (fa, base, LMBLOCK_WIDTH* LIGHTMAP_BYTES);
		}
	}
}

/*
========================
AllocBlock -- returns a texture number and the position inside it
========================
*/
int AllocBlock (int w, int h, int *x, int *y)
{
	int		i, j;
	int		best, best2;
	int		texnum;

	// ericw -- rather than searching starting at lightmap 0 every time,
	// start at the last lightmap we allocated a surface in.
	// This makes AllocBlock much faster on large levels (can shave off 3+ seconds
	// of load time on a level with 180 lightmaps), at a cost of not quite packing
	// lightmaps as tightly vs. not doing this (uses ~5% more lightmaps)
	for (texnum=last_lightmap_allocated ; texnum<MAX_SANITY_LIGHTMAPS ; texnum++)
	{
		if (texnum == lightmap_count)
		{
			lightmap_count++;
			lightmaps = (struct lightmap_s *) realloc(lightmaps, sizeof(*lightmaps)*lightmap_count);
			memset(&lightmaps[texnum], 0, sizeof(lightmaps[texnum]));
			lightmaps[texnum].data = (byte *) calloc(1, LIGHTMAP_BYTES *LMBLOCK_WIDTH*LMBLOCK_HEIGHT);
			for (i = 0; i < MAXLIGHTMAPS; ++i)
				lightmaps[texnum].lightstyle_data[i] = (byte*)calloc(1, LIGHTMAP_BYTES * LMBLOCK_WIDTH * LMBLOCK_HEIGHT);
			lightmaps[texnum].surface_indices = (uint32_t*)malloc(sizeof(uint32_t) * LMBLOCK_WIDTH * LMBLOCK_HEIGHT);
			memset(lightmaps[texnum].surface_indices, 0xFF, 4 * LMBLOCK_WIDTH * LMBLOCK_HEIGHT);
			lightmaps[texnum].workgroup_bounds = (lm_compute_workgroup_bounds_t*)calloc(1, WORKGROUP_BOUNDS_BUFFER_SIZE);
			for (i = 0; i < (LMBLOCK_WIDTH / 8) * (LMBLOCK_HEIGHT / 8); ++i)
			{
				for (j = 0; j < 3; ++j)
				{
					lightmaps[texnum].workgroup_bounds[i].mins[j] = FLT_MAX;
					lightmaps[texnum].workgroup_bounds[i].maxs[j] = -FLT_MAX;
				}
			}
			//as we're only tracking one texture, we don't need multiple copies of allocated any more.
			memset(allocated, 0, sizeof(allocated));  
		}
		best = LMBLOCK_HEIGHT;

		for (i=0 ; i<LMBLOCK_WIDTH-w ; i++)
		{
			best2 = 0;

			for (j=0 ; j<w ; j++)
			{
				if (allocated[i+j] >= best)
					break;
				if (allocated[i+j] > best2)
					best2 = allocated[i+j];
			}
			if (j == w)
			{	// this is a valid spot
				*x = i;
				*y = best = best2;
			}
		}

		if (best + h > LMBLOCK_HEIGHT)
			continue;

		for (i=0 ; i<w ; i++)
			allocated[*x + i] = best + h;

		last_lightmap_allocated = texnum;
		return texnum;
	}

	Sys_Error ("AllocBlock: full");
	return 0; //johnfitz -- shut up compiler
}


mvertex_t	*r_pcurrentvertbase;
qmodel_t	*currentmodel;

int	nColinElim;

/*
===============
R_AssignSurfaceIndex
===============
*/
static void R_AssignSurfaceIndex(msurface_t* surf, uint32_t index, uint32_t* surface_indices, int stride)
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
static void R_FillLightstyleTextures(msurface_t* surf, byte** lightstyles, int stride)
{
	int			smax, tmax;
	byte* lightmap;
	int			maps;

	smax = (surf->extents[0] >> 4) + 1;
	tmax = (surf->extents[1] >> 4) + 1;
	lightmap = surf->samples;
	stride -= smax * LIGHTMAP_BYTES;

	// add all the lightmaps
	if (lightmap)
	{
		for (maps = 0; maps < MAXLIGHTMAPS && surf->styles[maps] != 255; ++maps)
		{
			int height = tmax;
			while (height-- > 0)
			{
				int i;
				for (i = 0; i < smax; i++)
				{
					*lightstyles[maps]++ = *lightmap++;
					*lightstyles[maps]++ = *lightmap++;
					*lightstyles[maps]++ = *lightmap++;
					*lightstyles[maps]++ = 0;
				}
				lightstyles[maps] += stride;
			}
		}
	}
}

/*
===============
R_AssignWorkgroupBounds
===============
*/
static void R_AssignWorkgroupBounds (msurface_t* surf)
{
	lm_compute_workgroup_bounds_t *bounds = lightmaps[surf->lightmaptexturenum].workgroup_bounds;
	const int smax = (surf->extents[0] >> 4) + 1;
	const int tmax = (surf->extents[1] >> 4) + 1;

	lm_compute_workgroup_bounds_t surf_bounds;
	for (int i = 0; i < 3; ++i)
	{
		surf_bounds.mins[i] = FLT_MAX;
		surf_bounds.maxs[i] = -FLT_MAX;
	}

	float* v = surf->polys->verts[0];
	for (int i = 0; i < surf->polys->numverts; ++i, v += VERTEXSIZE)
	{
		for (int j = 0; j < 3; ++j)
		{
			if (v[j] < surf_bounds.mins[j]) surf_bounds.mins[j] = v[j];
			if (v[j] > surf_bounds.maxs[j]) surf_bounds.maxs[j] = v[j];
		}
	}

	for (int s = 0; s < smax; ++s)
	{
		for (int t = 0; t < tmax; ++t)
		{
			const int workgroup_x = (surf->light_s + s) / 8;
			const int workgroup_y = (surf->light_t + t) / 8;
			lm_compute_workgroup_bounds_t *workgroup_bounds = bounds + workgroup_x + (workgroup_y * (LMBLOCK_WIDTH / 8));
			for (int i = 0; i < 3; ++i)
			{
				if (surf_bounds.mins[i] < workgroup_bounds->mins[i]) workgroup_bounds->mins[i] = surf_bounds.mins[i];
				if (surf_bounds.maxs[i] > workgroup_bounds->maxs[i]) workgroup_bounds->maxs[i] = surf_bounds.maxs[i];
			}
		}
	}
}

/*
========================
GL_CreateSurfaceLightmap
========================
*/
static void GL_CreateSurfaceLightmap (msurface_t *surf, uint32_t surface_index)
{
	int		i;
	int		smax, tmax;
	byte	*base;
	byte	*lightstyles[MAXLIGHTMAPS];
	uint32_t *surface_indices;

	smax = (surf->extents[0]>>4)+1;
	tmax = (surf->extents[1]>>4)+1;

	surf->lightmaptexturenum = AllocBlock (smax, tmax, &surf->light_s, &surf->light_t);
	base = lightmaps[surf->lightmaptexturenum].data;
	base += (surf->light_t * LMBLOCK_WIDTH + surf->light_s) * LIGHTMAP_BYTES;
	R_BuildLightMap (surf, base, LMBLOCK_WIDTH * LIGHTMAP_BYTES);

	surface_indices = lightmaps[surf->lightmaptexturenum].surface_indices;
	surface_indices += (surf->light_t * LMBLOCK_WIDTH + surf->light_s);
	R_AssignSurfaceIndex (surf, surface_index, surface_indices, LMBLOCK_WIDTH);

	for(i = 0; i < MAXLIGHTMAPS; ++i)
	{
		lightstyles[i] = lightmaps[surf->lightmaptexturenum].lightstyle_data[i];
		lightstyles[i] += (surf->light_t * LMBLOCK_WIDTH + surf->light_s) * LIGHTMAP_BYTES;
	}
	R_FillLightstyleTextures(surf, lightstyles, LMBLOCK_WIDTH * LIGHTMAP_BYTES);
}

/*
================
BuildSurfaceDisplayList -- called at level load time
================
*/
void BuildSurfaceDisplayList (msurface_t *fa)
{
	int			i, lindex, lnumverts;
	medge_t		*pedges, *r_pedge;
	float		*vec;
	float		s, t;
	glpoly_t	*poly;
	float		*poly_vert;

// reconstruct the polygon
	pedges = currentmodel->edges;
	lnumverts = fa->numedges;

	//
	// draw texture
	//
	poly = (glpoly_t *) Hunk_Alloc (sizeof(glpoly_t) + (lnumverts-4) * VERTEXSIZE*sizeof(float));
	poly->next = fa->polys;
	fa->polys = poly;
	poly->numverts = lnumverts;

	for (i=0 ; i<lnumverts ; i++)
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
		s = DotProduct (vec, fa->texinfo->vecs[0]) + fa->texinfo->vecs[0][3];
		s /= fa->texinfo->texture->width;

		t = DotProduct (vec, fa->texinfo->vecs[1]) + fa->texinfo->vecs[1][3];
		t /= fa->texinfo->texture->height;

		poly_vert = &poly->verts[0][0] + (i * VERTEXSIZE);
		VectorCopy (vec, poly_vert);
		poly_vert[3] = s;
		poly_vert[4] = t;

		// Q64 RERELEASE texture shift
		if (fa->texinfo->texture->shift > 0)
		{
			poly_vert[3] /= ( 2 * fa->texinfo->texture->shift);
			poly_vert[4] /= ( 2 * fa->texinfo->texture->shift);
		}

		//
		// lightmap texture coordinates
		//
		s = DotProduct (vec, fa->texinfo->vecs[0]) + fa->texinfo->vecs[0][3];
		s -= fa->texturemins[0];
		s += fa->light_s*16;
		s += 8;
		s /= LMBLOCK_WIDTH*16; //fa->texinfo->texture->width;

		t = DotProduct (vec, fa->texinfo->vecs[1]) + fa->texinfo->vecs[1][3];
		t -= fa->texturemins[1];
		t += fa->light_t*16;
		t += 8;
		t /= LMBLOCK_HEIGHT*16; //fa->texinfo->texture->height;

		poly_vert[5] = s;
		poly_vert[6] = t;
	}

	//johnfitz -- removed gl_keeptjunctions code

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
		size_t buffer_size = MAX_LIGHTSTYLES * sizeof(float) * 2;

		Sys_Printf("Allocating lightstyles buffer (%u KB)\n", (int)buffer_size / 1024);

		VkBufferCreateInfo buffer_create_info;
		memset(&buffer_create_info, 0, sizeof(buffer_create_info));
		buffer_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		buffer_create_info.size = buffer_size;
		buffer_create_info.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;

		err = vkCreateBuffer(vulkan_globals.device, &buffer_create_info, NULL, &lightstyles_scales_buffer);
		if (err != VK_SUCCESS)
			Sys_Error("vkCreateBuffer failed");
		GL_SetObjectName((uint64_t)lightstyles_scales_buffer, VK_OBJECT_TYPE_BUFFER, "Lightstyles Buffer");

		VkMemoryRequirements memory_requirements;
		vkGetBufferMemoryRequirements(vulkan_globals.device, lightstyles_scales_buffer, &memory_requirements);

		VkMemoryAllocateInfo memory_allocate_info;
		memset(&memory_allocate_info, 0, sizeof(memory_allocate_info));
		memory_allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		memory_allocate_info.allocationSize = memory_requirements.size;
		memory_allocate_info.memoryTypeIndex = GL_MemoryTypeFromProperties(memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, VK_MEMORY_PROPERTY_HOST_CACHED_BIT);

		num_vulkan_misc_allocations += 1;
		R_AllocateVulkanMemory(&lightstyles_scales_buffer_memory, &memory_allocate_info, VULKAN_MEMORY_TYPE_DEVICE);
		GL_SetObjectName((uint64_t)lightstyles_scales_buffer_memory.handle, VK_OBJECT_TYPE_DEVICE_MEMORY, "Lightstyles Buffer");

		err = vkBindBufferMemory(vulkan_globals.device, lightstyles_scales_buffer, lightstyles_scales_buffer_memory.handle, 0);
		if (err != VK_SUCCESS)
			Sys_Error("vkBindBufferMemory failed");

		err = vkMapMemory(vulkan_globals.device, lightstyles_scales_buffer_memory.handle, 0, buffer_size, 0, (void**)&lightstyles_scales_buffer_mapped);
		if (err != VK_SUCCESS)
			Sys_Error("vkMapMemory failed");
	}

	{
		size_t buffer_size = MAX_DLIGHTS * sizeof(lm_compute_light_t) * 2;

		Sys_Printf("Allocating lights buffer (%u KB)\n", (int)buffer_size / 1024);

		VkBufferCreateInfo buffer_create_info;
		memset(&buffer_create_info, 0, sizeof(buffer_create_info));
		buffer_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		buffer_create_info.size = buffer_size;
		buffer_create_info.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;

		err = vkCreateBuffer(vulkan_globals.device, &buffer_create_info, NULL, &lights_buffer);
		if (err != VK_SUCCESS)
			Sys_Error("vkCreateBuffer failed");
		GL_SetObjectName((uint64_t)lights_buffer, VK_OBJECT_TYPE_BUFFER, "Lights Buffer");

		VkMemoryRequirements memory_requirements;
		vkGetBufferMemoryRequirements(vulkan_globals.device, lights_buffer, &memory_requirements);

		VkMemoryAllocateInfo memory_allocate_info;
		memset(&memory_allocate_info, 0, sizeof(memory_allocate_info));
		memory_allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		memory_allocate_info.allocationSize = memory_requirements.size;
		memory_allocate_info.memoryTypeIndex = GL_MemoryTypeFromProperties(memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, VK_MEMORY_PROPERTY_HOST_CACHED_BIT);

		num_vulkan_misc_allocations += 1;
		R_AllocateVulkanMemory(&lights_buffer_memory, &memory_allocate_info, VULKAN_MEMORY_TYPE_DEVICE);
		GL_SetObjectName((uint64_t)lights_buffer_memory.handle, VK_OBJECT_TYPE_DEVICE_MEMORY, "Lights Buffer");

		err = vkBindBufferMemory(vulkan_globals.device, lights_buffer, lights_buffer_memory.handle, 0);
		if (err != VK_SUCCESS)
			Sys_Error("vkBindBufferMemory failed");

		err = vkMapMemory(vulkan_globals.device, lights_buffer_memory.handle, 0, buffer_size, 0, (void**)&lights_buffer_mapped);
		if (err != VK_SUCCESS)
			Sys_Error("vkMapMemory failed");
	}
}

/*
==================
GL_AllocateSurfaceDataBuffer
==================
*/
static lm_compute_surface_data_t *GL_AllocateSurfaceDataBuffer (int num_surfaces)
{
	VkResult err;
	size_t buffer_size = num_surfaces * sizeof(lm_compute_surface_data_t);

	if (surface_data_buffer != VK_NULL_HANDLE)
	{
		vkDestroyBuffer(vulkan_globals.device, surface_data_buffer, NULL);
		R_FreeVulkanMemory(&surface_data_buffer_memory);
		num_vulkan_misc_allocations -= 1;
	}

	Sys_Printf("Allocating lightmap compute surface data (%u KB)\n", (int)buffer_size / 1024);

	VkBufferCreateInfo buffer_create_info;
	memset(&buffer_create_info, 0, sizeof(buffer_create_info));
	buffer_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	buffer_create_info.size = buffer_size;
	buffer_create_info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

	err = vkCreateBuffer(vulkan_globals.device, &buffer_create_info, NULL, &surface_data_buffer);
	if (err != VK_SUCCESS)
		Sys_Error("vkCreateBuffer failed");
	GL_SetObjectName((uint64_t)surface_data_buffer, VK_OBJECT_TYPE_BUFFER, "Lightmap Compute Surface Data Buffer");

	VkMemoryRequirements memory_requirements;
	vkGetBufferMemoryRequirements(vulkan_globals.device, surface_data_buffer, &memory_requirements);

	VkMemoryAllocateInfo memory_allocate_info;
	memset(&memory_allocate_info, 0, sizeof(memory_allocate_info));
	memory_allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	memory_allocate_info.allocationSize = memory_requirements.size;
	memory_allocate_info.memoryTypeIndex = GL_MemoryTypeFromProperties(memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0);

	num_vulkan_misc_allocations += 1;
	R_AllocateVulkanMemory(&surface_data_buffer_memory, &memory_allocate_info, VULKAN_MEMORY_TYPE_DEVICE);
	GL_SetObjectName((uint64_t)surface_data_buffer_memory.handle, VK_OBJECT_TYPE_DEVICE_MEMORY, "Lightmap Compute Surface Data Buffer");

	err = vkBindBufferMemory(vulkan_globals.device, surface_data_buffer, surface_data_buffer_memory.handle, 0);
	if (err != VK_SUCCESS)
		Sys_Error("vkBindBufferMemory failed");

	VkCommandBuffer command_buffer;
	VkBuffer staging_buffer;
	int staging_offset;
	lm_compute_surface_data_t* staging_mem = (lm_compute_surface_data_t*)R_StagingAllocate(buffer_size, 1, &command_buffer, &staging_buffer, &staging_offset);

	VkBufferCopy region;
	region.srcOffset = staging_offset;
	region.dstOffset = 0;
	region.size = buffer_size;
	vkCmdCopyBuffer(command_buffer, staging_buffer, surface_data_buffer, 1, &region);

	return staging_mem;
}

/*
==================
GL_AllocateWorkgroupBoundsBuffers
==================
*/
static void GL_AllocateWorkgroupBoundsBuffers()
{
	VkResult err;

	if (workgroup_bounds_buffer_memory.handle != VK_NULL_HANDLE)
	{
		R_FreeVulkanMemory(&workgroup_bounds_buffer_memory);
		num_vulkan_misc_allocations -= 1;
	}

	for (int i = 0; i < lightmap_count; i++)
	{
		VkBufferCreateInfo buffer_create_info;
		memset(&buffer_create_info, 0, sizeof(buffer_create_info));
		buffer_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		buffer_create_info.size = WORKGROUP_BOUNDS_BUFFER_SIZE;
		buffer_create_info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

		err = vkCreateBuffer(vulkan_globals.device, &buffer_create_info, NULL, &lightmaps[i].workgroup_bounds_buffer);
		if (err != VK_SUCCESS)
			Sys_Error("vkCreateBuffer failed");
		GL_SetObjectName((uint64_t)lightmaps[i].workgroup_bounds_buffer, VK_OBJECT_TYPE_BUFFER, "Workgroup Bounds Buffer");
	}

	int aligned_size = 0;
	if (lightmap_count > 0)
	{
		VkMemoryRequirements memory_requirements;
		vkGetBufferMemoryRequirements(vulkan_globals.device, lightmaps[0].workgroup_bounds_buffer, &memory_requirements);

		const int align_mod = memory_requirements.size % memory_requirements.alignment;
		aligned_size = ((memory_requirements.size % memory_requirements.alignment) == 0)
			? memory_requirements.size
			: (memory_requirements.size + memory_requirements.alignment - align_mod);

		VkMemoryAllocateInfo memory_allocate_info;
		memset(&memory_allocate_info, 0, sizeof(memory_allocate_info));
		memory_allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		memory_allocate_info.allocationSize = lightmap_count * aligned_size;
		memory_allocate_info.memoryTypeIndex = GL_MemoryTypeFromProperties(memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0);

		num_vulkan_misc_allocations += 1;
		R_AllocateVulkanMemory(&workgroup_bounds_buffer_memory, &memory_allocate_info, VULKAN_MEMORY_TYPE_DEVICE);
		GL_SetObjectName((uint64_t)workgroup_bounds_buffer_memory.handle, VK_OBJECT_TYPE_DEVICE_MEMORY, "Workgroup Bounds Buffer Memory");
	}

	for (int i = 0; i < lightmap_count; i++)
	{
		err = vkBindBufferMemory(vulkan_globals.device, lightmaps[i].workgroup_bounds_buffer, workgroup_bounds_buffer_memory.handle, aligned_size * i);
		if (err != VK_SUCCESS)
			Sys_Error("vkBindBufferMemory failed");
	}
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
	char name[32];
	int i, j;
	int num_surfaces = 0;
	uint32_t surface_index = 0;
	struct lightmap_s *lm;
	qmodel_t *m;
	lm_compute_surface_data_t* surface_data;
	msurface_t* surf;

	GL_WaitForDeviceIdle();

	r_framecount = 1; // no dlightcache

	//Spike -- wipe out all the lightmap data (johnfitz -- the gltexture objects were already freed by Mod_ClearAll)
	for (i=0; i < lightmap_count; i++)
	{
		free(lightmaps[i].data);
		for (j=0; j<MAXLIGHTMAPS; ++j)
			free(lightmaps[i].lightstyle_data[j]);
		free(lightmaps[i].surface_indices);
		R_FreeDescriptorSet(lightmaps[i].descriptor_set, &vulkan_globals.lightmap_compute_set_layout);
		if (lightmaps[i].workgroup_bounds_buffer != VK_NULL_HANDLE)
			vkDestroyBuffer(vulkan_globals.device, lightmaps[i].workgroup_bounds_buffer, NULL);
		free(lightmaps[i].workgroup_bounds);
	}

	free(lightmaps);
	lightmaps = NULL;
	last_lightmap_allocated = 0;
	lightmap_count = 0;

	for (i = 1; i < MAX_MODELS; ++i)
	{
		m = cl.model_precache[i];
		if (!m)
			break;
		if (m->name[0] == '*')
			continue;
		num_surfaces += m->numsurfaces;
	}

	surface_data = GL_AllocateSurfaceDataBuffer(num_surfaces);

	for (j=1 ; j<MAX_MODELS ; j++)
	{
		m = cl.model_precache[j];
		if (!m)
			break;
		if (m->name[0] == '*')
			continue;
		r_pcurrentvertbase = m->vertexes;
		currentmodel = m;
		for (i=0 ; i<m->numsurfaces ; i++)
		{
			surf = &m->surfaces[i];
			if (surf->flags & SURF_DRAWTILED)
				continue;
			GL_CreateSurfaceLightmap (surf, surface_index);
			BuildSurfaceDisplayList (surf);
			R_AssignWorkgroupBounds (surf);

			lm_compute_surface_data_t* surf_data = &surface_data[surface_index];
			surf_data->packed_lightstyles =
				((uint32_t)(surf->styles[0]) << 0) |
				((uint32_t)(surf->styles[1]) << 8) |
				((uint32_t)(surf->styles[2]) << 16) |
				((uint32_t)(surf->styles[3]) << 24);
			for (int k = 0; k < 3; ++k)
				surf_data->normal[k] = surf->plane->normal[k];
			surf_data->dist = surf->plane->dist;
			surf_data->light_s = surf->light_s;
			surf_data->light_t = surf->light_t;
			surf_data->packed_texturemins = (uint32_t)(surf->texturemins[0] + 32768) | ((uint32_t)(surf->texturemins[1] + 32768) << 16);
			Vector4Copy(surf->texinfo->vecs[0], surf_data->vecs[0]);
			Vector4Copy(surf->texinfo->vecs[1], surf_data->vecs[1]);

			surface_index += 1;
		}
	}

	GL_AllocateWorkgroupBoundsBuffers();

	//
	// upload all lightmaps that were filled
	//
	for (i=0; i<lightmap_count; i++)
	{
		lm = &lightmaps[i];
		lm->modified = false;
		lm->rectchange.l = LMBLOCK_WIDTH;
		lm->rectchange.t = LMBLOCK_HEIGHT;
		lm->rectchange.w = 0;
		lm->rectchange.h = 0;

		sprintf(name, "lightmap%07i",i);
		lm->texture = TexMgr_LoadImage (cl.worldmodel, name, LMBLOCK_WIDTH, LMBLOCK_HEIGHT,
						SRC_LIGHTMAP, lm->data, "", (src_offset_t)lm->data, TEXPREF_LINEAR | TEXPREF_NOPICMIP);
		for (j=0; j< MAXLIGHTMAPS; ++j)
		{ 
			sprintf(name, "lightstyle%d%07i", j, i);
			lm->lightstyle_textures[j] = TexMgr_LoadImage(cl.worldmodel, name, LMBLOCK_WIDTH, LMBLOCK_HEIGHT,
				SRC_RGBA, lm->lightstyle_data[j], "", (src_offset_t)lm->data, TEXPREF_LINEAR | TEXPREF_NOPICMIP);
		}

		lm->surface_indices_texture = TexMgr_LoadImage(cl.worldmodel, name, LMBLOCK_WIDTH, LMBLOCK_HEIGHT,
			SRC_SURF_INDICES, (byte*)lm->surface_indices, "", (src_offset_t)lm->surface_indices, TEXPREF_LINEAR | TEXPREF_NOPICMIP);

		lm->descriptor_set = R_AllocateDescriptorSet(&vulkan_globals.lightmap_compute_set_layout);
		GL_SetObjectName((uint64_t)lm->descriptor_set, VK_OBJECT_TYPE_DESCRIPTOR_SET, va("%s compute desc set", name));

		{
			VkCommandBuffer command_buffer;
			VkBuffer staging_buffer;
			int staging_offset;
			byte* bounds_staging = R_StagingAllocate(WORKGROUP_BOUNDS_BUFFER_SIZE, 1, &command_buffer, &staging_buffer, &staging_offset);
			memcpy(bounds_staging, lm->workgroup_bounds, WORKGROUP_BOUNDS_BUFFER_SIZE);

			VkBufferCopy region;
			region.srcOffset = staging_offset;
			region.dstOffset = 0;
			region.size = WORKGROUP_BOUNDS_BUFFER_SIZE;
			vkCmdCopyBuffer(command_buffer, staging_buffer, lm->workgroup_bounds_buffer, 1, &region);
		}

		VkDescriptorImageInfo output_image_info;
		memset(&output_image_info, 0, sizeof(output_image_info));
		output_image_info.imageView = lm->texture->target_image_view;
		output_image_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

		VkDescriptorImageInfo surface_indices_image_info;
		memset(&surface_indices_image_info, 0, sizeof(surface_indices_image_info));
		surface_indices_image_info.imageView = lm->surface_indices_texture->image_view;
		surface_indices_image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		surface_indices_image_info.sampler = vulkan_globals.point_sampler;

		VkDescriptorImageInfo lightmap_images_infos[MAXLIGHTMAPS];
		memset(&lightmap_images_infos, 0, sizeof(lightmap_images_infos));
		for (j=0; j< MAXLIGHTMAPS; ++j)
		{
			lightmap_images_infos[j].imageView = lm->lightstyle_textures[j]->image_view;
			lightmap_images_infos[j].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			lightmap_images_infos[j].sampler = vulkan_globals.point_sampler;
		}

		VkDescriptorBufferInfo surfaces_data_buffer_info;
		memset(&surfaces_data_buffer_info, 0, sizeof(surfaces_data_buffer_info));
		surfaces_data_buffer_info.buffer = surface_data_buffer;
		surfaces_data_buffer_info.offset = 0;
		surfaces_data_buffer_info.range = num_surfaces * sizeof(lm_compute_surface_data_t);

		VkDescriptorBufferInfo workgroup_bounds_buffer_info;
		memset(&workgroup_bounds_buffer_info, 0, sizeof(workgroup_bounds_buffer_info));
		workgroup_bounds_buffer_info.buffer = lm->workgroup_bounds_buffer;
		workgroup_bounds_buffer_info.offset = 0;
		workgroup_bounds_buffer_info.range = (LMBLOCK_WIDTH / 8) * (LMBLOCK_HEIGHT / 8) * sizeof(lm_compute_workgroup_bounds_t);

		VkDescriptorBufferInfo lightstyle_scales_buffer_info;
		memset(&lightstyle_scales_buffer_info, 0, sizeof(lightstyle_scales_buffer_info));
		lightstyle_scales_buffer_info.buffer = lightstyles_scales_buffer;
		lightstyle_scales_buffer_info.offset = 0;
		lightstyle_scales_buffer_info.range = MAX_LIGHTSTYLES * sizeof(float);

		VkDescriptorBufferInfo lights_buffer_info;
		memset(&lights_buffer_info, 0, sizeof(lights_buffer_info));
		lights_buffer_info.buffer = lights_buffer;
		lights_buffer_info.offset = 0;
		lights_buffer_info.range = MAX_DLIGHTS * sizeof(lm_compute_light_t);

		VkWriteDescriptorSet writes[7];
		memset(&writes, 0, sizeof(writes));

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
		writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		writes[1].dstSet = lm->descriptor_set;
		writes[1].pImageInfo = &surface_indices_image_info;

		writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[2].dstBinding = 2;
		writes[2].dstArrayElement = 0;
		writes[2].descriptorCount = MAXLIGHTMAPS;
		writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
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

		vkUpdateDescriptorSets(vulkan_globals.device, 7, writes, 0, NULL);
	}

	//johnfitz -- warn about exceeding old limits
	//GLQuake limit was 64 textures of 128x128. Estimate how many 128x128 textures we would need
	//given that we are using lightmap_count of LMBLOCK_WIDTH x LMBLOCK_HEIGHT
	i = lightmap_count * ((LMBLOCK_WIDTH / 128) * (LMBLOCK_HEIGHT / 128));
	if (i > 64)
		Con_DWarning("%i lightmaps exceeds standard limit of 64.\n",i);
	//johnfitz
}

/*
=============================================================

	VBO support

=============================================================
*/

void GL_DeleteBModelVertexBuffer (void)
{
	GL_WaitForDeviceIdle();

	if (bmodel_vertex_buffer)
		vkDestroyBuffer(vulkan_globals.device, bmodel_vertex_buffer, NULL);

	if (bmodel_memory.handle != VK_NULL_HANDLE)
	{
		num_vulkan_bmodel_allocations -= 1;
		R_FreeVulkanMemory(&bmodel_memory);
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
	unsigned int	numverts, varray_bytes, varray_index;
	int		i, j;
	qmodel_t	*m;
	float		*varray;
	int remaining_size;
	int copy_offset;

	// count all verts in all models
	numverts = 0;
	for (j=1 ; j<MAX_MODELS ; j++)
	{
		m = cl.model_precache[j];
		if (!m || m->name[0] == '*' || m->type != mod_brush)
			continue;

		for (i=0 ; i<m->numsurfaces ; i++)
		{
			numverts += m->surfaces[i].numedges;
		}
	}
	
	// build vertex array
	varray_bytes = VERTEXSIZE * sizeof(float) * numverts;
	varray = (float *) malloc (varray_bytes);
	varray_index = 0;
	
	for (j=1 ; j<MAX_MODELS ; j++)
	{
		m = cl.model_precache[j];
		if (!m || m->name[0] == '*' || m->type != mod_brush)
			continue;

		for (i=0 ; i<m->numsurfaces ; i++)
		{
			msurface_t *s = &m->surfaces[i];
			s->vbo_firstvert = varray_index;
			memcpy (&varray[VERTEXSIZE * varray_index], s->polys->verts, VERTEXSIZE * sizeof(float) * s->numedges);
			varray_index += s->numedges;
		}
	}

	// Allocate & upload to GPU
	VkResult err;

	VkBufferCreateInfo buffer_create_info;
	memset(&buffer_create_info, 0, sizeof(buffer_create_info));
	buffer_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	buffer_create_info.size = varray_bytes;
	buffer_create_info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	err = vkCreateBuffer(vulkan_globals.device, &buffer_create_info, NULL, &bmodel_vertex_buffer);
	if (err != VK_SUCCESS)
		Sys_Error("vkCreateBuffer failed");

	GL_SetObjectName((uint64_t)bmodel_vertex_buffer, VK_OBJECT_TYPE_BUFFER, "Brush Vertex Buffer");

	VkMemoryRequirements memory_requirements;
	vkGetBufferMemoryRequirements(vulkan_globals.device, bmodel_vertex_buffer, &memory_requirements);

	const int align_mod = memory_requirements.size % memory_requirements.alignment;
	const int aligned_size = ((memory_requirements.size % memory_requirements.alignment) == 0 ) 
		? memory_requirements.size 
		: (memory_requirements.size + memory_requirements.alignment - align_mod);

	VkMemoryAllocateInfo memory_allocate_info;
	memset(&memory_allocate_info, 0, sizeof(memory_allocate_info));
	memory_allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	memory_allocate_info.allocationSize = aligned_size;
	memory_allocate_info.memoryTypeIndex = GL_MemoryTypeFromProperties(memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0);

	num_vulkan_bmodel_allocations += 1;
	R_AllocateVulkanMemory(&bmodel_memory, &memory_allocate_info, VULKAN_MEMORY_TYPE_DEVICE);
	GL_SetObjectName((uint64_t)bmodel_memory.handle, VK_OBJECT_TYPE_DEVICE_MEMORY, "Brush Memory");

	err = vkBindBufferMemory(vulkan_globals.device, bmodel_vertex_buffer, bmodel_memory.handle, 0);
	if (err != VK_SUCCESS)
		Sys_Error("vkBindImageMemory failed");

	remaining_size = varray_bytes;
	copy_offset = 0;

	while (remaining_size > 0)
	{
		const int size_to_copy = q_min(remaining_size, vulkan_globals.staging_buffer_size);
		VkBuffer staging_buffer;
		VkCommandBuffer command_buffer;
		int staging_offset;
		unsigned char * staging_memory = R_StagingAllocate(size_to_copy, 1, &command_buffer, &staging_buffer, &staging_offset);

		memcpy(staging_memory, (byte*)varray + copy_offset, size_to_copy);

		VkBufferCopy region;
		region.srcOffset = staging_offset;
		region.dstOffset = copy_offset;
		region.size = size_to_copy;
		vkCmdCopyBuffer(command_buffer, staging_buffer, bmodel_vertex_buffer, 1, &region);

		copy_offset += size_to_copy;
		remaining_size -= size_to_copy;
	}

	free (varray);
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
	mtexinfo_t	*tex;
	//johnfitz -- lit support via lordhavoc
	float		cred, cgreen, cblue, brightness;
	unsigned	*bl;
	//johnfitz

	smax = (surf->extents[0]>>4)+1;
	tmax = (surf->extents[1]>>4)+1;
	tex = surf->texinfo;

	for (lnum=0 ; lnum<MAX_DLIGHTS ; lnum++)
	{
		if (! (surf->dlightbits[lnum >> 5] & (1U << (lnum & 31))))
			continue;		// not lit by this light

		rad = cl_dlights[lnum].radius;
		dist = DotProduct (cl_dlights[lnum].origin, surf->plane->normal) -
				surf->plane->dist;
		rad -= fabs(dist);
		minlight = cl_dlights[lnum].minlight;
		if (rad < minlight)
			continue;
		minlight = rad - minlight;

		for (i=0 ; i<3 ; i++)
		{
			impact[i] = cl_dlights[lnum].origin[i] -
					surf->plane->normal[i]*dist;
		}

		local[0] = DotProduct (impact, tex->vecs[0]) + tex->vecs[0][3];
		local[1] = DotProduct (impact, tex->vecs[1]) + tex->vecs[1][3];

		local[0] -= surf->texturemins[0];
		local[1] -= surf->texturemins[1];

		//johnfitz -- lit support via lordhavoc
		bl = blocklights;
		cred = cl_dlights[lnum].color[0] * 256.0f;
		cgreen = cl_dlights[lnum].color[1] * 256.0f;
		cblue = cl_dlights[lnum].color[2] * 256.0f;
		//johnfitz
		for (t = 0 ; t<tmax ; t++)
		{
			td = local[1] - t*16;
			if (td < 0)
				td = -td;
			for (s=0 ; s<smax ; s++)
			{
				sd = local[0] - s*16;
				if (sd < 0)
					sd = -sd;
				if (sd > td)
					dist = sd + (td>>1);
				else
					dist = td + (sd>>1);
				if (dist < minlight)
				//johnfitz -- lit support via lordhavoc
				{
					brightness = rad - dist;
					bl[0] += (int) (brightness * cred);
					bl[1] += (int) (brightness * cgreen);
					bl[2] += (int) (brightness * cblue);
				}
				bl += 3;
				//johnfitz
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
void R_AccumulateLightmap(byte* lightmap, unsigned scale, int texels)
{
	unsigned *bl = blocklights;
	int size = texels * 3;

#ifdef USE_SSE2
	if (use_simd && size >= 8)
	{
		__m128i vscale = _mm_set1_epi16(scale);
		__m128i vlo, vhi, vdst, vsrc, v;

		while (size >= 8)
		{
			vsrc = _mm_loadl_epi64((const __m128i*)lightmap);

			v = _mm_unpacklo_epi8(vsrc, _mm_setzero_si128());
			vlo = _mm_mullo_epi16(v, vscale);
			vhi = _mm_mulhi_epu16(v, vscale);

			vdst = _mm_loadu_si128((const __m128i*)bl);
			vdst = _mm_add_epi32(vdst, _mm_unpacklo_epi16(vlo, vhi));
			_mm_storeu_si128((__m128i*)bl, vdst);
			bl += 4;

			vdst = _mm_loadu_si128((const __m128i*)bl);
			vdst = _mm_add_epi32(vdst, _mm_unpackhi_epi16(vlo, vhi));
			_mm_storeu_si128((__m128i*)bl, vdst);
			bl += 4;

			lightmap += 8;
			size -= 8;
		}
	}
#endif // def USE_SSE2

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
void R_StoreLightmap(byte* dest, int width, int height, int stride)
{
	unsigned *src = blocklights;

#ifdef USE_SSE2
	if (use_simd)
	{
		__m128i vzero = _mm_setzero_si128();

		while (height-- > 0)
		{
			int i;
			for (i = 0; i < width; i++)
			{
				__m128i v = _mm_srli_epi32(_mm_loadu_si128((const __m128i*)src), 8);
				v = _mm_packs_epi32(v, vzero);
				v = _mm_packus_epi16(v, vzero);
				((uint32_t*)dest)[i] = _mm_cvtsi128_si32(v) | 0xff000000;
				src += 3;
			}
			dest += stride;
		}
	}
	else
#endif // def USE_SSE2
	{
		stride -= width * 4;
		while (height-- > 0)
		{
			int i;
			for (i = 0; i < width; i++)
			{
				unsigned c;
				c = *src++ >> 8; *dest++ = q_min(c, 255);
				c = *src++ >> 8; *dest++ = q_min(c, 255);
				c = *src++ >> 8; *dest++ = q_min(c, 255);
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
	int			smax, tmax;
	int			size;
	byte		*lightmap;
	unsigned	scale;
	int			maps;

	surf->cached_dlight = (surf->dlightframe == r_framecount);

	smax = (surf->extents[0]>>4)+1;
	tmax = (surf->extents[1]>>4)+1;
	size = smax*tmax;
	lightmap = surf->samples;

	if (cl.worldmodel->lightdata)
	{
		// clear to no light
		memset (&blocklights[0], 0, size * 3 * sizeof (unsigned int)); //johnfitz -- lit support via lordhavoc

		// add all the lightmaps
		if (lightmap)
		{
			for (maps = 0 ; maps < MAXLIGHTMAPS && surf->styles[maps] != 255 ;
				 maps++)
			{
				scale = d_lightstylevalue[surf->styles[maps]];
				surf->cached_light[maps] = scale;	// 8.8 fraction
				//johnfitz -- lit support via lordhavoc
				R_AccumulateLightmap(lightmap, scale, size);
				lightmap += size * 3;
				//johnfitz
			}
		}

		// add all the dynamic lights
		if (surf->dlightframe == r_framecount)
			R_AddDynamicLights (surf);
	}
	else
	{
		// set to full bright if no light data
		memset (&blocklights[0], 255, size * 3 * sizeof (unsigned int)); //johnfitz -- lit support via lordhavoc
	}

	R_StoreLightmap(dest, smax, tmax, stride);
}

/*
===============
R_UploadLightmap -- johnfitz -- uploads the modified lightmap to opengl if necessary

assumes lightmap texture is already bound
===============
*/
static void R_UploadLightmap(int lmap, gltexture_t * lightmap_tex)
{
	struct lightmap_s *lm = &lightmaps[lmap];
	if (!lm->modified)
		return;

	lm->modified = false;

	const int staging_size = LMBLOCK_WIDTH * lm->rectchange.h * 4;

	VkBuffer staging_buffer;
	VkCommandBuffer command_buffer;
	int staging_offset;
	unsigned char * staging_memory = R_StagingAllocate(staging_size, 4, &command_buffer, &staging_buffer, &staging_offset);

	byte * data = lm->data+lm->rectchange.t*LMBLOCK_WIDTH* LIGHTMAP_BYTES;
	memcpy(staging_memory, data, staging_size);

	VkBufferImageCopy region;
	memset(&region, 0, sizeof(region));
	region.bufferOffset = staging_offset;
	region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	region.imageSubresource.layerCount = 1;
	region.imageSubresource.mipLevel = 0;
	region.imageExtent.width = LMBLOCK_WIDTH;
	region.imageExtent.height = lm->rectchange.h;
	region.imageExtent.depth = 1;
	region.imageOffset.y = lm->rectchange.t;

	VkImageMemoryBarrier image_memory_barrier;
	memset(&image_memory_barrier, 0, sizeof(image_memory_barrier));
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

	vulkan_globals.vk_cmd_pipeline_barrier(command_buffer, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &image_memory_barrier);
	
	vulkan_globals.vk_cmd_copy_buffer_to_image(command_buffer, staging_buffer, lightmap_tex->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

	image_memory_barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	image_memory_barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	image_memory_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	image_memory_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	vulkan_globals.vk_cmd_pipeline_barrier(command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, NULL, 0, NULL, 1, &image_memory_barrier);

	lm->rectchange.l = LMBLOCK_WIDTH;
	lm->rectchange.t = LMBLOCK_HEIGHT;
	lm->rectchange.h = 0;
	lm->rectchange.w = 0;

	rs_dynamiclightmaps++;
}

/*
=============
R_FlushUpdateLightmaps
=============
*/
void R_FlushUpdateLightmaps(int batch_start_index, int num_batch_lightmaps, VkImageMemoryBarrier *pre_barriers, VkImageMemoryBarrier* post_barriers)
{
	vkCmdPipelineBarrier(vulkan_globals.command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, NULL, 0, NULL, num_batch_lightmaps, pre_barriers);
	R_BindPipeline(VK_PIPELINE_BIND_POINT_COMPUTE, vulkan_globals.update_lightmap_pipeline);
	uint32_t push_constants[2] = { MAX_DLIGHTS, LMBLOCK_WIDTH };
	uint32_t offsets[2] = {
		current_compute_lightmap_buffer_index * MAX_LIGHTSTYLES * sizeof(float),
		current_compute_lightmap_buffer_index * MAX_DLIGHTS * sizeof(lm_compute_light_t)
	};
	R_PushConstants(VK_SHADER_STAGE_COMPUTE_BIT, 0, 2 * sizeof(uint32_t), push_constants);
	for (int j = batch_start_index; j < batch_start_index + num_batch_lightmaps; ++j)
	{
		VkDescriptorSet sets[1] = { lightmaps[j].descriptor_set };
		vkCmdBindDescriptorSets(vulkan_globals.command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, vulkan_globals.update_lightmap_pipeline.layout.handle, 0, 1, sets, 2, offsets);
		vkCmdDispatch(vulkan_globals.command_buffer, LMBLOCK_WIDTH / 8, LMBLOCK_HEIGHT / 8, 1);
	}

	vkCmdPipelineBarrier(vulkan_globals.command_buffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, NULL, 0, NULL, num_batch_lightmaps, post_barriers);
}

/*
=============
R_UpdateLightmaps
=============
*/
void R_UpdateLightmaps(void)
{
#define UPDATE_LIGHTMAP_BATCH_SIZE 64

	R_BeginDebugUtilsLabel("Update Lightmaps");

	for (int i = 0; i < MAX_LIGHTSTYLES; ++i)
	{
		float *style = lightstyles_scales_buffer_mapped + i + (current_compute_lightmap_buffer_index * MAX_LIGHTSTYLES);
		*style = (float)d_lightstylevalue[i] / 256.0f;
	}

	for (int i = 0; i < MAX_DLIGHTS; ++i)
	{
		lm_compute_light_t* light = lights_buffer_mapped + i + (current_compute_lightmap_buffer_index * MAX_DLIGHTS);
		VectorCopy(cl_dlights[i].origin, light->origin);
		light->radius = (cl_dlights[i].die < cl.time) ? 0.0f : cl_dlights[i].radius;
		VectorCopy(cl_dlights[i].color, light->color);
		light->minlight = cl_dlights[i].minlight;
	}

	int batch_start_index = 0;
	int num_batch_lightmaps = 0;
	VkImageMemoryBarrier pre_lm_image_barriers[UPDATE_LIGHTMAP_BATCH_SIZE];
	VkImageMemoryBarrier post_lm_image_barriers[UPDATE_LIGHTMAP_BATCH_SIZE];

	for (int lightmap_index = 0; lightmap_index < lightmap_count; ++lightmap_index)
	{
		struct lightmap_s *lm = &lightmaps[lightmap_index];
		if (!lm->modified) continue;
		lm->modified = false;

		int batch_index = num_batch_lightmaps++;
		VkImageMemoryBarrier* pre_barrier = &pre_lm_image_barriers[batch_index];
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

		VkImageMemoryBarrier* post_barrier = &post_lm_image_barriers[batch_index];
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
			R_FlushUpdateLightmaps (batch_start_index, num_batch_lightmaps, pre_lm_image_barriers, post_lm_image_barriers);
	}

	if (num_batch_lightmaps > 0)
		R_FlushUpdateLightmaps (batch_start_index, num_batch_lightmaps, pre_lm_image_barriers, post_lm_image_barriers);

	R_EndDebugUtilsLabel();

	current_compute_lightmap_buffer_index = (current_compute_lightmap_buffer_index + 1) % 2;
}

void R_UploadLightmaps (void)
{
	int lmap;

	for (lmap = 0; lmap < lightmap_count; lmap++)
	{
		if (!lightmaps[lmap].modified)
			continue;

		R_UploadLightmap(lmap, lightmaps[lmap].texture);
	}
}
