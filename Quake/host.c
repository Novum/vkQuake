/*
Copyright (C) 1996-2001 Id Software, Inc.
Copyright (C) 2002-2009 John Fitzgibbons and others
Copyright (C) 2007-2008 Kristian Duske
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
// host.c -- coordinates spawning and killing of local servers

#include "quakedef.h"
#include "bgmusic.h"
#include "tasks.h"
#include <setjmp.h>
#ifdef _DEBUG
#include "gl_heap.h"
#endif

/*

A server can allways be started, even if the system started out as a client
to a remote system.

A client can NOT be started if the system started as a dedicated server.

Memory is cleared / released when a server or client begins, not when they end.

*/

quakeparms_t *host_parms;

qboolean host_initialized; // true if into command execution

double host_frametime;
double realtime;	// without any filtering or bounding
double oldrealtime; // last frame run

int host_framecount;

int minimum_memory;

client_t *host_client; // current client

jmp_buf host_abortserver;
jmp_buf screen_error;

byte  *host_colormap;
float  host_netinterval = 1.0 / 72;
cvar_t host_framerate = {"host_framerate", "0", CVAR_NONE}; // set for slow motion
cvar_t host_speeds = {"host_speeds", "0", CVAR_NONE};		// set for running times
cvar_t host_maxfps = {"host_maxfps", "200", CVAR_ARCHIVE};	// johnfitz
cvar_t host_timescale = {"host_timescale", "0", CVAR_NONE}; // johnfitz
cvar_t max_edicts = {"max_edicts", "8192", CVAR_NONE};		// johnfitz //ericw -- changed from 2048 to 8192, removed CVAR_ARCHIVE
cvar_t cl_nocsqc = {"cl_nocsqc", "0", CVAR_NONE};			// spike -- blocks the loading of any csqc modules

cvar_t sys_ticrate = {"sys_ticrate", "0.025", CVAR_NONE}; // dedicated server
cvar_t serverprofile = {"serverprofile", "0", CVAR_NONE};

cvar_t fraglimit = {"fraglimit", "0", CVAR_NOTIFY | CVAR_SERVERINFO};
cvar_t timelimit = {"timelimit", "0", CVAR_NOTIFY | CVAR_SERVERINFO};
cvar_t teamplay = {"teamplay", "0", CVAR_NOTIFY | CVAR_SERVERINFO};
cvar_t samelevel = {"samelevel", "0", CVAR_NONE};
cvar_t noexit = {"noexit", "0", CVAR_NOTIFY | CVAR_SERVERINFO};
cvar_t skill = {"skill", "1", CVAR_NONE};			// 0 - 3
cvar_t deathmatch = {"deathmatch", "0", CVAR_NONE}; // 0, 1, or 2
cvar_t coop = {"coop", "0", CVAR_NONE};				// 0 or 1

cvar_t pausable = {"pausable", "1", CVAR_NONE};

cvar_t autoload = {"autoload", "1", CVAR_ARCHIVE};
cvar_t autofastload = {"autofastload", "0", CVAR_ARCHIVE};

cvar_t developer = {"developer", "0", CVAR_NONE};

static cvar_t pr_engine = {"pr_engine", ENGINE_NAME_AND_VER, CVAR_NONE};
cvar_t		  temp1 = {"temp1", "0", CVAR_NONE};

cvar_t devstats = {"devstats", "0", CVAR_NONE}; // johnfitz -- track developer statistics that vary every frame

cvar_t campaign = {"campaign", "0", CVAR_NONE};	  // for the 2021 rerelease
cvar_t horde = {"horde", "0", CVAR_NONE};		  // for the 2021 rerelease
cvar_t sv_cheats = {"sv_cheats", "0", CVAR_NONE}; // for the 2021 rerelease

devstats_t		dev_stats, dev_peakstats;
overflowtimes_t dev_overflows; // this stores the last time overflow messages were displayed, not the last time overflows occured

/*
================
Max_Edicts_f -- johnfitz
================
*/
static void Max_Edicts_f (cvar_t *var)
{
	// TODO: clamp it here?
	if (cls.state == ca_connected || sv.active)
		Con_Printf ("Changes to max_edicts will not take effect until the next time a map is loaded.\n");
}

/*
================
Max_Fps_f -- ericw
================
*/
static void Max_Fps_f (cvar_t *var)
{
	if (var->value > 72 || var->value <= 0)
	{
		if (!host_netinterval)
			Con_Printf ("Using renderer/network isolation.\n");
		host_netinterval = 1.0 / 72;
	}
	else
	{
		if (host_netinterval)
			Con_Printf ("Disabling renderer/network isolation.\n");
		host_netinterval = 0;

		if (var->value > 72)
			Con_Warning ("host_maxfps above 72 breaks physics.\n");
	}
}

/*
================
Host_EndGame
================
*/
void Host_EndGame (const char *message, ...)
{
	va_list argptr;
	char	string[1024];

	va_start (argptr, message);
	q_vsnprintf (string, sizeof (string), message, argptr);
	va_end (argptr);
	Con_DPrintf ("Host_EndGame: %s\n", string);

	PR_SwitchQCVM (NULL);

	if (sv.active)
		Host_ShutdownServer (false);

	if (cls.state == ca_dedicated)
		Sys_Error ("Host_EndGame: %s\n", string); // dedicated servers exit

	if (cls.demonum != -1 && !cls.timedemo)
		CL_NextDemo ();
	else
		CL_Disconnect ();

	longjmp (host_abortserver, 1);
}

/*
================
Host_Error

This shuts down both the client and server
================
*/
void Host_Error (const char *error, ...)
{
	va_list			argptr;
	char			string[1024];
	static qboolean inerror = false;

	if (inerror)
		Sys_Error ("Host_Error: recursively entered");
	inerror = true;

	PR_SwitchQCVM (NULL);

	SCR_EndLoadingPlaque (); // reenable screen updates

	va_start (argptr, error);
	q_vsnprintf (string, sizeof (string), error, argptr);
	va_end (argptr);
	Con_Printf ("Host_Error: %s\n", string);

	if (cl.qcvm.extfuncs.CSQC_DrawHud && in_update_screen)
	{
		inerror = false;
		longjmp (screen_error, 1);
	}

	if (sv.active)
		Host_ShutdownServer (false);

	if (cls.state == ca_dedicated)
		Sys_Error ("Host_Error: %s\n", string); // dedicated servers exit

	CL_Disconnect ();
	cls.demonum = -1;
	cl.intermission = 0; // johnfitz -- for errors during intermissions (changelevel with no map found, etc.)

	inerror = false;

	longjmp (host_abortserver, 1);
}

/*
================
Host_FindMaxClients
================
*/
void Host_FindMaxClients (void)
{
	int i;

	svs.maxclients = 1;

	i = COM_CheckParm ("-dedicated");
	if (i)
	{
		cls.state = ca_dedicated;
		if (i != (com_argc - 1))
		{
			svs.maxclients = atoi (com_argv[i + 1]);
		}
		else
			svs.maxclients = 8;
	}
	else
		cls.state = ca_disconnected;

	i = COM_CheckParm ("-listen");
	if (i)
	{
		if (cls.state == ca_dedicated)
			Sys_Error ("Only one of -dedicated or -listen can be specified");
		if (i != (com_argc - 1))
			svs.maxclients = atoi (com_argv[i + 1]);
		else
			svs.maxclients = 8;
	}
	if (svs.maxclients < 1)
		svs.maxclients = 8;
	else if (svs.maxclients > MAX_SCOREBOARD)
		svs.maxclients = MAX_SCOREBOARD;

	svs.maxclientslimit = MAX_SCOREBOARD;
	svs.clients = (struct client_s *)Mem_Alloc (svs.maxclientslimit * sizeof (client_t));

	if (svs.maxclients > 1)
		Cvar_SetQuick (&deathmatch, "1");
	else
		Cvar_SetQuick (&deathmatch, "0");
}

void Host_Version_f (void)
{
	Con_Printf ("Quake Version %1.2f\n", VERSION);
	Con_Printf ("QuakeSpasm Version " QUAKESPASM_VER_STRING "\n");
	Con_Printf ("vkQuake Version " VKQUAKE_VER_STRING "\n");
	Con_Printf ("Exe: "__TIME__
				" "__DATE__
				"\n");
}

/* cvar callback functions : */
void Host_Callback_Notify (cvar_t *var)
{
	if (sv.active)
		SV_BroadcastPrintf ("\"%s\" changed to \"%s\"\n", var->name, var->string);
}

/*
=======================
Host_InitLocal
======================
*/
void Host_InitLocal (void)
{
	Cmd_AddCommand ("version", Host_Version_f);

	Host_InitCommands ();

	Cvar_RegisterVariable (&pr_engine);
	Cvar_RegisterVariable (&host_framerate);
	Cvar_RegisterVariable (&host_speeds);
	Cvar_RegisterVariable (&host_maxfps); // johnfitz
	Cvar_SetCallback (&host_maxfps, Max_Fps_f);
	Cvar_RegisterVariable (&host_timescale); // johnfitz

	Cvar_RegisterVariable (&cl_nocsqc);	 // spike
	Cvar_RegisterVariable (&max_edicts); // johnfitz
	Cvar_SetCallback (&max_edicts, Max_Edicts_f);
	Cvar_RegisterVariable (&devstats); // johnfitz

	Cvar_RegisterVariable (&sys_ticrate);
	Cvar_RegisterVariable (&serverprofile);

	Cvar_RegisterVariable (&fraglimit);
	Cvar_RegisterVariable (&timelimit);
	Cvar_RegisterVariable (&teamplay);
	Cvar_SetCallback (&fraglimit, Host_Callback_Notify);
	Cvar_SetCallback (&timelimit, Host_Callback_Notify);
	Cvar_SetCallback (&teamplay, Host_Callback_Notify);
	Cvar_RegisterVariable (&samelevel);
	Cvar_RegisterVariable (&noexit);
	Cvar_SetCallback (&noexit, Host_Callback_Notify);
	Cvar_RegisterVariable (&skill);
	Cvar_RegisterVariable (&developer);
	Cvar_RegisterVariable (&coop);
	Cvar_RegisterVariable (&deathmatch);

	Cvar_RegisterVariable (&campaign);
	Cvar_RegisterVariable (&horde);
	Cvar_RegisterVariable (&sv_cheats);

	Cvar_RegisterVariable (&pausable);

	Cvar_RegisterVariable (&autoload);
	Cvar_RegisterVariable (&autofastload);

	Cvar_RegisterVariable (&temp1);

	Host_FindMaxClients ();
}

/*
===============
Host_WriteConfiguration

Writes key bindings and archived cvars to config.cfg
===============
*/
void Host_WriteConfiguration (void)
{
	FILE *f = NULL;

	// dedicated servers initialize the host but don't parse and set the
	// config.cfg cvars
	if (host_initialized && !isDedicated && !host_parms->errstate)
	{
		if (multiuser)
		{
			char *pref_path = SDL_GetPrefPath ("", "vkQuake");
			f = fopen (va ("%s/config.cfg", pref_path), "w");
			SDL_free (pref_path);
		}
		else
			f = fopen (va ("%s/" CONFIG_NAME, com_gamedir), "w");
		if (!f)
		{
			Con_Printf ("Couldn't write " CONFIG_NAME ".\n");
			return;
		}

		// VID_SyncCvars (); //johnfitz -- write actual current mode to config file, in case cvars were messed with

		Key_WriteBindings (f);
		Cvar_WriteVariables (f);

		// johnfitz -- extra commands to preserve state
		fprintf (f, "vid_restart\n");
		fprintf (f, "+mlook\n"); // always enable mouse look on config, can be overriden by -mlook in autoexec.cfg
		// johnfitz

		fclose (f);
	}
}

/*
=================
SV_ClientPrintf

Sends text across to be displayed
FIXME: make this just a stuffed echo?
=================
*/
void SV_ClientPrintf (const char *fmt, ...)
{
	va_list argptr;
	char	string[1024];

	va_start (argptr, fmt);
	q_vsnprintf (string, sizeof (string), fmt, argptr);
	va_end (argptr);

	MSG_WriteByte (&host_client->message, svc_print);
	MSG_WriteString (&host_client->message, string);
}

/*
=================
SV_BroadcastPrintf

Sends text to all active clients
=================
*/
void SV_BroadcastPrintf (const char *fmt, ...)
{
	va_list argptr;
	char	string[1024];
	int		i;

	va_start (argptr, fmt);
	q_vsnprintf (string, sizeof (string), fmt, argptr);
	va_end (argptr);

	for (i = 0; i < svs.maxclients; i++)
	{
		if (svs.clients[i].active && svs.clients[i].spawned)
		{
			MSG_WriteByte (&svs.clients[i].message, svc_print);
			MSG_WriteString (&svs.clients[i].message, string);
		}
	}
}

/*
=================
Host_ClientCommands

Send text over to the client to be executed
=================
*/
void Host_ClientCommands (const char *fmt, ...)
{
	va_list argptr;
	char	string[1024];

	va_start (argptr, fmt);
	q_vsnprintf (string, sizeof (string), fmt, argptr);
	va_end (argptr);

	MSG_WriteByte (&host_client->message, svc_stufftext);
	MSG_WriteString (&host_client->message, string);
}

/*
=====================
SV_DropClient

Called when the player is getting totally kicked off the host
if (crash = true), don't bother sending signofs
=====================
*/
void SV_DropClient (qboolean crash)
{
	int		  saveSelf;
	int		  i;
	client_t *client;

	if (!crash)
	{
		// send any final messages (don't check for errors)
		if (NET_CanSendMessage (host_client->netconnection))
		{
			MSG_WriteByte (&host_client->message, svc_disconnect);
			NET_SendMessage (host_client->netconnection, &host_client->message);
		}

		if (host_client->edict && host_client->spawned)
		{
			// call the prog function for removing a client
			// this will set the body to a dead frame, among other things
			qcvm_t *oldvm = qcvm;
			PR_SwitchQCVM (NULL);
			PR_SwitchQCVM (&sv.qcvm);
			saveSelf = pr_global_struct->self;
			pr_global_struct->self = EDICT_TO_PROG (host_client->edict);
			PR_ExecuteProgram (pr_global_struct->ClientDisconnect);
			pr_global_struct->self = saveSelf;
			PR_SwitchQCVM (NULL);
			PR_SwitchQCVM (oldvm);
		}

		Sys_Printf ("Client %s removed\n", host_client->name);
	}

	// break the net connection
	NET_Close (host_client->netconnection);
	host_client->netconnection = NULL;

	SVFTE_DestroyFrames (host_client); // release any delta state

	// free the client (the body stays around)
	host_client->active = false;
	host_client->name[0] = 0;
	host_client->old_frags = -999999;
	net_activeconnections--;

	// send notification to all clients
	for (i = 0, client = svs.clients; i < svs.maxclients; i++, client++)
	{
		if (!client->knowntoqc)
			continue;

		MSG_WriteByte (&client->message, svc_updatename);
		MSG_WriteByte (&client->message, host_client - svs.clients);
		MSG_WriteString (&client->message, "");
		MSG_WriteByte (&client->message, svc_updatecolors);
		MSG_WriteByte (&client->message, host_client - svs.clients);
		MSG_WriteByte (&client->message, 0);

		MSG_WriteByte (&client->message, svc_updatefrags);
		MSG_WriteByte (&client->message, host_client - svs.clients);
		MSG_WriteShort (&client->message, 0);
	}
}

/*
==================
Host_ShutdownServer

This only happens at the end of a game, not between levels
==================
*/
void Host_ShutdownServer (qboolean crash)
{
	int		  i;
	int		  count;
	sizebuf_t buf;
	byte	  message[4];
	double	  start;

	if (!sv.active)
		return;

	sv.active = false;

	// stop all client sounds immediately
	if (cls.state == ca_connected)
		CL_Disconnect ();

	// flush any pending messages - like the score!!!
	start = Sys_DoubleTime ();
	do
	{
		count = 0;
		for (i = 0, host_client = svs.clients; i < svs.maxclients; i++, host_client++)
		{
			if (host_client->active && host_client->message.cursize && host_client->netconnection)
			{
				if (NET_CanSendMessage (host_client->netconnection))
				{
					NET_SendMessage (host_client->netconnection, &host_client->message);
					SZ_Clear (&host_client->message);
				}
				else
				{
					NET_GetMessage (host_client->netconnection);
					count++;
				}
			}
		}
		if ((Sys_DoubleTime () - start) > 3.0)
			break;
	} while (count);

	// make sure all the clients know we're disconnecting
	buf.data = message;
	buf.maxsize = 4;
	buf.cursize = 0;
	MSG_WriteByte (&buf, svc_disconnect);
	count = NET_SendToAll (&buf, 5.0);
	if (count)
		Con_Printf ("Host_ShutdownServer: NET_SendToAll failed for %u clients\n", count);

	PR_SwitchQCVM (&sv.qcvm);
	for (i = 0, host_client = svs.clients; i < svs.maxclients; i++, host_client++)
		if (host_client->active)
			SV_DropClient (crash);

	qcvm->worldmodel = NULL;
	PR_SwitchQCVM (NULL);

	//
	// clear structures
	//
	//	memset (&sv, 0, sizeof(sv)); // ServerSpawn already do this by Host_ClearMemory
	memset (svs.clients, 0, svs.maxclientslimit * sizeof (client_t));
}

/*
================
Host_ClearMemory

This clears all the memory used by both the client and server, but does
not reinitialize anything.

newmap contains the path of the map that is about to be loaded. If it matches the last one and keepbmodelcache is set, brush models are not freed.
If it's NULL, brush models are not freed (pending a later call to Mod_ClearBModelCaches once the map name is known)
================
*/
void Host_ClearMemory (char *newmap)
{
	if (cl.qcvm.extfuncs.CSQC_Shutdown)
	{
		PR_SwitchQCVM (&cl.qcvm);
		PR_ExecuteProgram (qcvm->extfuncs.CSQC_Shutdown);
		qcvm->extfuncs.CSQC_Shutdown = 0;
		PR_SwitchQCVM (NULL);
	}

	Con_DPrintf ("Clearing memory\n");
	if (newmap)
		Mod_ClearBModelCaches (newmap);

	if (!isDedicated)
		S_ClearAll ();
	cls.signon = 0;
	PR_ClearProgs (&sv.qcvm);
	Mem_Free (sv.static_entities); // spike -- this is dynamic too, now
	for (int i = 1; i < MAX_PARTICLETYPES; ++i)
		Mem_Free (sv.particle_precache[i]);
	memset (&sv, 0, sizeof (sv));

	CL_FreeState ();
}

//==============================================================================
//
// Host Frame
//
//==============================================================================

/*
===================
Host_FilterTime

Returns false if the time is too short to run a frame
===================
*/
qboolean Host_FilterTime (float time)
{
	float maxfps; // johnfitz
	float min_frame_time;
	float delta_since_last_frame;

	realtime += time;
	delta_since_last_frame = realtime - oldrealtime;

	if (host_maxfps.value)
	{
		// johnfitz -- max fps cvar
		maxfps = CLAMP (10.0, host_maxfps.value, 1000.0);

		// Check if we still have more than 2ms till next frame and if so wait for "1ms"
		// E.g. Windows is not a real time OS and the sleeps can vary in length even with timeBeginPeriod(1)
		min_frame_time = 1.0f / maxfps;
		if ((min_frame_time - delta_since_last_frame) > (2.0f / 1000.0f))
			SDL_Delay (1);

		if (!cls.timedemo && (delta_since_last_frame < min_frame_time))
			return false; // framerate is too high
						  // johnfitz
	}

	host_frametime = delta_since_last_frame;
	oldrealtime = realtime;

	if (cls.demoplayback && cls.demospeed != 1.f && cls.demospeed > 0.f)
		host_frametime *= cls.demospeed;
	// johnfitz -- host_timescale is more intuitive than host_framerate
	else if (host_timescale.value > 0)
		host_frametime *= host_timescale.value;
	// johnfitz
	else if (host_framerate.value > 0)
		host_frametime = host_framerate.value;
	else if (host_maxfps.value)								  // don't allow really long or short frames
		host_frametime = CLAMP (0.0001, host_frametime, 0.1); // johnfitz -- use CLAMP

	return true;
}

/*
===================
Host_GetConsoleCommands

Add them exactly as if they had been typed at the console
===================
*/
void Host_GetConsoleCommands (void)
{
	const char *cmd;

	if (!isDedicated)
		return; // no stdin necessary in graphical mode

	while (1)
	{
		cmd = Sys_ConsoleInput ();
		if (!cmd)
			break;
		Cbuf_AddText (cmd);
	}
}

/*
==================
Host_ServerFrame
==================
*/
void Host_ServerFrame (void)
{
	int		 i, active; // johnfitz
	edict_t *ent;		// johnfitz

	// run the world state
	pr_global_struct->frametime = host_frametime;

	// set the time and clear the general datagram
	SV_ClearDatagram ();

	// check for new clients
	SV_CheckForNewClients ();

	// read client messages
	SV_RunClients ();

	// move things around and think
	// always pause in single player if in console or menus
	if (!sv.paused && (svs.maxclients > 1 || key_dest == key_game))
		SV_Physics ();

	// johnfitz -- devstats
	if (cls.signon == SIGNONS)
	{
		for (i = 0, active = 0; i < qcvm->num_edicts; i++)
		{
			ent = EDICT_NUM (i);
			if (!ent->free)
				active++;
		}
		if (active > 600 && dev_peakstats.edicts <= 600)
			Con_DWarning ("%i edicts exceeds standard limit of 600 (max = %d).\n", active, qcvm->max_edicts);
		dev_stats.edicts = active;
		dev_peakstats.edicts = q_max (active, dev_peakstats.edicts);
	}
	// johnfitz

	// send all messages to the clients
	SV_SendClientMessages ();
}

static void CL_LoadCSProgs (void)
{
	PR_ClearProgs (&cl.qcvm);
	if (pr_checkextension.value && !cl_nocsqc.value)
	{ // only try to use csqc if qc extensions are enabled.
		char		 versionedname[MAX_QPATH];
		unsigned int csqchash;
		PR_SwitchQCVM (&cl.qcvm);
		csqchash = strtoul (Info_GetKey (cl.serverinfo, "*csprogs", versionedname, sizeof (versionedname)), NULL, 0);

		q_snprintf (versionedname, MAX_QPATH, "csprogsvers/%x.dat", csqchash);

		// try csprogs.dat first, then fall back on progs.dat in case someone tried merging the two.
		// we only care about it if it actually contains a CSQC_DrawHud, otherwise its either just a (misnamed) ssqc progs or a full csqc progs that would just
		// crash us on 3d stuff.
		if ((PR_LoadProgs (versionedname, false, PROGHEADER_CRC, pr_csqcbuiltins, pr_csqcnumbuiltins) && qcvm->extfuncs.CSQC_DrawHud) ||
			(PR_LoadProgs ("csprogs.dat", false, PROGHEADER_CRC, pr_csqcbuiltins, pr_csqcnumbuiltins) && qcvm->extfuncs.CSQC_DrawHud) ||
			(PR_LoadProgs ("progs.dat", false, PROGHEADER_CRC, pr_csqcbuiltins, pr_csqcnumbuiltins) && qcvm->extfuncs.CSQC_DrawHud))
		{
			qcvm->max_edicts = CLAMP (MIN_EDICTS, (int)max_edicts.value, MAX_EDICTS);
			qcvm->edicts = (edict_t *)Mem_Alloc (qcvm->max_edicts * qcvm->edict_size);
			qcvm->num_edicts = qcvm->reserved_edicts = 1;
			memset (qcvm->edicts, 0, qcvm->num_edicts * qcvm->edict_size);

			if (!qcvm->extfuncs.CSQC_DrawHud)
			{ // no simplecsqc entry points... abort entirely!
				PR_ClearProgs (qcvm);
				PR_SwitchQCVM (NULL);
				return;
			}

			// set a few globals, if they exist
			if (qcvm->extglobals.maxclients)
				*qcvm->extglobals.maxclients = cl.maxclients;
			pr_global_struct->time = cl.time;
			pr_global_struct->mapname = PR_SetEngineString (cl.mapname);
			pr_global_struct->total_monsters = cl.statsf[STAT_TOTALMONSTERS];
			pr_global_struct->total_secrets = cl.statsf[STAT_TOTALSECRETS];
			pr_global_struct->deathmatch = cl.gametype;
			pr_global_struct->coop = (cl.gametype == GAME_COOP) && cl.maxclients != 1;
			if (qcvm->extglobals.player_localnum)
				*qcvm->extglobals.player_localnum = cl.viewentity - 1; // this is a guess, but is important for scoreboards.

			// set a few worldspawn fields too
			qcvm->edicts->v.solid = SOLID_BSP;
			qcvm->edicts->v.modelindex = 1;
			qcvm->edicts->v.model = PR_SetEngineString (cl.worldmodel->name);
			VectorCopy (cl.worldmodel->mins, qcvm->edicts->v.mins);
			VectorCopy (cl.worldmodel->maxs, qcvm->edicts->v.maxs);
			qcvm->edicts->v.message = PR_SetEngineString (cl.levelname);

			// and call the init function... if it exists.
			qcvm->worldmodel = cl.worldmodel;
			SV_ClearWorld ();
			if (qcvm->extfuncs.CSQC_Init)
			{
				int maj = (int)VKQUAKE_VERSION;
				int min = (VKQUAKE_VERSION - maj) * 100;
				G_FLOAT (OFS_PARM0) = false;
				G_INT (OFS_PARM1) = PR_SetEngineString ("vkQuake");
				G_FLOAT (OFS_PARM2) = 10000 * maj + 100 * (min) + VKQUAKE_VER_PATCH;
				PR_ExecuteProgram (qcvm->extfuncs.CSQC_Init);
			}
		}
		else
			PR_ClearProgs (qcvm);
		PR_SwitchQCVM (NULL);
	}
}

/*
==================
Host_Frame

Runs all active servers
==================
*/
void _Host_Frame (double time)
{
	static double accumtime = 0;
	static double time1 = 0;
	static double time2 = 0;
	static double time3 = 0;
	double		  pass1, pass2, pass3;

	if (setjmp (host_abortserver))
		return; // something bad happened, or the server disconnected

	// keep the random time dependent
	rand ();

	// decide the simulation time
	accumtime += host_netinterval ? CLAMP (0, time, 0.2) : 0; // for renderer/server isolation
	if (!Host_FilterTime (time))
		return; // don't run too fast, or packets will flood out

	if (host_speeds.value)
		time3 = Sys_DoubleTime ();

	if (!isDedicated)
	{
		// get new key events
		Key_UpdateForDest ();
		IN_UpdateInputMode ();
		Sys_SendKeyEvents ();

		// allow mice or other external controllers to add commands
		IN_Commands ();
	}

	// check the stdin for commands (dedicated servers)
	Host_GetConsoleCommands ();

	// process console commands
	Cbuf_Execute ();

	NET_Poll ();

	if (cl.sendprespawn)
	{
		CL_LoadCSProgs ();

		cl.sendprespawn = false;
		MSG_WriteByte (&cls.message, clc_stringcmd);
		MSG_WriteString (&cls.message, "prespawn");
		vid.recalc_refdef = true;
	}

	CL_AccumulateCmd ();
	M_UpdateMouse ();

	// Run the server+networking (client->server->client), at a different rate from everyt
	while ((host_netinterval == 0) || (accumtime >= host_netinterval))
	{
		float realframetime = host_frametime;
		if (host_netinterval && isDedicated == 0)
		{
			host_frametime = sv.active ? (listening ? q_min (accumtime, 0.017) : host_netinterval) : accumtime;
			accumtime -= host_frametime;
			if (host_timescale.value > 0)
				host_frametime *= host_timescale.value;
			else if (host_framerate.value)
				host_frametime = host_framerate.value;
		}

		CL_SendCmd ();
		if (sv.active)
		{
			PR_SwitchQCVM (&sv.qcvm);
			Host_ServerFrame ();
			PR_SwitchQCVM (NULL);
		}
		host_frametime = realframetime;
		Cbuf_Waited ();

		if (host_netinterval == 0 || isDedicated)
			break;
	}

	if (cl.qcvm.progs)
	{
		PR_SwitchQCVM (&cl.qcvm);
		pr_global_struct->frametime = host_frametime;
		SV_Physics ();
		PR_SwitchQCVM (NULL);
	}

	// fetch results from server
	if (cls.state == ca_connected)
		CL_ReadFromServer ();

	// update video
	if (host_speeds.value)
		time1 = Sys_DoubleTime ();

	SCR_UpdateScreen (true);

	CL_RunParticles (); // johnfitz -- seperated from rendering

	if (host_speeds.value)
		time2 = Sys_DoubleTime ();

	// update audio
	BGM_Update (); // adds music raw samples and/or advances midi driver
	if (cls.signon == SIGNONS)
	{
		S_Update (r_origin, vpn, vright, vup);
		CL_DecayLights ();
	}
	else if (!isDedicated)
		S_Update (vec3_origin, vec3_origin, vec3_origin, vec3_origin);

	CDAudio_Update ();

	if (host_speeds.value)
	{
		pass1 = (time1 - time3) * 1000;
		time3 = Sys_DoubleTime ();
		pass2 = (time2 - time1) * 1000;
		pass3 = (time3 - time2) * 1000;
		Con_Printf ("%5.2f tot %5.2f server %5.2f gfx %5.2f snd\n", pass1 + pass2 + pass3, pass1, pass2, pass3);
	}

	host_framecount++;
}

void Host_Frame (double time)
{
	double		  time1, time2;
	static double timetotal;
	static int	  timecount;
	int			  i, c, m;

	if (!serverprofile.value)
	{
		_Host_Frame (time);
		return;
	}

	time1 = Sys_DoubleTime ();
	_Host_Frame (time);
	time2 = Sys_DoubleTime ();

	timetotal += time2 - time1;
	timecount++;

	if (timecount < 1000)
		return;

	m = timetotal * 1000 / timecount;
	timecount = 0;
	timetotal = 0;
	c = 0;
	for (i = 0; i < svs.maxclients; i++)
	{
		if (svs.clients[i].active)
			c++;
	}

	Con_Printf ("serverprofile: %2i clients %2i msec\n", c, m);
}

/*
====================
Tests_Init
====================
*/
static void Tests_Init ()
{
#ifdef _DEBUG
	Cmd_AddCommand ("test_hash_map", TestHashMap_f);
	Cmd_AddCommand ("test_gl_heap", GL_HeapTest_f);
	Cmd_AddCommand ("test_tasks", TestTasks_f);
#endif
}

/*
====================
Host_Init
====================
*/
void Host_Init (void)
{
	com_argc = host_parms->argc;
	com_argv = host_parms->argv;

	Mem_Init ();
	Tasks_Init ();
	Cbuf_Init ();
	Cmd_Init ();
	LOG_Init (host_parms);
	Cvar_Init (); // johnfitz
	COM_Init ();
	COM_InitFilesystem ();
	Host_InitLocal ();
	W_LoadWadFile (); // johnfitz -- filename is now hard-coded for honesty
	if (cls.state != ca_dedicated)
	{
		Key_Init ();
		Con_Init ();
	}
	PR_Init ();
	Mod_Init ();
	NET_Init ();
	SV_Init ();

	Con_Printf ("Exe: " __TIME__ " " __DATE__ "\n");

	if (cls.state != ca_dedicated)
	{
		host_colormap = (byte *)COM_LoadFile ("gfx/colormap.lmp", NULL);
		if (!host_colormap)
			Sys_Error ("Couldn't load gfx/colormap.lmp");

		V_Init ();
		Chase_Init ();
		M_Init ();
		ExtraMaps_Init (); // johnfitz
		Modlist_Init ();   // johnfitz
		DemoList_Init ();  // ericw
		SaveList_Init ();
		VID_Init ();
		IN_Init ();
		TexMgr_Init (); // johnfitz
		Draw_Init ();
		SCR_Init ();
		R_Init ();
		S_Init ();
		CDAudio_Init ();
		BGM_Init ();
		Sbar_Init ();
		CL_Init ();
		Tests_Init ();
	}

#ifdef PSET_SCRIPT
	PScript_InitParticles ();
#endif
	LOC_Init (); // for 2021 rerelease support.

	host_initialized = true;
	Con_Printf ("\n========= Quake Initialized =========\n\n");

	if (cls.state != ca_dedicated)
	{
		Cbuf_InsertText ("exec quake.rc\n");
		// johnfitz -- in case the vid mode was locked during vid_init, we can unlock it now.
		// note: two leading newlines because the command buffer swallows one of them.
		Cbuf_AddText ("\n\nvid_unlock\n");
	}

	if (cls.state == ca_dedicated)
	{
		Cbuf_AddText ("exec autoexec.cfg\n");
		Cbuf_AddText ("stuffcmds");
		Cbuf_Execute ();
		if (!sv.active)
			Cbuf_AddText ("map start\n");
	}
}

/*
===============
Host_Shutdown

FIXME: this is a callback from Sys_Quit and Sys_Error.  It would be better
to run quit through here before the final handoff to the sys code.
===============
*/
void Host_Shutdown (void)
{
	assert (!Tasks_IsWorker ());
	static qboolean isdown = false;

	if (isdown)
	{
		printf ("recursive shutdown\n");
		return;
	}
	isdown = true;

	// keep Con_Printf from trying to update the screen
	scr_disabled_for_loading = true;

	Host_WriteConfiguration ();

	NET_Shutdown ();

	if (cls.state != ca_dedicated)
	{
		if (con_initialized)
			History_Shutdown ();
		BGM_Shutdown ();
		CDAudio_Shutdown ();
		S_Shutdown ();
		IN_Shutdown ();
		VID_Shutdown ();
	}

	LOG_Close ();

	LOC_Shutdown ();
}
