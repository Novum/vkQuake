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

#include "quakedef.h"

// #define	STRINGTEMP_BUFFERS		16
// #define	STRINGTEMP_LENGTH		1024
static char pr_string_temp[STRINGTEMP_BUFFERS][STRINGTEMP_LENGTH];
static byte pr_string_tempindex = 0;

char *PR_GetTempString (void)
{
	return pr_string_temp[(STRINGTEMP_BUFFERS - 1) & ++pr_string_tempindex];
}

#define RETURN_EDICT(e) (((int *)qcvm->globals)[OFS_RETURN] = EDICT_TO_PROG (e))

/*
===============================================================================

	BUILT-IN FUNCTIONS

===============================================================================
*/

static const char *PF_GetStringArg (int idx, void *userdata)
{
	if (userdata)
		idx += *(int *)userdata;
	if (idx < 0 || idx >= qcvm->argc)
		return "";
	return LOC_GetString (G_STRING (OFS_PARM0 + idx * 3));
}

char *PF_VarString (int first)
{
	int			i;
	static char out[1024];
	const char *format;
	size_t		s;

	out[0] = 0;
	s = 0;

	if (first >= qcvm->argc)
		return out;

	format = LOC_GetString (G_STRING ((OFS_PARM0 + first * 3)));
	if (LOC_HasPlaceholders (format))
	{
		int offset = first + 1;
		s = LOC_Format (format, PF_GetStringArg, &offset, out, sizeof (out));
	}
	else
	{
		for (i = first; i < qcvm->argc; i++)
		{
			s = q_strlcat (out, LOC_GetString (G_STRING (OFS_PARM0 + i * 3)), sizeof (out));
			if (s >= sizeof (out))
			{
				Con_Warning ("PF_VarString: overflow (string truncated)\n");
				return out;
			}
		}
	}
	if (s > 255)
	{
		if (!dev_overflows.varstring || dev_overflows.varstring + CONSOLE_RESPAM_TIME < realtime)
		{
			Con_DWarning ("PF_VarString: %i characters exceeds standard limit of 255 (max = %d).\n", (int)s, (int)(sizeof (out) - 1));
			dev_overflows.varstring = realtime;
		}
	}
	return out;
}

/*
=================
PF_error

This is a TERMINAL error, which will kill off the entire server.
Dumps self.

error(value)
=================
*/
static void PF_error (void)
{
	char	*s;
	edict_t *ed;

	s = PF_VarString (0);
	Con_Printf ("======SERVER ERROR in %s:\n%s\n", PR_GetString (qcvm->xfunction->s_name), s);
	ed = PROG_TO_EDICT (pr_global_struct->self);
	ED_Print (ed);

	Host_Error ("Program error");
}

/*
=================
PF_objerror

Dumps out self, then an error message.  The program is aborted and self is
removed, but the level can continue.

objerror(value)
=================
*/
static void PF_objerror (void)
{
	char	*s;
	edict_t *ed;

	s = PF_VarString (0);
	Con_Printf ("======OBJECT ERROR in %s:\n%s\n", PR_GetString (qcvm->xfunction->s_name), s);
	ed = PROG_TO_EDICT (pr_global_struct->self);
	ED_Print (ed);
	ED_Free (ed);

	// Host_Error ("Program error"); //johnfitz -- by design, this should not be fatal
}

/*
==============
PF_makevectors

Writes new values for v_forward, v_up, and v_right based on angles
makevectors(vector)
==============
*/
static void PF_makevectors (void)
{
	AngleVectors (G_VECTOR (OFS_PARM0), pr_global_struct->v_forward, pr_global_struct->v_right, pr_global_struct->v_up);
}

/*
=================
PF_setorigin

This is the only valid way to move an object without using the physics
of the world (setting velocity and waiting).  Directly changing origin
will not set internal links correctly, so clipping would be messed up.

This should be called when an object is spawned, and then only if it is
teleported.

setorigin (entity, origin)
=================
*/
static void PF_setorigin (void)
{
	edict_t *e;
	float	*org;

	e = G_EDICT (OFS_PARM0);
	org = G_VECTOR (OFS_PARM1);
	VectorCopy (org, e->v.origin);
	SV_LinkEdict (e, false);
}

void SetMinMaxSize (edict_t *e, float *minvec, float *maxvec, qboolean rotate)
{
	float *angles;
	vec3_t rmin, rmax;
	float  bounds[2][3];
	float  xvector[2], yvector[2];
	float  a;
	vec3_t base, transformed;
	int	   i, j, k, l;

	for (i = 0; i < 3; i++)
		if (minvec[i] > maxvec[i])
			PR_RunError ("backwards mins/maxs");

	rotate = false; // FIXME: implement rotation properly again

	if (!rotate)
	{
		VectorCopy (minvec, rmin);
		VectorCopy (maxvec, rmax);
	}
	else
	{
		// find min / max for rotations
		angles = e->v.angles;

		a = angles[1] / 180 * M_PI;

		xvector[0] = cos (a);
		xvector[1] = sin (a);
		yvector[0] = -sin (a);
		yvector[1] = cos (a);

		VectorCopy (minvec, bounds[0]);
		VectorCopy (maxvec, bounds[1]);

		rmin[0] = rmin[1] = rmin[2] = FLT_MAX;
		rmax[0] = rmax[1] = rmax[2] = -FLT_MAX;

		for (i = 0; i <= 1; i++)
		{
			base[0] = bounds[i][0];
			for (j = 0; j <= 1; j++)
			{
				base[1] = bounds[j][1];
				for (k = 0; k <= 1; k++)
				{
					base[2] = bounds[k][2];

					// transform the point
					transformed[0] = xvector[0] * base[0] + yvector[0] * base[1];
					transformed[1] = xvector[1] * base[0] + yvector[1] * base[1];
					transformed[2] = base[2];

					for (l = 0; l < 3; l++)
					{
						if (transformed[l] < rmin[l])
							rmin[l] = transformed[l];
						if (transformed[l] > rmax[l])
							rmax[l] = transformed[l];
					}
				}
			}
		}
	}

	// set derived values
	VectorCopy (rmin, e->v.mins);
	VectorCopy (rmax, e->v.maxs);
	VectorSubtract (maxvec, minvec, e->v.size);

	SV_LinkEdict (e, false);
}

/*
=================
PF_setsize

the size box is rotated by the current angle

setsize (entity, minvector, maxvector)
=================
*/
static void PF_setsize (void)
{
	edict_t *e;
	float	*minvec, *maxvec;

	e = G_EDICT (OFS_PARM0);
	minvec = G_VECTOR (OFS_PARM1);
	maxvec = G_VECTOR (OFS_PARM2);
	SetMinMaxSize (e, minvec, maxvec, false);
}

/*
=================
PF_setmodel

setmodel(entity, model)
=================
*/
cvar_t		sv_gameplayfix_setmodelrealbox = {"sv_gameplayfix_setmodelrealbox", "1"};
static void PF_sv_setmodel (void)
{
	int			i;
	const char *m, **check;
	qmodel_t   *mod;
	edict_t	   *e;

	e = G_EDICT (OFS_PARM0);
	m = G_STRING (OFS_PARM1);

	// check to see if model was properly precached
	for (i = 0, check = sv.model_precache; *check; i++, check++)
	{
		if (!strcmp (*check, m))
			break;
	}

	if (!*check)
	{
		if (pr_checkextension.value)
		{
			//			PR_PrintStatement(pr_statements + pr_xstatement);
			//			PR_StackTrace();
			Con_Warning ("PF_setmodel(\"%s\"): Model was not precached\n", m);
			i = SV_Precache_Model (m);
		}
		else
			PR_RunError ("no precache: %s", m);
	}
	e->v.model = PR_SetEngineString (*check);
	e->v.modelindex = i; // SV_ModelIndex (m);

	mod = sv.models[(int)e->v.modelindex]; // Mod_ForName (m, true);

	if (mod)
	// johnfitz -- correct physics cullboxes for bmodels
	{
		if (mod->type == mod_brush)
			SetMinMaxSize (e, mod->clipmins, mod->clipmaxs, true);
		else
			SetMinMaxSize (e, mod->mins, mod->maxs, true);
	}
	// johnfitz
	else
		SetMinMaxSize (e, vec3_origin, vec3_origin, true);
}

/*
=================
PF_bprint

broadcast print to everyone on server

bprint(value)
=================
*/
void PF_bprint (void)
{
	char *s;

	s = PF_VarString (0);
	SV_BroadcastPrintf ("%s", s);
}

/*
=================
PF_sprint

single print to a specific client

sprint(clientent, value)
=================
*/
void PF_sprint (void)
{
	char	 *s;
	client_t *client;
	int		  entnum;

	entnum = G_EDICTNUM (OFS_PARM0);
	s = PF_VarString (1);

	if (entnum < 1 || entnum > svs.maxclients)
	{
		Con_Printf ("tried to sprint to a non-client\n");
		return;
	}

	client = &svs.clients[entnum - 1];

	MSG_WriteChar (&client->message, svc_print);
	MSG_WriteString (&client->message, s);
}

/*
=================
PF_centerprint

single print to a specific client

centerprint(clientent, value)
=================
*/
void PF_centerprint (void)
{
	char	 *s;
	client_t *client;
	int		  entnum;

	entnum = G_EDICTNUM (OFS_PARM0);
	s = PF_VarString (1);

	if (entnum < 1 || entnum > svs.maxclients)
	{
		Con_Printf ("tried to sprint to a non-client\n");
		return;
	}

	client = &svs.clients[entnum - 1];

	MSG_WriteChar (&client->message, svc_centerprint);
	MSG_WriteString (&client->message, s);
}

/*
=================
PF_normalize

vector normalize(vector)
=================
*/
static void PF_normalize (void)
{
	float *value1;
	vec3_t newvalue;
	double new_temp;

	value1 = G_VECTOR (OFS_PARM0);

	new_temp = (double)value1[0] * value1[0] + (double)value1[1] * value1[1] + (double)value1[2] * value1[2];
	new_temp = sqrt (new_temp);

	if (new_temp == 0)
		newvalue[0] = newvalue[1] = newvalue[2] = 0;
	else
	{
		new_temp = 1 / new_temp;
		newvalue[0] = value1[0] * new_temp;
		newvalue[1] = value1[1] * new_temp;
		newvalue[2] = value1[2] * new_temp;
	}

	VectorCopy (newvalue, G_VECTOR (OFS_RETURN));
}

/*
=================
PF_vlen

scalar vlen(vector)
=================
*/
static void PF_vlen (void)
{
	float *value1;
	double new_temp;

	value1 = G_VECTOR (OFS_PARM0);

	new_temp = (double)value1[0] * value1[0] + (double)value1[1] * value1[1] + (double)value1[2] * value1[2];
	new_temp = sqrt (new_temp);

	G_FLOAT (OFS_RETURN) = new_temp;
}

/*
=================
PF_vectoyaw

float vectoyaw(vector)
=================
*/
static void PF_vectoyaw (void)
{
	float *value1;
	float  yaw;

	value1 = G_VECTOR (OFS_PARM0);

	if (value1[1] == 0 && value1[0] == 0)
		yaw = 0;
	else
	{
		yaw = (int)(atan2 (value1[1], value1[0]) * 180 / M_PI);
		if (yaw < 0)
			yaw += 360;
	}

	G_FLOAT (OFS_RETURN) = yaw;
}

/*
=================
PF_vectoangles

vector vectoangles(vector)
=================
*/
static void PF_vectoangles (void)
{
	float *value1;
	float  forward;
	float  yaw, pitch;

	value1 = G_VECTOR (OFS_PARM0);

	if (value1[1] == 0 && value1[0] == 0)
	{
		yaw = 0;
		if (value1[2] > 0)
			pitch = 90;
		else
			pitch = 270;
	}
	else
	{
		yaw = (int)(atan2 (value1[1], value1[0]) * 180 / M_PI);
		if (yaw < 0)
			yaw += 360;

		forward = sqrt (value1[0] * value1[0] + value1[1] * value1[1]);
		pitch = (int)(atan2 (value1[2], forward) * 180 / M_PI);
		if (pitch < 0)
			pitch += 360;
	}

	G_FLOAT (OFS_RETURN + 0) = pitch;
	G_FLOAT (OFS_RETURN + 1) = yaw;
	G_FLOAT (OFS_RETURN + 2) = 0;
}

/*
=================
PF_Random

Returns a number from 0 <= num < 1

random()
=================
*/
static void PF_random (void)
{
	float num;

	num = (rand () & 0x7fff) / ((float)0x8000);

	G_FLOAT (OFS_RETURN) = num;
}

/*
=================
PF_particle

particle(origin, color, count)
=================
*/
static void PF_particle (void)
{
	float *org, *dir;
	float  color;
	float  count;

	org = G_VECTOR (OFS_PARM0);
	dir = G_VECTOR (OFS_PARM1);
	color = G_FLOAT (OFS_PARM2);
	count = G_FLOAT (OFS_PARM3);
	SV_StartParticle (org, dir, color, count);
}

/*
=================
PF_ambientsound

=================
*/
static void PF_sv_ambientsound (void)
{
	const char			  *samp, **check;
	float				  *pos;
	float				   vol, attenuation;
	int					   soundnum;
	struct ambientsound_s *st;

	pos = G_VECTOR (OFS_PARM0);
	samp = G_STRING (OFS_PARM1);
	vol = G_FLOAT (OFS_PARM2);
	attenuation = G_FLOAT (OFS_PARM3);

	// check to see if samp was properly precached
	for (soundnum = 0, check = sv.sound_precache; *check; check++, soundnum++)
	{
		if (!strcmp (*check, samp))
			break;
	}

	if (!*check)
	{
		Con_Printf ("no precache: %s\n", samp);
		return;
	}

	// generate data to splurge on a per-client basis in SV_SendAmbientSounds
	if (sv.num_ambients == sv.max_ambients)
	{
		int					   nm = sv.max_ambients + 128;
		struct ambientsound_s *n = (nm * sizeof (*n) < sv.max_ambients * sizeof (*n)) ? NULL : Mem_Realloc (sv.ambientsounds, nm * sizeof (*n));
		if (!n)
			PR_RunError ("PF_ambientsound: out of memory"); // shouldn't really happen.
		sv.ambientsounds = n;
		memset (sv.ambientsounds + sv.max_ambients, 0, (nm - sv.max_ambients) * sizeof (*n));
		sv.max_ambients = nm;
	}
	st = &sv.ambientsounds[sv.num_ambients++];
	VectorCopy (pos, st->origin);
	st->soundindex = soundnum;
	st->volume = vol;
	st->attenuation = attenuation;
}

/*
=================
PF_sound

Each entity can have eight independant sound sources, like voice,
weapon, feet, etc.

Channel 0 is an auto-allocate channel, the others override anything
already running on that entity/channel pair.

An attenuation of 0 will play full volume everywhere in the level.
Larger attenuations will drop off.

=================
*/
static void PF_sound (void)
{
	const char *sample;
	int			channel;
	edict_t	   *entity;
	int			volume;
	float		attenuation;

	entity = G_EDICT (OFS_PARM0);
	channel = G_FLOAT (OFS_PARM1);
	sample = G_STRING (OFS_PARM2);
	volume = G_FLOAT (OFS_PARM3) * 255;
	attenuation = G_FLOAT (OFS_PARM4);

	if (!*sample)
	{
		PR_RunWarning ("PF_sound: empty string\n");
		return;
	}

	SV_StartSound (entity, NULL, channel, sample, volume, attenuation);
}

/*
=================
PF_break

break()
=================
*/
static void PF_break (void)
{
	Con_Printf ("break statement\n");
	*(int *)-4 = 0; // dump to debugger
					//	PR_RunError ("break statement");
}

/*
=================
PF_traceline

Used for use tracing and shot targeting
Traces are blocked by bbox and exact bsp entityes, and also slide box entities
if the tryents flag is set.

traceline (vector1, vector2, tryents)
=================
*/
static void PF_traceline (void)
{
	float	*v1, *v2;
	trace_t	 trace;
	int		 nomonsters;
	edict_t *ent;

	v1 = G_VECTOR (OFS_PARM0);
	v2 = G_VECTOR (OFS_PARM1);
	nomonsters = G_FLOAT (OFS_PARM2);
	ent = G_EDICT (OFS_PARM3);

	/* FIXME FIXME FIXME: Why do we hit this with certain progs.dat ?? */
	if (developer.value)
	{
		if (IS_NAN (v1[0]) || IS_NAN (v1[1]) || IS_NAN (v1[2]) || IS_NAN (v2[0]) || IS_NAN (v2[1]) || IS_NAN (v2[2]))
		{
			Con_Warning ("NAN in traceline:\nv1(%f %f %f) v2(%f %f %f)\nentity %d\n", v1[0], v1[1], v1[2], v2[0], v2[1], v2[2], NUM_FOR_EDICT (ent));
		}
	}

	if (IS_NAN (v1[0]) || IS_NAN (v1[1]) || IS_NAN (v1[2]))
		v1[0] = v1[1] = v1[2] = 0;
	if (IS_NAN (v2[0]) || IS_NAN (v2[1]) || IS_NAN (v2[2]))
		v2[0] = v2[1] = v2[2] = 0;

	trace = SV_Move (v1, vec3_origin, vec3_origin, v2, nomonsters, ent);

	pr_global_struct->trace_allsolid = trace.allsolid;
	pr_global_struct->trace_startsolid = trace.startsolid;
	pr_global_struct->trace_fraction = trace.fraction;
	pr_global_struct->trace_inwater = trace.inwater;
	pr_global_struct->trace_inopen = trace.inopen;
	VectorCopy (trace.endpos, pr_global_struct->trace_endpos);
	VectorCopy (trace.plane.normal, pr_global_struct->trace_plane_normal);
	pr_global_struct->trace_plane_dist = trace.plane.dist;
	if (trace.ent)
		pr_global_struct->trace_ent = EDICT_TO_PROG (trace.ent);
	else
		pr_global_struct->trace_ent = EDICT_TO_PROG (qcvm->edicts);
}

/*
=================
PF_checkpos

Returns true if the given entity can move to the given position from it's
current position by walking or rolling.
FIXME: make work...
scalar checkpos (entity, vector)
=================
*/
#if 0
static void PF_checkpos (void)
{
}
#endif

//============================================================================

static byte *checkpvs; // ericw -- changed to malloc
static int	 checkpvs_capacity;

static int PF_newcheckclient (int check)
{
	int		 i;
	byte	*pvs;
	edict_t *ent;
	mleaf_t *leaf;
	vec3_t	 org;
	int		 pvsbytes;

	// cycle to the next one

	if (check < 1)
		check = 1;
	if (check > svs.maxclients)
		check = svs.maxclients;

	if (check == svs.maxclients)
		i = 1;
	else
		i = check + 1;

	for (;; i++)
	{
		if (i == svs.maxclients + 1)
			i = 1;

		ent = EDICT_NUM (i);

		if (i == check)
			break; // didn't find anything else

		if (ent->free)
			continue;
		if (ent->v.health <= 0)
			continue;
		if ((int)ent->v.flags & FL_NOTARGET)
			continue;

		// anything that is a client, or has a client as an enemy
		break;
	}

	// get the PVS for the entity
	VectorAdd (ent->v.origin, ent->v.view_ofs, org);
	leaf = Mod_PointInLeaf (org, qcvm->worldmodel);
	pvs = Mod_LeafPVS (leaf, qcvm->worldmodel);

	pvsbytes = (qcvm->worldmodel->numleafs + 31) >> 3;
	if (checkpvs == NULL || pvsbytes > checkpvs_capacity)
	{
		checkpvs_capacity = pvsbytes;
		checkpvs = (byte *)Mem_Realloc (checkpvs, checkpvs_capacity);
		if (!checkpvs)
			Sys_Error ("PF_newcheckclient: realloc() failed on %d bytes", checkpvs_capacity);
	}
	memcpy (checkpvs, pvs, pvsbytes);

	return i;
}

/*
=================
PF_checkclient

Returns a client (or object that has a client enemy) that would be a
valid target.

If there are more than one valid options, they are cycled each frame

If (self.origin + self.viewofs) is not in the PVS of the current target,
it is not returned at all.

name checkclient ()
=================
*/
#define MAX_CHECK 16
static int	c_invis, c_notvis;
static void PF_sv_checkclient (void)
{
	edict_t *ent, *self;
	mleaf_t *leaf;
	int		 l;
	vec3_t	 view;

	// find a new check if on a new frame
	if (qcvm->time - sv.lastchecktime >= 0.1)
	{
		sv.lastcheck = PF_newcheckclient (sv.lastcheck);
		sv.lastchecktime = qcvm->time;
	}

	// return check if it might be visible
	ent = EDICT_NUM (sv.lastcheck);
	if (ent->free || ent->v.health <= 0)
	{
		RETURN_EDICT (qcvm->edicts);
		return;
	}

	// if current entity can't possibly see the check entity, return 0
	self = PROG_TO_EDICT (pr_global_struct->self);
	VectorAdd (self->v.origin, self->v.view_ofs, view);
	leaf = Mod_PointInLeaf (view, qcvm->worldmodel);
	l = (leaf - qcvm->worldmodel->leafs) - 1;
	if ((l < 0) || !(checkpvs[l >> 3] & (1 << (l & 7))))
	{
		c_notvis++;
		RETURN_EDICT (qcvm->edicts);
		return;
	}

	// might be able to see it
	c_invis++;
	RETURN_EDICT (ent);
}

//============================================================================

/*
=================
PF_stuffcmd

Sends text over to the client's execution buffer

stuffcmd (clientent, value)
=================
*/
static void PF_stuffcmd (void)
{
	int			entnum;
	const char *str;
	client_t   *old;

	entnum = G_EDICTNUM (OFS_PARM0);
	if (entnum < 1 || entnum > svs.maxclients)
		PR_RunError ("Parm 0 not a client");
	str = G_STRING (OFS_PARM1);

	old = host_client;
	host_client = &svs.clients[entnum - 1];
	Host_ClientCommands ("%s", str);
	host_client = old;
}

/*
=================
PF_localcmd

Sends text over to the client's execution buffer

localcmd (string)
=================
*/
static void PF_localcmd (void)
{
	const char *str, *str2;

	str = G_STRING (OFS_PARM0);

	for (str2 = str; *str2 && *str2 <= ' '; str2++)
		;
	if (!strncmp (str2, "restart", strlen ("restart")))
	{
		if (svs.changelevel_issued)
			return;
		svs.changelevel_issued = true;
	}

	Cbuf_AddText (str);
}

/*
=================
PF_cvar

float cvar (string)
=================
*/
static void PF_cvar (void)
{
	const char *str;

	str = G_STRING (OFS_PARM0);

	G_FLOAT (OFS_RETURN) = Cvar_VariableValue (str);
}

/*
=================
PF_cvar_set

float cvar (string)
=================
*/
static void PF_cvar_set (void)
{
	const char *var, *val;

	var = G_STRING (OFS_PARM0);
	val = G_STRING (OFS_PARM1);

	Cvar_Set (var, val);
}

/*
=================
PF_findradius

Returns a chain of entities that have origins within a spherical area

findradius (origin, radius)
=================
*/
static void PF_findradius (void)
{
	edict_t *ent, *chain;
	float	 rad;
	float	*org;
	int		 i;

	chain = (edict_t *)qcvm->edicts;

	org = G_VECTOR (OFS_PARM0);
	rad = G_FLOAT (OFS_PARM1);
	rad *= rad;

	ent = NEXT_EDICT (qcvm->edicts);
	for (i = 1; i < qcvm->num_edicts; i++, ent = NEXT_EDICT (ent))
	{
		float d, lensq;
		if (ent->free)
			continue;
		if (ent->v.solid == SOLID_NOT)
			continue;

		d = org[0] - (ent->v.origin[0] + (ent->v.mins[0] + ent->v.maxs[0]) * 0.5);
		lensq = d * d;
		if (lensq > rad)
			continue;
		d = org[1] - (ent->v.origin[1] + (ent->v.mins[1] + ent->v.maxs[1]) * 0.5);
		lensq += d * d;
		if (lensq > rad)
			continue;
		d = org[2] - (ent->v.origin[2] + (ent->v.mins[2] + ent->v.maxs[2]) * 0.5);
		lensq += d * d;
		if (lensq > rad)
			continue;

		ent->v.chain = EDICT_TO_PROG (chain);
		chain = ent;
	}

	RETURN_EDICT (chain);
}

/*
=========
PF_dprint
=========
*/
static void PF_dprint (void)
{
	Con_DPrintf ("%s", PF_VarString (0));
}

static void PF_ftos (void)
{
	float v;
	char *s;

	v = G_FLOAT (OFS_PARM0);
	s = PR_GetTempString ();
	if (v == (int)v)
		q_snprintf (s, STRINGTEMP_LENGTH, "%d", (int)v);
	else
		q_snprintf (s, STRINGTEMP_LENGTH, "%5.1f", v); // dodgy path
	G_INT (OFS_RETURN) = PR_SetEngineString (s);
}

static void PF_fabs (void)
{
	float v;
	v = G_FLOAT (OFS_PARM0);
	G_FLOAT (OFS_RETURN) = fabs (v);
}

static void PF_vtos (void)
{
	char *s;

	s = PR_GetTempString ();
	q_snprintf (s, STRINGTEMP_LENGTH, "'%5.1f %5.1f %5.1f'", G_VECTOR (OFS_PARM0)[0], G_VECTOR (OFS_PARM0)[1], G_VECTOR (OFS_PARM0)[2]);
	G_INT (OFS_RETURN) = PR_SetEngineString (s);
}

static void PF_Spawn (void)
{
	edict_t *ed;

	ed = ED_Alloc ();

	RETURN_EDICT (ed);
}

static void PF_Remove (void)
{
	edict_t *ed;

	ed = G_EDICT (OFS_PARM0);
	ED_Free (ed);
}

// entity (entity start, .string field, string match) find = #5;
static void PF_Find (void)
{
	int			e;
	int			f;
	const char *s, *t;
	edict_t	   *ed;

	e = G_EDICTNUM (OFS_PARM0);
	f = G_INT (OFS_PARM1);
	s = G_STRING (OFS_PARM2);
	if (!s)
		PR_RunError ("PF_Find: bad search string");

	for (e++; e < qcvm->num_edicts; e++)
	{
		ed = EDICT_NUM (e);
		if (ed->free)
			continue;
		t = E_STRING (ed, f);
		if (!t)
			continue;
		if (!strcmp (t, s))
		{
			RETURN_EDICT (ed);
			return;
		}
	}

	RETURN_EDICT (qcvm->edicts);
}

static void PR_CheckEmptyString (const char *s)
{
	if (s[0] <= ' ')
		PR_RunError ("Bad string");
}

static void PF_precache_file (void)
{ // precache_file is only used to copy files with qcc, it does nothing
	G_INT (OFS_RETURN) = G_INT (OFS_PARM0);
}

int SV_Precache_Sound (const char *s)
{ // must be a persistent string.
	int i;

	for (i = 0; i < MAX_SOUNDS; i++)
	{
		if (!sv.sound_precache[i])
		{
			if (sv.state != ss_loading) // spike -- moved this so that there's no actual error any more.
			{
				Con_Warning ("PF_precache_sound(\"%s\"): Precache should only be done in spawn functions\n", s);
				// let existing clients know about it
				MSG_WriteByte (&sv.reliable_datagram, svcdp_precache);
				MSG_WriteShort (&sv.reliable_datagram, i | 0x8000);
				MSG_WriteString (&sv.reliable_datagram, s);
			}
			sv.sound_precache[i] = s;
			return i;
		}
		if (!strcmp (sv.sound_precache[i], s))
		{
			if (sv.state != ss_loading)
				Con_Warning ("PF_precache_sound(\"%s\"): Precache should only be done in spawn functions\n", s);
			return i;
		}
	}
	return 0;
}

static void PF_sv_precache_sound (void)
{
	const char *s;

	s = G_STRING (OFS_PARM0);
	G_INT (OFS_RETURN) = G_INT (OFS_PARM0);
	PR_CheckEmptyString (s);

	if (!SV_Precache_Sound (s))
		PR_RunError ("PF_precache_sound: overflow");
}

int SV_Precache_Model (const char *s)
{
	size_t i;
	for (i = 0; i < MAX_MODELS; i++)
	{
		if (!sv.model_precache[i])
		{
			if (sv.state != ss_loading)
			{
				// let existing clients know about it
				MSG_WriteByte (&sv.reliable_datagram, svcdp_precache);
				MSG_WriteShort (&sv.reliable_datagram, i | 0x0000);
				MSG_WriteString (&sv.reliable_datagram, s);
			}

			sv.model_precache[i] = s;
			sv.models[i] = Mod_ForName (s, i == 1);
			return i;
		}
		if (!strcmp (sv.model_precache[i], s))
			return i;
	}
	return 0;
}

static void PF_sv_precache_model (void)
{
	const char *s;
	int			i;

	s = G_STRING (OFS_PARM0);
	G_INT (OFS_RETURN) = G_INT (OFS_PARM0);
	PR_CheckEmptyString (s);

	for (i = 0; i < MAX_MODELS; i++)
	{
		if (!sv.model_precache[i])
		{
			if (sv.state != ss_loading)
			{
				Con_Warning ("PF_precache_model(\"%s\"): Precache should only be done in spawn functions\n", s);
				// let existing clients know about it
				MSG_WriteByte (&sv.reliable_datagram, svcdp_precache);
				MSG_WriteShort (&sv.reliable_datagram, i | 0x8000);
				MSG_WriteString (&sv.reliable_datagram, s);
			}

			sv.model_precache[i] = s;
			sv.models[i] = Mod_ForName (s, i == 1);
			return;
		}
		if (!strcmp (sv.model_precache[i], s))
		{
			if (sv.state != ss_loading && !pr_checkextension.value)
				Con_Warning ("PF_precache_model(\"%s\"): Precache should only be done in spawn functions\n", s);
			return;
		}
	}
	PR_RunError ("PF_precache_model: overflow");
}

static void PF_coredump (void)
{
	Cbuf_AddText ("edicts\n");
}

static void PF_traceon (void)
{
	qcvm->trace = true;
}

static void PF_traceoff (void)
{
	qcvm->trace = false;
}

static void PF_eprint (void)
{
	ED_PrintNum (G_EDICTNUM (OFS_PARM0));
}

/*
===============
PF_walkmove

float(float yaw, float dist) walkmove
===============
*/
static void PF_walkmove (void)
{
	edict_t		*ent;
	float		 yaw, dist;
	vec3_t		 move;
	dfunction_t *oldf;
	int			 oldself;

	ent = PROG_TO_EDICT (pr_global_struct->self);
	yaw = G_FLOAT (OFS_PARM0);
	dist = G_FLOAT (OFS_PARM1);

	if (!((int)ent->v.flags & (FL_ONGROUND | FL_FLY | FL_SWIM)))
	{
		G_FLOAT (OFS_RETURN) = 0;
		return;
	}

	yaw = yaw * M_PI * 2 / 360;

	move[0] = cos (yaw) * dist;
	move[1] = sin (yaw) * dist;
	move[2] = 0;

	// save program state, because SV_movestep may call other progs
	oldf = qcvm->xfunction;
	oldself = pr_global_struct->self;

	G_FLOAT (OFS_RETURN) = SV_movestep (ent, move, true);

	// restore program state
	qcvm->xfunction = oldf;
	pr_global_struct->self = oldself;
}

/*
===============
PF_droptofloor

void() droptofloor
===============
*/
static void PF_droptofloor (void)
{
	edict_t *ent;
	vec3_t	 end;
	trace_t	 trace;

	ent = PROG_TO_EDICT (pr_global_struct->self);

	VectorCopy (ent->v.origin, end);
	end[2] -= 256;

	trace = SV_Move (ent->v.origin, ent->v.mins, ent->v.maxs, end, false, ent);

	if (trace.fraction == 1 || trace.allsolid)
		G_FLOAT (OFS_RETURN) = 0;
	else
	{
		VectorCopy (trace.endpos, ent->v.origin);
		SV_LinkEdict (ent, false);
		ent->v.flags = (int)ent->v.flags | FL_ONGROUND;
		ent->v.groundentity = EDICT_TO_PROG (trace.ent);
		G_FLOAT (OFS_RETURN) = 1;
	}
}

/*
===============
PF_lightstyle

void(float style, string value) lightstyle
===============
*/
static void PF_sv_lightstyle (void)
{
	int			style;
	const char *val;
	client_t   *client;
	int			j;

	style = G_FLOAT (OFS_PARM0);
	val = G_STRING (OFS_PARM1);

	// bounds check to avoid clobbering sv struct
	if (style < 0 || style >= MAX_LIGHTSTYLES)
	{
		Con_DWarning ("PF_lightstyle: invalid style %d\n", style);
		return;
	}

	// change the string in sv
	sv.lightstyles[style] = val;

	// send message to all clients on this server
	if (sv.state != ss_active)
		return;

	for (j = 0, client = svs.clients; j < svs.maxclients; j++, client++)
	{
		if (client->active || client->spawned)
		{
			if (style > 0xff)
			{
				MSG_WriteByte (&client->message, svc_stufftext);
				MSG_WriteString (&client->message, va ("//ls %i \"%s\"\n", style, val));
			}
			else
			{
				MSG_WriteChar (&client->message, svc_lightstyle);
				MSG_WriteChar (&client->message, style);
				MSG_WriteString (&client->message, val);
			}
		}
	}
}

static void PF_rint (void)
{
	float f;
	f = G_FLOAT (OFS_PARM0);
	if (f > 0)
		G_FLOAT (OFS_RETURN) = (int)(f + 0.5);
	else
		G_FLOAT (OFS_RETURN) = (int)(f - 0.5);
}

static void PF_floor (void)
{
	G_FLOAT (OFS_RETURN) = floor (G_FLOAT (OFS_PARM0));
}

static void PF_ceil (void)
{
	G_FLOAT (OFS_RETURN) = ceil (G_FLOAT (OFS_PARM0));
}

/*
=============
PF_checkbottom
=============
*/
static void PF_checkbottom (void)
{
	edict_t *ent;

	ent = G_EDICT (OFS_PARM0);

	G_FLOAT (OFS_RETURN) = SV_CheckBottom (ent);
}

/*
=============
PF_pointcontents
=============
*/
static void PF_pointcontents (void)
{
	float *v;

	v = G_VECTOR (OFS_PARM0);

	G_FLOAT (OFS_RETURN) = SV_PointContents (v);
}

/*
=============
PF_nextent

entity nextent(entity)
=============
*/
static void PF_nextent (void)
{
	int		 i;
	edict_t *ent;

	i = G_EDICTNUM (OFS_PARM0);
	while (1)
	{
		i++;
		if (i == qcvm->num_edicts)
		{
			RETURN_EDICT (qcvm->edicts);
			return;
		}
		ent = EDICT_NUM (i);
		if (!ent->free)
		{
			RETURN_EDICT (ent);
			return;
		}
	}
}

/*
=============
PF_aim

Pick a vector for the player to shoot along
vector aim(entity, missilespeed)
=============
*/
cvar_t		sv_aim = {"sv_aim", "1", CVAR_NONE}; // ericw -- turn autoaim off by default. was 0.93
static void PF_aim (void)
{
	edict_t *ent, *check, *bestent;
	vec3_t	 start, dir, end, bestdir;
	int		 i, j;
	trace_t	 tr;
	float	 dist, bestdist;
	float	 speed;

	ent = G_EDICT (OFS_PARM0);
	speed = G_FLOAT (OFS_PARM1);
	(void)speed; /* variable set but not used */

	VectorCopy (ent->v.origin, start);
	start[2] += 20;

	// try sending a trace straight
	VectorCopy (pr_global_struct->v_forward, dir);
	VectorMA (start, 2048, dir, end);
	tr = SV_Move (start, vec3_origin, vec3_origin, end, false, ent);
	if (tr.ent && tr.ent->v.takedamage == DAMAGE_AIM && (!teamplay.value || ent->v.team <= 0 || ent->v.team != tr.ent->v.team))
	{
		VectorCopy (pr_global_struct->v_forward, G_VECTOR (OFS_RETURN));
		return;
	}

	// try all possible entities
	VectorCopy (dir, bestdir);
	bestdist = sv_aim.value;
	bestent = NULL;

	check = NEXT_EDICT (qcvm->edicts);
	for (i = 1; i < qcvm->num_edicts; i++, check = NEXT_EDICT (check))
	{
		if (check->v.takedamage != DAMAGE_AIM)
			continue;
		if (check == ent)
			continue;
		if (teamplay.value && ent->v.team > 0 && ent->v.team == check->v.team)
			continue; // don't aim at teammate
		for (j = 0; j < 3; j++)
			end[j] = check->v.origin[j] + 0.5 * (check->v.mins[j] + check->v.maxs[j]);
		VectorSubtract (end, start, dir);
		VectorNormalize (dir);
		dist = DotProduct (dir, pr_global_struct->v_forward);
		if (dist < bestdist)
			continue; // to far to turn
		tr = SV_Move (start, vec3_origin, vec3_origin, end, false, ent);
		if (tr.ent == check)
		{ // can shoot at this one
			bestdist = dist;
			bestent = check;
		}
	}

	if (bestent)
	{
		VectorSubtract (bestent->v.origin, ent->v.origin, dir);
		dist = DotProduct (dir, pr_global_struct->v_forward);
		VectorScale (pr_global_struct->v_forward, dist, end);
		end[2] = dir[2];
		VectorNormalize (end);
		VectorCopy (end, G_VECTOR (OFS_RETURN));
	}
	else
	{
		VectorCopy (bestdir, G_VECTOR (OFS_RETURN));
	}
}

/*
==============
PF_changeyaw

This was a major timewaster in progs, so it was converted to C
==============
*/
void PF_changeyaw (void)
{
	edict_t *ent;
	float	 ideal, current, move, speed;

	ent = PROG_TO_EDICT (pr_global_struct->self);
	current = anglemod (ent->v.angles[1]);
	ideal = ent->v.ideal_yaw;
	speed = ent->v.yaw_speed;

	if (current == ideal)
		return;
	move = ideal - current;
	if (ideal > current)
	{
		if (move >= 180)
			move = move - 360;
	}
	else
	{
		if (move <= -180)
			move = move + 360;
	}
	if (move > 0)
	{
		if (move > speed)
			move = speed;
	}
	else
	{
		if (move < -speed)
			move = -speed;
	}

	ent->v.angles[1] = anglemod (current + move);
}

/*
===============================================================================

MESSAGE WRITING

===============================================================================
*/

sizebuf_t *WriteDest (void)
{
	int		 entnum;
	int		 dest;
	edict_t *ent;

	dest = G_FLOAT (OFS_PARM0);
	switch (dest)
	{
	case MSG_BROADCAST:
		return &sv.datagram;

	case MSG_ONE:
		ent = PROG_TO_EDICT (pr_global_struct->msg_entity);
		entnum = NUM_FOR_EDICT (ent);
		if (entnum < 1 || entnum > svs.maxclients)
			PR_RunError ("WriteDest: not a client");
		return &svs.clients[entnum - 1].message;

	case MSG_ALL:
		return &sv.reliable_datagram;

	case MSG_INIT:
		return &sv.signon;

	case MSG_EXT_MULTICAST:
	case MSG_EXT_ENTITY: // just reuse it...
		return &sv.multicast;

	default:
		PR_RunError ("WriteDest: bad destination");
		break;
	}

	return NULL;
}

static void PF_sv_WriteByte (void)
{
	MSG_WriteByte (WriteDest (), G_FLOAT (OFS_PARM1));
}

static void PF_sv_WriteChar (void)
{
	MSG_WriteChar (WriteDest (), G_FLOAT (OFS_PARM1));
}

static void PF_sv_WriteShort (void)
{
	MSG_WriteShort (WriteDest (), G_FLOAT (OFS_PARM1));
}

static void PF_sv_WriteLong (void)
{
	MSG_WriteLong (WriteDest (), G_FLOAT (OFS_PARM1));
}

static void PF_sv_WriteAngle (void)
{
	MSG_WriteAngle (WriteDest (), G_FLOAT (OFS_PARM1), sv.protocolflags);
}

static void PF_sv_WriteCoord (void)
{
	MSG_WriteCoord (WriteDest (), G_FLOAT (OFS_PARM1), sv.protocolflags);
}

static void PF_sv_WriteString (void)
{
	MSG_WriteString (WriteDest (), LOC_GetString (G_STRING (OFS_PARM1)));
}

static void PF_sv_WriteEntity (void)
{
	extern unsigned int sv_protocol_pext2; // spike -- this ought to be client-specific, but we can't cope with that, so just live with the problems when
										   // ents>32768 (which QS doesn't support anyway)
	MSG_WriteEntity (WriteDest (), G_EDICTNUM (OFS_PARM1), sv_protocol_pext2);
}

//=============================================================================

static void PF_sv_makestatic (void)
{
	entity_state_t *st;
	edict_t		   *ent;

	ent = G_EDICT (OFS_PARM0);

	if (sv.num_statics == sv.max_statics)
	{
		int				nm = sv.max_statics + 128;
		entity_state_t *n = (nm * sizeof (*n) < sv.max_statics * sizeof (*n)) ? NULL : Mem_Realloc (sv.static_entities, nm * sizeof (*n));
		if (!n)
			PR_RunError ("PF_makestatic: out of memory"); // shouldn't really happen.
		sv.static_entities = n;
		memset (sv.static_entities + sv.max_statics, 0, (nm - sv.max_statics) * sizeof (*n));
		sv.max_statics = nm;
	}
	st = &sv.static_entities[sv.num_statics];
	SV_BuildEntityState (ent, st);
	if (st->alpha == ENTALPHA_ZERO)
		; // no point
	else
		sv.num_statics++;

	// throw the entity away now
	ED_Free (ent);
}

//=============================================================================

/*
==============
PF_setspawnparms
==============
*/
static void PF_sv_setspawnparms (void)
{
	edict_t	 *ent;
	int		  i;
	client_t *client;

	ent = G_EDICT (OFS_PARM0);
	i = NUM_FOR_EDICT (ent);
	if (i < 1 || i > svs.maxclients)
		PR_RunError ("Entity is not a client");

	// copy spawn parms out of the client_t
	client = svs.clients + (i - 1);

	for (i = 0; i < NUM_BASIC_SPAWN_PARMS; i++)
		(&pr_global_struct->parm1)[i] = client->spawn_parms[i];
}

/*
==============
PF_changelevel
==============
*/
static void PF_sv_changelevel (void)
{
	const char *s;

	// make sure we don't issue two changelevels
	if (svs.changelevel_issued)
		return;
	svs.changelevel_issued = true;

	s = G_STRING (OFS_PARM0);
	Cbuf_AddText (va ("changelevel %s\n", s));
}

static void PF_cl_sound (void)
{
	const char *sample;
	int			channel;
	edict_t	   *entity;
	int			volume;
	float		attenuation;
	int			entnum;

	entity = G_EDICT (OFS_PARM0);
	channel = G_FLOAT (OFS_PARM1);
	sample = G_STRING (OFS_PARM2);
	volume = G_FLOAT (OFS_PARM3) * 255;
	attenuation = G_FLOAT (OFS_PARM4);

	entnum = NUM_FOR_EDICT (entity);
	// fullcsqc fixme: if (entity->v->entnum)
	entnum *= -1;

	S_StartSound (entnum, channel, S_PrecacheSound (sample), entity->v.origin, volume, attenuation);
}
static void PF_cl_ambientsound (void)
{
	const char *samp;
	float	   *pos;
	float		vol, attenuation;

	pos = G_VECTOR (OFS_PARM0);
	samp = G_STRING (OFS_PARM1);
	vol = G_FLOAT (OFS_PARM2);
	attenuation = G_FLOAT (OFS_PARM3);

	S_StaticSound (S_PrecacheSound (samp), pos, vol, attenuation);
}

/*
==============
2021 re-release
==============
*/
void PF_sv_finalefinished (void)
{
	G_FLOAT (OFS_RETURN) = 0;
}
void PF_sv_CheckPlayerEXFlags (void)
{
	G_FLOAT (OFS_RETURN) = 0;
}
void PF_sv_walkpathtogoal (void)
{
	G_FLOAT (OFS_RETURN) = 0; /* PATH_ERROR */
}
void PF_sv_localsound (void)
{
	const char *sample;
	int			entnum;

	entnum = G_EDICTNUM (OFS_PARM0);
	sample = G_STRING (OFS_PARM1);
	if (entnum < 1 || entnum > svs.maxclients)
	{
		Con_Printf ("tried to localsound to a non-client\n");
		return;
	}
	SV_LocalSound (&svs.clients[entnum - 1], sample);
}

// clang-format off
builtin_t pr_ssqcbuiltins[] = {
	PF_Fixme,
	PF_makevectors,       // void(entity e) makevectors		= #1
	PF_setorigin,         // void(entity e, vector o) setorigin	= #2
	PF_sv_setmodel,       // void(entity e, string m) setmodel	= #3
	PF_setsize,           // void(entity e, vector min, vector max) setsize	= #4
	PF_Fixme,             // void(entity e, vector min, vector max) setabssize	= #5
	PF_break,             // void() break				= #6
	PF_random,            // float() random			= #7
	PF_sound,             // void(entity e, float chan, string samp) sound	= #8
	PF_normalize,         // vector(vector v) normalize		= #9
	PF_error,             // void(string e) error			= #10
	PF_objerror,          // void(string e) objerror		= #11
	PF_vlen,              // float(vector v) vlen			= #12
	PF_vectoyaw,          // float(vector v) vectoyaw		= #13
	PF_Spawn,             // entity() spawn			= #14
	PF_Remove,            // void(entity e) remove		= #15
	PF_traceline,         // float(vector v1, vector v2, float tryents) traceline	= #16
	PF_sv_checkclient,    // entity() clientlist			= #17
	PF_Find,              // entity(entity start, .string fld, string match) find	= #18
	PF_sv_precache_sound, // void(string s) precache_sound	= #19
	PF_sv_precache_model, // void(string s) precache_model	= #20
	PF_stuffcmd,          // void(entity client, string s)stuffcmd	= #21
	PF_findradius,        // entity(vector org, float rad) findradius	= #22
	PF_bprint,            // void(string s) bprint		= #23
	PF_sprint,            // void(entity client, string s) sprint	= #24
	PF_dprint,            // void(string s) dprint		= #25
	PF_ftos,              // void(string s) ftos			= #26
	PF_vtos,              // void(string s) vtos			= #27
	PF_coredump,
	PF_traceon,
	PF_traceoff,
	PF_eprint,   // void(entity e) debug print an entire entity
	PF_walkmove, // float(float yaw, float dist) walkmove
	PF_Fixme,    // float(float yaw, float dist) walkmove
	PF_droptofloor,
	PF_sv_lightstyle,
	PF_rint,
	PF_floor,
	PF_ceil,
	PF_Fixme,
	PF_checkbottom,
	PF_pointcontents,
	PF_Fixme,
	PF_fabs,
	PF_aim,
	PF_cvar,
	PF_localcmd,
	PF_nextent,
	PF_particle,
	PF_changeyaw,
	PF_Fixme,
	PF_vectoangles,

	PF_sv_WriteByte,
	PF_sv_WriteChar,
	PF_sv_WriteShort,
	PF_sv_WriteLong,
	PF_sv_WriteCoord,
	PF_sv_WriteAngle,
	PF_sv_WriteString,
	PF_sv_WriteEntity,

	PF_Fixme,
	PF_Fixme,
	PF_Fixme,
	PF_Fixme,
	PF_Fixme,
	PF_Fixme,
	PF_Fixme,

	SV_MoveToGoal,
	PF_precache_file,
	PF_sv_makestatic,

	PF_sv_changelevel,
	PF_Fixme,

	PF_cvar_set,
	PF_centerprint,

	PF_sv_ambientsound,

	PF_sv_precache_model,
	PF_sv_precache_sound, // precache_sound2 is different only for qcc
	PF_precache_file,

	PF_sv_setspawnparms,

	// 2021 release
	PF_sv_finalefinished, // float() finaleFinished = #79
	PF_sv_localsound,     // void localsound (entity client, string sample) = #80
	PF_Fixme,             // void draw_point (vector point, float colormap, float lifetime, float depthtest) = #81
	PF_Fixme,             // void draw_line (vector start, vector end, float colormap, float lifetime, float depthtest) = #82
	PF_Fixme,             // void draw_arrow (vector start, vector end, float colormap, float size, float lifetime, float depthtest) = #83
	PF_Fixme,             // void draw_ray (vector start, vector direction, float length, float colormap, float size, float lifetime, float depthtest) = #84
	PF_Fixme,             // void draw_circle (vector origin, float radius, float colormap, float lifetime, float depthtest) = #85
	PF_Fixme,             // void draw_bounds (vector min, vector max, float colormap, float lifetime, float depthtest) = #86
	PF_Fixme,             // void draw_worldtext (string s, vector origin, float size, float lifetime, float depthtest) = #87
	PF_Fixme,             // void draw_sphere (vector origin, float radius, float colormap, float lifetime, float depthtest) = #88
	PF_Fixme,             // void draw_cylinder (vector origin, float halfHeight, float radius, float colormap, float lifetime, float depthtest) = #89
};
int pr_ssqcnumbuiltins = sizeof (pr_ssqcbuiltins) / sizeof (pr_ssqcbuiltins[0]);

#define PF_NoCSQC   PF_Fixme
#define PF_CSQCToDo PF_Fixme
builtin_t pr_csqcbuiltins[] = {
	PF_Fixme,
	PF_makevectors, // void(entity e) makevectors		= #1
	PF_setorigin,   // void(entity e, vector o) setorigin	= #2
	PF_Fixme,       // void(entity e, string m) setmodel	= #3
	PF_setsize,     // void(entity e, vector min, vector max) setsize	= #4
	PF_Fixme,       // void(entity e, vector min, vector max) setabssize	= #5
	PF_break,       // void() break				= #6
	PF_random,      // float() random			= #7
	PF_cl_sound,    // void(entity e, float chan, string samp) sound	= #8
	PF_normalize,   // vector(vector v) normalize		= #9
	PF_error,       // void(string e) error			= #10
	PF_objerror,    // void(string e) objerror		= #11
	PF_vlen,        // float(vector v) vlen			= #12
	PF_vectoyaw,    // float(vector v) vectoyaw		= #13
	PF_Spawn,       // entity() spawn			= #14
	PF_Remove,      // void(entity e) remove		= #15
	PF_traceline,   // float(vector v1, vector v2, float tryents) traceline	= #16
	PF_NoCSQC,      // entity() checkclient (was: clientlist, apparently)			= #17
	PF_Find,        // entity(entity start, .string fld, string match) find	= #18
	PF_Fixme,       // void(string s) precache_sound	= #19
	PF_Fixme,       // void(string s) precache_model	= #20
	PF_NoCSQC,      // void(entity client, string s)stuffcmd	= #21
	PF_findradius,  // entity(vector org, float rad) findradius	= #22
	PF_NoCSQC,      // void(string s) bprint		= #23
	PF_NoCSQC,      // void(entity client, string s) sprint	= #24
	PF_dprint,      // void(string s) dprint		= #25
	PF_ftos,        // void(string s) ftos			= #26
	PF_vtos,        // void(string s) vtos			= #27
	PF_coredump,
	PF_traceon,
	PF_traceoff,
	PF_eprint,   // void(entity e) debug print an entire entity
	PF_walkmove, // float(float yaw, float dist) walkmove
	PF_Fixme,    // float(float yaw, float dist) walkmove
	PF_droptofloor,
	PF_Fixme,
	PF_rint,
	PF_floor,
	PF_ceil,
	PF_Fixme,
	PF_checkbottom,
	PF_pointcontents,
	PF_Fixme,
	PF_fabs,
	PF_NoCSQC, // PF_aim,
	PF_cvar,
	PF_localcmd,
	PF_nextent,
	PF_Fixme,
	PF_changeyaw,
	PF_Fixme,
	PF_vectoangles,

	PF_NoCSQC, // PF_WriteByte,
	PF_NoCSQC, // PF_WriteChar,
	PF_NoCSQC, // PF_WriteShort,
	PF_NoCSQC, // PF_WriteLong,
	PF_NoCSQC, // PF_WriteCoord,
	PF_NoCSQC, // PF_WriteAngle,
	PF_NoCSQC, // PF_WriteString,
	PF_NoCSQC, // PF_WriteEntity,

	PF_Fixme,
	PF_Fixme,
	PF_Fixme,
	PF_Fixme,
	PF_Fixme,
	PF_Fixme,
	PF_Fixme,

	SV_MoveToGoal,
	PF_precache_file,
	PF_Fixme,

	PF_NoCSQC, // PF_changelevel,
	PF_Fixme,

	PF_cvar_set,
	PF_NoCSQC, // PF_centerprint,

	PF_cl_ambientsound,

	PF_Fixme,
	PF_Fixme,
	PF_precache_file,

	PF_NoCSQC, // PF_setspawnparms
};
// clang-format on
int pr_csqcnumbuiltins = sizeof (pr_csqcbuiltins) / sizeof (pr_csqcbuiltins[0]);