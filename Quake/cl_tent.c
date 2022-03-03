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
// cl_tent.c -- client side temporary entities

#include "quakedef.h"

int      num_temp_entities;
entity_t cl_temp_entities[MAX_TEMP_ENTITIES];
beam_t   cl_beams[MAX_BEAMS];

static sfx_t *cl_sfx_wizhit;
static sfx_t *cl_sfx_knighthit;
static sfx_t *cl_sfx_tink1;
static sfx_t *cl_sfx_ric1;
static sfx_t *cl_sfx_ric2;
static sfx_t *cl_sfx_ric3;
static sfx_t *cl_sfx_r_exp3;

/*
=================
CL_ParseTEnt
=================
*/
void CL_InitTEnts (void)
{
	cl_sfx_wizhit = S_PrecacheSound ("wizard/hit.wav");
	cl_sfx_knighthit = S_PrecacheSound ("hknight/hit.wav");
	cl_sfx_tink1 = S_PrecacheSound ("weapons/tink1.wav");
	cl_sfx_ric1 = S_PrecacheSound ("weapons/ric1.wav");
	cl_sfx_ric2 = S_PrecacheSound ("weapons/ric2.wav");
	cl_sfx_ric3 = S_PrecacheSound ("weapons/ric3.wav");
	cl_sfx_r_exp3 = S_PrecacheSound ("weapons/r_exp3.wav");
}

/*
=================
CL_ParseBeam
=================
*/
void CL_UpdateBeam (qmodel_t *m, const char *trailname, const char *impactname, int ent, float *start, float *end)
{
	beam_t *b;
	int     i;

#ifdef PSET_SCRIPT
	{
		vec3_t normal, extra, impact;
		VectorSubtract (end, start, normal);
		VectorNormalize (normal);
		VectorMA (end, 4, normal, extra); // extend the end-point by four
		if (CL_TraceLine (start, extra, impact, normal, NULL) < 1)
			PScript_RunParticleEffectTypeString (impact, normal, 1, impactname);
	}
#endif

	// override any beam with the same entity
	for (i = 0, b = cl_beams; i < MAX_BEAMS; i++, b++)
		if (b->entity == ent)
		{
			b->entity = ent;
			b->model = m;
			b->endtime = cl.time + 0.2;
			VectorCopy (start, b->start);
			VectorCopy (end, b->end);
			return;
		}

	// find a free beam
	for (i = 0, b = cl_beams; i < MAX_BEAMS; i++, b++)
	{
		if (!b->model || b->endtime < cl.time)
		{
			b->entity = ent;
			b->model = m;
			b->endtime = cl.time + 0.2;
			VectorCopy (start, b->start);
			VectorCopy (end, b->end);
			return;
		}
	}

	// johnfitz -- less spammy overflow message
	if (!dev_overflows.beams || dev_overflows.beams + CONSOLE_RESPAM_TIME < realtime)
	{
		Con_Printf ("Beam list overflow!\n");
		dev_overflows.beams = realtime;
	}
	// johnfitz
}

static void CL_ParseBeam (qmodel_t *m, const char *trailname, const char *impactname)
{
	int    ent;
	vec3_t start, end;

	ent = MSG_ReadEntity (cl.protocol_pext2);

	start[0] = MSG_ReadCoord (cl.protocolflags);
	start[1] = MSG_ReadCoord (cl.protocolflags);
	start[2] = MSG_ReadCoord (cl.protocolflags);

	end[0] = MSG_ReadCoord (cl.protocolflags);
	end[1] = MSG_ReadCoord (cl.protocolflags);
	end[2] = MSG_ReadCoord (cl.protocolflags);

	CL_UpdateBeam (m, trailname, impactname, ent, start, end);
}

/*
=================
CL_ParseTEnt
=================
*/
void CL_ParseTEnt (void)
{
	int       type;
	vec3_t    pos;
	dlight_t *dl;
	int       rnd;
	int       colorStart, colorLength;

	type = MSG_ReadByte ();
	switch (type)
	{
	case TE_WIZSPIKE: // spike hitting wall
		pos[0] = MSG_ReadCoord (cl.protocolflags);
		pos[1] = MSG_ReadCoord (cl.protocolflags);
		pos[2] = MSG_ReadCoord (cl.protocolflags);
		if (PScript_RunParticleEffectTypeString (pos, NULL, 1, "TE_WIZSPIKE"))
			R_RunParticleEffect (pos, vec3_origin, 20, 30);
		S_StartSound (-1, 0, cl_sfx_wizhit, pos, 1, 1);
		break;

	case TE_KNIGHTSPIKE: // spike hitting wall
		pos[0] = MSG_ReadCoord (cl.protocolflags);
		pos[1] = MSG_ReadCoord (cl.protocolflags);
		pos[2] = MSG_ReadCoord (cl.protocolflags);
		if (PScript_RunParticleEffectTypeString (pos, NULL, 1, "TE_KNIGHTSPIKE"))
			R_RunParticleEffect (pos, vec3_origin, 226, 20);
		S_StartSound (-1, 0, cl_sfx_knighthit, pos, 1, 1);
		break;

	case TE_SPIKE: // spike hitting wall
		pos[0] = MSG_ReadCoord (cl.protocolflags);
		pos[1] = MSG_ReadCoord (cl.protocolflags);
		pos[2] = MSG_ReadCoord (cl.protocolflags);
		if (PScript_RunParticleEffectTypeString (pos, NULL, 1, "TE_SPIKE"))
			R_RunParticleEffect (pos, vec3_origin, 0, 10);
		if (rand () % 5)
			S_StartSound (-1, 0, cl_sfx_tink1, pos, 1, 1);
		else
		{
			rnd = rand () & 3;
			if (rnd == 1)
				S_StartSound (-1, 0, cl_sfx_ric1, pos, 1, 1);
			else if (rnd == 2)
				S_StartSound (-1, 0, cl_sfx_ric2, pos, 1, 1);
			else
				S_StartSound (-1, 0, cl_sfx_ric3, pos, 1, 1);
		}
		break;
	case TE_SUPERSPIKE: // super spike hitting wall
		pos[0] = MSG_ReadCoord (cl.protocolflags);
		pos[1] = MSG_ReadCoord (cl.protocolflags);
		pos[2] = MSG_ReadCoord (cl.protocolflags);
		if (PScript_RunParticleEffectTypeString (pos, NULL, 1, "TE_SUPERSPIKE"))
			R_RunParticleEffect (pos, vec3_origin, 0, 20);

		if (rand () % 5)
			S_StartSound (-1, 0, cl_sfx_tink1, pos, 1, 1);
		else
		{
			rnd = rand () & 3;
			if (rnd == 1)
				S_StartSound (-1, 0, cl_sfx_ric1, pos, 1, 1);
			else if (rnd == 2)
				S_StartSound (-1, 0, cl_sfx_ric2, pos, 1, 1);
			else
				S_StartSound (-1, 0, cl_sfx_ric3, pos, 1, 1);
		}
		break;

	case TE_GUNSHOT: // bullet hitting wall
		rnd = 20;
		pos[0] = MSG_ReadCoord (cl.protocolflags);
		pos[1] = MSG_ReadCoord (cl.protocolflags);
		pos[2] = MSG_ReadCoord (cl.protocolflags);
		if (PScript_RunParticleEffectTypeString (pos, NULL, rnd, "TE_GUNSHOT"))
			R_RunParticleEffect (pos, vec3_origin, 0, rnd);
		break;

	case TE_EXPLOSION: // rocket explosion
		pos[0] = MSG_ReadCoord (cl.protocolflags);
		pos[1] = MSG_ReadCoord (cl.protocolflags);
		pos[2] = MSG_ReadCoord (cl.protocolflags);
		if (PScript_RunParticleEffectTypeString (pos, NULL, 1, "TE_EXPLOSION"))
			R_ParticleExplosion (pos);
		dl = CL_AllocDlight (0);
		VectorCopy (pos, dl->origin);
		dl->radius = 350;
		dl->die = cl.time + 0.5;
		dl->decay = 300;
		S_StartSound (-1, 0, cl_sfx_r_exp3, pos, 1, 1);
		break;

	case TE_TAREXPLOSION: // tarbaby explosion
		pos[0] = MSG_ReadCoord (cl.protocolflags);
		pos[1] = MSG_ReadCoord (cl.protocolflags);
		pos[2] = MSG_ReadCoord (cl.protocolflags);
		if (PScript_RunParticleEffectTypeString (pos, NULL, 1, "TE_TAREXPLOSION"))
			R_BlobExplosion (pos);

		S_StartSound (-1, 0, cl_sfx_r_exp3, pos, 1, 1);
		break;

	case TE_LIGHTNING1: // lightning bolts
		CL_ParseBeam (Mod_ForName ("progs/bolt.mdl", true), "TE_LIGHTNING1", "TE_LIGHTNING1_END");
		break;

	case TE_LIGHTNING2: // lightning bolts
		CL_ParseBeam (Mod_ForName ("progs/bolt2.mdl", true), "TE_LIGHTNING2", "TE_LIGHTNING2_END");
		break;

	case TE_LIGHTNING3: // lightning bolts
		CL_ParseBeam (Mod_ForName ("progs/bolt3.mdl", true), "TE_LIGHTNING3", "TE_LIGHTNING3_END");
		break;

		// PGM 01/21/97
	case TE_BEAM: // grappling hook beam
		CL_ParseBeam (Mod_ForName ("progs/beam.mdl", true), "TE_BEAM", "TE_BEAM_END");
		break;
		// PGM 01/21/97

	case TE_LAVASPLASH:
		pos[0] = MSG_ReadCoord (cl.protocolflags);
		pos[1] = MSG_ReadCoord (cl.protocolflags);
		pos[2] = MSG_ReadCoord (cl.protocolflags);
		if (PScript_RunParticleEffectTypeString (pos, NULL, 1, "TE_LAVASPLASH"))
			R_LavaSplash (pos);
		break;

	case TE_TELEPORT:
		pos[0] = MSG_ReadCoord (cl.protocolflags);
		pos[1] = MSG_ReadCoord (cl.protocolflags);
		pos[2] = MSG_ReadCoord (cl.protocolflags);
		if (PScript_RunParticleEffectTypeString (pos, NULL, 1, "TE_TELEPORT"))
			R_TeleportSplash (pos);
		break;

	case TE_EXPLOSION2: // color mapped explosion
		pos[0] = MSG_ReadCoord (cl.protocolflags);
		pos[1] = MSG_ReadCoord (cl.protocolflags);
		pos[2] = MSG_ReadCoord (cl.protocolflags);
		colorStart = MSG_ReadByte ();
		colorLength = MSG_ReadByte ();
		if (PScript_RunParticleEffectTypeString (pos, NULL, 1, va ("TE_EXPLOSION2_%i_%i", colorStart, colorLength)))
			R_ParticleExplosion2 (pos, colorStart, colorLength);
		dl = CL_AllocDlight (0);
		VectorCopy (pos, dl->origin);
		dl->radius = 350;
		dl->die = cl.time + 0.5;
		dl->decay = 300;
		S_StartSound (-1, 0, cl_sfx_r_exp3, pos, 1, 1);
		break;

	case TEDP_PARTICLERAIN:
	case TEDP_PARTICLESNOW:
	{
		vec3_t dir, pos2;
		int    cnt, colour;

		// min
		pos[0] = MSG_ReadCoord (cl.protocolflags);
		pos[1] = MSG_ReadCoord (cl.protocolflags);
		pos[2] = MSG_ReadCoord (cl.protocolflags);

		// max
		pos2[0] = MSG_ReadCoord (cl.protocolflags);
		pos2[1] = MSG_ReadCoord (cl.protocolflags);
		pos2[2] = MSG_ReadCoord (cl.protocolflags);

		// dir
		dir[0] = MSG_ReadCoord (cl.protocolflags);
		dir[1] = MSG_ReadCoord (cl.protocolflags);
		dir[2] = MSG_ReadCoord (cl.protocolflags);

		cnt = (unsigned short)MSG_ReadShort (); // count
		colour = MSG_ReadByte ();               // colour

		PScript_RunParticleWeather (pos, pos2, dir, cnt, colour, ((type == TEDP_PARTICLESNOW) ? "snow" : "rain"));
	}
	break;

	default:
		Sys_Error ("CL_ParseTEnt: bad type");
	}
}

/*
=================
CL_NewTempEntity
=================
*/
entity_t *CL_NewTempEntity (void)
{
	entity_t *ent;

	if (cl_numvisedicts == cl_maxvisedicts)
		return NULL;
	if (num_temp_entities == MAX_TEMP_ENTITIES)
		return NULL;
	ent = &cl_temp_entities[num_temp_entities];
	memset (ent, 0, sizeof (*ent));
	num_temp_entities++;
	cl_visedicts[cl_numvisedicts] = ent;
	cl_numvisedicts++;

	ent->netstate.scale = 16;
	ent->netstate.colormod[0] = ent->netstate.colormod[1] = ent->netstate.colormod[2] = 32;
	ent->netstate.colormap = 0;
	return ent;
}

/*
=================
CL_UpdateTEnts
=================
*/
void CL_UpdateTEnts (void)
{
	int       i, j; // johnfitz -- use j instead of using i twice, so we don't corrupt memory
	beam_t   *b;
	vec3_t    dist, org;
	float     d;
	entity_t *ent;
	float     yaw, pitch;
	float     forward;

	num_temp_entities = 0;

	if (cl.paused)
		srand ((int)(cl.time * 1000)); // johnfitz -- freeze beams when paused

	// update lightning
	for (i = 0, b = cl_beams; i < MAX_BEAMS; i++, b++)
	{
		if (!b->model || b->endtime < cl.time)
			continue;

		// if coming from the player, update the start position
		if (b->entity == cl.viewentity && cl.entities)
		{
			VectorCopy (cl.entities[cl.viewentity].origin, b->start);
		}

		// calculate pitch and yaw
		VectorSubtract (b->end, b->start, dist);

		if (dist[1] == 0 && dist[0] == 0)
		{
			yaw = 0;
			if (dist[2] > 0)
				pitch = 90;
			else
				pitch = 270;
		}
		else
		{
			yaw = (int)(atan2 (dist[1], dist[0]) * 180 / M_PI);
			if (yaw < 0)
				yaw += 360;

			forward = sqrt (dist[0] * dist[0] + dist[1] * dist[1]);
			pitch = (int)(atan2 (dist[2], forward) * 180 / M_PI);
			if (pitch < 0)
				pitch += 360;
		}

		// add new entities for the lightning
		VectorCopy (b->start, org);
		d = VectorNormalize (dist);
		while (d > 0)
		{
			ent = CL_NewTempEntity ();
			if (!ent)
				return;
			VectorCopy (org, ent->origin);
			ent->model = b->model;
			ent->angles[0] = pitch;
			ent->angles[1] = yaw;
			ent->angles[2] = rand () % 360;

			// johnfitz -- use j instead of using i twice, so we don't corrupt memory
			for (j = 0; j < 3; j++)
				org[j] += dist[j] * 30;
			d -= 30;
		}
	}
}
