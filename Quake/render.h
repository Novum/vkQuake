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

#ifndef _QUAKE_RENDER_H
#define _QUAKE_RENDER_H

#include "tasks.h"

// refresh.h -- public interface to refresh functions

#define MAXCLIPPLANES 11

#define TOP_RANGE	 16 // soldier uniform colors
#define BOTTOM_RANGE 96

//=============================================================================

typedef struct efrag_s
{
	struct efrag_s	*leafnext;
	struct entity_s *entity;
} efrag_t;

typedef struct lightcache_s
{
	int	   surfidx; // < 0: black surface; == 0: no cache; > 0: 1+index of surface
	vec3_t pos;
	short  ds;
	short  dt;
} lightcache_t;

// Separate allocation for RT BLAS state (hot/cold split for cache efficiency)
typedef struct entity_blas_s
{
	VkAccelerationStructureKHR blas;
	VkBuffer				   buffer;
	struct glheapallocation_s *allocation;
	VkDeviceAddress			   address;
	VkDeviceSize			   build_scratch_size;
	VkDeviceSize			   update_scratch_size;
	struct qmodel_s			  *model;
	qboolean				   needs_initial_build;
} entity_blas_t;

// Entity interpolation state. Written only by the parse layer (view.c for the
// view weapon), read by the renderer. Change times are server (message) times.
typedef struct entlerp_s
{
	qboolean movestep;			// this is a MOVETYPE_STEP entity, enable movement lerp
	int		 prev_frame;		// frame before the current e->frame; equal to e->frame when the transition must not be lerped
	double	 frame_change_time; // server time the current frame took effect; 0 = show current frame without lerping
	double	 frame_duration;	// duration of the current frame transition, frozen when the change was recorded
	double	 frame_finish_time; // latest server hint (U_LERPFINISH) for the next frame change; becomes the duration of the next transition
	int		 snap_frames;		// pending frame changes to show without lerping (muzzleflash)
	double	 snap_msgtime;		// server time of the update that armed snap_frames, to arm once per update
	vec3_t	 prev_origin;		// origin/angles before msg_origins[0]/msg_angles[0]; shifted only when they change
	vec3_t	 prev_angles;
	double	 move_change_time; // server time msg_origins[0]/msg_angles[0] took effect; 0 = don't lerp movement
	double	 move_duration;	   // duration of the current movement transition, frozen when the change was recorded
} entlerp_t;

typedef struct entity_s
{
	qboolean forcelink; // no previous update to lerp from, snap to the new state

	int update_type;

	entity_state_t baseline; // to fill in defaults in updates
	entity_state_t netstate; // the latest network state

	double			 msgtime;		 // time of last update
	vec3_t			 msg_origins[2]; // last two updates (0 is newest)
	vec3_t			 origin;
	vec3_t			 msg_angles[2]; // last two updates (0 is newest)
	vec3_t			 angles;
	struct qmodel_s *model; // NULL = no model
	struct efrag_s	*efrag; // linked list of efrags
	int				 frame;
	float			 syncbase; // for client-side animations
	byte			*colormap;
	int				 effects;  // light, particles, etc
	int				 skinnum;  // for Alias models
	int				 visframe; // last frame this entity was
							   //  found in an active leaf

	int dlightframe; // dynamic lighting
	int dlightbits;

	// FIXME: could turn these into a union
	struct mnode_s *topnode; // for bmodels, first world node
							 //  that splits bmodel, or NULL if
							 //  not split

	byte	  eflags; // spike -- mostly a mirror of netstate, but handles tag inheritance (eww!)
	byte	  alpha;  // johnfitz -- alpha
	entlerp_t lerp;

#ifdef PSET_SCRIPT
	struct trailstate_s *trailstate; // spike -- managed by the particle system, so we don't loose our position and spawn the wrong number of particles, and we
									 // can track beams etc
	struct trailstate_s *emitstate;	 // spike -- for effects which are not so static.
#endif
	float  traildelay; // time left until next particle trail update
	vec3_t trailorg;   // previous particle trail point

	lightcache_t lightcache; // alias light trace cache

	int	   contentscache;
	vec3_t contentscache_origin;

	// Per-entity BLAS for animated models
	struct entity_blas_s *blas_data; // NULL when no BLAS allocated
} entity_t;

// !!! if this is changed, it must be changed in asm_draw.h too !!!
typedef struct
{
	vrect_t vrect;							   // subwindow in video for refresh
											   // FIXME: not need vrect next field here?
	vrect_t aliasvrect;						   // scaled Alias version
	int		vrectright, vrectbottom;		   // right & bottom screen coords
	int		aliasvrectright, aliasvrectbottom; // scaled Alias versions
	float	vrectrightedge;					   // rightmost right edge we care about,
											   //  for use in edge list
	float	fvrectx, fvrecty;				   // for floating-point compares
	float	fvrectx_adj, fvrecty_adj;		   // left and top edges, for clamping
	int		vrect_x_adj_shift20;			   // (vrect.x + 0.5 - epsilon) << 20
	int		vrectright_adj_shift20;			   // (vrectright + 0.5 - epsilon) << 20
	float	fvrectright_adj, fvrectbottom_adj;
	// right and bottom edges, for clamping
	float	fvrectright;		   // rightmost edge, for Alias clamping
	float	fvrectbottom;		   // bottommost edge, for Alias clamping
	float	horizontalFieldOfView; // at Z = 1.0, this many X is visible
								   // 2.0 = 90 degrees
	float	xOrigin;			   // should probably allways be 0.5
	float	yOrigin;			   // between be around 0.3 to 0.5

	vec3_t vieworg;
	vec3_t viewangles;

	float basefov;
	float fov_x, fov_y;

	int ambientlight;
} refdef_t;

typedef struct
{
	VkDeviceAddress input_address;
	VkDeviceAddress output_address;
	uint32_t		pose1_offset;
	uint32_t		pose2_offset;
	uint32_t		output_offset;
	uint32_t		num_verts;
	float			blend_factor;
	uint32_t		flags;
} mesh_interpolate_push_constants_t;

typedef struct
{
	VkDeviceAddress input_address;
	VkDeviceAddress joints_address;
	VkDeviceAddress output_address;
	uint32_t		joints_offset0;
	uint32_t		joints_offset1;
	uint32_t		output_offset;
	uint32_t		num_verts;
	float			blend_factor;
} skinning_push_constants_t;

//
// refresh
//
extern int reinit_surfcache;

extern refdef_t r_refdef;
extern vec3_t	r_origin, vpn, vright, vup;

void R_Init (void);
void R_InitTextures (void);
void R_InitEfrags (void);
void R_RenderView (
	qboolean use_tasks, task_handle_t begin_rendering_task, task_handle_t setup_frame_task, task_handle_t draw_done_task); // must set r_refdef first
void R_ViewChanged (vrect_t *pvrect, int lineadj, float aspect);
// called whenever r_refdef or vid change
// void R_InitSky (struct texture_s *mt);	// called at level load

void R_CheckEfrags (void); // johnfitz
void R_AddEfrags (entity_t *ent);

void R_NewMap (void);

void R_ParseParticleEffect (void);
void R_RunParticleEffect (vec3_t org, vec3_t dir, int color, int count);
void R_RocketTrail (vec3_t start, vec3_t end, int type);
void R_EntityParticles (entity_t *ent);
void R_BlobExplosion (vec3_t org);
void R_ParticleExplosion (vec3_t org);
void R_ParticleExplosion2 (vec3_t org, int colorStart, int colorLength);
void R_LavaSplash (vec3_t org);
void R_TeleportSplash (vec3_t org);

void R_PushDlights (void);

//
// surface cache related
//
extern int reinit_surfcache; // if 1, surface cache is currently empty and

int	 D_SurfaceCacheForRes (int width, int height);
void D_DeleteSurfaceCache (void);
void D_InitCaches (void *buffer, int size);
void R_SetVrect (vrect_t *pvrect, vrect_t *pvrectin, int lineadj);

#endif /* _QUAKE_RENDER_H */
