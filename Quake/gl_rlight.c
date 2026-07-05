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
// r_light.c

#include "quakedef.h"

int r_dlightframecount;

// dlight origins in the space of the model currently having its lightmaps built: world space for the world model,
// entity space for brush models (R_DrawBrushModel updates these while marking its surfaces)
vec3_t lightmap_dlight_origins[MAX_DLIGHTS];

extern cvar_t r_flatlightstyles; // johnfitz
extern cvar_t r_lerplightstyles;
extern cvar_t r_gpulightmapupdate;

/*
==================
R_AnimateLight
==================
*/
void R_AnimateLight (void)
{
	int	   i, j, k, n;
	double f;

	//
	// light animations
	// 'm' is normal light, 'a' is no light, 'z' is double bright
	i = f = cl.time * 10;
	for (j = 0; j < MAX_LIGHTSTYLES; j++)
	{
		if (!cl_lightstyle[j].length)
		{
			d_lightstylevalue[j] = 256; // should be 264 ?
			continue;
		}
		// johnfitz -- r_flatlightstyles
		if (r_flatlightstyles.value == 2)
			k = n = cl_lightstyle[j].peak - 'a';
		else if (r_flatlightstyles.value == 1 || !r_dynamic.value)
			k = n = cl_lightstyle[j].average - 'a';
		else
		{
			k = cl_lightstyle[j].map[i % cl_lightstyle[j].length] - 'a';
			n = cl_lightstyle[j].map[(i + 1) % cl_lightstyle[j].length] - 'a';
		}
		if (!r_gpulightmapupdate.value || !r_lerplightstyles.value || (r_lerplightstyles.value < 2 && abs (n - k) >= ('m' - 'a') / 2))
			n = k;
		d_lightstylevalue[j] = (k + (n - k) * (f - i)) * 22;
		// johnfitz
	}
}

/*
=============================================================================

DYNAMIC LIGHTS

=============================================================================
*/

/*
=============
R_MarkLights -- johnfitz -- rewritten to use LordHavoc's lighting speedup
=============
*/
void R_MarkLights (dlight_t *light, int num, mnode_t *node)
{
	mplane_t	*splitplane;
	msurface_t	*surf;
	vec3_t		 impact;
	float		 dist, l, maxdist;
	unsigned int i;
	int			 j, s, t;

start:

	if (node->contents < 0)
		return;

	splitplane = node->plane;
	if (splitplane->type < 3)
		dist = light->origin[splitplane->type] - splitplane->dist;
	else
		dist = DotProduct (light->origin, splitplane->normal) - splitplane->dist;

	if (dist > light->radius)
	{
		node = node->children[0];
		goto start;
	}
	if (dist < -light->radius)
	{
		node = node->children[1];
		goto start;
	}

	maxdist = light->radius * light->radius;
	// mark the polygons
	surf = cl.worldmodel->surfaces + node->firstsurface;
	for (i = 0; i < node->numsurfaces; i++, surf++)
	{
		for (j = 0; j < 3; j++)
			impact[j] = light->origin[j] - surf->plane->normal[j] * dist;
		// clamp center of light to corner and check brightness
		l = DotProduct (impact, surf->texinfo->vecs[0]) + surf->texinfo->vecs[0][3] - surf->texturemins[0];
		s = l + 0.5;
		if (s < 0)
			s = 0;
		else if (s > surf->extents[0])
			s = surf->extents[0];
		s = l - s;
		l = DotProduct (impact, surf->texinfo->vecs[1]) + surf->texinfo->vecs[1][3] - surf->texturemins[1];
		t = l + 0.5;
		if (t < 0)
			t = 0;
		else if (t > surf->extents[1])
			t = surf->extents[1];
		t = l - t;
		// compare to minimum light
		if ((s * s + t * t + dist * dist) < maxdist)
		{
			if (surf->dlightframe != r_dlightframecount) // not dynamic until now
			{
				surf->dlightbits[num >> 5] = 1U << (num & 31);
				surf->dlightframe = r_dlightframecount;
			}
			else // already dynamic
				surf->dlightbits[num >> 5] |= 1U << (num & 31);
		}
	}

	if (node->children[0]->contents >= 0)
		R_MarkLights (light, num, node->children[0]);
	if (node->children[1]->contents >= 0)
		R_MarkLights (light, num, node->children[1]);
}

/*
=============
R_PushDlights
=============
*/
void R_PushDlights (void)
{
	int		  i;
	dlight_t *l;

	r_dlightframecount = r_framecount;

	l = cl_dlights;

	for (i = 0; i < MAX_DLIGHTS; i++, l++)
	{
		if (l->die < cl.time || !l->radius)
			continue;
		VectorCopy (l->origin, lightmap_dlight_origins[i]);
		R_MarkLights (l, i, cl.worldmodel->nodes);
	}
}

/*
=============================================================================

2021 RERELEASE DYNAMIC LIGHT ENTITIES

Dimension of the Machine places "dynamiclight" entities for shadow casting
dynamic lights, e.g. behind the fans in hub and start. Their QC spawn function
is empty ("No special functionality, just to hold dynamic shadowcasting lights
for KEX"), the KEX engine reads them directly from the entity lump, so do the
same and turn them into persistent dlights.

The KEX shading math is public: QuakeEX.kpf ships the shader source, and
ComputeLightShading/ComputeSpotLight in progs/includes/QuakeClusteredShading.inc
add color * intensity * (range - dist) / range * saturate(N.L), spotlights
scaled by a linear falloff from cone axis to edge, all multiplied by a shadow
map term. KEX only renders them while its shadow system is enabled
(r_staticshadows 0 removes the lights entirely), so they are tied to the ray
traced occlusion path here as well.

The updated id1 and ctf maps also carry "_shadowlight 1" keys on info_null
entities, but those sit in areas whose lighting is fully baked into the
lightmaps (the same maps have to look right in the classic renderer). Adding
their light on top just double brightens the baked lighting, so they are
deliberately not parsed.

=============================================================================
*/

#define MAX_ENTITY_DLIGHTS 64
#define ENTITY_DLIGHT_KEY  0x40000000 // dlight key space for entity dlights, outside entity indices

extern cvar_t r_rtshadows;

// scales the intensity of the rerelease dynamiclight entities; the KEX intensity
// units don't map 1:1 onto lightmap space, so this is calibrated visually
cvar_t r_entdlightscale = {"r_entdlightscale", "1", CVAR_NONE};

typedef struct entity_dlight_s
{
	vec3_t origin;
	vec3_t color;
	float  radius;
	float  intensity;
	vec3_t cone_dir;
	float  cone_cos; // <= -1: not a spotlight
	float  start_fade_distance;
	float  end_fade_distance;
	int	   style;
} entity_dlight_t;

static entity_dlight_t entity_dlights[MAX_ENTITY_DLIGHTS];
static int			   num_entity_dlights;

/*
=============
R_ParseEntityDlights -- called at map load

Parses "dynamiclight" entities out of the entity lump. The spot direction comes
from an "angle" key or, in a second pass, the origin of the targeted entity
=============
*/
void R_ParseEntityDlights (void)
{
	char		key[128], value[4096];
	const char *data;
	char		targets[MAX_ENTITY_DLIGHTS][64];
	float		cone_angles[MAX_ENTITY_DLIGHTS];

	num_entity_dlights = 0;
	if (!cl.worldmodel || !cl.worldmodel->entities)
		return;

	const qboolean coop_game = (cl.maxclients > 1) && (cl.gametype == GAME_COOP);
	qboolean	   any_targets = false;

	data = cl.worldmodel->entities;
	while (num_entity_dlights < MAX_ENTITY_DLIGHTS)
	{
		data = COM_Parse (data);
		if (!data || com_token[0] != '{')
			break;

		entity_dlight_t *l = &entity_dlights[num_entity_dlights];
		memset (l, 0, sizeof (*l));
		l->color[0] = l->color[1] = l->color[2] = 1.0f;
		l->radius = 300.0f;
		l->intensity = 10.0f;
		l->cone_cos = -2.0f;

		qboolean is_dynamiclight = false;
		qboolean has_angle = false;
		float	 angle = 0.0f;
		float	 cone_angle = 0.0f;
		int		 spawnflags = 0;
		targets[num_entity_dlights][0] = 0;

		while (1)
		{
			data = COM_Parse (data);
			if (!data)
				return;
			if (com_token[0] == '}')
				break;
			q_strlcpy (key, com_token, sizeof (key));
			while (key[0] && key[strlen (key) - 1] == ' ') // remove trailing spaces
				key[strlen (key) - 1] = 0;
			data = COM_ParseEx (data, CPE_ALLOWTRUNC);
			if (!data)
				return;
			q_strlcpy (value, com_token, sizeof (value));

			if (!strcmp (key, "classname"))
				is_dynamiclight = !strcmp (value, "dynamiclight");
			else if (!strcmp (key, "origin"))
				sscanf (value, "%f %f %f", &l->origin[0], &l->origin[1], &l->origin[2]);
			else if (!strcmp (key, "_color"))
				sscanf (value, "%f %f %f", &l->color[0], &l->color[1], &l->color[2]);
			else if (!strcmp (key, "_shadowlightradius"))
				l->radius = atof (value);
			else if (!strcmp (key, "_shadowlightintensity"))
				l->intensity = atof (value);
			else if (!strcmp (key, "_shadowlightconeangle"))
				cone_angle = atof (value);
			else if (!strcmp (key, "_shadowlightstartfadedistance"))
				l->start_fade_distance = atof (value);
			else if (!strcmp (key, "_shadowlightendfadedistance"))
				l->end_fade_distance = atof (value);
			else if (!strcmp (key, "_shadowlightstyle"))
				l->style = CLAMP (0, atoi (value), MAX_LIGHTSTYLES - 1);
			else if (!strcmp (key, "angle"))
			{
				angle = atof (value);
				has_angle = true;
			}
			else if (!strcmp (key, "target"))
				q_strlcpy (targets[num_entity_dlights], value, sizeof (targets[0]));
			else if (!strcmp (key, "spawnflags"))
				spawnflags = atoi (value);
		}

		if (!is_dynamiclight)
			continue;
		if (coop_game && (spawnflags & 1)) // DYNAMICLIGHT_NOT_IN_COOP
			continue;

		if (has_angle && (cone_angle > 0.0f))
		{
			if (angle == -1.0f) // up
			{
				l->cone_dir[0] = l->cone_dir[1] = 0.0f;
				l->cone_dir[2] = 1.0f;
			}
			else if (angle == -2.0f) // down
			{
				l->cone_dir[0] = l->cone_dir[1] = 0.0f;
				l->cone_dir[2] = -1.0f;
			}
			else
			{
				l->cone_dir[0] = cosf (DEG2RAD (angle));
				l->cone_dir[1] = sinf (DEG2RAD (angle));
				l->cone_dir[2] = 0.0f;
			}
			// _shadowlightconeangle is the full apex angle: KEX visibly lights surfaces
			// sideways from the axis (e.g. the fan walls), impossible with a half angle read
			l->cone_cos = cosf (DEG2RAD (cone_angle));
		}
		cone_angles[num_entity_dlights] = cone_angle;
		any_targets = any_targets || (targets[num_entity_dlights][0] != 0);

		++num_entity_dlights;
	}

	if (num_entity_dlights > 0)
		Con_DPrintf ("%d entity dlights\n", num_entity_dlights);

	if (!any_targets)
		return;

	// resolve spot directions from targeted entities, "target" wins over "angle"
	data = cl.worldmodel->entities;
	while (1)
	{
		data = COM_Parse (data);
		if (!data || com_token[0] != '{')
			break;

		char   targetname[64] = "";
		vec3_t origin = {0.0f, 0.0f, 0.0f};

		while (1)
		{
			data = COM_Parse (data);
			if (!data)
				return;
			if (com_token[0] == '}')
				break;
			q_strlcpy (key, com_token, sizeof (key));
			while (key[0] && key[strlen (key) - 1] == ' ') // remove trailing spaces
				key[strlen (key) - 1] = 0;
			data = COM_ParseEx (data, CPE_ALLOWTRUNC);
			if (!data)
				return;
			q_strlcpy (value, com_token, sizeof (value));

			if (!strcmp (key, "targetname"))
				q_strlcpy (targetname, value, sizeof (targetname));
			else if (!strcmp (key, "origin"))
				sscanf (value, "%f %f %f", &origin[0], &origin[1], &origin[2]);
		}

		if (!targetname[0])
			continue;
		for (int i = 0; i < num_entity_dlights; i++)
		{
			if ((cone_angles[i] <= 0.0f) || strcmp (targets[i], targetname) != 0)
				continue;
			vec3_t dir;
			VectorSubtract (origin, entity_dlights[i].origin, dir);
			if (VectorLength (dir) > 0.0f)
			{
				VectorNormalize (dir);
				VectorCopy (dir, entity_dlights[i].cone_dir);
				entity_dlights[i].cone_cos = cosf (DEG2RAD (cone_angles[i]));
			}
		}
	}
}

/*
=============
R_UpdateEntityDlights -- called every frame, keeps the parsed entity dlights alive
=============
*/
void R_UpdateEntityDlights (void)
{
	// KEX ties these lights to its shadow system (r_staticshadows 0 removes them
	// entirely), so require the ray traced occlusion path here as well
	if (!vulkan_globals.ray_query || !r_rtshadows.value || !r_gpulightmapupdate.value)
		return;

	for (int i = 0; i < num_entity_dlights; i++)
	{
		entity_dlight_t *l = &entity_dlights[i];
		float			 intensity = ((float)d_lightstylevalue[l->style] / 256.0f) * l->intensity * r_entdlightscale.value;
		if (l->end_fade_distance > 0.0f)
		{
			vec3_t offset;
			VectorSubtract (l->origin, r_refdef.vieworg, offset);
			const float view_distance = VectorLength (offset);
			if (view_distance >= l->end_fade_distance)
				continue;
			if ((view_distance > l->start_fade_distance) && (l->end_fade_distance > l->start_fade_distance))
				intensity *= (l->end_fade_distance - view_distance) / (l->end_fade_distance - l->start_fade_distance);
		}
		if (intensity <= 0.0f)
			continue;

		dlight_t *dl = CL_AllocDlight (ENTITY_DLIGHT_KEY + i);
		VectorCopy (l->origin, dl->origin);
		dl->radius = l->radius;
		dl->die = cl.time + 0.001f;
		VectorCopy (l->color, dl->color);
		VectorCopy (l->cone_dir, dl->cone_dir);
		dl->cone_cos = l->cone_cos;
		dl->kex_intensity = intensity;
	}
}

/*
=============================================================================

LIGHT SAMPLING

=============================================================================
*/

static void InterpolateLightmap (vec3_t color, msurface_t *surf, int ds, int dt)
{
	byte *lightmap;
	int	  maps, line3, dsfrac = ds & 15, dtfrac = dt & 15, r00 = 0, g00 = 0, b00 = 0, r01 = 0, g01 = 0, b01 = 0, r10 = 0, g10 = 0, b10 = 0, r11 = 0, g11 = 0,
					 b11 = 0;
	int scale;
	line3 = ((surf->extents[0] >> 4) + 1) * 3;

	lightmap = surf->samples + ((dt >> 4) * ((surf->extents[0] >> 4) + 1) + (ds >> 4)) * 3; // LordHavoc: *3 for color

	for (maps = 0; maps < MAXLIGHTMAPS && surf->styles[maps] != 255; maps++)
	{
		scale = d_lightstylevalue[surf->styles[maps]];
		r00 += lightmap[0] * scale;
		g00 += lightmap[1] * scale;
		b00 += lightmap[2] * scale;
		r01 += lightmap[3] * scale;
		g01 += lightmap[4] * scale;
		b01 += lightmap[5] * scale;
		r10 += lightmap[line3 + 0] * scale;
		g10 += lightmap[line3 + 1] * scale;
		b10 += lightmap[line3 + 2] * scale;
		r11 += lightmap[line3 + 3] * scale;
		g11 += lightmap[line3 + 4] * scale;
		b11 += lightmap[line3 + 5] * scale;
		lightmap += ((surf->extents[0] >> 4) + 1) * ((surf->extents[1] >> 4) + 1) * 3; // LordHavoc: *3 for colored lighting
	}

	color[0] = ((((((((r11 - r10) * dsfrac) >> 4) + r10) - ((((r01 - r00) * dsfrac) >> 4) + r00)) * dtfrac) >> 4) + ((((r01 - r00) * dsfrac) >> 4) + r00)) *
			   (1.f / 256.f);
	color[1] = ((((((((g11 - g10) * dsfrac) >> 4) + g10) - ((((g01 - g00) * dsfrac) >> 4) + g00)) * dtfrac) >> 4) + ((((g01 - g00) * dsfrac) >> 4) + g00)) *
			   (1.f / 256.f);
	color[2] = ((((((((b11 - b10) * dsfrac) >> 4) + b10) - ((((b01 - b00) * dsfrac) >> 4) + b00)) * dtfrac) >> 4) + ((((b01 - b00) * dsfrac) >> 4) + b00)) *
			   (1.f / 256.f);
}

/*
=============
RecursiveLightPoint -- johnfitz -- replaced entire function for lit support via lordhavoc
=============
*/
int RecursiveLightPoint (lightcache_t *cache, mnode_t *node, vec3_t rayorg, vec3_t start, vec3_t end, float *maxdist)
{
	float  front, back, frac;
	vec3_t mid;

loc0:
	if (node->contents < 0)
		return false; // didn't hit anything

	// calculate mid point
	if (node->plane->type < 3)
	{
		front = start[node->plane->type] - node->plane->dist;
		back = end[node->plane->type] - node->plane->dist;
	}
	else
	{
		front = DotProduct (start, node->plane->normal) - node->plane->dist;
		back = DotProduct (end, node->plane->normal) - node->plane->dist;
	}

	// LordHavoc: optimized recursion
	if ((back < 0) == (front < 0))
	//		return RecursiveLightPoint (cache, node->children[front < 0], rayorg, start, end, maxdist);
	{
		node = node->children[front < 0];
		goto loc0;
	}

	frac = front / (front - back);
	mid[0] = start[0] + (end[0] - start[0]) * frac;
	mid[1] = start[1] + (end[1] - start[1]) * frac;
	mid[2] = start[2] + (end[2] - start[2]) * frac;

	// go down front side
	if (RecursiveLightPoint (cache, node->children[front < 0], rayorg, start, mid, maxdist))
		return true; // hit something
	else
	{
		unsigned int i;
		int			 ds, dt;
		msurface_t	*surf;

		surf = cl.worldmodel->surfaces + node->firstsurface;
		for (i = 0; i < node->numsurfaces; i++, surf++)
		{
			float  sfront, sback, dist;
			vec3_t raydelta;

			if (surf->flags & SURF_DRAWTILED)
				continue; // no lightmaps

			// ericw -- added double casts to force 64-bit precision.
			// Without them the zombie at the start of jam3_ericw.bsp was
			// incorrectly being lit up in SSE builds.
			ds = (int)((double)DoublePrecisionDotProduct (mid, surf->texinfo->vecs[0]) + surf->texinfo->vecs[0][3]);
			dt = (int)((double)DoublePrecisionDotProduct (mid, surf->texinfo->vecs[1]) + surf->texinfo->vecs[1][3]);

			if (ds < surf->texturemins[0] || dt < surf->texturemins[1])
				continue;

			ds -= surf->texturemins[0];
			dt -= surf->texturemins[1];

			if (ds > surf->extents[0] || dt > surf->extents[1])
				continue;

			if (surf->plane->type < 3)
			{
				sfront = rayorg[surf->plane->type] - surf->plane->dist;
				sback = end[surf->plane->type] - surf->plane->dist;
			}
			else
			{
				sfront = DotProduct (rayorg, surf->plane->normal) - surf->plane->dist;
				sback = DotProduct (end, surf->plane->normal) - surf->plane->dist;
			}
			VectorSubtract (end, rayorg, raydelta);
			dist = sfront / (sfront - sback) * VectorLength (raydelta);

			if (!surf->samples)
			{
				// We hit a surface that is flagged as lightmapped, but doesn't have actual lightmap info.
				// Instead of just returning black, we'll keep looking for nearby surfaces that do have valid samples.
				// This fixes occasional pitch-black models in otherwise well-lit areas in DOTM (e.g. mge1m1, mge4m1)
				// caused by overlapping surfaces with mixed lighting data.
				const float nearby = 8.f;
				dist += nearby;
				*maxdist = q_min (*maxdist, dist);
				continue;
			}

			if (dist < *maxdist)
			{
				cache->surfidx = surf - cl.worldmodel->surfaces + 1;
				cache->ds = ds;
				cache->dt = dt;
			}
			else
			{
				cache->surfidx = -1;
			}

			return true; // success
		}

		// go down back side
		return RecursiveLightPoint (cache, node->children[front >= 0], rayorg, mid, end, maxdist);
	}
}

/*
=============
R_LightPoint -- johnfitz -- replaced entire function for lit support via lordhavoc
=============
*/
int R_LightPoint (vec3_t p, float ofs, lightcache_t *cache, vec3_t *lightcolor)
{
	vec3_t start, end;
	float  maxdist = 8192.f; // johnfitz -- was 2048

	if (!cl.worldmodel->lightdata)
	{
		(*lightcolor)[0] = (*lightcolor)[1] = (*lightcolor)[2] = 255;
		return 255;
	}

	start[0] = p[0];
	start[1] = p[1];
	start[2] = p[2] + ofs;
	end[0] = start[0];
	end[1] = start[1];
	end[2] = start[2] - maxdist;

	(*lightcolor)[0] = (*lightcolor)[1] = (*lightcolor)[2] = 0;

	if (cache->surfidx <= 0 // no cache or pitch black
		|| cache->surfidx > cl.worldmodel->numsurfaces || fabsf (cache->pos[0] - p[0]) >= 1.f || fabsf (cache->pos[1] - p[1]) >= 1.f ||
		fabsf (cache->pos[2] - p[2]) >= 1.f)
	{
		cache->surfidx = 0;
		VectorCopy (p, cache->pos);
		RecursiveLightPoint (cache, cl.worldmodel->nodes, start, start, end, &maxdist);
	}

	if (cache && cache->surfidx > 0)
		InterpolateLightmap (*lightcolor, cl.worldmodel->surfaces + cache->surfidx - 1, cache->ds, cache->dt);

	return (((*lightcolor)[0] + (*lightcolor)[1] + (*lightcolor)[2]) * (1.0f / 3.0f));
}
