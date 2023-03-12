/*
Copyright (C) 1996-2001 Id Software, Inc.
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
#include "bgmusic.h"

static void CL_FinishTimeDemo (void);

char name[MAX_OSPATH];

/*
==============================================================================

DEMO CODE

When a demo is playing back, all NET_SendMessages are skipped, and
NET_GetMessages are read from the demo file.

Whenever cl.time gets past the last received message, another message is
read from the demo file.
==============================================================================
*/

/*
==============
CL_StopPlayback

Called when a demo file runs out, or the user starts a game
==============
*/
void CL_StopPlayback (void)
{
	if (!cls.demoplayback)
		return;

	fclose (cls.demofile);
	cls.demoplayback = false;
	cls.demoseeking = false;
	cls.demopaused = false;
	cls.demofile = NULL;
	cls.state = ca_disconnected;
	cls.demo_prespawn_end = 0;

	if (cls.timedemo)
		CL_FinishTimeDemo ();
}

/*
====================
CL_WriteDemoMessage

Dumps the current net message, prefixed by the length and view angles
====================
*/
static void CL_WriteDemoMessage (void)
{
	int	  len;
	int	  i;
	float f;

	len = LittleLong (net_message.cursize);
	fwrite (&len, 4, 1, cls.demofile);
	for (i = 0; i < 3; i++)
	{
		f = LittleFloat (cl.viewangles[i]);
		fwrite (&f, 4, 1, cls.demofile);
	}
	fwrite (net_message.data, net_message.cursize, 1, cls.demofile);
	fflush (cls.demofile);
}

static int CL_GetDemoMessage (void)
{
	int	  r, i;
	float f;

	if (cls.demopaused)
		return 0;

	if (cls.signon == (SIGNONS - 2))
		cls.demo_prespawn_end = ftell (cls.demofile);
	// decide if it is time to grab the next message
	else if (cls.signon == SIGNONS) // always grab until fully connected
	{
		if (cls.timedemo)
		{
			if (host_framecount == cls.td_lastframe)
				return 0; // already read this frame's message
			cls.td_lastframe = host_framecount;
			// if this is the second frame, grab the real td_starttime
			// so the bogus time on the first frame doesn't count
			if (host_framecount == cls.td_startframe + 1)
				cls.td_starttime = realtime;
		}
		else if (cls.demoseeking)
		{
			// feed a reasonable cl.time value for effects / centerprints
			cl.time = cl.mtime[0];
			if (cl.mtime[0] > cls.seektime)
			{
				cls.demoseeking = false;
				return 0;
			}
		}
		else if (/* cl.time > 0 && */ cl.time <= cl.mtime[0])
		{
			return 0; // don't need another message yet
		}
	}
	else if (cls.signon < (SIGNONS - 2))
		cls.demo_prespawn_end = 0;

	// get the next message
	if (fread (&net_message.cursize, 4, 1, cls.demofile) != 1)
	{
		CL_StopPlayback ();
		return 0;
	}
	VectorCopy (cl.mviewangles[0], cl.mviewangles[1]);
	for (i = 0; i < 3; i++)
	{
		if (fread (&f, 4, 1, cls.demofile) != 1)
		{
			CL_StopPlayback ();
			return 0;
		}
		cl.mviewangles[0][i] = LittleFloat (f);
	}

	net_message.cursize = LittleLong (net_message.cursize);
	if (net_message.cursize > MAX_MSGLEN)
		Sys_Error ("Demo message > MAX_MSGLEN");
	r = fread (net_message.data, net_message.cursize, 1, cls.demofile);
	if (r != 1)
	{
		CL_StopPlayback ();
		return 0;
	}

	return 1;
}

/*
====================
CL_Seek_f
====================
*/
extern float scr_clock_off;
void		 CL_Seek_f (void)
{
	if (cmd_source != src_command)
		return;

	if (Cmd_Argc () != 2)
	{
		Con_Printf ("seek [+/-]<offset> : [relative] seek in demo\n");
		return;
	}

	if (!cls.demoplayback)
	{
		Con_Printf ("Not playing a demo.\n");
		return;
	}

	cls.demopaused = cl.paused = false;
	if (cls.demospeed == 0.f)
		cls.demospeed = 1.f;

	float offset = 0, offset_seconds;
	int	  ret;
	if ((ret = sscanf (Cmd_Argv (1), "%f:%f", &offset, &offset_seconds)) == 2)
		offset = offset * 60 + (offset > 0 ? offset_seconds : -offset_seconds);

	if (!ret)
	{
		Con_Printf ("Expected time format is seconds or mm:ss with optional +/- prefix.\n");
		Con_Printf ("Examples:  12:34  +20  -3:15\n");
		return;
	}

	qboolean relative = offset < 0 || Cmd_Argv (1)[0] == '+';
	cls.seektime = relative ? cl.time + offset : offset;

	// large positive offsets could benefit from demoseeking, but we'd lose prints etc
	if ((offset < 0 || (!relative && offset < cl.time)) && cls.demo_prespawn_end)
	{
		fseek (cls.demofile, cls.demo_prespawn_end, SEEK_SET);
		cl.mtime[0] = cl.time = 0;
		cls.demoseeking = true;

		memset (cl_dlights, 0, sizeof (cl_dlights));
		memset (cl_temp_entities, 0, sizeof (cl_temp_entities));
		memset (cl_beams, 0, sizeof (cl_beams));
		V_ResetBlend ();
		Fog_NewMap ();
		Sky_NewMap ();
		R_ClearParticles ();
#ifdef PSET_SCRIPT
		PScript_ClearParticles (false);
#endif
		SCR_CenterPrintClear ();
		if (cl.intermission)
		{
			cl.intermission = 0;
			BGM_Stop ();
		}
		memset (cl.stats, 0, sizeof (cl.stats));
		memset (cl.statsf, 0, sizeof (cl.statsf));

		// replay last signon for stats and lightstyles
		cls.signon = (SIGNONS - 2);
		S_StopAllSounds (true, true);
	}
	else
		cl.time = cls.seektime;

	scr_clock_off = 2.5f; // show clock for a few seconds after a seek
}

/*
====================
CL_GetMessage

Handles recording and playback of demos, on top of NET_ code
====================
*/
int CL_GetMessage (void)
{
	int r;

	if (cls.demoplayback)
		return CL_GetDemoMessage ();

	while (1)
	{
		r = NET_GetMessage (cls.netcon);

		if (r != 1 && r != 2)
			return r;

		// discard nop keepalive message
		if (net_message.cursize == 1 && net_message.data[0] == svc_nop)
			Con_Printf ("<-- server to client keepalive\n");
		else
			break;
	}

	if (cls.demorecording)
		CL_WriteDemoMessage ();

	return r;
}

/*
====================
CL_Stop_f

stop recording a demo
====================
*/
void CL_Stop_f (void)
{
	if (cmd_source != src_command)
		return;

	if (!cls.demorecording)
	{
		Con_Printf ("Not recording a demo.\n");
		return;
	}

	// write a disconnect message to the demo file
	SZ_Clear (&net_message);
	MSG_WriteByte (&net_message, svc_disconnect);
	CL_WriteDemoMessage ();

	// finish up
	fclose (cls.demofile);
	cls.demofile = NULL;
	cls.demorecording = false;
	Con_Printf ("Completed demo\n");

	// ericw -- update demo tab-completion list
	DemoList_Rebuild ();
}

static void CL_Record_Serverdata (void)
{
	size_t i;
	MSG_WriteByte (&net_message, svc_serverinfo);
	if (cl.protocol_pext2)
	{
		MSG_WriteLong (&net_message, PROTOCOL_FTE_PEXT2);
		MSG_WriteLong (&net_message, cl.protocol_pext2);
	}
	MSG_WriteLong (&net_message, cl.protocol);
	if (cl.protocol == PROTOCOL_RMQ)
		MSG_WriteLong (&net_message, cl.protocolflags);
	if (cl.protocol_pext2 & PEXT2_PREDINFO)
		MSG_WriteString (&net_message, COM_SkipPath (com_gamedir));
	MSG_WriteByte (&net_message, cl.maxclients);
	MSG_WriteByte (&net_message, cl.gametype);
	MSG_WriteString (&net_message, cl.levelname);
	for (i = 1; cl.model_precache[i]; i++)
		MSG_WriteString (&net_message, cl.model_precache[i]->name);
	MSG_WriteByte (&net_message, 0);
	for (i = 1; cl.sound_precache[i]; i++) // FIXME: might not send any if nosound is set
		MSG_WriteString (&net_message, cl.sound_precache[i]->name);
	MSG_WriteByte (&net_message, 0);
	// FIXME: cd track (current rather than initial?)
	// FIXME: initial view entity (for clients that don't want to mess up scoreboards)
	MSG_WriteByte (&net_message, svc_signonnum);
	MSG_WriteByte (&net_message, 1);
	CL_WriteDemoMessage ();
	SZ_Clear (&net_message);
}

// spins out a baseline(idx>=0) or static entity(idx<0) into net_message
static void CL_Record_Prespawn (void)
{
	int idx, i;

	// baselines
	for (idx = 0; idx < cl.num_entities; idx++)
	{
		entity_state_t *state = &cl.entities[idx].baseline;
		if (!memcmp (state, &nullentitystate, sizeof (entity_state_t)))
			continue; // no need
		MSG_WriteStaticOrBaseLine (&net_message, idx, state, cl.protocol_pext2, cl.protocol, cl.protocolflags);

		if (net_message.cursize > 4096)
		{ // periodically flush so that large maps don't need larger than vanilla limits
			CL_WriteDemoMessage ();
			SZ_Clear (&net_message);
		}
	}

	// static ents
	for (idx = 1; idx < cl.num_statics; idx++)
	{
		MSG_WriteStaticOrBaseLine (&net_message, -1, &cl.static_entities[idx]->baseline, cl.protocol_pext2, cl.protocol, cl.protocolflags);

		if (net_message.cursize > 4096)
		{ // periodically flush so that large maps don't need larger than vanilla limits
			CL_WriteDemoMessage ();
			SZ_Clear (&net_message);
		}
	}

	// static sounds
	for (i = NUM_AMBIENTS; i < total_channels; i++)
	{
		channel_t  *ss = &snd_channels[i];
		sfxcache_t *sc;

		if (!ss->sfx)
			continue;
		if (ss->entnum || ss->entchannel)
			continue; // can't have been a static sound
		sc = S_LoadSound (ss->sfx);
		if (!sc || sc->loopstart == -1)
			continue; // can't have been a (valid) static sound

		for (idx = 1; idx < MAX_SOUNDS && cl.sound_precache[idx]; idx++)
			if (cl.sound_precache[idx] == ss->sfx)
				break;
		if (idx == MAX_SOUNDS)
			continue; // can't figure out which sound it was

		MSG_WriteByte (&net_message, (idx > 255) ? svc_spawnstaticsound2 : svc_spawnstaticsound);
		MSG_WriteCoord (&net_message, ss->origin[0], cl.protocolflags);
		MSG_WriteCoord (&net_message, ss->origin[1], cl.protocolflags);
		MSG_WriteCoord (&net_message, ss->origin[2], cl.protocolflags);
		if (idx > 255)
			MSG_WriteShort (&net_message, idx);
		else
			MSG_WriteByte (&net_message, idx);
		MSG_WriteByte (&net_message, ss->master_vol);
		MSG_WriteByte (&net_message, ss->dist_mult * 1000 * 64);

		if (net_message.cursize > 4096)
		{ // periodically flush so that large maps don't need larger than vanilla limits
			CL_WriteDemoMessage ();
			SZ_Clear (&net_message);
		}
	}

#ifdef PSET_SCRIPT
	// particleindexes
	for (idx = 0; idx < MAX_PARTICLETYPES; idx++)
	{
		if (!cl.particle_precache[idx].name)
			continue;
		MSG_WriteByte (&net_message, svcdp_precache);
		MSG_WriteShort (&net_message, 0x4000 | idx);
		MSG_WriteString (&net_message, cl.particle_precache[idx].name);

		if (net_message.cursize > 4096)
		{ // periodically flush so that large maps don't need larger than vanilla limits
			CL_WriteDemoMessage ();
			SZ_Clear (&net_message);
		}
	}
#endif

	MSG_WriteByte (&net_message, svc_signonnum);
	MSG_WriteByte (&net_message, 2);
	CL_WriteDemoMessage ();
	SZ_Clear (&net_message);
}

static void CL_Record_Spawn (void)
{
	int i;

	// player names, colors, and frag counts
	for (i = 0; i < cl.maxclients; i++)
	{
		MSG_WriteByte (&net_message, svc_updatename);
		MSG_WriteByte (&net_message, i);
		MSG_WriteString (&net_message, cl.scores[i].name);
		MSG_WriteByte (&net_message, svc_updatefrags);
		MSG_WriteByte (&net_message, i);
		MSG_WriteShort (&net_message, cl.scores[i].frags);
		MSG_WriteByte (&net_message, svc_updatecolors);
		MSG_WriteByte (&net_message, i);
		MSG_WriteByte (&net_message, cl.scores[i].colors);
	}

	// send all current light styles
	for (i = 0; i < MAX_LIGHTSTYLES; i++)
	{
		if (*cl_lightstyle[i].map)
		{
			MSG_WriteByte (&net_message, svc_lightstyle);
			MSG_WriteByte (&net_message, i);
			MSG_WriteString (&net_message, cl_lightstyle[i].map);
		}

		if (net_message.cursize > 4096)
		{ // periodically flush so that large maps don't need larger than vanilla limits
			CL_WriteDemoMessage ();
			SZ_Clear (&net_message);
		}
	}

	// what about the current CD track... future consideration.

	const char *fog_cmd = Fog_GetFogCommand (false);
	if (fog_cmd)
	{
		MSG_WriteByte (&net_message, svc_stufftext);
		MSG_WriteString (&net_message, fog_cmd);
	}

	const char *sky_cmd = Sky_GetSkyCommand (false);
	if (sky_cmd)
	{
		MSG_WriteByte (&net_message, svc_stufftext);
		MSG_WriteString (&net_message, sky_cmd);
	}

	// stats
	for (i = 0; i < MAX_CL_STATS; i++)
	{
		if (!cl.stats[i] && !cl.statsf[i])
			continue;

		if (net_message.cursize > 4096)
		{ // periodically flush so that large maps don't need larger than vanilla limits
			CL_WriteDemoMessage ();
			SZ_Clear (&net_message);
		}

		if ((double)cl.stats[i] != cl.statsf[i] && (unsigned int)cl.stats[i] <= 0x00ffffff)
		{ // if the float representation seems to have more precision then use that, unless its getting huge in which case we're probably getting fpu
		  // truncation, so go back to more compatible ints
			MSG_WriteByte (&net_message, svcfte_updatestatfloat);
			MSG_WriteByte (&net_message, i);
			MSG_WriteFloat (&net_message, cl.statsf[i]);
		}
		else if (cl.stats[i] >= 0 && cl.stats[i] <= 255 && (cl.protocol_pext2 & PEXT2_PREDINFO))
		{
			MSG_WriteByte (&net_message, svcdp_updatestatbyte);
			MSG_WriteByte (&net_message, i);
			MSG_WriteByte (&net_message, cl.stats[i]);
		}
		else
		{
			MSG_WriteByte (&net_message, svc_updatestat);
			MSG_WriteByte (&net_message, i);
			MSG_WriteLong (&net_message, cl.stats[i]);
		}
	}

	// view entity
	MSG_WriteByte (&net_message, svc_setview);
	MSG_WriteShort (&net_message, cl.viewentity);

	// signon
	MSG_WriteByte (&net_message, svc_signonnum);
	MSG_WriteByte (&net_message, 3);

	CL_WriteDemoMessage ();
	SZ_Clear (&net_message);

	// ask the server to reset entity deltas. yes this means playback will wait a couple of frames before it actually starts playing but oh well.
	if (cl.protocol_pext2 & PEXT2_REPLACEMENTDELTAS)
	{
		cl.ackframes_count = 0;
		cl.ackframes[cl.ackframes_count++] = -1;
	}
}

static void CL_Record_Signons (void)
{
	byte *data = net_message.data;
	int	  cursize = net_message.cursize;
	byte  weirdaltbufferthatprobablyisntneeded[NET_MAXMESSAGE];

	net_message.data = weirdaltbufferthatprobablyisntneeded;
	SZ_Clear (&net_message);

	CL_Record_Serverdata ();
	CL_Record_Prespawn ();
	CL_Record_Spawn ();

	// restore net_message
	net_message.data = data;
	net_message.cursize = cursize;
}

/*
====================
CL_Record_f

record <demoname> <map> [cd track]
====================
*/
void CL_Record_f (void)
{
	int c;
	int track;

	if (cmd_source != src_command)
		return;

	if (cls.demoplayback)
	{
		Con_Printf ("Can't record during demo playback\n");
		return;
	}

	if (cls.demorecording)
		CL_Stop_f ();

	c = Cmd_Argc ();
	if (c != 2 && c != 3 && c != 4)
	{
		Con_Printf ("record <demoname> [<map> [cd track]]\n");
		return;
	}

	if (strstr (Cmd_Argv (1), ".."))
	{
		Con_Printf ("Relative pathnames are not allowed.\n");
		return;
	}

	if (c == 2 && cls.state == ca_connected)
	{
#if 0
		Con_Printf("Can not record - already connected to server\nClient demo recording must be started before connecting\n");
		return;
#endif
		if (cls.signon < 2)
		{
			Con_Printf ("Can't record - try again when connected\n");
			return;
		}
		switch (cl.protocol)
		{
		case PROTOCOL_NETQUAKE:
		case PROTOCOL_FITZQUAKE:
		case PROTOCOL_RMQ:
			break;
		default:
			Con_Printf ("Can not record - protocol not supported for recording mid-map\nClient demo recording must be started before connecting\n");
			return;
		}
	}

	// write the forced cd track number, or -1
	if (c == 4)
	{
		track = atoi (Cmd_Argv (3));
		Con_Printf ("Forcing CD track to %i\n", cls.forcetrack);
	}
	else
	{
		track = -1;
	}

	q_snprintf (name, sizeof (name), "%s/%s", com_gamedir, Cmd_Argv (1));

	// start the map up
	if (c > 2)
	{
		Cmd_ExecuteString (va ("map %s", Cmd_Argv (2)), src_command);
		if (cls.state != ca_connected)
			return;
	}

	// open the demo file
	COM_AddExtension (name, ".dem", sizeof (name));

	Con_Printf ("recording to %s.\n", name);
	cls.demofile = fopen (name, "wb");
	if (!cls.demofile)
	{
		Con_Printf ("ERROR: couldn't create %s\n", name);
		return;
	}

	cls.forcetrack = track;
	fprintf (cls.demofile, "%i\n", cls.forcetrack);

	cls.demorecording = true;

	// from ProQuake: initialize the demo file if we're already connected
	if (c == 2 && cls.state == ca_connected)
		CL_Record_Signons ();
}

/*
====================
CL_Resume_Record

Keep recording demo after loading a savegame
====================
*/
void CL_Resume_Record (qboolean recordsignons)
{
	cls.demofile = fopen (name, "r+b");
	if (!cls.demofile)
	{
		Con_Printf ("ERROR: couldn't append to %s - recording stopped\n", name);
		return;
	}
	// overwrite svc_disconnect
	fseek (cls.demofile, -17, SEEK_END);
	Con_Printf ("Demo recording resumed\n");
	cls.demorecording = true;
	if (recordsignons)
		CL_Record_Signons ();
}

/*
====================
CL_PlayDemo_f

play [demoname]
====================
*/
void CL_PlayDemo_f (void)
{
	if (cmd_source != src_command)
		return;

	if (Cmd_Argc () != 2)
	{
		Con_Printf ("playdemo <demoname> : plays a demo\n");
		return;
	}

	// disconnect from server
	CL_Disconnect ();

	// open the demo file
	q_strlcpy (name, Cmd_Argv (1), sizeof (name));
	COM_AddExtension (name, ".dem", sizeof (name));

	Con_Printf ("Playing demo from %s.\n", name);

	COM_FOpenFile (name, &cls.demofile, NULL);
	if (!cls.demofile)
	{
		Con_Printf ("ERROR: couldn't open %s\n", name);
		cls.demonum = -1; // stop demo loop
		return;
	}

	// ZOID, fscanf is evil
	// O.S.: if a space character e.g. 0x20 (' ') follows '\n',
	// fscanf skips that byte too and screws up further reads.
	//	fscanf (cls.demofile, "%i\n", &cls.forcetrack);
	if (fscanf (cls.demofile, "%i", &cls.forcetrack) != 1 || fgetc (cls.demofile) != '\n')
	{
		fclose (cls.demofile);
		cls.demofile = NULL;
		cls.demonum = -1; // stop demo loop
		Con_Printf ("ERROR: demo \"%s\" is invalid\n", name);
		return;
	}

	cls.demoplayback = true;
	cls.demopaused = false;
	cls.demospeed = 1.f;
	cls.state = ca_connected;

	// get rid of the menu and/or console
	key_dest = key_game;
}

/*
====================
CL_FinishTimeDemo

====================
*/
static void CL_FinishTimeDemo (void)
{
	int	  frames;
	float time;

	cls.timedemo = false;

	// the first frame didn't count
	frames = (host_framecount - cls.td_startframe) - 1;
	time = realtime - cls.td_starttime;
	if (!time)
		time = 1;
	Con_Printf ("%i frames %5.1f seconds %5.1f fps\n", frames, time, frames / time);
}

/*
====================
CL_TimeDemo_f

timedemo [demoname]
====================
*/
void CL_TimeDemo_f (void)
{
	if (cmd_source != src_command)
		return;

	if (Cmd_Argc () != 2)
	{
		Con_Printf ("timedemo <demoname> : gets demo speeds\n");
		return;
	}

	CL_PlayDemo_f ();
	if (!cls.demofile)
		return;

	// cls.td_starttime will be grabbed at the second frame of the demo, so
	// all the loading time doesn't get counted

	cls.timedemo = true;
	cls.td_startframe = host_framecount;
	cls.td_lastframe = -1; // get a new message this frame
}
