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

// r_alias.c -- alias model rendering

#include "quakedef.h"

extern cvar_t r_drawflat, gl_fullbrights, r_lerpmodels, r_lerpmove, r_showtris; // johnfitz
extern cvar_t r_lerpturn;
extern cvar_t cl_gun_fovscale;

// up to 16 color translated skins
gltexture_t *playertextures[MAX_SCOREBOARD]; // johnfitz -- changed to an array of pointers

#define NUMVERTEXNORMALS 162

float r_avertexnormals[NUMVERTEXNORMALS][3] = {
#include "anorms.h"
};

// precalculated dot products for quantized angles
#define SHADEDOT_QUANT 16

typedef struct
{
	float	 model_matrix[16];
	float	 shade_vector[3];
	float	 blend_factor;
	float	 light_color[3];
	float	 entalpha;
	uint32_t flags;
} aliasubo_t;

typedef struct
{
	float	 model_matrix[16];
	float	 shade_vector[3];
	float	 blend_factor;
	float	 light_color[3];
	float	 entalpha;
	uint32_t flags;
	uint32_t joints_offsets[2];
} md5ubo_t;

/*
=============
GLARB_GetXYZOffset

Returns the offset of the first vertex's meshxyz_t.xyz in the vbo for the given
model and pose.
=============
*/
static VkDeviceSize GLARB_GetXYZOffset (entity_t *e, aliashdr_t *hdr, int pose)
{
	const int xyzoffs = offsetof (meshxyz_t, xyz);
	return hdr->numverts_vbo * pose * sizeof (meshxyz_t) + xyzoffs;
}

/*
=============
GL_DrawAliasFrame -- ericw

Optimized alias model drawing codepath. This makes 1 draw call,
no vertex data is uploaded (it's already in the r_meshvbo and r_meshindexesvbo
static VBOs), and lerping and lighting is done in the vertex shader.

Supports optional fullbright pixels.

Based on code by MH from RMQEngine
=============
*/
static void GL_DrawAliasFrame (
	cb_context_t *cbx, entity_t *e, aliashdr_t *paliashdr, lerpdata_t lerpdata, gltexture_t *tx, gltexture_t *fb, float model_matrix[16], float entity_alpha,
	qboolean alphatest, vec3_t shadevector, vec3_t lightcolor, int showtris)
{
	vulkan_pipeline_t pipeline;

	// only enable alpha management if entity have alpha or the surface texture has effective
	// non-opaque pixels:
	const bool has_alpha = (entity_alpha < 1.0f) || (tx->flags & TEXPREF_ALPHAPIXELS);

	int pipeline_index;
	if (showtris == 0)
		pipeline_index = (has_alpha ? MODEL_PIPELINE_ALPHA_BLEND_BIT : 0) | (alphatest ? MODEL_PIPELINE_ALPHA_TEST_BIT : 0);
	else
		pipeline_index = (showtris >= 2) ? MODEL_PIPELINE_SHOWTRIS_DEPTH_TEST : MODEL_PIPELINE_SHOWTRIS;

	const qboolean oit_pass = cbx->render_pass_index == RENDER_PASS_INDEX_WBOIT || cbx->render_pass_index == RENDER_PASS_INDEX_MBOIT_MOMENTS ||
							  cbx->render_pass_index == RENDER_PASS_INDEX_MBOIT_COMPOSITE;
	if (oit_pass && (showtris != 0 || !has_alpha))
		return;

	if (paliashdr->poseverttype == PV_MD5)
		pipeline = R_PipelineForRenderPass (
			cbx->render_pass_index, vulkan_globals.md5_pipelines[R_MainPassPipelineVariant (cbx->render_pass_index)][pipeline_index],
			vulkan_globals.md5_wboit_pipelines[pipeline_index], vulkan_globals.md5_mboit_moment_pipelines[pipeline_index],
			vulkan_globals.md5_mboit_composite_pipelines[pipeline_index]);
	else
		pipeline = R_PipelineForRenderPass (
			cbx->render_pass_index, vulkan_globals.alias_pipelines[R_MainPassPipelineVariant (cbx->render_pass_index)][pipeline_index],
			vulkan_globals.alias_wboit_pipelines[pipeline_index], vulkan_globals.alias_mboit_moment_pipelines[pipeline_index],
			vulkan_globals.alias_mboit_composite_pipelines[pipeline_index]);

	R_BindPipeline (cbx, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

	float blend;

	if (lerpdata.pose1 != lerpdata.pose2)
		blend = lerpdata.blend;
	else // poses the same means either 1. the entity has paused its animation, or 2. r_lerpmodels is disabled
		blend = 0;

	switch (paliashdr->poseverttype)
	{
	case PV_QUAKE1:
	case PV_QUAKE3:
	{
		VkBuffer		uniform_buffer;
		uint32_t		uniform_offset;
		VkDescriptorSet ubo_set;
		aliasubo_t	   *ubo = (aliasubo_t *)R_UniformAllocate (sizeof (aliasubo_t), &uniform_buffer, &uniform_offset, &ubo_set);

		memcpy (ubo->model_matrix, model_matrix, 16 * sizeof (float));
		memcpy (ubo->shade_vector, shadevector, 3 * sizeof (float));
		ubo->blend_factor = blend;
		memcpy (ubo->light_color, lightcolor, 3 * sizeof (float));
		ubo->flags = (fb != NULL) ? 0x1 : 0x0;

		if (r_fullbright_cheatsafe || (r_lightmap_cheatsafe && r_fullbright.value))
			ubo->flags |= 0x2;

		if (paliashdr->poseverttype == PV_QUAKE3)
			ubo->flags |= 0x4;

		ubo->entalpha = entity_alpha;

		VkDescriptorSet descriptor_sets[3] = {tx->descriptor_set, (fb != NULL) ? fb->descriptor_set : tx->descriptor_set, ubo_set};
		vulkan_globals.vk_cmd_bind_descriptor_sets (
			cbx->cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.layout.handle, 0, 3, descriptor_sets, 1, &uniform_offset);

		VkBuffer	 vertex_buffers[3] = {paliashdr->vertex_buffer, paliashdr->vertex_buffer, paliashdr->vertex_buffer};
		VkDeviceSize vertex_offsets[3] = {
			(unsigned)paliashdr->vbostofs, GLARB_GetXYZOffset (e, paliashdr, lerpdata.pose1), GLARB_GetXYZOffset (e, paliashdr, lerpdata.pose2)};
		vulkan_globals.vk_cmd_bind_vertex_buffers (cbx->cb, 0, 3, vertex_buffers, vertex_offsets);
		vulkan_globals.vk_cmd_bind_index_buffer (cbx->cb, paliashdr->index_buffer, 0, VK_INDEX_TYPE_UINT16);

		vulkan_globals.vk_cmd_draw_indexed (cbx->cb, paliashdr->numindexes, 1, 0, 0, 0);
		break;
	}
	case PV_MD5:
	{
		VkBuffer		uniform_buffer;
		uint32_t		uniform_offset;
		VkDescriptorSet ubo_set;
		md5ubo_t	   *ubo = (md5ubo_t *)R_UniformAllocate (sizeof (md5ubo_t), &uniform_buffer, &uniform_offset, &ubo_set);

		memcpy (ubo->model_matrix, model_matrix, 16 * sizeof (float));
		memcpy (ubo->shade_vector, shadevector, 3 * sizeof (float));
		ubo->blend_factor = blend;
		memcpy (ubo->light_color, lightcolor, 3 * sizeof (float));
		ubo->flags = (fb != NULL) ? 0x1 : 0x0;
		if (r_fullbright_cheatsafe || (r_lightmap_cheatsafe && r_fullbright.value))
			ubo->flags |= 0x2;
		ubo->entalpha = entity_alpha;
		ubo->joints_offsets[0] = lerpdata.pose1 * paliashdr->numjoints;
		ubo->joints_offsets[1] = lerpdata.pose2 * paliashdr->numjoints;

		VkDescriptorSet descriptor_sets[4] = {tx->descriptor_set, (fb != NULL) ? fb->descriptor_set : tx->descriptor_set, ubo_set, paliashdr->joints_set};
		vulkan_globals.vk_cmd_bind_descriptor_sets (
			cbx->cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.layout.handle, 0, 4, descriptor_sets, 1, &uniform_offset);

		VkBuffer	 vertex_buffers[1] = {paliashdr->vertex_buffer};
		VkDeviceSize vertex_offsets[1] = {0};
		vulkan_globals.vk_cmd_bind_vertex_buffers (cbx->cb, 0, 1, vertex_buffers, vertex_offsets);
		vulkan_globals.vk_cmd_bind_index_buffer (cbx->cb, paliashdr->index_buffer, 0, VK_INDEX_TYPE_UINT16);

		//
		vulkan_globals.vk_cmd_draw_indexed (cbx->cb, paliashdr->numindexes, 1, 0, 0, 0);
		break;
	}
	default:
		assert (false);
	}
}

/*
=================
R_EntityPoseAt

Pose displayed by the given model frame at the given time (framegroup poses
advance with time).
=================
*/
static int R_EntityPoseAt (aliashdr_t *paliashdr, int frame, double time)
{
	if ((frame >= paliashdr->numframes) || (frame < 0))
		frame = 0;

	int posenum = paliashdr->frames[frame].firstpose;
	int numposes = paliashdr->frames[frame].numposes;
	if (numposes > 1)
		posenum += (int)(time / paliashdr->frames[frame].interval) % numposes;
	return posenum;
}

/*
=================
R_SetupAliasFrame -- johnfitz -- rewritten to support lerping

Computes pose1/pose2/blend from the parse-side interpolation state and
cl.time. Does not modify the entity.
=================
*/
void R_SetupAliasFrame (const entity_t *e, aliashdr_t *paliashdr, lerpdata_t *lerpdata)
{
	int frame = e->frame;
	if ((frame >= paliashdr->numframes) || (frame < 0))
		frame = 0;

	if (r_lerpmodels.value && !(e->model->flags & MOD_NOLERP && r_lerpmodels.value != 2))
	{
		int	   numposes = paliashdr->frames[frame].numposes;
		double change_time = e->lerp.frame_change_time;

		if (numposes > 1)
		{
			// framegroup: poses advance with cl.time; lerp from the previous
			// group pose unless the entity entered this frame more recently
			double interval = paliashdr->frames[frame].interval;
			int	   idx = (int)(cl.time / interval);
			double boundary = idx * interval;

			lerpdata->pose2 = paliashdr->frames[frame].firstpose + idx % numposes;
			if (change_time > boundary)
			{
				lerpdata->pose1 = R_EntityPoseAt (paliashdr, e->lerp.prev_frame, change_time);
				lerpdata->blend = CLAMP (0, (cl.time - change_time) / interval, 1);
			}
			else
			{
				lerpdata->pose1 = paliashdr->frames[frame].firstpose + (idx + numposes - 1) % numposes;
				lerpdata->blend = CLAMP (0, (cl.time - boundary) / interval, 1);
			}
		}
		else if (change_time > 0)
		{
			double duration = (e->lerp.frame_duration > 0) ? e->lerp.frame_duration : 0.1;
			lerpdata->pose2 = paliashdr->frames[frame].firstpose;
			lerpdata->pose1 = R_EntityPoseAt (paliashdr, e->lerp.prev_frame, change_time);
			lerpdata->blend = CLAMP (0, (cl.time - change_time) / duration, 1);
		}
		else
		{
			lerpdata->pose2 = paliashdr->frames[frame].firstpose;
			lerpdata->pose1 = lerpdata->pose2;
			lerpdata->blend = 1;
		}

		// Clamp poses (safety check for Quake1 models)
		if (paliashdr->poseverttype == PV_QUAKE1)
		{
			if (lerpdata->pose2 >= paliashdr->numposes || lerpdata->pose2 < 0)
			{
				Con_DPrintf ("R_AliasSetupFrame: invalid current pose %d (%d total) for '%s'\n", lerpdata->pose2, paliashdr->numposes, e->model->name);
				lerpdata->pose2 = 0;
			}
			if (lerpdata->pose1 >= paliashdr->numposes || lerpdata->pose1 < 0)
			{
				Con_DPrintf ("R_AliasSetupFrame: invalid prev pose %d (%d total) for '%s'\n", lerpdata->pose1, paliashdr->numposes, e->model->name);
				lerpdata->pose1 = lerpdata->pose2;
			}
		}
		else if (paliashdr->poseverttype == PV_MD5)
		{
			// MD5 uses numframes for joint matrices
			if (lerpdata->pose1 >= paliashdr->numframes || lerpdata->pose1 < 0)
				lerpdata->pose1 = 0;
			if (lerpdata->pose2 >= paliashdr->numframes || lerpdata->pose2 < 0)
				lerpdata->pose2 = 0;
		}
	}
	else
	{
		lerpdata->blend = 1;
		lerpdata->pose1 = R_EntityPoseAt (paliashdr, frame, cl.time);
		lerpdata->pose2 = lerpdata->pose1;
	}
}

/*
=================
R_GetEntityLerpedTransform

Computes lerped origin/angles from the parse-side interpolation state and
cl.time. Does not modify the entity.

Attached entities (tagentity) use their post-attachment origin/angles, which
only exist on the entity itself.
=================
*/
void R_GetEntityLerpedTransform (const entity_t *e, vec3_t out_origin, vec3_t out_angles)
{
	if (r_lerpmove.value && e != &cl.viewent && e->lerp.movestep && !e->netstate.tagentity && e->lerp.move_change_time > 0)
	{
		double change_time = e->lerp.move_change_time;
		double duration = (e->lerp.move_duration > 0) ? e->lerp.move_duration : 0.1;
		float  blend = CLAMP (0, (cl.time - change_time) / duration, 1);

		// translation
		vec3_t d;
		VectorSubtract (e->msg_origins[0], e->lerp.prev_origin, d);
		out_origin[0] = e->lerp.prev_origin[0] + d[0] * blend;
		out_origin[1] = e->lerp.prev_origin[1] + d[1] * blend;
		out_origin[2] = e->lerp.prev_origin[2] + d[2] * blend;

		// rotation (if enabled); EF_ROTATE angles are client-side and only exist on the entity
		if (r_lerpturn.value && !(e->model->flags & EF_ROTATE))
		{
			VectorSubtract (e->msg_angles[0], e->lerp.prev_angles, d);
			for (int i = 0; i < 3; i++)
			{
				if (d[i] > 180)
					d[i] -= 360;
				if (d[i] < -180)
					d[i] += 360;
			}
			out_angles[0] = e->lerp.prev_angles[0] + d[0] * blend;
			out_angles[1] = e->lerp.prev_angles[1] + d[1] * blend;
			out_angles[2] = e->lerp.prev_angles[2] + d[2] * blend;
		}
		else
		{
			VectorCopy (e->angles, out_angles);
		}
	}
	else // don't lerp
	{
		VectorCopy (e->origin, out_origin);
		VectorCopy (e->angles, out_angles);
	}
}

/*
=================
R_SetupAliasLighting -- johnfitz -- broken out from R_DrawAliasModel and rewritten
=================
*/
static void R_SetupAliasLighting (entity_t *e, vec3_t *shadevector, vec3_t *lightcolor)
{
	vec3_t dist;
	float  add;
	int	   i;
	int	   quantizedangle;
	float  radiansangle;

	// if the initial trace is completely black, try again from above
	// this helps with models whose origin is slightly below ground level
	// (e.g. some of the candles in the DOTM start map)
	if (!R_LightPoint (e->origin, 0.f, &e->lightcache, lightcolor))
		R_LightPoint (e->origin, e->model->maxs[2] * 0.5f, &e->lightcache, lightcolor);

	// add dlights
	for (i = 0; i < MAX_DLIGHTS; i++)
	{
		if (cl_dlights[i].die >= cl.time)
		{
			VectorSubtract (e->origin, cl_dlights[i].origin, dist);
			add = cl_dlights[i].radius - VectorLength (dist);
			if (add > 0)
			{
				if (cl_dlights[i].cone_cos > -1.0f)
				{
					vec3_t dir;
					VectorCopy (dist, dir);
					VectorNormalize (dir);
					const float cone_cos = cl_dlights[i].cone_cos;
					const float cone_dot = DotProduct (dir, cl_dlights[i].cone_dir);
					float		cone_scale;
					if (cl_dlights[i].kex_intensity > 0.0f)
					{
						// linear falloff from cone axis to edge, matches update_lightmap.inc
						if (cone_dot < cone_cos)
							continue;
						cone_scale = 1.0f - (1.0f - cone_dot) / (1.0f - cone_cos);
					}
					else
					{
						// soft edged spotlight falloff, matches update_lightmap.inc
						const float cone_soft = cone_cos + ((1.0f - cone_cos) * 0.25f);
						cone_scale = CLAMP (0.0f, (cone_dot - cone_cos) / q_max (cone_soft - cone_cos, 0.0001f), 1.0f);
					}
					add *= cone_scale;
					if (add <= 0.0f)
						continue;
				}
				if (cl_dlights[i].kex_intensity > 0.0f)
				{
					// KEX falloff: range-normalized, scaled by intensity (matches update_lightmap.inc,
					// sans the Lambert term since alias models use their own shading)
					add *= cl_dlights[i].kex_intensity * 0.5f * (256.0f / cl_dlights[i].radius);
				}
				VectorMA (*lightcolor, add, cl_dlights[i].color, *lightcolor);
			}
		}
	}

	// minimum light value on gun (24)
	if (e == &cl.viewent)
	{
		add = 72.0f - ((*lightcolor)[0] + (*lightcolor)[1] + (*lightcolor)[2]);
		if (add > 0.0f)
		{
			(*lightcolor)[0] += add / 3.0f;
			(*lightcolor)[1] += add / 3.0f;
			(*lightcolor)[2] += add / 3.0f;
		}
	}

	// minimum light value on players (8)
	if (e > cl.entities && e <= cl.entities + cl.maxclients)
	{
		add = 24.0f - ((*lightcolor)[0] + (*lightcolor)[1] + (*lightcolor)[2]);
		if (add > 0.0f)
		{
			(*lightcolor)[0] += add / 3.0f;
			(*lightcolor)[1] += add / 3.0f;
			(*lightcolor)[2] += add / 3.0f;
		}
	}

	// clamp lighting so it doesn't overbright as much (96)
	add = 288.0f / ((*lightcolor)[0] + (*lightcolor)[1] + (*lightcolor)[2]);
	if (add < 1.0f)
		VectorScale ((*lightcolor), add, (*lightcolor));

	quantizedangle = ((int)(e->angles[1] * (SHADEDOT_QUANT / 360.0))) & (SHADEDOT_QUANT - 1);

	// ericw -- shadevector is passed to the shader to compute shadedots inside the
	// shader, see GLAlias_CreateShaders()
	radiansangle = (quantizedangle / 16.0) * 2.0 * 3.14159;
	(*shadevector)[0] = cos (-radiansangle);
	(*shadevector)[1] = sin (-radiansangle);
	(*shadevector)[2] = 1;
	VectorNormalize (*shadevector);
	// ericw --

	VectorScale ((*lightcolor), 1.0f / 200.0f, (*lightcolor));
}

/*
=================
R_DrawAliasModel -- johnfitz -- almost completely rewritten
=================
*/
void R_DrawAliasModel (cb_context_t *cbx, entity_t *e, int *aliaspolys)
{
	aliashdr_t	*paliashdr;
	int			 anim, skinnum = e->skinnum;
	gltexture_t *tx, *fb;
	lerpdata_t	 lerpdata;

	//
	// setup pose/lerp data -- do it first so we don't miss updates due to culling
	//
	paliashdr = (aliashdr_t *)Mod_Extradata_CheckSkin (e->model, skinnum);

	qboolean alphatest = !!(e->model->flags & MF_HOLEY);

	R_SetupAliasFrame (e, paliashdr, &lerpdata);
	R_GetEntityLerpedTransform (e, lerpdata.origin, lerpdata.angles);

	//
	// cull it
	//
	if (R_CullModelForEntity (e))
		return;

	//
	// transform it
	//
	float model_matrix[16];
	IdentityMatrix (model_matrix);
	R_RotateForEntity (model_matrix, lerpdata.origin, lerpdata.angles, e->netstate.scale);

	float fovscale = 1.0f;
	if (e == &cl.viewent && r_refdef.basefov > 90.f && cl_gun_fovscale.value)
	{
		fovscale = tan (r_refdef.basefov * (0.5f * M_PI / 180.f));
		fovscale = 1.f + (fovscale - 1.f) * cl_gun_fovscale.value;
	}

	float translation_matrix[16];
	TranslationMatrix (translation_matrix, paliashdr->scale_origin[0], paliashdr->scale_origin[1] * fovscale, paliashdr->scale_origin[2] * fovscale);
	MatrixMultiply (model_matrix, translation_matrix);

	float scale_matrix[16];
	ScaleMatrix (scale_matrix, paliashdr->scale[0], paliashdr->scale[1] * fovscale, paliashdr->scale[2] * fovscale);
	MatrixMultiply (model_matrix, scale_matrix);

	//
	// set up for alpha blending
	//
	float entalpha;
	if (r_lightmap_cheatsafe)
		entalpha = 1;
	else
		entalpha = ENTALPHA_DECODE (e->alpha);
	if (entalpha == 0)
		return;

	//
	// set up lighting
	//
	vec3_t shadevector, lightcolor;
	R_SetupAliasLighting (e, &shadevector, &lightcolor);

	// Draw each surface of the model independently:
	for (aliashdr_t *hdr = paliashdr; hdr != NULL; hdr = hdr->nextsurface)
	{
		//
		// set up textures
		//
		anim = (int)(cl.time * 10) & 3;
		if ((skinnum >= hdr->numskins) || (skinnum < 0))
		{
			Con_DPrintf ("R_DrawAliasModel: no such skin # %d for '%s'\n", skinnum, e->model->name);
			// ericw -- display skin 0 for winquake compatibility
			skinnum = 0;
		}
		tx = hdr->gltextures[skinnum][anim];
		fb = hdr->fbtextures[skinnum][anim];

		if (e->colormap != vid.colormap && !gl_nocolors.value)
			if ((uintptr_t)e >= (uintptr_t)&cl.entities[1] && (uintptr_t)e <= (uintptr_t)&cl.entities[cl.maxclients] && playertextures[e - cl.entities - 1])
				tx = playertextures[e - cl.entities - 1];

		// if there are no texture, force the grey one. (a.k.a lightmap).
		if (tx == NULL)
		{
			tx = greytexture;
			fb = NULL;
		}

		if (!gl_fullbrights.value)
			fb = NULL;

		if (r_fullbright_cheatsafe)
		{
			lightcolor[0] = 0.5f;
			lightcolor[1] = 0.5f;
			lightcolor[2] = 0.5f;
		}
		if (r_lightmap_cheatsafe)
		{
			tx = greytexture;
			fb = NULL;
			if (r_fullbright.value)
			{
				lightcolor[0] = 1.0f;
				lightcolor[1] = 1.0f;
				lightcolor[2] = 1.0f;
			}
		}

		//
		// draw it
		//
		GL_DrawAliasFrame (cbx, e, hdr, lerpdata, tx, fb, model_matrix, entalpha, alphatest, shadevector, lightcolor, false);

		// update polycounts
		*aliaspolys += hdr->numtris;
	} // e for each surface
}

// johnfitz -- values for shadow matrix
#define SHADOW_SKEW_X -0.7 // skew along x axis. -0.7 to mimic glquake shadows
#define SHADOW_SKEW_Y 0	   // skew along y axis. 0 to mimic glquake shadows
#define SHADOW_VSCALE 0	   // 0=completely flat
#define SHADOW_HEIGHT 0.1  // how far above the floor to render the shadow
// johnfitz

/*
=================
R_DrawAliasModel_ShowTris -- johnfitz
=================
*/
void R_DrawAliasModel_ShowTris (cb_context_t *cbx, entity_t *e)
{
	aliashdr_t *paliashdr;
	lerpdata_t	lerpdata;

	//
	// setup pose/lerp data -- do it first so we don't miss updates due to culling
	//
	paliashdr = (aliashdr_t *)Mod_Extradata_CheckSkin (e->model, e->skinnum);

	R_SetupAliasFrame (e, paliashdr, &lerpdata);
	R_GetEntityLerpedTransform (e, lerpdata.origin, lerpdata.angles);

	//
	// cull it
	//
	if (R_CullModelForEntity (e))
		return;

	//
	// transform it
	//
	float model_matrix[16];
	IdentityMatrix (model_matrix);
	R_RotateForEntity (model_matrix, lerpdata.origin, lerpdata.angles, e->netstate.scale);

	float fovscale = 1.0f;
	if (e == &cl.viewent && r_refdef.basefov > 90.f)
	{
		fovscale = tan (r_refdef.basefov * (0.5f * M_PI / 180.f));
		fovscale = 1.f + (fovscale - 1.f) * cl_gun_fovscale.value;
	}

	float translation_matrix[16];
	TranslationMatrix (translation_matrix, paliashdr->scale_origin[0], paliashdr->scale_origin[1] * fovscale, paliashdr->scale_origin[2] * fovscale);
	MatrixMultiply (model_matrix, translation_matrix);

	float scale_matrix[16];
	ScaleMatrix (scale_matrix, paliashdr->scale[0], paliashdr->scale[1] * fovscale, paliashdr->scale[2] * fovscale);
	MatrixMultiply (model_matrix, scale_matrix);

	vec3_t shadevector = {0.0f, 0.0f, 0.0f};
	vec3_t lightcolor = {0.0f, 0.0f, 0.0f};
	// Draw each surface of the model independently:
	for (aliashdr_t *hdr = paliashdr; hdr != NULL; hdr = hdr->nextsurface)
	{
		GL_DrawAliasFrame (cbx, e, hdr, lerpdata, nulltexture, nulltexture, model_matrix, 0.0f, false, shadevector, lightcolor, r_showtris.value);
	}
}
