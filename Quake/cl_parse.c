/*
Copyright (C) 1996-2001 Id Software, Inc.
Copyright (C) 2002-2009 John Fitzgibbons and others
Copyright (C) 2007-2008 Kristian Duske
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
// cl_parse.c  -- parse a message received from the server

#include "quakedef.h"
#include "bgmusic.h"

const char *svc_strings[128] = {
	"svc_bad", "svc_nop", "svc_disconnect", "svc_updatestat",
	"svc_version",	 // [long] server version
	"svc_setview",	 // [short] entity number
	"svc_sound",	 // <see code>
	"svc_time",		 // [float] server time
	"svc_print",	 // [string] null terminated string
	"svc_stufftext", // [string] stuffed into client's console buffer
					 // the string should be \n terminated
	"svc_setangle",	 // [vec3] set the view angle to this absolute value

	"svc_serverinfo",	// [long] version
						// [string] signon string
						// [string]..[0]model cache [string]...[0]sounds cache
						// [string]..[0]item cache
	"svc_lightstyle",	// [byte] [string]
	"svc_updatename",	// [byte] [string]
	"svc_updatefrags",	// [byte] [short]
	"svc_clientdata",	// <shortbits + data>
	"svc_stopsound",	// <see code>
	"svc_updatecolors", // [byte] [byte]
	"svc_particle",		// [vec3] <variable>
	"svc_damage",		// [byte] impact [byte] blood [vec3] from

	"svc_spawnstatic",
	/*"OBSOLETE svc_spawnbinary"*/ "21 svc_spawnstatic_fte", "svc_spawnbaseline",

	"svc_temp_entity", // <variable>
	"svc_setpause", "svc_signonnum", "svc_centerprint", "svc_killedmonster", "svc_foundsecret", "svc_spawnstaticsound", "svc_intermission",
	"svc_finale",  // [string] music [string] text
	"svc_cdtrack", // [byte] track [byte] looptrack
	"svc_sellscreen", "svc_cutscene",
	// johnfitz -- new server messages
	"svc_showpic_dp",			  // 35
	"svc_hidepic_dp",			  // 36
	"svc_skybox_fitz",			  // 37					// [string] skyname
	"38",						  // 38
	"39",						  // 39
	"svc_bf_fitz",				  // 40						// no data
	"svc_fog_fitz",				  // 41					// [byte] density [byte] red [byte] green [byte] blue [float] time
	"svc_spawnbaseline2_fitz",	  // 42			// support for large modelindex, large framenum, alpha, using flags
	"svc_spawnstatic2_fitz",	  // 43			// support for large modelindex, large framenum, alpha, using flags
	"svc_spawnstaticsound2_fitz", //	44		// [coord3] [short] samp [byte] vol [byte] aten
								  // johnfitz

	// 2021 RE-RELEASE:
	"svc_setviews",		  // 45
	"svc_updateping",	  // 46
	"svc_updatesocial",	  // 47
	"svc_updateplinfo",	  // 48
	"svc_rawprint",		  // 49
	"svc_servervars",	  // 50
	"svc_seq",			  // 51
	"svc_achievement",	  // 52
	"svc_chat",			  // 53
	"svc_levelcompleted", // 54
	"svc_backtolobby",	  // 55
	"svc_localsound"	  // 56
};
#define NUM_SVC_STRINGS (sizeof (svc_strings) / sizeof (svc_strings[0]))

qboolean warn_about_nehahra_protocol; // johnfitz

extern vec3_t v_punchangles[2];		  // johnfitz
extern double v_punchangles_times[2]; // spike -- don't assume 10fps...

//=============================================================================

/*
===============
CL_EntityNum

This error checks and tracks the total number of entities
===============
*/
entity_t *CL_EntityNum (int num)
{
	// johnfitz -- check minimum number too
	if (num < 0)
		Host_Error ("CL_EntityNum: %i is an invalid number", num);
	// john

	if (num >= cl.num_entities)
	{
		if (num >= cl.max_edicts) // johnfitz -- no more MAX_EDICTS
			Host_Error ("CL_EntityNum: %i is an invalid number", num);
		while (cl.num_entities <= num)
		{
			cl.entities[cl.num_entities].baseline = nullentitystate;
			cl.entities[cl.num_entities].lerpflags |= LERP_RESETMOVE | LERP_RESETANIM; // johnfitz
			cl.num_entities++;
		}
	}

	return &cl.entities[num];
}

static int MSG_ReadSize16 (sizebuf_t *sb)
{
	unsigned short ssolid = MSG_ReadShort ();
	if (ssolid == ES_SOLID_BSP)
		return ssolid;
	else
	{
		int solid = (((ssolid >> 7) & 0x1F8) - 32 + 32768) << 16; /*up can be negative*/
		solid |= ((ssolid & 0x1F) << 3);
		solid |= ((ssolid & 0x3E0) << 10);
		return solid;
	}
}
static unsigned int CLFTE_ReadDelta (unsigned int entnum, entity_state_t *news, const entity_state_t *olds, const entity_state_t *baseline)
{
	unsigned int predbits = 0;
	unsigned int bits;

	bits = MSG_ReadByte ();
	if (bits & UF_EXTEND1)
		bits |= MSG_ReadByte () << 8;
	if (bits & UF_EXTEND2)
		bits |= MSG_ReadByte () << 16;
	if (bits & UF_EXTEND3)
		bits |= MSG_ReadByte () << 24;

	if (cl_shownet.value >= 3)
		Con_SafePrintf ("%3i:     Update %4i 0x%x\n", msg_readcount, entnum, bits);

	if (bits & UF_RESET)
	{
		//		Con_Printf("%3i: Reset %i @ %i\n", msg_readcount, entnum, cls.netchan.incoming_sequence);
		*news = *baseline;
	}
	else if (!olds)
	{
		/*reset got lost, probably the data will be filled in later - FIXME: we should probably ignore this entity*/
		if (sv.active)
		{ // for extra debug info
			qcvm_t *old = qcvm;
			qcvm = NULL;
			PR_SwitchQCVM (&sv.qcvm);
			Con_DPrintf (
				"New entity %i(%s / %s) without reset\n", entnum, PR_GetString (EDICT_NUM (entnum)->v.classname), PR_GetString (EDICT_NUM (entnum)->v.model));
			PR_SwitchQCVM (old);
		}
		else
			Con_DPrintf ("New entity %i without reset\n", entnum);
		*news = *baseline;
	}
	else
		*news = *olds;

	if (bits & UF_FRAME)
	{
		if (bits & UF_16BIT)
			news->frame = MSG_ReadShort ();
		else
			news->frame = MSG_ReadByte ();
	}

	if (bits & UF_ORIGINXY)
	{
		news->origin[0] = MSG_ReadCoord (cl.protocolflags);
		news->origin[1] = MSG_ReadCoord (cl.protocolflags);
	}
	if (bits & UF_ORIGINZ)
		news->origin[2] = MSG_ReadCoord (cl.protocolflags);

	if ((bits & UF_PREDINFO) && !(cl.protocol_pext2 & PEXT2_PREDINFO))
	{
		// predicted stuff gets more precise angles
		if (bits & UF_ANGLESXZ)
		{
			news->angles[0] = MSG_ReadAngle16 (cl.protocolflags);
			news->angles[2] = MSG_ReadAngle16 (cl.protocolflags);
		}
		if (bits & UF_ANGLESY)
			news->angles[1] = MSG_ReadAngle16 (cl.protocolflags);
	}
	else
	{
		if (bits & UF_ANGLESXZ)
		{
			news->angles[0] = MSG_ReadAngle (cl.protocolflags);
			news->angles[2] = MSG_ReadAngle (cl.protocolflags);
		}
		if (bits & UF_ANGLESY)
			news->angles[1] = MSG_ReadAngle (cl.protocolflags);
	}

	if ((bits & (UF_EFFECTS | UF_EFFECTS2)) == (UF_EFFECTS | UF_EFFECTS2))
		news->effects = MSG_ReadLong ();
	else if (bits & UF_EFFECTS2)
		news->effects = (unsigned short)MSG_ReadShort ();
	else if (bits & UF_EFFECTS)
		news->effects = MSG_ReadByte ();

	//	news->movement[0] = 0;
	//	news->movement[1] = 0;
	//	news->movement[2] = 0;
	news->velocity[0] = 0;
	news->velocity[1] = 0;
	news->velocity[2] = 0;
	if (bits & UF_PREDINFO)
	{
		predbits = MSG_ReadByte ();

		if (predbits & UFP_FORWARD)
			/*news->movement[0] =*/MSG_ReadShort ();
		// else
		//	news->movement[0] = 0;
		if (predbits & UFP_SIDE)
			/*news->movement[1] =*/MSG_ReadShort ();
		// else
		//	news->movement[1] = 0;
		if (predbits & UFP_UP)
			/*news->movement[2] =*/MSG_ReadShort ();
		// else
		//	news->movement[2] = 0;
		if (predbits & UFP_MOVETYPE)
			news->pmovetype = MSG_ReadByte ();
		if (predbits & UFP_VELOCITYXY)
		{
			news->velocity[0] = MSG_ReadShort ();
			news->velocity[1] = MSG_ReadShort ();
		}
		else
		{
			news->velocity[0] = 0;
			news->velocity[1] = 0;
		}
		if (predbits & UFP_VELOCITYZ)
			news->velocity[2] = MSG_ReadShort ();
		else
			news->velocity[2] = 0;
		if (predbits & UFP_MSEC) // the msec value is how old the update is (qw clients normally predict without the server running an update every frame)
			/*news->msec =*/MSG_ReadByte ();
		// else
		//	news->msec = 0;

		if (cl.protocol_pext2 & PEXT2_PREDINFO)
		{
			if (predbits & UFP_VIEWANGLE)
			{
				if (bits & UF_ANGLESXZ)
				{
					/*news->vangle[0] =*/MSG_ReadShort ();
					/*news->vangle[2] =*/MSG_ReadShort ();
				}
				if (bits & UF_ANGLESY)
					/*news->vangle[1] =*/MSG_ReadShort ();
			}
		}
		else
		{
			if (predbits & UFP_WEAPONFRAME_OLD)
			{
				int wframe;
				wframe = MSG_ReadByte ();
				if (wframe & 0x80)
					wframe = (wframe & 127) | (MSG_ReadByte () << 7);
			}
		}
	}
	else
	{
		// news->msec = 0;
	}

	if (!(predbits & UFP_VIEWANGLE) || !(cl.protocol_pext2 & PEXT2_PREDINFO))
	{ /*
		 if (bits & UF_ANGLESXZ)
			 news->vangle[0] = ANGLE2SHORT(news->angles[0] * ((bits & UF_PREDINFO)?-3:-1));
		 if (bits & UF_ANGLESY)
			 news->vangle[1] = ANGLE2SHORT(news->angles[1]);
		 if (bits & UF_ANGLESXZ)
			 news->vangle[2] = ANGLE2SHORT(news->angles[2]);
		 */
	}

	if (bits & UF_MODEL)
	{
		if (bits & UF_16BIT)
			news->modelindex = MSG_ReadShort ();
		else
			news->modelindex = MSG_ReadByte ();
	}
	if (bits & UF_SKIN)
	{
		if (bits & UF_16BIT)
			news->skin = MSG_ReadShort ();
		else
			news->skin = MSG_ReadByte ();
	}
	if (bits & UF_COLORMAP)
		news->colormap = MSG_ReadByte ();

	if (bits & UF_SOLID)
		/*news->solidsize =*/MSG_ReadSize16 (&net_message);

	if (bits & UF_FLAGS)
		news->eflags = MSG_ReadByte ();

	if (bits & UF_ALPHA)
		news->alpha = (MSG_ReadByte () + 1) & 0xff;
	if (bits & UF_SCALE)
		news->scale = MSG_ReadByte ();
	if (bits & UF_BONEDATA)
	{
		unsigned char fl = MSG_ReadByte ();
		if (fl & 0x80)
		{
			// this is NOT finalized
			int i;
			int bonecount = MSG_ReadByte ();
			// short *bonedata = AllocateBoneSpace(newp, bonecount, &news->boneoffset);
			for (i = 0; i < bonecount * 7; i++) /*bonedata[i] =*/
				MSG_ReadShort ();
			// news->bonecount = bonecount;
		}
		// else
		// news->bonecount = 0;	//oo, it went away.
		if (fl & 0x40)
		{
			/*news->basebone =*/MSG_ReadByte ();
			/*news->baseframe =*/MSG_ReadShort ();
		}
		/*else
		{
			news->basebone = 0;
			news->baseframe = 0;
		}*/

		// fixme: basebone, baseframe, etc.
		if (fl & 0x3f)
			Host_EndGame ("unsupported entity delta info\n");
	}
	//	else if (news->bonecount)
	//	{	//still has bone data from the previous frame.
	//		short *bonedata = AllocateBoneSpace(newp, news->bonecount, &news->boneoffset);
	//		memcpy(bonedata, oldp->bonedata+olds->boneoffset, sizeof(short)*7*news->bonecount);
	//	}

	if (bits & UF_DRAWFLAGS)
	{
		int drawflags = MSG_ReadByte ();
		if ((drawflags & /*MLS_MASK*/ 7) == /*MLS_ABSLIGHT*/ 7)
			/*news->abslight =*/MSG_ReadByte ();
		// else
		//	news->abslight = 0;
		// news->drawflags = drawflags;
	}
	if (bits & UF_TAGINFO)
	{
		news->tagentity = MSG_ReadEntity (cl.protocol_pext2);
		news->tagindex = MSG_ReadByte ();
	}
	if (bits & UF_LIGHT)
	{
		/*news->light[0] =*/MSG_ReadShort ();
		/*news->light[1] =*/MSG_ReadShort ();
		/*news->light[2] =*/MSG_ReadShort ();
		/*news->light[3] =*/MSG_ReadShort ();
		/*news->lightstyle =*/MSG_ReadByte ();
		/*news->lightpflags =*/MSG_ReadByte ();
	}
	if (bits & UF_TRAILEFFECT)
	{
		unsigned short v = MSG_ReadShort ();
		news->emiteffectnum = 0;
		news->traileffectnum = v & 0x3fff;
		if (v & 0x8000)
			news->emiteffectnum = MSG_ReadShort () & 0x3fff;
		if (news->traileffectnum >= MAX_PARTICLETYPES)
			news->traileffectnum = 0;
		if (news->emiteffectnum >= MAX_PARTICLETYPES)
			news->emiteffectnum = 0;
	}

	if (bits & UF_COLORMOD)
	{
		news->colormod[0] = MSG_ReadByte ();
		news->colormod[1] = MSG_ReadByte ();
		news->colormod[2] = MSG_ReadByte ();
	}
	if (bits & UF_GLOW)
	{
		/*news->glowsize =*/MSG_ReadByte ();
		/*news->glowcolour =*/MSG_ReadByte ();
		/*news->glowmod[0] =*/MSG_ReadByte ();
		/*news->glowmod[1] =*/MSG_ReadByte ();
		/*news->glowmod[2] =*/MSG_ReadByte ();
	}
	if (bits & UF_FATNESS)
		/*news->fatness =*/MSG_ReadByte ();
	if (bits & UF_MODELINDEX2)
	{
		if (bits & UF_16BIT)
			/*news->modelindex2 =*/MSG_ReadShort ();
		else
			/*news->modelindex2 =*/MSG_ReadByte ();
	}
	if (bits & UF_GRAVITYDIR)
	{
		/*news->gravitydir[0] =*/MSG_ReadByte ();
		/*news->gravitydir[1] =*/MSG_ReadByte ();
	}
	if (bits & UF_UNUSED2)
	{
#ifdef LERP_BANDAID
		news->lerp = MSG_ReadShort ();
#else
		Host_EndGame ("UF_UNUSED2 bit\n");
#endif
	}
	if (bits & UF_UNUSED1)
	{
		Host_EndGame ("UF_UNUSED1 bit\n");
	}
	return bits;
}
static void CLFTE_ParseBaseline (entity_state_t *es)
{
	CLFTE_ReadDelta (0, es, &nullentitystate, &nullentitystate);
}

// called with both fte+dp deltas
static void CL_EntitiesDeltaed (void)
{
	int		  newnum;
	qmodel_t *model;
	qboolean  forcelink;
	entity_t *ent;
	int		  skin;

	for (newnum = 1; newnum < cl.num_entities; newnum++)
	{
		ent = CL_EntityNum (newnum);
		if (!ent->update_type)
			continue; // not interested in this one

		if (ent->msgtime == cl.mtime[0])
			forcelink = false; // update got fragmented, don't dirty anything.
		else
		{
			if (ent->msgtime != cl.mtime[1])
				forcelink = true; // no previous frame to lerp from
			else
				forcelink = false;

			// johnfitz -- lerping
			if (ent->msgtime + 0.2 < cl.mtime[0]) // more than 0.2 seconds since the last message (most entities think every 0.1 sec)
				ent->lerpflags |= LERP_RESETANIM; // if we missed a think, we'd be lerping from the wrong frame

			ent->msgtime = cl.mtime[0];

			// shift the known values for interpolation
			VectorCopy (ent->msg_origins[0], ent->msg_origins[1]);
			VectorCopy (ent->msg_angles[0], ent->msg_angles[1]);

			VectorCopy (ent->netstate.origin, ent->msg_origins[0]);
			VectorCopy (ent->netstate.angles, ent->msg_angles[0]);
		}
		skin = ent->netstate.skin;
		if (skin != ent->skinnum)
		{
			ent->skinnum = skin;
			if (newnum > 0 && newnum <= cl.maxclients)
				R_TranslateNewPlayerSkin (newnum - 1); // johnfitz -- was R_TranslatePlayerSkin
		}
		ent->effects = ent->netstate.effects;

		// johnfitz -- lerping for movetype_step entities
		if (ent->netstate.eflags & EFLAGS_STEP)
		{
			ent->lerpflags |= LERP_MOVESTEP;
			ent->forcelink = true;
		}
		else
			ent->lerpflags &= ~LERP_MOVESTEP;

		ent->alpha = ent->netstate.alpha;
		ent->lerpflags &= ~LERP_FINISH;
#ifdef LERP_BANDAID
		if (ent->netstate.lerp > 0)
		{
			ent->lerpfinish = ent->msgtime + (ent->netstate.lerp - 1) / 1000.f;
			ent->lerpflags |= LERP_FINISH;
		}
#endif

		model = cl.model_precache[ent->netstate.modelindex];
		if (model != ent->model)
		{
			ent->model = model;
			InvalidateTraceLineCache ();

			// automatic animation (torches, etc) can be either all together
			// or randomized
			if (model)
			{
				if (model->synctype == ST_FRAMETIME)
					ent->syncbase = -cl.time;
				else if (model->synctype == ST_RAND)
					ent->syncbase = (float)(rand () & 0x7fff) / 0x7fff;
				else
					ent->syncbase = 0.0;
			}
			else
				forcelink = true; // hack to make null model players work
			if (newnum > 0 && newnum <= cl.maxclients)
				R_TranslateNewPlayerSkin (newnum - 1); // johnfitz -- was R_TranslatePlayerSkin

			ent->lerpflags |= LERP_RESETANIM; // johnfitz -- don't lerp animation across model changes
		}
		else if (model && model->synctype == ST_FRAMETIME && ent->frame != ent->netstate.frame)
			ent->syncbase = -cl.time;
		ent->frame = ent->netstate.frame;

		if (forcelink)
		{ // didn't have an update last message
			VectorCopy (ent->msg_origins[0], ent->msg_origins[1]);
			VectorCopy (ent->msg_origins[0], ent->origin);
			VectorCopy (ent->msg_angles[0], ent->msg_angles[1]);
			VectorCopy (ent->msg_angles[0], ent->angles);
			ent->forcelink = true;
		}
	}
}

static void CLFTE_ParseEntitiesUpdate (void)
{
	int		  newnum;
	qboolean  removeflag;
	entity_t *ent;
	float	  newtime;

	// so the server can know when we got it, and guess which frames we didn't get
	if (cls.netcon && cl.ackframes_count < sizeof (cl.ackframes) / sizeof (cl.ackframes[0]))
		cl.ackframes[cl.ackframes_count++] = NET_QSocketGetSequenceIn (cls.netcon);

	if (cl.protocol_pext2 & PEXT2_PREDINFO)
	{
		int seq = (cl.movemessages & 0xffff0000) | (unsigned short)MSG_ReadShort (); // an ack from our input sequences. strictly ascending-or-equal
		if (seq > cl.movemessages)
			seq -= 0x10000; // check for cl.movemessages overflowing the low 16 bits, and compensate.
		cl.ackedmovemessages = seq;
	}

	newtime = MSG_ReadFloat ();
	if (newtime != cl.mtime[0])
	{ // don't mess up lerps if the server is splitting entities into multiple packets.
		cl.mtime[1] = cl.mtime[0];
		cl.mtime[0] = newtime;
	}

	for (;;)
	{
		newnum = (unsigned short)(short)MSG_ReadShort ();
		removeflag = !!(newnum & 0x8000);
		if (newnum & 0x4000)
			newnum = (newnum & 0x3fff) | (MSG_ReadByte () << 14);
		else
			newnum &= ~0x8000;

		if ((!newnum && !removeflag) || msg_badread)
			break;

		ent = CL_EntityNum (newnum);

		if (removeflag)
		{ // removal.
			if (cl_shownet.value >= 3)
				Con_SafePrintf ("%3i:     Remove %i\n", msg_readcount, newnum);

			if (!newnum)
			{
				/*removal of world - means forget all entities, aka a full reset*/
				if (cl_shownet.value >= 3)
					Con_SafePrintf ("%3i:     Reset all\n", msg_readcount);
				for (newnum = 1; newnum < cl.num_entities; newnum++)
				{
					CL_EntityNum (newnum)->netstate.pmovetype = 0;
					CL_EntityNum (newnum)->model = NULL;
					CL_EntityNum (newnum)->update_type = false;
				}
				cl.requestresend = false; // we got it.
				continue;
			}
			ent->update_type = false; // no longer valid
			ent->model = NULL;
			InvalidateTraceLineCache ();
			continue;
		}
		else if (ent->update_type)
		{ // simple update
			CLFTE_ReadDelta (newnum, &ent->netstate, &ent->netstate, &ent->baseline);
			if (ent->msgtime == cl.mtime[0])
				// we did get an update for this entity, force processing by CL_EntitiesDeltaed
				// even if qcvm time is frozen (sv_freezenonclients support)
				ent->msgtime = cl.mtime[1];
		}
		else
		{ // we had no previous copy of this entity...
			ent->update_type = true;
			CLFTE_ReadDelta (newnum, &ent->netstate, NULL, &ent->baseline);

			// stupid interpolation junk.
			ent->lerpflags |= LERP_RESETMOVE | LERP_RESETANIM;
		}
	}

	CL_EntitiesDeltaed ();

	if (cl.protocol_pext2 & PEXT2_PREDINFO)
	{ // stats should normally be sent before the entity data.
		extern cvar_t v_gunkick;
		VectorCopy (cl.mvelocity[0], cl.mvelocity[1]);
		ent = CL_EntityNum (cl.viewentity);
		cl.mvelocity[0][0] = ent->netstate.velocity[0] * (1 / 8.0);
		cl.mvelocity[0][1] = ent->netstate.velocity[1] * (1 / 8.0);
		cl.mvelocity[0][2] = ent->netstate.velocity[2] * (1 / 8.0);
		cl.onground = (ent->netstate.eflags & EFLAGS_ONGROUND) ? true : false;

		if (v_gunkick.value == 1)
		{ // truncate away any extra precision, like vanilla/qs would.
			cl.punchangle[0] = cl.stats[STAT_PUNCHANGLE_X];
			cl.punchangle[1] = cl.stats[STAT_PUNCHANGLE_Y];
			cl.punchangle[2] = cl.stats[STAT_PUNCHANGLE_Z];
		}
		else
		{ // woo, more precision
			cl.punchangle[0] = cl.statsf[STAT_PUNCHANGLE_X];
			cl.punchangle[1] = cl.statsf[STAT_PUNCHANGLE_Y];
			cl.punchangle[2] = cl.statsf[STAT_PUNCHANGLE_Z];
		}
		if (v_punchangles[0][0] != cl.punchangle[0] || v_punchangles[0][1] != cl.punchangle[1] || v_punchangles[0][2] != cl.punchangle[2])
		{
			v_punchangles_times[1] = v_punchangles_times[0];
			v_punchangles_times[0] = newtime;

			VectorCopy (v_punchangles[0], v_punchangles[1]);
			VectorCopy (cl.punchangle, v_punchangles[0]);
		}
	}

	if (!cl.requestresend)
	{
		if (cls.signon == SIGNONS - 1)
		{ // first update is the final signon stage
			cls.signon = SIGNONS;
			CL_SignonReply ();
		}
	}
}

/*
==================
CL_ParseStartSoundPacket
==================
*/
static void CL_ParseStartSoundPacket (void)
{
	vec3_t pos;
	int	   channel, ent;
	int	   sound_num;
	int	   volume;
	int	   field_mask;
	float  attenuation;
	int	   i;

	field_mask = MSG_ReadByte ();
	if (field_mask & SND_FTE_MOREFLAGS)
		field_mask |= MSG_ReadByte () << 8;

	if (field_mask & SND_VOLUME)
		volume = MSG_ReadByte ();
	else
		volume = DEFAULT_SOUND_PACKET_VOLUME;

	if (field_mask & SND_ATTENUATION)
		attenuation = MSG_ReadByte () / 64.0;
	else
		attenuation = DEFAULT_SOUND_PACKET_ATTENUATION;

	// fte's sound extensions
	if (cl.protocol_pext2 & PEXT2_REPLACEMENTDELTAS)
	{
		// spike -- our mixer can't deal with these, so just parse and ignore
		if (field_mask & SND_FTE_PITCHADJ)
			MSG_ReadByte (); // percentage
		if (field_mask & SND_FTE_TIMEOFS)
			MSG_ReadShort (); // in ms
		if (field_mask & SND_FTE_VELOCITY)
		{
			MSG_ReadShort (); // 1/8th
			MSG_ReadShort (); // 1/8th
			MSG_ReadShort (); // 1/8th
		}
	}
	else if (field_mask & (SND_FTE_MOREFLAGS | SND_FTE_PITCHADJ | SND_FTE_TIMEOFS))
		Con_Warning ("Unknown meaning for sound flags\n");
	if (cl.protocol_pext2 & PEXT2_REPLACEMENTDELTAS)
	{
		if (field_mask & SND_DP_PITCH)
			MSG_ReadShort ();
	}
	else if (field_mask & SND_DP_PITCH)
		Con_Warning ("Unknown meaning for sound flags\n");

	// johnfitz -- PROTOCOL_FITZQUAKE
	if (field_mask & SND_LARGEENTITY)
	{
		ent = (unsigned short)MSG_ReadShort ();
		channel = MSG_ReadByte ();
	}
	else
	{
		channel = (unsigned short)MSG_ReadShort ();
		ent = channel >> 3;
		channel &= 7;
	}

	if (field_mask & SND_LARGESOUND)
		sound_num = (unsigned short)MSG_ReadShort ();
	else
		sound_num = MSG_ReadByte ();
	// johnfitz

	// johnfitz -- check soundnum
	if (sound_num >= MAX_SOUNDS)
		Host_Error ("CL_ParseStartSoundPacket: %i > MAX_SOUNDS", sound_num);
	// johnfitz

	if (ent > cl.max_edicts) // johnfitz -- no more MAX_EDICTS
		Host_Error ("CL_ParseStartSoundPacket: ent = %i", ent);

	for (i = 0; i < 3; i++)
		pos[i] = MSG_ReadCoord (cl.protocolflags);

	S_StartSound (ent, channel, cl.sound_precache[sound_num], pos, volume / 255.0, attenuation);
}

/*
==================
CL_ParseLocalSound - for 2021 rerelease
==================
*/
void CL_ParseLocalSound (void)
{
	int field_mask, sound_num;

	field_mask = MSG_ReadByte ();
	sound_num = (field_mask & SND_LARGESOUND) ? MSG_ReadShort () : MSG_ReadByte ();
	if (sound_num >= MAX_SOUNDS)
		Host_Error ("CL_ParseLocalSound: %i > MAX_SOUNDS", sound_num);

	S_LocalSound (cl.sound_precache[sound_num]->name);
}

#if 0
/*
==================
CL_KeepaliveMessage

When the client is taking a long time to load stuff, send keepalive messages
so the server doesn't disconnect.
==================
*/
static byte	net_olddata[NET_MAXMESSAGE];
static void CL_KeepaliveMessage (void)
{
	float	time;
	static float lastmsg;
	int		ret;
	sizebuf_t	old;
	byte	*olddata;

	if (sv.active)
		return;		// no need if server is local
	if (cls.demoplayback)
		return;

// read messages from server, should just be nops
	olddata = net_olddata;
	old = net_message;
	memcpy (olddata, net_message.data, net_message.cursize);

	do
	{
		ret = CL_GetMessage ();
		switch (ret)
		{
		default:
			Host_Error ("CL_KeepaliveMessage: CL_GetMessage failed");
		case 0:
			break;	// nothing waiting
		case 1:
			Host_Error ("CL_KeepaliveMessage: received a message");
			break;
		case 2:
			if (MSG_ReadByte() != svc_nop)
				Host_Error ("CL_KeepaliveMessage: datagram wasn't a nop");
			break;
		}
	} while (ret);

	net_message = old;
	memcpy (net_message.data, olddata, net_message.cursize);

// check time
	time = Sys_DoubleTime ();
	if (time - lastmsg < 5)
		return;
	lastmsg = time;

// write out a nop
	Con_Printf ("--> client to server keepalive\n");

	MSG_WriteByte (&cls.message, clc_nop);
	NET_SendMessage (cls.netcon, &cls.message);
	SZ_Clear (&cls.message);
}
#endif

/*
==================
CL_ParseServerInfo
==================
*/
static void CL_ParseServerInfo (void)
{
	const char *str;
	int			i;
	qboolean	gamedirswitchwarning = false;
	char		gamedir[1024];
	char		protname[64];
	int			nummodels, numsounds;
	char		model_precache[MAX_MODELS][MAX_QPATH];
	char		sound_precache[MAX_SOUNDS][MAX_QPATH];

	Con_DPrintf ("Serverinfo packet received.\n");

	// ericw -- bring up loading plaque for map changes within a demo.
	//          it will be hidden in CL_SignonReply.
	if (cls.demoplayback)
		SCR_BeginLoadingPlaque ();

	//
	// wipe the client_state_t struct
	//
	CL_ClearState ();

	if (sv.loadgame)
		V_StopPitchDrift ();

	Key_ClearStates ();
	IN_ClearStates ();

	// parse protocol version number
	for (;;)
	{
		i = MSG_ReadLong ();
		if (i == PROTOCOL_FTE_PEXT1)
		{
			cl.protocol_pext1 = MSG_ReadLong ();
			if (cl.protocol_pext1 & ~PEXT1_ACCEPTED_CLIENT)
				Host_Error ("Server returned FTE1 protocol extensions that are not supported (%#x)", cl.protocol_pext1 & ~PEXT1_SUPPORTED_CLIENT);
			continue;
		}
		if (i == PROTOCOL_FTE_PEXT2)
		{
			cl.protocol_pext2 = MSG_ReadLong ();
			if (cl.protocol_pext2 & ~PEXT2_ACCEPTED_CLIENT)
				Host_Error ("Server returned FTE2 protocol extensions that are not supported (%#x)", cl.protocol_pext2 & ~PEXT2_SUPPORTED_CLIENT);
			continue;
		}
		break;
	}

	// johnfitz -- support multiple protocols
	if (i != PROTOCOL_NETQUAKE && i != PROTOCOL_FITZQUAKE && i != PROTOCOL_RMQ)
	{
		Con_Printf ("\n"); // because there's no newline after serverinfo print
		Host_Error ("Server returned version %i, not %i or %i or %i", i, PROTOCOL_NETQUAKE, PROTOCOL_FITZQUAKE, PROTOCOL_RMQ);
	}
	cl.protocol = i;
	// johnfitz

	if (cl.protocol == PROTOCOL_RMQ)
	{
		const unsigned int supportedflags = (PRFL_SHORTANGLE | PRFL_FLOATANGLE | PRFL_24BITCOORD | PRFL_FLOATCOORD | PRFL_EDICTSCALE | PRFL_INT32COORD);

		// mh - read protocol flags from server so that we know what protocol features to expect
		cl.protocolflags = (unsigned int)MSG_ReadLong ();

		if (0 != (cl.protocolflags & (~supportedflags)))
		{
			Con_Warning ("PROTOCOL_RMQ protocolflags %i contains unsupported flags\n", cl.protocolflags);
		}
	}
	else
		cl.protocolflags = 0;

	*gamedir = 0;
	if (cl.protocol_pext2 & PEXT2_PREDINFO)
	{
		q_strlcpy (gamedir, MSG_ReadString (), sizeof (gamedir));
		if (!COM_GameDirMatches (gamedir))
		{
			gamedirswitchwarning = true;
		}
	}

	// parse maxclients
	cl.maxclients = MSG_ReadByte ();
	if (cl.maxclients < 1 || cl.maxclients > MAX_SCOREBOARD)
	{
		Host_Error ("Bad maxclients (%u) from server", cl.maxclients);
	}
	cl.scores = (scoreboard_t *)Mem_Alloc (cl.maxclients * sizeof (*cl.scores));

	// parse gametype
	cl.gametype = MSG_ReadByte ();

	// parse signon message
	str = MSG_ReadString ();
	q_strlcpy (cl.levelname, str, sizeof (cl.levelname));

	// seperate the printfs so the server message can have a color
	Con_Printf ("\n%s\n", Con_Quakebar (40)); // johnfitz
	Con_Printf ("%c%s\n", 2, str);

	// johnfitz -- tell user which protocol this is
	if (cl.protocol_pext2 & PEXT2_REPLACEMENTDELTAS)
		q_snprintf (protname, sizeof (protname), "fte%i", cl.protocol);
	else
		q_snprintf (protname, sizeof (protname), "%i", cl.protocol);
	Con_Printf ("Using protocol %s\n", protname);
	if (gamedirswitchwarning)
		Con_Warning ("gamedir mismatch: server \"%s\" ours \"%s\"\n", gamedir, COM_GetGameNames (false));

	// first we go through and touch all of the precache data that still
	// happens to be in the cache, so precaching something else doesn't
	// needlessly purge it

	// precache models
	memset (cl.model_precache, 0, sizeof (cl.model_precache));
	for (nummodels = 1;; nummodels++)
	{
		str = MSG_ReadString ();
		if (!str[0])
			break;
		if (!sv.active && nummodels == 1)
			Mod_ClearBModelCaches (str);
		if (nummodels == MAX_MODELS)
		{
			Host_Error ("Server sent too many model precaches");
		}
		q_strlcpy (model_precache[nummodels], str, MAX_QPATH);
		Mod_TouchModel (str);
	}

	// johnfitz -- check for excessive models
	if (nummodels >= 256)
		Con_DWarning ("%i models exceeds standard limit of 256 (max = %d).\n", nummodels, MAX_MODELS);
	// johnfitz

	// precache sounds
	memset (cl.sound_precache, 0, sizeof (cl.sound_precache));
	for (numsounds = 1;; numsounds++)
	{
		str = MSG_ReadString ();
		if (!str[0])
			break;
		if (numsounds == MAX_SOUNDS)
		{
			Host_Error ("Server sent too many sound precaches");
		}
		q_strlcpy (sound_precache[numsounds], str, MAX_QPATH);
		S_TouchSound (str);
	}

	// johnfitz -- check for excessive sounds
	if (numsounds >= 256)
		Con_DWarning ("%i sounds exceeds standard limit of 256 (max = %d).\n", numsounds, MAX_SOUNDS);
	// johnfitz

	//
	// now we try to load everything else until a cache allocation fails
	//

	// copy the naked name of the map file to the cl structure -- O.S
	COM_StripExtension (COM_SkipPath (model_precache[1]), cl.mapname, sizeof (cl.mapname));

	for (i = 1; i < nummodels; i++)
	{
		cl.model_precache[i] = Mod_ForName (model_precache[i], false);
		if (cl.model_precache[i] == NULL)
		{
			Host_Error ("Model %s not found", model_precache[i]);
		}
	}
	S_BeginPrecaching ();
	for (i = 1; i < numsounds; i++)
	{
		cl.sound_precache[i] = S_PrecacheSound (sound_precache[i]);
	}
	S_EndPrecaching ();

	// local state
	cl.entities[0].model = cl.worldmodel = cl.model_precache[1];

	R_NewMap ();

	// johnfitz -- clear out string; we don't consider identical
	// messages to be duplicates if the map has changed in between
	con_lastcenterstring[0] = 0;
	// johnfitz

	noclip_anglehack = false; // noclip is turned off at start

	warn_about_nehahra_protocol = true; // johnfitz -- warn about nehahra protocol hack once per server connection

	// johnfitz -- reset developer stats
	memset (&dev_stats, 0, sizeof (dev_stats));
	memset (&dev_peakstats, 0, sizeof (dev_peakstats));
	memset (&dev_overflows, 0, sizeof (dev_overflows));

	cl.requestresend = true;
	cl.ackframes_count = 0;
	if (cl.protocol_pext2 & PEXT2_REPLACEMENTDELTAS)
		cl.ackframes[cl.ackframes_count++] = -1;
#ifdef PSET_SCRIPT
	// the protocol changing depending upon files found on the client's computer is of course a really shit way to design things
	// especially when users have a nasty habit of changing config files.
	if (cl.protocol_pext2 || (cl.protocol_pext1 & PEXT1_CSQC))
		cl.protocol_particles = true; // doesn't have a pext flag of its own, but at least we know what it is.
#endif
}

/*
==================
CL_ParseUpdate

Parse an entity update message from the server
If an entities model or origin changes from frame to frame, it must be
relinked.  Other attributes can change without relinking.
==================
*/
static void CL_ParseUpdate (int bits)
{
	int			 i;
	qmodel_t	*model;
	unsigned int modnum;
	qboolean	 forcelink;
	entity_t	*ent;
	int			 num;
	int			 skin;

	if (cls.signon == SIGNONS - 1)
	{ // first update is the final signon stage
		cls.signon = SIGNONS;
		CL_SignonReply ();
	}

	if (bits & U_MOREBITS)
	{
		i = MSG_ReadByte ();
		bits |= (i << 8);
	}

	// johnfitz -- PROTOCOL_FITZQUAKE
	if (cl.protocol == PROTOCOL_FITZQUAKE || cl.protocol == PROTOCOL_RMQ)
	{
		if (bits & U_EXTEND1)
			bits |= MSG_ReadByte () << 16;
		if (bits & U_EXTEND2)
			bits |= MSG_ReadByte () << 24;
	}
	// johnfitz

	if (bits & U_LONGENTITY)
		num = MSG_ReadShort ();
	else
		num = MSG_ReadByte ();

	ent = CL_EntityNum (num);

	if (ent->msgtime != cl.mtime[1])
		forcelink = true; // no previous frame to lerp from
	else
		forcelink = false;

	// johnfitz -- lerping
	if (ent->msgtime + 0.2 < cl.mtime[0]) // more than 0.2 seconds since the last message (most entities think every 0.1 sec)
		ent->lerpflags |= LERP_RESETANIM; // if we missed a think, we'd be lerping from the wrong frame
	// johnfitz

	ent->msgtime = cl.mtime[0];

	// copy the baseline into the netstate for the rest of the code to use.
#define netstate_start offsetof (entity_state_t, scale)
	memcpy (
		(char *)&ent->netstate + offsetof (entity_state_t, modelindex), (const char *)&ent->baseline + offsetof (entity_state_t, modelindex),
		sizeof (ent->baseline) - offsetof (entity_state_t, modelindex));

	if (bits & U_MODEL)
	{
		modnum = MSG_ReadByte ();
		if (modnum >= MAX_MODELS)
			Host_Error ("CL_ParseModel: bad modnum");
	}
	else
		modnum = ent->baseline.modelindex;

	if (bits & U_FRAME)
		ent->frame = MSG_ReadByte ();
	else
		ent->frame = ent->baseline.frame;

	if (bits & U_COLORMAP)
		ent->netstate.colormap = MSG_ReadByte ();
	if (bits & U_SKIN)
		skin = MSG_ReadByte ();
	else
		skin = ent->baseline.skin;
	if (skin != ent->skinnum)
	{
		ent->skinnum = skin;
		if (num > 0 && num <= cl.maxclients)
			R_TranslateNewPlayerSkin (num - 1); // johnfitz -- was R_TranslatePlayerSkin
	}
	if (bits & U_EFFECTS)
		ent->effects = MSG_ReadByte ();
	else
		ent->effects = ent->baseline.effects;

	// shift the known values for interpolation
	VectorCopy (ent->msg_origins[0], ent->msg_origins[1]);
	VectorCopy (ent->msg_angles[0], ent->msg_angles[1]);

	if (bits & U_ORIGIN1)
		ent->msg_origins[0][0] = MSG_ReadCoord (cl.protocolflags);
	else
		ent->msg_origins[0][0] = ent->baseline.origin[0];
	if (bits & U_ANGLE1)
		ent->msg_angles[0][0] = MSG_ReadAngle (cl.protocolflags);
	else
		ent->msg_angles[0][0] = ent->baseline.angles[0];

	if (bits & U_ORIGIN2)
		ent->msg_origins[0][1] = MSG_ReadCoord (cl.protocolflags);
	else
		ent->msg_origins[0][1] = ent->baseline.origin[1];
	if (bits & U_ANGLE2)
		ent->msg_angles[0][1] = MSG_ReadAngle (cl.protocolflags);
	else
		ent->msg_angles[0][1] = ent->baseline.angles[1];

	if (bits & U_ORIGIN3)
		ent->msg_origins[0][2] = MSG_ReadCoord (cl.protocolflags);
	else
		ent->msg_origins[0][2] = ent->baseline.origin[2];
	if (bits & U_ANGLE3)
		ent->msg_angles[0][2] = MSG_ReadAngle (cl.protocolflags);
	else
		ent->msg_angles[0][2] = ent->baseline.angles[2];

	// johnfitz -- lerping for movetype_step entities
	if (bits & U_STEP)
	{
		ent->lerpflags |= LERP_MOVESTEP;
		ent->forcelink = true;
	}
	else
		ent->lerpflags &= ~LERP_MOVESTEP;
	// johnfitz

	// johnfitz -- PROTOCOL_FITZQUAKE and PROTOCOL_NEHAHRA
	if (cl.protocol == PROTOCOL_FITZQUAKE || cl.protocol == PROTOCOL_RMQ)
	{
		if (bits & U_ALPHA)
			ent->alpha = MSG_ReadByte ();
		else
			ent->alpha = ent->baseline.alpha;
		if (bits & U_SCALE)
			ent->netstate.scale = MSG_ReadByte (); // PROTOCOL_RMQ
		if (bits & U_FRAME2)
			ent->frame = (ent->frame & 0x00FF) | (MSG_ReadByte () << 8);
		if (bits & U_MODEL2)
		{
			modnum = (modnum & 0x00FF) | (MSG_ReadByte () << 8);
			if (modnum >= MAX_MODELS)
				Host_Error ("CL_ParseModel: bad modnum");
		}
		if (bits & U_LERPFINISH)
		{
			ent->lerpfinish = ent->msgtime + ((float)(MSG_ReadByte ()) / 255);
			ent->lerpflags |= LERP_FINISH;
		}
		else
			ent->lerpflags &= ~LERP_FINISH;
	}
	else if (cl.protocol == PROTOCOL_NETQUAKE)
	{
		// HACK: if this bit is set, assume this is PROTOCOL_NEHAHRA
		if (bits & U_TRANS)
		{
			float a, b;

			if (cl.protocol == PROTOCOL_NETQUAKE && warn_about_nehahra_protocol)
			{
				Con_Warning ("nonstandard update bit, assuming Nehahra protocol\n");
				warn_about_nehahra_protocol = false;
			}

			a = MSG_ReadFloat ();
			b = MSG_ReadFloat (); // alpha
			if (a == 2)
				MSG_ReadFloat (); // fullbright (not using this yet)
			ent->alpha = ENTALPHA_ENCODE (b);
		}
		else
			ent->alpha = ent->baseline.alpha;
	}
	else
		ent->alpha = ent->baseline.alpha;
	// johnfitz

	// johnfitz -- moved here from above
	model = cl.model_precache[modnum];
	if (model != ent->model)
	{
		ent->model = model;
		InvalidateTraceLineCache ();
		// automatic animation (torches, etc) can be either all together
		// or randomized
		if (model)
		{
			if (model->synctype == ST_RAND)
				ent->syncbase = (float)(rand () & 0x7fff) / 0x7fff;
			else
				ent->syncbase = 0.0;
		}
		else
			forcelink = true; // hack to make null model players work
		if (num > 0 && num <= cl.maxclients)
			R_TranslateNewPlayerSkin (num - 1); // johnfitz -- was R_TranslatePlayerSkin

		ent->lerpflags |= LERP_RESETANIM; // johnfitz -- don't lerp animation across model changes
	}
	// johnfitz

	if (forcelink)
	{ // didn't have an update last message
		VectorCopy (ent->msg_origins[0], ent->msg_origins[1]);
		VectorCopy (ent->msg_origins[0], ent->origin);
		VectorCopy (ent->msg_angles[0], ent->msg_angles[1]);
		VectorCopy (ent->msg_angles[0], ent->angles);
		ent->forcelink = true;
	}
}

/*
==================
CL_ParseBaseline
==================
*/
static void CL_ParseBaseline (entity_t *ent, int version) // johnfitz -- added argument
{
	int i;
	int bits; // johnfitz

	if (version == 6)
	{
		CLFTE_ParseBaseline (&ent->baseline);
		return;
	}

	ent->baseline = nullentitystate;

	// johnfitz -- PROTOCOL_FITZQUAKE
	if (version == 7)
		bits = B_LARGEMODEL | B_LARGEFRAME; // dpp7's spawnstatic2
	else
		bits = (version == 2) ? MSG_ReadByte () : 0;
	ent->baseline.modelindex = (bits & B_LARGEMODEL) ? MSG_ReadShort () : MSG_ReadByte ();
	ent->baseline.frame = (bits & B_LARGEFRAME) ? MSG_ReadShort () : MSG_ReadByte ();
	// johnfitz

	ent->baseline.colormap = MSG_ReadByte ();
	ent->baseline.skin = MSG_ReadByte ();
	for (i = 0; i < 3; i++)
	{
		ent->baseline.origin[i] = MSG_ReadCoord (cl.protocolflags);
		ent->baseline.angles[i] = MSG_ReadAngle (cl.protocolflags);
	}

	ent->baseline.alpha = (bits & B_ALPHA) ? MSG_ReadByte () : ENTALPHA_DEFAULT; // johnfitz -- PROTOCOL_FITZQUAKE
	ent->baseline.scale = (bits & B_SCALE) ? MSG_ReadByte () : ENTSCALE_DEFAULT;
}

#define CL_SetStati(stat, val)	 cl.statsf[stat] = (cl.stats[stat] = val)
#define CL_SetHudStat(stat, val) CL_SetStati (stat, val)

/*
==================
CL_ParseClientdata

Server information pertaining to this client only
==================
*/
static void CL_ParseClientdata (void)
{
	int i;
	int bits; // johnfitz

	bits = (unsigned short)MSG_ReadShort (); // johnfitz -- read bits here isntead of in CL_ParseServerMessage()

	// johnfitz -- PROTOCOL_FITZQUAKE
	if (bits & SU_EXTEND1)
		bits |= (MSG_ReadByte () << 16);
	if (bits & SU_EXTEND2)
		bits |= (MSG_ReadByte () << 24);
	// johnfitz

	bits |= SU_ITEMS;

	if (bits & SU_VIEWHEIGHT)
		CL_SetStati (STAT_VIEWHEIGHT, MSG_ReadChar ());
	else
		CL_SetStati (STAT_VIEWHEIGHT, DEFAULT_VIEWHEIGHT);

	if (bits & SU_IDEALPITCH)
		CL_SetStati (STAT_IDEALPITCH, MSG_ReadChar ());
	else
		CL_SetStati (STAT_IDEALPITCH, 0);

	VectorCopy (cl.mvelocity[0], cl.mvelocity[1]);
	for (i = 0; i < 3; i++)
	{
		if (bits & (SU_PUNCH1 << i))
			cl.punchangle[i] = MSG_ReadChar ();
		else
			cl.punchangle[i] = 0;

		if (bits & (SU_VELOCITY1 << i))
			cl.mvelocity[0][i] = MSG_ReadChar () * 16;
		else
			cl.mvelocity[0][i] = 0;
	}

	// johnfitz -- update v_punchangles
	if (v_punchangles[0][0] != cl.punchangle[0] || v_punchangles[0][1] != cl.punchangle[1] || v_punchangles[0][2] != cl.punchangle[2])
	{
		v_punchangles_times[1] = v_punchangles_times[0];
		v_punchangles_times[0] = cl.mtime[0];
		VectorCopy (v_punchangles[0], v_punchangles[1]);
		VectorCopy (cl.punchangle, v_punchangles[0]);
	}
	// johnfitz

	if (bits & SU_ITEMS)
		CL_SetStati (STAT_ITEMS, MSG_ReadLong ());

	cl.onground = (bits & SU_ONGROUND) != 0;
	cl.inwater = (bits & SU_INWATER) != 0;

	{
		unsigned short weaponframe = 0;
		unsigned short armourval = 0;
		unsigned short weaponmodel = 0;
		unsigned int   activeweapon;
		short		   health;
		unsigned short ammo;
		unsigned short ammovals[4];

		if (bits & SU_WEAPONFRAME)
			weaponframe = MSG_ReadByte ();
		if (bits & SU_ARMOR)
			armourval = MSG_ReadByte ();
		if (bits & SU_WEAPON)
			weaponmodel = MSG_ReadByte ();
		health = MSG_ReadShort ();
		ammo = MSG_ReadByte ();
		for (i = 0; i < 4; i++)
			ammovals[i] = MSG_ReadByte ();
		activeweapon = MSG_ReadByte ();
		if (!standard_quake)
			activeweapon = 1u << activeweapon;

		// johnfitz -- PROTOCOL_FITZQUAKE
		if (bits & SU_WEAPON2)
			weaponmodel |= (MSG_ReadByte () << 8);
		if (bits & SU_ARMOR2)
			armourval |= (MSG_ReadByte () << 8);
		if (bits & SU_AMMO2)
			ammo |= (MSG_ReadByte () << 8);
		if (bits & SU_SHELLS2)
			ammovals[0] |= (MSG_ReadByte () << 8);
		if (bits & SU_NAILS2)
			ammovals[1] |= (MSG_ReadByte () << 8);
		if (bits & SU_ROCKETS2)
			ammovals[2] |= (MSG_ReadByte () << 8);
		if (bits & SU_CELLS2)
			ammovals[3] |= (MSG_ReadByte () << 8);
		if (bits & SU_WEAPONFRAME2)
			weaponframe |= (MSG_ReadByte () << 8);
		if (bits & SU_WEAPONALPHA)
			cl.viewent.alpha = MSG_ReadByte ();
		else
			cl.viewent.alpha = ENTALPHA_DEFAULT;
		// johnfitz

		CL_SetHudStat (STAT_WEAPONFRAME, weaponframe);
		CL_SetHudStat (STAT_ARMOR, armourval);
		CL_SetHudStat (STAT_WEAPON, weaponmodel);
		CL_SetHudStat (STAT_ACTIVEWEAPON, activeweapon);
		CL_SetHudStat (STAT_HEALTH, health);
		CL_SetHudStat (STAT_AMMO, ammo);
		CL_SetHudStat (STAT_SHELLS, ammovals[0]);
		CL_SetHudStat (STAT_NAILS, ammovals[1]);
		CL_SetHudStat (STAT_ROCKETS, ammovals[2]);
		CL_SetHudStat (STAT_CELLS, ammovals[3]);
	}
}

/*
=====================
CL_NewTranslation
=====================
*/
void CL_NewTranslation (int slot)
{
	if (slot > cl.maxclients)
		Sys_Error ("CL_NewTranslation: slot > cl.maxclients");
	R_TranslatePlayerSkin (slot);
}

/*
=====================
CL_ParseStatic
=====================
*/
static void CL_ParseStatic (int version) // johnfitz -- added a parameter
{
	entity_t *ent;
	int		  i;

	i = cl.num_statics;
	if (i >= cl.max_static_entities)
	{
		int		   ec = 64;
		entity_t **newstatics = Mem_Realloc (cl.static_entities, sizeof (*newstatics) * (cl.max_static_entities + ec));
		entity_t  *newents = Mem_Alloc (sizeof (*newents) * ec);
		if (!newstatics || !newents)
			Host_Error ("Too many static entities");
		cl.static_entities = newstatics;
		while (ec--)
			cl.static_entities[cl.max_static_entities++] = newents++;
	}

	ent = cl.static_entities[i];
	cl.num_statics++;
	CL_ParseBaseline (ent, version); // johnfitz -- added second parameter

	// copy it to the current state

	ent->netstate = ent->baseline;
	ent->eflags = ent->netstate.eflags; // spike -- annoying and probably not used anyway, but w/e

	ent->model = cl.model_precache[ent->baseline.modelindex];
	ent->lerpflags |= LERP_RESETANIM; // johnfitz -- lerping
	ent->frame = ent->baseline.frame;

	ent->skinnum = ent->baseline.skin;
	ent->effects = ent->baseline.effects;
	ent->alpha = ent->baseline.alpha; // johnfitz -- alpha

	VectorCopy (ent->baseline.origin, ent->origin);
	VectorCopy (ent->baseline.angles, ent->angles);
	if (ent->model)
		R_AddEfrags (ent);

	InvalidateTraceLineCache ();
}

/*
===================
CL_ParseStaticSound
===================
*/
static void CL_ParseStaticSound (int version) // johnfitz -- added argument
{
	vec3_t org;
	int	   sound_num, vol, atten;
	int	   i;

	for (i = 0; i < 3; i++)
		org[i] = MSG_ReadCoord (cl.protocolflags);

	// johnfitz -- PROTOCOL_FITZQUAKE
	if (version == 2)
		sound_num = MSG_ReadShort ();
	else
		sound_num = MSG_ReadByte ();
	// johnfitz

	vol = MSG_ReadByte ();
	atten = MSG_ReadByte ();

	S_StaticSound (cl.sound_precache[sound_num], org, vol, atten);
}

/*
CL_ParsePrecache

spike -- added this mostly for particle effects, but its also used for models+sounds (if needed)
*/
static void CL_ParsePrecache (void)
{
	unsigned short code = MSG_ReadShort ();
	unsigned int   index = code & 0x3fff;
	const char	  *name = MSG_ReadString ();
	switch ((code >> 14) & 0x3)
	{
	case 0: // models
		if (index < MAX_MODELS)
		{
			cl.model_precache[index] = Mod_ForName (name, false);
			// FIXME: if its a bsp model, generate lightmaps.
			// FIXME: update static entities with that modelindex
		}
		break;
#ifdef PSET_SCRIPT
	case 1: // particles
		if (index < MAX_PARTICLETYPES)
		{
			if (*name)
			{
				cl.particle_precache[index].name = q_strdup (name);
				cl.particle_precache[index].index = PScript_FindParticleType (cl.particle_precache[index].name);
			}
			else
			{
				SAFE_FREE (cl.particle_precache[index].name);
				cl.particle_precache[index].index = -1;
			}
		}
		break;
#endif
	case 2: // sounds
		if (index < MAX_SOUNDS)
			cl.sound_precache[index] = S_PrecacheSound (name);
		break;
		//	case 3:	//unused
	default:
		Con_Warning ("CL_ParsePrecache: unsupported precache type\n");
		break;
	}
}
#ifdef PSET_SCRIPT
int			CL_GenerateRandomParticlePrecache (const char *pname);
// small function for simpler reuse
static void CL_ForceProtocolParticles (void)
{
	cl.protocol_particles = true;
	PScript_FindParticleType ("effectinfo."); // make sure this is implicitly loaded.
	COM_Effectinfo_Enumerate (CL_GenerateRandomParticlePrecache);
	Con_Warning ("Received svcdp_pointparticles1 but extension not active");
}

/*
CL_RegisterParticles
called when the particle system has changed, and any cached indexes are now probably stale.
*/
void CL_RegisterParticles (void)
{
	extern qmodel_t mod_known[];
	extern int		mod_numknown;
	int				i;

	// make sure the precaches know the right effects
	for (i = 0; i < MAX_PARTICLETYPES; i++)
	{
		if (cl.particle_precache[i].name)
			cl.particle_precache[i].index = PScript_FindParticleType (cl.particle_precache[i].name);
		else
			cl.particle_precache[i].index = -1;
	}

	// and make sure models get the right effects+trails etc too
	for (i = 0; i < mod_numknown; i++)
		PScript_UpdateModelEffects (&mod_known[i]);
}

/*
CL_ParseParticles

spike -- this handles the various ssqc builtins (the ones that were based on csqc)
*/
static void CL_ParseParticles (int type)
{
	vec3_t org, vel;
	if (type < 0)
	{ // trail
		entity_t *ent;
		int		  entity = MSG_ReadShort ();
		int		  efnum = MSG_ReadShort ();
		org[0] = MSG_ReadCoord (cl.protocolflags);
		org[1] = MSG_ReadCoord (cl.protocolflags);
		org[2] = MSG_ReadCoord (cl.protocolflags);
		vel[0] = MSG_ReadCoord (cl.protocolflags);
		vel[1] = MSG_ReadCoord (cl.protocolflags);
		vel[2] = MSG_ReadCoord (cl.protocolflags);

		ent = CL_EntityNum (entity);

		if (efnum < MAX_PARTICLETYPES && cl.particle_precache[efnum].name)
			PScript_ParticleTrail (org, vel, cl.particle_precache[efnum].index, 1, 0, NULL, &ent->trailstate);
	}
	else
	{ // point
		int efnum = MSG_ReadShort ();
		int count;
		org[0] = MSG_ReadCoord (cl.protocolflags);
		org[1] = MSG_ReadCoord (cl.protocolflags);
		org[2] = MSG_ReadCoord (cl.protocolflags);
		if (type)
		{
			vel[0] = vel[1] = vel[2] = 0;
			count = 1;
		}
		else
		{
			vel[0] = MSG_ReadCoord (cl.protocolflags);
			vel[1] = MSG_ReadCoord (cl.protocolflags);
			vel[2] = MSG_ReadCoord (cl.protocolflags);
			count = MSG_ReadShort ();
		}
		if (efnum < MAX_PARTICLETYPES && cl.particle_precache[efnum].name)
		{
			PScript_RunParticleEffectState (org, vel, count, cl.particle_precache[efnum].index, NULL);
		}
	}
}
#endif

#define SHOWNET(x)             \
	if (cl_shownet.value == 2) \
		Con_Printf ("%3i:%s\n", msg_readcount - 1, x);

static void CL_ParseStatNumeric (int stat, int ival, float fval)
{
	if (stat < 0 || stat >= MAX_CL_STATS)
	{
		Con_DWarning ("svc_updatestat: %i is invalid\n", stat);
		return;
	}
	cl.stats[stat] = ival;
	cl.statsf[stat] = fval;
	if (stat == STAT_VIEWZOOM)
		vid.recalc_refdef = true;
}
static void CL_ParseStatFloat (int stat, float fval)
{
	CL_ParseStatNumeric (stat, fval, fval);
}
static void CL_ParseStatInt (int stat, int ival)
{
	CL_ParseStatNumeric (stat, ival, ival);
}
static void CL_ParseStatString (int stat, const char *str)
{
	if (stat < 0 || stat >= MAX_CL_STATS)
	{
		Con_DWarning ("svc_updatestat: %i is invalid\n", stat);
		return;
	}
	Mem_Free (cl.statss[stat]);
	cl.statss[stat] = q_strdup (str);
	// hud doesn't know/care about any of these strings so don't bother invalidating anything.
}

/*
=====================
CL_ParseServerMessage
=====================
*/
void CL_ParseServerMessage (void)
{
	int			cmd;
	int			i;
	const char *str;			   // johnfitz
	int			total, j, lastcmd; // johnfitz

	//
	// if recording demos, copy the message out
	//
	if (cl_shownet.value == 1)
		Con_Printf ("%i ", net_message.cursize);
	else if (cl_shownet.value == 2)
		Con_Printf ("------------------\n");

	//
	// parse the message
	//
	MSG_BeginReading ();

	lastcmd = 0;
	while (1)
	{
		if (msg_badread)
			Host_Error ("CL_ParseServerMessage: Bad server message");

		cmd = MSG_ReadByte ();

		if (cmd == -1)
		{
			SHOWNET ("END OF MESSAGE");

			if (cl.items != cl.stats[STAT_ITEMS])
			{
				for (i = 0; i < 32; i++)
					if (((uint32_t)cl.stats[STAT_ITEMS] & (1u << i)) && !((uint32_t)cl.items & (1u << i)))
						cl.item_gettime[i] = cl.time;
				cl.items = cl.stats[STAT_ITEMS];
			}
			return; // end of message
		}

		// if the high bit of the command byte is set, it is a fast update
		if (cmd & U_SIGNAL) // johnfitz -- was 128, changed for clarity
		{
			// for netquake demos, just parse the last 10 seconds to keep effects in order
			if (cls.demoseeking && cls.seektime > cl.mtime[0] + 10)
				return;
			SHOWNET ("fast update");
			CL_ParseUpdate (cmd & 127);
			continue;
		}

		if (cmd < (int)NUM_SVC_STRINGS)
		{
			SHOWNET (svc_strings[cmd]);
		}

		// other commands
		switch (cmd)
		{
		default:
			Host_Error ("Illegible server message %d, previous was %s", cmd, svc_strings[lastcmd]); // johnfitz -- added svc_strings[lastcmd]
			break;

		case svc_nop:
			//	Con_Printf ("svc_nop\n");
			break;

		case svc_time:
			cl.mtime[1] = cl.mtime[0];
			cl.mtime[0] = MSG_ReadFloat ();
			if (cl.protocol_pext2 & PEXT2_PREDINFO)
				MSG_ReadShort (); // input sequence ack.
			break;

		case svc_clientdata:
			CL_ParseClientdata (); // johnfitz -- removed bits parameter, we will read this inside CL_ParseClientdata()
			break;

		case svc_version:
			i = MSG_ReadLong ();
			// johnfitz -- support multiple protocols
			if (i != PROTOCOL_NETQUAKE && i != PROTOCOL_FITZQUAKE && i != PROTOCOL_RMQ)
				Host_Error ("Server returned version %i, not %i or %i or %i", i, PROTOCOL_NETQUAKE, PROTOCOL_FITZQUAKE, PROTOCOL_RMQ);
			cl.protocol = i;
			// johnfitz
			break;

		case svc_disconnect:
			Host_EndGame ("Server disconnected\n");

		case svc_print:
			str = MSG_ReadString ();
			if (!cls.demoseeking)
				Con_Printf ("%s", str);
			break;

		case svc_centerprint:
			// johnfitz -- log centerprints to console
			str = MSG_ReadString ();
			SCR_CenterPrint (str);
			Con_LogCenterPrint (str);
			// johnfitz
			break;

		case svc_stufftext:
			str = MSG_ReadString ();
			// handle special commands
			if (strlen (str) > 2 && str[0] == '/' && str[1] == '/')
			{
				if (!Cmd_ExecuteString (str + 2, src_server))
					Con_DPrintf ("Server sent unknown command %s\n", Cmd_Argv (0));
			}
			else
				Cbuf_AddText (str);
			break;

		case svc_damage:
			V_ParseDamage ();
			break;

		case svc_serverinfo:
			CL_ParseServerInfo ();
			vid.recalc_refdef = true; // leave intermission full screen
			break;

		case svc_setangle:
			for (i = 0; i < 3; i++)
				cl.viewangles[i] = MSG_ReadAngle (cl.protocolflags);
			cl.fixangle_time = cl.mtime[0];
			break;
		case svcfte_setangledelta:
			for (i = 0; i < 3; i++)
				cl.viewangles[i] += MSG_ReadAngle16 (cl.protocolflags);
			break;

		case svc_setview:
			cl.viewentity = MSG_ReadShort ();
			break;

		case svc_lightstyle:
			i = MSG_ReadByte ();
			if (i >= MAX_LIGHTSTYLES)
				Sys_Error ("svc_lightstyle > MAX_LIGHTSTYLES");
			q_strlcpy (cl_lightstyle[i].map, MSG_ReadString (), MAX_STYLESTRING);
			cl_lightstyle[i].length = strlen (cl_lightstyle[i].map);
			// johnfitz -- save extra info
			if (cl_lightstyle[i].length)
			{
				total = 0;
				cl_lightstyle[i].peak = 'a';
				for (j = 0; j < cl_lightstyle[i].length; j++)
				{
					total += cl_lightstyle[i].map[j] - 'a';
					cl_lightstyle[i].peak = q_max (cl_lightstyle[i].peak, cl_lightstyle[i].map[j]);
				}
				cl_lightstyle[i].average = total / cl_lightstyle[i].length + 'a';
			}
			else
				cl_lightstyle[i].average = cl_lightstyle[i].peak = 'm';
			// johnfitz
			break;

		case svc_sound:
			CL_ParseStartSoundPacket ();
			break;

		case svc_stopsound:
			i = MSG_ReadShort ();
			S_StopSound (i >> 3, i & 7);
			break;

		case svc_updatename:
			i = MSG_ReadByte ();
			if (i >= cl.maxclients)
				Host_Error ("CL_ParseServerMessage: svc_updatename > MAX_SCOREBOARD");
			q_strlcpy (cl.scores[i].name, MSG_ReadString (), MAX_SCOREBOARDNAME);
			Info_SetKey (cl.scores[i].userinfo, sizeof (cl.scores[i].userinfo), "name", cl.scores[i].name);
			break;

		case svc_updatefrags:
			i = MSG_ReadByte ();
			if (i >= cl.maxclients)
				Host_Error ("CL_ParseServerMessage: svc_updatefrags > MAX_SCOREBOARD");
			cl.scores[i].frags = MSG_ReadShort ();
			break;

		case svc_updatecolors:
			i = MSG_ReadByte ();
			if (i >= cl.maxclients)
				Host_Error ("CL_ParseServerMessage: svc_updatecolors > MAX_SCOREBOARD");
			cl.scores[i].colors = MSG_ReadByte ();
			CL_NewTranslation (i);
			Info_SetKey (cl.scores[i].userinfo, sizeof (cl.scores[i].userinfo), "topcolor", va ("%d", cl.scores[i].colors >> 4));
			Info_SetKey (cl.scores[i].userinfo, sizeof (cl.scores[i].userinfo), "bottomcolor", va ("%d", cl.scores[i].colors & 0xf));
			break;

		case svc_particle:
			R_ParseParticleEffect ();
			break;

		case svc_spawnbaseline:
			i = MSG_ReadShort ();
			// must use CL_EntityNum() to force cl.num_entities up
			CL_ParseBaseline (CL_EntityNum (i), 1); // johnfitz -- added second parameter
			break;

		case svc_spawnstatic:
			CL_ParseStatic (1); // johnfitz -- added parameter
			break;

		case svc_temp_entity:
			CL_ParseTEnt ();
			break;

		case svc_setpause:
			cl.paused = MSG_ReadByte ();
			if (cl.paused)
			{
				CDAudio_Pause ();
				BGM_Pause ();
			}
			else
			{
				CDAudio_Resume ();
				BGM_Resume ();
			}
			break;

		case svc_signonnum:
			i = MSG_ReadByte ();
			if (i <= cls.signon)
				Host_Error ("Received signon %i when at %i", i, cls.signon);
			cls.signon = i;
			// johnfitz -- if signonnum==2, signon packet has been fully parsed, so check for excessive static ents and efrags
			if (i == 2)
			{
				if (cl.num_statics > 128)
					Con_DWarning ("%i static entities exceeds standard limit of 128.\n", cl.num_statics);
				R_CheckEfrags ();
			}
			// johnfitz
			CL_SignonReply ();
			break;

		case svc_killedmonster:
			cl.stats[STAT_MONSTERS]++;
			cl.statsf[STAT_MONSTERS] = cl.stats[STAT_MONSTERS];
			break;

		case svc_foundsecret:
			cl.stats[STAT_SECRETS]++;
			cl.statsf[STAT_SECRETS] = cl.stats[STAT_SECRETS];
			break;

		case svc_updatestat:
			i = MSG_ReadByte ();
			CL_ParseStatInt (i, MSG_ReadLong ());
			break;

		case svc_spawnstaticsound:
			CL_ParseStaticSound (1); // johnfitz -- added parameter
			break;

		case svc_cdtrack:
			cl.cdtrack = MSG_ReadByte ();
			cl.looptrack = MSG_ReadByte ();
			if ((cls.demoplayback || cls.demorecording) && (cls.forcetrack != -1))
				BGM_PlayCDtrack ((byte)cls.forcetrack, true);
			else
				BGM_PlayCDtrack ((byte)cl.cdtrack, true);
			break;

		case svc_intermission:
			cl.intermission = 1;
			cl.completed_time = cl.time;
			vid.recalc_refdef = true; // go to full screen
			V_RestoreAngles ();
			break;

		case svc_finale:
			cl.intermission = 2;
			cl.completed_time = cl.time;
			vid.recalc_refdef = true; // go to full screen
			// johnfitz -- log centerprints to console
			str = MSG_ReadString ();
			SCR_CenterPrint (str);
			Con_LogCenterPrint (str);
			// johnfitz
			V_RestoreAngles ();
			break;

		case svc_cutscene:
			cl.intermission = 3;
			cl.completed_time = cl.time;
			vid.recalc_refdef = true; // go to full screen
			// johnfitz -- log centerprints to console
			str = MSG_ReadString ();
			SCR_CenterPrint (str);
			Con_LogCenterPrint (str);
			// johnfitz
			V_RestoreAngles ();
			break;

		case svc_sellscreen:
			Cmd_ExecuteString ("help", src_command);
			break;

		// johnfitz -- new svc types
		case svc_skybox:
			Sky_LoadSkyBox (MSG_ReadString ());
			break;

		case svc_bf:
			Cmd_ExecuteString ("bf", src_command);
			break;

		case svc_fog:
			Fog_ParseServerMessage ();
			break;

		case svc_spawnbaseline2: // PROTOCOL_FITZQUAKE
			i = MSG_ReadShort ();
			// must use CL_EntityNum() to force cl.num_entities up
			CL_ParseBaseline (CL_EntityNum (i), 2);
			break;

		case svc_spawnstatic2: // PROTOCOL_FITZQUAKE
			CL_ParseStatic (2);
			break;

		case svc_spawnstaticsound2: // PROTOCOL_FITZQUAKE
			CL_ParseStaticSound (2);
			break;
		// johnfitz

		// used by the 2021 rerelease
		case svc_achievement:
			str = MSG_ReadString ();
			Con_DPrintf ("Ignoring svc_achievement (%s)\n", str);
			break;
		case svc_localsound:
			CL_ParseLocalSound ();
			break;
#ifdef PSET_SCRIPT
		case svcdp_trailparticles:
			if (!cl.protocol_particles)
				CL_ForceProtocolParticles ();
			CL_ParseParticles (-1);
			break;
		case svcdp_pointparticles:
			if (!cl.protocol_particles)
				CL_ForceProtocolParticles ();
			CL_ParseParticles (0);
			break;
		case svcdp_pointparticles1:
			if (!cl.protocol_particles)
				CL_ForceProtocolParticles ();
			CL_ParseParticles (1);
			break;
#endif

		// spike -- for particles more than anything else
		case svcdp_precache:
			if (!cl.protocol_pext2)
				Host_Error ("Received svcdp_precache but extension not active");
			CL_ParsePrecache ();
			break;
		// spike -- new deltas (including new fields etc)
		// stats also changed, and are sent unreliably using the same ack mechanism (which means they're not blocked until the reliables are acked,
		// preventing the need to spam them in every packet).
		case svcdp_updatestatbyte:
			if (!(cl.protocol_pext2 & PEXT2_REPLACEMENTDELTAS))
				Host_Error ("Received svcdp_updatestatbyte but extension not active");
			i = MSG_ReadByte ();
			CL_ParseStatInt (i, MSG_ReadByte ());
			break;
		case svcfte_updatestatstring:
			if (!(cl.protocol_pext2 & PEXT2_REPLACEMENTDELTAS))
				Host_Error ("Received svcfte_updatestatstring but extension not active");
			i = MSG_ReadByte ();
			CL_ParseStatString (i, MSG_ReadString ());
			break;
		case svcfte_updatestatfloat:
			if (!(cl.protocol_pext2 & PEXT2_REPLACEMENTDELTAS))
				Host_Error ("Received svcfte_updatestatfloat but extension not active");
			i = MSG_ReadByte ();
			CL_ParseStatFloat (i, MSG_ReadFloat ());
			break;
		// static ents get all the new fields too, even if the client will probably ignore most of them, the option is at least there to fix it without
		// updating protocols separately.
		case svcfte_spawnstatic2:
			if (!(cl.protocol_pext2 & PEXT2_REPLACEMENTDELTAS))
				Host_Error ("Received svcfte_spawnstatic2 but extension not active");
			CL_ParseStatic (6);
			break;
		// baselines have all fields. hurrah for the same delta mechanism
		case svcfte_spawnbaseline2:
			if (!(cl.protocol_pext2 & PEXT2_REPLACEMENTDELTAS))
				Host_Error ("Received svcfte_spawnbaseline2 but extension not active");
			i = MSG_ReadEntity (cl.protocol_pext2);
			// must use CL_EntityNum() to force cl.num_entities up
			CL_ParseBaseline (CL_EntityNum (i), 6);
			break;
		// ent updates replace svc_time too
		case svcfte_updateentities:
			if (!(cl.protocol_pext2 & PEXT2_REPLACEMENTDELTAS))
				Host_Error ("Received svcfte_updateentities but extension not active");
			CLFTE_ParseEntitiesUpdate ();
			break;

		case svcfte_cgamepacket:
			if (!(cl.protocol_pext1 & PEXT1_CSQC))
				Host_Error ("Received svcfte_cgamepacket but extension not active");
			if (cl.qcvm.extfuncs.CSQC_Parse_Event)
			{
				PR_SwitchQCVM (&cl.qcvm);
				PR_ExecuteProgram (cl.qcvm.extfuncs.CSQC_Parse_Event);
				PR_SwitchQCVM (NULL);
			}
			else
				Host_Error ("CSQC_Parse_Event: Missing or incompatible CSQC\n");
			break;

		case svcfte_voicechat:
			if (!(cl.protocol_pext2 & PEXT2_VOICECHAT))
				Host_Error ("Received svcfte_voicechat but extension not active");
			/*sender =*/MSG_ReadByte ();
			/*gen =*/MSG_ReadByte ();
			/*seq =*/MSG_ReadByte ();
			int bytes = MSG_ReadShort ();
			while (bytes-- > 0)
				MSG_ReadByte ();
			break;
		}

		lastcmd = cmd; // johnfitz
	}
}
