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

//r_alias.c -- alias model rendering

#include "quakedef.h"

extern cvar_t r_drawflat, gl_fullbrights, r_lerpmodels, r_lerpmove; //johnfitz

//up to 16 color translated skins
gltexture_t *playertextures[MAX_SCOREBOARD]; //johnfitz -- changed to an array of pointers

#define NUMVERTEXNORMALS	162

float	r_avertexnormals[NUMVERTEXNORMALS][3] =
{
#include "anorms.h"
};

extern vec3_t	lightcolor; //johnfitz -- replaces "float shadelight" for lit support

// precalculated dot products for quantized angles
#define SHADEDOT_QUANT 16
float	r_avertexnormal_dots[SHADEDOT_QUANT][256] =
{
#include "anorm_dots.h"
};

extern	vec3_t			lightspot;

float	*shadedots = r_avertexnormal_dots[0];
vec3_t	shadevector;

float	entalpha; //johnfitz

qboolean shading = true; //johnfitz -- if false, disable vertex shading for various reasons (fullbright, r_lightmap, showtris, etc)

//johnfitz -- struct for passing lerp information to drawing functions
typedef struct {
	short pose1;
	short pose2;
	float blend;
	vec3_t origin;
	vec3_t angles;
} lerpdata_t;
//johnfitz

typedef struct {
	float model_matrix[16];
	float shade_vector[3];
	float blend_factor;
	float light_color[3];
	unsigned int use_fullbright;
	float entalpha;
} aliasubo_t;

/*
=============
GLARB_GetXYZOffset

Returns the offset of the first vertex's meshxyz_t.xyz in the vbo for the given
model and pose.
=============
*/
static VkDeviceSize GLARB_GetXYZOffset (aliashdr_t *hdr, int pose)
{
	meshxyz_t dummy;
	int xyzoffs = ((char*)&dummy.xyz - (char*)&dummy);
	return currententity->model->vboxyzofs + (hdr->numverts_vbo * pose * sizeof (meshxyz_t)) + xyzoffs;
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
static void GL_DrawAliasFrame (aliashdr_t *paliashdr, lerpdata_t lerpdata, gltexture_t *tx, gltexture_t *fb, float model_matrix[16], float entalpha)
{
	float	blend;

	if (lerpdata.pose1 != lerpdata.pose2)
	{
		blend = lerpdata.blend;
	}
	else // poses the same means either 1. the entity has paused its animation, or 2. r_lerpmodels is disabled
	{
		blend = 0;
	}

	vkCmdBindPipeline(vulkan_globals.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, (entalpha < 1.0f) ? vulkan_globals.alias_blend_pipeline : vulkan_globals.alias_pipeline);

	VkBuffer uniform_buffer;
	uint32_t uniform_offset;
	VkDescriptorSet ubo_set;
	aliasubo_t * ubo = (aliasubo_t*)R_UniformAllocate(sizeof(aliasubo_t), &uniform_buffer, &uniform_offset, &ubo_set);
	
	memcpy(ubo->model_matrix, model_matrix, 16 * sizeof(float));
	memcpy(ubo->shade_vector, shadevector, 3 * sizeof(float));
	ubo->blend_factor = blend;
	memcpy(ubo->light_color, lightcolor, 3 * sizeof(float));
	ubo->use_fullbright = (fb != NULL) ? 1 : 0;
	ubo->entalpha = entalpha;

	VkDescriptorSet descriptor_sets[3] = { tx->descriptor_set, (fb != NULL) ? fb->descriptor_set : tx->descriptor_set, ubo_set };
	vkCmdBindDescriptorSets(vulkan_globals.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_globals.alias_pipeline_layout, 0, 3, descriptor_sets, 1, &uniform_offset);

	VkBuffer vertex_buffers[3] = { currententity->model->vertex_buffer, currententity->model->vertex_buffer, currententity->model->vertex_buffer };
	VkDeviceSize vertex_offsets[3] = { (unsigned)currententity->model->vbostofs, GLARB_GetXYZOffset (paliashdr, lerpdata.pose1), GLARB_GetXYZOffset (paliashdr, lerpdata.pose2) };
	vkCmdBindVertexBuffers(vulkan_globals.command_buffer, 0, 3, vertex_buffers, vertex_offsets);
	vkCmdBindIndexBuffer(vulkan_globals.command_buffer, currententity->model->index_buffer, 0, VK_INDEX_TYPE_UINT16);

	vkCmdDrawIndexed(vulkan_globals.command_buffer, paliashdr->numindexes, 1, 0, 0, 0);

	rs_aliaspasses += paliashdr->numtris;
}

/*
=================
R_SetupAliasFrame -- johnfitz -- rewritten to support lerping
=================
*/
void R_SetupAliasFrame (aliashdr_t *paliashdr, int frame, lerpdata_t *lerpdata)
{
	entity_t		*e = currententity;
	int				posenum, numposes;

	if ((frame >= paliashdr->numframes) || (frame < 0))
	{
		Con_DPrintf ("R_AliasSetupFrame: no such frame %d for '%s'\n", frame, e->model->name);
		frame = 0;
	}

	posenum = paliashdr->frames[frame].firstpose;
	numposes = paliashdr->frames[frame].numposes;

	if (numposes > 1)
	{
		e->lerptime = paliashdr->frames[frame].interval;
		posenum += (int)(cl.time / e->lerptime) % numposes;
	}
	else
		e->lerptime = 0.1;

	if (e->lerpflags & LERP_RESETANIM) //kill any lerp in progress
	{
		e->lerpstart = 0;
		e->previouspose = posenum;
		e->currentpose = posenum;
		e->lerpflags -= LERP_RESETANIM;
	}
	else if (e->currentpose != posenum) // pose changed, start new lerp
	{
		if (e->lerpflags & LERP_RESETANIM2) //defer lerping one more time
		{
			e->lerpstart = 0;
			e->previouspose = posenum;
			e->currentpose = posenum;
			e->lerpflags -= LERP_RESETANIM2;
		}
		else
		{
			e->lerpstart = cl.time;
			e->previouspose = e->currentpose;
			e->currentpose = posenum;
		}
	}

	//set up values
	if (r_lerpmodels.value && !(e->model->flags & MOD_NOLERP && r_lerpmodels.value != 2))
	{
		if (e->lerpflags & LERP_FINISH && numposes == 1)
			lerpdata->blend = CLAMP (0, (cl.time - e->lerpstart) / (e->lerpfinish - e->lerpstart), 1);
		else
			lerpdata->blend = CLAMP (0, (cl.time - e->lerpstart) / e->lerptime, 1);
		lerpdata->pose1 = e->previouspose;
		lerpdata->pose2 = e->currentpose;
	}
	else //don't lerp
	{
		lerpdata->blend = 1;
		lerpdata->pose1 = posenum;
		lerpdata->pose2 = posenum;
	}
}

/*
=================
R_SetupEntityTransform -- johnfitz -- set up transform part of lerpdata
=================
*/
void R_SetupEntityTransform (entity_t *e, lerpdata_t *lerpdata)
{
	float blend;
	vec3_t d;
	int i;

	// if LERP_RESETMOVE, kill any lerps in progress
	if (e->lerpflags & LERP_RESETMOVE)
	{
		e->movelerpstart = 0;
		VectorCopy (e->origin, e->previousorigin);
		VectorCopy (e->origin, e->currentorigin);
		VectorCopy (e->angles, e->previousangles);
		VectorCopy (e->angles, e->currentangles);
		e->lerpflags -= LERP_RESETMOVE;
	}
	else if (!VectorCompare (e->origin, e->currentorigin) || !VectorCompare (e->angles, e->currentangles)) // origin/angles changed, start new lerp
	{
		e->movelerpstart = cl.time;
		VectorCopy (e->currentorigin, e->previousorigin);
		VectorCopy (e->origin,  e->currentorigin);
		VectorCopy (e->currentangles, e->previousangles);
		VectorCopy (e->angles,  e->currentangles);
	}

	//set up values
	if (r_lerpmove.value && e != &cl.viewent && e->lerpflags & LERP_MOVESTEP)
	{
		if (e->lerpflags & LERP_FINISH)
			blend = CLAMP (0, (cl.time - e->movelerpstart) / (e->lerpfinish - e->movelerpstart), 1);
		else
			blend = CLAMP (0, (cl.time - e->movelerpstart) / 0.1, 1);

		//translation
		VectorSubtract (e->currentorigin, e->previousorigin, d);
		lerpdata->origin[0] = e->previousorigin[0] + d[0] * blend;
		lerpdata->origin[1] = e->previousorigin[1] + d[1] * blend;
		lerpdata->origin[2] = e->previousorigin[2] + d[2] * blend;

		//rotation
		VectorSubtract (e->currentangles, e->previousangles, d);
		for (i = 0; i < 3; i++)
		{
			if (d[i] > 180)  d[i] -= 360;
			if (d[i] < -180) d[i] += 360;
		}
		lerpdata->angles[0] = e->previousangles[0] + d[0] * blend;
		lerpdata->angles[1] = e->previousangles[1] + d[1] * blend;
		lerpdata->angles[2] = e->previousangles[2] + d[2] * blend;
	}
	else //don't lerp
	{
		VectorCopy (e->origin, lerpdata->origin);
		VectorCopy (e->angles, lerpdata->angles);
	}
}

/*
=================
R_SetupAliasLighting -- johnfitz -- broken out from R_DrawAliasModel and rewritten
=================
*/
void R_SetupAliasLighting (entity_t	*e)
{
	vec3_t		dist;
	float		add;
	int			i;
	int		quantizedangle;
	float		radiansangle;

	R_LightPoint (e->origin);

	//add dlights
	for (i=0 ; i<MAX_DLIGHTS ; i++)
	{
		if (cl_dlights[i].die >= cl.time)
		{
			VectorSubtract (currententity->origin, cl_dlights[i].origin, dist);
			add = cl_dlights[i].radius - VectorLength(dist);
			if (add > 0)
				VectorMA (lightcolor, add, cl_dlights[i].color, lightcolor);
		}
	}

	// minimum light value on gun (24)
	if (e == &cl.viewent)
	{
		add = 72.0f - (lightcolor[0] + lightcolor[1] + lightcolor[2]);
		if (add > 0.0f)
		{
			lightcolor[0] += add / 3.0f;
			lightcolor[1] += add / 3.0f;
			lightcolor[2] += add / 3.0f;
		}
	}

	// minimum light value on players (8)
	if (currententity > cl_entities && currententity <= cl_entities + cl.maxclients)
	{
		add = 24.0f - (lightcolor[0] + lightcolor[1] + lightcolor[2]);
		if (add > 0.0f)
		{
			lightcolor[0] += add / 3.0f;
			lightcolor[1] += add / 3.0f;
			lightcolor[2] += add / 3.0f;
		}
	}

	// clamp lighting so it doesn't overbright as much (96)
	add = 288.0f / (lightcolor[0] + lightcolor[1] + lightcolor[2]);
	if (add < 1.0f)
		VectorScale(lightcolor, add, lightcolor);

	quantizedangle = ((int)(e->angles[1] * (SHADEDOT_QUANT / 360.0))) & (SHADEDOT_QUANT - 1);

//ericw -- shadevector is passed to the shader to compute shadedots inside the
//shader, see GLAlias_CreateShaders()
	radiansangle = (quantizedangle / 16.0) * 2.0 * 3.14159;
	shadevector[0] = cos(-radiansangle);
	shadevector[1] = sin(-radiansangle);
	shadevector[2] = 1;
	VectorNormalize(shadevector);
//ericw --

	shadedots = r_avertexnormal_dots[quantizedangle];
	VectorScale (lightcolor, 1.0f / 200.0f, lightcolor);
}

/*
=================
R_DrawAliasModel -- johnfitz -- almost completely rewritten
=================
*/
void R_DrawAliasModel (entity_t *e)
{
	aliashdr_t	*paliashdr;
	int			i, anim;
	gltexture_t	*tx, *fb;
	lerpdata_t	lerpdata;

	//
	// setup pose/lerp data -- do it first so we don't miss updates due to culling
	//
	paliashdr = (aliashdr_t *)Mod_Extradata (e->model);
	R_SetupAliasFrame (paliashdr, e->frame, &lerpdata);
	R_SetupEntityTransform (e, &lerpdata);

	//
	// cull it
	//
	if (R_CullModelForEntity(e))
		return;

	//
	// transform it
	//
	float model_matrix[16];
	IdentityMatrix(model_matrix);
	R_RotateForEntity (model_matrix, lerpdata.origin, lerpdata.angles);

	float translation_matrix[16];
	TranslationMatrix (translation_matrix, paliashdr->scale_origin[0], paliashdr->scale_origin[1], paliashdr->scale_origin[2]);
	MatrixMultiply(model_matrix, translation_matrix);

	float scale_matrix[16];
	ScaleMatrix (scale_matrix, paliashdr->scale[0], paliashdr->scale[1], paliashdr->scale[2]);
	MatrixMultiply(model_matrix, scale_matrix);

	//
	// random stuff
	//
	//if (gl_smoothmodels.value && !r_drawflat_cheatsafe)
	//	glShadeModel (GL_SMOOTH);
	//if (gl_affinemodels.value)
	//	glHint (GL_PERSPECTIVE_CORRECTION_HINT, GL_FASTEST);
	shading = true;

	//
	// set up for alpha blending
	//
	if (r_drawflat_cheatsafe || r_lightmap_cheatsafe) //no alpha in drawflat or lightmap mode
		entalpha = 1;
	else
		entalpha = ENTALPHA_DECODE(e->alpha);
	if (entalpha == 0)
		return;

	//
	// set up lighting
	//
	rs_aliaspolys += paliashdr->numtris;
	R_SetupAliasLighting (e);

	//
	// set up textures
	//
	anim = (int)(cl.time*10) & 3;
	if ((e->skinnum >= paliashdr->numskins) || (e->skinnum < 0))
	{
		Con_DPrintf ("R_DrawAliasModel: no such skin # %d for '%s'\n", e->skinnum, e->model->name);
		tx = NULL; // NULL will give the checkerboard texture
		fb = NULL;
	}
	else
	{
		tx = paliashdr->gltextures[e->skinnum][anim];
		fb = paliashdr->fbtextures[e->skinnum][anim];
	} 
	if (e->colormap != vid.colormap && !gl_nocolors.value)
	{
		i = e - cl_entities;
		if (i >= 1 && i<=cl.maxclients )
		    tx = playertextures[i - 1];
	}
	if (!gl_fullbrights.value)
		fb = NULL;

	//
	// draw it
	//
	if (r_drawflat_cheatsafe)
	{
		/*glDisable (GL_TEXTURE_2D);
		GL_DrawAliasFrame (paliashdr, lerpdata);
		glEnable (GL_TEXTURE_2D);
		srand((int) (cl.time * 1000)); //restore randomness*/
	}
	else if (r_fullbright_cheatsafe)
	{
		/*GL_Bind (tx);
		shading = false;
		glColor4f(1,1,1,entalpha);
		GL_DrawAliasFrame (paliashdr, lerpdata);
		if (fb)
		{
			glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
			GL_Bind(fb);
			glEnable(GL_BLEND);
			glBlendFunc (GL_ONE, GL_ONE);
			glDepthMask(GL_FALSE);
			glColor3f(entalpha,entalpha,entalpha);
			Fog_StartAdditive ();
			GL_DrawAliasFrame (paliashdr, lerpdata);
			Fog_StopAdditive ();
			glDepthMask(GL_TRUE);
			glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			glDisable(GL_BLEND);
		}*/
	}
	else if (r_lightmap_cheatsafe)
	{
		/*glDisable (GL_TEXTURE_2D);
		shading = false;
		glColor3f(1,1,1);
		GL_DrawAliasFrame (paliashdr, lerpdata);
		glEnable (GL_TEXTURE_2D);*/
	}
	else
	{
		GL_DrawAliasFrame (paliashdr, lerpdata, tx, fb, model_matrix, entalpha);
	}
}

//johnfitz -- values for shadow matrix
#define SHADOW_SKEW_X -0.7 //skew along x axis. -0.7 to mimic glquake shadows
#define SHADOW_SKEW_Y 0 //skew along y axis. 0 to mimic glquake shadows
#define SHADOW_VSCALE 0 //0=completely flat
#define SHADOW_HEIGHT 0.1 //how far above the floor to render the shadow
//johnfitz

/*
=============
GL_DrawAliasShadow -- johnfitz -- rewritten

TODO: orient shadow onto "lightplane" (a global mplane_t*)
=============
*/
void GL_DrawAliasShadow (entity_t *e)
{
	/*float	shadowmatrix[16] = {1,				0,				0,				0,
								0,				1,				0,				0,
								SHADOW_SKEW_X,	SHADOW_SKEW_Y,	SHADOW_VSCALE,	0,
								0,				0,				SHADOW_HEIGHT,	1};
	float		lheight;
	aliashdr_t	*paliashdr;
	lerpdata_t	lerpdata;

	if (R_CullModelForEntity(e))
		return;

	if (e == &cl.viewent || e->model->flags & MOD_NOSHADOW)
		return;

	entalpha = ENTALPHA_DECODE(e->alpha);
	if (entalpha == 0) return;

	paliashdr = (aliashdr_t *)Mod_Extradata (e->model);
	R_SetupAliasFrame (paliashdr, e->frame, &lerpdata);
	R_SetupEntityTransform (e, &lerpdata);
	R_LightPoint (e->origin);
	lheight = currententity->origin[2] - lightspot[2];

// set up matrix
	glPushMatrix ();
	glTranslatef (lerpdata.origin[0],  lerpdata.origin[1],  lerpdata.origin[2]);
	glTranslatef (0,0,-lheight);
	glMultMatrixf (shadowmatrix);
	glTranslatef (0,0,lheight);
	glRotatef (lerpdata.angles[1],  0, 0, 1);
	glRotatef (-lerpdata.angles[0],  0, 1, 0);
	glRotatef (lerpdata.angles[2],  1, 0, 0);
	glTranslatef (paliashdr->scale_origin[0], paliashdr->scale_origin[1], paliashdr->scale_origin[2]);
	glScalef (paliashdr->scale[0], paliashdr->scale[1], paliashdr->scale[2]);

// draw it
	glDepthMask(GL_FALSE);
	glEnable (GL_BLEND);
	GL_DisableMultitexture ();
	glDisable (GL_TEXTURE_2D);
	shading = false;
	glColor4f(0,0,0,entalpha * 0.5);
	GL_DrawAliasFrame (paliashdr, lerpdata);
	glEnable (GL_TEXTURE_2D);
	glDisable (GL_BLEND);
	glDepthMask(GL_TRUE);

//clean up
	glPopMatrix ();*/
}

/*
=================
R_DrawAliasModel_ShowTris -- johnfitz
=================
*/
void R_DrawAliasModel_ShowTris (entity_t *e)
{
	/*aliashdr_t	*paliashdr;
	lerpdata_t	lerpdata;

	if (R_CullModelForEntity(e))
		return;

	paliashdr = (aliashdr_t *)Mod_Extradata (e->model);
	R_SetupAliasFrame (paliashdr, e->frame, &lerpdata);
	R_SetupEntityTransform (e, &lerpdata);

	glPushMatrix ();
	R_RotateForEntity (lerpdata.origin,lerpdata.angles);
	glTranslatef (paliashdr->scale_origin[0], paliashdr->scale_origin[1], paliashdr->scale_origin[2]);
	glScalef (paliashdr->scale[0], paliashdr->scale[1], paliashdr->scale[2]);

	shading = false;
	glColor3f(1,1,1);
	GL_DrawAliasFrame (paliashdr, lerpdata);

	glPopMatrix ();*/
}

