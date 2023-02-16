/*
Copyright (C) 1996-1997 Id Software, Inc.
Copyright (C) 2016      Spike

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

/*
The aim of this particle system is to have as much as possible configurable.
Some parts still fail here, and are marked FIXME
Effects are flushed on new maps.
The engine has a few builtins.
*/

#include "quakedef.h"

cvar_t r_fteparticles = {"r_fteparticles", "1", CVAR_ARCHIVE};

#ifdef PSET_SCRIPT
#define USE_DECALS
#define Con_Printf Con_SafePrintf

#define frandom()  (rand () * (1.0f / (float)RAND_MAX))
#define crandom()  (rand () * (2.0f / (float)RAND_MAX) - 1.0f)
#define hrandom()  (rand () * (1.0f / (float)RAND_MAX) - 0.5f)
#define particle_s fparticle_s
#define particle_t fparticle_t
typedef vec_t vec2_t[2];
#define FloatInterpolate(a, bness, b, c) ((c) = (a) + (b - a) * bness)
#define Vector2Copy(a, b) \
	do                    \
	{                     \
		(b)[0] = (a)[0];  \
		(b)[1] = (a)[1];  \
	} while (0)
#define Vector2Set(r, x, y) \
	do                      \
	{                       \
		(r)[0] = x;         \
		(r)[1] = y;         \
	} while (0)
#define VectorClear(a) ((a)[0] = (a)[1] = (a)[2] = 0)
#define VectorInterpolate(a, bness, b, c) \
	FloatInterpolate ((a)[0], bness, (b)[0], (c)[0]), FloatInterpolate ((a)[1], bness, (b)[1], (c)[1]), FloatInterpolate ((a)[2], bness, (b)[2], (c)[2])
#define VectorSet(r, x, y, z) \
	do                        \
	{                         \
		(r)[0] = x;           \
		(r)[1] = y;           \
		(r)[2] = z;           \
	} while (0)
#define Vector4Clear(a)				 ((a)[0] = (a)[1] = (a)[2] = (a)[3] = 0)
#define Vector4Scale(in, scale, out) ((out)[0] = (in)[0] * scale, (out)[1] = (in)[1] * scale, (out)[2] = (in)[2] * scale, (out)[3] = (in)[3] * scale)
#define FloatToColor(a, b)                              \
	do                                                  \
	{                                                   \
		(b) = (byte)(CLAMP (0.0f, (a), 1.0f) * 255.0f); \
	} while (0)
#define Vector3ToColor(a, b)                                  \
	do                                                        \
	{                                                         \
		(b)[0] = (byte)(CLAMP (0.0f, (a)[0], 1.0f) * 255.0f); \
		(b)[1] = (byte)(CLAMP (0.0f, (a)[1], 1.0f) * 255.0f); \
		(b)[2] = (byte)(CLAMP (0.0f, (a)[2], 1.0f) * 255.0f); \
	} while (0)
#define Vector4ToColor(a, b)                                  \
	do                                                        \
	{                                                         \
		(b)[0] = (byte)(CLAMP (0.0f, (a)[0], 1.0f) * 255.0f); \
		(b)[1] = (byte)(CLAMP (0.0f, (a)[1], 1.0f) * 255.0f); \
		(b)[2] = (byte)(CLAMP (0.0f, (a)[2], 1.0f) * 255.0f); \
		(b)[3] = (byte)(CLAMP (0.0f, (a)[3], 1.0f) * 255.0f); \
	} while (0)
vec_t VectorNormalize2 (const vec3_t v, vec3_t out)
{
	float length, ilength;

	length = v[0] * v[0] + v[1] * v[1] + v[2] * v[2];

	if (length)
	{
		length = sqrt (length); // FIXME
		ilength = 1 / length;
		out[0] = v[0] * ilength;
		out[1] = v[1] * ilength;
		out[2] = v[2] * ilength;
	}
	else
	{
		VectorClear (out);
	}

	return length;
}
void VectorVectors (const vec3_t forward, vec3_t right, vec3_t up)
{
	if (!forward[0] && !forward[1])
	{
		if (forward[2])
			right[1] = -1;
		else
			right[1] = 0;
		right[0] = right[2] = 0;
	}
	else
	{
		right[0] = forward[1];
		right[1] = -forward[0];
		right[2] = 0;
		VectorNormalize (right);
	}
	CrossProduct (right, forward, up);
}
typedef enum
{
	BM_BLEND /*SRC_ALPHA ONE_MINUS_SRC_ALPHA*/,
	BM_BLENDCOLOUR /*SRC_COLOR ONE_MINUS_SRC_COLOR*/,
	BM_ADDA /*SRC_ALPHA ONE*/,
	BM_ADDC /*GL_SRC_COLOR GL_ONE*/,
	BM_SUBTRACT /*SRC_ALPHA ONE_MINUS_SRC_COLOR*/,
	BM_INVMODA /*ZERO ONE_MINUS_SRC_ALPHA*/,
	BM_INVMODC /*ZERO ONE_MINUS_SRC_COLOR*/,
	BM_PREMUL /*ONE ONE_MINUS_SRC_ALPHA*/
} blendmode_t;
typedef struct trailstate_s
{
	struct trailstate_s **key;		// key to check if ts has been overwriten
	struct trailstate_s	 *assoc;	// assoc linked trail
	struct beamseg_s	 *lastbeam; // last beam pointer (flagged with BS_LASTSEG)
	union
	{
		float lastdist;	 // last distance used with particle effect
		float statetime; // time to emit effect again (used by spawntime field)
	} state1;
	union
	{
		float laststop; // last stopping point for particle effect
		float emittime; // used by r_effect emitters
	} state2;
} trailstate_t;
#define CON_WARNING "Warning: "
entity_t *CL_EntityNum (int num);
#define BEF_LINES 1

extern int PClassic_PointFile (int c, vec3_t point);

#define PART_VALID(part) ((part) >= 0 && (part) < numparticletypes)

static int pe_default = P_INVALID;
static int pe_size2 = P_INVALID;
static int pe_size3 = P_INVALID;
static int pe_defaulttrail = P_INVALID;

#define SINTABLE_ENTRIES 128
static float psintable[SINTABLE_ENTRIES];
static float pcostable[SINTABLE_ENTRIES];

int r_trace_line_cache_counter;

int				PScript_RunParticleEffectState (vec3_t org, vec3_t dir, float count, int typenum, trailstate_t **tsk);
int				PScript_ParticleTrail (vec3_t startpos, vec3_t end, int type, float timeinterval, int dlkey, vec3_t axis[3], trailstate_t **tsk);
static qboolean P_LoadParticleSet (char *name, qboolean implicit, qboolean showwarning);
static void		R_Particles_KillAllEffects (void);

static void buildsintable (void)
{
	int i;
	for (i = 0; i < SINTABLE_ENTRIES; i++)
	{
		psintable[i] = sin ((i * M_PI) / (SINTABLE_ENTRIES / 2));
		pcostable[i] = cos ((i * M_PI) / (SINTABLE_ENTRIES / 2));
	}
}
#define sin(x) (psintable[(size_t)(int)((x) * ((SINTABLE_ENTRIES / 2) / M_PI)) % SINTABLE_ENTRIES])
#define cos(x) (pcostable[(size_t)(int)((x) * ((SINTABLE_ENTRIES / 2) / M_PI)) % SINTABLE_ENTRIES])

typedef struct particle_s
{
	struct particle_s *next;
	float			   die;

	// driver-usable fields
	vec3_t org;
	vec4_t rgba;
	float  scale;
	float  s1, t1, s2, t2;

	vec3_t oldorg; // to throttle traces
	vec3_t vel;	   // renderer uses for sparks
	float  angle;
	union
	{
		float		  nextemit;
		trailstate_t *trailstate;
	} state;
	// drivers never touch the following fields
	float rotationspeed;
} particle_t;

typedef struct clippeddecal_s
{
	struct clippeddecal_s *next;
	float				   die;

	int		  entity; //>0 is a lerpentity, <0 is a csqc ent. 0 is world. woot.
	qmodel_t *model;  // just for paranoia

	vec3_t vertex[3];
	vec2_t texcoords[3];
	float  valpha[3];

	vec4_t rgba;
} clippeddecal_t;

#define BS_LASTSEG 0x1 // no draw to next, no delete
#define BS_DEAD	   0x2 // segment is dead
#define BS_NODRAW  0x4 // only used for lerp switching

typedef struct beamseg_s
{
	struct beamseg_s *next; // next in beamseg list

	particle_t *p;
	int			flags; // flags for beamseg
	vec3_t		dir;

	float texture_s;
} beamseg_t;

typedef struct skytris_s
{
	struct skytris_s  *next;
	vec3_t			   org;
	vec3_t			   x;
	vec3_t			   y;
	float			   area;
	double			   nexttime;
	int				   ptype;
	struct msurface_s *face;
} skytris_t;

typedef struct skytriblock_s
{
	struct skytriblock_s *next;
	unsigned int		  count;
	skytris_t			  tris[1024];
} skytriblock_t;

// this is the required render state for each particle
// dynamic per-particle stuff isn't important. only static state.
typedef struct
{
	enum
	{
		PT_NORMAL,
		PT_SPARK,
		PT_SPARKFAN,
		PT_TEXTUREDSPARK,
		PT_BEAM,
		PT_CDECAL,
		PT_UDECAL,
		PT_INVISIBLE
	} type;

	blendmode_t	 blendmode;
	gltexture_t *texture;
	qboolean	 nearest;

	float scalefactor;
	float invscalefactor;
	float stretch;
	float minstretch; // limits the particle's length to a multiple of its width.
	int	  premul;	  // 0: direct rgba. 1: rgb*a,a (blend). 2: rgb*a,0 (add).
} plooks_t;

// these could be deltas or absolutes depending on ramping mode.
typedef struct
{
	vec3_t rgb;
	float  alpha;
	float  scale;
	float  rotation;
} ramp_t;
typedef struct
{
	char  name[MAX_QPATH];
	float vol;
	float atten;
	float delay;
	float pitch;
	float weight;
} partsounds_t;
// TODO: merge in alpha with rgb to gain benefit of vector opts
typedef struct part_type_s
{
	char name[MAX_QPATH];
	char config[MAX_QPATH];
	char texname[MAX_QPATH];

	int			  numsounds;
	partsounds_t *sounds;

	vec3_t rgb; // initial colour
	float  alpha;
	vec3_t rgbchange; // colour delta (per second)
	float  alphachange;
	vec3_t rgbrand; // random rgb colour to start with
	float  alpharand;
	int	   colorindex;			   // get colour from a palette
	int	   colorrand;			   // and add up to this amount
	float  rgbchangetime;		   // colour stops changing at this time
	vec3_t rgbrandsync;			   // like rgbrand, but a single random value instead of separate (can mix)
	float  scale;				   // initial scale
	float  scalerand;			   // with up to this much extra
	float  die, randdie;		   // how long it lasts (plus some rand)
	float  veladd, randomveladd;   // scale the incoming velocity by this much
	float  orgadd, randomorgadd;   // spawn the particle this far along its velocity direction
	float  spawnvel, spawnvelvert; // spawn the particle with a velocity based upon its spawn type (generally so it flies outwards)
	vec3_t orgbias;				   // static 3d world-coord bias
	vec3_t velbias;
	vec3_t orgwrand; // 3d world-coord randomisation without relation to spawn mode
	vec3_t velwrand; // 3d world-coord randomisation without relation to spawn mode
	float  viewspacefrac;
	float  flurry;
	int	   surfflagmatch; // this decal only spawns on these surfaces
	int	   surfflagmask;  // this decal only spawns on these surfaces

	float s1, t1, s2, t2; // texture coords
	float texsstride;	  // addition for s for each random slot.
	int	  randsmax;		  // max times the stride can be added

	plooks_t *slooks; // shared looks, so state switches don't apply between particles so much.
	plooks_t  looks;  //

	float spawntime;   // time limit for trails
	float spawnchance; // if < 0, particles might not spawn so many

	float rotationstartmin, rotationstartrand;
	float rotationmin, rotationrand;

	float scaledelta;
	float countextra;
	float count;
	float countrand;
	float countspacing;	 // for trails.
	float countoverflow; // for badly-designed effects, instead of depending on trail state.
	float rainfrequency; // surface emitter multiplier

	int	  assoc;
	int	  cliptype;
	int	  inwater;
	float clipcount;
	int	  emit;
	float emittime;
	float emitrand;
	float emitstart;

	float areaspread;
	float areaspreadvert;

	float spawnparam1;
	float spawnparam2;
	/*	float spawnparam3; */

	enum
	{
		SM_BOX,		   // box = even spread within the area
		SM_CIRCLE,	   // circle = around edge of a circle
		SM_BALL,	   // ball = filled sphere
		SM_SPIRAL,	   // spiral = spiral trail
		SM_TRACER,	   // tracer = tracer trail
		SM_TELEBOX,	   // telebox = q1-style telebox
		SM_LAVASPLASH, // lavasplash = q1-style lavasplash
		SM_UNICIRCLE,  // unicircle = uniform circle
		SM_FIELD,	   // field = synced field (brightfield, etc)
		SM_DISTBALL,   // uneven distributed ball
		SM_MESHSURFACE // distributed roughly evenly over the surface of the mesh
	} spawnmode;

	float  gravity;
	vec3_t friction;
	float  clipbounce;
	float  stainonimpact;

	vec3_t dl_rgb;
	float  dl_radius[2];
	float  dl_time;
	vec4_t dl_decay;
	float  dl_corona_intensity;
	float  dl_corona_scale;
	vec3_t dl_scales;
	// PT_NODLSHADOW
	int	   dl_cubemapnum;

	enum
	{
		RAMP_NONE,
		RAMP_DELTA,
		RAMP_NEAREST,
		RAMP_LERP
	} rampmode;
	int		rampindexes;
	ramp_t *ramp;

	int					loaded; // 0 if not loaded, 1 if automatically loaded, 2 if user loaded
	particle_t		   *particles;
	clippeddecal_t	   *clippeddecals;
	beamseg_t		   *beams;
	struct part_type_s *nexttorun;

	unsigned int flags;
#define PT_VELOCITY		  0x0001 // has velocity modifiers
#define PT_FRICTION		  0x0002 // has friction modifiers
#define PT_CHANGESCOLOUR  0x0004
#define PT_CITRACER		  0x0008 // Q1-style tracer behavior for colorindex
#define PT_INVFRAMETIME	  0x0010 // apply inverse frametime to count (causes emits to be per frame)
#define PT_AVERAGETRAIL	  0x0020 // average trail points from start to end, useful with t_lightning, etc
#define PT_NOSTATE		  0x0040 // don't use trailstate for this emitter (careful with assoc...)
#define PT_NOSPREADFIRST  0x0080 // don't randomize org/vel for first generated particle
#define PT_NOSPREADLAST	  0x0100 // don't randomize org/vel for last generated particle
#define PT_TROVERWATER	  0x0200 // don't spawn if underwater
#define PT_TRUNDERWATER	  0x0400 // don't spawn if overwater
#define PT_NODLSHADOW	  0x0800 // dlights from this effect don't cast shadows.
#define PT_WORLDSPACERAND 0x1000 // effect has orgwrand or velwrand properties
	unsigned int fluidmask;

	unsigned int state;
#define PS_INRUNLIST 0x1 // particle type is currently in execution list
} part_type_t;

typedef struct pcfg_s
{
	struct pcfg_s *next;
	char		   name[1];
} pcfg_t;
static pcfg_t *loadedconfigs;

#ifndef TYPESONLY

#define crand() (rand () % 32767 / 16383.5f - 1)

#define MAX_BEAMSEGS	(1 << 11) // default max # of beam segments
#define MAX_PARTICLES	(1 << 18) // max # of particles at one time
#define MAX_DECALS		(1 << 18) // max # of decal fragments at one time
#define MAX_TRAILSTATES (1 << 10) // default max # of trailstates

static particle_t *free_particles;
static particle_t *particles; // contains the initial list of alloced particles.
static int		   r_numparticles;
static int		   r_particlerecycle;

static beamseg_t *free_beams;
static beamseg_t *beams;
static int		  r_numbeams;

static clippeddecal_t *free_decals;
static clippeddecal_t *decals;
static int			   r_numdecals;
static int			   r_decalrecycle;

static trailstate_t *trailstates;
static int			 ts_cycle; // current cyclic index of trailstates
static int			 r_numtrailstates;

static qboolean r_plooksdirty; // a particle effect was changed, reevaluate shared looks.

static void FinishParticleType (part_type_t *ptype);

static void	  R_ParticleDesc_Callback (struct cvar_s *var);
static cvar_t r_bouncysparks = {"r_bouncysparks", "1"};
static cvar_t r_part_rain = {"r_part_rain", "1"};
static cvar_t r_decal_noperpendicular = {"r_decal_noperpendicular", "1"};
cvar_t		  r_particledesc = {"r_particledesc", "classic"};
static cvar_t r_part_rain_quantity = {"r_part_rain_quantity", "1"};
static cvar_t r_particle_tracelimit = {"r_particle_tracelimit", "16777216"};
static cvar_t r_part_sparks = {"r_part_sparks", "1"};
static cvar_t r_part_sparks_trifan = {"r_part_sparks_trifan", "1"};
static cvar_t r_part_sparks_textured = {"r_part_sparks_textured", "1"};
static cvar_t r_part_beams = {"r_part_beams", "1"};
static cvar_t r_part_contentswitch = {"r_part_contentswitch", "1"};
static cvar_t r_part_density = {"r_part_density", "1"};
static cvar_t r_part_maxparticles = {"r_part_maxparticles", "65536"};
static cvar_t r_part_maxdecals = {"r_part_maxdecals", "8192"};
static cvar_t r_lightflicker = {"r_lightflicker", "1"};
extern cvar_t r_showtris;
extern cvar_t r_particles;

static float particletime;

typedef struct
{
	int firstidx;
	int firstvert;
	int numidx;
	int numvert;

	gltexture_t *texture;
	blendmode_t	 blendmode;
	int			 beflags;
} scenetris_t;

#define MAX_INDICES			 0xffff
#define INITIAL_NUM_VERTICES 100000
#define INITIAL_NUM_INDICES	 150000

static scenetris_t	  *cl_stris;
static unsigned int	   cl_numstris;
static unsigned int	   cl_maxstris;
static basicvertex_t  *cl_strisvert[2];
static basicvertex_t  *cl_curstrisvert;
static unsigned int	   cl_numstrisvert;
static unsigned int	   cl_maxstrisvert[2];
static unsigned short *cl_strisidx[2];
static unsigned short *cl_curstrisidx;
static unsigned int	   cl_numstrisidx;
static unsigned int	   cl_maxstrisidx[2];

/*
Q1BSP_RecursiveHullTrace
Optimised version of vanilla's SV_RecursiveHullCheck that avoids the excessive pointcontents calls by using the traceline itself to check for contents.
call Q1BSP_RecursiveHullCheck for a drop-in replacement of SV_RecursiveHullCheck, if desired.
*/
enum
{
	rht_solid,
	rht_empty,
	rht_impact
};
struct rhtctx_s
{
	vec3_t		 start, end;
	mclipnode_t *clipnodes;
	mplane_t	*planes;
};
static int Q1BSP_RecursiveHullTrace (struct rhtctx_s *ctx, int num, float p1f, float p2f, vec3_t p1, vec3_t p2, trace_t *trace)
{
	mclipnode_t *node;
	mplane_t	*plane;
	float		 t1, t2;
	vec3_t		 mid;
	int			 side;
	float		 midf;
	int			 rht;

reenter:

	if (num < 0)
	{
		/*hit a leaf*/
		if (num == CONTENTS_SOLID)
		{
			if (trace->allsolid)
				trace->startsolid = true;
			return rht_solid;
		}
		else
		{
			trace->allsolid = false;
			if (num == CONTENTS_EMPTY)
				trace->inopen = true;
			else
				trace->inwater = true;
			return rht_empty;
		}
	}

	/*its a node*/

	/*get the node info*/
	node = ctx->clipnodes + num;
	plane = ctx->planes + node->planenum;

	if (plane->type < 3)
	{
		t1 = p1[plane->type] - plane->dist;
		t2 = p2[plane->type] - plane->dist;
	}
	else
	{
		t1 = DotProduct (plane->normal, p1) - plane->dist;
		t2 = DotProduct (plane->normal, p2) - plane->dist;
	}

	/*if its completely on one side, resume on that side*/
	if (t1 >= 0 && t2 >= 0)
	{
		num = node->children[0];
		goto reenter;
	}
	if (t1 < 0 && t2 < 0)
	{
		num = node->children[1];
		goto reenter;
	}

	if (plane->type < 3)
	{
		t1 = ctx->start[plane->type] - plane->dist;
		t2 = ctx->end[plane->type] - plane->dist;
	}
	else
	{
		t1 = DotProduct (plane->normal, ctx->start) - plane->dist;
		t2 = DotProduct (plane->normal, ctx->end) - plane->dist;
	}

	side = t1 < 0;

	midf = t1 / (t1 - t2);
	if (midf < p1f)
		midf = p1f;
	if (midf > p2f)
		midf = p2f;
	VectorInterpolate (ctx->start, midf, ctx->end, mid);

	rht = Q1BSP_RecursiveHullTrace (ctx, node->children[side], p1f, midf, p1, mid, trace);
	if (rht != rht_empty && !trace->allsolid)
		return rht;
	rht = Q1BSP_RecursiveHullTrace (ctx, node->children[side ^ 1], midf, p2f, mid, p2, trace);
	if (rht != rht_solid)
		return rht;

	if (side)
	{
		/*we impacted the back of the node, so flip the plane*/
		trace->plane.dist = -plane->dist;
		VectorScale (plane->normal, -1, trace->plane.normal);
		midf = (t1 + DIST_EPSILON) / (t1 - t2);
	}
	else
	{
		/*we impacted the front of the node*/
		trace->plane.dist = plane->dist;
		VectorCopy (plane->normal, trace->plane.normal);
		midf = (t1 - DIST_EPSILON) / (t1 - t2);
	}

	t1 = DotProduct (trace->plane.normal, ctx->start) - trace->plane.dist;
	t2 = DotProduct (trace->plane.normal, ctx->end) - trace->plane.dist;
	midf = (t1 - DIST_EPSILON) / (t1 - t2);
	if (midf < 0)
		midf = 0;
	if (midf > 1)
		midf = 1;
	trace->fraction = midf;
	VectorCopy (mid, trace->endpos);
	VectorInterpolate (ctx->start, midf, ctx->end, trace->endpos);

	return rht_impact;
}
static qboolean Q1BSP_RecursiveHullCheck (hull_t *hull, int num, float p1f, float p2f, vec3_t p1, vec3_t p2, trace_t *trace)
{
	// this function is basicall meant as a drop-in replacement for fte's SV_RecursiveHullCheck. p1f and p2f must be 0+1 respectively, num must be
	// hull->firstclipnode
	struct rhtctx_s ctx;
	VectorCopy (p1, ctx.start);
	VectorCopy (p2, ctx.end);
	ctx.clipnodes = hull->clipnodes;
	ctx.planes = hull->planes;
	return Q1BSP_RecursiveHullTrace (&ctx, num, p1f, p2f, p1, p2, trace) != rht_impact;
}

float CL_TraceLine (vec3_t start, vec3_t end, vec3_t impact, vec3_t normal, int *entnum)
{ // FIXME: not sure what to do about startsolid.
	int		  i;
	trace_t	  trace;
	float	  frac = 1;
	entity_t *ent;
	vec3_t	  relstart, relend;
	VectorCopy (end, impact);
	VectorSet (normal, 0, 0, 1);

	static int num_trace_line_ents;
	static int trace_line_ents[MAX_EDICTS];
	static int cache_valid_count = -1;
	if (cache_valid_count != r_trace_line_cache_counter)
	{
		num_trace_line_ents = 0;
		for (i = 0; i < cl.num_entities; i++)
		{
			ent = &cl.entities[i];
			if (!ent->model || ent->model->needload || ent->model->type != mod_brush)
				continue;
			trace_line_ents[num_trace_line_ents++] = i;
		}
		cache_valid_count = r_trace_line_cache_counter;
	}

	if (entnum)
		*entnum = 0;
	for (i = 0; i < num_trace_line_ents; i++)
	{
		ent = &cl.entities[trace_line_ents[i]];

		// FIXME: deal with rotations
		VectorSubtract (start, ent->origin, relstart);
		VectorSubtract (end, ent->origin, relend);

		memset (&trace, 0, sizeof (trace));
		trace.fraction = 1;
		Q1BSP_RecursiveHullCheck (&ent->model->hulls[0], ent->model->hulls[0].firstclipnode, 0, 1, relstart, relend, &trace);

		if (frac > trace.fraction)
		{
			frac = trace.fraction;

			// FIXME: deal with rotations.
			VectorAdd (trace.endpos, ent->origin, impact);
			VectorCopy (trace.plane.normal, normal);

			if (entnum)
				*entnum = i;
			if (frac <= 0)
				break;
		}
	}
	return frac;
}

// these are not the actual values, but they'll do
#define FTECONTENTS_EMPTY	   0
#define FTECONTENTS_SOLID	   1
#define FTECONTENTS_WATER	   2
#define FTECONTENTS_SLIME	   4
#define FTECONTENTS_LAVA	   8
#define FTECONTENTS_SKY		   16
#define FTECONTENTS_FLUID	   (FTECONTENTS_WATER | FTECONTENTS_SLIME | FTECONTENTS_LAVA | FTECONTENTS_SKY)
#define FTECONTENTS_PLAYERCLIP 0

int					SV_HullPointContents (hull_t *hull, int num, vec3_t p);
static unsigned int CL_PointContentsMask (vec3_t p)
{
	static const unsigned int cont_qtof[] = {
		0, // invalid
		FTECONTENTS_EMPTY,
		FTECONTENTS_SOLID,
		FTECONTENTS_WATER,
		FTECONTENTS_SLIME,
		FTECONTENTS_LAVA,
		FTECONTENTS_SKY};

	unsigned int cont;

	cont = -SV_HullPointContents (&cl.worldmodel->hulls[0], 0, p);
	if (cont < sizeof (cont_qtof) / sizeof (cont_qtof[0]))
		return cont_qtof[cont];
	else
		return cont_qtof[-(CONTENTS_WATER)]; // assume water
}

static int			numparticletypes;
static part_type_t *part_type;
static part_type_t *part_run_list;

static struct
{
	char *oldn;
	char *newn;
} legacynames[] = {
	{"t_rocket", "TR_ROCKET"},
	{"t_grenade", "TR_GRENADE"},
	{"t_gib", "TR_BLOOD"},

	{"te_plasma", "TE_TEI_PLASMAHIT"},
	{"te_smoke", "TE_TEI_SMOKE"},

	{NULL}};

static struct partalias_s
{
	struct partalias_s *next;
	const char		   *from;
	const char		   *to;
} *partaliaslist;
typedef struct associatedeffect_s
{
	struct associatedeffect_s *next;
	char					   mname[MAX_QPATH];
	char					   pname[MAX_QPATH];
	unsigned int			   flags;
	enum
	{
		AE_TRAIL,
		AE_EMIT,
	} type;
} associatedeffect_t;
static associatedeffect_t *associatedeffect;
static void				   PScript_AssociateEffect_f (void)
{
	const char		   *modelname = Cmd_Argv (1);
	const char		   *effectname = Cmd_Argv (2);
	unsigned int		flags = 0;
	int					type;
	associatedeffect_t *ae;
	int					i;

	if (!strcmp (Cmd_Argv (0), "r_trail"))
		type = AE_TRAIL;
	else
	{
		type = AE_EMIT;
		for (i = 3; i < Cmd_Argc (); i++)
		{
			const char *fn = Cmd_Argv (i);
			if (!strcmp (fn, "replace") || !strcmp (fn, "1"))
				flags |= MOD_EMITREPLACE;
			else if (!strcmp (fn, "forwards") || !strcmp (fn, "forward"))
				flags |= MOD_EMITFORWARDS;
			else if (!strcmp (fn, "0"))
				; // 1 or 0 are legacy, meaning replace or not
			else
				Con_DPrintf ("%s %s: unknown flag %s\n", Cmd_Argv (0), modelname, fn);
		}
	}

	if (strstr (modelname, "player") || strstr (modelname, "eyes") || strstr (modelname, "flag") || strstr (modelname, "tf_stan") ||
		strstr (modelname, ".bsp") || strstr (modelname, "turr"))
	{
		// there is a very real possibility of attaching 'large' effects to models so that they become more visible (eg: a stream of particles passing through
		// walls showing you the entity that they're eminating from)
		Con_Printf ("Sorry: Not allowed to attach effects to model \"%s\"\n", modelname);
		return;
	}

	if (strlen (modelname) >= MAX_QPATH || strlen (effectname) >= MAX_QPATH)
		return;

	/*replace the old one if it exists*/
	for (ae = associatedeffect; ae; ae = ae->next)
	{
		if (!strcmp (ae->mname, modelname))
			if ((ae->type == AE_TRAIL) == (type == AE_TRAIL))
				break;
	}
	if (!ae)
	{
		ae = Mem_Alloc (sizeof (*ae));
		strcpy (ae->mname, modelname);
		ae->next = associatedeffect;
		associatedeffect = ae;
	}
	strcpy (ae->pname, effectname);
	ae->type = type;
	ae->flags = flags;

	r_plooksdirty = true;
}
static void P_PartRedirect_f (void)
{
	struct partalias_s **link, *l;
	const char			*from = Cmd_Argv (1);
	const char			*to = Cmd_Argv (2);

	// user wants to list all
	if (!*from)
	{
		for (l = partaliaslist; l; l = l->next)
		{
			Con_Printf ("%s -> %s\n", l->from, l->to);
		}
		return;
	}

	// unlink the current value
	for (link = &partaliaslist; (l = *link); link = &(*link)->next)
	{
		if (!q_strcasecmp (l->from, from))
		{
			// they didn't specify a to, so just print out this one effect without removing it.
			if (Cmd_Argc () == 2)
			{
				Con_Printf ("particle %s is currently remapped to %s\n", l->from, l->to);
				return;
			}
			*link = l->next;
			Mem_Free (l);
			break;
		}
	}

	// create a new entry.
	if (*to && q_strcasecmp (from, to))
	{
		l = Mem_Alloc (sizeof (*l) + strlen (from) + strlen (to) + 2);
		l->from = (char *)(l + 1);
		strcpy ((char *)l->from, from);
		l->to = l->from + strlen (l->from) + 1;
		strcpy ((char *)l->to, to);
		l->next = partaliaslist;
		partaliaslist = l;
	}

	r_plooksdirty = true;
}
void PScript_UpdateModelEffects (qmodel_t *mod)
{
	associatedeffect_t *ae;
	mod->emiteffect = P_INVALID;
	mod->traileffect = P_INVALID;
	for (ae = associatedeffect; ae; ae = ae->next)
	{
		if (!strcmp (ae->mname, mod->name))
		{
			switch (ae->type)
			{
			case AE_TRAIL:
				mod->traileffect = PScript_FindParticleType (ae->pname);
				break;
			case AE_EMIT:
				mod->emiteffect = PScript_FindParticleType (ae->pname);
				mod->flags &= ~(MOD_EMITREPLACE | MOD_EMITFORWARDS);
				mod->flags |= ae->flags;
				break;
			}
		}
	}
}

static part_type_t *P_GetParticleType (const char *config, const char *name)
{
	int			 i;
	part_type_t *ptype;
	part_type_t *oldlist = part_type;
	char		 cfgbuf[MAX_QPATH];
	char		*dot = strchr (name, '.');
	if (dot && (dot - name) < MAX_QPATH - 1)
	{
		config = cfgbuf;
		memcpy (cfgbuf, name, dot - name);
		cfgbuf[dot - name] = 0;
		name = dot + 1;
	}

	for (i = 0; legacynames[i].oldn; i++)
	{
		if (!strcmp (name, legacynames[i].oldn))
		{
			name = legacynames[i].newn;
			break;
		}
	}
	for (i = 0; i < numparticletypes; i++)
	{
		ptype = &part_type[i];
		if (!q_strcasecmp (ptype->name, name))
			if (!q_strcasecmp (ptype->config, config)) // must be an exact match.
				return ptype;
	}
	part_type = Mem_Realloc (part_type, sizeof (part_type_t) * (numparticletypes + 1));
	ptype = &part_type[numparticletypes++];
	memset (ptype, 0, sizeof (*ptype));
	q_strlcpy (ptype->name, name, sizeof (ptype->name));
	q_strlcpy (ptype->config, config, sizeof (ptype->config));
	ptype->assoc = P_INVALID;
	ptype->inwater = P_INVALID;
	ptype->cliptype = P_INVALID;
	ptype->emit = P_INVALID;

	if (oldlist)
	{
		if (part_run_list)
			part_run_list = (part_type_t *)((char *)part_run_list - (char *)oldlist + (char *)part_type);

		for (i = 0; i < numparticletypes; i++)
			if (part_type[i].nexttorun)
				part_type[i].nexttorun = (part_type_t *)((char *)part_type[i].nexttorun - (char *)oldlist + (char *)part_type);
	}

	ptype->loaded = 0;
	ptype->ramp = NULL;
	ptype->particles = NULL;
	ptype->beams = NULL;

	r_plooksdirty = true;
	return ptype;
}

// unconditionally allocates a particle object. this allows out-of-order allocations.
static int P_AllocateParticleType (const char *config, const char *name) // guarentees that the particle type exists, returning it's index.
{
	part_type_t *pt = P_GetParticleType (config, name);
	return pt - part_type;
}

static void PScript_RetintEffect (part_type_t *to, part_type_t *from, const char *colourcodes)
{
	char name[sizeof (to->name)];
	char config[sizeof (to->config)];

	q_strlcpy (name, to->name, sizeof (to->name));
	q_strlcpy (config, to->config, sizeof (to->config));

	//'to' was already purged, so we don't need to care about that.
	memcpy (to, from, sizeof (*to));

	q_strlcpy (to->name, name, sizeof (to->name));
	q_strlcpy (to->config, config, sizeof (to->config));

	// make sure 'to' has its own copy of any lists, so that we don't have issues when freeing this memory again.
	if (to->sounds)
	{
		to->sounds = Mem_Alloc (to->numsounds * sizeof (*to->sounds));
		memcpy (to->sounds, from->sounds, to->numsounds * sizeof (*to->sounds));
	}
	if (to->ramp)
	{
		to->ramp = Mem_Alloc (to->rampindexes * sizeof (*to->ramp));
		memcpy (to->ramp, from->ramp, to->rampindexes * sizeof (*to->ramp));
	}

	//'from' might still have some links so we need to clear those out.
	to->nexttorun = NULL;
	to->particles = NULL;
	to->clippeddecals = NULL;
	to->beams = NULL;
	to->slooks = &to->looks;
	r_plooksdirty = true;

	to->colorindex = strtoul (colourcodes, (char **)&colourcodes, 10);
	if (*colourcodes == '_')
		colourcodes++;
	to->colorrand = strtoul (colourcodes, (char **)&colourcodes, 10);
}

// public interface. get without creating.
int PScript_FindParticleType (const char *fullname)
{
	int			 i;
	part_type_t *ptype = NULL;
	char		 cfg[MAX_QPATH];
	char		*dot;
	const char	*name = fullname;

	// check particle aliases, mostly for tex_sky1 -> weather.te_rain for example, or whatever
	struct partalias_s *l;
	int					recurselimit = 5;
	for (l = partaliaslist; l;)
	{
		if (!q_strcasecmp (l->from, name))
		{
			name = l->to;

			if (recurselimit-- > 0)
				l = partaliaslist;
			else
				return P_INVALID;
		}
		else
			l = l->next;
	}

	dot = strchr (name, '.');
	if (dot && (dot - name) < MAX_QPATH - 1)
	{
		memcpy (cfg, name, dot - name);
		cfg[dot - name] = 0;
		name = dot + 1;
	}
	else
		*cfg = 0;

	for (i = 0; legacynames[i].oldn; i++)
	{
		if (!strcmp (name, legacynames[i].oldn))
		{
			name = legacynames[i].newn;
			break;
		}
	}

	if (*cfg)
	{ // favour the namespace if one is specified
		for (i = 0; i < numparticletypes; i++)
		{
			if (!q_strcasecmp (part_type[i].name, name))
			{
				if (!q_strcasecmp (part_type[i].config, cfg))
				{
					ptype = &part_type[i];
					break;
				}
			}
		}
	}
	else
	{
		// but be prepared to load it from any namespace if its not got a namespace specified.
		for (i = 0; i < numparticletypes; i++)
		{
			if (!q_strcasecmp (part_type[i].name, name))
			{
				ptype = &part_type[i];
				if (ptype->loaded) //(mostly) ignore ones that are not currently loaded
					break;
			}
		}
	}
	if (!ptype || !ptype->loaded)
	{
		if (!q_strncasecmp (name, "te_explosion2_", 14))
		{
			int from = PScript_FindParticleType (va ("%s.te_explosion2", cfg));
			if (from != P_INVALID)
			{
				int to = P_AllocateParticleType (cfg, name);
				PScript_RetintEffect (&part_type[to], &part_type[from], name + 14);
				return to;
			}
		}
		if (*cfg)
			if (P_LoadParticleSet (cfg, true, true))
				return PScript_FindParticleType (fullname);

		return P_INVALID;
	}
	return i;
}

static int CheckAssosiation (const char *config, const char *name, int from)
{
	int to, orig;

	orig = to = P_AllocateParticleType (config, name);

	while (to != P_INVALID)
	{
		if (to == from)
		{
			Con_Printf ("Assosiation of %s would cause infinate loop\n", name);
			return P_INVALID;
		}
		to = part_type[to].assoc;
	}
	return orig;
}

static void P_LoadTexture (part_type_t *ptype, qboolean warn)
{
	if (*ptype->texname)
	{
		byte *data = NULL;
		char  filename[MAX_QPATH];
		int	  fwidth = 0, fheight = 0;
		char *texname = va ("%s%s%s", ptype->texname, ptype->looks.premul ? "_premul" : "", ptype->looks.nearest ? "_nearest" : "");

		ptype->looks.texture = TexMgr_FindTexture (NULL, texname);
		if (!ptype->looks.texture)
		{
			enum srcformat fmt = SRC_RGBA;
			if (!data)
			{
				q_snprintf (filename, sizeof (filename), "textures/%s", ptype->texname);
				data = Image_LoadImage (filename, &fwidth, &fheight, &fmt);
			}
			if (!data)
			{
				q_snprintf (filename, sizeof (filename), "%s", ptype->texname);
				data = Image_LoadImage (filename, &fwidth, &fheight, &fmt);
			}

			if (data)
			{
				ptype->looks.texture = TexMgr_LoadImage (
					NULL, texname, fwidth, fheight, fmt, data, filename, 0,
					(ptype->looks.premul ? TEXPREF_PREMULTIPLY : 0) | (ptype->looks.nearest ? TEXPREF_NEAREST : 0) | TEXPREF_NOPICMIP | TEXPREF_ALPHA);
			}
		}
	}
	else
		ptype->looks.texture = 0;

	if (!ptype->looks.texture)
	{
		// the specified texture isn't valid. make something up based upon the particle's type
		ptype->s1 = 0;
		ptype->t1 = 0;
		ptype->s2 = 1;
		ptype->t2 = 1;
		ptype->randsmax = 1;

#define PARTICLETEXTURESIZE 64
		if (ptype->looks.type == PT_SPARK)
		{
			static gltexture_t *thetex;
			if (!thetex)
			{
				static byte data[4 * 4 * 4];
				memset (data, 0xff, sizeof (data));
				thetex = TexMgr_LoadImage (
					NULL, "particles/white", 4, 4, SRC_RGBA, data, "", (src_offset_t)data, TEXPREF_PERSIST | TEXPREF_NOPICMIP | TEXPREF_ALPHA);
			}
			ptype->looks.texture = thetex;
		}
		else if (ptype->looks.type == PT_BEAM) // untextured beams get a single continuous blob
		{
			static gltexture_t *thetex;
			if (!thetex)
			{
				int			y, x;
				float		dy, d;
				static byte data[PARTICLETEXTURESIZE * PARTICLETEXTURESIZE * 4];
				memset (data, 0xff, sizeof (data));
				for (y = 0; y < PARTICLETEXTURESIZE; y++)
				{
					dy = (y - 0.5f * PARTICLETEXTURESIZE) / (PARTICLETEXTURESIZE * 0.5f - 1);
					d = 256 * (1 - (dy * dy));
					if (d < 0)
						d = 0;
					for (x = 0; x < PARTICLETEXTURESIZE; x++)
					{
						data[(y * PARTICLETEXTURESIZE + x) * 4 + 3] = (byte)d;
					}
				}
				thetex = TexMgr_LoadImage (
					NULL, "particles/beamtexture", PARTICLETEXTURESIZE, PARTICLETEXTURESIZE, SRC_RGBA, data, "", (src_offset_t)data,
					TEXPREF_PERSIST | TEXPREF_NOPICMIP | TEXPREF_ALPHA);
			}
			ptype->looks.texture = thetex;
		}
		else if (ptype->looks.type == PT_SPARKFAN) // untextured beams get a single continuous blob
		{
			static gltexture_t *thetex;
			if (!thetex)
			{
				int			y, x;
				float		dy, dx, d;
				static byte data[PARTICLETEXTURESIZE * PARTICLETEXTURESIZE * 4];
				for (y = 0; y < PARTICLETEXTURESIZE; y++)
				{
					dy = y / (PARTICLETEXTURESIZE * 0.5f - 1);
					for (x = 0; x < PARTICLETEXTURESIZE; x++)
					{
						dx = x / (PARTICLETEXTURESIZE * 0.5f - 1);
						d = 256 * (1 - (dx + dy));
						if (d < 0)
							d = 0;
						data[(y * PARTICLETEXTURESIZE + x) * 4 + 0] = (byte)d;
						data[(y * PARTICLETEXTURESIZE + x) * 4 + 1] = (byte)d;
						data[(y * PARTICLETEXTURESIZE + x) * 4 + 2] = (byte)d;
						data[(y * PARTICLETEXTURESIZE + x) * 4 + 3] = (byte)d / 2;
					}
				}
				thetex = TexMgr_LoadImage (
					NULL, "particles/ptritexture", PARTICLETEXTURESIZE, PARTICLETEXTURESIZE, SRC_RGBA, data, "", (src_offset_t)data,
					TEXPREF_PERSIST | TEXPREF_NOPICMIP | TEXPREF_ALPHA);
			}
			ptype->looks.texture = thetex;
		}
		else if (strstr (ptype->texname, "classicparticle"))
		{
			extern gltexture_t *particletexture1;
			ptype->looks.texture = particletexture1;
			ptype->s2 = 0.5;
			ptype->t2 = 0.5;
		}
		else if (strstr (ptype->texname, "glow") || strstr (ptype->texname, "ball") || ptype->looks.type == PT_TEXTUREDSPARK) // sparks and special names get a
																															  // nice circular texture.
		{
			static gltexture_t *thetex;
			if (!thetex)
			{
				int			y, x;
				float		dy, dx, d;
				static byte data[PARTICLETEXTURESIZE * PARTICLETEXTURESIZE * 4];
				memset (data, 0xff, sizeof (data));
				for (y = 0; y < PARTICLETEXTURESIZE; y++)
				{
					dy = (y - 0.5f * PARTICLETEXTURESIZE) / (PARTICLETEXTURESIZE * 0.5f - 1);
					for (x = 0; x < PARTICLETEXTURESIZE; x++)
					{
						dx = (x - 0.5f * PARTICLETEXTURESIZE) / (PARTICLETEXTURESIZE * 0.5f - 1);
						d = 255 * (1 - (dx * dx + dy * dy));
						if (d < 0)
							d = 0;
						data[(y * PARTICLETEXTURESIZE + x) * 4 + 3] = (byte)d;
					}
				}
				thetex = TexMgr_LoadImage (
					NULL, "particles/balltexture", PARTICLETEXTURESIZE, PARTICLETEXTURESIZE, SRC_RGBA, data, "", (src_offset_t)data,
					TEXPREF_PERSIST | TEXPREF_NOPICMIP | TEXPREF_ALPHA);
			}
			ptype->looks.texture = thetex;
		}
		else // anything else gets a fuzzy texture
		{
			static gltexture_t *thetex;
			if (!thetex)
			{
				int			y, x;
				static byte exptexture[16][16] = {
					{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0},
					{0, 0, 0, 1, 1, 1, 1, 1, 3, 1, 1, 2, 1, 0, 0, 0}, {0, 0, 0, 1, 1, 1, 1, 4, 4, 4, 5, 4, 2, 1, 1, 0},
					{0, 0, 1, 1, 6, 5, 5, 8, 6, 8, 3, 6, 3, 2, 1, 0}, {0, 0, 1, 5, 6, 7, 5, 6, 8, 8, 8, 3, 3, 1, 0, 0},
					{0, 0, 0, 1, 6, 8, 9, 9, 9, 9, 4, 6, 3, 1, 0, 0}, {0, 0, 2, 1, 7, 7, 9, 9, 9, 9, 5, 3, 1, 0, 0, 0},
					{0, 0, 2, 4, 6, 8, 9, 9, 9, 9, 8, 6, 1, 0, 0, 0}, {0, 0, 2, 2, 3, 5, 6, 8, 9, 8, 8, 4, 4, 1, 0, 0},
					{0, 0, 1, 2, 4, 1, 8, 7, 8, 8, 6, 5, 4, 1, 0, 0}, {0, 1, 1, 1, 7, 8, 1, 6, 7, 5, 4, 7, 1, 0, 0, 0},
					{0, 1, 2, 1, 1, 5, 1, 3, 4, 3, 1, 1, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0},
					{0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
				};
				static byte data[16 * 16 * 4];
				for (x = 0; x < 16; x++)
				{
					for (y = 0; y < 16; y++)
					{
						data[(y * 16 + x) * 4 + 0] = 255;
						data[(y * 16 + x) * 4 + 1] = 255;
						data[(y * 16 + x) * 4 + 2] = 255;
						data[(y * 16 + x) * 4 + 3] = exptexture[x][y] * 255 / 9.0;
					}
				}
				thetex = TexMgr_LoadImage (
					NULL, "particles/fuzzyparticle", 16, 16, SRC_RGBA, data, "", (src_offset_t)data, TEXPREF_PERSIST | TEXPREF_NOPICMIP | TEXPREF_ALPHA);
			}
			ptype->looks.texture = thetex;
		}
	}
}

static void P_ResetToDefaults (part_type_t *ptype)
{
	particle_t	*parts;
	part_type_t *torun;
	char		 tnamebuf[sizeof (ptype->name)];
	char		 tconfbuf[sizeof (ptype->config)];

	// go with a lazy clear of list.. mark everything as DEAD and let
	// the beam rendering handle removing nodes
	beamseg_t *beamsegs = ptype->beams;
	while (beamsegs)
	{
		beamsegs->flags |= BS_DEAD;
		beamsegs = beamsegs->next;
	}

	// forget any particles before its wiped
	while (ptype->particles)
	{
		parts = ptype->particles->next;
		ptype->particles->next = free_particles;
		free_particles = ptype->particles;
		ptype->particles = parts;
	}

	// if we're in the runstate loop through and remove from linked list
	if (ptype->state & PS_INRUNLIST)
	{
		if (part_run_list == ptype)
			part_run_list = part_run_list->nexttorun;
		else
		{
			for (torun = part_run_list; torun != NULL; torun = torun->nexttorun)
			{
				if (torun->nexttorun == ptype)
					torun->nexttorun = torun->nexttorun->nexttorun;
			}
		}
	}

	// some things need to be preserved before we clear everything.
	beamsegs = ptype->beams;
	strcpy (tnamebuf, ptype->name);
	strcpy (tconfbuf, ptype->config);

	// free uneeded info
	if (ptype->ramp)
		Mem_Free (ptype->ramp);
	if (ptype->sounds)
		Mem_Free (ptype->sounds);

	// reset everything we're too lazy to specifically set
	memset (ptype, 0, sizeof (*ptype));

	// now set any non-0 defaults.

	ptype->beams = beamsegs;
	ptype->rainfrequency = 1;
	strcpy (ptype->name, tnamebuf);
	strcpy (ptype->config, tconfbuf);
	ptype->assoc = P_INVALID;
	ptype->inwater = P_INVALID;
	ptype->cliptype = P_INVALID;
	ptype->emit = P_INVALID;
	ptype->fluidmask = FTECONTENTS_FLUID;
	ptype->alpha = 1;
	ptype->alphachange = 1;
	ptype->clipbounce = 0.8;
	ptype->clipcount = 1;
	ptype->colorindex = -1;
	ptype->rotationstartmin = -M_PI; // start with a random angle
	ptype->rotationstartrand = M_PI - ptype->rotationstartmin;
	ptype->spawnchance = 1;
	ptype->dl_time = 0;
	VectorSet (ptype->dl_rgb, 1, 1, 1);
	ptype->dl_corona_intensity = 0.25;
	ptype->dl_corona_scale = 0.5;
	VectorSet (ptype->dl_scales, 0, 1, 1);
	ptype->looks.stretch = 0.05;

	ptype->randsmax = 1;
	ptype->s2 = 1;
	ptype->t2 = 1;
}

char *PScript_ReadLine (char *buffer, size_t buffersize, const char *filedata, size_t filesize, size_t *offset)
{
	const char *start = filedata + *offset;
	const char *f = start;
	const char *e = filedata + filesize;
	if (f >= e)
		return NULL; // eof
	while (f < e)
	{
		if (*f++ == '\n')
			break;
	}

	*offset = f - filedata;

	buffersize--;
	if (buffersize >= (size_t)(f - start))
		buffersize = f - start;
	memcpy (buffer, start, buffersize);
	buffer[buffersize] = 0; // null terminate it

	return buffer;
}

// This is the function that loads the effect descriptions.
void PScript_ParseParticleEffectFile (const char *config, qboolean part_parseweak, char *context, size_t filesize)
{
	const char *var, *value;
	char	   *buf;
	qboolean	settype;
	qboolean	setalphadelta;
	qboolean	setbeamlen;

	part_type_t *ptype;
	int			 pnum, assoc;
	char		 line[512];
	char		 part_parsenamespace[MAX_QPATH];

	byte  *palrgba = (byte *)d_8to24table;
	size_t offset = 0;

	q_strlcpy (part_parsenamespace, config, sizeof (part_parsenamespace));
	config = part_parsenamespace;

nexteffect:

	if (!PScript_ReadLine (line, sizeof (line), context, filesize, &offset))
		return; // eof
reparse:

	Cmd_TokenizeString (line);

	var = Cmd_Argv (0);

	if (!strcmp (var, "r_effect") || !strcmp (var, "r_trail"))
	{ // add an emit/trail effect to all ents using said model
		PScript_AssociateEffect_f ();
		goto nexteffect;
	}
	else if (!strcmp (var, "r_partredirect"))
	{ // add an emit/trail effect to all ents using said model
		P_PartRedirect_f ();
		goto nexteffect;
	}
	else if (strcmp (var, "r_part"))
	{
		if (*var)
			Con_SafePrintf ("Unknown particle command \"%s\"\n", var);
		goto nexteffect;
	}

	settype = false;
	setalphadelta = false;
	setbeamlen = false;

	if (Cmd_Argc () != 2)
	{
		if (!strcmp (Cmd_Argv (1), "namespace"))
		{
			q_strlcpy (part_parsenamespace, Cmd_Argv (2), sizeof (part_parsenamespace));
			if (Cmd_Argc () >= 4)
				part_parseweak = atoi (Cmd_Argv (3));
			goto nexteffect;
		}
		Con_Printf ("No name for particle effect\n");
		goto nexteffect;
	}

	buf = PScript_ReadLine (line, sizeof (line), context, filesize, &offset);
	if (!buf)
		return; // eof
	while (*buf && *buf <= ' ')
		buf++; // no whitespace please.
	if (*buf != '{')
	{
		Con_Printf ("This is a multiline command and should be used within config files\n");
		goto reparse;
	}

	var = Cmd_Argv (1);
	if (*var == '+')
		ptype = P_GetParticleType (config, var + 1);
	else
		ptype = P_GetParticleType (config, var);

	//'weak' configs do not replace 'strong' configs
	// we allow weak to replace weak as a solution to the +assoc chain thing (to add, we effectively need to 'replace').
	if ((part_parseweak && ptype->loaded == 2))
	{
		int depth = 1;
		while (1)
		{
			buf = PScript_ReadLine (line, sizeof (line), context, filesize, &offset);
			if (!buf)
				return;

			while (*buf && *buf <= ' ')
				buf++; // no whitespace please.
			if (*buf == '{')
				depth++;
			else if (*buf == '}')
			{
				if (--depth == 0)
					break;
			}
		}
		goto nexteffect;
	}

	if (*var == '+')
	{
		if (ptype->loaded)
		{
			int	 i, parenttype;
			char newname[256];
			for (i = 0; i < 64; i++)
			{
				parenttype = ptype - part_type;
				q_snprintf (newname, sizeof (newname), "+%i%s", i, ptype->name);
				ptype = P_GetParticleType (config, newname);
				if (!ptype->loaded)
				{
					if (part_type[parenttype].assoc != P_INVALID)
						Con_Printf ("warning: assoc on particle chain %s overridden\n", var + 1);
					part_type[parenttype].assoc = ptype - part_type;
					break;
				}
			}
			if (i == 64)
			{
				Con_Printf ("Too many duplicate names, gave up\n");
				return;
			}
		}
	}
	else
	{
		if (ptype->loaded)
		{
			assoc = ptype->assoc;
			while (assoc != P_INVALID && assoc < numparticletypes)
			{
				if (*part_type[assoc].name == '+')
				{
					part_type[assoc].loaded = false;
					assoc = part_type[assoc].assoc;
				}
				else
					break;
			}
		}
	}
	if (!ptype)
	{
		Con_Printf ("Bad name\n");
		return;
	}

	pnum = ptype - part_type;

	P_ResetToDefaults (ptype);

	while (1)
	{
		buf = PScript_ReadLine (line, sizeof (line), context, filesize, &offset);
		if (!buf)
		{
			Con_Printf ("Unexpected end of buffer with effect %s\n", ptype->name);
			return;
		}
	skipread:
		while (*buf && *buf <= ' ')
			buf++; // no whitespace please.
		if (*buf == '}')
			break;

		Cmd_TokenizeString (buf);
		var = Cmd_Argv (0);
		value = Cmd_Argv (1);

		// TODO: switch this mess to some sort of binary tree to increase parse speed
		if (!strcmp (var, "shader"))
		{
			q_strlcpy (ptype->texname, ptype->name, sizeof (ptype->texname));

			buf = PScript_ReadLine (line, sizeof (line), context, filesize, &offset);
			if (!buf)
				continue;
			while (*buf && *buf <= ' ')
				buf++; // no leading whitespace please.
			if (*buf == '{')
			{
				int	  nest = 1;
				char *str = Mem_Alloc (3);
				int	  slen = 2;
				str[0] = '{';
				str[1] = '\n';
				str[2] = 0;
				while (nest)
				{
					buf = PScript_ReadLine (line, sizeof (line), context, filesize, &offset);
					if (!buf)
					{
						Con_Printf ("Unexpected end of buffer with effect %s\n", ptype->name);
						break;
					}
					while (*buf && *buf <= ' ')
						buf++; // no leading whitespace please.
					if (*buf == '}')
						--nest;
					if (*buf == '{')
						nest++;
					str = Mem_Realloc (str, slen + strlen (buf) + 2);
					strcpy (str + slen, buf);
					slen += strlen (str + slen);
					str[slen++] = '\n';
				}
				str[slen] = 0;
				Mem_Free (str);
			}
			else
				goto skipread;
		}
		else if (!strcmp (var, "texture") || !strcmp (var, "linear_texture") || !strcmp (var, "nearest_texture") || !strcmp (var, "nearesttexture"))
		{
			q_strlcpy (ptype->texname, value, sizeof (ptype->texname));
			ptype->looks.nearest = !strncmp (var, "nearest", 7);
		}
		else if (!strcmp (var, "tcoords"))
		{
			float tscale;

			tscale = atof (Cmd_Argv (5));
			if (tscale <= 0)
				tscale = 1;

			ptype->s1 = atof (value) / tscale;
			ptype->t1 = atof (Cmd_Argv (2)) / tscale;
			ptype->s2 = atof (Cmd_Argv (3)) / tscale;
			ptype->t2 = atof (Cmd_Argv (4)) / tscale;

			ptype->randsmax = atoi (Cmd_Argv (6));
			if (Cmd_Argc () > 7)
				ptype->texsstride = atof (Cmd_Argv (7)); /*FIXME: divide-by-tscale missing */
			else
				ptype->texsstride = 1 / tscale;

			if (ptype->randsmax < 1 || ptype->texsstride == 0)
				ptype->randsmax = 1;
		}
		else if (!strcmp (var, "atlas"))
		{ // atlas countineachaxis first [last]
			int dims;
			int i;
			int m;

			dims = atof (Cmd_Argv (1));
			i = atoi (Cmd_Argv (2));
			m = atoi (Cmd_Argv (3));
			if (dims < 1)
				dims = 1;

			if (m > (m / dims) * dims + dims - 1)
			{
				m = (m / dims) * dims + dims - 1;
				Con_Printf ("effect %s wraps across an atlased line\n", ptype->name);
			}
			if (m < i)
				m = i;

			ptype->s1 = 1.0 / dims * (i % dims);
			ptype->s2 = 1.0 / dims * (1 + (i % dims));
			ptype->t1 = 1.0 / dims * (i / dims);
			ptype->t2 = 1.0 / dims * (1 + (i / dims));

			ptype->randsmax = m - i;
			ptype->texsstride = ptype->s2 - ptype->s1;

			// its modulo
			ptype->randsmax++;
		}
		else if (!strcmp (var, "rotation"))
		{
			ptype->rotationstartmin = atof (value) * M_PI / 180;
			if (Cmd_Argc () > 2)
				ptype->rotationstartrand = atof (Cmd_Argv (2)) * M_PI / 180 - ptype->rotationstartmin;
			else
				ptype->rotationstartrand = 0;

			ptype->rotationmin = atof (Cmd_Argv (3)) * M_PI / 180;
			if (Cmd_Argc () > 4)
				ptype->rotationrand = atof (Cmd_Argv (4)) * M_PI / 180 - ptype->rotationmin;
			else
				ptype->rotationrand = 0;
		}
		else if (!strcmp (var, "rotationstart"))
		{
			ptype->rotationstartmin = atof (value) * M_PI / 180;
			if (Cmd_Argc () > 2)
				ptype->rotationstartrand = atof (Cmd_Argv (2)) * M_PI / 180 - ptype->rotationstartmin;
			else
				ptype->rotationstartrand = 0;
		}
		else if (!strcmp (var, "rotationspeed"))
		{
			ptype->rotationmin = atof (value) * M_PI / 180;
			if (Cmd_Argc () > 2)
				ptype->rotationrand = atof (Cmd_Argv (2)) * M_PI / 180 - ptype->rotationmin;
			else
				ptype->rotationrand = 0;
		}
		else if (!strcmp (var, "beamtexstep"))
		{
			ptype->rotationstartmin = 1 / atof (value);
			ptype->rotationstartrand = 0;
			setbeamlen = true;
		}
		else if (!strcmp (var, "beamtexspeed"))
		{
			ptype->rotationmin = atof (value);
		}
		else if (!strcmp (var, "scale"))
		{
			ptype->scale = atof (value);
			if (Cmd_Argc () > 2)
				ptype->scalerand = atof (Cmd_Argv (2)) - ptype->scale;
		}
		else if (!strcmp (var, "scalerand"))
			ptype->scalerand = atof (value);

		else if (!strcmp (var, "scalefactor"))
			ptype->looks.scalefactor = atof (value);
		else if (!strcmp (var, "scaledelta"))
			ptype->scaledelta = atof (value);
		else if (!strcmp (var, "stretchfactor")) // affects sparks
		{
			ptype->looks.stretch = atof (value);
			ptype->looks.minstretch = (Cmd_Argc () > 2) ? atof (Cmd_Argv (2)) : 0;
		}

		else if (!strcmp (var, "step"))
		{
			ptype->countspacing = atof (value);
			ptype->count = 1 / atof (value);
			if (Cmd_Argc () > 2)
				ptype->countrand = 1 / atof (Cmd_Argv (2));
			if (Cmd_Argc () > 3)
				ptype->countextra = atof (Cmd_Argv (3));
		}
		else if (!strcmp (var, "count"))
		{
			ptype->countspacing = 0;
			ptype->count = atof (value);
			if (Cmd_Argc () > 2)
				ptype->countrand = atof (Cmd_Argv (2));
			if (Cmd_Argc () > 3)
				ptype->countextra = atof (Cmd_Argv (3));
		}
		else if (!strcmp (var, "rainfrequency"))
		{ // multiplier to ramp up the effect or whatever (without affecting spawn patterns).
			ptype->rainfrequency = atof (value);
		}

		else if (!strcmp (var, "alpha"))
			ptype->alpha = atof (value);
		else if (!strcmp (var, "alpharand"))
			ptype->alpharand = atof (value);
#ifndef NOLEGACY
		else if (!strcmp (var, "alphachange"))
		{
			Con_DPrintf ("%s.%s: alphachange is deprecated, use alphadelta\n", ptype->config, ptype->name);
			ptype->alphachange = atof (value);
		}
#endif
		else if (!strcmp (var, "alphadelta"))
		{
			ptype->alphachange = atof (value);
			setalphadelta = true;
		}
		else if (!strcmp (var, "die"))
		{
			ptype->die = atof (value);
			if (Cmd_Argc () > 2)
			{
				float mn = ptype->die, mx = atof (Cmd_Argv (2));
				if (mn > mx)
				{
					mn = mx;
					mx = ptype->die;
				}
				ptype->die = mx;
				ptype->randdie = mx - mn;
			}
		}
#ifndef NOLEGACY
		else if (!strcmp (var, "diesubrand"))
		{
			Con_DPrintf ("%s.%s: diesubrand is deprecated, use die with two arguments\n", ptype->config, ptype->name);
			ptype->randdie = atof (value);
		}
#endif

		else if (!strcmp (var, "randomvel"))
		{ // shortcut for velwrand (and velbias for z bias)
			ptype->velbias[0] = ptype->velbias[1] = 0;
			ptype->velwrand[0] = ptype->velwrand[1] = atof (value);
			if (Cmd_Argc () > 3)
			{
				ptype->velbias[2] = atof (Cmd_Argv (2));
				ptype->velwrand[2] = atof (Cmd_Argv (3));
				ptype->velwrand[2] -= ptype->velbias[2]; /*make vert be the total range*/
				ptype->velwrand[2] /= 2;				 /*vert is actually +/- 1, not 0 to 1, so rescale it*/
				ptype->velbias[2] += ptype->velwrand[2]; /*and bias must be centered to the range*/
			}
			else if (Cmd_Argc () > 2)
			{
				ptype->velwrand[2] = atof (Cmd_Argv (2));
				ptype->velbias[2] = 0;
			}
			else
			{
				ptype->velwrand[2] = ptype->velwrand[0];
				ptype->velbias[2] = 0;
			}
		}
		else if (!strcmp (var, "veladd"))
		{
			ptype->veladd = atof (value);
			ptype->randomveladd = 0;
			if (Cmd_Argc () > 2)
				ptype->randomveladd = atof (Cmd_Argv (2)) - ptype->veladd;
		}
		else if (!strcmp (var, "orgadd"))
		{
			ptype->orgadd = atof (value);
			ptype->randomorgadd = 0;
			if (Cmd_Argc () > 2)
				ptype->randomorgadd = atof (Cmd_Argv (2)) - ptype->orgadd;
		}

		else if (!strcmp (var, "orgbias"))
		{
			ptype->orgbias[0] = atof (value);
			ptype->orgbias[1] = atof (Cmd_Argv (2));
			ptype->orgbias[2] = atof (Cmd_Argv (3));
		}
		else if (!strcmp (var, "orgwrand"))
		{
			ptype->orgwrand[0] = atof (value);
			ptype->orgwrand[1] = atof (Cmd_Argv (2));
			ptype->orgwrand[2] = atof (Cmd_Argv (3));
		}

		else if (!strcmp (var, "velbias"))
		{
			ptype->velbias[0] = atof (value);
			ptype->velbias[1] = atof (Cmd_Argv (2));
			ptype->velbias[2] = atof (Cmd_Argv (3));
		}
		else if (!strcmp (var, "velwrand"))
		{
			ptype->velwrand[0] = atof (value);
			ptype->velwrand[1] = atof (Cmd_Argv (2));
			ptype->velwrand[2] = atof (Cmd_Argv (3));
		}

		else if (!strcmp (var, "friction"))
		{
			ptype->friction[2] = ptype->friction[1] = ptype->friction[0] = atof (value);

			if (Cmd_Argc () > 3)
			{
				ptype->friction[2] = atof (Cmd_Argv (3));
				ptype->friction[1] = atof (Cmd_Argv (2));
			}
			else if (Cmd_Argc () > 2)
			{
				ptype->friction[2] = atof (Cmd_Argv (2));
			}
		}
		else if (!strcmp (var, "gravity"))
			ptype->gravity = atof (value);
		else if (!strcmp (var, "flurry"))
			ptype->flurry = atof (value);

		else if (!strcmp (var, "assoc"))
		{
			assoc = CheckAssosiation (config, value, pnum); // careful - this can realloc all the particle types
			ptype = &part_type[pnum];
			ptype->assoc = assoc;
		}
		else if (!strcmp (var, "inwater"))
		{
			// the underwater effect switch should only occur for
			// 1 level so the standard assoc check works
			assoc = CheckAssosiation (config, value, pnum);
			ptype = &part_type[pnum];
			ptype->inwater = assoc;
		}
		else if (!strcmp (var, "underwater"))
		{
			ptype->flags |= PT_TRUNDERWATER;

		parsefluid:
			if ((ptype->flags & (PT_TRUNDERWATER | PT_TROVERWATER)) == (PT_TRUNDERWATER | PT_TROVERWATER))
			{
				ptype->flags &= ~PT_TRUNDERWATER;
				Con_Printf ("%s.%s: both over and under water\n", ptype->config, ptype->name);
			}
			if (Cmd_Argc () == 1)
				ptype->fluidmask = FTECONTENTS_FLUID;
			else
			{
				int i = Cmd_Argc ();
				ptype->fluidmask = 0;
				while (i-- > 1)
				{
					const char *value_i = Cmd_Argv (i);
					if (!strcmp (value_i, "water"))
						ptype->fluidmask |= FTECONTENTS_WATER;
					else if (!strcmp (value_i, "slime"))
						ptype->fluidmask |= FTECONTENTS_SLIME;
					else if (!strcmp (value_i, "lava"))
						ptype->fluidmask |= FTECONTENTS_LAVA;
					else if (!strcmp (value_i, "sky"))
						ptype->fluidmask |= FTECONTENTS_SKY;
					else if (!strcmp (value_i, "fluid"))
						ptype->fluidmask |= FTECONTENTS_FLUID;
					else if (!strcmp (value_i, "solid"))
						ptype->fluidmask |= FTECONTENTS_SOLID;
					else if (!strcmp (value_i, "playerclip"))
						ptype->fluidmask |= FTECONTENTS_PLAYERCLIP;
					else if (!strcmp (value_i, "none"))
						ptype->fluidmask |= 0;
					else
						Con_Printf ("%s.%s: unknown contents: %s\n", ptype->config, ptype->name, value_i);
				}
			}
		}
		else if (!strcmp (var, "notunderwater"))
		{
			ptype->flags |= PT_TROVERWATER;
			goto parsefluid;
		}
		else if (!strcmp (var, "model"))
		{
			Con_DPrintf ("%s.%s: model particles are not supported in this build\n", ptype->config, ptype->name);
		}
		else if (!strcmp (var, "sound"))
		{
			const char *e;
			ptype->sounds = Mem_Realloc (ptype->sounds, sizeof (partsounds_t) * (ptype->numsounds + 1));
			q_strlcpy (ptype->sounds[ptype->numsounds].name, Cmd_Argv (1), sizeof (ptype->sounds[ptype->numsounds].name));
			if (*ptype->sounds[ptype->numsounds].name)
				S_PrecacheSound (ptype->sounds[ptype->numsounds].name);

			ptype->sounds[ptype->numsounds].vol = 1;
			ptype->sounds[ptype->numsounds].atten = 1;
			ptype->sounds[ptype->numsounds].pitch = 100;
			ptype->sounds[ptype->numsounds].delay = 0;
			ptype->sounds[ptype->numsounds].weight = 0;

			strtoul (Cmd_Argv (2), (char **)&e, 0);
			while (*e == ' ' || *e == '\t')
				e++;
			if (*e)
			{
				int p;
				for (p = 2; p < Cmd_Argc (); p++)
				{
					e = Cmd_Argv (p);

					if (!q_strncasecmp (e, "vol=", 4) || !q_strncasecmp (e, "volume=", 7))
						ptype->sounds[ptype->numsounds].vol = atof (strchr (e, '=') + 1);
					else if (!q_strncasecmp (e, "attn=", 5) || !q_strncasecmp (e, "atten=", 6) || !q_strncasecmp (e, "attenuation=", 12))
					{
						e = strchr (e, '=') + 1;
						if (!strcmp (e, "none"))
							ptype->sounds[ptype->numsounds].atten = 0;
						else if (!strcmp (e, "normal"))
							ptype->sounds[ptype->numsounds].atten = 1;
						else
							ptype->sounds[ptype->numsounds].atten = atof (e);
					}
					else if (!q_strncasecmp (e, "pitch=", 6))
						ptype->sounds[ptype->numsounds].pitch = atof (strchr (e, '=') + 1);
					else if (!q_strncasecmp (e, "delay=", 6))
						ptype->sounds[ptype->numsounds].delay = atof (strchr (e, '=') + 1);
					else if (!q_strncasecmp (e, "weight=", 7))
						ptype->sounds[ptype->numsounds].weight = atof (strchr (e, '=') + 1);
					else
						Con_Printf ("Bad named argument: %s\n", e);
				}
			}
			else
			{
				ptype->sounds[ptype->numsounds].vol = atof (Cmd_Argv (2));
				if (!ptype->sounds[ptype->numsounds].vol)
					ptype->sounds[ptype->numsounds].vol = 1;
				ptype->sounds[ptype->numsounds].atten = atof (Cmd_Argv (3));
				if (!ptype->sounds[ptype->numsounds].atten)
					ptype->sounds[ptype->numsounds].atten = 1;
				ptype->sounds[ptype->numsounds].pitch = atof (Cmd_Argv (4));
				if (!ptype->sounds[ptype->numsounds].pitch)
					ptype->sounds[ptype->numsounds].pitch = 100;
				ptype->sounds[ptype->numsounds].delay = atof (Cmd_Argv (5));
				if (!ptype->sounds[ptype->numsounds].delay)
					ptype->sounds[ptype->numsounds].delay = 0;
				ptype->sounds[ptype->numsounds].weight = atof (Cmd_Argv (6));
			}
			if (!ptype->sounds[ptype->numsounds].weight)
				ptype->sounds[ptype->numsounds].weight = 1;
			ptype->numsounds++;
		}
		else if (!strcmp (var, "colorindex"))
		{
			if (Cmd_Argc () > 2)
				ptype->colorrand = strtoul (Cmd_Argv (2), NULL, 0);
			ptype->colorindex = strtoul (value, NULL, 0);
		}
		else if (!strcmp (var, "colorrand"))
			ptype->colorrand = atoi (value); // now obsolete
		else if (!strcmp (var, "citracer"))
			ptype->flags |= PT_CITRACER;

		else if (!strcmp (var, "red"))
			ptype->rgb[0] = atof (value) / 255;
		else if (!strcmp (var, "green"))
			ptype->rgb[1] = atof (value) / 255;
		else if (!strcmp (var, "blue"))
			ptype->rgb[2] = atof (value) / 255;
		else if (!strcmp (var, "rgb"))
		{ // byte version
			ptype->rgb[0] = ptype->rgb[1] = ptype->rgb[2] = atof (value) / 255;
			if (Cmd_Argc () > 3)
			{
				ptype->rgb[1] = atof (Cmd_Argv (2)) / 255;
				ptype->rgb[2] = atof (Cmd_Argv (3)) / 255;
			}
		}
		else if (!strcmp (var, "rgbf"))
		{ // float version
			ptype->rgb[0] = ptype->rgb[1] = ptype->rgb[2] = atof (value);
			if (Cmd_Argc () > 3)
			{
				ptype->rgb[1] = atof (Cmd_Argv (2));
				ptype->rgb[2] = atof (Cmd_Argv (3));
			}
		}

		else if (!strcmp (var, "reddelta"))
		{
			ptype->rgbchange[0] = atof (value) / 255;
			if (!ptype->rgbchangetime)
				ptype->rgbchangetime = ptype->die;
		}
		else if (!strcmp (var, "greendelta"))
		{
			ptype->rgbchange[1] = atof (value) / 255;
			if (!ptype->rgbchangetime)
				ptype->rgbchangetime = ptype->die;
		}
		else if (!strcmp (var, "bluedelta"))
		{
			ptype->rgbchange[2] = atof (value) / 255;
			if (!ptype->rgbchangetime)
				ptype->rgbchangetime = ptype->die;
		}
		else if (!strcmp (var, "rgbdelta"))
		{ // byte version
			ptype->rgbchange[0] = ptype->rgbchange[1] = ptype->rgbchange[2] = atof (value) / 255;
			if (Cmd_Argc () > 3)
			{
				ptype->rgbchange[1] = atof (Cmd_Argv (2)) / 255;
				ptype->rgbchange[2] = atof (Cmd_Argv (3)) / 255;
			}
			if (!ptype->rgbchangetime)
				ptype->rgbchangetime = ptype->die;
		}
		else if (!strcmp (var, "rgbdeltaf"))
		{ // float version
			ptype->rgbchange[0] = ptype->rgbchange[1] = ptype->rgbchange[2] = atof (value);
			if (Cmd_Argc () > 3)
			{
				ptype->rgbchange[1] = atof (Cmd_Argv (2));
				ptype->rgbchange[2] = atof (Cmd_Argv (3));
			}
			if (!ptype->rgbchangetime)
				ptype->rgbchangetime = ptype->die;
		}
		else if (!strcmp (var, "rgbdeltatime"))
			ptype->rgbchangetime = atof (value);

		else if (!strcmp (var, "redrand"))
			ptype->rgbrand[0] = atof (value) / 255;
		else if (!strcmp (var, "greenrand"))
			ptype->rgbrand[1] = atof (value) / 255;
		else if (!strcmp (var, "bluerand"))
			ptype->rgbrand[2] = atof (value) / 255;
		else if (!strcmp (var, "rgbrand"))
		{ // byte version
			ptype->rgbrand[0] = ptype->rgbrand[1] = ptype->rgbrand[2] = atof (value) / 255;
			if (Cmd_Argc () > 3)
			{
				ptype->rgbrand[1] = atof (Cmd_Argv (2)) / 255;
				ptype->rgbrand[2] = atof (Cmd_Argv (3)) / 255;
			}
		}
		else if (!strcmp (var, "rgbrandf"))
		{ // float version
			ptype->rgbrand[0] = ptype->rgbrand[1] = ptype->rgbrand[2] = atof (value);
			if (Cmd_Argc () > 3)
			{
				ptype->rgbrand[1] = atof (Cmd_Argv (2));
				ptype->rgbrand[2] = atof (Cmd_Argv (3));
			}
		}

		else if (!strcmp (var, "rgbrandsync"))
		{
			ptype->rgbrandsync[0] = ptype->rgbrandsync[1] = ptype->rgbrandsync[2] = atof (value);
			if (Cmd_Argc () > 3)
			{
				ptype->rgbrandsync[1] = atof (Cmd_Argv (2));
				ptype->rgbrandsync[2] = atof (Cmd_Argv (3));
			}
		}
		else if (!strcmp (var, "redrandsync"))
			ptype->rgbrandsync[0] = atof (value);
		else if (!strcmp (var, "greenrandsync"))
			ptype->rgbrandsync[1] = atof (value);
		else if (!strcmp (var, "bluerandsync"))
			ptype->rgbrandsync[2] = atof (value);

		else if (!strcmp (var, "stains"))
			ptype->stainonimpact = atof (value);
		else if (!strcmp (var, "blend"))
		{
			// small note: use premultiplied alpha where possible. this reduces the required state switches.
			ptype->looks.premul = false;
			if (!strcmp (value, "adda") || !strcmp (value, "add"))
				ptype->looks.blendmode = BM_ADDA;
			else if (!strcmp (value, "addc"))
				ptype->looks.blendmode = BM_ADDC;
			else if (!strcmp (value, "subtract"))
				ptype->looks.blendmode = BM_SUBTRACT;
			else if (!strcmp (value, "invmoda") || !strcmp (value, "invmod"))
				ptype->looks.blendmode = BM_INVMODA;
			else if (!strcmp (value, "invmodc"))
				ptype->looks.blendmode = BM_INVMODC;
			else if (!strcmp (value, "blendcolour") || !strcmp (value, "blendcolor"))
				ptype->looks.blendmode = BM_BLENDCOLOUR;
			else if (!strcmp (value, "blendalpha") || !strcmp (value, "blend"))
				ptype->looks.blendmode = BM_BLEND;
			else if (!strcmp (value, "premul_subtract"))
			{
				ptype->looks.premul = 1;
				ptype->looks.blendmode = BM_INVMODC;
			}
			else if (!strcmp (value, "premul_add"))
			{
				ptype->looks.premul = 2;
				ptype->looks.blendmode = BM_PREMUL;
			}
			else if (!strcmp (value, "premul_blend"))
			{
				ptype->looks.premul = 1;
				ptype->looks.blendmode = BM_PREMUL;
			}
			else
			{
				Con_DPrintf ("%s.%s: uses unknown blend type '%s', assuming legacy 'blendalpha'\n", ptype->config, ptype->name, value);
				ptype->looks.blendmode = BM_BLEND; // fallback
			}
		}
		else if (!strcmp (var, "spawnmode"))
		{
			if (!strcmp (value, "circle"))
				ptype->spawnmode = SM_CIRCLE;
			else if (!strcmp (value, "ball"))
				ptype->spawnmode = SM_BALL;
			else if (!strcmp (value, "spiral"))
				ptype->spawnmode = SM_SPIRAL;
			else if (!strcmp (value, "tracer"))
				ptype->spawnmode = SM_TRACER;
			else if (!strcmp (value, "telebox"))
				ptype->spawnmode = SM_TELEBOX;
			else if (!strcmp (value, "lavasplash"))
				ptype->spawnmode = SM_LAVASPLASH;
			else if (!strcmp (value, "uniformcircle"))
				ptype->spawnmode = SM_UNICIRCLE;
			else if (!strcmp (value, "syncfield"))
			{
				ptype->spawnmode = SM_FIELD;
#ifndef NOLEGACY
				ptype->spawnparam1 = 16;
				ptype->spawnparam2 = 0;
#endif
			}
			else if (!strcmp (value, "distball"))
				ptype->spawnmode = SM_DISTBALL;
			else if (!strcmp (value, "box"))
				ptype->spawnmode = SM_BOX;
			else
			{
				Con_DPrintf ("%s.%s: uses unknown spawn type '%s', assuming 'box'\n", ptype->config, ptype->name, value);
				ptype->spawnmode = SM_BOX;
			}

			if (Cmd_Argc () > 2)
			{
				if (Cmd_Argc () > 3)
					ptype->spawnparam2 = atof (Cmd_Argv (3));
				ptype->spawnparam1 = atof (Cmd_Argv (2));
			}
		}
		else if (!strcmp (var, "type"))
		{
			if (!strcmp (value, "beam"))
				ptype->looks.type = PT_BEAM;
			else if (!strcmp (value, "spark") || !strcmp (value, "linespark"))
				ptype->looks.type = PT_SPARK;
			else if (!strcmp (value, "sparkfan") || !strcmp (value, "trianglefan"))
				ptype->looks.type = PT_SPARKFAN;
			else if (!strcmp (value, "texturedspark"))
				ptype->looks.type = PT_TEXTUREDSPARK;
			else if (!strcmp (value, "decal") || !strcmp (value, "cdecal"))
				ptype->looks.type = PT_CDECAL;
			else if (!strcmp (value, "udecal"))
				ptype->looks.type = PT_UDECAL;
			else if (!strcmp (value, "normal"))
				ptype->looks.type = PT_NORMAL;
			else
			{
				Con_DPrintf ("%s.%s: uses unknown render type '%s', assuming 'normal'\n", ptype->config, ptype->name, value);
				ptype->looks.type = PT_NORMAL; // fallback
			}
			settype = true;
		}
		else if (!strcmp (var, "clippeddecal")) // mask, match
		{
			if (Cmd_Argc () >= 2)
			{ // decal only appears where: (surfflags&mask)==match
				ptype->surfflagmatch = ptype->surfflagmask = strtoul (Cmd_Argv (1), NULL, 0);
				if (Cmd_Argc () >= 3)
					ptype->surfflagmatch = strtoul (Cmd_Argv (2), NULL, 0);
			}
			ptype->looks.type = PT_CDECAL;
			settype = true;
		}
#ifndef NOLEGACY
		else if (!strcmp (var, "isbeam"))
		{
			Con_DPrintf ("%s.%s: isbeam is deprecated, use type beam\n", ptype->config, ptype->name);
			ptype->looks.type = PT_BEAM;
			settype = true;
		}
#endif
		else if (!strcmp (var, "spawntime"))
			ptype->spawntime = atof (value);
		else if (!strcmp (var, "spawnchance"))
			ptype->spawnchance = atof (value);
		else if (!strcmp (var, "cliptype"))
		{
			assoc = P_AllocateParticleType (config, value); // careful - this can realloc all the particle types
			ptype = &part_type[pnum];
			ptype->cliptype = assoc;
		}
		else if (!strcmp (var, "clipcount"))
			ptype->clipcount = atof (value);
		else if (!strcmp (var, "clipbounce"))
		{
			ptype->clipbounce = atof (value);
			if (ptype->clipbounce < 0 && ptype->cliptype == P_INVALID)
				ptype->cliptype = pnum;
		}
		else if (!strcmp (var, "bounce"))
		{
			ptype->cliptype = pnum;
			ptype->clipbounce = atof (value);
		}

		else if (!strcmp (var, "emit"))
		{
			assoc = P_AllocateParticleType (config, value); // careful - this can realloc all the particle types
			ptype = &part_type[pnum];
			ptype->emit = assoc;
		}
		else if (!strcmp (var, "emitinterval"))
			ptype->emittime = atof (value);
		else if (!strcmp (var, "emitintervalrand"))
			ptype->emitrand = atof (value);
		else if (!strcmp (var, "emitstart"))
			ptype->emitstart = atof (value);

#ifndef NOLEGACY
		// old names
		else if (!strcmp (var, "areaspread"))
		{
			Con_DPrintf ("%s.%s: areaspread is deprecated, use spawnorg\n", ptype->config, ptype->name);
			ptype->areaspread = atof (value);
		}
		else if (!strcmp (var, "areaspreadvert"))
		{
			Con_DPrintf ("%s.%s: areaspreadvert is deprecated, use spawnorg\n", ptype->config, ptype->name);
			ptype->areaspreadvert = atof (value);
		}
		else if (!strcmp (var, "offsetspread"))
		{
			Con_DPrintf ("%s.%s: offsetspread is deprecated, use spawnvel\n", ptype->config, ptype->name);
			ptype->spawnvel = atof (value);
		}
		else if (!strcmp (var, "offsetspreadvert"))
		{
			Con_DPrintf ("%s.%s: offsetspreadvert is deprecated, use spawnvel\n", ptype->config, ptype->name);
			ptype->spawnvelvert = atof (value);
		}
#endif

		// current names
		else if (!strcmp (var, "spawnorg"))
		{
			ptype->areaspreadvert = ptype->areaspread = atof (value);

			if (Cmd_Argc () > 2)
				ptype->areaspreadvert = atof (Cmd_Argv (2));
		}
		else if (!strcmp (var, "spawnvel"))
		{
			ptype->spawnvelvert = ptype->spawnvel = atof (value);

			if (Cmd_Argc () > 2)
				ptype->spawnvelvert = atof (Cmd_Argv (2));
		}

#ifndef NOLEGACY
		// spawn mode param fields
		else if (!strcmp (var, "spawnparam1"))
		{
			ptype->spawnparam1 = atof (value);
			Con_DPrintf ("%s.%s: 'spawnparam1' is deprecated, use 'spawnmode foo X'\n", ptype->config, ptype->name);
		}
		else if (!strcmp (var, "spawnparam2"))
		{
			ptype->spawnparam2 = atof (value);
			Con_DPrintf ("%s.%s: 'spawnparam2' is deprecated, use 'spawnmode foo X Y'\n", ptype->config, ptype->name);
		}
		/*		else if (!strcmp(var, "spawnparam3"))
					ptype->spawnparam3 = atof(value); */
		else if (!strcmp (var, "up"))
		{
			ptype->orgbias[2] = atof (value);
			Con_DPrintf ("%s.%s: up is deprecated, use orgbias 0 0 Z\n", ptype->config, ptype->name);
		}
#endif

		else if (!strcmp (var, "rampmode"))
		{
			if (!strcmp (value, "none"))
				ptype->rampmode = RAMP_NONE;
#ifndef NOLEGACY
			else if (!strcmp (value, "absolute"))
			{
				Con_DPrintf ("%s.%s: 'rampmode absolute' is deprecated, use 'rampmode nearest'\n", ptype->config, ptype->name);
				ptype->rampmode = RAMP_NEAREST;
			}
#endif
			else if (!strcmp (value, "nearest"))
				ptype->rampmode = RAMP_NEAREST;
			else if (!strcmp (value, "lerp")) // don't use the name 'linear'. ramps are there to avoid linear...
				ptype->rampmode = RAMP_LERP;
			else if (!strcmp (value, "delta"))
				ptype->rampmode = RAMP_DELTA;
			else
			{
				Con_DPrintf ("%s.%s: uses unknown ramp mode '%s', assuming 'delta'\n", ptype->config, ptype->name, value);
				ptype->rampmode = RAMP_DELTA;
			}
		}
		else if (!strcmp (var, "rampindexlist"))
		{ // better not use this with delta ramps...
			int cidx, i;

			i = 1;
			while (i < Cmd_Argc ())
			{
				ptype->ramp = Mem_Realloc (ptype->ramp, sizeof (ramp_t) * (ptype->rampindexes + 1));

				cidx = atoi (Cmd_Argv (i));
				ptype->ramp[ptype->rampindexes].alpha = cidx > 255 ? 0.5 : 1;

				cidx = (cidx & 0xff) * 4;
				ptype->ramp[ptype->rampindexes].rgb[0] = palrgba[cidx] * (1 / 255.0);
				ptype->ramp[ptype->rampindexes].rgb[1] = palrgba[cidx + 1] * (1 / 255.0);
				ptype->ramp[ptype->rampindexes].rgb[2] = palrgba[cidx + 2] * (1 / 255.0);

				ptype->ramp[ptype->rampindexes].scale = ptype->scale;

				ptype->rampindexes++;
				i++;
			}
		}
		else if (!strcmp (var, "rampindex"))
		{
			int cidx;
			ptype->ramp = Mem_Realloc (ptype->ramp, sizeof (ramp_t) * (ptype->rampindexes + 1));

			cidx = atoi (value);
			ptype->ramp[ptype->rampindexes].alpha = cidx > 255 ? 0.5 : 1;

			if (Cmd_Argc () > 2) // they gave alpha
				ptype->ramp[ptype->rampindexes].alpha *= atof (Cmd_Argv (2));

			cidx = (cidx & 0xff) * 4;
			ptype->ramp[ptype->rampindexes].rgb[0] = palrgba[cidx] * (1 / 255.0);
			ptype->ramp[ptype->rampindexes].rgb[1] = palrgba[cidx + 1] * (1 / 255.0);
			ptype->ramp[ptype->rampindexes].rgb[2] = palrgba[cidx + 2] * (1 / 255.0);

			if (Cmd_Argc () > 3) // they gave scale
				ptype->ramp[ptype->rampindexes].scale = atof (Cmd_Argv (3));
			else
				ptype->ramp[ptype->rampindexes].scale = ptype->scale;

			ptype->rampindexes++;
		}
		else if (!strcmp (var, "ramp"))
		{
			ptype->ramp = Mem_Realloc (ptype->ramp, sizeof (ramp_t) * (ptype->rampindexes + 1));

			ptype->ramp[ptype->rampindexes].rgb[0] = atof (value) / 255;
			if (Cmd_Argc () > 3) // seperate rgb
			{
				ptype->ramp[ptype->rampindexes].rgb[1] = atof (Cmd_Argv (2)) / 255;
				ptype->ramp[ptype->rampindexes].rgb[2] = atof (Cmd_Argv (3)) / 255;

				if (Cmd_Argc () > 4) // have we alpha and scale changes?
				{
					ptype->ramp[ptype->rampindexes].alpha = atof (Cmd_Argv (4));
					if (Cmd_Argc () > 5) // have we scale changes?
						ptype->ramp[ptype->rampindexes].scale = atof (Cmd_Argv (5));
					else
						ptype->ramp[ptype->rampindexes].scale = ptype->scaledelta;
				}
				else
				{
					ptype->ramp[ptype->rampindexes].alpha = ptype->alpha;
					ptype->ramp[ptype->rampindexes].scale = ptype->scaledelta;
				}
			}
			else // they only gave one value
			{
				ptype->ramp[ptype->rampindexes].rgb[1] = ptype->ramp[ptype->rampindexes].rgb[0];
				ptype->ramp[ptype->rampindexes].rgb[2] = ptype->ramp[ptype->rampindexes].rgb[0];

				ptype->ramp[ptype->rampindexes].alpha = ptype->alpha;
				ptype->ramp[ptype->rampindexes].scale = ptype->scaledelta;
			}

			ptype->rampindexes++;
		}
		else if (!strcmp (var, "viewspace"))
		{
			Con_DPrintf ("%s.%s: viewspace particles are not supported in this build\n", ptype->config, ptype->name);
		}
		else if (!strcmp (var, "perframe"))
			ptype->flags |= PT_INVFRAMETIME;
		else if (!strcmp (var, "averageout"))
			ptype->flags |= PT_AVERAGETRAIL;
		else if (!strcmp (var, "nostate"))
			ptype->flags |= PT_NOSTATE;
		else if (!strcmp (var, "nospreadfirst"))
			ptype->flags |= PT_NOSPREADFIRST;
		else if (!strcmp (var, "nospreadlast"))
			ptype->flags |= PT_NOSPREADLAST;

		else if (!strcmp (var, "lightradius"))
		{ // float version
			ptype->dl_radius[0] = ptype->dl_radius[1] = atof (value);
			if (Cmd_Argc () > 2)
				ptype->dl_radius[1] = atof (Cmd_Argv (2));
			ptype->dl_radius[1] -= ptype->dl_radius[0];
		}
		else if (!strcmp (var, "lightradiusfade"))
			ptype->dl_decay[3] = atof (value);
		else if (!strcmp (var, "lightrgb"))
		{
			ptype->dl_rgb[0] = atof (value);
			ptype->dl_rgb[1] = atof (Cmd_Argv (2));
			ptype->dl_rgb[2] = atof (Cmd_Argv (3));
		}
		else if (!strcmp (var, "lightrgbfade"))
		{
			ptype->dl_decay[0] = atof (value);
			ptype->dl_decay[1] = atof (Cmd_Argv (2));
			ptype->dl_decay[2] = atof (Cmd_Argv (3));
		}
		else if (!strcmp (var, "lightcorona"))
		{
			ptype->dl_corona_intensity = atof (value);
			ptype->dl_corona_scale = atof (Cmd_Argv (2));
		}
		else if (!strcmp (var, "lighttime"))
			ptype->dl_time = atof (value);
		else if (!strcmp (var, "lightshadows"))
			ptype->flags = (ptype->flags & ~PT_NODLSHADOW) | (atof (value) ? 0 : PT_NODLSHADOW);
		else if (!strcmp (var, "lightcubemap"))
			ptype->dl_cubemapnum = atoi (value);
		else if (!strcmp (var, "lightscales"))
		{ // ambient diffuse specular
			ptype->dl_scales[0] = atof (value);
			ptype->dl_scales[1] = atof (Cmd_Argv (2));
			ptype->dl_scales[2] = atof (Cmd_Argv (3));
		}
		else if (!strcmp (var, "spawnstain"))
		{
			Con_DPrintf ("%s.%s: spawnstain is not supported in this build\n", ptype->config, ptype->name);
		}
		else if (Cmd_Argc ())
			Con_DPrintf ("%s.%s: %s is not a recognised particle type field\n", ptype->config, ptype->name, var);
	}
	ptype->loaded = part_parseweak ? 1 : 2;
	if (ptype->clipcount < 1)
		ptype->clipcount = 1;

	if (!settype)
	{
		if (ptype->looks.type == PT_NORMAL && !*ptype->texname)
		{
			if (ptype->scale)
			{
				ptype->looks.type = PT_SPARKFAN;
				Con_DPrintf ("%s.%s: effect lacks a texture. assuming type sparkfan.\n", ptype->config, ptype->name);
			}
			else
			{
				ptype->looks.type = PT_SPARK;
				Con_DPrintf ("%s.%s: effect lacks a texture. assuming type spark.\n", ptype->config, ptype->name);
			}
		}
		else if (ptype->looks.type == PT_SPARK)
		{
			if (*ptype->texname)
				ptype->looks.type = PT_TEXTUREDSPARK;
			else if (ptype->scale)
				ptype->looks.type = PT_SPARKFAN;
		}
	}

	// use old behavior if not using alphadelta
	if (!setalphadelta)
		ptype->alphachange = (-ptype->alphachange / ptype->die) * ptype->alpha;

	FinishParticleType (ptype);

	if (ptype->looks.type == PT_BEAM && !setbeamlen)
		ptype->rotationstartmin = 1 / 128.0;

	goto nexteffect;
}

#if 1 //_DEBUG
// R_BeamInfo_f - debug junk
static void P_BeamInfo_f (void)
{
	beamseg_t *bs;
	int		   i, j, k, l, m;

	i = 0;

	for (bs = free_beams; bs; bs = bs->next)
		i++;

	Con_Printf ("%i free beams\n", i);

	for (i = 0; i < numparticletypes; i++)
	{
		m = l = k = j = 0;
		for (bs = part_type[i].beams; bs; bs = bs->next)
		{
			if (!bs->p)
				k++;

			if (bs->flags & BS_DEAD)
				l++;

			if (bs->flags & BS_LASTSEG)
				m++;

			j++;
		}

		if (j)
			Con_Printf ("Type %i = %i NULL p, %i DEAD, %i LASTSEG, %i total\n", i, k, l, m, j);
	}
}

static void P_PartInfo_f (void)
{
	particle_t	   *p;
	clippeddecal_t *d;
	part_type_t	   *ptype;
	int				totalp = 0, totald = 0, freep, freed, runningp = 0, runningd = 0, runninge = 0, runningt = 0;

	int i, j, k;

	Con_DPrintf ("Full list of  effects:\n");
	for (i = 0; i < numparticletypes; i++)
	{
		j = 0;
		for (p = part_type[i].particles; p; p = p->next)
			j++;
		totalp += j;

		k = 0;
		for (d = part_type[i].clippeddecals; d; d = d->next)
			k++;
		totald += k;

		if (j || k)
		{
			Con_DPrintf ("Type %s.%s = %i+%i total\n", part_type[i].config, part_type[i].name, j, k);
			if (!(part_type[i].state & PS_INRUNLIST))
				Con_Printf (CON_WARNING "%s.%s NOT RUNNING\n", part_type[i].config, part_type[i].name);
		}
	}

	Con_Printf ("Running effects:\n");
	// maintain run list
	for (ptype = part_run_list; ptype; ptype = ptype->nexttorun)
	{
		Con_Printf ("Type %s.%s", ptype->config, ptype->name);

		j = 0;
		for (p = ptype->particles; p; p = p->next)
			j++;
		if (j)
		{
			Con_Printf ("\t%i particles", j);
			if (ptype->cliptype >= 0 || ptype->stainonimpact)
			{
				Con_Printf ("(+traceline)");
				runningt += j;
			}
		}
		runningp += j;

		k = 0;
		for (d = ptype->clippeddecals; d; d = d->next)
			k++;
		if (k)
			Con_Printf ("%s%i decals", ptype->particles ? ", " : "\t", k);
		runningd += k;

		Con_Printf ("\n");
		runninge++;
	}
	Con_Printf ("End of list\n");

	for (p = free_particles, freep = 0; p; p = p->next)
		freep++;
	for (d = free_decals, freed = 0; d; d = d->next)
		freed++;

	Con_DPrintf ("%i running effects.\n", runninge);
	Con_Printf ("%i particles, %i free, %i traces.\n", runningp, freep, runningt);
	Con_Printf ("%i decals, %i free.\n", runningd, freed);

	if (totalp != runningp)
		Con_Printf ("%i particles unaccounted for\n", totalp - runningp);
	if (totald != runningd)
		Con_Printf ("%i decals unaccounted for\n", totald - runningd);
}
#endif

static void FinishParticleType (part_type_t *ptype)
{
	// if there is a chance that it moves
	if (ptype->gravity || ptype->veladd || ptype->spawnvel || ptype->spawnvelvert || DotProduct (ptype->velwrand, ptype->velwrand) ||
		DotProduct (ptype->velbias, ptype->velbias) || ptype->flurry)
		ptype->flags |= PT_VELOCITY;
	if (DotProduct (ptype->velbias, ptype->velbias) || DotProduct (ptype->velwrand, ptype->velwrand) || DotProduct (ptype->orgwrand, ptype->orgwrand))
		ptype->flags |= PT_WORLDSPACERAND;
	// if it has friction
	if (ptype->friction[0] || ptype->friction[1] || ptype->friction[2])
		ptype->flags |= PT_FRICTION;

	P_LoadTexture (ptype, true);
	if (ptype->dl_decay[3] && !ptype->dl_time)
		ptype->dl_time = ptype->dl_radius[0] / ptype->dl_decay[3];
	if (ptype->looks.scalefactor > 1 && !ptype->looks.invscalefactor)
	{
		ptype->scale *= ptype->looks.scalefactor;
		ptype->scalerand *= ptype->looks.scalefactor;
		/*too lazy to go through ramps*/
		ptype->looks.scalefactor = 1;
	}
	ptype->looks.invscalefactor = 1 - ptype->looks.scalefactor;

	if (ptype->looks.type == PT_TEXTUREDSPARK && !ptype->looks.stretch)
		ptype->looks.stretch = 0.05; // the old default.

	if (ptype->looks.type == PT_SPARK && r_part_sparks.value < 0)
		ptype->looks.type = PT_INVISIBLE;
	if (ptype->looks.type == PT_TEXTUREDSPARK && !r_part_sparks_textured.value)
		ptype->looks.type = PT_SPARK;
	if (ptype->looks.type == PT_SPARKFAN && !r_part_sparks_trifan.value)
		ptype->looks.type = PT_SPARK;
	if (ptype->looks.type == PT_SPARK && !r_part_sparks.value)
		ptype->looks.type = PT_INVISIBLE;
	if (ptype->looks.type == PT_BEAM && r_part_beams.value <= 0)
		ptype->looks.type = PT_INVISIBLE;

	if (ptype->rampmode && !ptype->ramp)
	{
		ptype->rampmode = RAMP_NONE;
		Con_Printf ("%s.%s: Particle has a ramp mode but no ramp\n", ptype->config, ptype->name);
	}
	else if (ptype->ramp && !ptype->rampmode)
	{
		Con_Printf ("%s.%s: Particle has a ramp but no ramp mode\n", ptype->config, ptype->name);
	}
	r_plooksdirty = true;
}

#ifdef PSET_SCRIPT_EFFECTINFO
static void FinishEffectinfoParticleType (part_type_t *ptype, qboolean blooddecalonimpact)
{
	if (ptype->looks.type == PT_CDECAL)
	{
		if (ptype->die == 9999)
			ptype->die = 20;
		ptype->alphachange = -(ptype->alpha / ptype->die);
	}
	else if (ptype->looks.type == PT_UDECAL)
	{
		// dp's decals have a size as a radius. fte's udecals are 'just' quads.
		// also, dp uses 'stretch'.
		ptype->looks.stretch *= 1 / 1.414213562373095;
		ptype->scale *= ptype->looks.stretch;
		ptype->scalerand *= ptype->looks.stretch;
		ptype->scaledelta *= ptype->looks.stretch;
		ptype->looks.stretch = 1;
	}
	else if (ptype->looks.type == PT_NORMAL)
	{
		// fte's textured particles are *0.25 for some reason.
		// but fte also uses radiuses, while dp uses total size so we only need to double it here..
		ptype->scale *= 2 * ptype->looks.stretch;
		ptype->scalerand *= 2 * ptype->looks.stretch;
		ptype->scaledelta *= 2 * 2 * ptype->looks.stretch;
		ptype->looks.stretch = 1;
	}
	if (blooddecalonimpact) // DP blood particles generate decals unconditionally (and prevent blood from bouncing)
		ptype->clipbounce = -2;
	if (ptype->looks.type == PT_TEXTUREDSPARK)
	{
		ptype->looks.stretch *= 0.04;
		if (ptype->looks.stretch < 0)
			ptype->looks.stretch = 0.000001;
	}

	if (ptype->die == 9999) // internal: means unspecified.
	{
		if (ptype->alphachange)
			ptype->die = (ptype->alpha + ptype->alpharand) / -ptype->alphachange;
		else
			ptype->die = 15;
	}
	ptype->looks.minstretch = 0.5;
	FinishParticleType (ptype);
}
static void P_ImportEffectInfo (const char *config, char *line, qboolean part_parseweak)
{
	part_type_t *ptype = NULL;
	int			 parenttype;
	char		 arg[8][1024];
	unsigned int args = 0;
	qboolean	 blooddecalonimpact = false; // tracked separately because it needs to override another field

	float teximages[256][4];

	{
		int			i;
		char	   *file;
		const char *font_line;
		char		linebuf[1024];
		// default assumes 8*8 grid, but we allow more
		for (i = 0; i < 256; i++)
		{
			teximages[i][0] = 1 / 8.0 * (i & 7);
			teximages[i][1] = 1 / 8.0 * (1 + (i & 7));
			teximages[i][2] = 1 / 8.0 * (1 + (i >> 3));
			teximages[i][3] = 1 / 8.0 * (i >> 3);
		}

		file = (char *)COM_LoadFile ("particles/particlefont.txt", NULL);
		if (file)
		{
			size_t offset = 0;
			while (PScript_ReadLine (linebuf, sizeof (linebuf), file, com_filesize, &offset))
			{
				float s1, s2, t1, t2;
				font_line = COM_Parse (linebuf);
				i = atoi (com_token);
				font_line = COM_Parse (font_line);
				s1 = atof (com_token);
				font_line = COM_Parse (font_line);
				t1 = atof (com_token);
				font_line = COM_Parse (font_line);
				s2 = atof (com_token);
				font_line = COM_Parse (font_line);
				t2 = atof (com_token);
				if (font_line)
				{
					teximages[i][0] = s1;
					teximages[i][1] = s2;
					teximages[i][2] = t2;
					teximages[i][3] = t1;
				}
			}
			Mem_Free (file);
		}
	}

	for (; line && *line;)
	{
		char *eol;

		// multi-line comments need special handling.
		while (*line == ' ' || *line == '\t')
			line++;
		if (line[0] == '/' && line[1] == '*')
		{
			line += 2;
			while (*line)
			{
				if (line[0] == '*' && line[1] == '/')
				{
					line += 2;
					break;
				}
				line++;
			}
			continue;
		}

		eol = strchr (line, '\n');
		args = 0;
		if (eol)
			*eol++ = 0;
		for (args = 0; line;)
		{
			line = (char *)COM_Parse (line);
			if (line && args < sizeof (arg) / sizeof (arg[args]))
			{
				q_strlcpy (arg[args], com_token, sizeof (arg[args]));
				args++;
			}
		}
		line = eol;

		if (args <= 0)
			continue;

		if (!strcmp (arg[0], "effect"))
		{
			char newname[64];
			int	 i;

			if (ptype)
				FinishEffectinfoParticleType (ptype, blooddecalonimpact);
			blooddecalonimpact = false;

			ptype = P_GetParticleType (config, arg[1]);
			if (ptype->loaded)
			{
				for (i = 0; i < 64; i++)
				{
					parenttype = ptype - part_type;
					q_snprintf (newname, sizeof (newname), "%i+%s", i, arg[1]);
					ptype = P_GetParticleType (config, newname);
					if (!ptype->loaded)
					{
						part_type[parenttype].assoc = ptype - part_type;
						break;
					}
				}
				if (i == 64)
				{
					Con_Printf ("Too many duplicate names, gave up\n");
					break;
				}
			}
			P_ResetToDefaults (ptype);
			ptype->loaded = part_parseweak ? 1 : 2;
			ptype->scale = 1;
			ptype->alpha = 0;
			ptype->alpharand = 1;
			ptype->alphachange = -1;
			ptype->die = 9999;
			strcpy (ptype->texname, "particles/particlefont");
			ptype->rgb[0] = 1;
			ptype->rgb[1] = 1;
			ptype->rgb[2] = 1;

			//			ptype->spawnmode = SM_BALL;

			ptype->colorindex = -1;
			ptype->spawnchance = 1;
			ptype->looks.scalefactor = 2;
			ptype->looks.invscalefactor = 0;
			ptype->looks.type = PT_NORMAL;
			ptype->looks.blendmode = BM_PREMUL;
			ptype->looks.premul = 1;
			ptype->looks.stretch = 1;

			ptype->dl_time = 0;

			i = 63; // default texture is 63.
			ptype->s1 = teximages[i][0];
			ptype->s2 = teximages[i][1];
			ptype->t1 = teximages[i][2];
			ptype->t2 = teximages[i][3];
			ptype->texsstride = 0;
			ptype->randsmax = 1;
		}
		else if (!ptype)
		{
			Con_Printf ("Bad effectinfo file\n");
			break;
		}
		else if (!strcmp (arg[0], "countabsolute") && args == 2)
			ptype->countextra = atof (arg[1]);
		else if (!strcmp (arg[0], "count") && args == 2)
			ptype->count = atof (arg[1]);
		else if (!strcmp (arg[0], "type") && args == 2)
		{
			if (!strcmp (arg[1], "decal") || !strcmp (arg[1], "cdecal"))
			{
				ptype->looks.type = PT_CDECAL;
				ptype->looks.blendmode = BM_INVMODC;
				ptype->looks.premul = 2;
			}
			else if (!strcmp (arg[1], "udecal"))
			{
				ptype->looks.type = PT_UDECAL;
				ptype->looks.blendmode = BM_INVMODC;
				ptype->looks.premul = 2;
			}
			else if (!strcmp (arg[1], "alphastatic"))
			{
				ptype->looks.type = PT_NORMAL;
				ptype->looks.blendmode = BM_PREMUL; // BM_BLEND;
				ptype->looks.premul = 1;
			}
			else if (!strcmp (arg[1], "static"))
			{
				ptype->looks.type = PT_NORMAL;
				ptype->looks.blendmode = BM_PREMUL; // BM_ADDA;
				ptype->looks.premul = 2;
			}
			else if (!strcmp (arg[1], "smoke"))
			{
				ptype->looks.type = PT_NORMAL;
				ptype->looks.blendmode = BM_PREMUL; // BM_ADDA;
				ptype->looks.premul = 2;
			}
			else if (!strcmp (arg[1], "spark"))
			{
				ptype->looks.type = PT_TEXTUREDSPARK;
				ptype->looks.blendmode = BM_PREMUL; // BM_ADDA;
				ptype->looks.premul = 2;
			}
			else if (!strcmp (arg[1], "bubble"))
			{
				ptype->looks.type = PT_NORMAL;
				ptype->looks.blendmode = BM_PREMUL; // BM_ADDA;
				ptype->looks.premul = 2;
			}
			else if (!strcmp (arg[1], "blood"))
			{
				ptype->looks.type = PT_NORMAL;
				ptype->looks.blendmode = BM_INVMODC;
				ptype->looks.premul = 2;
				ptype->gravity = 800 * 1;
				blooddecalonimpact = true;
			}
			else if (!strcmp (arg[1], "beam"))
			{
				ptype->looks.type = PT_BEAM;
				ptype->looks.blendmode = BM_PREMUL; // BM_ADDA;
				ptype->looks.premul = 2;
			}
			else if (!strcmp (arg[1], "snow"))
			{
				ptype->looks.type = PT_NORMAL;
				ptype->looks.blendmode = BM_PREMUL; // BM_ADDA;
				ptype->looks.premul = 2;
				ptype->flurry = 32; // may not still be valid later, but at least it would be an obvious issue with the original.
			}
			else
			{
				Con_Printf ("effectinfo type %s not supported\n", arg[1]);
			}
		}
		else if (!strcmp (arg[0], "tex") && args == 3)
		{
			int mini = atoi (arg[1]);
			int maxi = atoi (arg[2]);
			ptype->s1 = teximages[mini][0];
			ptype->s2 = teximages[mini][1];
			ptype->t1 = teximages[mini][2];
			ptype->t2 = teximages[mini][3];
			ptype->texsstride = teximages[(mini + 1) & (sizeof (teximages) / sizeof (teximages[0]) - 1)][0] - teximages[mini][0];
			ptype->randsmax = (maxi - mini);
			if (ptype->randsmax < 1)
				ptype->randsmax = 1;
		}
		else if (!strcmp (arg[0], "size") && args == 3)
		{
			float s1 = atof (arg[1]), s2 = atof (arg[2]);
			ptype->scale = s1;
			ptype->scalerand = (s2 - s1);
		}
		else if (!strcmp (arg[0], "sizeincrease") && args == 2)
			ptype->scaledelta = atof (arg[1]);
		else if (!strcmp (arg[0], "color") && args == 3)
		{
			unsigned int rgb1 = strtoul (arg[1], NULL, 0), rgb2 = strtoul (arg[2], NULL, 0);
			int			 i;
			for (i = 0; i < 3; i++)
			{
				ptype->rgb[i] = ((rgb1 >> (16 - i * 8)) & 0xff) / 255.0;
				ptype->rgbrand[i] = (int)(((rgb2 >> (16 - i * 8)) & 0xff) - ((rgb1 >> (16 - i * 8)) & 0xff)) / 255.0;
				ptype->rgbrandsync[i] = 1;
			}
		}
		else if (!strcmp (arg[0], "alpha") && args == 4)
		{
			float a1 = atof (arg[1]), a2 = atof (arg[2]), f = atof (arg[3]);
			if (a1 > a2)
			{ // backwards
				ptype->alpha = a2 / 256;
				ptype->alpharand = (a1 - a2) / 256;
			}
			else
			{
				ptype->alpha = a1 / 256;
				ptype->alpharand = (a2 - a1) / 256;
			}
			ptype->alphachange = -f / 256;
		}
		else if (!strcmp (arg[0], "velocityoffset") && args == 4)
		{ /*a 3d world-coord addition*/
			ptype->velbias[0] = atof (arg[1]);
			ptype->velbias[1] = atof (arg[2]);
			ptype->velbias[2] = atof (arg[3]);
		}
		else if (!strcmp (arg[0], "velocityjitter") && args == 4)
		{
			ptype->velwrand[0] = atof (arg[1]);
			ptype->velwrand[1] = atof (arg[2]);
			ptype->velwrand[2] = atof (arg[3]);
		}
		else if (!strcmp (arg[0], "originoffset") && args == 4)
		{ /*a 3d world-coord addition*/
			ptype->orgbias[0] = atof (arg[1]);
			ptype->orgbias[1] = atof (arg[2]);
			ptype->orgbias[2] = atof (arg[3]);
		}
		else if (!strcmp (arg[0], "originjitter") && args == 4)
		{
			ptype->orgwrand[0] = atof (arg[1]);
			ptype->orgwrand[1] = atof (arg[2]);
			ptype->orgwrand[2] = atof (arg[3]);
		}
		else if (!strcmp (arg[0], "gravity") && args == 2)
		{
			ptype->gravity = 800 * atof (arg[1]);
		}
		else if (!strcmp (arg[0], "bounce") && args == 2)
		{
			ptype->clipbounce = atof (arg[1]);
			if (ptype->clipbounce < 0)
				ptype->cliptype = ptype - part_type;
		}
		else if (!strcmp (arg[0], "airfriction") && args == 2)
			ptype->friction[2] = ptype->friction[1] = ptype->friction[0] = atof (arg[1]);
		else if (!strcmp (arg[0], "liquidfriction") && args == 2)
			;
		else if (!strcmp (arg[0], "underwater") && args == 1)
			ptype->flags |= PT_TRUNDERWATER;
		else if (!strcmp (arg[0], "notunderwater") && args == 1)
			ptype->flags |= PT_TROVERWATER;
		else if (!strcmp (arg[0], "velocitymultiplier") && args == 2)
			ptype->veladd = atof (arg[1]);
		else if (!strcmp (arg[0], "trailspacing") && args == 2)
		{
			ptype->countspacing = atof (arg[1]);
			ptype->count = 1 / ptype->countspacing;
		}
		else if (!strcmp (arg[0], "time") && args == 3)
		{
			ptype->die = atof (arg[1]);
			ptype->randdie = atof (arg[2]) - ptype->die;
			if (ptype->randdie < 0)
			{
				ptype->die = atof (arg[2]);
				ptype->randdie = atof (arg[1]) - ptype->die;
			}
		}
		else if (!strcmp (arg[0], "stretchfactor") && args == 2)
			ptype->looks.stretch = atof (arg[1]);
		else if (!strcmp (arg[0], "blend") && args == 2)
		{
			if (!strcmp (arg[1], "invmod"))
			{
				ptype->looks.blendmode = BM_INVMODC;
				ptype->looks.premul = 2;
			}
			else if (!strcmp (arg[1], "alpha"))
			{
				ptype->looks.blendmode = BM_PREMUL;
				ptype->looks.premul = 1;
			}
			else if (!strcmp (arg[1], "add"))
			{
				ptype->looks.blendmode = BM_PREMUL;
				ptype->looks.premul = 2;
			}
			else
				Con_Printf ("effectinfo 'blend %s' not supported\n", arg[1]);
		}
		else if (!strcmp (arg[0], "orientation") && args == 2)
		{
			if (!strcmp (arg[1], "billboard"))
				ptype->looks.type = PT_NORMAL;
			else if (!strcmp (arg[1], "spark"))
				ptype->looks.type = PT_TEXTUREDSPARK;
			else if (!strcmp (arg[1], "oriented")) // FIXME: not sure this points the right way. also, its double-sided in dp.
			{
				if (ptype->looks.type != PT_CDECAL)
					ptype->looks.type = PT_UDECAL;
			}
			else if (!strcmp (arg[1], "beam"))
				ptype->looks.type = PT_BEAM;
			else
				Con_Printf ("effectinfo 'orientation %s' not supported\n", arg[1]);
		}
		else if (!strcmp (arg[0], "lightradius") && args == 2)
		{
			ptype->dl_radius[0] = atof (arg[1]);
			ptype->dl_radius[1] = 0;
		}
		else if (!strcmp (arg[0], "lightradiusfade") && args == 2)
			ptype->dl_decay[3] = atof (arg[1]);
		else if (!strcmp (arg[0], "lightcolor") && args == 4)
		{
			ptype->dl_rgb[0] = atof (arg[1]);
			ptype->dl_rgb[1] = atof (arg[2]);
			ptype->dl_rgb[2] = atof (arg[3]);
		}
		else if (!strcmp (arg[0], "lighttime") && args == 2)
			ptype->dl_time = atof (arg[1]);
		else if (!strcmp (arg[0], "lightshadow") && args == 2)
			ptype->flags = (ptype->flags & ~PT_NODLSHADOW) | (!atoi (arg[1]) ? PT_NODLSHADOW : 0);
		else if (!strcmp (arg[0], "lightcubemapnum") && args == 2)
			ptype->dl_cubemapnum = atoi (arg[1]);
		else if (!strcmp (arg[0], "lightcorona") && args == 3)
		{
			ptype->dl_corona_intensity = atof (arg[1]) * 0.25; // dp scales them by 0.25
			ptype->dl_corona_scale = atof (arg[2]);
		}
#if 1
		else if (!strcmp (arg[0], "staincolor") && args == 3) // stainmaps multiplier
			Con_DPrintf2 ("Particle effect token %s not supported\n", arg[0]);
		else if (!strcmp (arg[0], "stainalpha") && args == 3) // affects stainmaps AND stain-decals.
			Con_DPrintf2 ("Particle effect token %s not supported\n", arg[0]);
		else if (!strcmp (arg[0], "stainsize") && args == 3) // affects stainmaps AND stain-decals.
			Con_DPrintf2 ("Particle effect token %s not supported\n", arg[0]);
		else if (!strcmp (arg[0], "staintex") && args == 3) // actually spawns a decal
			Con_DPrintf2 ("Particle effect token %s not supported\n", arg[0]);
		else if (!strcmp (arg[0], "stainless") && args == 2)
			Con_DPrintf2 ("Particle effect token %s not supported\n", arg[0]);
#endif
		else if (!strcmp (arg[0], "rotate") && args == 5)
		{
			ptype->rotationstartmin = atof (arg[1]);
			ptype->rotationstartrand = atof (arg[2]) - ptype->rotationstartmin;
			ptype->rotationmin = atof (arg[3]);
			ptype->rotationrand = atof (arg[4]) - ptype->rotationmin;
			ptype->rotationstartmin *= M_PI / 180;
			ptype->rotationstartrand *= M_PI / 180;
			ptype->rotationmin *= M_PI / 180;
			ptype->rotationrand *= M_PI / 180;
			ptype->rotationstartmin += M_PI / 4;
		}
		else
			Con_Printf (
				"Particle effect token not recognised, or invalid args: %s %s %s %s %s %s\n", arg[0], args < 2 ? "" : arg[1], args < 3 ? "" : arg[2],
				args < 4 ? "" : arg[3], args < 5 ? "" : arg[4], args < 6 ? "" : arg[5]);
		args = 0;
	}

	if (ptype)
		FinishEffectinfoParticleType (ptype, blooddecalonimpact);

	r_plooksdirty = true;
}

static qboolean P_ImportEffectInfo_Name (char *config)
{
	char *file;

	file = (char *)COM_LoadFile (va ("%s.txt", config), NULL);
	if (!file)
	{
		Con_Printf ("%s.txt not found\n", config);
		return false;
	}
	P_ImportEffectInfo (config, file, false);
	Mem_Free (file);
	return true;
}
#endif

/*
===============
R_InitParticles
===============
*/
void PScript_InitParticles (void)
{
	Cvar_RegisterVariable (&r_fteparticles); // johnfitz
	Cvar_RegisterVariable (&r_bouncysparks);
	Cvar_RegisterVariable (&r_part_rain);
	Cvar_RegisterVariable (&r_decal_noperpendicular);
	Cvar_RegisterVariable (&r_particledesc);
	Cvar_RegisterVariable (&r_part_rain_quantity);
	Cvar_RegisterVariable (&r_particle_tracelimit);
	Cvar_RegisterVariable (&r_part_sparks);
	Cvar_RegisterVariable (&r_part_sparks_trifan);
	Cvar_RegisterVariable (&r_part_sparks_textured);
	Cvar_RegisterVariable (&r_part_beams);
	Cvar_RegisterVariable (&r_part_contentswitch);
	Cvar_RegisterVariable (&r_part_density);
	Cvar_RegisterVariable (&r_part_maxparticles);
	Cvar_RegisterVariable (&r_part_maxdecals);
	Cvar_RegisterVariable (&r_lightflicker);

	Cmd_AddCommand ("r_partredirect", P_PartRedirect_f);

	// #if _DEBUG
	Cmd_AddCommand ("r_partinfo", P_PartInfo_f);
	Cmd_AddCommand ("r_beaminfo", P_BeamInfo_f);
	// #endif
}

void PScript_ClearSurfaceParticles (qmodel_t *mod)
{
	mod->skytime = 0;
	mod->skytris = NULL;
	while (mod->skytrimem)
	{
		void *f = mod->skytrimem;
		mod->skytrimem = mod->skytrimem->next;
		Mem_Free (f);
	}
}
static void PScript_ClearAllSurfaceParticles (void)
{ // make sure we hit all models, even ones from the previous map. maybe this is overkill
	extern qmodel_t mod_known[];
	extern int		mod_numknown;
	int				i;
	for (i = 0; i < mod_numknown; i++)
		PScript_ClearSurfaceParticles (&mod_known[i]);
}

void PScript_Shutdown (void)
{
	Cvar_SetCallback (&r_particledesc, NULL);

	CL_ClearTrailStates ();

	pe_default = P_INVALID;
	pe_size2 = P_INVALID;
	pe_size3 = P_INVALID;
	pe_defaulttrail = P_INVALID;

	while (loadedconfigs)
	{
		pcfg_t *cfg;
		cfg = loadedconfigs;
		loadedconfigs = cfg->next;
		Mem_Free (cfg);
	}

	while (numparticletypes > 0)
	{
		numparticletypes--;
		if (part_type[numparticletypes].sounds)
			Mem_Free (part_type[numparticletypes].sounds);
		if (part_type[numparticletypes].ramp)
			Mem_Free (part_type[numparticletypes].ramp);
	}
	Mem_Free (part_type);
	part_type = NULL;
	part_run_list = NULL;

	Mem_Free (particles);
	particles = NULL;
	Mem_Free (beams);
	beams = NULL;
	Mem_Free (decals);
	decals = NULL;
	Mem_Free (trailstates);
	trailstates = NULL;

	free_particles = NULL;
	free_decals = NULL;
	free_beams = NULL;

	PScript_ClearAllSurfaceParticles ();

	r_numparticles = 0;
	r_numdecals = 0;
}

qboolean PScript_Startup (void)
{
	int newmaxp, newmaxd;

	newmaxp = r_part_maxparticles.value;
	if (newmaxp < 1)
		newmaxp = 1;
	if (newmaxp > MAX_PARTICLES)
		newmaxp = MAX_PARTICLES;
	newmaxd = r_part_maxdecals.value;
	if (newmaxd < 1)
		newmaxd = 1;
	if (newmaxd > MAX_DECALS)
		newmaxd = MAX_DECALS;

	if (!r_numparticles) // already inited
	{
		r_numparticles = newmaxp;
		r_numdecals = newmaxd;

		buildsintable ();

		r_numbeams = MAX_BEAMSEGS;
		r_numtrailstates = MAX_TRAILSTATES;

		particles = (particle_t *)Mem_Alloc (r_numparticles * sizeof (particle_t));

		beams = (beamseg_t *)Mem_Alloc (r_numbeams * sizeof (beamseg_t));

		decals = (clippeddecal_t *)Mem_Alloc (r_numdecals * sizeof (clippeddecal_t));

		trailstates = (trailstate_t *)Mem_Alloc (r_numtrailstates * sizeof (trailstate_t));
		memset (trailstates, 0, r_numtrailstates * sizeof (trailstate_t));
		ts_cycle = 0;

		Cvar_SetCallback (&r_particledesc, R_ParticleDesc_Callback);
	}
	r_particledesc.callback (&r_particledesc);

	return true;
}

void PScript_RecalculateSkyTris (void)
{
	qmodel_t *m = cl.worldmodel;
	size_t	  modidx;

	PScript_ClearAllSurfaceParticles ();

	for (modidx = 0; modidx < MAX_MODELS; modidx++)
	{
		m = cl.model_precache[modidx];

		if (m && !m->needload && m->type == mod_brush)
		{
			int			t;
			int			i;
			int			ptype;
			msurface_t *surf;
			char		key[128];
			const char *data = COM_Parse (m->entities);
			int		   *remaps;
			remaps = Mem_Alloc (sizeof (*remaps) * m->numtextures);
			if (!remaps)
				break;
			for (t = 0; t < m->numtextures; t++)
				remaps[t] = P_INVALID;

			// parse the worldspawn entity fields for "_texpart_FOO" keys to give texture "FOO" particles from the effect specified by the value
			if (data && com_token[0] == '{')
			{
				while (1)
				{
					data = COM_Parse (data);
					if (!data)
						break; // error
					if (com_token[0] == '}')
						break; // end of worldspawn
					if (com_token[0] == '_')
						strcpy (key, com_token + 1);
					else
						strcpy (key, com_token);
					while (key[strlen (key) - 1] == ' ') // remove trailing spaces
						key[strlen (key) - 1] = 0;
					data = COM_Parse (data);
					if (!data)
						break; // error
					if (!q_strncasecmp ("texpart_", key, 8))
					{
						/*in quakespasm there are always two textures added on the end (rather than pointing to textures outside the model)*/
						for (t = 0; t < m->numtextures - 2; t++)
						{
							if (!m->textures[t])
								continue;
							if (!q_strcasecmp (key + 8, m->textures[t]->name))
								remaps[t] = PScript_FindParticleType (com_token);
						}
					}
				}
			}

			for (t = 0; t < m->numtextures; t++)
			{
				ptype = remaps[t];
				if (ptype == P_INVALID && m->textures[t])
					ptype = PScript_FindParticleType (va ("tex_%s", m->textures[t]->name));

				if (ptype >= 0)
				{
					for (i = 0; i < m->nummodelsurfaces; i++)
					{
						surf = m->surfaces + i + m->firstmodelsurface;
						if (surf->texinfo->texture == m->textures[t])
						{
							/*FIXME: it would be a good idea to determine the surface's (midpoint) pvs cluster so that we're not spamming for the entire map*/
							PScript_EmitSkyEffectTris (m, surf, ptype);
						}
					}
				}
			}
			Mem_Free (remaps);
		}
	}
}
/*
===============
P_ClearParticles
===============
*/
void PScript_ClearParticles (qboolean load)
{
	int i;

	if (load)
		PScript_Startup ();

	free_particles = &particles[0];
	for (i = 0; i < r_numparticles; i++)
		particles[i].next = &particles[i + 1];
	particles[r_numparticles - 1].next = NULL;

	free_decals = &decals[0];
	for (i = 0; i < r_numdecals; i++)
		decals[i].next = &decals[i + 1];
	decals[r_numdecals - 1].next = NULL;

	free_beams = &beams[0];
	for (i = 0; i < r_numbeams; i++)
	{
		beams[i].p = NULL;
		beams[i].flags = BS_DEAD;
		beams[i].next = &beams[i + 1];
	}
	beams[r_numbeams - 1].next = NULL;

	particletime = cl.time;

	if (load)
		for (i = 0; i < numparticletypes; i++)
			P_LoadTexture (&part_type[i], false);

	for (i = 0; i < numparticletypes; i++)
	{
		part_type[i].clippeddecals = NULL;
		part_type[i].particles = NULL;
		part_type[i].beams = NULL;
	}

	PScript_ClearAllSurfaceParticles ();
	r_plooksdirty = load;

	CL_ClearTrailStates ();
}

static qboolean P_LoadParticleSet (char *name, qboolean implicit, qboolean showwarning)
{
	char   *file;
	pcfg_t *cfg;

	if (!*name)
		return false;

	// protect against configs being loaded multiple times. this can easily happen with namespaces (especially if an effect is missing).
	for (cfg = loadedconfigs; cfg; cfg = cfg->next)
	{
		// already loaded?
		if (!strcmp (cfg->name, name))
			return false;
	}
	cfg = Mem_Alloc (sizeof (*cfg) + strlen (name));
	if (!cfg)
		return false;
	strcpy (cfg->name, name);
	cfg->next = loadedconfigs;
	loadedconfigs = cfg;

	if (!strcmp (name, "classic"))
	{
#ifdef PSET_CLASSIC
		if (fallback)
			fallback->ShutdownParticles ();
		fallback = &pe_classic;
		if (fallback)
		{
			fallback->InitParticles ();
			fallback->ClearParticles ();
		}
#endif
		return true;
	}

	file = (char *)COM_LoadFile (va ("particles/%s.cfg", name), NULL);
	if (!file)
		file = (char *)COM_LoadFile (va ("%s.cfg", name), NULL);
	if (file)
	{
		PScript_ParseParticleEffectFile (name, implicit, file, com_filesize);
		Mem_Free (file);
	}
	else
	{
#ifdef PSET_SCRIPT_EFFECTINFO
		if (!strcmp (name, "effectinfo") || !strncmp (name, "effectinfo_", 11))
		{
			// FIXME: we're loading this too early to deal with per-map stuff.
			// FIXME: wait until after particle precache info has been received, and only reload if the loaded configs actually changed.
			P_ImportEffectInfo_Name (name);
			return true;
		}
#endif
		if (showwarning)
			Con_Printf (CON_WARNING "Couldn't find particle description %s\n", name);
		return false;
	}
	return true;
}

static void R_Particles_KillAllEffects (void)
{
	int		i;
	pcfg_t *cfg;

	for (i = 0; i < numparticletypes; i++)
	{
		*part_type[i].texname = '\0';
		part_type[i].scale = 0;
		part_type[i].loaded = 0;
		if (part_type->ramp)
			Mem_Free (part_type->ramp);
		part_type->ramp = NULL;
		part_type->rampmode = RAMP_NONE;
	}

	while (loadedconfigs)
	{
		cfg = loadedconfigs;
		loadedconfigs = cfg->next;
		Mem_Free (cfg);
	}
}

static void R_ParticleDesc_Callback (struct cvar_s *var)
{
	const char *c;

	R_Particles_KillAllEffects ();
	r_plooksdirty = true;

	for (c = var->string; (c = COM_Parse (c));)
	{
		if (*com_token)
			P_LoadParticleSet (com_token, false, true);
	}

	if (cls.state == ca_connected && cl.model_precache[1])
	{
		// per-map configs. because we can.
		memcpy (com_token, "map_", 4);
		COM_FileBase (cl.model_precache[1]->name, com_token + 4, sizeof (com_token) - 4);
		P_LoadParticleSet (com_token, false, false);
	}

	// make sure nothing is stale.
	CL_RegisterParticles ();
}

static void P_AddRainParticles (qmodel_t *mod, vec3_t axis[3], vec3_t eorg, float contribution)
{
	float		 x;
	float		 y;
	part_type_t *type;

	vec3_t org, vdist, worg, wnorm;

	skytris_t *st;
	if (!r_part_rain_quantity.value)
		return;

	mod->skytime += contribution;

	for (st = mod->skytris; st; st = st->next)
	{
		if ((unsigned int)st->ptype >= (unsigned int)numparticletypes)
			continue;
		type = &part_type[st->ptype];
		if (!type->loaded) // woo, batch skipping.
			continue;

		while (st->nexttime < mod->skytime)
		{
			if (!free_particles)
				return;

			st->nexttime += 10000.0 / (st->area * r_part_rain_quantity.value * type->rainfrequency);

			x = frandom () * frandom ();
			y = frandom () * (1 - x);
			VectorMA (st->org, x, st->x, org);
			VectorMA (org, y, st->y, org);

			worg[0] = DotProduct (org, axis[0]) + eorg[0];
			worg[1] = -DotProduct (org, axis[1]) + eorg[1];
			worg[2] = DotProduct (org, axis[2]) + eorg[2];

			// ignore it if its too far away
			VectorSubtract (worg, r_refdef.vieworg, vdist);
			if (VectorLength (vdist) > (1024 + 512) * frandom ())
				continue;

			if (st->face->flags & SURF_PLANEBACK)
				VectorScale (st->face->plane->normal, -1, vdist);
			else
				VectorCopy (st->face->plane->normal, vdist);

			wnorm[0] = DotProduct (vdist, axis[0]);
			wnorm[1] = -DotProduct (vdist, axis[1]);
			wnorm[2] = DotProduct (vdist, axis[2]);

			VectorMA (worg, 0.5, wnorm, worg);
			if (!(CL_PointContentsMask (worg) & FTECONTENTS_SOLID)) // should be paranoia, at least for the world.
			{
				PScript_RunParticleEffectState (worg, wnorm, 1, st->ptype, NULL);
			}
		}
	}
}

static void R_Part_SkyTri (qmodel_t *mod, float *v1, float *v2, float *v3, msurface_t *surf, int ptype)
{
	float  dot;
	float  xm;
	float  ym;
	float  theta;
	vec3_t xd;
	vec3_t yd;

	skytris_t *st;

	skytriblock_t *mem = mod->skytrimem;
	if (!mem || mem->count == sizeof (mem->tris) / sizeof (mem->tris[0]))
	{
		mod->skytrimem = Mem_Alloc (sizeof (*mod->skytrimem));
		mod->skytrimem->next = mem;
		mod->skytrimem->count = 0;
		mem = mod->skytrimem;
	}

	st = &mem->tris[mem->count];
	VectorCopy (v1, st->org);
	VectorSubtract (v2, st->org, st->x);
	VectorSubtract (v3, st->org, st->y);

	VectorCopy (st->x, xd);
	VectorCopy (st->y, yd);

	xm = VectorLength (xd);
	ym = VectorLength (yd);

	dot = DotProduct (xd, yd);
	theta = acos (dot / (xm * ym));
	st->area = sin (theta) * xm * ym;
	st->nexttime = mod->skytime;
	st->face = surf;
	st->ptype = ptype;

	if (st->area <= 0)
		return; // bummer.
	mem->count++;

	st->next = mod->skytris;
	mod->skytris = st;
}

void PScript_EmitSkyEffectTris (qmodel_t *mod, msurface_t *fa, int ptype)
{
	vec3_t verts[64];
	int	   v1;
	int	   v2;
	int	   v3;
	int	   numverts;
	int	   i, lindex;
	float *vec;

	if (ptype < 0 || ptype >= numparticletypes)
		return;

	//
	// convert edges back to a normal polygon
	//
	numverts = 0;
	for (i = 0; i < fa->numedges; i++)
	{
		lindex = mod->surfedges[fa->firstedge + i];

		if (lindex > 0)
			vec = mod->vertexes[mod->edges[lindex].v[0]].position;
		else
			vec = mod->vertexes[mod->edges[-lindex].v[1]].position;
		VectorCopy (vec, verts[numverts]);
		numverts++;

		if (numverts >= 64)
		{
			Con_Printf ("Too many verts on sky surface\n");
			return;
		}
	}

	v1 = 0;
	v2 = 1;
	for (v3 = 2; v3 < numverts; v3++)
	{
		R_Part_SkyTri (mod, verts[v1], verts[v2], verts[v3], fa, ptype);

		v2 = v3;
	}
}

// Trailstate functions
static void P_CleanTrailstate (trailstate_t *ts)
{
	// clear LASTSEG flag from lastbeam so it can be reused
	if (ts->lastbeam)
	{
		ts->lastbeam->flags &= ~BS_LASTSEG;
		ts->lastbeam->flags |= BS_NODRAW;
	}

	// clean structure
	memset (ts, 0, sizeof (trailstate_t));
}

void PScript_DelinkTrailstate (trailstate_t **tsk)
{
	trailstate_t *ts;
	trailstate_t *assoc;

	if (*tsk == NULL)
		return; // not linked to a trailstate

	ts = *tsk;	 // store old pointer
	*tsk = NULL; // clear pointer

	if (ts->key != tsk)
		return; // prevent overwrite

	assoc = ts->assoc;		// store assoc
	P_CleanTrailstate (ts); // clean directly linked trailstate

	// clean trailstates assoc linked
	while (assoc)
	{
		ts = assoc->assoc;
		P_CleanTrailstate (assoc);
		assoc = ts;
	}
}

static trailstate_t *P_NewTrailstate (trailstate_t **key)
{
	trailstate_t *ts;

	// bounds check here in case r_numtrailstates changed
	if (ts_cycle >= r_numtrailstates)
		ts_cycle = 0;

	// get trailstate
	ts = trailstates + ts_cycle;

	// clear trailstate
	P_CleanTrailstate (ts);

	// set key
	ts->key = key;

	// advance index cycle
	ts_cycle++;

	// return clean trailstate
	return ts;
}

#define NUMVERTEXNORMALS 162
static float r_avertexnormals[NUMVERTEXNORMALS][3] = {
#include "anorms.h"
};
static vec2_t avelocities[NUMVERTEXNORMALS];
#define BEAMLENGTH 16

static void PScript_EffectSpawned (part_type_t *ptype, vec3_t org, vec3_t axis[3], int dlkey, float countscale)
{
	if (ptype->dl_radius[0] || ptype->dl_radius[1]) // && r_rocketlight.value)
	{
		float	  radius;
		dlight_t *dl;

		static int flickertime;
		static int flicker;
		int		   i = realtime * 20;
		if (flickertime != i)
		{
			flickertime = i;
			flicker = rand ();
		}
		radius = ptype->dl_radius[0] + (r_lightflicker.value ? ((flicker + dlkey * 2000) & 0xffff) * (1.0f / 0xffff) : 0.5) * ptype->dl_radius[1];

		dl = CL_AllocDlight (dlkey);
		VectorCopy (org, dl->origin);
		dl->radius = radius;
		dl->minlight = 0;
		dl->die = cl.time + ptype->dl_time;
		dl->decay = ptype->dl_decay[3];
		VectorCopy (ptype->dl_rgb, dl->color);
	}
	if (ptype->numsounds)
	{
		int	  i;
		float w, tw;
		for (i = 0, tw = 0; i < ptype->numsounds; i++)
			tw += ptype->sounds[i].weight;
		w = frandom () * tw; // select the sound by weight
		// and figure out which one that weight corresponds to
		for (i = 0, tw = 0; i < ptype->numsounds; i++)
		{
			tw += ptype->sounds[i].weight;
			if (w <= tw)
			{
				if (*ptype->sounds[i].name && ptype->sounds[i].vol > 0)
				{ // FIXME: no delay, no pitch
					S_StartSound (0, 0, S_PrecacheSound (ptype->sounds[i].name), org, ptype->sounds[i].vol, ptype->sounds[i].atten);
				}
				break;
			}
		}
	}
}

#ifdef USE_DECALS
typedef struct
{
	part_type_t *ptype;
	int			 entity;
	qmodel_t	*model;
	vec3_t		 center;
	vec3_t		 normal;
	vec3_t		 tangent1;
	vec3_t		 tangent2;

	float scale0;
	float scale1;
	float scale2;

	float bias1;
	float bias2;
} decalctx_t;
static void PScript_AddDecals (void *vctx, vec3_t *points, size_t numtris)
{
	decalctx_t	   *ctx = vctx;
	part_type_t	   *ptype = ctx->ptype;
	clippeddecal_t *d;
	unsigned int	i;
	vec3_t			vec;
	byte		   *palrgba = (byte *)d_8to24table;
	while (numtris-- > 0)
	{
		if (!free_decals)
			break;

		d = free_decals;
		free_decals = d->next;
		d->next = ptype->clippeddecals;
		ptype->clippeddecals = d;

		for (i = 0; i < 3; i++)
		{
			VectorCopy (points[i], d->vertex[i]);
			VectorSubtract (d->vertex[i], ctx->center, vec);
			d->texcoords[i][0] = (DotProduct (vec, ctx->tangent1) * ctx->scale1) + ctx->bias1;
			d->texcoords[i][1] = (DotProduct (vec, ctx->tangent2) * ctx->scale2) + ctx->bias2;
			if (r_decal_noperpendicular.value)
			{
				// the decal code is already making sure the surfaces are mostly aligned, which should solve some issues.
				// this means we can make sure that there's NO fading at all, so no issues if the center of the effect is not actually aligned with any surface
				// (yay inprecision).
				d->valpha[i] = 1;
			}
			else
			{
				// fade the alpha depending on the distance from the center)
				// FIXME: should be fabsed by glsl so that linear interpolation works correctly
				d->valpha[i] = 1 - fabs ((DotProduct (vec, ctx->normal) * ctx->scale0));
			}
		}
		points += 3;

		d->entity = ctx->entity;
		d->model = ctx->model;
		d->die = ptype->randdie * frandom ();

		if (ptype->die)
			d->rgba[3] = ptype->alpha + d->die * ptype->alphachange;
		else
			d->rgba[3] = ptype->alpha;
		d->rgba[3] += ptype->alpharand * frandom ();

		if (ptype->colorindex >= 0)
		{
			int cidx;
			cidx = ptype->colorrand > 0 ? rand () % ptype->colorrand : 0;
			cidx = ptype->colorindex + cidx;
			if (cidx > 255)
				d->rgba[3] = d->rgba[3] / 2; // Hexen 2 style transparency
			cidx = (cidx & 0xff) * 4;
			d->rgba[0] = palrgba[cidx] * (1 / 255.0);
			d->rgba[1] = palrgba[cidx + 1] * (1 / 255.0);
			d->rgba[2] = palrgba[cidx + 2] * (1 / 255.0);
		}
		else
			VectorCopy (ptype->rgb, d->rgba);

		vec[2] = frandom ();
		vec[0] = vec[2] * ptype->rgbrandsync[0] + frandom () * (1 - ptype->rgbrandsync[0]);
		vec[1] = vec[2] * ptype->rgbrandsync[1] + frandom () * (1 - ptype->rgbrandsync[1]);
		vec[2] = vec[2] * ptype->rgbrandsync[2] + frandom () * (1 - ptype->rgbrandsync[2]);
		d->rgba[0] += vec[0] * ptype->rgbrand[0] + ptype->rgbchange[0] * d->die;
		d->rgba[1] += vec[1] * ptype->rgbrand[1] + ptype->rgbchange[1] * d->die;
		d->rgba[2] += vec[2] * ptype->rgbrand[2] + ptype->rgbchange[2] * d->die;

		d->die = particletime + ptype->die - d->die;

		if (ptype->looks.type != PT_CDECAL)
			d->die += 20;

		// maintain run list
		if (!(ptype->state & PS_INRUNLIST))
		{
			ptype->nexttorun = part_run_list;
			part_run_list = ptype;
			ptype->state |= PS_INRUNLIST;
		}
	}
}

typedef struct fragmentdecal_s fragmentdecal_t;
static void					   Mod_ClipDecal (
					   qmodel_t *mod, vec3_t center, vec3_t normal, vec3_t tangent1, vec3_t tangent2, float size, unsigned int surfflagmask, unsigned int surfflagmatch,
					   void (*callback) (void *ctx, vec3_t *points, size_t numpoints), void *ctx);

// clipped decals actually work by defining the area of the decal with some planes, and then chopping away the entirety of the world based upon those planes
// (hurrah for bsp to trivially reject most of it) the decal is then textured according to some texture projection.
#define MAXFRAGMENTVERTS (128 * 3)
struct fragmentdecal_s
{
	vec3_t center;

	vec3_t normal;
	vec3_t planenorm[6];
	float  planedist[6];
	int	   numplanes;

	vec_t radius;

	// will only appear on surfaces with the matching surfaceflag
	unsigned int surfflagmask;
	unsigned int surfflagmatch;

	void (*callback) (void *ctx, vec3_t *points, size_t numpoints);
	void *ctx;
};
static int Fragment_ClipPolyToPlane (vec3_t *inverts, vec3_t *outverts, int incount, float *plane, float planedist)
{
	float dotv[MAXFRAGMENTVERTS + 1];
	char  keep[MAXFRAGMENTVERTS + 1];
#define KEEP_KILL	0
#define KEEP_KEEP	1
#define KEEP_BORDER 2
	int	   i;
	int	   outcount = 0;
	int	   clippedcount = 0;
	float  d;
	float *p1, *p2;
	float *out;
#define FRAG_EPSILON (1.0 / 32) // 0.5

	for (i = 0; i < incount; i++)
	{
		dotv[i] = DotProduct (inverts[i], plane) - planedist;
		if (dotv[i] < -FRAG_EPSILON)
		{
			keep[i] = KEEP_KILL;
			clippedcount++;
		}
		else if (dotv[i] > FRAG_EPSILON)
			keep[i] = KEEP_KEEP;
		else
			keep[i] = KEEP_BORDER;
	}
	dotv[i] = dotv[0];
	keep[i] = keep[0];

	if (clippedcount == incount)
		return 0; // all were clipped
	if (clippedcount == 0)
	{ // none were clipped
		for (i = 0; i < incount; i++)
			VectorCopy (inverts[i], outverts[i]);
		return incount;
	}

	for (i = 0; i < incount; i++)
	{
		p1 = inverts[i];
		if (keep[i] == KEEP_BORDER)
		{
			out = outverts[outcount++];
			VectorCopy (p1, out);
			continue;
		}
		if (keep[i] == KEEP_KEEP)
		{
			out = outverts[outcount++];
			VectorCopy (p1, out);
		}
		if (keep[i + 1] == KEEP_BORDER || keep[i] == keep[i + 1])
			continue;
		p2 = inverts[(i + 1) % incount];
		d = dotv[i] - dotv[i + 1];
		if (d)
			d = dotv[i] / d;

		out = outverts[outcount++];
		VectorInterpolate (p1, d, p2, out);
	}
	return outcount;
}
static void Fragment_ClipPoly (fragmentdecal_t *dec, int numverts, vec3_t *inverts)
{
	// emit the triangle, and clip it's fragments.
	int	   p;
	vec3_t verts[2][MAXFRAGMENTVERTS];
	vec3_t decalfragmentverts[MAXFRAGMENTVERTS];
	int	   flip;
	vec3_t d1, d2, n;
	size_t numtris;

	if (numverts > MAXFRAGMENTVERTS)
		return;

	if (r_decal_noperpendicular.value)
	{
		VectorSubtract (inverts[1], inverts[0], d1);
		for (p = 2;; p++)
		{
			if (p >= numverts)
				return;
			VectorSubtract (inverts[p], inverts[0], d2);
			CrossProduct (d1, d2, n);
			if (DotProduct (n, n) > .1)
				break;
		}
		VectorNormalizeFast (n);
		if (DotProduct (n, dec->normal) < 0.1)
			return; // faces too far way from the normal
	}

	flip = 0;
	// clip to the first plane specially, so we don't have extra copys
	numverts = Fragment_ClipPolyToPlane (inverts, verts[flip], numverts, dec->planenorm[0], dec->planedist[0]);

	if (numverts < 3) // totally clipped.
		return;

	// clip the polygon to the 6 planes.
	for (p = 1; p < dec->numplanes; p++)
	{
		numverts = Fragment_ClipPolyToPlane (verts[flip], verts[flip ^ 1], numverts, dec->planenorm[p], dec->planedist[p]);
		flip ^= 1;

		if (numverts < 3) // totally clipped.
			return;
	}

	// decompose the resulting polygon into triangles.

	numtris = 0;
	while (numverts-- > 2)
	{
		if (numtris + 3 > MAXFRAGMENTVERTS)
		{
			dec->callback (dec->ctx, decalfragmentverts, numtris);
			numtris = 0;
			break;
		}

		VectorCopy (verts[flip][0], decalfragmentverts[numtris * 3 + 0]);
		VectorCopy (verts[flip][numverts - 1], decalfragmentverts[numtris * 3 + 1]);
		VectorCopy (verts[flip][numverts], decalfragmentverts[numtris * 3 + 2]);
		numtris++;
	}
	if (numtris)
		dec->callback (dec->ctx, decalfragmentverts, numtris);
}
// this could be inlined, but I'm lazy.
static void Q1BSP_Fragment_Surface (fragmentdecal_t *dec, msurface_t *surf)
{
	int		  i;
	vec3_t	  verts[MAXFRAGMENTVERTS];
	glpoly_t *poly;
	float	 *poly_vert;

	// water and sky should not get decals.
	if (surf->flags & (SURF_DRAWSKY | SURF_DRAWTURB))
		return;

	for (poly = surf->polys; poly; poly = poly->next)
	{
		if (poly->numverts > MAXFRAGMENTVERTS)
			continue;

		for (i = 0; i < poly->numverts; i++)
		{
			poly_vert = &poly->verts[0][0] + (i * VERTEXSIZE);
			VectorCopy (poly_vert, verts[i]);
		}
		Fragment_ClipPoly (dec, i, verts);
	}
}
static void Q1BSP_ClipDecalToNodes (qmodel_t *mod, fragmentdecal_t *dec, mnode_t *node)
{
	mplane_t	*splitplane;
	float		 dist;
	msurface_t	*surf;
	unsigned int i;

	if (node->contents < 0)
		return;

	splitplane = node->plane;
	dist = DotProduct (dec->center, splitplane->normal) - splitplane->dist;

	if (dist > dec->radius)
	{
		Q1BSP_ClipDecalToNodes (mod, dec, node->children[0]);
		return;
	}
	if (dist < -dec->radius)
	{
		Q1BSP_ClipDecalToNodes (mod, dec, node->children[1]);
		return;
	}

	// mark the polygons
	surf = mod->surfaces + node->firstsurface;
	if (r_decal_noperpendicular.value)
	{
		for (i = 0; i < node->numsurfaces; i++, surf++)
		{
			if (surf->flags & SURF_PLANEBACK)
			{
				if (-DotProduct (surf->plane->normal, dec->normal) > -0.5)
					continue;
			}
			else
			{
				if (DotProduct (surf->plane->normal, dec->normal) > -0.5)
					continue;
			}
			Q1BSP_Fragment_Surface (dec, surf);
		}
	}
	else
	{
		for (i = 0; i < node->numsurfaces; i++, surf++)
			Q1BSP_Fragment_Surface (dec, surf);
	}

	Q1BSP_ClipDecalToNodes (mod, dec, node->children[0]);
	Q1BSP_ClipDecalToNodes (mod, dec, node->children[1]);
}

static void Mod_ClipDecal (
	qmodel_t *mod, vec3_t center, vec3_t normal, vec3_t tangent1, vec3_t tangent2, float size, unsigned int surfflagmask, unsigned int surfflagmatch,
	void (*callback) (void *ctx, vec3_t *points, size_t numpoints), void *ctx)
{ // quad marks a full, independant quad
	int				p;
	float			r;
	fragmentdecal_t dec;

	VectorCopy (center, dec.center);
	VectorCopy (normal, dec.normal);
	dec.radius = 0;
	dec.callback = callback;
	dec.ctx = ctx;
	dec.surfflagmask = surfflagmask;
	dec.surfflagmatch = surfflagmatch;

	VectorCopy (tangent1, dec.planenorm[0]);
	VectorScale (tangent1, -1, dec.planenorm[1]);
	VectorCopy (tangent2, dec.planenorm[2]);
	VectorScale (tangent2, -1, dec.planenorm[3]);
	VectorCopy (dec.normal, dec.planenorm[4]);
	VectorScale (dec.normal, -1, dec.planenorm[5]);
	for (p = 0; p < 6; p++)
	{
		r = sqrt (DotProduct (dec.planenorm[p], dec.planenorm[p]));
		VectorScale (dec.planenorm[p], 1 / r, dec.planenorm[p]);
		r *= size / 2;
		if (r > dec.radius)
			dec.radius = r;
		dec.planedist[p] = -(r - DotProduct (dec.center, dec.planenorm[p]));
	}
	dec.numplanes = 6;

	if (mod && !mod->needload && mod->type == mod_brush)
		Q1BSP_ClipDecalToNodes (mod, &dec, mod->nodes + mod->hulls[0].firstclipnode);
}
#endif

void PerpendicularVector (vec3_t dst, const vec3_t src);

int PScript_RunParticleEffectState (vec3_t org, vec3_t dir, float count, int typenum, trailstate_t **tsk)
{
	part_type_t *ptype = &part_type[typenum];
	int			 i, j, k, l, spawnspc;
	float		 m, pcount; //, orgadd, veladd;
	vec3_t		 axis[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, -1}};
	particle_t	*p;
	beamseg_t	*b, *bfirst;
	vec3_t		 ofsvec, arsvec; // offsetspread vec, areaspread vec
	vec3_t		 bestdir;

	float		  orgadd, veladd;
	trailstate_t *ts;
	byte		 *palrgba = (byte *)d_8to24table;

	if (typenum < 0 || typenum >= numparticletypes)
		return 1;

	if (!ptype->loaded)
		return 1;

	// inwater check, switch only once
	if (r_part_contentswitch.value && ptype->inwater >= 0 && cl.worldmodel)
	{
		unsigned int cont;
		cont = CL_PointContentsMask (org);

		if (cont & FTECONTENTS_FLUID)
			ptype = &part_type[ptype->inwater];
	}

	// eliminate trailstate if flag set
	if (ptype->flags & PT_NOSTATE)
		tsk = NULL;

	// trailstate allocation/deallocation
	if (tsk)
	{
		// if *tsk = NULL get a new one
		if (*tsk == NULL)
		{
			ts = P_NewTrailstate (tsk);
			*tsk = ts;
		}
		else
		{
			ts = *tsk;

			if (ts->key != tsk) // trailstate was overwritten
			{
				ts = P_NewTrailstate (tsk); // so get a new one
				*tsk = ts;
			}
		}
	}
	else
		ts = NULL;

	// get msvc to shut up
	j = k = l = 0;
	m = 0;

	while (ptype)
	{
		if (r_part_contentswitch.value && (ptype->flags & (PT_TRUNDERWATER | PT_TROVERWATER)) && cl.worldmodel)
		{
			unsigned int cont;
			cont = CL_PointContentsMask (org);

			if ((ptype->flags & PT_TROVERWATER) && (cont & ptype->fluidmask))
				goto skip;
			if ((ptype->flags & PT_TRUNDERWATER) && !(cont & ptype->fluidmask))
				goto skip;
		}

		if (dir && (dir[0] || dir[1] || dir[2]))
		{
			VectorCopy (dir, axis[2]);
			VectorNormalize (axis[2]);
			PerpendicularVector (axis[0], axis[2]);
			VectorNormalize (axis[0]);
			CrossProduct (axis[2], axis[0], axis[1]);
			VectorNormalize (axis[1]);
		}
		PScript_EffectSpawned (ptype, org, axis, 0, count);

		if (ptype->looks.type == PT_CDECAL)
		{
#ifdef USE_DECALS
			vec3_t	   vec = {0.5, 0.5, 0.5};
			int		   n;
			decalctx_t ctx;
			vec3_t	   start, end;

			if (!free_decals)
				return 0;

			ctx.entity = 0;

			VectorCopy (org, ctx.center);
			if (!dir || (dir[0] == 0 && dir[1] == 0 && dir[2] == 0))
			{
				float  bestfrac = 1;
				float  frac;
				vec3_t impact, normal;
				int	   what;
				bestdir[0] = 0;
				bestdir[1] = 0.73;
				bestdir[2] = 0.73;
				VectorNormalize (bestdir);
				for (n = 0; n < 6; n++)
				{
					if (n >= 3)
					{
						end[0] = (n == 3) * 16;
						end[1] = (n == 4) * 16;
						end[2] = (n == 5) * 16;
					}
					else
					{
						end[0] = -(n == 0) * 16;
						end[1] = -(n == 1) * 16;
						end[2] = -(n == 2) * 16;
					}
					VectorSubtract (org, end, start);
					VectorAdd (org, end, end);

					frac = CL_TraceLine (start, end, impact, normal, &what);
					if (bestfrac > frac)
					{
						bestfrac = frac;
						VectorCopy (normal, bestdir);
						VectorCopy (impact, ctx.center);
						ctx.entity = what;
					}
				}
				dir = bestdir;
			}
			else
			{ // try to get it exactly on the plane, otherwise network or collision inprecisions can leave us further away from the surface than the radius of
			  // the decal
				VectorSubtract (org, dir, start);
				VectorAdd (org, dir, end);
				CL_TraceLine (start, end, ctx.center, bestdir, &ctx.entity);
			}
			if (ctx.entity)
			{
				entity_t *ent = CL_EntityNum (ctx.entity);
				if (ent->model) // looks like its active.
				{
					ctx.model = ent->model;
					// FIXME: rotate normal
					VectorSubtract (ctx.center, ent->origin, ctx.center);
				}
				else
				{
					ctx.entity = 0;
					ctx.model = cl.worldmodel;
				}
			}
			else
			{
				ctx.entity = 0;
				ctx.model = cl.worldmodel;
			}
			if (!ctx.model)
				return 0;

			VectorScale (dir, -1, ctx.normal);
			VectorNormalize (ctx.normal);

			// we know the normal now. pick two random tangents.
			VectorNormalize (vec);
			CrossProduct (ctx.normal, vec, ctx.tangent1);
			RotatePointAroundVector (ctx.tangent2, ctx.normal, ctx.tangent1, frandom () * 360);
			CrossProduct (ctx.normal, ctx.tangent2, ctx.tangent1);

			VectorNormalize (ctx.tangent1);
			VectorNormalize (ctx.tangent2);

			ctx.ptype = ptype;
			ctx.scale1 = ptype->s2 - ptype->s1;
			ctx.bias1 = ptype->s1 + ctx.scale1 / 2;
			ctx.scale2 = ptype->t2 - ptype->t1;
			ctx.bias2 = ptype->t1 + ctx.scale2 / 2;
			m = ptype->scale + frandom () * ptype->scalerand;
			ctx.scale0 = 2.0 / m;
			ctx.scale1 /= m;
			ctx.scale2 /= m;

			if (ptype->randsmax != 1)
				ctx.bias1 += ptype->texsstride * (rand () % ptype->randsmax);

			// inserts decals through a callback.
			Mod_ClipDecal (
				ctx.model, ctx.center, ctx.normal, ctx.tangent2, ctx.tangent1, m, ptype->surfflagmask, ptype->surfflagmatch, PScript_AddDecals, &ctx);
#endif
			if (ptype->assoc < 0)
				break;
			ptype = &part_type[ptype->assoc];
			continue;
		}
		// init spawn specific variables
		b = bfirst = NULL;
		spawnspc = 8;
		pcount = ptype->countextra + r_part_density.value * count * (ptype->count + ptype->countrand * frandom ());
		if (ptype->flags & PT_INVFRAMETIME)
			pcount /= host_frametime;
		if (ts)
			pcount += ts->state2.emittime;

		switch (ptype->spawnmode)
		{
		case SM_UNICIRCLE:
			m = pcount;
			if (ptype->looks.type == PT_BEAM)
				m--;

			if (m < 1)
				m = 0;
			else
				m = (M_PI * 2) / m;

			if (ptype->spawnparam1) /* use for weird shape hacks */
				m *= ptype->spawnparam1;
			break;
		case SM_TELEBOX:
			spawnspc = 4;
			l = -ptype->areaspreadvert;
		case SM_LAVASPLASH:
			j = k = -ptype->areaspread;
			if (ptype->spawnparam1)
				m = ptype->spawnparam1;
			else
				m = 0.55752; /* default weird number for tele/lavasplash used in vanilla Q1 */

			if (ptype->spawnparam2)
				spawnspc = (int)ptype->spawnparam2;
			break;
		case SM_FIELD:
			if (!avelocities[0][0])
			{
				for (j = 0; j < NUMVERTEXNORMALS; j++)
				{
					avelocities[j][0] = (rand () & 255) * 0.01;
					avelocities[j][1] = (rand () & 255) * 0.01;
				}
			}

			j = 0;
			m = 0;
			break;
		default: // others don't need intitialisation
			break;
		}

		// time limit (for completeness)
		if (ptype->spawntime && ts)
		{
			if (ts->state1.statetime > particletime)
				return 0; // timelimit still in effect

			ts->state1.statetime = particletime + ptype->spawntime; // record old time
		}

		// random chance for point effects
		if (ptype->spawnchance < frandom ())
		{
			i = ceil (pcount);
			break;
		}

		/*this is a hack, use countextra=1, count=0*/
		if (!ptype->die && ptype->count == 1 && ptype->countrand == 0 && pcount < 1)
			pcount = 1;

		// particle spawning loop
		for (i = 0; i < pcount; i++)
		{
			if (!free_particles)
				break;
			p = free_particles;
			if (ptype->looks.type == PT_BEAM)
			{
				if (!free_beams)
					break;
				if (b)
				{
					b = b->next = free_beams;
					free_beams = free_beams->next;
				}
				else
				{
					b = bfirst = free_beams;
					free_beams = free_beams->next;
				}
				b->texture_s = i; // TODO: FIX THIS NUMBER
				b->flags = 0;
				b->p = p;
				VectorClear (b->dir);
			}
			free_particles = p->next;
			p->next = ptype->particles;
			ptype->particles = p;

			p->die = ptype->randdie * frandom ();
			p->scale = ptype->scale + ptype->scalerand * frandom ();
			if (ptype->die)
				p->rgba[3] = ptype->alpha + p->die * ptype->alphachange;
			else
				p->rgba[3] = ptype->alpha;
			p->rgba[3] += ptype->alpharand * frandom ();
			// p->color = 0;
			if (ptype->emittime < 0)
				p->state.trailstate = NULL;
			else
				p->state.nextemit = particletime + ptype->emitstart - p->die;

			p->rotationspeed = ptype->rotationmin + frandom () * ptype->rotationrand;
			p->angle = ptype->rotationstartmin + frandom () * ptype->rotationstartrand;
			p->s1 = ptype->s1;
			p->t1 = ptype->t1;
			p->s2 = ptype->s2;
			p->t2 = ptype->t2;
			if (ptype->randsmax != 1)
			{
				m = ptype->texsstride * (rand () % ptype->randsmax);
				p->s1 += m;
				p->s2 += m;
			}

			if (ptype->colorindex >= 0)
			{
				int cidx;
				cidx = ptype->colorrand > 0 ? rand () % ptype->colorrand : 0;
				cidx = ptype->colorindex + cidx;
				if (cidx > 255)
					p->rgba[3] = p->rgba[3] / 2; // Hexen 2 style transparency
				cidx = (cidx & 0xff) * 4;
				p->rgba[0] = palrgba[cidx] * (1 / 255.0);
				p->rgba[1] = palrgba[cidx + 1] * (1 / 255.0);
				p->rgba[2] = palrgba[cidx + 2] * (1 / 255.0);
			}
			else
				VectorCopy (ptype->rgb, p->rgba);

			// use org temporarily for rgbsync
			p->org[2] = frandom ();
			p->org[0] = p->org[2] * ptype->rgbrandsync[0] + frandom () * (1 - ptype->rgbrandsync[0]);
			p->org[1] = p->org[2] * ptype->rgbrandsync[1] + frandom () * (1 - ptype->rgbrandsync[1]);
			p->org[2] = p->org[2] * ptype->rgbrandsync[2] + frandom () * (1 - ptype->rgbrandsync[2]);

			p->rgba[0] += p->org[0] * ptype->rgbrand[0] + ptype->rgbchange[0] * p->die;
			p->rgba[1] += p->org[1] * ptype->rgbrand[1] + ptype->rgbchange[1] * p->die;
			p->rgba[2] += p->org[2] * ptype->rgbrand[2] + ptype->rgbchange[2] * p->die;

			p->vel[0] = 0;
			p->vel[1] = 0;
			p->vel[2] = 0;

			// handle spawn modes (org/vel)
			switch (ptype->spawnmode)
			{
			case SM_BOX:
				ofsvec[0] = crandom ();
				ofsvec[1] = crandom ();
				ofsvec[2] = crandom ();

				arsvec[0] = ofsvec[0] * ptype->areaspread;
				arsvec[1] = ofsvec[1] * ptype->areaspread;
				arsvec[2] = ofsvec[2] * ptype->areaspreadvert;
				break;
			case SM_TELEBOX:
				ofsvec[0] = k;
				ofsvec[1] = j;
				ofsvec[2] = l + 4;
				VectorNormalize (ofsvec);
				VectorScale (ofsvec, 1.0 - (frandom ()) * m, ofsvec);

				// org is just like the original
				arsvec[0] = j + (rand () % spawnspc);
				arsvec[1] = k + (rand () % spawnspc);
				arsvec[2] = l + (rand () % spawnspc);

				// advance telebox loop
				j += spawnspc;
				if (j >= ptype->areaspread)
				{
					j = -ptype->areaspread;
					k += spawnspc;
					if (k >= ptype->areaspread)
					{
						k = -ptype->areaspread;
						l += spawnspc;
						if (l >= ptype->areaspreadvert)
							l = -ptype->areaspreadvert;
					}
				}
				break;
			case SM_LAVASPLASH:
				// calc directions, org with temp vector
				ofsvec[0] = k + (rand () % spawnspc);
				ofsvec[1] = j + (rand () % spawnspc);
				ofsvec[2] = 256;

				arsvec[0] = ofsvec[0];
				arsvec[1] = ofsvec[1];
				arsvec[2] = frandom () * ptype->areaspreadvert;

				VectorNormalize (ofsvec);
				VectorScale (ofsvec, 1.0 - (frandom ()) * m, ofsvec);

				// advance splash loop
				j += spawnspc;
				if (j >= ptype->areaspread)
				{
					j = -ptype->areaspread;
					k += spawnspc;
					if (k >= ptype->areaspread)
						k = -ptype->areaspread;
				}
				break;
			case SM_UNICIRCLE:
				ofsvec[0] = cos (m * i);
				ofsvec[1] = sin (m * i);
				ofsvec[2] = 0;
				VectorScale (ofsvec, ptype->areaspread, arsvec);
				break;
			case SM_FIELD:
				arsvec[0] = (cl.time * avelocities[i][0]) + m;
				arsvec[1] = (cl.time * avelocities[i][1]) + m;
				arsvec[2] = cos (arsvec[1]);

				ofsvec[0] = arsvec[2] * cos (arsvec[0]);
				ofsvec[1] = arsvec[2] * sin (arsvec[0]);
				ofsvec[2] = -sin (arsvec[1]);

				orgadd = ptype->spawnparam2 * sin (cl.time + j + m);
				arsvec[0] = r_avertexnormals[j][0] * (ptype->areaspread + orgadd) + ofsvec[0] * ptype->spawnparam1;
				arsvec[1] = r_avertexnormals[j][1] * (ptype->areaspread + orgadd) + ofsvec[1] * ptype->spawnparam1;
				arsvec[2] = r_avertexnormals[j][2] * (ptype->areaspreadvert + orgadd) + ofsvec[2] * ptype->spawnparam1;

				VectorNormalize (ofsvec);

				j++;
				if (j >= NUMVERTEXNORMALS)
				{
					j = 0;
					m += 0.1762891; // some BS number to try to "randomize" things
				}
				break;
			case SM_DISTBALL:
			{
				float rdist;

				rdist = ptype->spawnparam2 - crandom () * (1 - (crandom () * ptype->spawnparam1));

				// this is a strange spawntype, which is based on the fact that
				// crandom()*crandom() provides something similar to an exponential
				// probability curve
				ofsvec[0] = hrandom ();
				ofsvec[1] = hrandom ();
				if (ptype->areaspreadvert)
					ofsvec[2] = hrandom ();
				else
					ofsvec[2] = 0;

				VectorNormalize (ofsvec);
				VectorScale (ofsvec, rdist, ofsvec);

				arsvec[0] = ofsvec[0] * ptype->areaspread;
				arsvec[1] = ofsvec[1] * ptype->areaspread;
				arsvec[2] = ofsvec[2] * ptype->areaspreadvert;
			}
			break;
			default: // SM_BALL, SM_CIRCLE
			{
				ofsvec[0] = hrandom ();
				ofsvec[1] = hrandom ();
				if (ptype->areaspreadvert)
					ofsvec[2] = hrandom ();
				else
					ofsvec[2] = 0;

				VectorNormalize (ofsvec);
				if (ptype->spawnmode != SM_CIRCLE)
					VectorScale (ofsvec, frandom (), ofsvec);

				arsvec[0] = ofsvec[0] * ptype->areaspread;
				arsvec[1] = ofsvec[1] * ptype->areaspread;
				arsvec[2] = ofsvec[2] * ptype->areaspreadvert;
			}
			break;
			}

			// apply arsvec+ofsvec
			orgadd = ptype->orgadd + frandom () * ptype->randomorgadd;
			veladd = ptype->veladd + frandom () * ptype->randomveladd;

			if (dir)
				veladd *= VectorLength (dir);
			VectorMA (p->vel, ofsvec[0] * ptype->spawnvel, axis[0], p->vel);
			VectorMA (p->vel, ofsvec[1] * ptype->spawnvel, axis[1], p->vel);
			VectorMA (p->vel, veladd + ofsvec[2] * ptype->spawnvelvert, axis[2], p->vel);

			VectorMA (org, arsvec[0], axis[0], p->org);
			VectorMA (p->org, arsvec[1], axis[1], p->org);
			VectorMA (p->org, orgadd + arsvec[2], axis[2], p->org);

			if (ptype->flags & PT_WORLDSPACERAND)
			{
				do
				{
					ofsvec[0] = crand ();
					ofsvec[1] = crand ();
					ofsvec[2] = crand ();
				} while (DotProduct (ofsvec, ofsvec) > 1); // crap, but I'm trying to mimic dp
				p->org[0] += ofsvec[0] * ptype->orgwrand[0];
				p->org[1] += ofsvec[1] * ptype->orgwrand[1];
				p->org[2] += ofsvec[2] * ptype->orgwrand[2];
				p->vel[0] += ofsvec[0] * ptype->velwrand[0];
				p->vel[1] += ofsvec[1] * ptype->velwrand[1];
				p->vel[2] += ofsvec[2] * ptype->velwrand[2];
				VectorAdd (p->vel, ptype->velbias, p->vel);
			}
			VectorAdd (p->org, ptype->orgbias, p->org);

			p->die = particletime + ptype->die - p->die;

			VectorCopy (p->org, p->oldorg);
		}

		// update beam list
		if (ptype->looks.type == PT_BEAM)
		{
			if (b)
			{
				// update dir for bfirst for certain modes since it will never get updated
				switch (ptype->spawnmode)
				{
				case SM_UNICIRCLE:
					// kinda hackish here, assuming ofsvec contains the point at i-1
					arsvec[0] = cos (m * (i - 2));
					arsvec[1] = sin (m * (i - 2));
					arsvec[2] = 0;
					VectorSubtract (b->p->org, arsvec, bfirst->dir);
					VectorNormalize (bfirst->dir);
					break;
				default:
					break;
				}

				b->flags |= BS_NODRAW;
				b->next = ptype->beams;
				ptype->beams = bfirst;
			}
		}

		// save off emit times in trailstate
		if (ts)
			ts->state2.emittime = pcount - i;

		// maintain run list
		if (!(ptype->state & PS_INRUNLIST) && (ptype->particles || ptype->clippeddecals))
		{
			if (part_run_list)
			{
				// insert after, to try to avoid edge-case weirdness
				ptype->nexttorun = part_run_list->nexttorun;
				part_run_list->nexttorun = ptype;
			}
			else
			{
				ptype->nexttorun = part_run_list;
				part_run_list = ptype;
			}
			ptype->state |= PS_INRUNLIST;
		}

	skip:

		// go to next associated effect
		if (ptype->assoc < 0)
			break;

		// new trailstate
		if (ts)
		{
			tsk = &(ts->assoc);
			// if *tsk = NULL get a new one
			if (*tsk == NULL)
			{
				ts = P_NewTrailstate (tsk);
				*tsk = ts;
			}
			else
			{
				ts = *tsk;

				if (ts->key != tsk) // trailstate was overwritten
				{
					ts = P_NewTrailstate (tsk); // so get a new one
					*tsk = ts;
				}
			}
		}

		ptype = &part_type[ptype->assoc];
	}

	return 0;
}

int PScript_RunParticleEffectTypeString (vec3_t org, vec3_t dir, float count, const char *name)
{
	if (r_fteparticles.value == 0)
		return 1;
	int type = PScript_FindParticleType (name);
	if (type < 0)
		return 1;

	return PScript_RunParticleEffectState (org, dir, count, type, NULL);
}

int PScript_EntParticleTrail (vec3_t oldorg, entity_t *ent, const char *name)
{
	if (r_fteparticles.value == 0)
		return 1;
	float  timeinterval = cl.time - cl.oldtime;
	vec3_t axis[3];
	int	   type = PScript_FindParticleType (name);
	if (type < 0)
		return 1;

	AngleVectors (ent->angles, axis[0], axis[1], axis[2]);
	return PScript_ParticleTrail (oldorg, ent->origin, type, timeinterval, ent - cl.entities, axis, &ent->trailstate);
}

/*
===============
P_RunParticleEffect

===============
*/
int PScript_RunParticleEffect (vec3_t org, vec3_t dir, int color, int count)
{
	if (r_fteparticles.value == 0)
		return false;
	int ptype;

	ptype = PScript_FindParticleType (va ("pe_%i", color));
	if (PScript_RunParticleEffectState (org, dir, count, ptype, NULL))
	{
		if (count > 130 && PART_VALID (pe_size3))
		{
			part_type[pe_size3].colorindex = color & ~0x7;
			part_type[pe_size3].colorrand = 8;
			return PScript_RunParticleEffectState (org, dir, count, pe_size3, NULL);
		}
		else if (count > 20 && PART_VALID (pe_size2))
		{
			part_type[pe_size2].colorindex = color & ~0x7;
			part_type[pe_size2].colorrand = 8;
			return PScript_RunParticleEffectState (org, dir, count, pe_size2, NULL);
		}
		else if (PART_VALID (pe_default))
		{
			part_type[pe_default].colorindex = color & ~0x7;
			part_type[pe_default].colorrand = 8;
			return PScript_RunParticleEffectState (org, dir, count, pe_default, NULL);
		}
		return true;
	}
	return false;
}

void PScript_RunParticleWeather (vec3_t minb, vec3_t maxb, vec3_t dir, float count, int colour, const char *efname)
{
	vec3_t org;
	int	   i, j;
	float  num;
	float  invcount;

	int ptype = PScript_FindParticleType (va ("te_%s_%i", efname, colour));
	if (!PART_VALID (ptype))
	{
		ptype = PScript_FindParticleType (va ("te_%s", efname));
		if (!PART_VALID (ptype))
			ptype = pe_default;
		if (!PART_VALID (ptype))
			return;
		part_type[ptype].colorindex = colour;
	}

	invcount = 1 / part_type[ptype].count; // using this to get R_RPET to always spawn 1
	count = count * part_type[ptype].count;

	for (i = 0; i < count; i++)
	{
		if (!free_particles)
			return;

		for (j = 0; j < 3; j++)
		{
			num = rand () / (float)RAND_MAX;
			org[j] = minb[j] + num * (maxb[j] - minb[j]);
		}
		PScript_RunParticleEffectState (org, dir, invcount, ptype, NULL);
	}
}

static void PScript_ParticleTrailSpawn (vec3_t startpos, vec3_t end, part_type_t *ptype, float timeinterval, trailstate_t **tsk, int dlkey, vec3_t dlaxis[3])
{
	vec3_t		  vec, vstep, right, up, start;
	float		  len;
	int			  tcount;
	particle_t	 *p;
	beamseg_t	 *b;
	beamseg_t	 *bfirst;
	trailstate_t *ts;
	float		  count;

	float veladd = -ptype->veladd;
	float step;
	float stop;
	float tdegree = 2.0 * M_PI / 256; /* MSVC whine */
	float sdegree = 0;
	float nrfirst, nrlast;
	byte *palrgba = (byte *)d_8to24table;

	VectorCopy (startpos, start);

	// eliminate trailstate if flag set
	if (ptype->flags & PT_NOSTATE)
		tsk = NULL;

	// trailstate allocation/deallocation
	if (tsk)
	{
		// if *tsk = NULL get a new one
		if (*tsk == NULL)
		{
			ts = P_NewTrailstate (tsk);
			*tsk = ts;
		}
		else
		{
			ts = *tsk;

			if (ts->key != tsk) // trailstate was overwritten
			{
				ts = P_NewTrailstate (tsk); // so get a new one
				*tsk = ts;
			}
		}
	}
	else
		ts = NULL;

	PScript_EffectSpawned (ptype, start, dlaxis, dlkey, 1);

	if (ptype->assoc >= 0)
	{
		if (ts)
			PScript_ParticleTrail (start, end, ptype->assoc, timeinterval, dlkey, NULL, &(ts->assoc));
		else
			PScript_ParticleTrail (start, end, ptype->assoc, timeinterval, dlkey, NULL, NULL);
	}

	if (r_part_contentswitch.value && (ptype->flags & (PT_TRUNDERWATER | PT_TROVERWATER)) && cl.worldmodel)
	{
		unsigned int cont;
		cont = CL_PointContentsMask (startpos);

		if ((ptype->flags & PT_TROVERWATER) && (cont & ptype->fluidmask))
			return;
		if ((ptype->flags & PT_TRUNDERWATER) && !(cont & ptype->fluidmask))
			return;
	}

	// time limit for trails
	if (ptype->spawntime && ts)
	{
		if (ts->state1.statetime > particletime)
			return; // timelimit still in effect

		ts->state1.statetime = particletime + ptype->spawntime; // record old time
		ts = NULL;												// clear trailstate so we don't save length/lastseg
	}

	// random chance for trails
	if (ptype->spawnchance < frandom ())
		return; // don't spawn but return success

	if (!ptype->die)
		ts = NULL;

	VectorSubtract (end, start, vec);
	len = VectorNormalize (vec);

	// use ptype step to calc step vector and step size
	if (ptype->countspacing)
	{
		step = ptype->countspacing;	  // particles per qu
		step /= r_part_density.value; // scaled...

		if (ptype->countextra)
		{
			count = ptype->countextra;
			if (step > 0)
				count += len / step;
			step = len / count;
		}
	}
	else
	{
		step = ptype->count * r_part_density.value * timeinterval;
		step += ptype->countextra; // particles per frame
		step += ptype->countoverflow;
		count = (int)step;
		ptype->countoverflow = step - count; // the part that we're forgetting, to add to the next frame...
		if (count <= 0)
			return;
		else
			step = len / count; // particles per second
	}

	if (ptype->flags & PT_AVERAGETRAIL)
	{
		float tavg;
		// mangle len/step to get last point to be at end
		tavg = len / step;
		tavg = tavg / ceil (tavg);
		step *= tavg;
		len += step;
	}

	VectorScale (vec, step, vstep);

	// add offset
	//	VectorAdd(start, ptype->orgbias, start);

	// spawn mode precalculations
	if (ptype->spawnmode == SM_SPIRAL)
	{
		VectorVectors (vec, right, up);

		// precalculate degree of rotation
		if (ptype->spawnparam1)
			tdegree = 2.0 * M_PI / ptype->spawnparam1; /* distance per rotation inversed */
		sdegree = ptype->spawnparam2 * (M_PI / 180);
	}
	else if (ptype->spawnmode == SM_CIRCLE)
	{
		VectorVectors (vec, right, up);
	}

	// store last stop here for lack of a better solution besides vectors
	if (ts)
	{
		ts->state2.laststop = stop = ts->state2.laststop + len; // when to stop
		len = ts->state1.lastdist;
	}
	else
	{
		stop = len;
		len = 0;
	}

	//	len = ts->lastdist/step;
	//	len = (len - (int)len)*step;
	//	VectorMA (start, -len, vec, start);

	if (ptype->flags & PT_NOSPREADFIRST)
		nrfirst = len + step * 1.5;
	else
		nrfirst = len;

	if (ptype->flags & PT_NOSPREADLAST)
		nrlast = stop;
	else
		nrlast = stop + step;

	b = bfirst = NULL;

	if (len < stop)
		count = (stop - len) / step;
	else
	{
		count = 0;
		step = 0;
		VectorClear (vstep);
	}
	//	count += ptype->countextra;

	while (count-- > 0) // len < stop)
	{
		len += step;

		if (!free_particles)
		{
			len = stop;
			break;
		}

		p = free_particles;
		if (ptype->looks.type == PT_BEAM)
		{
			if (!free_beams)
			{
				len = stop;
				break;
			}
			if (b)
			{
				b = b->next = free_beams;
				free_beams = free_beams->next;
			}
			else
			{
				b = bfirst = free_beams;
				free_beams = free_beams->next;
			}
			b->texture_s = len; // not sure how to calc this
			b->flags = 0;
			b->p = p;
			VectorCopy (vec, b->dir);
		}

		free_particles = p->next;
		p->next = ptype->particles;
		ptype->particles = p;

		p->die = ptype->randdie * frandom ();
		p->scale = ptype->scale + ptype->scalerand * frandom ();
		if (ptype->die)
			p->rgba[3] = ptype->alpha + p->die * ptype->alphachange;
		else
			p->rgba[3] = ptype->alpha;
		p->rgba[3] += ptype->alpharand * frandom ();
		//		p->color = 0;

		//		if (ptype->spawnmode == SM_TRACER)
		if (ptype->spawnparam1)
			tcount = (int)(len * ptype->count / ptype->spawnparam1);
		else
			tcount = (int)(len * ptype->count);

		if (ptype->colorindex >= 0)
		{
			int cidx;
			cidx = ptype->colorrand > 0 ? rand () % ptype->colorrand : 0;
			if (ptype->flags & PT_CITRACER) // colorindex behavior as per tracers in std Q1
				cidx += ((tcount & 4) << 1);

			cidx = ptype->colorindex + cidx;
			if (cidx > 255)
				p->rgba[3] = p->rgba[3] / 2;
			cidx = (cidx & 0xff) * 4;
			p->rgba[0] = palrgba[cidx] * (1 / 255.0);
			p->rgba[1] = palrgba[cidx + 1] * (1 / 255.0);
			p->rgba[2] = palrgba[cidx + 2] * (1 / 255.0);
		}
		else
			VectorCopy (ptype->rgb, p->rgba);

		// use org temporarily for rgbsync
		p->org[2] = frandom ();
		p->org[0] = p->org[2] * ptype->rgbrandsync[0] + frandom () * (1 - ptype->rgbrandsync[0]);
		p->org[1] = p->org[2] * ptype->rgbrandsync[1] + frandom () * (1 - ptype->rgbrandsync[1]);
		p->org[2] = p->org[2] * ptype->rgbrandsync[2] + frandom () * (1 - ptype->rgbrandsync[2]);

		p->rgba[0] += p->org[0] * ptype->rgbrand[0] + ptype->rgbchange[0] * p->die;
		p->rgba[1] += p->org[1] * ptype->rgbrand[1] + ptype->rgbchange[1] * p->die;
		p->rgba[2] += p->org[2] * ptype->rgbrand[2] + ptype->rgbchange[2] * p->die;

		VectorClear (p->vel);
		if (ptype->emittime < 0)
			p->state.trailstate = NULL; // init trailstate
		else
			p->state.nextemit = particletime + ptype->emitstart - p->die;

		p->rotationspeed = ptype->rotationmin + frandom () * ptype->rotationrand;
		p->angle = ptype->rotationstartmin + frandom () * ptype->rotationstartrand;
		p->s1 = ptype->s1;
		p->t1 = ptype->t1;
		p->s2 = ptype->s2;
		p->t2 = ptype->t2;
		if (ptype->randsmax != 1)
		{
			float offs;
			offs = ptype->texsstride * (rand () % ptype->randsmax);
			p->s1 += offs;
			p->s2 += offs;
			while (p->s1 >= 1)
			{
				p->s1 -= 1;
				p->s2 -= 1;
				p->t1 += ptype->texsstride;
				p->t2 += ptype->texsstride;
			}
		}

		if (len < nrfirst || len >= nrlast)
		{
			// no offset or areaspread for these particles...
			p->vel[0] = vec[0] * veladd;
			p->vel[1] = vec[1] * veladd;
			p->vel[2] = vec[2] * veladd;

			VectorCopy (start, p->org);
		}
		else
		{
			switch (ptype->spawnmode)
			{
			case SM_TRACER:
				if (tcount & 1)
				{
					p->vel[0] = vec[1] * ptype->spawnvel;
					p->vel[1] = -vec[0] * ptype->spawnvel;
					p->org[0] = vec[1] * ptype->areaspread;
					p->org[1] = -vec[0] * ptype->areaspread;
				}
				else
				{
					p->vel[0] = -vec[1] * ptype->spawnvel;
					p->vel[1] = vec[0] * ptype->spawnvel;
					p->org[0] = -vec[1] * ptype->areaspread;
					p->org[1] = vec[0] * ptype->areaspread;
				}

				p->vel[0] += vec[0] * veladd;
				p->vel[1] += vec[1] * veladd;
				p->vel[2] = vec[2] * veladd;

				p->org[0] += start[0];
				p->org[1] += start[1];
				p->org[2] = start[2];
				break;
			case SM_SPIRAL:
			{
				float tsin, tcos;
				float tright, tup;

				tcos = cos (len * tdegree + sdegree);
				tsin = sin (len * tdegree + sdegree);

				tright = tcos * ptype->areaspread;
				tup = tsin * ptype->areaspread;

				p->org[0] = start[0] + right[0] * tright + up[0] * tup;
				p->org[1] = start[1] + right[1] * tright + up[1] * tup;
				p->org[2] = start[2] + right[2] * tright + up[2] * tup;

				tright = tcos * ptype->spawnvel;
				tup = tsin * ptype->spawnvel;

				p->vel[0] = vec[0] * veladd + right[0] * tright + up[0] * tup;
				p->vel[1] = vec[1] * veladd + right[1] * tright + up[1] * tup;
				p->vel[2] = vec[2] * veladd + right[2] * tright + up[2] * tup;
			}
			break;
			// TODO: directionalize SM_BALL/SM_CIRCLE/SM_DISTBALL
			case SM_BALL:
				p->org[0] = crandom ();
				p->org[1] = crandom ();
				p->org[2] = crandom ();
				VectorNormalize (p->org);
				VectorScale (p->org, frandom (), p->org);

				p->vel[0] = vec[0] * veladd + p->org[0] * ptype->spawnvel;
				p->vel[1] = vec[1] * veladd + p->org[1] * ptype->spawnvel;
				p->vel[2] = vec[2] * veladd + p->org[2] * ptype->spawnvelvert;

				p->org[0] = p->org[0] * ptype->areaspread + start[0];
				p->org[1] = p->org[1] * ptype->areaspread + start[1];
				p->org[2] = p->org[2] * ptype->areaspreadvert + start[2];
				break;

			case SM_CIRCLE:
			{
				float tsin, tcos;

				tcos = cos (len * tdegree) * ptype->areaspread;
				tsin = sin (len * tdegree) * ptype->areaspread;

				p->org[0] = start[0] + right[0] * tcos + up[0] * tsin + vstep[0] * (len * tdegree);
				p->org[1] = start[1] + right[1] * tcos + up[1] * tsin + vstep[1] * (len * tdegree);
				p->org[2] = start[2] + right[2] * tcos + up[2] * tsin + vstep[2] * (len * tdegree) * 50;

				tcos = cos (len * tdegree) * ptype->spawnvel;
				tsin = sin (len * tdegree) * ptype->spawnvel;

				p->vel[0] = vec[0] * veladd + right[0] * tcos + up[0] * tsin;
				p->vel[1] = vec[1] * veladd + right[1] * tcos + up[1] * tsin;
				p->vel[2] = vec[2] * veladd + right[2] * tcos + up[2] * tsin;
			}
			break;

			case SM_DISTBALL:
			{
				float rdist;

				rdist = ptype->spawnparam2 - crandom () * (1 - (crandom () * ptype->spawnparam1));

				// this is a strange spawntype, which is based on the fact that
				// crandom()*crandom() provides something similar to an exponential
				// probability curve
				p->org[0] = crandom ();
				p->org[1] = crandom ();
				p->org[2] = crandom ();

				VectorNormalize (p->org);
				VectorScale (p->org, rdist, p->org);

				p->vel[0] = vec[0] * veladd + p->org[0] * ptype->spawnvel;
				p->vel[1] = vec[1] * veladd + p->org[1] * ptype->spawnvel;
				p->vel[2] = vec[2] * veladd + p->org[2] * ptype->spawnvelvert;

				p->org[0] = p->org[0] * ptype->areaspread + start[0];
				p->org[1] = p->org[1] * ptype->areaspread + start[1];
				p->org[2] = p->org[2] * ptype->areaspreadvert + start[2];
			}
			break;
			default:
				p->org[0] = crandom ();
				p->org[1] = crandom ();
				p->org[2] = crandom ();

				p->vel[0] = vec[0] * veladd + p->org[0] * ptype->spawnvel;
				p->vel[1] = vec[1] * veladd + p->org[1] * ptype->spawnvel;
				p->vel[2] = vec[2] * veladd + p->org[2] * ptype->spawnvelvert;

				p->org[0] = p->org[0] * ptype->areaspread + start[0];
				p->org[1] = p->org[1] * ptype->areaspread + start[1];
				p->org[2] = p->org[2] * ptype->areaspreadvert + start[2];
				break;
			}

			if (ptype->orgadd)
			{
				p->org[0] += vec[0] * ptype->orgadd;
				p->org[1] += vec[1] * ptype->orgadd;
				p->org[2] += vec[2] * ptype->orgadd;
			}
		}
		if (ptype->flags & PT_WORLDSPACERAND)
		{
			vec3_t vtmp;
			do
			{
				vtmp[0] = crand ();
				vtmp[1] = crand ();
				vtmp[2] = crand ();
			} while (DotProduct (vtmp, vtmp) > 1); // crap, but I'm trying to mimic dp
			p->org[0] += vtmp[0] * ptype->orgwrand[0];
			p->org[1] += vtmp[1] * ptype->orgwrand[1];
			p->org[2] += vtmp[2] * ptype->orgwrand[2];
			p->vel[0] += vtmp[0] * ptype->velwrand[0];
			p->vel[1] += vtmp[1] * ptype->velwrand[1];
			p->vel[2] += vtmp[2] * ptype->velwrand[2];
			VectorAdd (p->vel, ptype->velbias, p->vel);
		}
		VectorAdd (p->org, ptype->orgbias, p->org);

		VectorAdd (start, vstep, start);

		if (ptype->countrand)
		{
			float rstep = frandom () / ptype->countrand;
			VectorMA (start, rstep, vec, start);
			step += rstep;
		}

		p->die = particletime + ptype->die - p->die;
		VectorCopy (p->org, p->oldorg);
	}

	if (ts)
	{
		ts->state1.lastdist = len;

		// update beamseg list
		if (ptype->looks.type == PT_BEAM)
		{
			if (b)
			{
				if (ptype->beams)
				{
					if (ts->lastbeam)
					{
						b->next = ts->lastbeam->next;
						ts->lastbeam->next = bfirst;
						ts->lastbeam->flags &= ~BS_LASTSEG;
					}
					else
					{
						b->next = ptype->beams;
						ptype->beams = bfirst;
					}
				}
				else
				{
					ptype->beams = bfirst;
					b->next = NULL;
				}

				b->flags |= BS_LASTSEG;
				ts->lastbeam = b;
			}

			if ((!free_particles || !free_beams) && ts->lastbeam)
			{
				ts->lastbeam->flags &= ~BS_LASTSEG;
				ts->lastbeam->flags |= BS_NODRAW;
				ts->lastbeam = NULL;
			}
		}
	}
	else if (ptype->looks.type == PT_BEAM)
	{
		if (b)
		{
			b->flags |= BS_NODRAW;
			b->next = ptype->beams;
			ptype->beams = bfirst;
		}
	}

	// maintain run list
	if (!(ptype->state & PS_INRUNLIST))
	{
		ptype->nexttorun = part_run_list;
		part_run_list = ptype;
		ptype->state |= PS_INRUNLIST;
	}

	return;
}

int PScript_ParticleTrail (vec3_t startpos, vec3_t end, int type, float timeinterval, int dlkey, vec3_t axis[3], trailstate_t **tsk)
{
	part_type_t *ptype = &part_type[type];

	if (r_fteparticles.value == 0)
		return 1;

	if (type < 0 || type >= numparticletypes)
		return 1; // bad value

	if (!ptype->loaded)
		return 1;

	// inwater check, switch only once
	if (r_part_contentswitch.value && ptype->inwater >= 0 && cl.worldmodel)
	{
		unsigned int cont;
		cont = CL_PointContentsMask (startpos);

		if (cont & FTECONTENTS_FLUID)
			ptype = &part_type[ptype->inwater];
	}

	PScript_ParticleTrailSpawn (startpos, end, ptype, timeinterval, tsk, dlkey, axis);
	return 0;
}

static int			   current_buffer_index = 0;
static VkBuffer		   vertex_buffers[2] = {VK_NULL_HANDLE, VK_NULL_HANDLE};
static vulkan_memory_t vertex_buffers_memory[2] = {{VK_NULL_HANDLE, 0, 0}, {VK_NULL_HANDLE, 0, 0}};
static VkBuffer		   index_buffers[2] = {VK_NULL_HANDLE, VK_NULL_HANDLE};
static vulkan_memory_t index_buffers_memory[2] = {{VK_NULL_HANDLE, 0, 0}, {VK_NULL_HANDLE, 0, 0}};

static void ReallocateVertexBuffer ()
{
	VkResult err;

	if (vertex_buffers[current_buffer_index] != VK_NULL_HANDLE)
		vkDestroyBuffer (vulkan_globals.device, vertex_buffers[current_buffer_index], NULL);

	vulkan_memory_t		 old_memory = vertex_buffers_memory[current_buffer_index];
	const basicvertex_t *old_cl_curstrisvert = cl_curstrisvert;
	const int			 old_maxstrisvert = cl_maxstrisvert[current_buffer_index];

	cl_maxstrisvert[current_buffer_index] = q_max (cl_maxstrisvert[current_buffer_index] * 2, INITIAL_NUM_VERTICES);
	const VkDeviceSize new_size = cl_maxstrisvert[current_buffer_index] * sizeof (basicvertex_t);
	Sys_Printf ("Reallocating FTE particle vertex buffer (%u KB)\n", (int)(new_size / 1024));

	ZEROED_STRUCT (VkBufferCreateInfo, buffer_create_info);
	buffer_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	buffer_create_info.size = new_size;
	buffer_create_info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;

	err = vkCreateBuffer (vulkan_globals.device, &buffer_create_info, NULL, &vertex_buffers[current_buffer_index]);
	if (err != VK_SUCCESS)
		Sys_Error ("vkCreateBuffer failed");
	GL_SetObjectName ((uint64_t)vertex_buffers[current_buffer_index], VK_OBJECT_TYPE_BUFFER, "FTE Particle Vertex Buffer");

	VkMemoryRequirements memory_requirements;
	vkGetBufferMemoryRequirements (vulkan_globals.device, vertex_buffers[current_buffer_index], &memory_requirements);

	const int aligned_size = q_align (memory_requirements.size, memory_requirements.alignment);

	ZEROED_STRUCT (VkMemoryAllocateInfo, memory_allocate_info);
	memory_allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	memory_allocate_info.allocationSize = aligned_size;
	memory_allocate_info.memoryTypeIndex =
		GL_MemoryTypeFromProperties (memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, VK_MEMORY_PROPERTY_HOST_CACHED_BIT);

	R_AllocateVulkanMemory (&vertex_buffers_memory[current_buffer_index], &memory_allocate_info, VULKAN_MEMORY_TYPE_HOST, &num_vulkan_dynbuf_allocations);
	GL_SetObjectName ((uint64_t)vertex_buffers_memory[current_buffer_index].handle, VK_OBJECT_TYPE_DEVICE_MEMORY, "FTE Particle Vertex Buffer");

	err = vkBindBufferMemory (vulkan_globals.device, vertex_buffers[current_buffer_index], vertex_buffers_memory[current_buffer_index].handle, 0);
	if (err != VK_SUCCESS)
		Sys_Error ("vkBindBufferMemory failed");

	err = vkMapMemory (vulkan_globals.device, vertex_buffers_memory[current_buffer_index].handle, 0, new_size, 0, (void **)&cl_curstrisvert);
	if (err != VK_SUCCESS)
		Sys_Error ("vkMapMemory failed");
	cl_strisvert[current_buffer_index] = cl_curstrisvert;

	if (old_memory.handle != VK_NULL_HANDLE)
	{
		// Copy over data from old buffer
		memcpy (cl_curstrisvert, old_cl_curstrisvert, old_maxstrisvert * sizeof (basicvertex_t));

		vkUnmapMemory (vulkan_globals.device, old_memory.handle);
		R_FreeVulkanMemory (&old_memory, &num_vulkan_dynbuf_allocations);
	}
}

static void ReallocateIndexBuffer ()
{
	VkResult err;

	if (index_buffers[current_buffer_index] != VK_NULL_HANDLE)
		vkDestroyBuffer (vulkan_globals.device, index_buffers[current_buffer_index], NULL);

	vulkan_memory_t		  old_memory = index_buffers_memory[current_buffer_index];
	const unsigned short *old_cl_curstrisidx = cl_curstrisidx;
	const int			  old_maxstrisidx = cl_maxstrisidx[current_buffer_index];

	cl_maxstrisidx[current_buffer_index] = q_max (cl_maxstrisidx[current_buffer_index] * 2, INITIAL_NUM_INDICES);
	const VkDeviceSize new_size = cl_maxstrisidx[current_buffer_index] * sizeof (unsigned short);
	Sys_Printf ("Reallocating FTE particle index buffer (%u KB)\n", (int)(new_size / 1024));

	ZEROED_STRUCT (VkBufferCreateInfo, buffer_create_info);
	buffer_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	buffer_create_info.size = new_size;
	buffer_create_info.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;

	err = vkCreateBuffer (vulkan_globals.device, &buffer_create_info, NULL, &index_buffers[current_buffer_index]);
	if (err != VK_SUCCESS)
		Sys_Error ("vkCreateBuffer failed");
	GL_SetObjectName ((uint64_t)index_buffers[current_buffer_index], VK_OBJECT_TYPE_BUFFER, "FTE Particle Index Buffer");

	VkMemoryRequirements memory_requirements;
	vkGetBufferMemoryRequirements (vulkan_globals.device, index_buffers[current_buffer_index], &memory_requirements);

	const int aligned_size = q_align (memory_requirements.size, memory_requirements.alignment);

	ZEROED_STRUCT (VkMemoryAllocateInfo, memory_allocate_info);
	memory_allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	memory_allocate_info.allocationSize = aligned_size;
	memory_allocate_info.memoryTypeIndex =
		GL_MemoryTypeFromProperties (memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, VK_MEMORY_PROPERTY_HOST_CACHED_BIT);

	R_AllocateVulkanMemory (&index_buffers_memory[current_buffer_index], &memory_allocate_info, VULKAN_MEMORY_TYPE_HOST, &num_vulkan_dynbuf_allocations);
	GL_SetObjectName ((uint64_t)index_buffers_memory[current_buffer_index].handle, VK_OBJECT_TYPE_DEVICE_MEMORY, "FTE Particle index Buffer");

	err = vkBindBufferMemory (vulkan_globals.device, index_buffers[current_buffer_index], index_buffers_memory[current_buffer_index].handle, 0);
	if (err != VK_SUCCESS)
		Sys_Error ("vkBindBufferMemory failed");

	err = vkMapMemory (vulkan_globals.device, index_buffers_memory[current_buffer_index].handle, 0, new_size, 0, (void **)&cl_curstrisidx);
	if (err != VK_SUCCESS)
		Sys_Error ("vkMapMemory failed");
	cl_strisidx[current_buffer_index] = cl_curstrisidx;

	if (old_memory.handle != VK_NULL_HANDLE)
	{
		// Copy over data from old buffer
		memcpy (cl_curstrisidx, old_cl_curstrisidx, old_maxstrisidx * sizeof (unsigned short));

		vkUnmapMemory (vulkan_globals.device, old_memory.handle);
		R_FreeVulkanMemory (&old_memory, &num_vulkan_dynbuf_allocations);
	}
}

static vec3_t pright, pup;

static void R_AddFanSparkParticle (scenetris_t *t, particle_t *p, plooks_t *type)
{
	vec3_t v, cr, o2;
	float  scale;

	if (cl_numstrisvert + 3 > cl_maxstrisvert[current_buffer_index])
		ReallocateVertexBuffer ();

	scale = (p->org[0] - r_origin[0]) * vpn[0] + (p->org[1] - r_origin[1]) * vpn[1] + (p->org[2] - r_origin[2]) * vpn[2];
	scale = (scale * p->scale) * (type->invscalefactor) + p->scale * (type->scalefactor * 250);
	if (scale < 20)
		scale = 0.05;
	else
		scale = 0.05 + scale * 0.0001;

	if (type->premul)
	{
		vec4_t rgba;
		float  a = p->rgba[3];
		if (a > 1)
			a = 1;
		a *= 255.0f;
		rgba[0] = p->rgba[0] * a;
		rgba[1] = p->rgba[1] * a;
		rgba[2] = p->rgba[2] * a;
		rgba[3] = (type->premul == 2) ? 0 : a;
		Vector4ToColor (rgba, cl_curstrisvert[cl_numstrisvert + 0].color);
		Vector4ToColor (rgba, cl_curstrisvert[cl_numstrisvert + 1].color);
		Vector4ToColor (rgba, cl_curstrisvert[cl_numstrisvert + 2].color);
	}
	else
	{
		Vector4ToColor (p->rgba, cl_curstrisvert[cl_numstrisvert + 0].color);
		Vector4ToColor (p->rgba, cl_curstrisvert[cl_numstrisvert + 1].color);
		Vector4ToColor (p->rgba, cl_curstrisvert[cl_numstrisvert + 2].color);
	}

	Vector2Set (cl_curstrisvert[cl_numstrisvert + 0].texcoord, p->s1, p->t1);
	Vector2Set (cl_curstrisvert[cl_numstrisvert + 1].texcoord, p->s1, p->t2);
	Vector2Set (cl_curstrisvert[cl_numstrisvert + 2].texcoord, p->s2, p->t1);

	VectorMA (p->org, -scale, p->vel, o2);
	VectorSubtract (r_refdef.vieworg, o2, v);
	CrossProduct (v, p->vel, cr);
	VectorNormalize (cr);

	VectorCopy (p->org, cl_curstrisvert[cl_numstrisvert + 0].position);
	VectorMA (o2, -p->scale, cr, cl_curstrisvert[cl_numstrisvert + 1].position);
	VectorMA (o2, p->scale, cr, cl_curstrisvert[cl_numstrisvert + 2].position);

	if (cl_numstrisidx + 3 > cl_maxstrisidx[current_buffer_index])
		ReallocateIndexBuffer ();

	cl_curstrisidx[cl_numstrisidx++] = (cl_numstrisvert - t->firstvert) + 0;
	cl_curstrisidx[cl_numstrisidx++] = (cl_numstrisvert - t->firstvert) + 1;
	cl_curstrisidx[cl_numstrisidx++] = (cl_numstrisvert - t->firstvert) + 2;

	cl_numstrisvert += 3;

	t->numvert += 3;
	t->numidx += 3;
}

static void R_AddLineSparkParticle (scenetris_t *t, particle_t *p, plooks_t *type)
{
	if (cl_numstrisvert + 2 > cl_maxstrisvert[current_buffer_index])
		ReallocateVertexBuffer ();

	if (type->premul)
	{
		vec4_t scaled_color;
		float  a = p->rgba[3];
		if (a > 1)
			a = 1;
		VectorScale (p->rgba, a, scaled_color);
		Vector3ToColor (scaled_color, cl_curstrisvert[cl_numstrisvert + 0].color);
		FloatToColor ((type->premul == 2) ? 0 : a, cl_curstrisvert[cl_numstrisvert + 0].color[3]);
		Vector4Clear (cl_curstrisvert[cl_numstrisvert + 1].color);
	}
	else
	{
		Vector4ToColor (p->rgba, cl_curstrisvert[cl_numstrisvert + 0].color);
		Vector3ToColor (p->rgba, cl_curstrisvert[cl_numstrisvert + 1].color);
		cl_curstrisvert[cl_numstrisvert + 1].color[3] = 0;
	}
	Vector2Set (cl_curstrisvert[cl_numstrisvert + 0].texcoord, p->s1, p->t1);
	Vector2Set (cl_curstrisvert[cl_numstrisvert + 1].texcoord, p->s2, p->t2);

	VectorCopy (p->org, cl_curstrisvert[cl_numstrisvert + 0].position);
	VectorMA (p->org, -1.0 / 10, p->vel, cl_curstrisvert[cl_numstrisvert + 1].position);

	if (cl_numstrisidx + 2 > cl_maxstrisidx[current_buffer_index])
		ReallocateIndexBuffer ();

	cl_curstrisidx[cl_numstrisidx++] = (cl_numstrisvert - t->firstvert) + 0;
	cl_curstrisidx[cl_numstrisidx++] = (cl_numstrisvert - t->firstvert) + 1;

	cl_numstrisvert += 2;

	t->numvert += 2;
	t->numidx += 2;
}

static void R_AddTSparkParticle (scenetris_t *t, particle_t *p, plooks_t *type)
{
	vec3_t v, cr, o2;

	if (cl_numstrisvert + 4 > cl_maxstrisvert[current_buffer_index])
		ReallocateVertexBuffer ();

	if (type->premul)
	{
		vec4_t rgba;
		float  a = p->rgba[3];
		if (a > 1)
			a = 1;
		rgba[0] = p->rgba[0] * a;
		rgba[1] = p->rgba[1] * a;
		rgba[2] = p->rgba[2] * a;
		rgba[3] = (type->premul == 2) ? 0 : a;
		Vector4ToColor (rgba, cl_curstrisvert[cl_numstrisvert + 0].color);
		Vector4ToColor (rgba, cl_curstrisvert[cl_numstrisvert + 1].color);
		Vector4ToColor (rgba, cl_curstrisvert[cl_numstrisvert + 2].color);
		Vector4ToColor (rgba, cl_curstrisvert[cl_numstrisvert + 3].color);
	}
	else
	{
		Vector4ToColor (p->rgba, cl_curstrisvert[cl_numstrisvert + 0].color);
		Vector4ToColor (p->rgba, cl_curstrisvert[cl_numstrisvert + 1].color);
		Vector4ToColor (p->rgba, cl_curstrisvert[cl_numstrisvert + 2].color);
		Vector4ToColor (p->rgba, cl_curstrisvert[cl_numstrisvert + 3].color);
	}

	Vector2Set (cl_curstrisvert[cl_numstrisvert + 0].texcoord, p->s1, p->t1);
	Vector2Set (cl_curstrisvert[cl_numstrisvert + 1].texcoord, p->s1, p->t2);
	Vector2Set (cl_curstrisvert[cl_numstrisvert + 2].texcoord, p->s2, p->t2);
	Vector2Set (cl_curstrisvert[cl_numstrisvert + 3].texcoord, p->s2, p->t1);

	{
		vec3_t movedir;
		float  halfscale = p->scale * 0.5;
		float  length = VectorNormalize2 (p->vel, movedir);
		if (type->stretch < 0)
			length = -type->stretch; // fixed lengths
		else if (type->stretch)
			length *= type->stretch; // velocity multiplier
		else
			Sys_Error ("type->stretch should be 0.05\n");
		//			length *= 0.05;				//fallback

		if (length < halfscale * type->minstretch)
			length = halfscale * type->minstretch;

		VectorMA (p->org, -length, movedir, o2);
		VectorSubtract (r_refdef.vieworg, o2, v);
		CrossProduct (v, p->vel, cr);
		VectorNormalize (cr);
		VectorMA (o2, -p->scale / 2, cr, cl_curstrisvert[cl_numstrisvert + 0].position);
		VectorMA (o2, p->scale / 2, cr, cl_curstrisvert[cl_numstrisvert + 1].position);

		VectorMA (p->org, length, movedir, o2);
	}

	VectorSubtract (r_refdef.vieworg, o2, v);
	CrossProduct (v, p->vel, cr);
	VectorNormalize (cr);

	VectorMA (o2, p->scale * 0.5, cr, cl_curstrisvert[cl_numstrisvert + 2].position);
	VectorMA (o2, -p->scale * 0.5, cr, cl_curstrisvert[cl_numstrisvert + 3].position);

	if (cl_numstrisidx + 6 > cl_maxstrisidx[current_buffer_index])
		ReallocateIndexBuffer ();

	cl_curstrisidx[cl_numstrisidx++] = (cl_numstrisvert - t->firstvert) + 0;
	cl_curstrisidx[cl_numstrisidx++] = (cl_numstrisvert - t->firstvert) + 1;
	cl_curstrisidx[cl_numstrisidx++] = (cl_numstrisvert - t->firstvert) + 2;
	cl_curstrisidx[cl_numstrisidx++] = (cl_numstrisvert - t->firstvert) + 0;
	cl_curstrisidx[cl_numstrisidx++] = (cl_numstrisvert - t->firstvert) + 2;
	cl_curstrisidx[cl_numstrisidx++] = (cl_numstrisvert - t->firstvert) + 3;

	cl_numstrisvert += 4;

	t->numvert += 4;
	t->numidx += 6;
}

static void R_DrawParticleBeam (scenetris_t *t, beamseg_t *b, plooks_t *type)
{
	vec3_t		v;
	vec3_t		cr;
	beamseg_t  *c;
	particle_t *p;
	particle_t *q;
	float		ts;

	c = b->next;

	q = c->p;
	if (!q)
		return;
	p = b->p;

	if (cl_numstrisvert + 4 > cl_maxstrisvert[current_buffer_index])
		ReallocateVertexBuffer ();

	VectorSubtract (r_refdef.vieworg, q->org, v);
	VectorNormalize (v);
	CrossProduct (c->dir, v, cr);
	VectorNormalize (cr);
	ts = c->texture_s * q->angle + particletime * q->rotationspeed;
	Vector4ToColor (q->rgba, cl_curstrisvert[cl_numstrisvert + 0].color);
	Vector4ToColor (q->rgba, cl_curstrisvert[cl_numstrisvert + 1].color);
	Vector2Set (cl_curstrisvert[cl_numstrisvert + 0].texcoord, ts, p->t1);
	Vector2Set (cl_curstrisvert[cl_numstrisvert + 1].texcoord, ts, p->t2);
	VectorMA (q->org, -q->scale, cr, cl_curstrisvert[cl_numstrisvert + 0].position);
	VectorMA (q->org, q->scale, cr, cl_curstrisvert[cl_numstrisvert + 1].position);

	VectorSubtract (r_refdef.vieworg, p->org, v);
	VectorNormalize (v);
	CrossProduct (b->dir, v, cr); // replace with old p->dir?
	VectorNormalize (cr);
	ts = b->texture_s * p->angle + particletime * p->rotationspeed;
	Vector4ToColor (p->rgba, cl_curstrisvert[cl_numstrisvert + 2].color);
	Vector4ToColor (p->rgba, cl_curstrisvert[cl_numstrisvert + 3].color);
	Vector2Set (cl_curstrisvert[cl_numstrisvert + 2].texcoord, ts, p->t2);
	Vector2Set (cl_curstrisvert[cl_numstrisvert + 3].texcoord, ts, p->t1);
	VectorMA (p->org, p->scale, cr, cl_curstrisvert[cl_numstrisvert + 2].position);
	VectorMA (p->org, -p->scale, cr, cl_curstrisvert[cl_numstrisvert + 3].position);

	t->numvert += 4;

	if (cl_numstrisidx + 6 > cl_maxstrisidx[current_buffer_index])
		ReallocateIndexBuffer ();

	cl_curstrisidx[cl_numstrisidx++] = (cl_numstrisvert - t->firstvert) + 0;
	cl_curstrisidx[cl_numstrisidx++] = (cl_numstrisvert - t->firstvert) + 1;
	cl_curstrisidx[cl_numstrisidx++] = (cl_numstrisvert - t->firstvert) + 2;
	cl_curstrisidx[cl_numstrisidx++] = (cl_numstrisvert - t->firstvert) + 0;
	cl_curstrisidx[cl_numstrisidx++] = (cl_numstrisvert - t->firstvert) + 2;
	cl_curstrisidx[cl_numstrisidx++] = (cl_numstrisvert - t->firstvert) + 3;
	cl_numstrisvert += 4;
	t->numidx += 4;
}

static void R_AddClippedDecal (scenetris_t *t, clippeddecal_t *d, plooks_t *type)
{
	if (cl_numstrisvert + 4 > cl_maxstrisvert[current_buffer_index])
		ReallocateVertexBuffer ();

	if (d->entity > 0)
	{
		entity_t *le = CL_EntityNum (d->entity);
		if (le->angles[0] || le->angles[1] || le->angles[2])
		{ // FIXME: deal with rotated entities.
			d->die = -1;
			return;
		}
		VectorAdd (d->vertex[0], le->origin, cl_curstrisvert[cl_numstrisvert + 0].position);
		VectorAdd (d->vertex[1], le->origin, cl_curstrisvert[cl_numstrisvert + 1].position);
		VectorAdd (d->vertex[2], le->origin, cl_curstrisvert[cl_numstrisvert + 2].position);
	}
	else
	{
		VectorCopy (d->vertex[0], cl_curstrisvert[cl_numstrisvert + 0].position);
		VectorCopy (d->vertex[1], cl_curstrisvert[cl_numstrisvert + 1].position);
		VectorCopy (d->vertex[2], cl_curstrisvert[cl_numstrisvert + 2].position);
	}

	if (type->premul)
	{
		vec4_t rgba;
		vec4_t scaled_color;
		float  a = d->rgba[3];
		if (a > 1)
			a = 1;
		rgba[0] = d->rgba[0] * a;
		rgba[1] = d->rgba[1] * a;
		rgba[2] = d->rgba[2] * a;
		rgba[3] = (type->premul == 2) ? 0 : a;
		Vector4Scale (rgba, d->valpha[0], scaled_color);
		Vector4ToColor (scaled_color, cl_curstrisvert[cl_numstrisvert + 0].color);
		Vector4Scale (rgba, d->valpha[1], scaled_color);
		Vector4ToColor (scaled_color, cl_curstrisvert[cl_numstrisvert + 1].color);
		Vector4Scale (rgba, d->valpha[2], scaled_color);
		Vector4ToColor (scaled_color, cl_curstrisvert[cl_numstrisvert + 2].color);
	}
	else
	{
		vec4_t rgba;
		rgba[0] = d->rgba[0];
		rgba[1] = d->rgba[1];
		rgba[2] = d->rgba[2];
		rgba[3] = d->rgba[3] * d->valpha[0];
		Vector4ToColor (rgba, cl_curstrisvert[cl_numstrisvert + 0].color);
		rgba[3] = d->rgba[3] * d->valpha[1];
		Vector4ToColor (rgba, cl_curstrisvert[cl_numstrisvert + 1].color);
		rgba[3] = d->rgba[3] * d->valpha[2];
		Vector4ToColor (rgba, cl_curstrisvert[cl_numstrisvert + 2].color);
	}

	Vector2Copy (d->texcoords[0], cl_curstrisvert[cl_numstrisvert + 0].texcoord);
	Vector2Copy (d->texcoords[1], cl_curstrisvert[cl_numstrisvert + 1].texcoord);
	Vector2Copy (d->texcoords[2], cl_curstrisvert[cl_numstrisvert + 2].texcoord);

	if (cl_numstrisidx + 3 > cl_maxstrisidx[current_buffer_index])
		ReallocateIndexBuffer ();

	cl_curstrisidx[cl_numstrisidx++] = (cl_numstrisvert - t->firstvert) + 0;
	cl_curstrisidx[cl_numstrisidx++] = (cl_numstrisvert - t->firstvert) + 1;
	cl_curstrisidx[cl_numstrisidx++] = (cl_numstrisvert - t->firstvert) + 2;

	cl_numstrisvert += 3;

	t->numvert += 3;
	t->numidx += 3;
}

static void R_AddUnclippedDecal (scenetris_t *t, particle_t *p, plooks_t *type)
{
	float  x, y;
	vec3_t sdir, tdir;

	if (cl_numstrisvert + 4 > cl_maxstrisvert[current_buffer_index])
		ReallocateVertexBuffer ();

	if (type->premul)
	{
		vec4_t rgba;
		float  a = p->rgba[3];
		if (a > 1)
			a = 1;
		rgba[0] = p->rgba[0] * a;
		rgba[1] = p->rgba[1] * a;
		rgba[2] = p->rgba[2] * a;
		rgba[3] = (type->premul == 2) ? 0 : a;
		Vector4ToColor (rgba, cl_curstrisvert[cl_numstrisvert + 0].color);
		Vector4ToColor (rgba, cl_curstrisvert[cl_numstrisvert + 1].color);
		Vector4ToColor (rgba, cl_curstrisvert[cl_numstrisvert + 2].color);
		Vector4ToColor (rgba, cl_curstrisvert[cl_numstrisvert + 3].color);
	}
	else
	{
		Vector4ToColor (p->rgba, cl_curstrisvert[cl_numstrisvert + 0].color);
		Vector4ToColor (p->rgba, cl_curstrisvert[cl_numstrisvert + 1].color);
		Vector4ToColor (p->rgba, cl_curstrisvert[cl_numstrisvert + 2].color);
		Vector4ToColor (p->rgba, cl_curstrisvert[cl_numstrisvert + 3].color);
	}

	Vector2Set (cl_curstrisvert[cl_numstrisvert + 0].texcoord, p->s1, p->t1);
	Vector2Set (cl_curstrisvert[cl_numstrisvert + 1].texcoord, p->s1, p->t2);
	Vector2Set (cl_curstrisvert[cl_numstrisvert + 2].texcoord, p->s2, p->t2);
	Vector2Set (cl_curstrisvert[cl_numstrisvert + 3].texcoord, p->s2, p->t1);

	//	if (p->vel[1] == 1)
	{
		VectorSet (sdir, 1, 0, 0);
		VectorSet (tdir, 0, 1, 0);
	}

	if (p->angle)
	{
		x = sin (p->angle) * p->scale;
		y = cos (p->angle) * p->scale;

		cl_curstrisvert[cl_numstrisvert + 0].position[0] = p->org[0] - x * sdir[0] - y * tdir[0];
		cl_curstrisvert[cl_numstrisvert + 0].position[1] = p->org[1] - x * sdir[1] - y * tdir[1];
		cl_curstrisvert[cl_numstrisvert + 0].position[2] = p->org[2] - x * sdir[2] - y * tdir[2];
		cl_curstrisvert[cl_numstrisvert + 1].position[0] = p->org[0] - y * sdir[0] + x * tdir[0];
		cl_curstrisvert[cl_numstrisvert + 1].position[1] = p->org[1] - y * sdir[1] + x * tdir[1];
		cl_curstrisvert[cl_numstrisvert + 1].position[2] = p->org[2] - y * sdir[2] + x * tdir[2];
		cl_curstrisvert[cl_numstrisvert + 2].position[0] = p->org[0] + x * sdir[0] + y * tdir[0];
		cl_curstrisvert[cl_numstrisvert + 2].position[1] = p->org[1] + x * sdir[1] + y * tdir[1];
		cl_curstrisvert[cl_numstrisvert + 2].position[2] = p->org[2] + x * sdir[2] + y * tdir[2];
		cl_curstrisvert[cl_numstrisvert + 3].position[0] = p->org[0] + y * sdir[0] - x * tdir[0];
		cl_curstrisvert[cl_numstrisvert + 3].position[1] = p->org[1] + y * sdir[1] - x * tdir[1];
		cl_curstrisvert[cl_numstrisvert + 3].position[2] = p->org[2] + y * sdir[2] - x * tdir[2];
	}
	else
	{
		VectorMA (p->org, -p->scale, tdir, cl_curstrisvert[cl_numstrisvert + 0].position);
		VectorMA (p->org, -p->scale, sdir, cl_curstrisvert[cl_numstrisvert + 1].position);
		VectorMA (p->org, p->scale, tdir, cl_curstrisvert[cl_numstrisvert + 2].position);
		VectorMA (p->org, p->scale, sdir, cl_curstrisvert[cl_numstrisvert + 3].position);
	}

	if (cl_numstrisidx + 6 > cl_maxstrisidx[current_buffer_index])
		ReallocateIndexBuffer ();

	cl_curstrisidx[cl_numstrisidx++] = (cl_numstrisvert - t->firstvert) + 0;
	cl_curstrisidx[cl_numstrisidx++] = (cl_numstrisvert - t->firstvert) + 1;
	cl_curstrisidx[cl_numstrisidx++] = (cl_numstrisvert - t->firstvert) + 2;
	cl_curstrisidx[cl_numstrisidx++] = (cl_numstrisvert - t->firstvert) + 0;
	cl_curstrisidx[cl_numstrisidx++] = (cl_numstrisvert - t->firstvert) + 2;
	cl_curstrisidx[cl_numstrisidx++] = (cl_numstrisvert - t->firstvert) + 3;

	cl_numstrisvert += 4;

	t->numvert += 4;
	t->numidx += 6;
}

static void R_AddTexturedParticle (scenetris_t *t, particle_t *p, plooks_t *type)
{
	float scale, x, y;

	if (cl_numstrisvert + 4 > cl_maxstrisvert[current_buffer_index])
		ReallocateVertexBuffer ();

	if (type->scalefactor == 1)
		scale = p->scale * 0.25;
	else
	{
		scale = (p->org[0] - r_origin[0]) * vpn[0] + (p->org[1] - r_origin[1]) * vpn[1] + (p->org[2] - r_origin[2]) * vpn[2];
		scale = (scale * p->scale) * (type->invscalefactor) + p->scale * (type->scalefactor * 250);
		if (scale < 20)
			scale = 0.25;
		else
			scale = 0.25 + scale * 0.001;
	}

	if (type->premul)
	{
		vec4_t rgba;
		float  a = p->rgba[3];
		if (a > 1)
			a = 1;
		rgba[0] = p->rgba[0] * a;
		rgba[1] = p->rgba[1] * a;
		rgba[2] = p->rgba[2] * a;
		rgba[3] = (type->premul == 2) ? 0 : a;
		Vector4ToColor (rgba, cl_curstrisvert[cl_numstrisvert + 0].color);
		Vector4ToColor (rgba, cl_curstrisvert[cl_numstrisvert + 1].color);
		Vector4ToColor (rgba, cl_curstrisvert[cl_numstrisvert + 2].color);
		Vector4ToColor (rgba, cl_curstrisvert[cl_numstrisvert + 3].color);
	}
	else
	{
		Vector4ToColor (p->rgba, cl_curstrisvert[cl_numstrisvert + 0].color);
		Vector4ToColor (p->rgba, cl_curstrisvert[cl_numstrisvert + 1].color);
		Vector4ToColor (p->rgba, cl_curstrisvert[cl_numstrisvert + 2].color);
		Vector4ToColor (p->rgba, cl_curstrisvert[cl_numstrisvert + 3].color);
	}

	Vector2Set (cl_curstrisvert[cl_numstrisvert + 0].texcoord, p->s1, p->t1);
	Vector2Set (cl_curstrisvert[cl_numstrisvert + 1].texcoord, p->s1, p->t2);
	Vector2Set (cl_curstrisvert[cl_numstrisvert + 2].texcoord, p->s2, p->t2);
	Vector2Set (cl_curstrisvert[cl_numstrisvert + 3].texcoord, p->s2, p->t1);

	if (p->angle)
	{
		x = sin (p->angle) * scale;
		y = cos (p->angle) * scale;

		cl_curstrisvert[cl_numstrisvert + 0].position[0] = p->org[0] - x * pright[0] - y * pup[0];
		cl_curstrisvert[cl_numstrisvert + 0].position[1] = p->org[1] - x * pright[1] - y * pup[1];
		cl_curstrisvert[cl_numstrisvert + 0].position[2] = p->org[2] - x * pright[2] - y * pup[2];
		cl_curstrisvert[cl_numstrisvert + 1].position[0] = p->org[0] - y * pright[0] + x * pup[0];
		cl_curstrisvert[cl_numstrisvert + 1].position[1] = p->org[1] - y * pright[1] + x * pup[1];
		cl_curstrisvert[cl_numstrisvert + 1].position[2] = p->org[2] - y * pright[2] + x * pup[2];
		cl_curstrisvert[cl_numstrisvert + 2].position[0] = p->org[0] + x * pright[0] + y * pup[0];
		cl_curstrisvert[cl_numstrisvert + 2].position[1] = p->org[1] + x * pright[1] + y * pup[1];
		cl_curstrisvert[cl_numstrisvert + 2].position[2] = p->org[2] + x * pright[2] + y * pup[2];
		cl_curstrisvert[cl_numstrisvert + 3].position[0] = p->org[0] + y * pright[0] - x * pup[0];
		cl_curstrisvert[cl_numstrisvert + 3].position[1] = p->org[1] + y * pright[1] - x * pup[1];
		cl_curstrisvert[cl_numstrisvert + 3].position[2] = p->org[2] + y * pright[2] - x * pup[2];
	}
	else
	{
		VectorMA (p->org, -scale, pup, cl_curstrisvert[cl_numstrisvert + 0].position);
		VectorMA (p->org, -scale, pright, cl_curstrisvert[cl_numstrisvert + 1].position);
		VectorMA (p->org, scale, pup, cl_curstrisvert[cl_numstrisvert + 2].position);
		VectorMA (p->org, scale, pright, cl_curstrisvert[cl_numstrisvert + 3].position);
	}

	if (cl_numstrisidx + 6 > cl_maxstrisidx[current_buffer_index])
		ReallocateIndexBuffer ();

	cl_curstrisidx[cl_numstrisidx++] = (cl_numstrisvert - t->firstvert) + 0;
	cl_curstrisidx[cl_numstrisidx++] = (cl_numstrisvert - t->firstvert) + 1;
	cl_curstrisidx[cl_numstrisidx++] = (cl_numstrisvert - t->firstvert) + 2;
	cl_curstrisidx[cl_numstrisidx++] = (cl_numstrisvert - t->firstvert) + 0;
	cl_curstrisidx[cl_numstrisidx++] = (cl_numstrisvert - t->firstvert) + 2;
	cl_curstrisidx[cl_numstrisidx++] = (cl_numstrisvert - t->firstvert) + 3;

	cl_numstrisvert += 4;

	t->numvert += 4;
	t->numidx += 6;
}

static void PScript_DrawParticleTypes (cb_context_t *cbx, float pframetime)
{
	void (*bdraw) (scenetris_t * t, beamseg_t * p, plooks_t * type);
	void (*tdraw) (scenetris_t * t, particle_t * p, plooks_t * type);

	vec3_t			oldorg;
	vec3_t			stop, normal;
	part_type_t	   *type, *lastvalidtype;
	particle_t	   *p, *kill;
	clippeddecal_t *d, *dkill;
	ramp_t		   *ramp;
	float			grav;
	vec3_t			friction;
	scenetris_t	   *scenetri;
	float			dist;
	particle_t	   *kill_list, *kill_first; // the kill list is to stop particles from being freed and reused whilst still in this loop
											// which is bad because beams need to find out when particles died. Reuse can do wierd things.
											// remember that they're not drawn instantly either.
	beamseg_t	   *b, *bkill;

	int			 traces = r_particle_tracelimit.value;
	int			 rampind;
	static float flurrytime;
	qboolean	 doflurry;
	int			 batchflags;
	unsigned int i, o;

	if (r_plooksdirty)
	{
		int j, k;

		pe_default = PScript_FindParticleType ("PE_DEFAULT");
		pe_size2 = PScript_FindParticleType ("PE_SIZE2");
		pe_size3 = PScript_FindParticleType ("PE_SIZE3");
		pe_defaulttrail = PScript_FindParticleType ("PE_DEFAULTTRAIL");

		for (j = 0; j < numparticletypes; j++)
		{
			// set the fallback
			part_type[j].slooks = &part_type[j].looks;
			for (k = j - 1; k-- > 0;)
			{
				if (!memcmp (&part_type[j].looks, &part_type[k].looks, sizeof (plooks_t)))
				{
					part_type[j].slooks = part_type[k].slooks;
					break;
				}
			}
		}
		r_plooksdirty = false;
		CL_RegisterParticles ();
		PScript_RecalculateSkyTris ();
	}

	VectorScale (vup, 1.5, pup);
	VectorScale (vright, 1.5, pright);

	kill_list = kill_first = NULL;

	flurrytime -= pframetime;
	if (flurrytime < 0)
	{
		doflurry = true;
		flurrytime = 0.1 + frandom () * 0.3;
	}
	else
		doflurry = false;

	if (!free_decals)
	{
		// mark some as dead, so we can keep spawning new ones next frame.
		for (i = 0; i < 256; i++)
		{
			decals[r_decalrecycle].die = -1;
			if (++r_decalrecycle >= r_numdecals)
				r_decalrecycle = 0;
		}
	}
	if (!free_particles)
	{
		// mark some as dead.
		for (i = 0; i < 256; i++)
		{
			particles[r_particlerecycle].die = -1;
			if (++r_particlerecycle >= r_numparticles)
				r_particlerecycle = 0;
		}
	}

	for (type = part_run_list, lastvalidtype = NULL; type != NULL; type = type->nexttorun)
	{
		if (type->clippeddecals)
		{
			if (cl_numstris && cl_stris[cl_numstris - 1].texture == type->looks.texture && cl_stris[cl_numstris - 1].blendmode == type->looks.blendmode &&
				cl_stris[cl_numstris - 1].beflags == 0)
				scenetri = &cl_stris[cl_numstris - 1];
			else
			{
				if (cl_numstris == cl_maxstris)
				{
					cl_maxstris += 8;
					cl_stris = Mem_Realloc (cl_stris, sizeof (*cl_stris) * cl_maxstris);
				}
				scenetri = &cl_stris[cl_numstris++];
				scenetri->texture = type->looks.texture;
				scenetri->blendmode = type->looks.blendmode;
				scenetri->beflags = 0;
				scenetri->firstidx = cl_numstrisidx;
				scenetri->firstvert = cl_numstrisvert;
				scenetri->numvert = 0;
				scenetri->numidx = 0;
			}

			for (;;)
			{
				dkill = type->clippeddecals;
				if (dkill && dkill->die < particletime)
				{
					type->clippeddecals = dkill->next;
					dkill->next = free_decals;
					free_decals = dkill;
					continue;
				}
				break;
			}
			for (d = type->clippeddecals; d; d = d->next)
			{
				for (;;)
				{
					dkill = d->next;
					if (dkill && dkill->die < particletime)
					{
						d->next = dkill->next;
						dkill->next = free_decals;
						free_decals = dkill;
						continue;
					}
					break;
				}

				if (d->die - particletime <= type->die)
				{
					switch (type->rampmode)
					{
					case RAMP_NEAREST:
						rampind = (int)(type->rampindexes * (type->die - (d->die - particletime)) / type->die);
						if (rampind >= type->rampindexes)
							rampind = type->rampindexes - 1;
						ramp = type->ramp + rampind;
						VectorCopy (ramp->rgb, d->rgba);
						d->rgba[3] = ramp->alpha;
						break;
					case RAMP_LERP:
					{
						float frac = (type->rampindexes * (type->die - (d->die - particletime)) / type->die);
						int	  s1, s2;
						s1 = frac;
						s2 = s1 + 1;
						if (s1 > type->rampindexes - 1)
							s1 = type->rampindexes - 1;
						if (s2 > type->rampindexes - 1)
							s2 = type->rampindexes - 1;
						frac -= s1;
						VectorInterpolate (type->ramp[s1].rgb, frac, type->ramp[s2].rgb, d->rgba);
						FloatInterpolate (type->ramp[s1].alpha, frac, type->ramp[s2].alpha, d->rgba[3]);
					}
					break;
					case RAMP_DELTA: // particle ramps
						ramp = type->ramp + (int)(type->rampindexes * (type->die - (d->die - particletime)) / type->die);
						VectorMA (d->rgba, pframetime, ramp->rgb, d->rgba);
						d->rgba[3] -= pframetime * ramp->alpha;
						break;
					case RAMP_NONE: // particle changes acording to it's preset properties.
						if (particletime < (d->die - type->die + type->rgbchangetime))
						{
							d->rgba[0] += pframetime * type->rgbchange[0];
							d->rgba[1] += pframetime * type->rgbchange[1];
							d->rgba[2] += pframetime * type->rgbchange[2];
						}
						d->rgba[3] += pframetime * type->alphachange;
					}
				}

				if (cl_numstrisvert - scenetri->firstvert >= MAX_INDICES - 6)
				{
					// generate a new mesh if the old one overflowed. yay smc...
					if (cl_numstris == cl_maxstris)
					{
						cl_maxstris += 8;
						cl_stris = Mem_Realloc (cl_stris, sizeof (*cl_stris) * cl_maxstris);
					}
					scenetri = &cl_stris[cl_numstris++];
					scenetri->texture = scenetri[-1].texture;
					scenetri->blendmode = scenetri[-1].blendmode;
					scenetri->beflags = scenetri[-1].beflags;
					scenetri->firstidx = cl_numstrisidx;
					scenetri->firstvert = cl_numstrisvert;
					scenetri->numvert = 0;
					scenetri->numidx = 0;
				}
				R_AddClippedDecal (scenetri, d, type->slooks);
			}
		}

		bdraw = NULL;
		tdraw = NULL;
		batchflags = 0;

		// set drawing methods by type and cvars and hope branch
		// prediction takes care of the rest
		switch (type->looks.type)
		{
		default:
		case PT_INVISIBLE:
			break;
		case PT_BEAM:
			bdraw = R_DrawParticleBeam;
			break;
		case PT_CDECAL:
			break;
		case PT_UDECAL:
			tdraw = R_AddUnclippedDecal;
			break;
		case PT_NORMAL:
			tdraw = R_AddTexturedParticle;
			break;
		case PT_SPARK:
			tdraw = R_AddLineSparkParticle;
			batchflags = BEF_LINES;
			break;
		case PT_SPARKFAN:
			tdraw = R_AddFanSparkParticle;
			break;
		case PT_TEXTUREDSPARK:
			tdraw = R_AddTSparkParticle;
			break;
		}

		if (cl_numstris && cl_stris[cl_numstris - 1].texture == type->looks.texture && cl_stris[cl_numstris - 1].blendmode == type->looks.blendmode &&
			cl_stris[cl_numstris - 1].beflags == batchflags)
			scenetri = &cl_stris[cl_numstris - 1];
		else
		{
			if (cl_numstris == cl_maxstris)
			{
				cl_maxstris += 8;
				cl_stris = Mem_Realloc (cl_stris, sizeof (*cl_stris) * cl_maxstris);
			}
			scenetri = &cl_stris[cl_numstris++];
			scenetri->texture = type->looks.texture;
			scenetri->blendmode = type->looks.blendmode;
			scenetri->beflags = batchflags;
			scenetri->firstidx = cl_numstrisidx;
			scenetri->firstvert = cl_numstrisvert;
			scenetri->numvert = 0;
			scenetri->numidx = 0;
		}

		if (!type->die)
		{
			while ((p = type->particles))
			{
				if (scenetri && tdraw)
				{
					if (cl_numstrisvert - scenetri->firstvert >= MAX_INDICES - 6)
					{
						// generate a new mesh if the old one overflowed. yay smc...
						if (cl_numstris == cl_maxstris)
						{
							cl_maxstris += 8;
							cl_stris = Mem_Realloc (cl_stris, sizeof (*cl_stris) * cl_maxstris);
						}
						scenetri = &cl_stris[cl_numstris++];
						scenetri->texture = scenetri[-1].texture;
						scenetri->blendmode = scenetri[-1].blendmode;
						scenetri->beflags = scenetri[-1].beflags;
						scenetri->firstidx = cl_numstrisidx;
						scenetri->firstvert = cl_numstrisvert;
						scenetri->numvert = 0;
						scenetri->numidx = 0;
					}
					tdraw (scenetri, p, type->slooks);
				}

				// make sure emitter runs at least once
				if (type->emit >= 0 && type->emitstart <= 0)
					PScript_RunParticleEffectState (p->org, p->vel, 1, type->emit, NULL);

				type->particles = p->next;
				p->next = kill_list;
				kill_list = p;
				if (!kill_first) // branch here is probably faster than list traversal later
					kill_first = p;
			}

			if (type->beams)
			{
				b = type->beams;
			}

			while ((b = type->beams) && (b->flags & BS_DEAD))
			{
				type->beams = b->next;
				b->next = free_beams;
				free_beams = b;
			}

			while (b)
			{
				if (!(b->flags & BS_NODRAW))
				{
					// no BS_NODRAW implies b->next != NULL
					// BS_NODRAW should imply b->next == NULL or b->next->flags & BS_DEAD
					VectorCopy (b->next->p->org, stop);
					VectorCopy (b->p->org, oldorg);
					VectorSubtract (stop, oldorg, b->next->dir);
					VectorNormalize (b->next->dir);
					if (bdraw)
						bdraw (scenetri, b, type->slooks);
				}

				// clean up dead entries ahead of current
				for (;;)
				{
					bkill = b->next;
					if (bkill && (bkill->flags & BS_DEAD))
					{
						b->next = bkill->next;
						bkill->next = free_beams;
						free_beams = bkill;
						continue;
					}
					break;
				}

				b->flags |= BS_DEAD;
				b = b->next;
			}

			goto endtype;
		}

		// kill off early ones.
		if (type->emittime < 0)
		{
			for (;;)
			{
				kill = type->particles;
				if (kill && kill->die < particletime)
				{
					PScript_DelinkTrailstate (&kill->state.trailstate);
					type->particles = kill->next;
					kill->next = kill_list;
					kill_list = kill;
					if (!kill_first)
						kill_first = kill;
					continue;
				}
				break;
			}
		}
		else
		{
			for (;;)
			{
				kill = type->particles;
				if (kill && kill->die < particletime)
				{
					type->particles = kill->next;
					kill->next = kill_list;
					kill_list = kill;
					if (!kill_first)
						kill_first = kill;
					continue;
				}
				break;
			}
		}

		grav = type->gravity * pframetime;
		friction[0] = 1 - type->friction[0] * pframetime;
		friction[1] = 1 - type->friction[1] * pframetime;
		friction[2] = 1 - type->friction[2] * pframetime;

		for (p = type->particles; p; p = p->next)
		{
			if (type->emittime < 0)
			{
				for (;;)
				{
					kill = p->next;
					if (kill && kill->die < particletime)
					{
						PScript_DelinkTrailstate (&kill->state.trailstate);
						p->next = kill->next;
						kill->next = kill_list;
						kill_list = kill;
						if (!kill_first)
							kill_first = kill;
						continue;
					}
					break;
				}
			}
			else
			{
				for (;;)
				{
					kill = p->next;
					if (kill && kill->die < particletime)
					{
						p->next = kill->next;
						kill->next = kill_list;
						kill_list = kill;
						if (!kill_first)
							kill_first = kill;
						continue;
					}
					break;
				}
			}

			VectorCopy (p->org, oldorg);
			if (type->flags & PT_VELOCITY)
			{
				p->org[0] += p->vel[0] * pframetime;
				p->org[1] += p->vel[1] * pframetime;
				p->org[2] += p->vel[2] * pframetime;
				p->vel[2] -= grav;
				if (type->flags & PT_FRICTION)
				{
					p->vel[0] *= friction[0];
					p->vel[1] *= friction[1];
					p->vel[2] *= friction[2];
				}
				if (type->flurry && doflurry)
				{ // these should probably be partially synced,
					p->vel[0] += crandom () * type->flurry;
					p->vel[1] += crandom () * type->flurry;
				}
			}

			p->angle += p->rotationspeed * pframetime;

			switch (type->rampmode)
			{
			case RAMP_NEAREST:
				rampind = (int)(type->rampindexes * (type->die - (p->die - particletime)) / type->die);
				if (rampind >= type->rampindexes)
					rampind = type->rampindexes - 1;
				ramp = type->ramp + rampind;
				VectorCopy (ramp->rgb, p->rgba);
				p->rgba[3] = ramp->alpha;
				p->scale = ramp->scale;
				break;
			case RAMP_LERP:
			{
				float frac = (type->rampindexes * (type->die - (p->die - particletime)) / type->die);
				int	  s1, s2;
				s1 = frac;
				s2 = s1 + 1;
				if (s1 > type->rampindexes - 1)
					s1 = type->rampindexes - 1;
				if (s2 > type->rampindexes - 1)
					s2 = type->rampindexes - 1;
				frac -= s1;
				VectorInterpolate (type->ramp[s1].rgb, frac, type->ramp[s2].rgb, p->rgba);
				FloatInterpolate (type->ramp[s1].alpha, frac, type->ramp[s2].alpha, p->rgba[3]);
				FloatInterpolate (type->ramp[s1].scale, frac, type->ramp[s2].scale, p->scale);
			}
			break;
			case RAMP_DELTA: // particle ramps
				rampind = (int)(type->rampindexes * (type->die - (p->die - particletime)) / type->die);
				if (rampind >= type->rampindexes)
					rampind = type->rampindexes - 1;
				ramp = type->ramp + rampind;
				VectorMA (p->rgba, pframetime, ramp->rgb, p->rgba);
				p->rgba[3] -= pframetime * ramp->alpha;
				p->scale += pframetime * ramp->scale;
				break;
			case RAMP_NONE: // particle changes acording to it's preset properties.
				if (particletime < (p->die - type->die + type->rgbchangetime))
				{
					p->rgba[0] += pframetime * type->rgbchange[0];
					p->rgba[1] += pframetime * type->rgbchange[1];
					p->rgba[2] += pframetime * type->rgbchange[2];
				}
				p->rgba[3] += pframetime * type->alphachange;
				p->scale += pframetime * type->scaledelta;
			}

			if (type->emit >= 0)
			{
				if (type->emittime < 0)
					PScript_ParticleTrail (oldorg, p->org, type->emit, pframetime, 0, NULL, &p->state.trailstate);
				else if (p->state.nextemit < particletime)
				{
					p->state.nextemit = particletime + type->emittime + frandom () * type->emitrand;
					PScript_RunParticleEffectState (p->org, p->vel, 1, type->emit, NULL);
				}
			}

			if (type->cliptype >= 0 && r_bouncysparks.value)
			{
				VectorSubtract (p->org, p->oldorg, stop);
				if (!type->clipbounce || DotProduct (stop, stop) > 10 * 10)
				{
					int e;
					if (traces-- > 0 && CL_TraceLine (p->oldorg, p->org, stop, normal, &e) < 1)
					{
						if (type->clipbounce < 0)
						{
							p->die = -1;
#ifdef USE_DECALS
							if (type->clipbounce == -2)
							{ // this type of particle splatters itself as a decal when it hits a wall.
								decalctx_t ctx;
								float	   m;
								vec3_t	   vec = {0.5, 0.5, 0.431};
								qmodel_t  *model;

								ctx.entity = e;
								if (!ctx.entity)
								{
									model = cl.worldmodel;
									VectorCopy (p->org, ctx.center);
								}
								else if (e)
								{ // this trace hit a door or something.
									entity_t *ent = CL_EntityNum (e);
									model = ent->model;
									VectorSubtract (p->org, ent->origin, ctx.center);
									// FIXME: rotate center+normal around entity.
								}
								else
									continue; // err, no idea.

								VectorScale (normal, -1, ctx.normal);
								VectorNormalize (ctx.normal);

								VectorNormalize (vec);
								CrossProduct (ctx.normal, vec, ctx.tangent1);
								RotatePointAroundVector (ctx.tangent2, ctx.normal, ctx.tangent1, frandom () * 360);
								CrossProduct (ctx.normal, ctx.tangent2, ctx.tangent1);

								VectorNormalize (ctx.tangent1);
								VectorNormalize (ctx.tangent2);

								ctx.ptype = type;
								ctx.scale1 = type->s2 - type->s1;
								ctx.bias1 = type->s1 + (ctx.scale1 * 0.5);
								ctx.scale2 = type->t2 - type->t1;
								ctx.bias2 = type->t1 + (ctx.scale2 * 0.5);
								m = p->scale * (1.5 + frandom () * 0.5) * 0.5; // decals should be a little bigger, for some reason.
								ctx.scale0 = 2.0 / m;
								ctx.scale1 /= m;
								ctx.scale2 /= m;

								// inserts decals through a callback.
								Mod_ClipDecal (
									model, ctx.center, ctx.normal, ctx.tangent2, ctx.tangent1, m, type->surfflagmask, type->surfflagmatch, PScript_AddDecals,
									&ctx);
							}
#endif
							continue;
						}
						else if (part_type + type->cliptype == type)
						{										// bounce
							dist = DotProduct (p->vel, normal); // * (-1-(rand()/(float)0x7fff)/2);
							dist *= -type->clipbounce;
							VectorMA (p->vel, dist, normal, p->vel);
							VectorCopy (stop, p->org);

							if (!*type->texname && VectorLength (p->vel) < 1000 * pframetime && type->looks.type == PT_NORMAL)
							{
								p->die = -1;
								continue;
							}
						}
						else
						{
							p->die = -1;
							VectorNormalize (p->vel);

							if (type->clipbounce)
							{
								VectorScale (normal, type->clipbounce, normal);
								PScript_RunParticleEffectState (stop, normal, type->clipcount / part_type[type->cliptype].count, type->cliptype, NULL);
							}
							else
								PScript_RunParticleEffectState (stop, p->vel, type->clipcount / part_type[type->cliptype].count, type->cliptype, NULL);
							continue;
						}
					}
					VectorCopy (p->org, p->oldorg);
				}
			}
			if (scenetri && tdraw)
			{
				if (cl_numstrisvert - scenetri->firstvert >= MAX_INDICES - 6)
				{
					// generate a new mesh if the old one overflowed. yay smc...
					if (cl_numstris == cl_maxstris)
					{
						cl_maxstris += 8;
						cl_stris = Mem_Realloc (cl_stris, sizeof (*cl_stris) * cl_maxstris);
					}
					scenetri = &cl_stris[cl_numstris++];
					scenetri->texture = scenetri[-1].texture;
					scenetri->blendmode = scenetri[-1].blendmode;
					scenetri->beflags = scenetri[-1].beflags;
					scenetri->firstidx = cl_numstrisidx;
					scenetri->firstvert = cl_numstrisvert;
					scenetri->numvert = 0;
					scenetri->numidx = 0;
				}
				tdraw (scenetri, p, type->slooks);
			}
		}

		// beams are dealt with here

		// kill early entries
		for (;;)
		{
			bkill = type->beams;
			if (bkill && (bkill->flags & BS_DEAD || bkill->p->die < particletime) && !(bkill->flags & BS_LASTSEG))
			{
				type->beams = bkill->next;
				bkill->next = free_beams;
				free_beams = bkill;
				continue;
			}
			break;
		}

		b = type->beams;
		if (b)
		{
			for (;;)
			{
				if (b->next)
				{
					// mark dead entries
					if (b->flags & (BS_LASTSEG | BS_DEAD | BS_NODRAW))
					{
						// kill some more dead entries
						for (;;)
						{
							bkill = b->next;
							if (bkill && (bkill->flags & BS_DEAD) && !(bkill->flags & BS_LASTSEG))
							{
								b->next = bkill->next;
								bkill->next = free_beams;
								free_beams = bkill;
								continue;
							}
							break;
						}

						if (!bkill) // have to check so we don't hit NULL->next
							continue;
					}
					else
					{
						if (!(b->next->flags & BS_DEAD))
						{
							VectorCopy (b->next->p->org, stop);
							VectorCopy (b->p->org, oldorg);
							VectorSubtract (stop, oldorg, b->next->dir);
							VectorNormalize (b->next->dir);
							if (bdraw)
							{
								VectorAdd (stop, oldorg, stop);
								VectorScale (stop, 0.5, stop);
							}
						}

						if (b->p->die < particletime)
							b->flags |= BS_DEAD;
					}
				}
				else
				{
					if (b->p->die < particletime) // end of the list check
						b->flags |= BS_DEAD;

					break;
				}

				if (b->p->die < particletime)
					b->flags |= BS_DEAD;

				b = b->next;
			}
		}

	endtype:

		// delete from run list if necessary
		if (!type->particles && !type->beams && !type->clippeddecals)
		{
			if (!lastvalidtype)
				part_run_list = type->nexttorun;
			else if (lastvalidtype->nexttorun == type)
				lastvalidtype->nexttorun = type->nexttorun;
			else
				lastvalidtype->nexttorun->nexttorun = type->nexttorun;
			type->state &= ~PS_INRUNLIST;
		}
		else
			lastvalidtype = type;
	}

	// lazy delete for particles is done here
	if (kill_list)
	{
		kill_first->next = free_particles;
		free_particles = kill_list;
	}

	particletime += pframetime;

	if (!cl_numstris)
		return;

	R_BeginDebugUtilsLabel (cbx, "FTE Particles");
	Fog_DisableGFog (cbx);

	for (o = 0; o < 3; o++)
	{
		static int blend_modes_order[] = {1, 1, 2, 2, 0, 0, 0, 2};
		for (i = 0; i < cl_numstris; i++)
		{
			scenetris_t *tris = &cl_stris[i];
			const int	 blend_mode = tris->blendmode;
			if (blend_modes_order[blend_mode] != o)
				continue;
			const qboolean draw_lines = ((tris->beflags & BEF_LINES) != 0);
			if (!vulkan_globals.non_solid_fill && draw_lines)
				continue; // Can't draw lines
			if (tris->numidx == 0)
				continue;

			const vulkan_pipeline_t pipeline = vulkan_globals.fte_particle_pipelines[blend_mode + (draw_lines ? 8 : 0)];
			R_BindPipeline (cbx, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
			gltexture_t *tex = (tris->beflags & BEF_LINES) ? whitetexture : tris->texture;

			const int		   num_indices = tris->numidx;
			const VkDeviceSize vertex_buffer_offset = 0;
			vulkan_globals.vk_cmd_bind_index_buffer (cbx->cb, index_buffers[current_buffer_index], 0, VK_INDEX_TYPE_UINT16);
			vulkan_globals.vk_cmd_bind_vertex_buffers (cbx->cb, 0, 1, &vertex_buffers[current_buffer_index], &vertex_buffer_offset);
			vulkan_globals.vk_cmd_bind_descriptor_sets (cbx->cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.layout.handle, 0, 1, &tex->descriptor_set, 0, NULL);
			vulkan_globals.vk_cmd_draw_indexed (cbx->cb, num_indices, 1, tris->firstidx, tris->firstvert, 0);
		}
	}
	R_EndDebugUtilsLabel (cbx);
}

/*
===============
PScript_DrawParticles
===============
*/
void PScript_DrawParticles (cb_context_t *cbx)
{
	int			 i;
	entity_t	*ent;
	vec3_t		 axis[3];
	float		 pframetime;
	static float oldtime;

	pframetime = cl.time - oldtime;
	if (pframetime < 0)
		pframetime = 0;
	if (pframetime > 1)
		pframetime = 1;
	oldtime = cl.time;

	current_buffer_index = (current_buffer_index + 1) % 2;
	cl_numstris = 0;
	cl_numstrisvert = 0;
	cl_numstrisidx = 0;
	cl_curstrisvert = cl_strisvert[current_buffer_index];
	cl_curstrisidx = cl_strisidx[current_buffer_index];

	if (!r_particles.value)
		return;

	if (r_part_rain.value && r_fteparticles.value)
	{
		for (i = 0; i < cl.num_entities; i++)
		{
			ent = &cl.entities[i];
			if (!ent->model || ent->model->needload)
				continue;
			if (!ent->model->skytris)
				continue;
			AngleVectors (ent->angles, axis[0], axis[1], axis[2]);
			// this timer, as well as the per-tri timer, are unable to deal with certain rates+sizes. it would be good to fix that...
			// it would also be nice to do mdls too...
			P_AddRainParticles (ent->model, axis, ent->origin, pframetime);
		}
	}

	PScript_DrawParticleTypes (cbx, pframetime);
}

/*
===============
R_DrawParticles_ShowTris
===============
*/
void PScript_DrawParticles_ShowTris (cb_context_t *cbx)
{
	if (r_showtris.value == 1)
		R_BindPipeline (cbx, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_globals.showtris_pipeline);
	else
		R_BindPipeline (cbx, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_globals.showtris_depth_test_pipeline);

	for (unsigned int i = 0; i < cl_numstris; i++)
	{
		scenetris_t		  *tris = &cl_stris[i];
		const int		   num_indices = tris->numidx;
		const VkDeviceSize vertex_buffer_offset = 0;
		vulkan_globals.vk_cmd_bind_index_buffer (cbx->cb, index_buffers[current_buffer_index], 0, VK_INDEX_TYPE_UINT16);
		vulkan_globals.vk_cmd_bind_vertex_buffers (cbx->cb, 0, 1, &vertex_buffers[current_buffer_index], &vertex_buffer_offset);
		vulkan_globals.vk_cmd_draw_indexed (cbx->cb, num_indices, 1, tris->firstidx, tris->firstvert, 0);
	}
}

#endif
#endif
