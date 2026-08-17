/*
Copyright (C) 1996-2001 Id Software, Inc.
Copyright (C) 2002-2009 John Fitzgibbons and others
Copyright (C) 2010-2014 QuakeSpasm developers

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
// r_main.c

#include "quakedef.h"
#include "tasks.h"
#include "atomics.h"

int r_visframecount; // bumped when going to a new PVS
int r_framecount;	 // used for dlight push checking

mplane_t frustum[4];

qboolean render_warp;
int		 render_scale;

// johnfitz -- rendering statistics
atomic_uint32_t rs_brushpolys, rs_aliaspolys, rs_skypolys, rs_particles, rs_fogpolys;
atomic_uint32_t rs_dynamiclightmaps, rs_brushpasses, rs_aliaspasses;
uint32_t		rs_cputime_us, rs_gputime_us;
uint32_t		rs_gpuwaittime_us, rs_gpuwaitaccum_us;
double			rs_frame_starttime;
char			rs_display_lines[3][40];
int				rs_display_numlines;

//
// view origin
//
vec3_t vup;
vec3_t vpn;
vec3_t vright;
vec3_t r_origin;

float r_fovx, r_fovy; // johnfitz -- rendering fov may be different becuase of r_waterwarp

//
// screen size info
//
refdef_t r_refdef;

mleaf_t *r_viewleaf, *r_oldviewleaf;

int d_lightstylevalue[MAX_LIGHTSTYLES]; // 8.8 fraction of base light value

cvar_t r_drawentities = {"r_drawentities", "1", CVAR_NONE};
cvar_t r_drawviewmodel = {"r_drawviewmodel", "1", CVAR_NONE};
cvar_t scr_speeds = {"scr_speeds", "0", CVAR_NONE};
cvar_t r_pos = {"r_pos", "0", CVAR_NONE};
cvar_t r_fullbright = {"r_fullbright", "0", CVAR_NONE};
cvar_t r_lightmap = {"r_lightmap", "0", CVAR_NONE};
cvar_t r_wateralpha = {"r_wateralpha", "1", CVAR_ARCHIVE};
cvar_t r_oit = {"r_oit", "1", CVAR_ARCHIVE};
cvar_t r_dynamic = {"r_dynamic", "1", CVAR_ARCHIVE};
cvar_t r_novis = {"r_novis", "0", CVAR_ARCHIVE};
#if defined(USE_SIMD)
cvar_t r_simd = {"r_simd", "1", CVAR_ARCHIVE};
#endif
cvar_t r_alphasort = {"r_alphasort", "1", CVAR_ARCHIVE};

cvar_t gl_finish = {"gl_finish", "0", CVAR_NONE};
cvar_t gl_polyblend = {"gl_polyblend", "1", CVAR_NONE};
cvar_t gl_nocolors = {"gl_nocolors", "0", CVAR_NONE};

// johnfitz -- new cvars
cvar_t r_clearcolor = {"r_clearcolor", "2", CVAR_ARCHIVE};
cvar_t r_fastclear = {"r_fastclear", "1", CVAR_ARCHIVE};
cvar_t r_flatlightstyles = {"r_flatlightstyles", "0", CVAR_NONE};
cvar_t r_lerplightstyles = {"r_lerplightstyles", "1", CVAR_ARCHIVE}; // 0=off; 1=skip abrupt transitions; 2=always lerp
cvar_t gl_fullbrights = {"gl_fullbrights", "1", CVAR_ARCHIVE};
cvar_t gl_farclip = {"gl_farclip", "16384", CVAR_ARCHIVE};
cvar_t r_oldskyleaf = {"r_oldskyleaf", "0", CVAR_NONE};
cvar_t r_drawworld = {"r_drawworld", "1", CVAR_NONE};
cvar_t r_showtris = {"r_showtris", "0", CVAR_NONE};
cvar_t r_showskel = {"r_showskel", "0", CVAR_NONE};
cvar_t r_showbboxes = {"r_showbboxes", "0", CVAR_NONE};
cvar_t r_showbboxes_think = {"r_showbboxes_think", "0", CVAR_NONE};	  // 0=show all; 1=thinkers only; -1=non-thinkers only
cvar_t r_showbboxes_health = {"r_showbboxes_health", "0", CVAR_NONE}; // 0=show all; 1=healthy only; -1=non-healthy only
cvar_t r_showbboxes_links = {"r_showbboxes_links", "3", CVAR_NONE};	  // 0=off; 1=outgoing only; 2=incoming only; 3=incoming+outgoing
cvar_t r_showbboxes_targets = {"r_showbboxes_targets", "1", CVAR_NONE};
cvar_t r_showfields = {"r_showfields", "0", CVAR_NONE};
cvar_t r_showfields_align = {"r_showfields_align", "1", CVAR_ARCHIVE}; // 0=entity pos; 1=bottom-right
cvar_t r_lerpmodels = {"r_lerpmodels", "1", CVAR_ARCHIVE};
cvar_t r_lerpmove = {"r_lerpmove", "1", CVAR_ARCHIVE};
cvar_t r_lerpturn = {"r_lerpturn", "1", CVAR_ARCHIVE};
cvar_t r_nolerp_list = {
	"r_nolerp_list",
	"progs/flame.mdl,progs/flame2.mdl,progs/braztall.mdl,progs/brazshrt.mdl,progs/longtrch.mdl,progs/flame_pyre.mdl,progs/v_saw.mdl,progs/"
	"v_xfist.mdl,progs/h2stuff/newfire.mdl",
	CVAR_NONE};

extern cvar_t r_vfog;
// johnfitz

cvar_t gl_zfix = {"gl_zfix", "1", CVAR_ARCHIVE}; // QuakeSpasm z-fighting fix

cvar_t r_lavaalpha = {"r_lavaalpha", "0", CVAR_NONE};
cvar_t r_telealpha = {"r_telealpha", "0", CVAR_NONE};
cvar_t r_slimealpha = {"r_slimealpha", "0", CVAR_NONE};

float map_wateralpha, map_lavaalpha, map_telealpha, map_slimealpha;
float map_fallbackalpha;

qboolean r_drawworld_cheatsafe, r_fullbright_cheatsafe, r_lightmap_cheatsafe; // johnfitz

cvar_t r_scale = {"r_scale", "1", CVAR_ARCHIVE};

cvar_t r_gpulightmapupdate = {"r_gpulightmapupdate", "1", CVAR_NONE};
cvar_t r_rtshadows = {"r_rtshadows", "2", CVAR_ARCHIVE};

cvar_t r_tasks = {"r_tasks", "1", CVAR_NONE};

cvar_t			r_indirect = {"r_indirect", "1", CVAR_NONE};
extern qboolean indirect_ready;

extern SDL_Mutex *draw_qcvm_mutex;

static atomic_uint32_t next_visedict;

/*
=================
R_CullBox -- johnfitz -- replaced with new function from lordhavoc

Returns true if the box is completely outside the frustum
=================
*/
qboolean R_CullBox (vec3_t emins, vec3_t emaxs)
{
	int		  i;
	mplane_t *p;
	byte	  signbits;
	float	  vec[3];
	for (i = 0; i < 4; i++)
	{
		p = frustum + i;
		signbits = p->signbits;
		vec[0] = ((signbits & 1) ? emins : emaxs)[0];
		vec[1] = ((signbits & 2) ? emins : emaxs)[1];
		vec[2] = ((signbits & 4) ? emins : emaxs)[2];
		if (p->normal[0] * vec[0] + p->normal[1] * vec[1] + p->normal[2] * vec[2] < p->dist)
			return true;
	}
	return false;
}
/*
===============
R_CullModelForEntity -- johnfitz -- uses correct bounds based on rotation
===============
*/
qboolean R_CullModelForEntity (entity_t *e)
{
	vec3_t mins, maxs;
	vec_t  scalefactor, *minbounds, *maxbounds;

	if (e->angles[0] || e->angles[2]) // pitch or roll
	{
		minbounds = e->model->rmins;
		maxbounds = e->model->rmaxs;
	}
	else if (e->angles[1]) // yaw
	{
		minbounds = e->model->ymins;
		maxbounds = e->model->ymaxs;
	}
	else // no rotation
	{
		minbounds = e->model->mins;
		maxbounds = e->model->maxs;
	}

	scalefactor = ENTSCALE_DECODE (e->netstate.scale);
	if (scalefactor != 1.0f)
	{
		VectorMA (e->origin, scalefactor, minbounds, mins);
		VectorMA (e->origin, scalefactor, maxbounds, maxs);
	}
	else
	{
		VectorAdd (e->origin, minbounds, mins);
		VectorAdd (e->origin, maxbounds, maxs);
	}

	return R_CullBox (mins, maxs);
}

/*
===============
R_RotateForEntity -- johnfitz -- modified to take origin and angles instead of pointer to entity
===============
*/
void R_RotateForEntity (float matrix[16], vec3_t origin, vec3_t angles, unsigned char scale)
{
	float translation_matrix[16];
	TranslationMatrix (translation_matrix, origin[0], origin[1], origin[2]);
	MatrixMultiply (matrix, translation_matrix);

	float rotation_matrix[16];
	RotationMatrix (rotation_matrix, DEG2RAD (angles[1]), 0, 0, 1);
	MatrixMultiply (matrix, rotation_matrix);
	RotationMatrix (rotation_matrix, DEG2RAD (-angles[0]), 0, 1, 0);
	MatrixMultiply (matrix, rotation_matrix);
	RotationMatrix (rotation_matrix, DEG2RAD (angles[2]), 1, 0, 0);
	MatrixMultiply (matrix, rotation_matrix);

	float scalefactor = ENTSCALE_DECODE (scale);
	if (scalefactor != 1.0f)
	{
		float mscale_matrix[16];
		ScaleMatrix (mscale_matrix, scalefactor, scalefactor, scalefactor);
		MatrixMultiply (matrix, mscale_matrix);
	}
}

//==============================================================================
//
// SETUP FRAME
//
//==============================================================================

int SignbitsForPlane (mplane_t *out)
{
	int bits, j;

	// for fast box on planeside test

	bits = 0;
	for (j = 0; j < 3; j++)
	{
		if (out->normal[j] < 0)
			bits |= 1 << j;
	}
	return bits;
}

/*
===============
TurnVector -- johnfitz

turn forward towards side on the plane defined by forward and side
if angle = 90, the result will be equal to side
assumes side and forward are perpendicular, and normalized
to turn away from side, use a negative angle
===============
*/
#define DEG2RAD(a) ((a) * M_PI_DIV_180)
static void TurnVector (vec3_t out, const vec3_t forward, const vec3_t side, float angle)
{
	float scale_forward, scale_side;

	scale_forward = cos (DEG2RAD (angle));
	scale_side = sin (DEG2RAD (angle));

	out[0] = scale_forward * forward[0] + scale_side * side[0];
	out[1] = scale_forward * forward[1] + scale_side * side[1];
	out[2] = scale_forward * forward[2] + scale_side * side[2];
}

/*
===============
R_SetFrustum -- johnfitz -- rewritten
===============
*/
void R_SetFrustum (float fovx, float fovy)
{
	int i;

	TurnVector (frustum[0].normal, vpn, vright, fovx / 2 - 90); // right plane
	TurnVector (frustum[1].normal, vpn, vright, 90 - fovx / 2); // left plane
	TurnVector (frustum[2].normal, vpn, vup, 90 - fovy / 2);	// bottom plane
	TurnVector (frustum[3].normal, vpn, vup, fovy / 2 - 90);	// top plane

	for (i = 0; i < 4; i++)
	{
		frustum[i].type = PLANE_ANYZ;
		frustum[i].dist = DotProduct (r_origin, frustum[i].normal); // FIXME: shouldn't this always be zero?
		frustum[i].signbits = SignbitsForPlane (&frustum[i]);
	}
}

/*
=============
GL_FrustumMatrix
=============
*/
#define NEARCLIP 4
static void GL_FrustumMatrix (float matrix[16], float fovx, float fovy)
{
	const float w = 1.0f / tanf (fovx * 0.5f);
	const float h = 1.0f / tanf (fovy * 0.5f);

	// reduce near clip distance at high FOV's to avoid seeing through walls
	const float d = 12.f * q_min (w, h);
	const float n = CLAMP (0.5f, d, NEARCLIP);
	const float f = gl_farclip.value;

	memset (matrix, 0, 16 * sizeof (float));

	// First column
	matrix[0 * 4 + 0] = w;

	// Second column
	matrix[1 * 4 + 1] = -h;

	// Third column
	matrix[2 * 4 + 2] = f / (f - n) - 1.0f;
	matrix[2 * 4 + 3] = -1.0f;

	// Fourth column
	matrix[3 * 4 + 2] = (n * f) / (f - n);
}

/*
=============
R_SetupMatrices
=============
*/
static void R_SetupMatrices ()
{
	// Projection matrix
	GL_FrustumMatrix (vulkan_globals.projection_matrix, DEG2RAD (r_fovx), DEG2RAD (r_fovy));

	// View matrix
	float rotation_matrix[16];
	RotationMatrix (vulkan_globals.view_matrix, -M_PI / 2.0f, 1.0f, 0.0f, 0.0f);
	RotationMatrix (rotation_matrix, M_PI / 2.0f, 0.0f, 0.0f, 1.0f);
	MatrixMultiply (vulkan_globals.view_matrix, rotation_matrix);
	RotationMatrix (rotation_matrix, DEG2RAD (-r_refdef.viewangles[2]), 1.0f, 0.0f, 0.0f);
	MatrixMultiply (vulkan_globals.view_matrix, rotation_matrix);
	RotationMatrix (rotation_matrix, DEG2RAD (-r_refdef.viewangles[0]), 0.0f, 1.0f, 0.0f);
	MatrixMultiply (vulkan_globals.view_matrix, rotation_matrix);
	RotationMatrix (rotation_matrix, DEG2RAD (-r_refdef.viewangles[1]), 0.0f, 0.0f, 1.0f);
	MatrixMultiply (vulkan_globals.view_matrix, rotation_matrix);

	float translation_matrix[16];
	TranslationMatrix (translation_matrix, -r_refdef.vieworg[0], -r_refdef.vieworg[1], -r_refdef.vieworg[2]);
	MatrixMultiply (vulkan_globals.view_matrix, translation_matrix);

	// View projection matrix
	memcpy (vulkan_globals.view_projection_matrix, vulkan_globals.projection_matrix, 16 * sizeof (float));
	MatrixMultiply (vulkan_globals.view_projection_matrix, vulkan_globals.view_matrix);
}

/*
=============
R_SetupContext
=============
*/
static void R_SetupContext (cb_context_t *cbx)
{
	GL_Viewport (cbx, r_refdef.vrect.x, glheight - r_refdef.vrect.y - r_refdef.vrect.height, r_refdef.vrect.width, r_refdef.vrect.height, 0.0f, 1.0f);
	R_BindPipeline (cbx, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_globals.basic_blend_pipeline[cbx->render_pass_index]);
	R_PushConstants (cbx, VK_SHADER_STAGE_ALL_GRAPHICS, 0, 16 * sizeof (float), vulkan_globals.view_projection_matrix);
}

static void R_PrepareDebugEntityInfo (void);

/*
===============
R_SetupViewBeforeMark
===============
*/
static void R_SetupViewBeforeMark (void *unused)
{
	// must happen here: in indirect mode draw_world only depends on this task, latching
	// bmodel_instances_index any later would race the read in R_DrawIndirectBrushes
	if (indirect)
		R_ClearBModelInstanceClaims ();

	// Need to do those early because we now update dynamic light maps during R_MarkSurfaces
	if (!r_gpulightmapupdate.value)
		R_PushDlights ();
	R_AnimateLight ();

	// build the transformation matrix for the given view angles
	VectorCopy (r_refdef.vieworg, r_origin);
	AngleVectors (r_refdef.viewangles, vpn, vright, vup);

	// current viewleaf
	r_oldviewleaf = r_viewleaf;
	r_viewleaf = Mod_PointInLeaf (r_origin, cl.worldmodel);

	V_SetContentsColor (r_viewleaf->contents);
	V_CalcBlend ();

	// johnfitz -- calculate r_fovx and r_fovy here
	r_fovx = r_refdef.fov_x;
	r_fovy = r_refdef.fov_y;
	render_warp = false;
	render_scale = (int)r_scale.value;

	if (r_waterwarp.value)
	{
		int contents = r_viewleaf->contents;
		if (contents == CONTENTS_WATER || contents == CONTENTS_SLIME || contents == CONTENTS_LAVA)
		{
			if (r_waterwarp.value == 1)
				render_warp = true;
			else
			{
				// variance is a percentage of width, where width = 2 * tan(fov / 2) otherwise the effect is too dramatic at high FOV and too subtle at low FOV.
				// what a mess!
				r_fovx = atan (tan (DEG2RAD (r_refdef.fov_x) / 2) * (0.97 + sin (cl.time * 1.5) * 0.03)) * 2 / M_PI_DIV_180;
				r_fovy = atan (tan (DEG2RAD (r_refdef.fov_y) / 2) * (1.03 - sin (cl.time * 1.5) * 0.03)) * 2 / M_PI_DIV_180;
			}
		}
	}
	// johnfitz

	R_SetFrustum (r_fovx, r_fovy); // johnfitz -- use r_fov* vars
	R_SetupMatrices ();
	R_PrepareDebugEntityInfo ();

	// johnfitz -- cheat-protect some draw modes
	r_fullbright_cheatsafe = false;
	r_lightmap_cheatsafe = false;
	r_drawworld_cheatsafe = true;
	if (cl.maxclients == 1)
	{
		if (!r_drawworld.value)
			r_drawworld_cheatsafe = false;
		if (r_lightmap.value)
			r_lightmap_cheatsafe = true;
		else if (r_fullbright.value)
			r_fullbright_cheatsafe = true;
	}
	if (!cl.worldmodel->lightdata)
	{
		r_fullbright_cheatsafe = true;
		r_lightmap_cheatsafe = false;
	}
	// johnfitz
}

//==============================================================================
//
// RENDER VIEW
//
//==============================================================================

/*
=============
R_IsEntityTransparent
=============
*/
static qboolean R_IsEntityTransparent (entity_t *e, qboolean *opaque_with_transparent_water)
{
	qboolean transparent = ENTALPHA_DECODE (e->alpha) != 1;
	*opaque_with_transparent_water =
		(!transparent && e->model->type == mod_brush && e->model->used_specials & SURF_DRAWTURB && e->alpha == ENTALPHA_DEFAULT &&
		 ((e->model->used_specials & SURF_DRAWLAVA && GL_WaterAlphaForTextureType (TEXTYPE_LAVA) != 1) ||
		  (e->model->used_specials & SURF_DRAWTELE && GL_WaterAlphaForTextureType (TEXTYPE_TELE) != 1) ||
		  (e->model->used_specials & SURF_DRAWSLIME && GL_WaterAlphaForTextureType (TEXTYPE_SLIME) != 1) ||
		  (e->model->used_specials & SURF_DRAWWATER && GL_WaterAlphaForTextureType (TEXTYPE_WATER) != 1)));
	return transparent;
}

/*
=============
R_DrawEntitiesOnList

alphapass 0 for opaque, 1 for transparent overwater, 2 for transpatent underwater
=============
*/
void R_DrawEntitiesOnList (cb_context_t *cbx, int alphapass, int chain, qboolean use_tasks) // johnfitz -- added parameter
{
	int i = -1;

	if (!r_drawentities.value)
		return;

	int brushpolys = 0;
	int brushpasses = 0;
	int aliaspolys = 0;
	int aliaspasses = 0;

	const int		 total = !alphapass ? cl_numvisedicts : alphapass == 1 ? cl_numvisedicts_alpha_overwater : cl_numvisedicts_alpha_underwater;
	entity_t **const list = !alphapass ? cl_visedicts : alphapass == 1 ? cl_visedicts_alpha : cl_visedicts_alpha + cl_numvisedicts_alpha_overwater;

	R_BeginDebugUtilsLabel (cbx, alphapass ? "Entities Alpha Pass" : "Entities");
	// johnfitz -- sprites are not a special case
	while (true)
	{
		if (use_tasks)
			i = Atomic_IncrementUInt32 (&next_visedict);
		else
			i += 1;

		if (i >= total)
			break;

		entity_t *currententity = list[i];

		qboolean opaque_with_transparent_water;
		qboolean transparent = R_IsEntityTransparent (currententity, &opaque_with_transparent_water);

		// johnfitz -- if alphapass is true, draw only alpha entites this time
		// if alphapass is false, draw only nonalpha entities this time
		if (transparent != !!alphapass && !opaque_with_transparent_water)
			continue;

		// johnfitz -- chasecam
		if (currententity == &cl.entities[cl.viewentity])
			currententity->angles[0] *= 0.3;
		// johnfitz

		// spike -- this would be more efficient elsewhere, but its more correct here.
		if (currententity->eflags & EFLAGS_EXTERIORMODEL)
			continue;

		switch (currententity->model->type)
		{
		case mod_alias:
			R_DrawAliasModel (cbx, currententity, &aliaspolys);
			++aliaspasses;
			break;
		case mod_brush:
			R_DrawBrushModel (
				cbx, currententity, chain, &brushpolys, alphapass && R_UseAlphaSort (), !alphapass && opaque_with_transparent_water,
				alphapass && opaque_with_transparent_water);
			++brushpasses;
			break;
		case mod_sprite:
			R_DrawSpriteModel (cbx, currententity);
			break;
		}
	}
	R_EndDebugUtilsLabel (cbx);

	Atomic_AddUInt32 (&rs_brushpolys, brushpolys);
	Atomic_AddUInt32 (&rs_brushpasses, brushpasses);
	Atomic_AddUInt32 (&rs_aliaspolys, aliaspolys);
	Atomic_AddUInt32 (&rs_aliaspasses, aliaspasses);
}

/*
=============
R_DrawViewModel -- johnfitz -- gutted
=============
*/
void R_DrawViewModel (cb_context_t *cbx)
{
	if (!r_drawviewmodel.value || !r_drawentities.value || chase_active.value || scr_viewsize.value >= 130)
		return;

	if (cl.items & IT_INVISIBILITY || cl.stats[STAT_HEALTH] <= 0)
		return;

	entity_t *currententity = &cl.viewent;
	if (!currententity->model)
		return;

	// johnfitz -- this fixes a crash
	if (currententity->model->type != mod_alias)
		return;
	// johnfitz

	R_BeginDebugUtilsLabel (cbx, "View Model");

	// hack the depth range to prevent view model from poking into walls
	GL_Viewport (cbx, r_refdef.vrect.x, glheight - r_refdef.vrect.y - r_refdef.vrect.height, r_refdef.vrect.width, r_refdef.vrect.height, 0.7f, 1.0f);

	int aliaspolys = 0;
	R_DrawAliasModel (cbx, currententity, &aliaspolys);
	Atomic_AddUInt32 (&rs_aliaspolys, aliaspolys);
	Atomic_IncrementUInt32 (&rs_aliaspasses);

	GL_Viewport (cbx, r_refdef.vrect.x, glheight - r_refdef.vrect.y - r_refdef.vrect.height, r_refdef.vrect.width, r_refdef.vrect.height, 0.0f, 1.0f);

	R_EndDebugUtilsLabel (cbx);
}

/*
================
R_FillDebugVertex
================
*/
static void R_FillDebugVertex (basicvertex_t *vertex, const vec3_t position, uint32_t color)
{
	VectorCopy (position, vertex->position);
	vertex->texcoord[0] = vertex->texcoord[1] = 0.0f;
	vertex->color[0] = (byte)(color >> 24);
	vertex->color[1] = (byte)(color >> 16);
	vertex->color[2] = (byte)(color >> 8);
	vertex->color[3] = (byte)color;
}

/*
================
R_EmitWirePoint -- johnfitz -- draws a wireframe cross shape for point entities
================
*/
static void R_EmitWirePoint (cb_context_t *cbx, const vec3_t origin, uint32_t color)
{
	VkBuffer	   vertex_buffer;
	VkDeviceSize   vertex_buffer_offset;
	basicvertex_t *vertices = (basicvertex_t *)R_VertexAllocate (6 * sizeof (basicvertex_t), &vertex_buffer, &vertex_buffer_offset);
	const float	   size = 8.0f;
	vec3_t		   positions[6];

	VectorCopy (origin, positions[0]);
	VectorCopy (origin, positions[1]);
	VectorCopy (origin, positions[2]);
	VectorCopy (origin, positions[3]);
	VectorCopy (origin, positions[4]);
	VectorCopy (origin, positions[5]);
	positions[0][0] -= size;
	positions[1][0] += size;
	positions[2][1] -= size;
	positions[3][1] += size;
	positions[4][2] -= size;
	positions[5][2] += size;

	for (int i = 0; i < countof (positions); i++)
		R_FillDebugVertex (&vertices[i], positions[i], color);

	vulkan_globals.vk_cmd_bind_vertex_buffers (cbx->cb, 0, 1, &vertex_buffer, &vertex_buffer_offset);
	vulkan_globals.vk_cmd_draw (cbx->cb, 6, 1, 0, 0);
}

/*
================
R_EmitWireBox -- johnfitz -- draws one axis aligned bounding box
================
*/
static void
R_EmitWireBox (cb_context_t *cbx, const vec3_t mins, const vec3_t maxs, VkBuffer box_index_buffer, VkDeviceSize box_index_buffer_offset, uint32_t color)
{
	VkBuffer	   vertex_buffer;
	VkDeviceSize   vertex_buffer_offset;
	basicvertex_t *vertices = (basicvertex_t *)R_VertexAllocate (8 * sizeof (basicvertex_t), &vertex_buffer, &vertex_buffer_offset);

	for (int i = 0; i < 8; ++i)
	{
		vertices[i].position[0] = ((i % 2) < 1) ? mins[0] : maxs[0];
		vertices[i].position[1] = ((i % 4) < 2) ? mins[1] : maxs[1];
		vertices[i].position[2] = ((i % 8) < 4) ? mins[2] : maxs[2];
		vertices[i].texcoord[0] = vertices[i].texcoord[1] = 0.0f;
		vertices[i].color[0] = (byte)(color >> 24);
		vertices[i].color[1] = (byte)(color >> 16);
		vertices[i].color[2] = (byte)(color >> 8);
		vertices[i].color[3] = (byte)color;
	}

	vulkan_globals.vk_cmd_bind_index_buffer (cbx->cb, box_index_buffer, box_index_buffer_offset, VK_INDEX_TYPE_UINT16);
	vulkan_globals.vk_cmd_bind_vertex_buffers (cbx->cb, 0, 1, &vertex_buffer, &vertex_buffer_offset);
	vulkan_globals.vk_cmd_draw_indexed (cbx->cb, 24, 1, 0, 0, 0);
}

static uint16_t box_indices[24] = {0, 1, 2, 3, 4, 5, 6, 7, 0, 4, 1, 5, 2, 6, 3, 7, 0, 2, 1, 3, 4, 6, 5, 7};

/*
================
R_RayVsBox
================
*/
static qboolean R_RayVsBox (const vec3_t org, const vec3_t rcpdelta, const vec3_t mins, const vec3_t maxs, float *frac)
{
	float enter, exit;

	if (frac)
		*frac = 1.0f;

	enter = 0.0f;
	exit = 1.0f;

	for (int i = 0; i < 3; i++)
	{
		const float t0 = (mins[i] - org[i]) * rcpdelta[i];
		const float t1 = (maxs[i] - org[i]) * rcpdelta[i];
		const float tmin = q_min (t0, t1);
		const float tmax = q_max (t0, t1);
		enter = q_max (enter, tmin);
		exit = q_min (exit, tmax);
	}

	if (enter > exit)
		return false;

	if (frac)
		*frac = enter;

	return true;
}

/*
================
R_EmitArrow
================
*/
static void R_EmitArrow (cb_context_t *cbx, const vec3_t from, const vec3_t to, uint32_t color)
{
	VkBuffer	   vertex_buffer;
	VkDeviceSize   vertex_buffer_offset;
	basicvertex_t *vertices = (basicvertex_t *)R_VertexAllocate (6 * sizeof (basicvertex_t), &vertex_buffer, &vertex_buffer_offset);
	float		   frac, len;
	vec3_t		   center, dir, side, tmp;
	vec3_t		   positions[6];

	VectorCopy (from, positions[0]);
	VectorCopy (to, positions[1]);

	VectorSubtract (to, from, dir);
	len = VectorNormalize (dir);
	if (len < 1e-2f)
	{
		VectorCopy (vup, dir);
		VectorCopy (vright, side);
	}
	else
	{
		VectorSubtract (from, r_origin, tmp);
		CrossProduct (dir, tmp, side);
		VectorNormalize (side);
	}

	frac = (float)(realtime - floor (realtime));
	for (int i = 0; i < 3; i++)
		center[i] = from[i] + (to[i] - from[i]) * frac;

	VectorMA (center, 8.0f, side, tmp);
	VectorMA (tmp, -8.0f, dir, tmp);
	VectorCopy (tmp, positions[2]);
	VectorCopy (center, positions[3]);

	VectorMA (tmp, -16.0f, side, tmp);
	VectorCopy (tmp, positions[4]);
	VectorCopy (center, positions[5]);

	for (int i = 0; i < countof (positions); i++)
		R_FillDebugVertex (&vertices[i], positions[i], color);

	vulkan_globals.vk_cmd_bind_vertex_buffers (cbx->cb, 0, 1, &vertex_buffer, &vertex_buffer_offset);
	vulkan_globals.vk_cmd_draw (cbx->cb, 6, 1, 0, 0);
}

/*
================
R_EdictCenter
================
*/
static void R_EdictCenter (const edict_t *ed, vec3_t center)
{
	VectorCopy (ed->v.origin, center);
	if (!VectorCompare (ed->v.mins, ed->v.maxs))
	{
		VectorMA (center, 0.5f, ed->v.mins, center);
		VectorMA (center, 0.5f, ed->v.maxs, center);
	}
}

/*
================
R_EmitEdictLink
================
*/
static void R_EmitEdictLink (cb_context_t *cbx, const edict_t *from, const edict_t *to, showbboxflags_t flags)
{
	vec3_t vec_from, vec_to;

	if (!flags)
		return;

	R_EdictCenter (from, vec_from);
	R_EdictCenter (to, vec_to);

	if (flags == SHOWBBOX_LINK_BOTH)
	{
		VkBuffer	   vertex_buffer;
		VkDeviceSize   vertex_buffer_offset;
		basicvertex_t *vertices = (basicvertex_t *)R_VertexAllocate (2 * sizeof (basicvertex_t), &vertex_buffer, &vertex_buffer_offset);

		R_FillDebugVertex (&vertices[0], vec_from, 0x7f7f3f7fu);
		R_FillDebugVertex (&vertices[1], vec_to, 0x7f7f3f7fu);

		vulkan_globals.vk_cmd_bind_vertex_buffers (cbx->cb, 0, 1, &vertex_buffer, &vertex_buffer_offset);
		vulkan_globals.vk_cmd_draw (cbx->cb, 2, 1, 0, 0);
	}
	else if (flags == SHOWBBOX_LINK_OUTGOING)
		R_EmitArrow (cbx, vec_from, vec_to, 0x7f7f3f3fu);
	else if (flags == SHOWBBOX_LINK_INCOMING)
		R_EmitArrow (cbx, vec_to, vec_from, 0x7f3f3f7fu);
}

/*
================
R_ShowBoundingBoxesFilter

r_showbboxes_filter "artifact,=trigger_secret"
================
 */
char	*r_showbboxes_filter_strings = NULL;
qboolean r_showbboxes_filter_byindex = false;

static qboolean R_ShowBoundingBoxesFilter (edict_t *ed)
{
	char		entnum[16] = "";
	const char *classname = NULL;
	const char *filter_p = r_showbboxes_filter_strings;

	if (!r_showbboxes_filter_strings || !r_showbboxes_filter_strings[0])
		return true;

	if (r_showbboxes_filter_byindex)
		q_snprintf (entnum, sizeof (entnum), "%d", NUM_FOR_EDICT (ed));

	if (ed->v.classname)
		classname = PR_GetString (ed->v.classname);

	for (filter_p = r_showbboxes_filter_strings; *filter_p; filter_p += strlen (filter_p) + 1)
	{
		if (*filter_p == '#')
		{
			if (!strcmp (entnum, filter_p + 1))
				return true;
			continue;
		}

		if (!classname)
			continue;

		if (*filter_p == '=')
		{
			if (!strcmp (classname, filter_p + 1))
				return true;
			continue;
		}

		if (strstr (classname, filter_p) != NULL)
			return true;
	}

	return false;
}

static edict_t **bbox_edicts = NULL;
edict_t		   **bbox_linked = NULL;
static edict_t	*bbox_focused = NULL;

void R_ClearDebugEntityInfo (void)
{
	VEC_CLEAR (bbox_edicts);
	VEC_CLEAR (bbox_linked);
	bbox_focused = NULL;
}

/*
================
R_AddHighlightedEntity
================
*/
static void R_AddHighlightedEntity (edict_t *ed, showbboxflags_t flags)
{
	if (ed->showbboxframe != r_framecount)
	{
		ed->showbboxframe = r_framecount;
		ed->showbboxflags = SHOWBBOX_LINK_NONE;
		VEC_PUSH (bbox_edicts, ed);
	}

	if (!(ed->showbboxflags & flags) && (int)r_showbboxes_links.value & flags)
	{
		VEC_PUSH (bbox_linked, ed);
		ed->showbboxflags |= flags;
	}
}

/*
================
R_PrepareDebugEntityInfo

Find entities and links used by r_showbboxes/r_showfields. This runs before the
parallel draw tasks so the 3D debug pass and GUI overlay read stable state.
================
*/
static void R_PrepareDebugEntityInfo (void)
{
	extern edict_t *sv_player;
	vec3_t			mins, maxs, rcpdelta;
	edict_t		   *ed;
	byte		   *pvs;
	int				i, j, mode;
	float			dist, bestdist;

	R_ClearDebugEntityInfo ();

	mode = abs ((int)r_showbboxes.value);
	if ((!mode && !r_showfields.value) || cl.maxclients > 1 || !r_drawentities.value || !sv.active)
		return;

	SDL_LockMutex (draw_qcvm_mutex);
	PR_SwitchQCVM (&sv.qcvm);

	if (mode >= 2 || mode == 0)
	{
		vec3_t org;
		VectorAdd (sv_player->v.origin, sv_player->v.view_ofs, org);
		pvs = SV_FatPVS (org, qcvm->worldmodel);
	}
	else
	{
		pvs = NULL;
	}

	for (i = 0; i < 3; i++)
		rcpdelta[i] = 1.0f / (gl_farclip.value * vpn[i]);

	bestdist = FLT_MAX;
	for (i = 1, ed = NEXT_EDICT (qcvm->edicts); i < qcvm->num_edicts; i++, ed = NEXT_EDICT (ed))
	{
		if (ed == sv_player || ed->free)
			continue;

		if (r_showbboxes_think.value && (ed->v.nextthink <= 0) == (r_showbboxes_think.value > 0))
			continue;

		if (r_showbboxes_health.value && (ed->v.health <= 0) == (r_showbboxes_health.value > 0))
			continue;

		for (j = 0; j < 3; j++)
		{
			const float extend = VectorCompare (ed->v.mins, ed->v.maxs) ? 8.0f : 0.0f;
			mins[j] = ed->v.origin[j] + ed->v.mins[j] - extend;
			maxs[j] = ed->v.origin[j] + ed->v.maxs[j] + extend;
		}

		if (R_CullBox (mins, maxs))
			continue;

		if (!R_ShowBoundingBoxesFilter (ed))
			continue;

		if (pvs)
		{
			qboolean inpvs = ed->num_leafs ? SV_EdictInPVS (ed, pvs) : SV_BoxInPVS (ed->v.absmin, ed->v.absmax, pvs, qcvm->worldmodel);
			if (!inpvs)
				continue;
		}

		if (R_RayVsBox (r_origin, rcpdelta, mins, maxs, &dist) && dist > 0.0f && dist < bestdist)
		{
			bestdist = dist;
			bbox_focused = ed;
		}

		R_AddHighlightedEntity (ed, SHOWBBOX_LINK_NONE);
	}

	if (bbox_focused)
		VEC_PUSH (bbox_linked, bbox_focused);

	if (bbox_focused && r_showbboxes_links.value)
	{
		if ((int)r_showbboxes_links.value & SHOWBBOX_LINK_OUTGOING)
		{
			for (i = 0; i < qcvm->numentityfields; i++)
			{
				eval_t *val = (eval_t *)((char *)&bbox_focused->v + qcvm->entityfieldofs[i]);
				if (qcvm->entityfieldofs[i] == offsetof (entvars_t, chain) || !val->edict)
					continue;
				ed = PROG_TO_EDICT (val->edict);
				if (ed == bbox_focused || ed->free || ed == sv_player)
					continue;
				R_AddHighlightedEntity (ed, SHOWBBOX_LINK_OUTGOING);
			}
		}

		if ((int)r_showbboxes_links.value & SHOWBBOX_LINK_INCOMING || r_showbboxes_targets.value)
		{
			const char *focus_target = PR_GetString (bbox_focused->v.target);
			const char *focus_targetname = PR_GetString (bbox_focused->v.targetname);

			for (i = 1, ed = NEXT_EDICT (qcvm->edicts); i < qcvm->num_edicts; i++, ed = NEXT_EDICT (ed))
			{
				if (ed == sv_player || ed->free || ed == bbox_focused)
					continue;

				if (r_showbboxes_targets.value && (*focus_target || *focus_targetname))
				{
					const char *target = PR_GetString (ed->v.target);
					const char *targetname = PR_GetString (ed->v.targetname);

					if (*focus_targetname && !strcmp (focus_targetname, target))
						R_AddHighlightedEntity (ed, SHOWBBOX_LINK_INCOMING);
					if (*focus_target && !strcmp (focus_target, targetname))
						R_AddHighlightedEntity (ed, SHOWBBOX_LINK_OUTGOING);
				}

				if ((int)r_showbboxes_links.value & SHOWBBOX_LINK_INCOMING)
				{
					for (j = 0; j < qcvm->numentityfields; j++)
					{
						eval_t *val = (eval_t *)((char *)&ed->v + qcvm->entityfieldofs[j]);
						if (qcvm->entityfieldofs[j] == offsetof (entvars_t, chain) || !val->edict)
							continue;
						if (PROG_TO_EDICT (val->edict) == bbox_focused)
							R_AddHighlightedEntity (ed, SHOWBBOX_LINK_INCOMING);
					}
				}
			}
		}
	}

	PR_SwitchQCVM (NULL);
	SDL_UnlockMutex (draw_qcvm_mutex);
}

/*
================
R_ShowBoundingBoxes -- johnfitz

draw bounding boxes -- the server-side boxes, not the renderer cullboxes
================
*/
static void R_ShowBoundingBoxes (cb_context_t *cbx)
{
	vec3_t	 mins, maxs, center;
	edict_t *ed;
	int		 i, j, pass, mode;
	uint32_t color;

	mode = abs ((int)r_showbboxes.value);
	if ((!mode && !r_showfields.value) || VEC_SIZE (bbox_edicts) == 0)
		return;

	R_BeginDebugUtilsLabel (cbx, "show bboxes");
	R_BindPipeline (cbx, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_globals.debug_lines_pipeline[R_MainPassPipelineVariant (cbx->render_pass_index)]);

	VkBuffer	 box_index_buffer;
	VkDeviceSize box_index_buffer_offset;
	uint16_t	*indices = (uint16_t *)R_IndexAllocate (24 * sizeof (uint16_t), &box_index_buffer, &box_index_buffer_offset);
	memcpy (indices, box_indices, 24 * sizeof (uint16_t));

	if (bbox_focused && r_showbboxes_links.value)
		for (j = 0; j < (int)VEC_SIZE (bbox_linked); j++)
			R_EmitEdictLink (cbx, bbox_focused, bbox_linked[j], bbox_linked[j]->showbboxflags);

	for (pass = 0; pass < 2; pass++) // two passes (0 = lines, 1 = text) to avoid switching pipelines for every edict and so that the text is on top
	{
		if (pass == 1 && r_showbboxes.value < 0)
			continue;
		for (i = 0; i < (int)VEC_SIZE (bbox_edicts); i++)
		{
			ed = bbox_edicts[i];
			if (ed == bbox_focused)
				color = 0xffffffffu;
			else if (ed->showbboxflags)
				color = 0xaaaaaaaau;
			else if (r_showbboxes.value > 0.0f)
			{
				int modelindex = (int)ed->v.modelindex;
				color = 0x7f800080u;
				if (modelindex >= 0 && modelindex < MAX_MODELS && sv.models[modelindex])
				{
					switch (sv.models[modelindex]->type)
					{
					case mod_brush:
						color = 0x7fff8080u;
						break;
					case mod_alias:
						color = 0x7f408080u;
						break;
					case mod_sprite:
						color = 0x7f4040ffu;
						break;
					default:
						break;
					}
				}
				if (ed->v.health > 0)
					color = 0x7f0000ffu;
			}
			else if (r_showbboxes.value < 0.0f)
				color = 0x7fffffffu;
			else
				color = 0x5f7f7f7fu;

			if (ed->v.mins[0] == ed->v.maxs[0] && ed->v.mins[1] == ed->v.maxs[1] && ed->v.mins[2] == ed->v.maxs[2])
			{
				// point entity
				if (pass == 0)
				{
					R_EmitWirePoint (cbx, ed->v.origin, color);
				}
				else
				{
					VectorCopy (ed->v.origin, center);
					center[2] += 16; // show a bit above
				}
			}
			else
			{
				// box entity
				VectorAdd (ed->v.mins, ed->v.origin, mins);
				VectorAdd (ed->v.maxs, ed->v.origin, maxs);
				if (pass == 0)
				{
					R_EmitWireBox (cbx, mins, maxs, box_index_buffer, box_index_buffer_offset, color);
				}
				else
				{
					VectorAdd (mins, maxs, center);
					for (int axis = 0; axis < 3; axis++)
						center[axis] /= 2;
				}
			}

			if (pass == 1)
			{
				char text[16];
				q_snprintf (text, sizeof (text), "%i", i);
				for (char *c = text; *c; c++)
					*c |= 0x80; // the lines are already white so gold is more legible
				Draw_String_3D (cbx, center, 8, text);
			}
		}
	}

	R_EndDebugUtilsLabel (cbx);
}

/*
===============
R_ShowPointFile
===============
*/
static void R_ShowPointFile (cb_context_t *cbx)
{
	size_t i;

	if (VEC_SIZE (r_pointfile) == 0)
		return;

	R_BeginDebugUtilsLabel (cbx, "pointfile");
	R_BindPipeline (cbx, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_globals.debug_lines_pipeline[R_MainPassPipelineVariant (cbx->render_pass_index)]);
	for (i = 1; i < VEC_SIZE (r_pointfile); i++)
		R_EmitArrow (cbx, r_pointfile[i - 1], r_pointfile[i], 0xff3f3f7fu);
	R_EndDebugUtilsLabel (cbx);
}

/*
================
R_ShowTris -- johnfitz
================
 */
void R_ShowTris (cb_context_t *cbx)
{
	extern cvar_t r_particles;
	int			  i;

	if (r_showtris.value < 1 || r_showtris.value > 2 || cl.maxclients > 1 || !vulkan_globals.non_solid_fill)
		return;

	R_BeginDebugUtilsLabel (cbx, "show tris");
	if (indirect)
		R_DrawIndirectBrushes_ShowTris (cbx);
	else if (r_drawworld.value)
		R_DrawWorld_ShowTris (cbx);

	if (r_drawentities.value)
	{
		for (i = 0; i < cl_numvisedicts; i++)
		{
			entity_t *currententity = cl_visedicts[i];

			if (currententity == &cl.entities[cl.viewentity]) // chasecam
				currententity->angles[0] *= 0.3;

			switch (currententity->model->type)
			{
			case mod_brush:
				R_DrawBrushModel_ShowTris (cbx, currententity);
				break;
			case mod_alias:
				R_DrawAliasModel_ShowTris (cbx, currententity);
				break;
			case mod_sprite:
				R_DrawSpriteModel_ShowTris (cbx, currententity);
				break;
			default:
				break;
			}
		}

		// viewmodel
		entity_t *currententity = &cl.viewent;
		if (r_drawviewmodel.value && !chase_active.value && cl.stats[STAT_HEALTH] > 0 && !(cl.items & IT_INVISIBILITY) && currententity->model &&
			currententity->model->type == mod_alias && scr_viewsize.value < 130)
		{
			R_DrawAliasModel_ShowTris (cbx, currententity);
		}
	}

	if (r_particles.value)
	{
		R_DrawParticles_ShowTris (cbx);
#ifdef PSET_SCRIPT
		PScript_DrawParticles_ShowTris (cbx);
#endif
	}

	R_EndDebugUtilsLabel (cbx);
}

/*
================
R_ShowSkeletons
================
*/
static void R_ShowSkeletons (cb_context_t *cbx)
{
	if (!r_showskel.value || cl.maxclients > 1 || !r_drawentities.value)
		return;

	R_BeginDebugUtilsLabel (cbx, "show skeletons");
	for (int i = 0; i < cl_numvisedicts; i++)
	{
		entity_t *currententity = cl_visedicts[i];
		if (currententity->model->type == mod_alias)
			R_DrawAliasModel_ShowSkel (cbx, currententity);
	}
	R_EndDebugUtilsLabel (cbx);
}

/*
================
R_DrawWorldTask
================
*/
static void R_DrawWorldTask (int index, void *use_tasks)
{
	cb_context_t *cbx = &vulkan_globals.secondary_cb_contexts[SCBX_WORLD][index];
	R_SetupContext (cbx);
	Fog_EnableGFog (cbx);
	if (indirect)
		R_DrawIndirectBrushes (cbx, false, false, false, use_tasks ? index : -1);
	else
		R_DrawWorld (cbx, index);
}

/*
================
R_DrawSkyTask
================
*/
static void R_DrawSkyTask (void *unused)
{
	cb_context_t *cbx = vulkan_globals.secondary_cb_contexts[SCBX_SKY];
	R_SetupContext (cbx);
	Fog_EnableGFog (cbx);
	R_DrawWorld_Water (cbx, false); // draw opaque water before sky (more likely to occlude)
	Sky_DrawSky (cbx);
}

/*
================
R_DrawWaterTask
================
*/
static void R_DrawWaterTask (void *unused)
{
	cb_context_t *cbx = vulkan_globals.secondary_cb_contexts[SCBX_WATER];
	R_SetupContext (cbx);
	Fog_EnableGFog (cbx);
	R_DrawWorld_Water (cbx, true); // transparent worldmodel water only

	if (R_UseMBOIT ())
	{
		cbx = vulkan_globals.secondary_cb_contexts[SCBX_MBOIT_COMPOSITE_WATER];
		R_SetupContext (cbx);
		Fog_EnableGFog (cbx);
		R_DrawWorld_Water (cbx, true);
	}
}

/*
================
R_SortAlphaEntitiesTask
================
*/
static void R_SortAlphaEntitiesTask (void *unused)
{
	typedef struct
	{
		int		 visedict;
		unsigned sortkey;
	} transp_sort;
	const qboolean sort_alpha = R_UseAlphaSort ();
	cl_numvisedicts_alpha_overwater = cl_numvisedicts_alpha_underwater = 0;
	TEMP_ALLOC_COND (transp_sort, edicts, cl_numvisedicts * 2, sort_alpha);
	int sort_bins[3][128];
	if (sort_alpha)
		memset (sort_bins, 0, sizeof (sort_bins));
	for (int i = 0; i < cl_numvisedicts; ++i)
	{
		entity_t *currententity = cl_visedicts[i];

		qboolean opaque_with_transparent_water;
		qboolean transparent = R_IsEntityTransparent (currententity, &opaque_with_transparent_water);

		if (!transparent && !opaque_with_transparent_water)
			continue;
		if (currententity->eflags & EFLAGS_EXTERIORMODEL)
			continue;
		// box culling here is not safe (R_DrawAliasModel updates lerp information)

		if (!sort_alpha)
		{
			cl_visedicts_alpha[cl_numvisedicts_alpha_overwater++] = cl_visedicts[i];
			continue;
		}

		vec3_t		center;
		const float scalefactor = ENTSCALE_DECODE (currententity->netstate.scale);
		float		dist_squared = 0;
		for (int j = 0; j < 3; ++j)
		{
			const float mins = currententity->origin[j] + scalefactor * currententity->model->mins[j];
			const float maxs = currententity->origin[j] + scalefactor * currententity->model->maxs[j];
			center[j] = (mins + maxs) / 2;
			const float dist = q_max (0.0f, q_max (mins - r_refdef.vieworg[j], r_refdef.vieworg[j] - maxs));
			dist_squared += dist * dist;
		}
		int contents;
		if (currententity->contentscache < 0 && memcmp (currententity->contentscache_origin, center, sizeof (vec3_t)) == 0)
		{
			contents = currententity->contentscache;
		}
		else
		{
			currententity->contentscache = contents = Mod_PointInLeaf (center, cl.worldmodel)->contents;
			memcpy (currententity->contentscache_origin, center, sizeof (vec3_t));
		}
		const qboolean underwater = contents == CONTENTS_WATER || contents == CONTENTS_SLIME || contents == CONTENTS_LAVA;
		const unsigned dist = sqrtf (dist_squared) * 2.0f;
		const unsigned sortkey = !underwater << 20 | q_min (dist, (1 << 20) - 1);
		sort_bins[2][(sortkey >> 14)] += 1;
		sort_bins[1][(sortkey >> 7) % 128] += 1;
		sort_bins[0][(sortkey >> 0) % 128] += 1;
		transp_sort *const edict = &edicts[cl_numvisedicts_alpha_overwater + cl_numvisedicts_alpha_underwater];
		edict->visedict = i;
		edict->sortkey = sortkey;
		if (underwater)
			++cl_numvisedicts_alpha_underwater;
		else
			++cl_numvisedicts_alpha_overwater;
	}

	if (!sort_alpha)
		return;

	const int highest = cl_numvisedicts_alpha_underwater + cl_numvisedicts_alpha_overwater - 1;
	for (int pass = 0; pass < 3; ++pass)
	{
		transp_sort *from = pass % 2 ? edicts + cl_numvisedicts : edicts;
		transp_sort *to = pass % 2 ? edicts : edicts + cl_numvisedicts;
		for (int i = 1; i < 128; ++i)
			sort_bins[pass][i] += sort_bins[pass][i - 1];
		for (int i = highest; i >= 0; --i)
		{
			int key = (from[i].sortkey >> 7 * pass) % 128;
			sort_bins[pass][key] -= 1;
			if (pass < 2)
				to[sort_bins[pass][key]] = from[i];
			else
				cl_visedicts_alpha[highest - sort_bins[pass][key]] = cl_visedicts[from[i].visedict];
		}
	}

	TEMP_FREE (edicts);
}

/*
================
R_DrawEntitiesTask
================
*/
static void R_DrawEntitiesTask (int index, void *use_tasks)
{
	cb_context_t *cbx = &vulkan_globals.secondary_cb_contexts[SCBX_ENTITIES][index];
	R_SetupContext (cbx);
	Fog_EnableGFog (cbx); // johnfitz
	R_DrawEntitiesOnList (cbx, false, index + chain_model_0, use_tasks ? true : false);
}

/*
================
R_DrawAlphaEntitiesTask
================
*/
static void R_DrawAlphaEntitiesTask (int index, void *use_tasks)
{
	const int	   contents = r_viewleaf->contents;
	const qboolean underwater = R_UseAlphaSort () && (contents == CONTENTS_WATER || contents == CONTENTS_SLIME || contents == CONTENTS_LAVA);
	for (int i = use_tasks ? index : 0; i <= (use_tasks ? index : 1); ++i)
	{
		cb_context_t *cbx = vulkan_globals.secondary_cb_contexts[i ? SCBX_ALPHA_ENTITIES : SCBX_ALPHA_ENTITIES_ACROSS_WATER];
		R_SetupContext (cbx);
		Fog_EnableGFog (cbx);
		R_DrawEntitiesOnList (cbx, underwater ? 1 + i : 2 - i, i ? chain_alpha_model : chain_alpha_model_across_water, false);

		if (R_UseMBOIT ())
		{
			cbx = vulkan_globals.secondary_cb_contexts[i ? SCBX_MBOIT_COMPOSITE_ALPHA_ENTITIES : SCBX_MBOIT_COMPOSITE_ALPHA_ENTITIES_ACROSS_WATER];
			R_SetupContext (cbx);
			Fog_EnableGFog (cbx);
			R_DrawEntitiesOnList (cbx, underwater ? 1 + i : 2 - i, i ? chain_alpha_model : chain_alpha_model_across_water, false);
		}
	}
}

/*
================
R_DrawParticlesTask
================
*/
static void R_DrawParticlesTask (void *unused)
{
	cb_context_t *cbx = vulkan_globals.secondary_cb_contexts[SCBX_PARTICLES];
	R_SetupContext (cbx);
	Fog_EnableGFog (cbx); // johnfitz
	R_DrawParticles (cbx);

	if (R_UseMBOIT ())
	{
		// MBOIT needs all transparent geometry a second time in the composite pass
		cb_context_t *composite_cbx = vulkan_globals.secondary_cb_contexts[SCBX_MBOIT_COMPOSITE_PARTICLES];
		R_SetupContext (composite_cbx);
		Fog_EnableGFog (composite_cbx);
		R_DrawParticles (composite_cbx);
	}
#ifdef PSET_SCRIPT
	cb_context_t *fte_blend_cbx = NULL;
	if (R_UseOIT ())
	{
		// the blend context is recorded for the resolve subpass, so only set the viewport here:
		// R_SetupContext would bind a pipeline created for subpass 0
		fte_blend_cbx = vulkan_globals.secondary_cb_contexts[SCBX_FTE_PARTICLES_BLEND];
		GL_Viewport (
			fte_blend_cbx, r_refdef.vrect.x, glheight - r_refdef.vrect.y - r_refdef.vrect.height, r_refdef.vrect.width, r_refdef.vrect.height, 0.0f, 1.0f);
	}
	PScript_DrawParticles (R_UseOIT () ? fte_blend_cbx : cbx, NULL);
#endif
}

/*
================
R_DrawViewModelTask
================
*/
static void R_DrawViewModelTask (void *unused)
{
	cb_context_t *cbx = vulkan_globals.secondary_cb_contexts[SCBX_VIEW_MODEL];
	R_SetupContext (cbx);
	R_DrawViewModel (cbx); // johnfitz -- moved here from R_RenderView
	R_ShowTris (cbx);	   // johnfitz
	R_ShowSkeletons (cbx);
	R_ShowBoundingBoxes (cbx); // johnfitz
	R_ShowPointFile (cbx);
}

/*
================
R_PrintStats
================
*/
static void R_PrintStats (void)
{
	// johnfitz -- modified scr_speeds output
	double		 lms = r_gpulightmapupdate.value
						   ? (double)Atomic_LoadUInt32 (&rs_dynamiclightmaps) / (LMBLOCK_HEIGHT / LM_CULL_BLOCK_H * LMBLOCK_WIDTH / LM_CULL_BLOCK_W)
						   : Atomic_LoadUInt32 (&rs_dynamiclightmaps);
	const double cpu_ms = (double)rs_cputime_us / 1000.0;
	const double gpu_ms = (double)rs_gputime_us / 1000.0;
	const double gpu_wait_ms = (double)rs_gpuwaittime_us / 1000.0;
	rs_display_numlines = 0;
	if (r_pos.value)
		Con_Printf (
			"x %i y %i z %i (pitch %i yaw %i roll %i)\n", (int)cl.entities[cl.viewentity].origin[0], (int)cl.entities[cl.viewentity].origin[1],
			(int)cl.entities[cl.viewentity].origin[2], (int)cl.viewangles[PITCH], (int)cl.viewangles[YAW], (int)cl.viewangles[ROLL]);
	else if (scr_speeds.value)
	{
		q_snprintf (rs_display_lines[0], sizeof (rs_display_lines[0]), "cpu%6.2f gpu%6.2f wait%6.2f ms", cpu_ms, gpu_ms, gpu_wait_ms);
		if (scr_speeds.value == 2)
		{
			q_snprintf (
				rs_display_lines[1], sizeof (rs_display_lines[1]), "%4u/%u wpoly %4u/%u epoly", rs_brushpolys, rs_brushpasses, rs_aliaspolys, rs_aliaspasses);
			q_snprintf (rs_display_lines[2], sizeof (rs_display_lines[2]), "%5.3g lmap %4u skypoly", lms, rs_skypolys);
			rs_display_numlines = 3;
		}
		else
		{
			q_snprintf (rs_display_lines[1], sizeof (rs_display_lines[1]), "%4u wpoly %4u epoly %5.3g lmap", rs_brushpolys, rs_aliaspolys, lms);
			rs_display_numlines = 2;
		}
	}
	// johnfitz
}

/*
================
R_RenderView
================
*/
void R_RenderView (
	qboolean use_tasks, task_handle_t begin_rendering_task, task_handle_t setup_frame_task, task_handle_t draw_done_task, task_handle_t draw_gui_task)
{
	static qboolean stats_ready;

	indirect = r_indirect.value && indirect_ready && r_gpulightmapupdate.value && !scr_speeds.value;

	if (!cl.worldmodel)
		Sys_Error ("R_RenderView: NULL worldmodel");

	if (scr_speeds.value)
		rs_frame_starttime = Sys_DoubleTime ();

	if (use_tasks && (r_pos.value || stats_ready))
		R_PrintStats (); // stats and frame times of the last completed frame

	if (scr_speeds.value)
	{
		// johnfitz -- rendering statistics
		Atomic_StoreUInt32 (&rs_brushpolys, 0u);
		Atomic_StoreUInt32 (&rs_aliaspolys, 0u);
		Atomic_StoreUInt32 (&rs_skypolys, 0u);
		Atomic_StoreUInt32 (&rs_particles, 0u);
		Atomic_StoreUInt32 (&rs_fogpolys, 0u);
		Atomic_StoreUInt32 (&rs_dynamiclightmaps, 0u);
		Atomic_StoreUInt32 (&rs_aliaspasses, 0u);
		Atomic_StoreUInt32 (&rs_brushpasses, 0u);
		stats_ready = true;
	}
	else
		stats_ready = false;

	if (use_tasks)
	{
		task_handle_t before_mark = Task_AllocateAndAssignFunc (R_SetupViewBeforeMark, NULL, 0);
		Task_AddDependency (setup_frame_task, before_mark);
		if (draw_gui_task != INVALID_TASK_HANDLE)
			Task_AddDependency (before_mark, draw_gui_task);

		task_handle_t store_efrags = INVALID_TASK_HANDLE;
		task_handle_t cull_surfaces = INVALID_TASK_HANDLE;
		task_handle_t chain_surfaces = INVALID_TASK_HANDLE;
		R_MarkSurfaces (use_tasks, before_mark, &store_efrags, &cull_surfaces, &chain_surfaces);

		task_handle_t update_warp_textures = Task_AllocateAndAssignFunc ((task_func_t)R_UpdateWarpTextures, NULL, 0);
		Task_AddDependency (cull_surfaces, update_warp_textures);
		Task_AddDependency (begin_rendering_task, update_warp_textures);
		Task_AddDependency (update_warp_textures, draw_done_task);

		task_handle_t draw_world_task = Task_AllocateAndAssignIndexedFunc (R_DrawWorldTask, NUM_WORLD_CBX, &use_tasks, sizeof (use_tasks));
		if (indirect)
			Task_AddDependency (before_mark, draw_world_task);
		else
			Task_AddDependency (chain_surfaces, draw_world_task);
		Task_AddDependency (begin_rendering_task, draw_world_task);
		Task_AddDependency (draw_world_task, draw_done_task);

		task_handle_t sort_transparents = Task_AllocateAndAssignFunc (R_SortAlphaEntitiesTask, NULL, 0);
		Task_AddDependency (store_efrags, sort_transparents);

		task_handle_t draw_sky_task = Task_AllocateAndAssignFunc (R_DrawSkyTask, NULL, 0);
		Task_AddDependency (store_efrags, draw_sky_task);
		Task_AddDependency (chain_surfaces, draw_sky_task);
		Task_AddDependency (begin_rendering_task, draw_sky_task);
		Task_AddDependency (draw_sky_task, draw_done_task);

		task_handle_t draw_water_task = Task_AllocateAndAssignFunc (R_DrawWaterTask, NULL, 0);
		Task_AddDependency (chain_surfaces, draw_water_task);
		Task_AddDependency (begin_rendering_task, draw_water_task);
		Task_AddDependency (draw_water_task, draw_done_task);

		task_handle_t draw_view_model_task = Task_AllocateAndAssignFunc (R_DrawViewModelTask, NULL, 0);
		Task_AddDependency (before_mark, draw_view_model_task);
		Task_AddDependency (begin_rendering_task, draw_view_model_task);
		Task_AddDependency (draw_view_model_task, draw_done_task);

		Atomic_StoreUInt32 (&next_visedict, 0u);
		task_handle_t draw_entities_task = Task_AllocateAndAssignIndexedFunc (R_DrawEntitiesTask, NUM_ENTITIES_CBX, &use_tasks, sizeof (use_tasks));
		Task_AddDependency (store_efrags, draw_entities_task);
		Task_AddDependency (begin_rendering_task, draw_entities_task);

		task_handle_t draw_alpha_entities_task = Task_AllocateAndAssignIndexedFunc (R_DrawAlphaEntitiesTask, 2, &use_tasks, sizeof (use_tasks));
		Task_AddDependency (sort_transparents, draw_alpha_entities_task);
		Task_AddDependency (begin_rendering_task, draw_alpha_entities_task);

#ifdef PSET_SCRIPT
		// dlights queued by last frame's deferred effect spawns; must run before
		// anything reads cl_dlights and before layout refills the queues
		task_handle_t flush_dlights_task = Task_AllocateAndAssignFunc (PScript_FlushDlightsTask, NULL, 0);
		Task_AddDependency (flush_dlights_task, draw_view_model_task);
		Task_AddDependency (flush_dlights_task, draw_entities_task);
		Task_AddDependency (flush_dlights_task, draw_alpha_entities_task);

		task_handle_t update_particles_setup_task = Task_AllocateAndAssignFunc (PScript_UpdateParticlesSetupTask, NULL, 0);
		Task_AddDependency (before_mark, update_particles_setup_task);

		task_handle_t update_particles_task = Task_AllocateAndAssignIndexedFunc (PScript_UpdateParticlesTask, Tasks_NumWorkers (), NULL, 0);
		Task_AddDependency (update_particles_setup_task, update_particles_task);

		// layout is the first task that writes the double buffered vertex/index buffers, it
		// must wait for begin_rendering so the GPU is done reading them from two frames ago
		task_handle_t layout_particles_task = Task_AllocateAndAssignFunc (PScript_LayoutParticlesTask, NULL, 0);
		Task_AddDependency (update_particles_task, layout_particles_task);
		Task_AddDependency (begin_rendering_task, layout_particles_task);
		Task_AddDependency (flush_dlights_task, layout_particles_task);

		task_handle_t emit_particles_task = Task_AllocateAndAssignIndexedFunc (PScript_EmitParticlesTask, Tasks_NumWorkers (), NULL, 0);
		Task_AddDependency (layout_particles_task, emit_particles_task);
#endif

		task_handle_t draw_particles_task = Task_AllocateAndAssignFunc (R_DrawParticlesTask, NULL, 0);
		Task_AddDependency (before_mark, draw_particles_task);
#ifdef PSET_SCRIPT
		Task_AddDependency (emit_particles_task, draw_particles_task);
#endif
		Task_AddDependency (begin_rendering_task, draw_particles_task);
		Task_AddDependency (draw_particles_task, draw_done_task);

		task_handle_t build_tlas_task = Task_AllocateAndAssignFunc (R_BuildTopLevelAccelerationStructure, NULL, 0);
		Task_AddDependency (store_efrags, build_tlas_task);
		Task_AddDependency (begin_rendering_task, build_tlas_task);
		Task_AddDependency (build_tlas_task, draw_done_task);

		task_handle_t update_lightmaps_task = Task_AllocateAndAssignFunc (R_UpdateLightmapsAndIndirect, NULL, 0);
		Task_AddDependency (cull_surfaces, update_lightmaps_task);
		Task_AddDependency (draw_entities_task, update_lightmaps_task);
		Task_AddDependency (draw_alpha_entities_task, update_lightmaps_task);
#ifdef PSET_SCRIPT
		Task_AddDependency (flush_dlights_task, update_lightmaps_task);
#endif
		Task_AddDependency (update_lightmaps_task, draw_done_task);

		if (r_showtris.value)
		{
			if (!indirect)
				Task_AddDependency (chain_surfaces, draw_view_model_task);

			Task_AddDependency (draw_entities_task, draw_view_model_task);		 // not dependent, but mutually exclusive
			Task_AddDependency (draw_alpha_entities_task, draw_view_model_task); // not dependent, but mutually exclusive

#ifdef PSET_SCRIPT
			Task_AddDependency (draw_particles_task, draw_view_model_task); // only scriptable particles are dependent
#endif
		}

		task_handle_t tasks[] = {before_mark,			store_efrags,
								 update_warp_textures,	draw_world_task,
								 sort_transparents,		draw_sky_task,
								 draw_water_task,		draw_view_model_task,
								 draw_entities_task,	draw_alpha_entities_task,
#ifdef PSET_SCRIPT
								 flush_dlights_task,	update_particles_setup_task,
								 update_particles_task, layout_particles_task,
								 emit_particles_task,
#endif
								 draw_particles_task,	build_tlas_task,
								 update_lightmaps_task};
		Tasks_Submit ((sizeof (tasks) / sizeof (task_handle_t)), tasks);
		if (cull_surfaces != chain_surfaces)
		{
			Task_Submit (cull_surfaces);
			Task_Submit (chain_surfaces);
		}
	}
	else
	{
		R_SetupViewBeforeMark (NULL);
		R_MarkSurfaces (use_tasks, INVALID_TASK_HANDLE, NULL, NULL, NULL); // johnfitz -- create texture chains from PVS
		R_UpdateWarpTextures (NULL);
		R_DrawWorldTask (0, NULL);
		R_DrawSkyTask (NULL);
		R_DrawWaterTask (NULL);
		R_DrawEntitiesTask (0, NULL);
		R_SortAlphaEntitiesTask (NULL);
		R_DrawAlphaEntitiesTask (0, NULL);
#ifdef PSET_SCRIPT
		PScript_FlushDlightsTask (NULL); // no-op here (spawns run on the main thread), but keeps the queues drained across mode switches
		PScript_UpdateParticlesSetupTask (NULL);
		for (int pi = 0; pi < q_max (Tasks_NumWorkers (), 1); pi++)
			PScript_UpdateParticlesTask (pi, NULL);
		PScript_LayoutParticlesTask (NULL);
		for (int pi = 0; pi < q_max (Tasks_NumWorkers (), 1); pi++)
			PScript_EmitParticlesTask (pi, NULL);
#endif
		R_DrawParticlesTask (NULL);
		R_DrawViewModelTask (NULL);
		if (r_gpulightmapupdate.value)
		{
			R_BuildTopLevelAccelerationStructure (NULL);
			R_UpdateLightmapsAndIndirect (NULL);
		}
		R_PrintStats ();
	}
}
