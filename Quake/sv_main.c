/*
Copyright (C) 1996-2001 Id Software, Inc.
Copyright (C) 2002-2009 John Fitzgibbons and others
Copyright (C) 2010-2014 QuakeSpasm developers
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
// sv_main.c -- server main program

#include "quakedef.h"

server_t		sv;
server_static_t svs;

static char localmodels[MAX_MODELS][8]; // inline model names for precache

int sv_protocol = PROTOCOL_RMQ; // spike -- enough maps need this now that we can probably afford incompatibility with engines that still don't support 999
								// (vanilla was already broken) -- PROTOCOL_FITZQUAKE; //johnfitz
unsigned int sv_protocol_pext1 = PEXT1_SUPPORTED_SERVER; // spike
unsigned int sv_protocol_pext2 = PEXT2_SUPPORTED_SERVER; // spike

static cvar_t sv_netsort = {"sv_netsort", "1", CVAR_NONE};
static cvar_t sv_smoothplatformlerps = {"sv_smoothplatformlerps", "1", CVAR_NONE};

/*
=============
SV_UsePredThinkPos
=============
*/
static qboolean SV_UsePredThinkPos (edict_t *ent)
{
	extern cvar_t r_lerpmove;
	if (!sv_smoothplatformlerps.value || (!isDedicated && !r_lerpmove.value))
		return false;
	if (ent->v.movetype != MOVETYPE_STEP)
		return false;
	if (!((int)ent->v.flags & FL_ONGROUND))
		return false;
	float elapsedtime = qcvm->time - ent->lastthink;
	if (elapsedtime < 0 || elapsedtime > 0.1)
		return false;
	return true;
}

//============================================================================

void SV_CalcStats (client_t *client, int *statsi, float *statsf, const char **statss)
{
	size_t	 i;
	edict_t *ent = client->edict;
	// FIXME: string stats!
	int		 items;
	eval_t	*val = GetEdictFieldValue (ent, qcvm->extfields.items2);
	if (val)
		items = (int)((uint32_t)ent->v.items | ((uint32_t)val->_float << 23));
	else
		items = (int)((uint32_t)ent->v.items | ((uint32_t)pr_global_struct->serverflags << 28));

	memset (statsi, 0, sizeof (*statsi) * MAX_CL_STATS);
	memset (statsf, 0, sizeof (*statsf) * MAX_CL_STATS);
	memset ((void *)statss, 0, sizeof (*statss) * MAX_CL_STATS);
	statsf[STAT_HEALTH] = ent->v.health;
	statsi[STAT_WEAPON] = SV_ModelIndex (PR_GetString (ent->v.weaponmodel));
	if ((unsigned int)statsi[STAT_WEAPON] >= client->limit_models)
		statsi[STAT_WEAPON] = 0;
	statsf[STAT_AMMO] = ent->v.currentammo;
	statsf[STAT_ARMOR] = ent->v.armorvalue;
	statsf[STAT_WEAPONFRAME] = ent->v.weaponframe;
	statsf[STAT_SHELLS] = ent->v.ammo_shells;
	statsf[STAT_NAILS] = ent->v.ammo_nails;
	statsf[STAT_ROCKETS] = ent->v.ammo_rockets;
	statsf[STAT_CELLS] = ent->v.ammo_cells;
	statsf[STAT_ACTIVEWEAPON] = ent->v.weapon; // sent in a way that does NOT depend upon the current mod...
	if ((val = GetEdictFieldValue (ent, qcvm->extfields.viewzoom)) && val->_float)
	{
		statsf[STAT_VIEWZOOM] = val->_float * 255;
		if (statsf[STAT_VIEWZOOM] < 1)
			statsf[STAT_VIEWZOOM] = 1;
	}

	if (client->protocol_pext2 & PEXT2_PREDINFO)
	{ // predinfo also kills clc_clientdata
		statsi[STAT_ITEMS] = items;
		statsf[STAT_VIEWHEIGHT] = ent->v.view_ofs[2];
		statsf[STAT_IDEALPITCH] = ent->v.idealpitch;
		statsf[STAT_PUNCHANGLE_X] = ent->v.punchangle[0];
		statsf[STAT_PUNCHANGLE_Y] = ent->v.punchangle[1];
		statsf[STAT_PUNCHANGLE_Z] = ent->v.punchangle[2];
	}

	for (i = 0; i < sv.numcustomstats; i++)
	{
		eval_t *eval = sv.customstats[i].ptr;
		if (!eval)
			eval = GetEdictFieldValue (ent, sv.customstats[i].fld);

		switch (sv.customstats[i].type)
		{
		case ev_ext_integer:
			statsi[sv.customstats[i].idx] = eval->_int;
			break;
		case ev_entity:
			statsi[sv.customstats[i].idx] = NUM_FOR_EDICT (PROG_TO_EDICT (eval->edict));
			break;
		case ev_float:
			statsf[sv.customstats[i].idx] = eval->_float;
			break;
		case ev_vector:
			statsf[sv.customstats[i].idx + 0] = eval->vector[0];
			statsf[sv.customstats[i].idx + 1] = eval->vector[1];
			statsf[sv.customstats[i].idx + 2] = eval->vector[2];
			break;
		case ev_string: // not supported in this build... send with svcfte_updatestatstring on change, which is annoying.
			statss[sv.customstats[i].idx] = PR_GetString (eval->string);
			break;
		case ev_void:	  // nothing...
		case ev_field:	  // panic! everyone panic!
		case ev_function: // doesn't make much sense
		case ev_pointer:  // doesn't make sense
		default:
			break;
		}
	}
}

/*server-side-only flags that re-use encoding bits*/
#define UF_REMOVE		   UF_16BIT	   /*says we removed the entity in this frame*/
#define UF_MOVETYPE		   UF_EFFECTS2 /*this flag isn't present in the header itself*/
#define UF_RESET2		   UF_EXTEND1  /*so new ents are reset multiple times to avoid weird baselines*/
// #define UF_UNUSED		UF_EXTEND2	/**/
#define UF_WEAPONFRAME_OLD UF_EXTEND2
#define UF_VIEWANGLES	   UF_EXTEND3 /**/

static unsigned int SVFTE_DeltaPredCalcBits (entity_state_t *from, entity_state_t *to)
{
	unsigned int bits = 0;
	//	if (from && from->pmovetype != to->pmovetype)
	//		bits |= UFP_MOVETYPE;

	//	if (to->movement[0])
	//		bits |= UFP_FORWARD;
	//	if (to->movement[1])
	//		bits |= UFP_SIDE;
	//	if (to->movement[2])
	//		bits |= UFP_UP;
	if (to->velocity[0])
		bits |= UFP_VELOCITYXY;
	if (to->velocity[1])
		bits |= UFP_VELOCITYXY;
	if (to->velocity[2])
		bits |= UFP_VELOCITYZ;
	//	if (to->msec)
	//		bits |= UFP_MSEC;

	return bits;
}

static unsigned int MSGFTE_DeltaCalcBits (entity_state_t *from, entity_state_t *to)
{
	unsigned int bits = 0;

	if (from->pmovetype != to->pmovetype)
		bits |= UF_PREDINFO | UF_MOVETYPE;
	{
		if (SVFTE_DeltaPredCalcBits (from, to))
			bits |= UF_PREDINFO;

		// moving players get extra data forced upon them which is not deltatracked
		if ((bits & UF_PREDINFO) && (from->velocity[0] || from->velocity[1] || from->velocity[2]))
		{
			// if we've got player movement then write the origin anyway, to cover packetloss
			bits |= UF_ORIGINXY | UF_ORIGINZ;
		}
	}

	if (to->origin[0] != from->origin[0])
		bits |= UF_ORIGINXY;
	if (to->origin[1] != from->origin[1])
		bits |= UF_ORIGINXY;
	if (to->origin[2] != from->origin[2])
		bits |= UF_ORIGINZ;

	if (to->angles[0] != from->angles[0])
		bits |= UF_ANGLESXZ;
	if (to->angles[1] != from->angles[1])
		bits |= UF_ANGLESY;
	if (to->angles[2] != from->angles[2])
		bits |= UF_ANGLESXZ;

	if (to->modelindex != from->modelindex)
		bits |= UF_MODEL;
	if (to->frame != from->frame)
		bits |= UF_FRAME;
	if (to->skin != from->skin)
		bits |= UF_SKIN;
	if (to->colormap != from->colormap)
		bits |= UF_COLORMAP;
	if (to->effects != from->effects)
		bits |= UF_EFFECTS;
	if (to->eflags != from->eflags)
		bits |= UF_FLAGS;
	if (to->scale != from->scale)
		bits |= UF_SCALE;
	if (to->alpha != from->alpha)
		bits |= UF_ALPHA;
	if (to->colormod[0] != from->colormod[0] || to->colormod[1] != from->colormod[1] || to->colormod[2] != from->colormod[2])
		bits |= UF_COLORMOD;
	if (to->tagentity != from->tagentity || to->tagindex != from->tagindex)
		bits |= UF_TAGINFO;
	if (to->traileffectnum != from->traileffectnum || to->emiteffectnum != from->emiteffectnum)
		bits |= UF_TRAILEFFECT;
#ifdef LERP_BANDAID
	if (to->lerp != from->lerp)
		bits |= UF_UNUSED2;
#endif

	return bits;
}

static void MSGFTE_WriteEntityUpdate (unsigned int bits, entity_state_t *state, sizebuf_t *msg, unsigned int pext2, unsigned int protocolflags)
{
	unsigned int predbits = 0;
	if (bits & UF_MOVETYPE)
	{
		bits &= ~UF_MOVETYPE;
		predbits |= UFP_MOVETYPE;
	}
	if (pext2 & PEXT2_PREDINFO)
	{
		if (bits & UF_VIEWANGLES)
		{
			bits &= ~UF_VIEWANGLES;
			bits |= UF_PREDINFO;
			predbits |= UFP_VIEWANGLE;
		}
	}
	else
	{
		if (bits & UF_VIEWANGLES)
		{
			bits &= ~UF_VIEWANGLES;
			bits |= UF_PREDINFO;
		}
		if (bits & UF_WEAPONFRAME_OLD)
		{
			bits &= ~UF_WEAPONFRAME_OLD;
			predbits |= UFP_WEAPONFRAME_OLD;
		}
	}

#ifdef LERP_BANDAID
	if (bits & UF_UNUSED2 && (cls.demorecording || strcmp (NET_QSocketGetTrueAddressString (host_client->netconnection), "LOCAL")))
		bits &= ~UF_UNUSED2;
#endif

	bits &= ~UF_BONEDATA;

	/*check if we need more precision for some things*/
	if ((bits & UF_MODEL) && state->modelindex > 255)
		bits |= UF_16BIT;
	if ((bits & UF_FRAME) && state->frame > 255)
		bits |= UF_16BIT;

	/*convert effects bits to higher lengths if needed*/
	if (bits & UF_EFFECTS)
	{
		if (state->effects & 0xffff0000) /*both*/
			bits |= UF_EFFECTS | UF_EFFECTS2;
		else if (state->effects & 0x0000ff00) /*2 only*/
			bits = (bits & ~UF_EFFECTS) | UF_EFFECTS2;
	}
	if (bits & 0xff000000)
		bits |= UF_EXTEND3;
	if (bits & 0x00ff0000)
		bits |= UF_EXTEND2;
	if (bits & 0x0000ff00)
		bits |= UF_EXTEND1;

	MSG_WriteByte (msg, (bits >> 0) & 0xff);
	if (bits & UF_EXTEND1)
		MSG_WriteByte (msg, (bits >> 8) & 0xff);
	if (bits & UF_EXTEND2)
		MSG_WriteByte (msg, (bits >> 16) & 0xff);
	if (bits & UF_EXTEND3)
		MSG_WriteByte (msg, (bits >> 24) & 0xff);

	if (bits & UF_FRAME)
	{
		if (bits & UF_16BIT)
			MSG_WriteShort (msg, state->frame);
		else
			MSG_WriteByte (msg, state->frame);
	}
	if (bits & UF_ORIGINXY)
	{
		MSG_WriteCoord (msg, state->origin[0], protocolflags);
		MSG_WriteCoord (msg, state->origin[1], protocolflags);
	}
	if (bits & UF_ORIGINZ)
		MSG_WriteCoord (msg, state->origin[2], protocolflags);

	if ((bits & UF_PREDINFO) && !(pext2 & PEXT2_PREDINFO))
	{ /*if we have pred info, (always) use more precise angles*/
		if (bits & UF_ANGLESXZ)
		{
			MSG_WriteAngle16 (msg, state->angles[0], protocolflags);
			MSG_WriteAngle16 (msg, state->angles[2], protocolflags);
		}
		if (bits & UF_ANGLESY)
			MSG_WriteAngle16 (msg, state->angles[1], protocolflags);
	}
	else
	{
		if (bits & UF_ANGLESXZ)
		{
			MSG_WriteAngle (msg, state->angles[0], protocolflags);
			MSG_WriteAngle (msg, state->angles[2], protocolflags);
		}
		if (bits & UF_ANGLESY)
			MSG_WriteAngle (msg, state->angles[1], protocolflags);
	}

	if ((bits & (UF_EFFECTS | UF_EFFECTS2)) == (UF_EFFECTS | UF_EFFECTS2))
		MSG_WriteLong (msg, state->effects);
	else if (bits & UF_EFFECTS2)
		MSG_WriteShort (msg, state->effects);
	else if (bits & UF_EFFECTS)
		MSG_WriteByte (msg, state->effects);

	if (bits & UF_PREDINFO)
	{
		/*movetype is set above somewhere*/
		predbits |= SVFTE_DeltaPredCalcBits (NULL, state);

		MSG_WriteByte (msg, predbits);
		if (predbits & UFP_MOVETYPE)
			MSG_WriteByte (msg, state->pmovetype);
		if (predbits & UFP_VELOCITYXY)
		{
			MSG_WriteShort (msg, state->velocity[0]);
			MSG_WriteShort (msg, state->velocity[1]);
		}
		if (predbits & UFP_VELOCITYZ)
			MSG_WriteShort (msg, state->velocity[2]);
	}

	if (bits & UF_MODEL)
	{
		if (bits & UF_16BIT)
			MSG_WriteShort (msg, state->modelindex);
		else
			MSG_WriteByte (msg, state->modelindex);
	}
	if (bits & UF_SKIN)
	{
		if (bits & UF_16BIT)
			MSG_WriteShort (msg, state->skin);
		else
			MSG_WriteByte (msg, state->skin);
	}
	if (bits & UF_COLORMAP)
		MSG_WriteByte (msg, state->colormap & 0xff);
	if (bits & UF_FLAGS)
		MSG_WriteByte (msg, state->eflags);

	if (bits & UF_ALPHA)
		MSG_WriteByte (msg, (state->alpha - 1) & 0xff);
	if (bits & UF_SCALE)
		MSG_WriteByte (msg, state->scale);

	if (bits & UF_TAGINFO)
	{
		MSG_WriteEntity (msg, state->tagentity, pext2);
		MSG_WriteByte (msg, state->tagindex);
	}

	if (bits & UF_TRAILEFFECT)
	{
		if (state->emiteffectnum)
		{ // 3 spare bits. so that's nice (this is guarenteed to be 14 bits max due to precaches using the upper two bits).
			MSG_WriteShort (msg, (state->traileffectnum & 0x3fff) | 0x8000);
			MSG_WriteShort (msg, state->emiteffectnum & 0x3fff);
		}
		else
			MSG_WriteShort (msg, state->traileffectnum & 0x3fff);
	}

	if (bits & UF_COLORMOD)
	{
		MSG_WriteByte (msg, state->colormod[0]);
		MSG_WriteByte (msg, state->colormod[1]);
		MSG_WriteByte (msg, state->colormod[2]);
	}

#ifdef LERP_BANDAID
	if (bits & UF_UNUSED2)
		MSG_WriteShort (msg, state->lerp);
#endif
}

static struct entity_num_state_s *snapshot_entstate;
static size_t					  snapshot_numents;
static size_t					  snapshot_maxents;

void SVFTE_DestroyFrames (client_t *client)
{
	int i;
	for (i = 0; i < MAX_CL_STATS; i++)
	{
		if (!client->oldstats_s[i])
			continue;
		Mem_Free (client->oldstats_s[i]);
		client->oldstats_s[i] = 0;
	}
	if (client->previousentities)
		Mem_Free (client->previousentities);
	client->previousentities = NULL;
	client->numpreviousentities = 0;
	client->maxpreviousentities = 0;

	if (client->pendingentities_bits)
		Mem_Free (client->pendingentities_bits);
	client->pendingentities_bits = NULL;
	client->numpendingentities = 0;

	while (client->numframes > 0)
	{
		client->numframes--;
		Mem_Free (client->frames[client->numframes].ents);
	}
	if (client->frames)
		Mem_Free (client->frames);
	client->frames = NULL;

	client->lastacksequence = 0;
}
static void SVFTE_SetupFrames (client_t *client)
{
	size_t fr;
	// the client will clear out their stats on receipt of the svc_serverinfo packet.
	// we won't send any reliables until they receive it
	// so it should be enough to just clear these here, and they'll get their new stats with the first entity update once they're spawned
	memset (client->oldstats_i, 0, sizeof (client->oldstats_i));
	memset (client->oldstats_f, 0, sizeof (client->oldstats_f));
	client->lastmovemessage = 0; // it'll clear this too

	if (!client->protocol_pext2)
	{
		SVFTE_DestroyFrames (client);
		return;
	}

	client->numframes = 64; // must be power-of-two
	client->frames = Mem_Alloc (sizeof (*client->frames) * client->numframes);
	client->lastacksequence = (int)0x80000000;
	memset (client->frames, 0, sizeof (*client->frames) * client->numframes);
	for (fr = 0; fr < client->numframes; fr++)
		client->frames[fr].sequence = client->lastacksequence;

	client->numpendingentities = qcvm->num_edicts;
	client->pendingentities_bits = Mem_Alloc (client->numpendingentities * sizeof (*client->pendingentities_bits));

	client->pendingentities_bits[0] = UF_REMOVE;
}
static void SVFTE_DroppedFrame (client_t *client, int sequence)
{
	int					 i;
	struct deltaframe_s *frame = &client->frames[sequence & (client->numframes - 1)];
	if (frame->sequence != sequence)
		return; // this frame was stale... client is running too far behind. we'll probably be spamming resends as a result.
	frame->sequence = -1;
	// flag their stats for resend
	for (i = 0; i < MAX_CL_STATS / 32; i++)
	{
		client->resendstatsnum[i] |= frame->resendstatsnum[i];
		client->resendstatsstr[i] |= frame->resendstatsstr[i];
	}
	// flag their various entities as needing a resend too.
	for (i = 0; i < frame->numents; i++)
	{
		if (frame->ents[i].ebits)
			client->pendingentities_bits[frame->ents[i].num] |= frame->ents[i].ebits;
	}
}
void SVFTE_Ack (client_t *client, int sequence)
{ // any gaps in the sequence need to considered dropped
	struct deltaframe_s *frame;
	int					 dropseq = client->lastacksequence + 1;
	if (!client->numframes)
		return; // client shouldn't be using this.
	if (sequence == -1)
		client->pendingentities_bits[0] |=
			UF_REMOVE; // client wants a full resend. which might happen from it just starting to record a demo, saving it from writing all the deltas out.
	if (sequence < client->lastacksequence)
	{
		//		else Con_SafePrintf("dupe or stale ack (%s, %i->%i)\n", client->name, client->lastacksequence, sequence);
		return; // panic
	}
	if ((unsigned)(dropseq - sequence) >= client->numframes)
		dropseq = sequence - client->numframes;
	while (dropseq < sequence)
		SVFTE_DroppedFrame (client, dropseq++);
	client->lastacksequence = sequence;

	frame = &client->frames[sequence & (client->numframes - 1)];
	if (frame->sequence >= 0)
	{
		frame->sequence = -1;
		host_client->ping_times[host_client->num_pings % NUM_PING_TIMES] = qcvm->time - frame->timestamp;
		host_client->num_pings++;
	}
}
static void SVFTE_WriteStats (client_t *client, sizebuf_t *msg)
{
	int					 statsi[MAX_CL_STATS];
	float				 statsf[MAX_CL_STATS];
	const char			*statss[MAX_CL_STATS];
	int					 i;
	struct deltaframe_s *frame;
	int					 sequence = NET_QSocketGetSequenceOut (client->netconnection);
	int					 maxstats;

	if (client->protocol_pext2 & PEXT2_REPLACEMENTDELTAS)
		maxstats = MAX_CL_STATS;
	else
		maxstats = 32;

	frame = &client->frames[sequence & (client->numframes - 1)];

	if (frame->sequence == sequence - (int)client->numframes) // client is getting behind... this may get really spammy, lets hope it clears up at some point
		SVFTE_DroppedFrame (client, frame->sequence);

	// figure out the current values in a nice easy way (yay for copying to make arrays easier!)
	SV_CalcStats (client, statsi, statsf, statss);

	for (i = 0; i < maxstats; i++)
	{
		// small cleanup
		if (!statsi[i])
			statsi[i] = statsf[i];
		else
			statsf[i] = 0; // statsi[i];

		// if it changed flag for sending
		if (statsi[i] != client->oldstats_i[i] || statsf[i] != client->oldstats_f[i])
		{
			client->oldstats_i[i] = statsi[i];
			client->oldstats_f[i] = statsf[i];
			client->resendstatsnum[i / 32] |= 1u << (i & 31);
		}

		if (statss[i] || client->oldstats_s[i])
		{
			const char *os = client->oldstats_s[i];
			const char *ns = statss[i];
			if (!ns)
				ns = "";
			if (!os)
				os = "";
			if (strcmp (os, ns))
			{
				client->resendstatsstr[i / 32] |= 1u << (i & 31);
				Mem_Free (client->oldstats_s[i]);
				client->oldstats_s[i] = q_strdup (ns);
			}
		}

		// if its flagged then unflag it, log it, and send it
		if (client->resendstatsnum[i / 32] & (1u << (i & 31)))
		{
			client->resendstatsnum[i / 32] &= ~(1u << (i & 31));
			frame->resendstatsnum[i / 32] |= 1u << (i & 31);

			if ((double)statsi[i] != statsf[i] && statsf[i])
			{ // didn't round nicely, so send as a float
				MSG_WriteByte (msg, svcfte_updatestatfloat);
				MSG_WriteByte (msg, i);
				MSG_WriteFloat (msg, statsf[i]);
			}
			else
			{
				if (statsi[i] < 0 || statsi[i] > 255)
				{ // needs to be big
					MSG_WriteByte (msg, svc_updatestat);
					MSG_WriteByte (msg, i);
					MSG_WriteLong (msg, statsi[i]);
				}
				else
				{ // can be fairly small
					MSG_WriteByte (msg, svcdp_updatestatbyte);
					MSG_WriteByte (msg, i);
					MSG_WriteByte (msg, statsi[i]);
				}
			}
		}
		// if its flagged then unflag it, log it, and send it
		if (client->resendstatsstr[i / 32] & (1u << (i & 31)))
		{
			client->resendstatsstr[i / 32] &= ~(1u << (i & 31));
			frame->resendstatsstr[i / 32] |= 1u << (i & 31);

			MSG_WriteByte (msg, svcfte_updatestatstring);
			MSG_WriteByte (msg, i);
			if (statss[i])
				MSG_WriteString (msg, statss[i]);
			else
				MSG_WriteString (msg, NULL);
		}
	}
}
static void SVFTE_CalcEntityDeltas (client_t *client)
{
	struct entity_num_state_s *olds, *news, *oldstop, *newstop;

	if ((int)client->numpendingentities < qcvm->num_edicts)
	{
		int newmax = qcvm->num_edicts + 64;
		client->pendingentities_bits = Mem_Realloc (client->pendingentities_bits, sizeof (*client->pendingentities_bits) * newmax);
		memset (client->pendingentities_bits + client->numpendingentities, 0, sizeof (*client->pendingentities_bits) * (newmax - client->numpendingentities));
		client->numpendingentities = newmax;
	}

	// if we're clearing the list and starting from scratch, just wipe all lingering state
	if (client->pendingentities_bits[0] & UF_REMOVE)
	{
		client->numpreviousentities = 0;
		client->pendingentities_bits[0] = UF_REMOVE;
	}

	news = snapshot_entstate;
	newstop = news + snapshot_numents;
	olds = client->previousentities;
	oldstop = (olds != NULL) ? (olds + client->numpreviousentities) : NULL;

	// we have two sets of entity state, pvs culled etc already.
	// figure out which flags changed,
	for (;;)
	{
		if (olds == oldstop && news == newstop)
			break;
		if (news == newstop || (olds != oldstop && olds->num < news->num))
		{
			// old ent is no longer visible, so flag for removal.
			client->pendingentities_bits[olds->num] = UF_REMOVE;
			olds++;
		}
		else if (olds == oldstop || (news != newstop && news->num < olds->num))
		{
			// new ent is new this frame, so reset everything.
			client->pendingentities_bits[news->num] = UF_RESET;
			// don't need to calc the other bits here, resets are enough
			news++;
		}
		else
		{ // simple entity delta
			// its flagged for removing, that's weird... must be some killer packetloss. turn that back into a reset or something
			if (client->pendingentities_bits[news->num] & UF_REMOVE)
				client->pendingentities_bits[news->num] = (client->pendingentities_bits[news->num] & ~UF_REMOVE) | UF_RESET2;
			client->pendingentities_bits[news->num] |= MSGFTE_DeltaCalcBits (&olds->state, &news->state);
			news++;
			olds++;
		}
	}

	// now we know what flags to apply, the client needs a copy of that state for the next frame too.
	// outgoing data can just read off these states too, instead of needing to hit the edicts memory (which may be spread over multiple allocations, yay cache).
	// to avoid a potentially large memcopy, I'm just going to swap these buffers.
	olds = client->previousentities;
	oldstop = (olds != NULL) ? (olds + client->maxpreviousentities) : NULL;

	client->previousentities = snapshot_entstate;
	client->numpreviousentities = snapshot_numents;
	client->maxpreviousentities = snapshot_maxents;

	snapshot_entstate = olds;
	snapshot_numents = 0;
	snapshot_maxents = (olds != NULL) ? (oldstop - olds) : 0;
}
static void SVFTE_WriteEntitiesToClient (client_t *client, sizebuf_t *msg, size_t overflowsize)
{
	struct entity_num_state_s *state, *stateend;
	unsigned int			   entbits, logbits, netbits;
	size_t					   entnum;
	int						   sequence = NET_QSocketGetSequenceOut (client->netconnection);
	size_t					   origmaxsize = msg->maxsize;
	size_t					   rollbacksize; // I'm too lazy to figure out sizes (especially if someone updates this for bone states or whatever)
	struct deltaframe_s		  *frame = &client->frames[sequence & (client->numframes - 1)];
	frame->sequence = sequence; // so we know that it wasn't stale later.
	frame->timestamp = qcvm->time;

	msg->maxsize = overflowsize;

	state = client->previousentities;
	stateend = state + client->numpreviousentities;

	MSG_WriteByte (msg, svcfte_updateentities);

	frame->numents = 0;
	if (client->protocol_pext2 & PEXT2_PREDINFO)
		MSG_WriteShort (msg, (client->lastmovemessage & 0xffff));
	MSG_WriteFloat (msg, frame->timestamp); // should be the time the last physics frame was run.
	for (entnum = client->snapshotresume; entnum < client->numpendingentities; entnum++)
	{
		entbits = client->pendingentities_bits[entnum];
		if (!(entbits & ~UF_RESET2))
			continue; // nothing to send (if reset2 is still set, then leave it pending until there's more data

		rollbacksize = msg->cursize;
		client->pendingentities_bits[entnum] = 0;
		logbits = 0;
		if (entbits & UF_REMOVE)
		{
			if (entnum > 0x3fff)
			{
				MSG_WriteShort (msg, 0xc000 | (entnum & 0x3fff));
				MSG_WriteByte (msg, entnum >> 14);
			}
			else
				MSG_WriteShort (msg, 0x8000 | entnum);
			logbits = UF_REMOVE;
		}
		else
		{
			while (state < stateend && state->num < entnum)
				state++;
			if (state < stateend && state->num == entnum)
			{
				if (entbits & UF_RESET2)
				{
					/*if reset2, then this is the second packet sent to the client and should have a forced reset (but which isn't tracked)*/
					logbits = entbits & ~(UF_RESET | UF_RESET2);
					netbits = UF_RESET | MSGFTE_DeltaCalcBits (&EDICT_NUM (entnum)->baseline, &state->state);
					//					Con_Printf("RESET2 %u @ %i\n", (int)entnum, sequence);
				}
				else if (entbits & UF_RESET)
				{
					/*flag the entity for the next packet, so we always get two resets when it appears, to reduce the effects of packetloss on seeing rockets
					 * etc*/
					client->pendingentities_bits[entnum] = UF_RESET2;
					netbits = UF_RESET | MSGFTE_DeltaCalcBits (&EDICT_NUM (entnum)->baseline, &state->state);
					logbits = UF_RESET;
					//					Con_Printf("RESET %u @ %i\n", (int)entnum, sequence);
				}
				else
					logbits = netbits = entbits;

				if (entnum >= 0x4000)
				{
					MSG_WriteShort (msg, 0x4000 | (entnum & 0x3fff));
					MSG_WriteByte (msg, entnum >> 14);
				}
				else
					MSG_WriteShort (msg, entnum);
				//				SV_EmitDeltaEntIndex(msg, j, false, true);
				MSGFTE_WriteEntityUpdate (netbits, &state->state, msg, client->protocol_pext2, sv.protocolflags);
			}
		}

		if ((size_t)msg->cursize + 2 > origmaxsize)
		{
			msg->cursize = rollbacksize;					// roll back
			client->pendingentities_bits[entnum] = entbits; // make sure those bits get re-applied later.
			break;
		}
		if (frame->numents == frame->maxents)
		{
			frame->maxents += 64;
			frame->ents = Mem_Realloc (frame->ents, sizeof (*frame->ents) * frame->maxents);
		}
		frame->ents[frame->numents].num = entnum;
		frame->ents[frame->numents].ebits = logbits;
		frame->ents[frame->numents].csqcbits = 0;
		frame->numents++;
	}
	msg->maxsize = origmaxsize;
	MSG_WriteShort (msg, 0); // eom

	// remember how far we got, so we can keep things flushed, instead of only updating the first N entities.
	client->snapshotresume = entnum;

	if (msg->cursize > 1024 && dev_peakstats.packetsize <= 1024)
		Con_DWarning ("%i byte packet exceeds standard limit of 1024.\n", msg->cursize);
	dev_stats.packetsize = msg->cursize;
	dev_peakstats.packetsize = q_max (msg->cursize, dev_peakstats.packetsize);
}

/*
SV_BuildEntityState
copies edict state into a more compact entity_state_t with all the extension fields etc sorted out and neatened up for network precision.
note: ignores viewmodelforclient and other client-specific stuff.
*/
void SV_BuildEntityState (edict_t *ent, entity_state_t *state)
{
	eval_t *val;
	state->eflags = 0;
	if (SV_UsePredThinkPos (ent))
		VectorCopy (ent->predthinkpos, state->origin);
	else
		VectorCopy (ent->v.origin, state->origin);
	VectorCopy (ent->v.angles, state->angles);
	state->modelindex = ent->v.modelindex;
	state->frame = ent->v.frame;
	state->colormap = ent->v.colormap;
	state->skin = ent->v.skin;
	if ((val = GetEdictFieldValue (ent, qcvm->extfields.scale)))
		state->scale = ENTSCALE_ENCODE (val->_float);
	else
		state->scale = ENTSCALE_DEFAULT;
	if ((val = GetEdictFieldValue (ent, qcvm->extfields.alpha)))
		state->alpha = ENTALPHA_ENCODE (val->_float);
	else
		state->alpha = ent->alpha;
	if ((val = GetEdictFieldValue (ent, qcvm->extfields.colormod)) && (val->vector[0] || val->vector[1] || val->vector[2]))
	{
		state->colormod[0] = val->vector[0] * 32;
		state->colormod[1] = val->vector[1] * 32;
		state->colormod[2] = val->vector[2] * 32;
	}
	else
		state->colormod[0] = state->colormod[1] = state->colormod[2] = 32;
	state->traileffectnum = qcvm->extfields.traileffectnum >= 0 ? GetEdictFieldValue (ent, qcvm->extfields.traileffectnum)->_float : 0;
	state->emiteffectnum = qcvm->extfields.emiteffectnum >= 0 ? GetEdictFieldValue (ent, qcvm->extfields.emiteffectnum)->_float : 0;
	if ((val = GetEdictFieldValue (ent, qcvm->extfields.tag_entity)) && val->edict)
		state->tagentity = NUM_FOR_EDICT (PROG_TO_EDICT (val->edict));
	else
		state->tagentity = 0;
	if ((val = GetEdictFieldValue (ent, qcvm->extfields.tag_index)))
		state->tagindex = val->_float;
	else
		state->tagindex = 0;
	state->effects = (int)ent->v.effects & sv.effectsmask;
	if ((val = GetEdictFieldValue (ent, qcvm->extfields.modelflags)))
		state->effects |= ((unsigned int)val->_float) << 24;
	if (ent->v.movetype == MOVETYPE_STEP)
		state->eflags |= EFLAGS_STEP;

	state->pmovetype = 0;
	state->velocity[0] = state->velocity[1] = state->velocity[2] = 0;

#ifdef LERP_BANDAID
	state->lerp = ent->sendinterval ? Q_rint ((ent->v.nextthink - qcvm->time) * 1000) + 1 : 0;
#endif
}

byte	   *SV_FatPVS (vec3_t org, qmodel_t *worldmodel);
static void SVFTE_BuildSnapshotForClient (client_t *client)
{
	unsigned int  e, i;
	byte		 *pvs;
	vec3_t		  org;
	edict_t		 *ent, *parent;
	unsigned int  maxentities = client->limit_entities;
	edict_t		 *clent = client->edict;
	unsigned char eflags;

	struct entity_num_state_s *ents = snapshot_entstate;
	size_t					   numents = 0;
	size_t					   maxents = snapshot_maxents;

	// find the client's PVS
	VectorAdd (clent->v.origin, clent->v.view_ofs, org);
	pvs = SV_FatPVS (org, qcvm->worldmodel);

	if (maxentities > (unsigned int)qcvm->num_edicts)
		maxentities = (unsigned int)qcvm->num_edicts;

	// send over all entities (excpet the client) that touch the pvs
	ent = NEXT_EDICT (qcvm->edicts);
	for (e = 1; e < maxentities; e++, ent = NEXT_EDICT (ent))
	{
		eflags = 0;
		if (ent != clent) // clent is ALLWAYS sent
		{
			// ignore ents without visible models
			if ((!ent->v.modelindex || !PR_GetString (ent->v.model)[0]))
			{
			invisible:
				continue;
			}

			{
				// attached entities should use the pvs of the parent rather than the child (because the child will typically be bugging out around '0 0 0', so
				// won't be useful)
				parent = ent;
				if (parent->num_leafs)
				{
					// ignore if not touching a PV leaf
					for (i = 0; i < parent->num_leafs; i++)
						if (pvs[parent->leafnums[i] >> 3] & (1 << (parent->leafnums[i] & 7)))
							break;

					// ericw -- added ent->num_leafs < MAX_ENT_LEAFS condition.
					//
					// if ent->num_leafs == MAX_ENT_LEAFS, the ent is visible from too many leafs
					// for us to say whether it's in the PVS, so don't try to vis cull it.
					// this commonly happens with rotators, because they often have huge bboxes
					// spanning the entire map, or really tall lifts, etc.
					if (i == parent->num_leafs && parent->num_leafs < MAX_ENT_LEAFS)
						goto invisible; // not visible
				}
			}
		}

		// okay, we care about this entity.

		if (numents == maxents)
		{
			maxents += 64;
			ents = Mem_Realloc (ents, maxents * sizeof (*ents));
		}

		ents[numents].num = e;
		SV_BuildEntityState (ent, &ents[numents].state);
		if ((unsigned int)ents[numents].state.modelindex >= client->limit_models)
			ents[numents].state.modelindex = 0;
		if (ent == clent) // add velocity, but we only care for the local player (should add prediction for other entities some time too).
		{
			ents[numents].state.pmovetype = 0; // ent->v.movetype;	//fixme: we don't do prediction, so don't tell the client that it can try
			if ((int)ent->v.flags & FL_ONGROUND)
				eflags |= EFLAGS_ONGROUND;
			ents[numents].state.velocity[0] = ent->v.velocity[0] * 8;
			ents[numents].state.velocity[1] = ent->v.velocity[1] * 8;
			ents[numents].state.velocity[2] = ent->v.velocity[2] * 8;
		}
		else if (ents[numents].state.alpha == ENTALPHA_ZERO && !ent->v.effects) // don't send invisible entities unless they have effects
			continue;
		// EFLAGS_VIEWMODEL was handled above
		ents[numents].state.eflags |= eflags;

		numents++;
	}

	snapshot_entstate = ents;
	snapshot_numents = numents;
	snapshot_maxents = maxents;
}

void MSG_WriteStaticOrBaseLine (sizebuf_t *buf, int idx, entity_state_t *state, unsigned int protocol_pext2, unsigned int protocol, unsigned int protocolflags)
{
	int i;
	if (protocol_pext2 & PEXT2_REPLACEMENTDELTAS)
	{
		if (idx >= 0)
		{
			MSG_WriteByte (buf, svcfte_spawnbaseline2);
			MSG_WriteShort (buf, idx);
		}
		else
			MSG_WriteByte (buf, svcfte_spawnstatic2);
		MSGFTE_WriteEntityUpdate (MSGFTE_DeltaCalcBits (&nullentitystate, state), state, buf, protocol_pext2, protocolflags);
	}
	else
	{
		int bits = 0;
		{
			if (protocol == PROTOCOL_FITZQUAKE || protocol == PROTOCOL_RMQ) // still want to send baseline in PROTOCOL_NETQUAKE, so reset these values
			{
				if (state->modelindex & 0xFF00)
					bits |= B_LARGEMODEL;
				if (state->frame & 0xFF00)
					bits |= B_LARGEFRAME;
				if (state->alpha != ENTALPHA_DEFAULT)
					bits |= B_ALPHA;
				if (state->scale != ENTSCALE_DEFAULT && protocol == PROTOCOL_RMQ)
					bits |= B_SCALE;
			}
			if (idx >= 0)
			{
				MSG_WriteByte (buf, bits ? svc_spawnbaseline2 : svc_spawnbaseline);
				MSG_WriteEntity (buf, idx, protocol_pext2);
			}
			else
				MSG_WriteByte (buf, bits ? svc_spawnstatic2 : svc_spawnstatic);

			if (bits)
				MSG_WriteByte (buf, bits);
		}

		if (bits & B_LARGEMODEL)
			MSG_WriteShort (buf, state->modelindex);
		else
			MSG_WriteByte (buf, state->modelindex);

		if (bits & B_LARGEFRAME)
			MSG_WriteShort (buf, state->frame);
		else
			MSG_WriteByte (buf, state->frame);

		MSG_WriteByte (buf, state->colormap);
		MSG_WriteByte (buf, state->skin);
		for (i = 0; i < 3; i++)
		{
			MSG_WriteCoord (buf, state->origin[i], protocolflags);
			MSG_WriteAngle (buf, state->angles[i], protocolflags);
		}
		if (bits & B_ALPHA)
			MSG_WriteByte (buf, state->alpha);
		if (bits & B_SCALE)
			MSG_WriteByte (buf, state->scale);
	}
}
static void SV_Pext_f (void);

/*
===============
SV_Protocol_f
===============
*/
static void SV_Protocol_f (void)
{
	int			i;
	const char *s;
	int			prot, pext1, pext2;

	prot = sv_protocol;
	pext1 = sv_protocol_pext1;
	pext2 = sv_protocol_pext2;

	switch (Cmd_Argc ())
	{
	case 1:
		//"FTE+15" or "15", just to be explicit about it
		Con_Printf ("\"sv_protocol\" is \"%s%i\"\n", sv_protocol_pext2 ? "fte" : "", sv_protocol);
		break;
	case 2:
		s = Cmd_Argv (1);
		if (!q_strncasecmp (s, "FTE", 3))
		{
			s += 3;
			if (*s == '+' || *s == '-')
				s++;
			pext1 = PEXT1_SUPPORTED_SERVER;
			pext2 = PEXT2_SUPPORTED_SERVER;
		}
		else if (!q_strncasecmp (s, "+", 3))
		{
			s += 1;
			pext1 = PEXT1_SUPPORTED_SERVER;
			pext2 = PEXT2_SUPPORTED_SERVER;
		}
		else if (!q_strncasecmp (s, "Base", 4))
		{
			s += 4;
			if (*s == '+' || *s == '-')
				s++;
			pext1 = 0;
			pext2 = 0;
		}
		else if (*s == '-')
		{
			s++;
			pext1 = 0;
			pext2 = 0;
		}

		i = strtol (s, (char **)&s, 0);
		if (*s == '-')
		{
			pext1 = 0;
			pext2 = 0;
		}
		else if (*s == '+')
		{
			pext1 = PEXT1_SUPPORTED_SERVER;
			pext2 = PEXT2_SUPPORTED_SERVER;
		}

		if (i != PROTOCOL_NETQUAKE && i != PROTOCOL_FITZQUAKE && i != PROTOCOL_RMQ)
			Con_Printf (
				"sv_protocol must be %i or %i or %i.\nProtocol may be prefixed with FTE+ or Base- to enable/disable FTE extensions.\n", PROTOCOL_NETQUAKE,
				PROTOCOL_FITZQUAKE, PROTOCOL_RMQ);
		else
		{
			sv_protocol = i;
			sv_protocol_pext1 = pext1;
			sv_protocol_pext2 = pext2;
			if (sv.active)
			{
				if (prot == sv_protocol && pext1 == sv_protocol_pext1 && pext2 == sv_protocol_pext2)
					Con_Printf ("specified protocol already active.\n");
				else
					Con_Printf ("changes will not take effect until the next level load.\n");
			}
		}
		break;
	default:
		Con_SafePrintf ("usage: sv_protocol <protocol>\n");
		break;
	}
}

/*
===============
SV_Init
===============
*/
void SV_Init (void)
{
	int			  i;
	const char	 *p;
	extern cvar_t sv_maxvelocity;
	extern cvar_t sv_gravity;
	extern cvar_t sv_nostep;
	extern cvar_t sv_freezenonclients;
	extern cvar_t sv_friction;
	extern cvar_t sv_edgefriction;
	extern cvar_t sv_stopspeed;
	extern cvar_t sv_maxspeed;
	extern cvar_t sv_accelerate;
	extern cvar_t sv_idealpitchscale;
	extern cvar_t sv_aim;
	extern cvar_t sv_altnoclip; // johnfitz

	Cvar_RegisterVariable (&sv_maxvelocity);
	Cvar_RegisterVariable (&sv_gravity);
	Cvar_RegisterVariable (&sv_friction);
	Cvar_SetCallback (&sv_gravity, Host_Callback_Notify);
	Cvar_SetCallback (&sv_friction, Host_Callback_Notify);
	Cvar_RegisterVariable (&sv_edgefriction);
	Cvar_RegisterVariable (&sv_stopspeed);
	Cvar_RegisterVariable (&sv_maxspeed);
	Cvar_SetCallback (&sv_maxspeed, Host_Callback_Notify);
	Cvar_RegisterVariable (&sv_accelerate);
	Cvar_RegisterVariable (&sv_idealpitchscale);
	Cvar_RegisterVariable (&sv_aim);
	Cvar_RegisterVariable (&sv_nostep);
	Cvar_RegisterVariable (&sv_freezenonclients);
	Cvar_RegisterVariable (&pr_checkextension);
	Cvar_RegisterVariable (&sv_altnoclip); // johnfitz
	Cvar_RegisterVariable (&sv_netsort);
	Cvar_RegisterVariable (&sv_smoothplatformlerps);

	Cmd_AddCommand ("pext", SV_Pext_f);
	Cmd_AddCommand ("sv_protocol", &SV_Protocol_f); // johnfitz

	for (i = 0; i < MAX_MODELS; i++)
		q_snprintf (localmodels[i], 8, "*%i", i);

	i = COM_CheckParm ("-protocol");
	if (i && i < com_argc - 1)
		sv_protocol = atoi (com_argv[i + 1]);
	switch (sv_protocol)
	{
	case PROTOCOL_NETQUAKE:
		p = "NetQuake";
		break;
	case PROTOCOL_FITZQUAKE:
		p = "FitzQuake";
		break;
	case PROTOCOL_RMQ:
		p = "RMQ";
		break;
	default:
		Sys_Error ("Bad protocol version request %i. Accepted values: %i, %i, %i.", sv_protocol, PROTOCOL_NETQUAKE, PROTOCOL_FITZQUAKE, PROTOCOL_RMQ);
		return; /* silence compiler */
	}
	Sys_Printf ("Server using protocol %i%s (%s%s)\n", sv_protocol, sv_protocol_pext2 ? "+" : "", sv_protocol_pext2 ? "FTE-" : "", p);
}

/*
=============================================================================

EVENT MESSAGES

=============================================================================
*/

/*
==================
SV_StartParticle

Make sure the event gets sent to all clients
==================
*/
void SV_StartParticle (vec3_t org, vec3_t dir, int color, int count)
{
	int i, v;

	if (sv.datagram.cursize > sv.datagram.maxsize - 18)
		return;
	MSG_WriteByte (&sv.datagram, svc_particle);
	MSG_WriteCoord (&sv.datagram, org[0], sv.protocolflags);
	MSG_WriteCoord (&sv.datagram, org[1], sv.protocolflags);
	MSG_WriteCoord (&sv.datagram, org[2], sv.protocolflags);
	for (i = 0; i < 3; i++)
	{
		v = dir[i] * 16;
		if (v > 127)
			v = 127;
		else if (v < -128)
			v = -128;
		MSG_WriteChar (&sv.datagram, v);
	}
	MSG_WriteByte (&sv.datagram, count);
	MSG_WriteByte (&sv.datagram, color);
}

/*
==================
SV_StartSound

Each entity can have eight independant sound sources, like voice,
weapon, feet, etc.

Channel 0 is an auto-allocate channel, the others override anything
allready running on that entity/channel pair.

An attenuation of 0 will play full volume everywhere in the level.
Larger attenuations will drop off.  (max 4 attenuation)

==================
*/
void SV_StartSound (edict_t *entity, float *origin, int channel, const char *sample, int volume, float attenuation)
{
	unsigned int sound_num, ent;
	int			 i, field_mask;
	int			 p;
	client_t	*client;

	if (volume < 0)
		Host_Error ("SV_StartSound: volume = %i", volume);
	else if (volume > 255)
	{
		volume = 255;
		Con_Printf ("SV_StartSound: volume = %i\n", volume);
	}

	if (attenuation < 0 || attenuation > 4)
		Host_Error ("SV_StartSound: attenuation = %f", attenuation);

	if (channel < 0 || channel > 255)
		Host_Error ("SV_StartSound: channel = %i", channel);
	else if (channel > 7)
		Con_DPrintf ("SV_StartSound: channel = %i\n", channel);

	// find precache number for sound
	for (sound_num = 1; sound_num < MAX_SOUNDS && sv.sound_precache[sound_num]; sound_num++)
	{
		if (!strcmp (sample, sv.sound_precache[sound_num]))
			break;
	}

	if (sound_num == MAX_SOUNDS || !sv.sound_precache[sound_num])
	{
		Con_Printf ("SV_StartSound: %s not precacheed\n", sample);
		return;
	}

	ent = NUM_FOR_EDICT (entity);

	field_mask = 0;
	if (volume != DEFAULT_SOUND_PACKET_VOLUME)
		field_mask |= SND_VOLUME;
	if (attenuation != DEFAULT_SOUND_PACKET_ATTENUATION)
		field_mask |= SND_ATTENUATION;

	// johnfitz -- PROTOCOL_FITZQUAKE
	if (ent >= 8192 || channel >= 8)
		field_mask |= SND_LARGEENTITY;
	if (sound_num >= 256)
		field_mask |= SND_LARGESOUND;
	// johnfitz

	for (p = 0; p < svs.maxclients; p++)
	{
		client = &svs.clients[p];
		if (!client->active || !client->spawned)
			continue;

		if (ent >= client->limit_entities)
			continue;
		if (sound_num >= client->limit_sounds)
			continue;
		// PROTOCOL_NETQUAKE do not support more than 256 sounds and/or 8192 entities.
		if ((field_mask & (SND_LARGEENTITY | SND_LARGESOUND)) && (sv.protocol == PROTOCOL_NETQUAKE))
			continue;

		if (client->datagram.cursize > client->datagram.maxsize - 22)
			continue;

		// directed messages go only to the entity the are targeted on
		MSG_WriteByte (&client->datagram, svc_sound);
		MSG_WriteByte (&client->datagram, field_mask);
		if (field_mask & SND_VOLUME)
			MSG_WriteByte (&client->datagram, volume);
		if (field_mask & SND_ATTENUATION)
			MSG_WriteByte (&client->datagram, attenuation * 64);

		// johnfitz -- PROTOCOL_FITZQUAKE
		if (field_mask & SND_LARGEENTITY)
		{
			if ((client->protocol_pext2 & PEXT2_REPLACEMENTDELTAS) && ent > 0x7fff)
			{
				MSG_WriteShort (&client->datagram, (ent >> 8) | 0x8000);
				MSG_WriteByte (&client->datagram, ent & 0xff);
			}
			else
				MSG_WriteShort (&client->datagram, ent);
			MSG_WriteByte (&client->datagram, channel);
		}
		else
			MSG_WriteShort (&client->datagram, (ent << 3) | channel);
		if (field_mask & SND_LARGESOUND)
			MSG_WriteShort (&client->datagram, sound_num);
		else
			MSG_WriteByte (&client->datagram, sound_num);
		// johnfitz

		for (i = 0; i < 3; i++)
		{
			if (origin)
				MSG_WriteCoord (&client->datagram, origin[i], sv.protocolflags);
			else
				MSG_WriteCoord (&client->datagram, entity->v.origin[i] + 0.5 * (entity->v.mins[i] + entity->v.maxs[i]), sv.protocolflags);
		}
	}
}

/*
==================
SV_LocalSound - for 2021 rerelease
==================
*/
void SV_LocalSound (client_t *client, const char *sample)
{
	int sound_num, field_mask;

	for (sound_num = 1; sound_num < MAX_SOUNDS && sv.sound_precache[sound_num]; sound_num++)
	{
		if (!strcmp (sample, sv.sound_precache[sound_num]))
			break;
	}
	if (sound_num == MAX_SOUNDS || !sv.sound_precache[sound_num])
	{
		Con_Printf ("SV_LocalSound: %s not precached\n", sample);
		return;
	}

	field_mask = 0;
	if (sound_num >= 256)
	{
		if (sv.protocol == PROTOCOL_NETQUAKE)
			return;
		field_mask = SND_LARGESOUND;
	}

	MSG_WriteByte (&client->message, svc_localsound);
	MSG_WriteByte (&client->message, field_mask);
	if (field_mask & SND_LARGESOUND)
		MSG_WriteShort (&client->message, sound_num);
	else
		MSG_WriteByte (&client->message, sound_num);
}

/*
==============================================================================

CLIENT SPAWNING

==============================================================================
*/

/*
================
SV_SendServerinfo

Sends the first message from the server to a connected client.
This will be sent on the initial connection and upon each server load.
================
*/
void SV_SendServerinfo (client_t *client)
{
	const char **s;
	char		 message[2048];
	unsigned int i; // johnfitz
	qboolean	 cantruncate;
	qboolean	 truncated = false;

	client->spawned = false; // need prespawn, spawn, etc

	// assume some safe defaults if we early out.
	client->limit_unreliable = 1024;
	client->limit_reliable = 8192;
	client->limit_entities = 0;
	client->limit_models = 0;
	client->limit_sounds = 0;

	if (!sv_protocol_pext2)
	{ // server disabled pext completely, don't bother trying.
		// make sure we try reenabling it again on the next map though.
		client->pextknown = false;
	}
	else if (!client->pextknown)
	{
		MSG_WriteByte (&client->message, svc_stufftext);
		MSG_WriteString (&client->message, "cmd pext\n");
		client->sendsignon = PRESPAWN_FLUSH;
		return;
	}
	client->protocol_pext2 &= sv_protocol_pext2;

	if (!(client->protocol_pext2 & PEXT2_REPLACEMENTDELTAS))
		client->protocol_pext2 &= ~PEXT2_PREDINFO; // stats can't be deltaed if there's no deltas, so just pretend its not supported on its own.

	// now we know their protocol, pick some real defaults that match the limits of the engine that most defines that protocol's limits.
	switch (client->protocol_pext2 ? PROTOCOL_FTE_PEXT2 : sv.protocol)
	{
	default: // eep
	case PROTOCOL_NETQUAKE:
		client->limit_unreliable = 1024;
		client->limit_reliable = 8192;
		if (sv_protocol_pext2 && NET_QSocketGetProQuakeAngleHack (client->netconnection))
			client->limit_entities = 2048; // proquake supports more so assume we can use that limit if angles are also available (but only if we're not
										   // being strict about protocols)
		else
			client->limit_entities = 600; // vanilla sucks.
		client->limit_models = 256;		  // single byte
		client->limit_sounds = 256;		  // single byte
		break;
	case PROTOCOL_FITZQUAKE: // fitzquake didn't get abused quite as much as later engines did.
		client->limit_unreliable = 32000;
		client->limit_reliable = 32000;
		client->limit_entities = 32000;
		client->limit_models = 2048;
		client->limit_sounds = 2048;
		break;
	case PROTOCOL_RMQ: // actually QS - a moving target, so use our server's limits.
		client->limit_unreliable = 32000;
		client->limit_reliable = 64000;
		client->limit_entities = 32000;
		client->limit_models = 2048;
		client->limit_sounds = 2048;
		break;
	case PROTOCOL_FTE_PEXT2:					   // not a real protocol in itself, used to indicate QSS's full limits. FTE will match or allow higher.
		client->limit_unreliable = NET_MAXMESSAGE; // some safe ethernet limit. these clients should accept pretty much anything, but any routers will not.
		client->limit_reliable = NET_MAXMESSAGE;   // adhere to fitzquake's limits if we're recording a demoquite large, ip allows 16 bits
		client->limit_entities = MAX_EDICTS;	   // we don't really know, 8k is probably a save guess but could be 32k, 65k, or even more...
		client->limit_models = MAX_MODELS;		   // not really sure, client's problem until >14bits
		client->limit_sounds = MAX_SOUNDS;		   // not really sure, client's problem until >14bits
		break;
	}

	if (!strcmp (NET_QSocketGetTrueAddressString (client->netconnection), "LOCAL"))
	{ // might as well super-size it. demo playback doesn't care. mostly only affects vanilla. we should trigger other warnings if this limit is exceeded so
	  // don't worry about testers.
		client->limit_unreliable = client->limit_reliable;
	}
	else
	{ // remote clients must not exceed ip MTUs.
		if (client->limit_unreliable > DATAGRAM_MTU)
			client->limit_unreliable = DATAGRAM_MTU;
	}
	if (client->limit_entities > 0x8000 && !(client->protocol_pext2 & PEXT2_REPLACEMENTDELTAS))
		client->limit_entities =
			0x8000; // pext2 changes the encoding of entities to support 23 bits instead of dpp7's 15bits or vanilla's 16bits, but our writeentity is lazy.
	if (client->limit_entities > (unsigned int)qcvm->max_edicts)
		client->limit_entities = (unsigned int)qcvm->max_edicts;

	// unfortunately we can't split this up, so if its oversized, we'll just let the client complain instead of always kicking them
	client->message.maxsize = sizeof (client->msgbuf);
	if (client->message.maxsize > (int)client->limit_reliable)
		client->message.maxsize = client->limit_reliable;
	if (client->datagram.maxsize > (int)client->limit_unreliable)
		client->datagram.maxsize = client->limit_unreliable;

	NET_QSocketSetMSS (client->netconnection, client->limit_unreliable);

	if (client->message.cursize)
	{ // try and flush the reliable NOW, in case the qc is evil
		if (NET_CanSendMessage (host_client->netconnection))
		{
			if (NET_SendMessage (host_client->netconnection, &host_client->message) != -1)
			{
				SZ_Clear (&host_client->message);
				host_client->last_message = realtime;
			}
		}
	}

	cantruncate = client->message.cursize == 0;
retry:
	MSG_WriteByte (&client->message, svc_print);
	//	q_snprintf (message, "%c\nFITZQUAKE %1.2f SERVER (%i CRC)\n", 2, FITZQUAKE_VERSION, pr_crc); //johnfitz -- include fitzquake version
	q_snprintf (
		message, sizeof (message), "%c\n" ENGINE_NAME_AND_VER " Server (%i CRC)\n", 2,
		qcvm->progscrc); // spike -- quakespasm has moved on, and has its own server capabilities now. Advertising = good, right?
	MSG_WriteString (&client->message, message);

	MSG_WriteByte (&client->message, svc_serverinfo);
	if (client->protocol_pext2)
	{ // pext stuff takes the form of modifiers to an underlaying protocol
		MSG_WriteLong (&client->message, PROTOCOL_FTE_PEXT2);
		MSG_WriteLong (&client->message, client->protocol_pext2); // active extensions that the client needs to look out for
	}
	MSG_WriteLong (&client->message, sv.protocol); // johnfitz -- sv.protocol instead of PROTOCOL_VERSION

	if (sv.protocol == PROTOCOL_RMQ)
	{
		// mh - now send protocol flags so that the client knows the protocol features to expect
		MSG_WriteLong (&client->message, sv.protocolflags);
	}

	if (client->protocol_pext2 & PEXT2_PREDINFO)
	{
		// if multiple gamedirs were used, we should list all the active ones eg: "id1;hipnotic;rogue;quoth;mod".
		// fixme: engine-specific forced gamedirs like id1/ or qw/ or fte/ are redundant, so don't bother listing them
		// we don't really track that stuff, so I'm just going to report the last one
		MSG_WriteString (&client->message, COM_GetGameNames (false));
	}

	MSG_WriteByte (&client->message, svs.maxclients);

	if (!coop.value && deathmatch.value)
		MSG_WriteByte (&client->message, GAME_DEATHMATCH);
	else
		MSG_WriteByte (&client->message, GAME_COOP);

	MSG_WriteString (&client->message, PR_GetString (qcvm->edicts->v.message));

	// johnfitz -- only send the first 256 model and sound precaches if protocol is 15
	for (i = 1, s = sv.model_precache + 1; *s && i < client->limit_models; s++, i++)
		MSG_WriteString (&client->message, *s);
	MSG_WriteByte (&client->message, 0);
	client->signon_models = i;

	// Spike: if we have svc_precache then use it for sounds. this reduces the stress on the serverinfo message size.
	if (host_client->protocol_pext2 && truncated)
		i = 1; // we tried, it didn't fit.
	else
		for (i = 1, s = sv.sound_precache + 1; *s && i < client->limit_sounds; s++, i++)
			MSG_WriteString (&client->message, *s);
	MSG_WriteByte (&client->message, 0);
	client->signon_sounds = i;
	// johnfitz

	// send music
	MSG_WriteByte (&client->message, svc_cdtrack);
	MSG_WriteByte (&client->message, qcvm->edicts->v.sounds);
	MSG_WriteByte (&client->message, qcvm->edicts->v.sounds);

	// set view
	MSG_WriteByte (&client->message, svc_setview);
	MSG_WriteShort (&client->message, NUM_FOR_EDICT (client->edict));

	MSG_WriteByte (&client->message, svc_signonnum);
	MSG_WriteByte (&client->message, 1);

	client->sendsignon = PRESPAWN_FLUSH;

	SVFTE_SetupFrames (client);

	if (client->message.overflowed && client->limit_models > 64 && cantruncate)
	{
		if (!host_client->protocol_pext2 || truncated)
		{ // first time around we can just drop sounds completely, filling them in later.
			// theoretically we can do the same with models too, but we don't entirely trust clients to handle lightmaps properly when its external bmodels.
			if (client->limit_models > client->limit_sounds || host_client->protocol_pext2)
				client->limit_models /= 2;
			else
				client->limit_sounds /= 2;
		}
		SZ_Clear (&client->message);
		truncated = true;
		goto retry;
	}

	// try and flush the reliable NOW, in case the qc is evil
	if (NET_CanSendMessage (client->netconnection))
	{
		if (NET_SendMessage (client->netconnection, &client->message) != -1)
		{
			SZ_Clear (&client->message);
			client->last_message = realtime;
			client->sendsignon = PRESPAWN_DONE;
		}
	}

	if (truncated)
		Con_Printf ("Protocol limitation (serverinfo) for %s\n", NET_QSocketGetTrueAddressString (client->netconnection));
}

void SV_Pext_f (void)
{
	// this only makes sense on the server. the clientside part only takes the form of 'cmd pext', for compat with clients that don't support this.
	if (cmd_source != src_client)
	{
		if (!cls.state)
		{
			Con_Printf ("Not connected\n");
			return;
		}
		Con_Printf ("Current Protocols:\n");
		if (cl.protocol_pext2 & PEXT2_REPLACEMENTDELTAS)
			Con_Printf ("  Replacement Entity Deltas\n");
		if (cl.protocol_pext2 & PEXT2_PREDINFO)
			Con_Printf ("  Replacement Stats ('predinfo')\n");
		if (cl.protocol == PROTOCOL_NETQUAKE)
			Con_Printf ("  vanilla(15)\n");
		else if (cl.protocol == PROTOCOL_FITZQUAKE)
			Con_Printf ("  fitzquake(666)\n");
		else if (cl.protocol == PROTOCOL_RMQ)
			Con_Printf ("  rmq(999)\n");
		else
			Con_Printf ("  unknown protocol(%i)\n", cl.protocol);
		return;
	}

	if (!host_client->pextknown && !host_client->spawned)
	{
		int i;
		int key;
		int value;
		for (i = 1; i < Cmd_Argc (); i += 2)
		{
			key = strtoul (Cmd_Argv (i), NULL, 0);
			value = strtoul (Cmd_Argv (i + 1), NULL, 0);

			if (key == PROTOCOL_FTE_PEXT2)
				host_client->protocol_pext2 = value & PEXT2_SUPPORTED_SERVER;
			// else some other extension that we don't know
		}

		host_client->pextknown = true;
		SV_SendServerinfo (host_client);
	}
}

/*
================
SV_ConnectClient

Initializes a client_t for a new net connection.  This will only be called
once for a player each game, not once for each level change.
================
*/
void SV_ConnectClient (int clientnum)
{
	edict_t			 *ent;
	client_t		 *client;
	int				  edictnum;
	struct qsocket_s *netconnection;
	int				  i;
	float			  spawn_parms[NUM_TOTAL_SPAWN_PARMS];

	client = svs.clients + clientnum;

	if (client->netconnection)
		Con_DPrintf ("Client %s connected\n", NET_QSocketGetTrueAddressString (client->netconnection));
	else
		Con_DPrintf ("Bot connected\n");

	edictnum = clientnum + 1;

	ent = EDICT_NUM (edictnum);

	// set up the client_t
	netconnection = client->netconnection;
	net_activeconnections++;

	if (sv.loadgame)
		memcpy (spawn_parms, client->spawn_parms, sizeof (spawn_parms));
	memset (client, 0, sizeof (*client));
	client->netconnection = netconnection;

	strcpy (client->name, "unconnected");
	client->active = true;
	client->spawned = false;
	client->edict = ent;
	client->message.data = client->msgbuf;
	client->message.maxsize = sizeof (client->msgbuf);
	client->message.allowoverflow = true; // we can catch it

	client->datagram.data = client->datagram_buf;
	client->datagram.maxsize = sizeof (client->datagram_buf);
	client->datagram.allowoverflow = true; // simply ignored on overflow

	client->pextknown = false;
	client->protocol_pext2 = 0;

	if (sv.loadgame)
		memcpy (client->spawn_parms, spawn_parms, sizeof (spawn_parms));
	else
	{
		// call the progs to get default spawn parms for the new client
		PR_ExecuteProgram (pr_global_struct->SetNewParms);
		for (i = 0; i < NUM_TOTAL_SPAWN_PARMS; i++)
			client->spawn_parms[i] = (&pr_global_struct->parm1)[i];
	}

	SV_SendServerinfo (client);
}

/*
===================
SV_CheckForNewClients

===================
*/
void SV_CheckForNewClients (void)
{
	struct qsocket_s *ret;
	int				  i;

	//
	// check for new connections
	//
	while (1)
	{
		ret = NET_CheckNewConnections ();
		if (!ret)
			break;

		//
		// init a new client structure
		//
		for (i = 0; i < svs.maxclients; i++)
			if (!svs.clients[i].active)
				break;
		if (i == svs.maxclients)
			Sys_Error ("Host_CheckForNewClients: no free clients");

		svs.clients[i].netconnection = ret;
		SV_ConnectClient (i);
	}
}

/*
===============================================================================

FRAME UPDATES

===============================================================================
*/

/*
==================
SV_ClearDatagram

==================
*/
void SV_ClearDatagram (void)
{
	SZ_Clear (&sv.datagram);
}

/*
=============================================================================

The PVS must include a small area around the client to allow head bobbing
or other small motion on the client side.  Otherwise, a bob might cause an
entity that should be visible to not show up, especially when the bob
crosses a waterline.

=============================================================================
*/

static int		fatbytes;
static byte	   *fatpvs;
static int		fatpvs_capacity;
static qboolean fatpvs_any;

void SV_AddToFatPVS (vec3_t org, mnode_t *node, qmodel_t *worldmodel) // johnfitz -- added worldmodel as a parameter
{
	int		  i;
	byte	 *pvs;
	mplane_t *plane;
	float	  d;

	while (1)
	{
		// if this is a leaf, accumulate the pvs bits
		if (node->contents < 0)
		{
			if (node->contents != CONTENTS_SOLID)
			{
				fatpvs_any = true;
				pvs = Mod_LeafPVS ((mleaf_t *)node, worldmodel); // johnfitz -- worldmodel as a parameter
				for (i = 0; i < fatbytes - 3; i += 4)
					*(uint32_t *)&fatpvs[i] |= *(uint32_t *)&pvs[i];
			}
			return;
		}

		plane = node->plane;
		d = DotProduct (org, plane->normal) - plane->dist;
		if (d > 8)
			node = node->children[0];
		else if (d < -8)
			node = node->children[1];
		else
		{														 // go down both
			SV_AddToFatPVS (org, node->children[0], worldmodel); // johnfitz -- worldmodel as a parameter
			node = node->children[1];
		}
	}
}

/*
=============
SV_FatPVS

Calculates a PVS that is the inclusive or of all leafs within 8 pixels of the
given point.
=============
*/
byte *SV_FatPVS (vec3_t org, qmodel_t *worldmodel) // johnfitz -- added worldmodel as a parameter
{
	fatbytes = (worldmodel->numleafs + 31) / 8;
	if (fatpvs == NULL || fatbytes > fatpvs_capacity)
	{
		fatpvs_capacity = fatbytes;
		fatpvs = (byte *)Mem_Realloc (fatpvs, fatpvs_capacity);
		if (!fatpvs)
			Sys_Error ("SV_FatPVS: realloc() failed on %d bytes", fatpvs_capacity);
	}

	memset (fatpvs, 0, fatbytes);
	fatpvs_any = false;
	SV_AddToFatPVS (org, worldmodel->nodes, worldmodel); // johnfitz -- worldmodel as a parameter
	if (fatpvs_any == false)
		memset (fatpvs, 0xff, fatbytes);
	return fatpvs;
}

/*
=============
SV_VisibleToClient -- johnfitz

PVS test encapsulated in a nice function
=============
*/
qboolean SV_VisibleToClient (edict_t *client, edict_t *test, qmodel_t *worldmodel)
{
	byte		*pvs;
	vec3_t		 org;
	unsigned int i;

	VectorAdd (client->v.origin, client->v.view_ofs, org);
	pvs = SV_FatPVS (org, worldmodel);

	for (i = 0; i < test->num_leafs; i++)
		if (pvs[test->leafnums[i] >> 3] & (1 << (test->leafnums[i] & 7)))
			return true;

	return false;
}

//=============================================================================

static uint16_t net_edicts[MAX_EDICTS];
static byte		net_edict_dists[MAX_EDICTS];
static int		net_edict_bins[256];
static uint16_t net_edicts_sorted[MAX_EDICTS];

/*
=============
SV_WriteEntitiesToClient

=============
*/
void SV_WriteEntitiesToClient (client_t *client, sizebuf_t *msg, size_t overflowsize)
{
	edict_t		*clent = client->edict;
	unsigned int e, i, maxedict = qcvm->num_edicts, j, numents;
	int			 bits;
	byte		*pvs;
	vec3_t		 org, forward, right, up;
	float		 miss, dist, size;
	edict_t		*ent;
	eval_t		*val;
	size_t		 rollbacksize, origmaxsize = msg->maxsize;
	qboolean	 sort = sv_netsort.value > 1;
	float		 scale;
	const char	*model;

	// with sv_netsort = 1, sort only if (any client) overflowed in the last 10 seconds
	if (sv_netsort.value == 1 && dev_overflows.packetsize + 10 > realtime)
		sort = true;

	msg->maxsize = overflowsize;

	if (maxedict > client->limit_entities)
		maxedict = client->limit_entities;

	// find the client's PVS
	VectorAdd (clent->v.origin, clent->v.view_ofs, org);
	pvs = SV_FatPVS (org, qcvm->worldmodel);

	// find the client's orientation
	AngleVectors (clent->v.v_angle, forward, right, up);

	// reset sorting bins
	memset (net_edict_bins, 0, sizeof (net_edict_bins));

	// add clent
	if (sort)
	{
		net_edicts[0] = NUM_FOR_EDICT (clent);
		net_edict_dists[0] = 0;
		net_edict_bins[0] = 1;
	}
	else
		net_edicts_sorted[0] = NUM_FOR_EDICT (clent);
	numents = 1;

	// add all other entities that touch the pvs
	ent = NEXT_EDICT (qcvm->edicts);
	for (e = 1; e < maxedict; e++, ent = NEXT_EDICT (ent))
	{
		if (ent != clent) // clent already added before the loop
		{
			// ignore ents without visible models
			if (!ent->v.modelindex || !(model = PR_GetString (ent->v.model))[0])
				continue;

			// johnfitz -- don't send model>255 entities if protocol is 15
			if ((unsigned int)ent->v.modelindex >= client->limit_models)
				continue;

			// ignore if not touching a PV leaf
			for (i = 0; i < ent->num_leafs; i++)
				if (pvs[ent->leafnums[i] >> 3] & (1 << (ent->leafnums[i] & 7)))
					break;

			// ericw -- added ent->num_leafs < MAX_ENT_LEAFS condition.
			//
			// if ent->num_leafs == MAX_ENT_LEAFS, the ent is visible from too many leafs
			// for us to say whether it's in the PVS, so don't try to vis cull it.
			// this commonly happens with rotators, because they often have huge bboxes
			// spanning the entire map, or really tall lifts, etc.
			if (i == ent->num_leafs && ent->num_leafs < MAX_ENT_LEAFS)
				continue; // not visible

			if (sort)
			{
				// compute ent bbox size and distance from org to the closest point in ent's bbox
				dist = size = 0.f;
				for (i = 0; i < 3; i++)
				{
					float delta = CLAMP (ent->v.absmin[i], org[i], ent->v.absmax[i]) - org[i];
					dist += delta * delta;
					delta = ent->v.absmax[i] - ent->v.absmin[i];
					size += delta * delta;
				}
				size = q_max (1.f, size);

				// prioritize point-sized projectiles that do something on impact
				if (size < 50 && ent->v.touch)
				{
					if (ent->v.movetype == MOVETYPE_FLYMISSILE || ent->v.movetype == MOVETYPE_FLY)
					{
						vec3_t to_self;
						VectorSubtract (org, ent->v.origin, to_self);
						float direction = DotProduct (ent->v.velocity, to_self); // if >0, coming toward us; otherwise rockets always get priority
						size = (direction > 0 || strstr (model, "miss") || strstr (model, "rocket")) ? 3072 : 768; // set a 32-/16-sided cube's size
					}
					else if (ent->v.movetype == MOVETYPE_BOUNCE || ent->v.movetype == MOVETYPE_TOSS)
						// for gibs, set size to a 16-sided cube. for grenades / lavaballs, 32-sided cube
						size = (ent->v.nextthink > 0 && !strstr (model, "gib")) ? 3072 : 768;
				}

				// use scaled square root of (distance/size) as sort key
				dist = 8.f * sqrt (sqrt (dist / size));
				net_edict_dists[numents] = (int)q_min (dist, 255.f);
				net_edicts[numents] = e;

				// compute max distance along forward axis
				dist = 0.f;
				for (i = 0; i < 3; i++)
					dist += ((forward[i] < 0.f ? ent->v.absmin[i] : ent->v.absmax[i]) - org[i]) * forward[i];
				if (dist < 0.f)
					net_edict_dists[numents] |= 128; // deprioritize entities behind the client

				net_edict_bins[net_edict_dists[numents]]++;
			}
			else
				net_edicts_sorted[numents] = e;

			++numents;
		}
		else
			continue;
	}

	if (sort)
	{
		// compute bin offsets
		e = 0;
		for (i = 0; i < countof (net_edict_bins); i++)
		{
			int tmp = net_edict_bins[i];
			net_edict_bins[i] = e;
			e += tmp;
		}

		// generate sorted list
		for (e = 0; e < numents; e++)
			net_edicts_sorted[net_edict_bins[net_edict_dists[e]]++] = net_edicts[e];
	}

	// send entities (closest first)
	for (j = 0; j < numents; j++)
	{
		e = net_edicts_sorted[j];
		ent = EDICT_NUM (e);

		rollbacksize = msg->cursize;

		// send an update
		bits = 0;

		vec3_t origin;
		if (SV_UsePredThinkPos (ent))
			VectorCopy (ent->predthinkpos, origin);
		else
			VectorCopy (ent->v.origin, origin);

		for (i = 0; i < 3; i++)
		{
			miss = origin[i] - ent->baseline.origin[i];
			if (miss < -0.1 || miss > 0.1)
				bits |= U_ORIGIN1 << i;
		}

		if (ent->v.angles[0] != ent->baseline.angles[0])
			bits |= U_ANGLE1;

		if (ent->v.angles[1] != ent->baseline.angles[1])
			bits |= U_ANGLE2;

		if (ent->v.angles[2] != ent->baseline.angles[2])
			bits |= U_ANGLE3;

		if (ent->v.movetype == MOVETYPE_STEP)
			bits |= U_STEP; // don't mess up the step animation

		if (ent->baseline.colormap != ent->v.colormap)
			bits |= U_COLORMAP;

		if (ent->baseline.skin != ent->v.skin)
			bits |= U_SKIN;

		if (ent->baseline.frame != ent->v.frame)
			bits |= U_FRAME;

		if ((ent->baseline.effects ^ (int)ent->v.effects) & sv.effectsmask)
			bits |= U_EFFECTS;

		if (ent->baseline.modelindex != ent->v.modelindex)
			bits |= U_MODEL;

		// johnfitz -- alpha
		//  TODO: find a cleaner place to put this code
		val = GetEdictFieldValue (ent, qcvm->extfields.alpha);
		if (val)
			ent->alpha = ENTALPHA_ENCODE (val->_float);

		// don't send invisible entities unless they have effects
		if (ent->alpha == ENTALPHA_ZERO && !((int)ent->v.effects & sv.effectsmask))
			continue;
		// johnfitz

		val = GetEdictFieldValue (ent, qcvm->extfields.scale);
		scale = val ? ENTSCALE_ENCODE (val->_float) : ENTSCALE_DEFAULT;

		// johnfitz -- PROTOCOL_FITZQUAKE
		if (sv.protocol != PROTOCOL_NETQUAKE)
		{
			if (ent->baseline.alpha != ent->alpha)
				bits |= U_ALPHA;
			if (sv.protocol == PROTOCOL_RMQ)
			{
				if (ent->baseline.scale != scale)
					bits |= U_SCALE;
			}
			else if (ENTSCALE_DEFAULT != scale) // for 666, we didn't send the scale in the baseline!
				bits |= U_SCALE;
			if (bits & U_FRAME && (int)ent->v.frame & 0xFF00)
				bits |= U_FRAME2;
			if (bits & U_MODEL && (int)ent->v.modelindex & 0xFF00)
				bits |= U_MODEL2;
			if (ent->sendinterval)
				bits |= U_LERPFINISH;
			if (bits >= 65536)
				bits |= U_EXTEND1;
			if (bits >= 16777216)
				bits |= U_EXTEND2;
		}
		// johnfitz

		if (e >= 256)
			bits |= U_LONGENTITY;

		if (bits >= 256)
			bits |= U_MOREBITS;

		//
		// write the message
		//
		MSG_WriteByte (msg, bits | U_SIGNAL);

		if (bits & U_MOREBITS)
			MSG_WriteByte (msg, bits >> 8);

		// johnfitz -- PROTOCOL_FITZQUAKE
		if (bits & U_EXTEND1)
			MSG_WriteByte (msg, bits >> 16);
		if (bits & U_EXTEND2)
			MSG_WriteByte (msg, bits >> 24);
		// johnfitz

		if (bits & U_LONGENTITY)
			MSG_WriteShort (msg, e);
		else
			MSG_WriteByte (msg, e);

		if (bits & U_MODEL)
			MSG_WriteByte (msg, ent->v.modelindex);
		if (bits & U_FRAME)
			MSG_WriteByte (msg, ent->v.frame);
		if (bits & U_COLORMAP)
			MSG_WriteByte (msg, ent->v.colormap);
		if (bits & U_SKIN)
			MSG_WriteByte (msg, ent->v.skin);
		if (bits & U_EFFECTS)
			MSG_WriteByte (msg, (int)ent->v.effects & sv.effectsmask);
		if (bits & U_ORIGIN1)
			MSG_WriteCoord (msg, origin[0], sv.protocolflags);
		if (bits & U_ANGLE1)
			MSG_WriteAngle (msg, ent->v.angles[0], sv.protocolflags);
		if (bits & U_ORIGIN2)
			MSG_WriteCoord (msg, origin[1], sv.protocolflags);
		if (bits & U_ANGLE2)
			MSG_WriteAngle (msg, ent->v.angles[1], sv.protocolflags);
		if (bits & U_ORIGIN3)
			MSG_WriteCoord (msg, origin[2], sv.protocolflags);
		if (bits & U_ANGLE3)
			MSG_WriteAngle (msg, ent->v.angles[2], sv.protocolflags);

		// johnfitz -- PROTOCOL_FITZQUAKE
		if (bits & U_ALPHA)
			MSG_WriteByte (msg, ent->alpha);
		if (bits & U_SCALE)
			MSG_WriteByte (msg, scale);
		if (bits & U_FRAME2)
			MSG_WriteByte (msg, (int)ent->v.frame >> 8);
		if (bits & U_MODEL2)
			MSG_WriteByte (msg, (int)ent->v.modelindex >> 8);
		if (bits & U_LERPFINISH)
			MSG_WriteByte (msg, (byte)(Q_rint ((ent->v.nextthink - qcvm->time) * 255)));
		// johnfitz

		if ((size_t)msg->cursize > origmaxsize)
		{
			msg->cursize = rollbacksize; // roll back
			// johnfitz -- less spammy overflow message
			if (!dev_overflows.packetsize || dev_overflows.packetsize + CONSOLE_RESPAM_TIME < realtime)
			{
				Con_Printf ("Packet overflow!\n");
				dev_overflows.packetsize = realtime;
			}
			break; // we could keep searching for something else that fits, but ehh
		}
	}

	msg->maxsize = origmaxsize;

	// johnfitz -- devstats
	if (msg->cursize > 1024 && dev_peakstats.packetsize <= 1024)
		Con_DWarning ("%i byte packet exceeds standard limit of 1024 (max = %d).\n", msg->cursize, msg->maxsize);
	dev_stats.packetsize = msg->cursize;
	dev_peakstats.packetsize = q_max (msg->cursize, dev_peakstats.packetsize);
	// johnfitz
}

/*
=============
SV_CleanupEnts

=============
*/
void SV_CleanupEnts (void)
{
	int		 e;
	edict_t *ent;

	ent = NEXT_EDICT (qcvm->edicts);

	for (e = 1; e < qcvm->num_edicts; e++, ent = NEXT_EDICT (ent))
	{
		ent->v.effects = (int)ent->v.effects & ~EF_MUZZLEFLASH;
	}
}

/*
==================
SV_WriteDamageToMessage

==================
*/
void SV_WriteDamageToMessage (edict_t *ent, sizebuf_t *msg)
{
	edict_t *other;
	int		 i;

	//
	// send a damage message
	//
	if (ent->v.dmg_take || ent->v.dmg_save)
	{
		other = PROG_TO_EDICT (ent->v.dmg_inflictor);
		MSG_WriteByte (msg, svc_damage);
		MSG_WriteByte (msg, ent->v.dmg_save);
		MSG_WriteByte (msg, ent->v.dmg_take);
		for (i = 0; i < 3; i++)
			MSG_WriteCoord (msg, other->v.origin[i] + 0.5 * (other->v.mins[i] + other->v.maxs[i]), sv.protocolflags);

		ent->v.dmg_take = 0;
		ent->v.dmg_save = 0;
	}

	//
	// send the current viewpos offset from the view entity
	//
	SV_SetIdealPitch (); // how much to look up / down ideally

	// a fixangle might get lost in a dropped packet.  Oh well.
	if (ent->v.fixangle)
	{
		MSG_WriteByte (msg, svc_setangle);
		for (i = 0; i < 3; i++)
			MSG_WriteAngle (msg, ent->v.angles[i], sv.protocolflags);
		ent->v.fixangle = 0;
	}
}

/*
==================
SV_WriteClientdataToMessage

==================
*/
void SV_WriteClientdataToMessage (client_t *client, sizebuf_t *msg)
{
	edict_t		*ent = client->edict;
	int			 bits;
	int			 i;
	int			 items;
	eval_t		*val;
	unsigned int weaponmodelindex = SV_ModelIndex (PR_GetString (ent->v.weaponmodel));

	if (weaponmodelindex >= client->limit_models)
		weaponmodelindex = 0;

	bits = 0;

	if (ent->v.view_ofs[2] != DEFAULT_VIEWHEIGHT)
		bits |= SU_VIEWHEIGHT;

	if (ent->v.idealpitch)
		bits |= SU_IDEALPITCH;

	// stuff the sigil bits into the high bits of items for sbar, or else
	// mix in items2
	val = GetEdictFieldValue (ent, ED_FindFieldOffset ("items2"));

	if (val)
		items = (int)ent->v.items | ((int)val->_float << 23);
	else
		items = (int)ent->v.items | ((int)pr_global_struct->serverflags << 28);

	bits |= SU_ITEMS;

	if ((int)ent->v.flags & FL_ONGROUND)
		bits |= SU_ONGROUND;

	if (ent->v.waterlevel >= 2)
		bits |= SU_INWATER;

	for (i = 0; i < 3; i++)
	{
		if (ent->v.punchangle[i])
			bits |= (SU_PUNCH1 << i);
		if (ent->v.velocity[i])
			bits |= (SU_VELOCITY1 << i);
	}

	if (ent->v.weaponframe)
		bits |= SU_WEAPONFRAME;

	if (ent->v.armorvalue)
		bits |= SU_ARMOR;

	//	if (ent->v.weapon)
	bits |= SU_WEAPON;

	// johnfitz -- PROTOCOL_FITZQUAKE
	if (sv.protocol != PROTOCOL_NETQUAKE)
	{
		if (bits & SU_WEAPON && weaponmodelindex & 0xFF00)
			bits |= SU_WEAPON2;
		if ((int)ent->v.armorvalue & 0xFF00)
			bits |= SU_ARMOR2;
		if ((int)ent->v.currentammo & 0xFF00)
			bits |= SU_AMMO2;
		if ((int)ent->v.ammo_shells & 0xFF00)
			bits |= SU_SHELLS2;
		if ((int)ent->v.ammo_nails & 0xFF00)
			bits |= SU_NAILS2;
		if ((int)ent->v.ammo_rockets & 0xFF00)
			bits |= SU_ROCKETS2;
		if ((int)ent->v.ammo_cells & 0xFF00)
			bits |= SU_CELLS2;
		if (bits & SU_WEAPONFRAME && (int)ent->v.weaponframe & 0xFF00)
			bits |= SU_WEAPONFRAME2;
		if (bits & SU_WEAPON && ent->alpha != ENTALPHA_DEFAULT)
			bits |= SU_WEAPONALPHA; // for now, weaponalpha = client entity alpha
		if (bits >= 65536)
			bits |= SU_EXTEND1;
		if (bits >= 16777216)
			bits |= SU_EXTEND2;
	}
	// johnfitz

	// send the data

	MSG_WriteByte (msg, svc_clientdata);
	MSG_WriteShort (msg, bits);

	// johnfitz -- PROTOCOL_FITZQUAKE
	if (bits & SU_EXTEND1)
		MSG_WriteByte (msg, bits >> 16);
	if (bits & SU_EXTEND2)
		MSG_WriteByte (msg, bits >> 24);
	// johnfitz

	if (bits & SU_VIEWHEIGHT)
		MSG_WriteChar (msg, ent->v.view_ofs[2]);

	if (bits & SU_IDEALPITCH)
		MSG_WriteChar (msg, ent->v.idealpitch);

	for (i = 0; i < 3; i++)
	{
		if (bits & (SU_PUNCH1 << i))
			MSG_WriteChar (msg, ent->v.punchangle[i]);
		if (bits & (SU_VELOCITY1 << i))
			MSG_WriteChar (msg, ent->v.velocity[i] / 16);
	}

	// [always sent]	if (bits & SU_ITEMS)
	MSG_WriteLong (msg, items);

	if (bits & SU_WEAPONFRAME)
		MSG_WriteByte (msg, ent->v.weaponframe);
	if (bits & SU_ARMOR)
		MSG_WriteByte (msg, ent->v.armorvalue);
	if (bits & SU_WEAPON)
		MSG_WriteByte (msg, weaponmodelindex);

	MSG_WriteShort (msg, ent->v.health);
	MSG_WriteByte (msg, ent->v.currentammo);
	MSG_WriteByte (msg, ent->v.ammo_shells);
	MSG_WriteByte (msg, ent->v.ammo_nails);
	MSG_WriteByte (msg, ent->v.ammo_rockets);
	MSG_WriteByte (msg, ent->v.ammo_cells);

	if (standard_quake)
	{
		MSG_WriteByte (msg, ent->v.weapon);
	}
	else
	{
		for (i = 0; i < 32; i++)
		{
			if (((int)ent->v.weapon) & (1 << i))
			{
				MSG_WriteByte (msg, i);
				break;
			}
		}
	}

	// johnfitz -- PROTOCOL_FITZQUAKE
	if (bits & SU_WEAPON2)
		MSG_WriteByte (msg, weaponmodelindex >> 8);
	if (bits & SU_ARMOR2)
		MSG_WriteByte (msg, (int)ent->v.armorvalue >> 8);
	if (bits & SU_AMMO2)
		MSG_WriteByte (msg, (int)ent->v.currentammo >> 8);
	if (bits & SU_SHELLS2)
		MSG_WriteByte (msg, (int)ent->v.ammo_shells >> 8);
	if (bits & SU_NAILS2)
		MSG_WriteByte (msg, (int)ent->v.ammo_nails >> 8);
	if (bits & SU_ROCKETS2)
		MSG_WriteByte (msg, (int)ent->v.ammo_rockets >> 8);
	if (bits & SU_CELLS2)
		MSG_WriteByte (msg, (int)ent->v.ammo_cells >> 8);
	if (bits & SU_WEAPONFRAME2)
		MSG_WriteByte (msg, (int)ent->v.weaponframe >> 8);
	if (bits & SU_WEAPONALPHA)
		MSG_WriteByte (msg, ent->alpha); // for now, weaponalpha = client entity alpha
										 // johnfitz
}

void SV_PresendClientDatagram (client_t *client)
{
	if (!client->netconnection)
		return; // botclient
	if (!client->spawned)
		return; // not ready yet.
	if (!(client->protocol_pext2 & PEXT2_REPLACEMENTDELTAS))
		return; // brute force networking.
	SVFTE_BuildSnapshotForClient (client);
	SVFTE_CalcEntityDeltas (client);
	client->snapshotresume = 0;
}

/*
=======================
SV_ParticleSize

If the start of buf contains a svc_particle, returns its size. Otherwise returns 0.
=======================
*/
static int SV_ParticleSize (byte *buf)
{
	if (buf[0] == svc_particle)
	{
		int coord_size = 2;
		if (sv.protocolflags & PRFL_24BITCOORD)
			coord_size = 3;
		else if (sv.protocolflags & (PRFL_FLOATCOORD | PRFL_INT32COORD))
			coord_size = 4;
		return 6 + 3 * coord_size;
	}
	else
		return 0;
}

/*
=======================
SV_SendClientDatagram
=======================
*/
qboolean SV_SendClientDatagram (client_t *client)
{
	byte	  buf[MAX_DATAGRAM + 1000];
	sizebuf_t msg;

	if (!client->netconnection)
	{
		// botclient, shouldn't be sent anything.
		SZ_Clear (&client->datagram);
		return true;
	}

	msg.allowoverflow = false;
	msg.data = buf;
	msg.maxsize = q_min (MAX_DATAGRAM, client->limit_unreliable);
	msg.cursize = 0;

	host_client = client;
	if (client->spawned)
	{
		sv_player = client->edict;

		if (client->protocol_pext2 & PEXT2_REPLACEMENTDELTAS)
		{
			SV_WriteDamageToMessage (client->edict, &msg);
			if (!(client->protocol_pext2 & PEXT2_PREDINFO))
				SV_WriteClientdataToMessage (client, &msg);
			else
				SVFTE_WriteStats (client, &msg);
			SVFTE_WriteEntitiesToClient (client, &msg, sizeof (buf)); // must always write some data, or the stats will break

			// this delta protocol doesn't wipe old state just because there's a new packet.
			// the server isn't required to sync with the client frames either
			// so we can just spam multiple packets to keep our udp data under the MTU
			while (client->snapshotresume < client->numpendingentities)
			{
				NET_SendUnreliableMessage (client->netconnection, &msg);
				SZ_Clear (&msg);
				SVFTE_WriteEntitiesToClient (client, &msg, sizeof (buf));
			}
		}
		else
		{
			MSG_WriteByte (&msg, svc_time);
			MSG_WriteFloat (&msg, qcvm->time);
			if (client->protocol_pext2 & PEXT2_PREDINFO)
				MSG_WriteShort (&msg, (client->lastmovemessage & 0xffff));

			SV_WriteEntitiesToClient (client, &msg, sizeof (buf));
		}

		// copy the private datagram if there is space
		if (client->datagram.cursize && !client->datagram.overflowed)
		{
			if (msg.cursize + client->datagram.cursize < msg.maxsize)
				SZ_Write (&msg, client->datagram.data, client->datagram.cursize);
			else if (client->datagram.cursize < msg.maxsize)
			{
				// send private datagram in another packet
				NET_SendUnreliableMessage (client->netconnection, &msg);
				SZ_Clear (&msg);
				SZ_Write (&msg, client->datagram.data, client->datagram.cursize);
			}
		}
		SZ_Clear (&client->datagram);

		// copy the server datagram if there is space
		if (msg.cursize + sv.datagram.cursize < msg.maxsize)
			SZ_Write (&msg, sv.datagram.data, sv.datagram.cursize);
		else if (sv.datagram.cursize)
		{
			// if the server datagram starts with particles, split them across multiple packets
			int position = 0;
			int size;
			while (sv.datagram.cursize > position && (size = SV_ParticleSize (&sv.datagram.data[position])))
			{
				if (msg.cursize + size < msg.maxsize)
				{
					SZ_Write (&msg, &sv.datagram.data[position], size);
					position += size;
				}
				else
				{
					NET_SendUnreliableMessage (client->netconnection, &msg);
					SZ_Clear (&msg);
				}
			}
			int remaining = sv.datagram.cursize - position;
			if (msg.cursize + remaining < msg.maxsize)
				SZ_Write (&msg, &sv.datagram.data[position], remaining);
			else if (remaining < msg.maxsize)
			{
				NET_SendUnreliableMessage (client->netconnection, &msg);
				SZ_Clear (&msg);
				SZ_Write (&msg, &sv.datagram.data[position], remaining);
			}
		}

		if (!(client->protocol_pext2 & PEXT2_REPLACEMENTDELTAS))
		{
			// add the client specific data to the datagram last to play nice with clients which reset onground on every packet
			// (and to leave a few more bytes for entity updates)
			// cannibalize client->datagram (cleared above) to get an exact size
			SV_WriteDamageToMessage (client->edict, &client->datagram);
			SV_WriteClientdataToMessage (client, &client->datagram);
			if (msg.cursize + client->datagram.cursize > msg.maxsize)
			{
				NET_SendUnreliableMessage (client->netconnection, &msg);
				SZ_Clear (&msg);
			}
			SZ_Write (&msg, client->datagram.data, client->datagram.cursize);
			SZ_Clear (&client->datagram);
		}
	}

	// send the datagram
	if (msg.cursize && NET_SendUnreliableMessage (client->netconnection, &msg) == -1)
	{
		SV_DropClient (false); // if the message couldn't send, kick off
		return false;
	}

	return true;
}

/*
=======================
SV_UpdateToReliableMessages
=======================
*/
void SV_UpdateToReliableMessages (void)
{
	int		  i, j;
	client_t *client;

	// check for changes to be sent over the reliable streams
	for (i = 0, host_client = svs.clients; i < svs.maxclients; i++, host_client++)
	{
		if (host_client->old_frags != host_client->edict->v.frags)
		{
			for (j = 0, client = svs.clients; j < svs.maxclients; j++, client++)
			{
				if (!client->knowntoqc)
					continue;
				MSG_WriteByte (&client->message, svc_updatefrags);
				MSG_WriteByte (&client->message, i);
				MSG_WriteShort (&client->message, host_client->edict->v.frags);
			}

			host_client->old_frags = host_client->edict->v.frags;
		}
	}

	for (j = 0, client = svs.clients; j < svs.maxclients; j++, client++)
	{
		if (!client->active)
			continue;
		SZ_Write (&client->message, sv.reliable_datagram.data, sv.reliable_datagram.cursize);
	}

	SZ_Clear (&sv.reliable_datagram);
}

/*
=======================
SV_SendNop

Send a nop message without trashing or sending the accumulated client
message buffer
=======================
*/
void SV_SendNop (client_t *client)
{
	sizebuf_t msg;
	byte	  buf[4];

	msg.data = buf;
	msg.maxsize = sizeof (buf);
	msg.cursize = 0;

	MSG_WriteChar (&msg, svc_nop);

	if (NET_SendUnreliableMessage (client->netconnection, &msg) == -1)
		SV_DropClient (false); // if the message couldn't send, kick off
	client->last_message = realtime;
}

qboolean SV_SendPrespawnModelPrecaches (void)
{
	return false;
}
qboolean SV_SendPrespawnSoundPrecaches (void)
{
	unsigned int idx = host_client->signon_sounds;
	size_t		 maxsize = host_client->message.maxsize; // we can go quite large
	if (!host_client->protocol_pext2)
		return false; // unsupported by this client...
	for (; idx < host_client->limit_sounds; idx++)
	{
		if (!sv.sound_precache[idx])
			continue;
		if ((size_t)host_client->message.cursize + 4 + strlen (sv.sound_precache[idx]) > maxsize)
			break;
		MSG_WriteByte (&host_client->message, svcdp_precache);
		MSG_WriteShort (&host_client->message, 0x8000 | idx);
		MSG_WriteString (&host_client->message, sv.sound_precache[idx]);
	}
	host_client->signon_sounds = idx;
	return idx < host_client->limit_sounds;
}
int SV_SendPrespawnParticlePrecaches (int idx)
{
	size_t maxsize = host_client->message.maxsize; // we can go quite large
	if (!host_client->protocol_pext2)
		return -1; // unsupported by this client.
	for (;; idx++)
	{
		if (idx == MAX_PARTICLETYPES)
			return -1;
		if (!sv.particle_precache[idx])
			continue;
		if (host_client->message.cursize + 4 + strlen (sv.particle_precache[idx]) > maxsize)
			break;
		MSG_WriteByte (&host_client->message, svcdp_precache);
		MSG_WriteShort (&host_client->message, 0x4000 | idx);
		MSG_WriteString (&host_client->message, sv.particle_precache[idx]);
	}
	return idx;
}
int SV_SendPrespawnStatics (int idx)
{
	entity_state_t *svent;
	int				maxsize = host_client->message.maxsize - 128; // we can go quite large

	while (1)
	{
		if (idx >= sv.num_statics)
			return -1;
		svent = &sv.static_entities[idx];

		if (host_client->message.cursize > maxsize)
			break;
		idx++;

		if (svent->modelindex >= host_client->limit_models)
			continue;
		if (memcmp (&nullentitystate, svent, sizeof (nullentitystate)))
			MSG_WriteStaticOrBaseLine (&host_client->message, -1, svent, host_client->protocol_pext2, sv.protocol, sv.protocolflags);
	}
	return idx;
}
int SV_SendAmbientSounds (int idx)
{
	struct ambientsound_s *snd;
	int					   maxsize = host_client->message.maxsize - 128; // we can go quite large
	qboolean			   large;
	size_t				   i;

	while (1)
	{
		if (idx >= sv.num_ambients)
			return -1;
		snd = &sv.ambientsounds[idx];

		if (host_client->message.cursize > maxsize)
			break;
		idx++;

		if (snd->soundindex >= host_client->limit_sounds)
			continue;

		large = (snd->soundindex > 255);
		if (large)
			MSG_WriteByte (&host_client->message, svc_spawnstaticsound2); // johnfitz -- PROTOCOL_FITZQUAKE
		else
			MSG_WriteByte (&host_client->message, svc_spawnstaticsound);
		for (i = 0; i < 3; i++)
			MSG_WriteCoord (&host_client->message, snd->origin[i], sv.protocolflags);
		if (large)
			MSG_WriteShort (&host_client->message, snd->soundindex);
		else
			MSG_WriteByte (&host_client->message, snd->soundindex);
		MSG_WriteByte (&host_client->message, snd->volume * 255);
		MSG_WriteByte (&host_client->message, snd->attenuation * 64);
	}
	return idx;
}
int SV_SendPrespawnBaselines (int idx)
{
	edict_t *svent;
	int		 maxsize = host_client->message.maxsize - 128; // we can go quite large

	while (1)
	{
		if (idx >= qcvm->num_edicts)
			return -1;
		svent = EDICT_NUM (idx);

		if (host_client->message.cursize > maxsize)
			break;

		if (memcmp (&nullentitystate, &svent->baseline, sizeof (nullentitystate)))
			MSG_WriteStaticOrBaseLine (&host_client->message, idx, &svent->baseline, host_client->protocol_pext2, sv.protocol, sv.protocolflags);

		idx++;
	}
	return idx;
}

/*
=======================
SV_SendClientMessages
=======================
*/
void SV_SendClientMessages (void)
{
	int i;

	// update frags, names, etc
	SV_UpdateToReliableMessages ();

	for (i = 0, host_client = svs.clients; i < svs.maxclients; i++, host_client++)
	{
		if (!host_client->active)
			continue;

		SV_PresendClientDatagram (host_client); // generates client snapshots (and updates csqc pending flags)
	}

	// build individual updates
	for (i = 0, host_client = svs.clients; i < svs.maxclients; i++, host_client++)
	{
		if (!host_client->active)
			continue;

		if (!SV_SendClientDatagram (host_client))
			continue;
		if (!host_client->spawned)
		{
			// the player isn't totally in the game yet
			// send small keepalive messages if too much time has passed
			// send a full message when the next signon stage has been requested
			// some other message data (name changes, etc) may accumulate
			// between signon stages
			if (!host_client->sendsignon)
			{
				if (realtime - host_client->last_message > 5)
					SV_SendNop (host_client);
				continue; // don't send out non-signon messages
			}
			if (host_client->sendsignon == PRESPAWN_MODELS)
			{
				if (!SV_SendPrespawnModelPrecaches ())
				{
					host_client->signonidx = 0;
					host_client->sendsignon++;
				}
			}
			if (host_client->sendsignon == PRESPAWN_SOUNDS)
			{
				if (!SV_SendPrespawnSoundPrecaches ())
				{
					host_client->signonidx = 0;
					host_client->sendsignon++;
				}
			}
			if (host_client->sendsignon == PRESPAWN_PARTICLES)
			{
				host_client->signonidx = SV_SendPrespawnParticlePrecaches (host_client->signonidx);
				if (host_client->signonidx < 0)
				{
					host_client->signonidx = 0;
					host_client->sendsignon++;
				}
			}
			if (host_client->sendsignon == PRESPAWN_BASELINES)
			{
				host_client->signonidx = SV_SendPrespawnBaselines (host_client->signonidx);
				if (host_client->signonidx < 0)
				{
					host_client->signonidx = 0;
					host_client->sendsignon++;
				}
			}
			if (host_client->sendsignon == PRESPAWN_STATICS)
			{
				host_client->signonidx = SV_SendPrespawnStatics (host_client->signonidx);
				if (host_client->signonidx < 0)
				{
					host_client->signonidx = 0;
					host_client->sendsignon++;
				}
			}
			if (host_client->sendsignon == PRESPAWN_AMBIENTS)
			{
				host_client->signonidx = SV_SendAmbientSounds (host_client->signonidx);
				if (host_client->signonidx < 0)
				{
					host_client->signonidx = 0;
					host_client->sendsignon++;
				}
			}
			if (host_client->sendsignon == PRESPAWN_SIGNONMSG)
			{
				if (host_client->message.cursize + sv.signon.cursize + 2 < host_client->message.maxsize)
				{
					SZ_Write (&host_client->message, sv.signon.data, sv.signon.cursize);
					MSG_WriteByte (&host_client->message, svc_signonnum);
					MSG_WriteByte (&host_client->message, 2);
					host_client->sendsignon = PRESPAWN_FLUSH;
				}
			}
		}

		// check for an overflowed message.  Should only happen
		// on a very fucked up connection that backs up a lot, then
		// changes level
		if (host_client->message.overflowed)
		{
			SZ_Clear (&host_client->message);
			SV_DropClient (false);
			continue;
		}

		if (host_client->message.cursize || host_client->dropasap)
		{
			if (!NET_CanSendMessage (host_client->netconnection))
			{
				//				I_Printf ("can't write\n");
				continue;
			}

			if (host_client->dropasap)
				SV_DropClient (false); // went to another level
			else
			{
				if (NET_SendMessage (host_client->netconnection, &host_client->message) == -1)
					SV_DropClient (false); // if the message couldn't send, kick off
				SZ_Clear (&host_client->message);
				host_client->last_message = realtime;
				if (host_client->sendsignon == PRESPAWN_FLUSH)
					host_client->sendsignon = PRESPAWN_DONE;
			}
		}
	}

	// clear muzzle flashes
	SV_CleanupEnts ();
}

/*
==============================================================================

SERVER SPAWNING

==============================================================================
*/

/*
================
SV_ModelIndex

================
*/
int SV_ModelIndex (const char *name)
{
	int i;

	if (!name || !name[0])
		return 0;

	for (i = 0; i < MAX_MODELS && sv.model_precache[i]; i++)
		if (!strcmp (sv.model_precache[i], name))
			return i;
	if (i == MAX_MODELS || !sv.model_precache[i])
		Sys_Error ("SV_ModelIndex: model %s not precached", name);
	return i;
}

/*
================
SV_CreateBaseline
================
*/
void SV_CreateBaseline (void)
{
	edict_t *svent;
	int		 entnum;
	eval_t	*val;

	for (entnum = 0; entnum < qcvm->num_edicts; entnum++)
	{
		// get the current server version
		svent = EDICT_NUM (entnum);
		if (svent->free)
			continue;
		if (entnum > svs.maxclients && !svent->v.modelindex)
			continue;

		//
		// create entity baseline
		//
		svent->baseline = nullentitystate;
		VectorCopy (svent->v.origin, svent->baseline.origin);
		VectorCopy (svent->v.angles, svent->baseline.angles);
		svent->baseline.frame = svent->v.frame;
		svent->baseline.skin = svent->v.skin;
		if (entnum > 0 && entnum <= svs.maxclients)
		{
			svent->baseline.colormap = entnum;
			svent->baseline.modelindex = SV_ModelIndex ("progs/player.mdl");
		}
		else
		{
			svent->baseline.colormap = 0;
			svent->baseline.modelindex = SV_ModelIndex (PR_GetString (svent->v.model));
			val = GetEdictFieldValue (svent, qcvm->extfields.alpha);
			if (val)
				svent->baseline.alpha = ENTALPHA_ENCODE (val->_float);
			else
				svent->baseline.alpha = svent->alpha; // johnfitz -- alpha support
			if ((val = GetEdictFieldValue (svent, qcvm->extfields.scale)))
				svent->baseline.scale = ENTSCALE_ENCODE (val->_float);
		}

		// Spike -- baselines are now transmitted on a per-client basis.
		// FIXME: should merge the above with other edict->entity_state copies (updates, baselines, spawnstatics)
		// 1) this allows per-client extensions.
		// 2) this avoids pre-generating a single signon buffer, splitting it over multiple packets.
		//    thereby allowing more than 3k or so entities
	}
}

/*
================
SV_SendReconnect

Tell all the clients that the server is changing levels
================
*/
void SV_SendReconnect (void)
{
	byte	  data[128];
	sizebuf_t msg;

	msg.data = data;
	msg.cursize = 0;
	msg.maxsize = sizeof (data);

	MSG_WriteChar (&msg, svc_stufftext);
	MSG_WriteString (&msg, "reconnect\n");
	NET_SendToAll (&msg, 5.0);

	if (!isDedicated)
		Cmd_ExecuteString ("reconnect\n", src_command);
}

/*
================
SV_SaveSpawnparms

Grabs the current state of each client for saving across the
transition to another level
================
*/
void SV_SaveSpawnparms (void)
{
	int i, j;

	svs.serverflags = pr_global_struct->serverflags;

	for (i = 0, host_client = svs.clients; i < svs.maxclients; i++, host_client++)
	{
		if (!host_client->active)
			continue;

		// call the progs to get default spawn parms for the new client
		pr_global_struct->self = EDICT_TO_PROG (host_client->edict);
		PR_ExecuteProgram (pr_global_struct->SetChangeParms);
		for (j = 0; j < NUM_BASIC_SPAWN_PARMS; j++)
			host_client->spawn_parms[j] = (&pr_global_struct->parm1)[j];
		for (; j < NUM_TOTAL_SPAWN_PARMS; j++)
		{
			ddef_t *g = ED_FindGlobal (va ("parm%i", j + 1));
			host_client->spawn_parms[j] = g ? qcvm->globals[g->ofs] : 0;
		}
	}
}

// used for sv.qcvm.GetModel (so ssqc+csqc can share builtins)
qmodel_t *SV_ModelForIndex (int index)
{
	if (index < 0 || index >= MAX_MODELS)
		return NULL;
	return sv.models[index];
}

/*
================
SV_SpawnServer

This is called at the start of each level
================
*/
void SV_SpawnServer (const char *server)
{
	static char dummy[8] = {0, 0, 0, 0, 0, 0, 0, 0};
	edict_t	   *ent;
	int			i;
	qcvm_t	   *vm = qcvm;

	// let's not have any servers with no name
	if (hostname.string[0] == 0)
		Cvar_Set ("hostname", "UNNAMED");
	SCR_CenterPrintClear ();

	Con_DPrintf ("SpawnServer: %s\n", server);
	svs.changelevel_issued = false; // now safe to issue another

	PR_SwitchQCVM (NULL);

	//
	// tell all connected clients that we are going to a new level
	//
	if (sv.active)
		SV_SendReconnect ();

	//
	// make cvars consistant
	//
	if (coop.value)
		Cvar_Set ("deathmatch", "0");
	current_skill = (int)(skill.value + 0.5);
	if (current_skill < 0)
		current_skill = 0;
	if (current_skill > 3)
		current_skill = 3;

	Cvar_SetValue ("skill", (float)current_skill);

	//
	// set up the new server
	//
	// memset (&sv, 0, sizeof(sv));
	Host_ClearMemory (va ("maps/%s.bsp", server));

	q_strlcpy (sv.name, server, sizeof (sv.name));

	sv.protocol = sv_protocol; // johnfitz

	if (sv.protocol == PROTOCOL_RMQ)
	{
		// set up the protocol flags used by this server
		// (note - these could be cvar-ised so that server admins could choose the protocol features used by their servers)
		if (sv_protocol_pext2) // spike: I don't really want to step on anyone's toes, but floats have the exact same precision as qc does.
			sv.protocolflags = PRFL_FLOATCOORD | PRFL_SHORTANGLE;
		else // spike: purists might want to preserve the inprecision and just extend the range though. This matches vanilla QS. should compress a bit better
			 // too.
			sv.protocolflags = PRFL_INT32COORD | PRFL_SHORTANGLE;
	}
	else
		sv.protocolflags = 0;

	PR_SwitchQCVM (vm);
	// load progs to get entity field count
	PR_LoadProgs ("progs.dat", true, PROGHEADER_CRC, pr_ssqcbuiltins, pr_ssqcnumbuiltins);

	// allocate server memory
	/* Host_ClearMemory() called above already cleared the whole sv structure */
	qcvm->max_edicts = CLAMP (MIN_EDICTS, (int)max_edicts.value, MAX_EDICTS);  // johnfitz -- max_edicts cvar
	qcvm->edicts = (edict_t *)Mem_Alloc (qcvm->max_edicts * qcvm->edict_size); // ericw -- sv.edicts switched to use malloc()
	assert (qcvm->free_edicts_head == NULL);
	assert (qcvm->free_edicts_tail == NULL);

	sv.datagram.maxsize = sizeof (sv.datagram_buf);
	sv.datagram.cursize = 0;
	sv.datagram.data = sv.datagram_buf;

	sv.multicast.maxsize = sizeof (sv.multicast_buf);
	sv.multicast.cursize = 0;
	sv.multicast.data = sv.multicast_buf;

	sv.reliable_datagram.maxsize = sizeof (sv.reliable_datagram_buf);
	sv.reliable_datagram.cursize = 0;
	sv.reliable_datagram.data = sv.reliable_datagram_buf;

	sv.signon.maxsize = sizeof (sv.signon_buf);
	sv.signon.cursize = 0;
	sv.signon.data = sv.signon_buf;

	// leave slots at start for clients only
	qcvm->num_edicts = qcvm->reserved_edicts = svs.maxclients + 1;
	memset (qcvm->edicts, 0, qcvm->num_edicts * qcvm->edict_size); // ericw -- sv.edicts switched to use malloc()
	for (i = 0; i < svs.maxclients; i++)
	{
		ent = EDICT_NUM (i + 1);
		svs.clients[i].edict = ent;
	}

	sv.state = ss_loading;
	sv.paused = false;

	qcvm->time = 1.0;

	q_strlcpy (sv.name, server, sizeof (sv.name));
	q_snprintf (sv.modelname, sizeof (sv.modelname), "maps/%s.bsp", server);
	qcvm->worldmodel = Mod_ForName (sv.modelname, false);
	if (!qcvm->worldmodel || qcvm->worldmodel->type != mod_brush)
	{
		Con_Printf ("Couldn't spawn server %s\n", sv.modelname);
		sv.active = false;
		return;
	}
	sv.models[1] = qcvm->worldmodel;
	qcvm->GetModel = SV_ModelForIndex;

	//
	// clear world interaction links
	//
	SV_ClearWorld ();

	sv.sound_precache[0] = dummy;
	sv.model_precache[0] = dummy;
	sv.model_precache[1] = sv.modelname;
	if (qcvm->worldmodel->numsubmodels > MAX_MODELS)
	{
		Con_Printf ("too many inline models %s\n", sv.modelname);
		sv.active = false;
		return;
	}
	for (i = 1; i < qcvm->worldmodel->numsubmodels; i++)
	{
		sv.model_precache[1 + i] = localmodels[i];
		sv.models[i + 1] = Mod_ForName (localmodels[i], false);
	}

	//
	// load the rest of the entities
	//
	ent = EDICT_NUM (0);
	memset (&ent->v, 0, qcvm->progs->entityfields * 4);
	ent->free = false;
	ent->v.model = PR_SetEngineString (qcvm->worldmodel->name);
	ent->v.modelindex = 1; // world model
	ent->v.solid = SOLID_BSP;
	ent->v.movetype = MOVETYPE_PUSH;

	if (coop.value)
		pr_global_struct->coop = coop.value;
	else
		pr_global_struct->deathmatch = deathmatch.value;

	pr_global_struct->mapname = PR_SetEngineString (sv.name);

	// serverflags are for cross level information (sigils)
	pr_global_struct->serverflags = svs.serverflags;

	ED_LoadFromFile (qcvm->worldmodel->entities);

	sv.active = true;

	SV_Precache_Model ("progs/player.mdl"); // Spike -- SV_CreateBaseline depends on this model.

	// all setup is completed, any further precache statements are errors
	sv.state = ss_active;

	// run two frames to allow everything to settle
	host_frametime = 0.1;
	SV_Physics ();
	SV_Physics ();

	// create a baseline for more efficient communications
	SV_CreateBaseline ();
	qcvm->min_edicts = qcvm->num_edicts;

	// johnfitz -- warn if signon buffer larger than standard server can handle
	if (sv.signon.cursize > 8000 - 2) // max size that will fit into 8000-sized client->message buffer with 2 extra bytes on the end
		Con_DWarning ("%i byte signon buffer exceeds standard limit of 7998 (max = %d).\n", sv.signon.cursize, sv.signon.maxsize);
	// johnfitz

	// send serverinfo to all connected clients
	for (i = 0, host_client = svs.clients; i < svs.maxclients; i++, host_client++)
	{
		host_client->knowntoqc = false;
		if (host_client->active)
			SV_SendServerinfo (host_client);
	}

	Con_DPrintf ("Server spawned.\n");
}
