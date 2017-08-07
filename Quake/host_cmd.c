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

#include "quakedef.h"
#ifndef _WIN32
#include <dirent.h>
#endif

extern cvar_t	pausable;

int	current_skill;

void Mod_Print (void);

/*
==================
Host_Quit_f
==================
*/

void Host_Quit_f (void)
{
	if (key_dest != key_console && cls.state != ca_dedicated)
	{
		M_Menu_Quit_f ();
		return;
	}
	CL_Disconnect ();
	Host_ShutdownServer(false);

	Sys_Quit ();
}

//==============================================================================
//johnfitz -- extramaps management
//==============================================================================

/*
==================
FileList_Add
==================
*/
void FileList_Add (const char *name, filelist_item_t **list)
{
	filelist_item_t	*item,*cursor,*prev;

	// ignore duplicate
	for (item = *list; item; item = item->next)
	{
		if (!Q_strcmp (name, item->name))
			return;
	}

	item = (filelist_item_t *) Z_Malloc(sizeof(filelist_item_t));
	q_strlcpy (item->name, name, sizeof(item->name));

	// insert each entry in alphabetical order
	if (*list == NULL ||
	    q_strcasecmp(item->name, (*list)->name) < 0) //insert at front
	{
		item->next = *list;
		*list = item;
	}
	else //insert later
	{
		prev = *list;
		cursor = (*list)->next;
		while (cursor && (q_strcasecmp(item->name, cursor->name) > 0))
		{
			prev = cursor;
			cursor = cursor->next;
		}
		item->next = prev->next;
		prev->next = item;
	}
}

static void FileList_Clear (filelist_item_t **list)
{
	filelist_item_t *blah;
	
	while (*list)
	{
		blah = (*list)->next;
		Z_Free(*list);
		*list = blah;
	}
}

filelist_item_t	*extralevels;

void ExtraMaps_Add (const char *name)
{
	FileList_Add(name, &extralevels);
}

void ExtraMaps_Init (void)
{
#ifdef _WIN32
	WIN32_FIND_DATA	fdat;
	HANDLE		fhnd;
#else
	DIR		*dir_p;
	struct dirent	*dir_t;
#endif
	char		filestring[MAX_OSPATH];
	char		mapname[32];
	char		ignorepakdir[32];
	searchpath_t	*search;
	pack_t		*pak;
	int		i;

	// we don't want to list the maps in id1 pakfiles,
	// because these are not "add-on" levels
	q_snprintf (ignorepakdir, sizeof(ignorepakdir), "/%s/", GAMENAME);

	for (search = com_searchpaths; search; search = search->next)
	{
		if (*search->filename) //directory
		{
#ifdef _WIN32
			q_snprintf (filestring, sizeof(filestring), "%s/maps/*.bsp", search->filename);
			fhnd = FindFirstFile(filestring, &fdat);
			if (fhnd == INVALID_HANDLE_VALUE)
				continue;
			do
			{
				COM_StripExtension(fdat.cFileName, mapname, sizeof(mapname));
				ExtraMaps_Add (mapname);
			} while (FindNextFile(fhnd, &fdat));
			FindClose(fhnd);
#else
			q_snprintf (filestring, sizeof(filestring), "%s/maps/", search->filename);
			dir_p = opendir(filestring);
			if (dir_p == NULL)
				continue;
			while ((dir_t = readdir(dir_p)) != NULL)
			{
				if (q_strcasecmp(COM_FileGetExtension(dir_t->d_name), "bsp") != 0)
					continue;
				COM_StripExtension(dir_t->d_name, mapname, sizeof(mapname));
				ExtraMaps_Add (mapname);
			}
			closedir(dir_p);
#endif
		}
		else //pakfile
		{
			if (!strstr(search->pack->filename, ignorepakdir))
			{ //don't list standard id maps
				for (i = 0, pak = search->pack; i < pak->numfiles; i++)
				{
					if (!strcmp(COM_FileGetExtension(pak->files[i].name), "bsp"))
					{
						if (pak->files[i].filelen > 32*1024)
						{ // don't list files under 32k (ammo boxes etc)
							COM_StripExtension(pak->files[i].name + 5, mapname, sizeof(mapname));
							ExtraMaps_Add (mapname);
						}
					}
				}
			}
		}
	}
}

static void ExtraMaps_Clear (void)
{
	FileList_Clear(&extralevels);
}

void ExtraMaps_NewGame (void)
{
	ExtraMaps_Clear ();
	ExtraMaps_Init ();
}

/*
==================
Host_Maps_f
==================
*/
void Host_Maps_f (void)
{
	int i;
	filelist_item_t	*level;

	for (level = extralevels, i = 0; level; level = level->next, i++)
		Con_SafePrintf ("   %s\n", level->name);

	if (i)
		Con_SafePrintf ("%i map(s)\n", i);
	else
		Con_SafePrintf ("no maps found\n");
}

//==============================================================================
//johnfitz -- modlist management
//==============================================================================

filelist_item_t	*modlist;

void Modlist_Add (const char *name)
{
	FileList_Add(name, &modlist);
}

#ifdef _WIN32
void Modlist_Init (void)
{
	WIN32_FIND_DATA	fdat;
	HANDLE		fhnd;
	DWORD		attribs;
	char		dir_string[MAX_OSPATH], mod_string[MAX_OSPATH];

	q_snprintf (dir_string, sizeof(dir_string), "%s/*", com_basedir);
	fhnd = FindFirstFile(dir_string, &fdat);
	if (fhnd == INVALID_HANDLE_VALUE)
		return;

	do
	{
		if (!strcmp(fdat.cFileName, ".") || !strcmp(fdat.cFileName, ".."))
			continue;
		q_snprintf (mod_string, sizeof(mod_string), "%s/%s", com_basedir, fdat.cFileName);
		attribs = GetFileAttributes (mod_string);
		if (attribs != INVALID_FILE_ATTRIBUTES && (attribs & FILE_ATTRIBUTE_DIRECTORY)) {
			/* don't bother testing for pak files / progs.dat */
			Modlist_Add(fdat.cFileName);
		}
	} while (FindNextFile(fhnd, &fdat));

	FindClose(fhnd);
}
#else
void Modlist_Init (void)
{
	DIR		*dir_p, *mod_dir_p;
	struct dirent	*dir_t;
	char		dir_string[MAX_OSPATH], mod_string[MAX_OSPATH];

	q_snprintf (dir_string, sizeof(dir_string), "%s/", com_basedir);
	dir_p = opendir(dir_string);
	if (dir_p == NULL)
		return;

	while ((dir_t = readdir(dir_p)) != NULL)
	{
		if (!strcmp(dir_t->d_name, ".") || !strcmp(dir_t->d_name, ".."))
			continue;
		if (!q_strcasecmp (COM_FileGetExtension (dir_t->d_name), "app")) // skip .app bundles on macOS
			continue;
		q_snprintf(mod_string, sizeof(mod_string), "%s%s/", dir_string, dir_t->d_name);
		mod_dir_p = opendir(mod_string);
		if (mod_dir_p == NULL)
			continue;
		/* don't bother testing for pak files / progs.dat */
		Modlist_Add(dir_t->d_name);
		closedir(mod_dir_p);
	}

	closedir(dir_p);
}
#endif

//==============================================================================
//ericw -- demo list management
//==============================================================================

filelist_item_t	*demolist;

static void DemoList_Clear (void)
{
	FileList_Clear (&demolist);
}

void DemoList_Rebuild (void)
{
	DemoList_Clear ();
	DemoList_Init ();
}

// TODO: Factor out to a general-purpose file searching function
void DemoList_Init (void)
{
#ifdef _WIN32
	WIN32_FIND_DATA	fdat;
	HANDLE		fhnd;
#else
	DIR		*dir_p;
	struct dirent	*dir_t;
#endif
	char		filestring[MAX_OSPATH];
	char		demname[32];
	char		ignorepakdir[32];
	searchpath_t	*search;
	pack_t		*pak;
	int		i;
	
	// we don't want to list the demos in id1 pakfiles,
	// because these are not "add-on" demos
	q_snprintf (ignorepakdir, sizeof(ignorepakdir), "/%s/", GAMENAME);
	
	for (search = com_searchpaths; search; search = search->next)
	{
		if (*search->filename) //directory
		{
#ifdef _WIN32
			q_snprintf (filestring, sizeof(filestring), "%s/*.dem", search->filename);
			fhnd = FindFirstFile(filestring, &fdat);
			if (fhnd == INVALID_HANDLE_VALUE)
				continue;
			do
			{
				COM_StripExtension(fdat.cFileName, demname, sizeof(demname));
				FileList_Add (demname, &demolist);
			} while (FindNextFile(fhnd, &fdat));
			FindClose(fhnd);
#else
			q_snprintf (filestring, sizeof(filestring), "%s/", search->filename);
			dir_p = opendir(filestring);
			if (dir_p == NULL)
				continue;
			while ((dir_t = readdir(dir_p)) != NULL)
			{
				if (q_strcasecmp(COM_FileGetExtension(dir_t->d_name), "dem") != 0)
					continue;
				COM_StripExtension(dir_t->d_name, demname, sizeof(demname));
				FileList_Add (demname, &demolist);
			}
			closedir(dir_p);
#endif
		}
		else //pakfile
		{
			if (!strstr(search->pack->filename, ignorepakdir))
			{ //don't list standard id demos
				for (i = 0, pak = search->pack; i < pak->numfiles; i++)
				{
					if (!strcmp(COM_FileGetExtension(pak->files[i].name), "dem"))
					{
						COM_StripExtension(pak->files[i].name, demname, sizeof(demname));
						FileList_Add (demname, &demolist);
					}
				}
			}
		}
	}
}


/*
==================
Host_Mods_f -- johnfitz

list all potential mod directories (contain either a pak file or a progs.dat)
==================
*/
void Host_Mods_f (void)
{
	int i;
	filelist_item_t	*mod;

	for (mod = modlist, i=0; mod; mod = mod->next, i++)
		Con_SafePrintf ("   %s\n", mod->name);

	if (i)
		Con_SafePrintf ("%i mod(s)\n", i);
	else
		Con_SafePrintf ("no mods found\n");
}

//==============================================================================

/*
=============
Host_Mapname_f -- johnfitz
=============
*/
void Host_Mapname_f (void)
{
	if (sv.active)
	{
		Con_Printf ("\"mapname\" is \"%s\"\n", sv.name);
		return;
	}

	if (cls.state == ca_connected)
	{
		Con_Printf ("\"mapname\" is \"%s\"\n", cl.mapname);
		return;
	}

	Con_Printf ("no map loaded\n");
}

/*
==================
Host_Status_f
==================
*/
void Host_Status_f (void)
{
	void	(*print_fn) (const char *fmt, ...)
				 FUNCP_PRINTF(1,2);
	client_t	*client;
	int			seconds;
	int			minutes;
	int			hours = 0;
	int			j;

	if (cmd_source == src_command)
	{
		if (!sv.active)
		{
			Cmd_ForwardToServer ();
			return;
		}
		print_fn = Con_Printf;
	}
	else
		print_fn = SV_ClientPrintf;

	print_fn ("host:    %s\n", Cvar_VariableString ("hostname"));
	print_fn ("version: %4.2f\n", VERSION);
	if (tcpipAvailable)
		print_fn ("tcp/ip:  %s\n", my_tcpip_address);
	if (ipxAvailable)
		print_fn ("ipx:     %s\n", my_ipx_address);
	print_fn ("map:     %s\n", sv.name);
	print_fn ("players: %i active (%i max)\n\n", net_activeconnections, svs.maxclients);
	for (j = 0, client = svs.clients; j < svs.maxclients; j++, client++)
	{
		if (!client->active)
			continue;
		seconds = (int)(net_time - NET_QSocketGetTime(client->netconnection));
		minutes = seconds / 60;
		if (minutes)
		{
			seconds -= (minutes * 60);
			hours = minutes / 60;
			if (hours)
				minutes -= (hours * 60);
		}
		else
			hours = 0;
		print_fn ("#%-2u %-16.16s  %3i  %2i:%02i:%02i\n", j+1, client->name, (int)client->edict->v.frags, hours, minutes, seconds);
		print_fn ("   %s\n", NET_QSocketGetAddressString(client->netconnection));
	}
}

/*
==================
Host_God_f

Sets client to godmode
==================
*/
void Host_God_f (void)
{
	if (cmd_source == src_command)
	{
		Cmd_ForwardToServer ();
		return;
	}

	if (pr_global_struct->deathmatch)
		return;

	//johnfitz -- allow user to explicitly set god mode to on or off
	switch (Cmd_Argc())
	{
	case 1:
		sv_player->v.flags = (int)sv_player->v.flags ^ FL_GODMODE;
		if (!((int)sv_player->v.flags & FL_GODMODE) )
			SV_ClientPrintf ("godmode OFF\n");
		else
			SV_ClientPrintf ("godmode ON\n");
		break;
	case 2:
		if (Q_atof(Cmd_Argv(1)))
		{
			sv_player->v.flags = (int)sv_player->v.flags | FL_GODMODE;
			SV_ClientPrintf ("godmode ON\n");
		}
		else
		{
			sv_player->v.flags = (int)sv_player->v.flags & ~FL_GODMODE;
			SV_ClientPrintf ("godmode OFF\n");
		}
		break;
	default:
		Con_Printf("god [value] : toggle god mode. values: 0 = off, 1 = on\n");
		break;
	}
	//johnfitz
}

/*
==================
Host_Notarget_f
==================
*/
void Host_Notarget_f (void)
{
	if (cmd_source == src_command)
	{
		Cmd_ForwardToServer ();
		return;
	}

	if (pr_global_struct->deathmatch)
		return;

	//johnfitz -- allow user to explicitly set notarget to on or off
	switch (Cmd_Argc())
	{
	case 1:
		sv_player->v.flags = (int)sv_player->v.flags ^ FL_NOTARGET;
		if (!((int)sv_player->v.flags & FL_NOTARGET) )
			SV_ClientPrintf ("notarget OFF\n");
		else
			SV_ClientPrintf ("notarget ON\n");
		break;
	case 2:
		if (Q_atof(Cmd_Argv(1)))
		{
			sv_player->v.flags = (int)sv_player->v.flags | FL_NOTARGET;
			SV_ClientPrintf ("notarget ON\n");
		}
		else
		{
			sv_player->v.flags = (int)sv_player->v.flags & ~FL_NOTARGET;
			SV_ClientPrintf ("notarget OFF\n");
		}
		break;
	default:
		Con_Printf("notarget [value] : toggle notarget mode. values: 0 = off, 1 = on\n");
		break;
	}
	//johnfitz
}

qboolean noclip_anglehack;

/*
==================
Host_Noclip_f
==================
*/
void Host_Noclip_f (void)
{
	if (cmd_source == src_command)
	{
		Cmd_ForwardToServer ();
		return;
	}

	if (pr_global_struct->deathmatch)
		return;

	//johnfitz -- allow user to explicitly set noclip to on or off
	switch (Cmd_Argc())
	{
	case 1:
		if (sv_player->v.movetype != MOVETYPE_NOCLIP)
		{
			noclip_anglehack = true;
			sv_player->v.movetype = MOVETYPE_NOCLIP;
			SV_ClientPrintf ("noclip ON\n");
		}
		else
		{
			noclip_anglehack = false;
			sv_player->v.movetype = MOVETYPE_WALK;
			SV_ClientPrintf ("noclip OFF\n");
		}
		break;
	case 2:
		if (Q_atof(Cmd_Argv(1)))
		{
			noclip_anglehack = true;
			sv_player->v.movetype = MOVETYPE_NOCLIP;
			SV_ClientPrintf ("noclip ON\n");
		}
		else
		{
			noclip_anglehack = false;
			sv_player->v.movetype = MOVETYPE_WALK;
			SV_ClientPrintf ("noclip OFF\n");
		}
		break;
	default:
		Con_Printf("noclip [value] : toggle noclip mode. values: 0 = off, 1 = on\n");
		break;
	}
	//johnfitz
}

/*
====================
Host_SetPos_f

adapted from fteqw, originally by Alex Shadowalker
====================
*/
void Host_SetPos_f(void)
{
	if (cmd_source == src_command)
	{
		Cmd_ForwardToServer ();
		return;
	}
	
	if (pr_global_struct->deathmatch)
		return;
	
	if (Cmd_Argc() != 7 && Cmd_Argc() != 4)
	{
		SV_ClientPrintf("usage:\n");
		SV_ClientPrintf("   setpos <x> <y> <z>\n");
		SV_ClientPrintf("   setpos <x> <y> <z> <pitch> <yaw> <roll>\n");
		SV_ClientPrintf("current values:\n");
		SV_ClientPrintf("   %i %i %i %i %i %i\n",
			(int)sv_player->v.origin[0],
			(int)sv_player->v.origin[1],
			(int)sv_player->v.origin[2],
			(int)sv_player->v.v_angle[0],
			(int)sv_player->v.v_angle[1],
			(int)sv_player->v.v_angle[2]);
		return;
	}
	
	if (sv_player->v.movetype != MOVETYPE_NOCLIP)
	{
		noclip_anglehack = true;
		sv_player->v.movetype = MOVETYPE_NOCLIP;
		SV_ClientPrintf ("noclip ON\n");
	}
	
	//make sure they're not going to whizz away from it
	sv_player->v.velocity[0] = 0;
	sv_player->v.velocity[1] = 0;
	sv_player->v.velocity[2] = 0;
	
	sv_player->v.origin[0] = atof(Cmd_Argv(1));
	sv_player->v.origin[1] = atof(Cmd_Argv(2));
	sv_player->v.origin[2] = atof(Cmd_Argv(3));
	
	if (Cmd_Argc() == 7)
	{
		sv_player->v.angles[0] = atof(Cmd_Argv(4));
		sv_player->v.angles[1] = atof(Cmd_Argv(5));
		sv_player->v.angles[2] = atof(Cmd_Argv(6));
		sv_player->v.fixangle = 1;
	}
	
	SV_LinkEdict (sv_player, false);
}

/*
==================
Host_Fly_f

Sets client to flymode
==================
*/
void Host_Fly_f (void)
{
	if (cmd_source == src_command)
	{
		Cmd_ForwardToServer ();
		return;
	}

	if (pr_global_struct->deathmatch)
		return;

	//johnfitz -- allow user to explicitly set noclip to on or off
	switch (Cmd_Argc())
	{
	case 1:
		if (sv_player->v.movetype != MOVETYPE_FLY)
		{
			sv_player->v.movetype = MOVETYPE_FLY;
			SV_ClientPrintf ("flymode ON\n");
		}
		else
		{
			sv_player->v.movetype = MOVETYPE_WALK;
			SV_ClientPrintf ("flymode OFF\n");
		}
		break;
	case 2:
		if (Q_atof(Cmd_Argv(1)))
		{
			sv_player->v.movetype = MOVETYPE_FLY;
			SV_ClientPrintf ("flymode ON\n");
		}
		else
		{
			sv_player->v.movetype = MOVETYPE_WALK;
			SV_ClientPrintf ("flymode OFF\n");
		}
		break;
	default:
		Con_Printf("fly [value] : toggle fly mode. values: 0 = off, 1 = on\n");
		break;
	}
	//johnfitz
}


/*
==================
Host_Ping_f

==================
*/
void Host_Ping_f (void)
{
	int		i, j;
	float		total;
	client_t	*client;

	if (cmd_source == src_command)
	{
		Cmd_ForwardToServer ();
		return;
	}

	SV_ClientPrintf ("Client ping times:\n");
	for (i = 0, client = svs.clients; i < svs.maxclients; i++, client++)
	{
		if (!client->active)
			continue;
		total = 0;
		for (j = 0; j < NUM_PING_TIMES; j++)
			total+=client->ping_times[j];
		total /= NUM_PING_TIMES;
		SV_ClientPrintf ("%4i %s\n", (int)(total*1000), client->name);
	}
}

/*
===============================================================================

SERVER TRANSITIONS

===============================================================================
*/


/*
======================
Host_Map_f

handle a
map <servername>
command from the console.  Active clients are kicked off.
======================
*/
void Host_Map_f (void)
{
	int		i;
	char	name[MAX_QPATH], *p;

	if (Cmd_Argc() < 2)	//no map name given
	{
		if (cls.state == ca_dedicated)
		{
			if (sv.active)
				Con_Printf ("Current map: %s\n", sv.name);
			else
				Con_Printf ("Server not active\n");
		}
		else if (cls.state == ca_connected)
		{
			Con_Printf ("Current map: %s ( %s )\n", cl.levelname, cl.mapname);
		}
		else
		{
			Con_Printf ("map <levelname>: start a new server\n");
		}
		return;
	}

	if (cmd_source != src_command)
		return;

	cls.demonum = -1;		// stop demo loop in case this fails

	CL_Disconnect ();
	Host_ShutdownServer(false);

	if (cls.state != ca_dedicated)
		IN_Activate();
	key_dest = key_game;			// remove console or menu
	SCR_BeginLoadingPlaque ();

	svs.serverflags = 0;			// haven't completed an episode yet
	q_strlcpy (name, Cmd_Argv(1), sizeof(name));
	// remove (any) trailing ".bsp" from mapname -- S.A.
	p = strstr(name, ".bsp");
	if (p && p[4] == '\0')
		*p = '\0';
	SV_SpawnServer (name);
	if (!sv.active)
		return;

	if (cls.state != ca_dedicated)
	{
		memset (cls.spawnparms, 0, MAX_MAPSTRING);
		for (i = 2; i < Cmd_Argc(); i++)
		{
			q_strlcat (cls.spawnparms, Cmd_Argv(i), MAX_MAPSTRING);
			q_strlcat (cls.spawnparms, " ", MAX_MAPSTRING);
		}

		Cmd_ExecuteString ("connect local", src_command);
	}
}

/*
======================
Host_Randmap_f

Loads a random map from the "maps" list.
======================
*/
void Host_Randmap_f (void)
{
	int	i, randlevel, numlevels;
	filelist_item_t	*level;

	if (cmd_source != src_command)
		return;

	for (level = extralevels, numlevels = 0; level; level = level->next)
		numlevels++;

	if (numlevels == 0)
	{
		Con_Printf ("no maps\n");
		return;
	}

	randlevel = (rand() % numlevels);

	for (level = extralevels, i = 0; level; level = level->next, i++)
	{
		if (i == randlevel)
		{
			Con_Printf ("Starting map %s...\n", level->name);
			Cbuf_AddText (va("map %s\n", level->name));
			return;
		}
	}
}

/*
==================
Host_Changelevel_f

Goes to a new map, taking all clients along
==================
*/
void Host_Changelevel_f (void)
{
	char	level[MAX_QPATH];

	if (Cmd_Argc() != 2)
	{
		Con_Printf ("changelevel <levelname> : continue game on a new level\n");
		return;
	}
	if (!sv.active || cls.demoplayback)
	{
		Con_Printf ("Only the server may changelevel\n");
		return;
	}

	//johnfitz -- check for client having map before anything else
	q_snprintf (level, sizeof(level), "maps/%s.bsp", Cmd_Argv(1));
	if (!COM_FileExists(level, NULL))
		Host_Error ("cannot find map %s", level);
	//johnfitz

	if (cls.state != ca_dedicated)
		IN_Activate();	// -- S.A.
	key_dest = key_game;	// remove console or menu
	SV_SaveSpawnparms ();
	q_strlcpy (level, Cmd_Argv(1), sizeof(level));
	SV_SpawnServer (level);
	// also issue an error if spawn failed -- O.S.
	if (!sv.active)
		Host_Error ("cannot run map %s", level);
}

/*
==================
Host_Restart_f

Restarts the current server for a dead player
==================
*/
void Host_Restart_f (void)
{
	char	mapname[MAX_QPATH];

	if (cls.demoplayback || !sv.active)
		return;

	if (cmd_source != src_command)
		return;
	q_strlcpy (mapname, sv.name, sizeof(mapname));	// mapname gets cleared in spawnserver
	SV_SpawnServer (mapname);
	if (!sv.active)
		Host_Error ("cannot restart map %s", mapname);
}

/*
==================
Host_Reconnect_f

This command causes the client to wait for the signon messages again.
This is sent just before a server changes levels
==================
*/
void Host_Reconnect_f (void)
{
	if (cls.demoplayback)	// cross-map demo playback fix from Baker
		return;

	SCR_BeginLoadingPlaque ();
	cls.signon = 0;		// need new connection messages
}

/*
=====================
Host_Connect_f

User command to connect to server
=====================
*/
void Host_Connect_f (void)
{
	char	name[MAX_QPATH];

	cls.demonum = -1;		// stop demo loop in case this fails
	if (cls.demoplayback)
	{
		CL_StopPlayback ();
		CL_Disconnect ();
	}
	q_strlcpy (name, Cmd_Argv(1), sizeof(name));
	CL_EstablishConnection (name);
	Host_Reconnect_f ();
}


/*
===============================================================================

LOAD / SAVE GAME

===============================================================================
*/

#define	SAVEGAME_VERSION	5

/*
===============
Host_SavegameComment

Writes a SAVEGAME_COMMENT_LENGTH character comment describing the current
===============
*/
void Host_SavegameComment (char *text)
{
	int		i;
	char	kills[20];

	for (i = 0; i < SAVEGAME_COMMENT_LENGTH; i++)
		text[i] = ' ';
	memcpy (text, cl.levelname, q_min(strlen(cl.levelname),22)); //johnfitz -- only copy 22 chars.
	sprintf (kills,"kills:%3i/%3i", cl.stats[STAT_MONSTERS], cl.stats[STAT_TOTALMONSTERS]);
	memcpy (text+22, kills, strlen(kills));
// convert space to _ to make stdio happy
	for (i = 0; i < SAVEGAME_COMMENT_LENGTH; i++)
	{
		if (text[i] == ' ')
			text[i] = '_';
	}
	text[SAVEGAME_COMMENT_LENGTH] = '\0';
}


/*
===============
Host_Savegame_f
===============
*/
void Host_Savegame_f (void)
{
	char	name[MAX_OSPATH];
	FILE	*f;
	int	i;
	char	comment[SAVEGAME_COMMENT_LENGTH+1];

	if (cmd_source != src_command)
		return;

	if (!sv.active)
	{
		Con_Printf ("Not playing a local game.\n");
		return;
	}

	if (cl.intermission)
	{
		Con_Printf ("Can't save in intermission.\n");
		return;
	}

	if (svs.maxclients != 1)
	{
		Con_Printf ("Can't save multiplayer games.\n");
		return;
	}

	if (Cmd_Argc() != 2)
	{
		Con_Printf ("save <savename> : save a game\n");
		return;
	}

	if (strstr(Cmd_Argv(1), ".."))
	{
		Con_Printf ("Relative pathnames are not allowed.\n");
		return;
	}

	for (i=0 ; i<svs.maxclients ; i++)
	{
		if (svs.clients[i].active && (svs.clients[i].edict->v.health <= 0) )
		{
			Con_Printf ("Can't savegame with a dead player\n");
			return;
		}
	}

	q_snprintf (name, sizeof(name), "%s/%s", com_gamedir, Cmd_Argv(1));
	COM_AddExtension (name, ".sav", sizeof(name));

	Con_Printf ("Saving game to %s...\n", name);
	f = fopen (name, "w");
	if (!f)
	{
		Con_Printf ("ERROR: couldn't open.\n");
		return;
	}

	fprintf (f, "%i\n", SAVEGAME_VERSION);
	Host_SavegameComment (comment);
	fprintf (f, "%s\n", comment);
	for (i = 0; i < NUM_SPAWN_PARMS; i++)
		fprintf (f, "%f\n", svs.clients->spawn_parms[i]);
	fprintf (f, "%d\n", current_skill);
	fprintf (f, "%s\n", sv.name);
	fprintf (f, "%f\n",sv.time);

// write the light styles

	for (i = 0; i < MAX_LIGHTSTYLES; i++)
	{
		if (sv.lightstyles[i])
			fprintf (f, "%s\n", sv.lightstyles[i]);
		else
			fprintf (f,"m\n");
	}


	ED_WriteGlobals (f);
	for (i = 0; i < sv.num_edicts; i++)
	{
		ED_Write (f, EDICT_NUM(i));
		fflush (f);
	}
	fclose (f);
	Con_Printf ("done.\n");
}


/*
===============
Host_Loadgame_f
===============
*/
void Host_Loadgame_f (void)
{
	static char	*start;
	
	char	name[MAX_OSPATH];
	char	mapname[MAX_QPATH];
	float	time, tfloat;
	const char	*data;
	int	i;
	edict_t	*ent;
	int	entnum;
	int	version;
	float	spawn_parms[NUM_SPAWN_PARMS];

	if (cmd_source != src_command)
		return;

	if (Cmd_Argc() != 2)
	{
		Con_Printf ("load <savename> : load a game\n");
		return;
	}
	
	if (strstr(Cmd_Argv(1), ".."))
	{
		Con_Printf ("Relative pathnames are not allowed.\n");
		return;
	}

	cls.demonum = -1;		// stop demo loop in case this fails

	q_snprintf (name, sizeof(name), "%s/%s", com_gamedir, Cmd_Argv(1));
	COM_AddExtension (name, ".sav", sizeof(name));

// we can't call SCR_BeginLoadingPlaque, because too much stack space has
// been used.  The menu calls it before stuffing loadgame command
//	SCR_BeginLoadingPlaque ();

	Con_Printf ("Loading game from %s...\n", name);
	
// avoid leaking if the previous Host_Loadgame_f failed with a Host_Error
	if (start != NULL)
		free (start);
	
	start = (char *) COM_LoadMallocFile_TextMode_OSPath(name, NULL);
	if (start == NULL)
	{
		Con_Printf ("ERROR: couldn't open.\n");
		return;
	}

	data = start;
	data = COM_ParseIntNewline (data, &version);
	if (version != SAVEGAME_VERSION)
	{
		free (start);
		start = NULL;
		Con_Printf ("Savegame is version %i, not %i\n", version, SAVEGAME_VERSION);
		return;
	}
	data = COM_ParseStringNewline (data);
	for (i = 0; i < NUM_SPAWN_PARMS; i++)
		data = COM_ParseFloatNewline (data, &spawn_parms[i]);
// this silliness is so we can load 1.06 save files, which have float skill values
	data = COM_ParseFloatNewline(data, &tfloat);
	current_skill = (int)(tfloat + 0.1);
	Cvar_SetValue ("skill", (float)current_skill);

	data = COM_ParseStringNewline (data);
	q_strlcpy (mapname, com_token, sizeof(mapname));
	data = COM_ParseFloatNewline (data, &time);

	CL_Disconnect_f ();

	SV_SpawnServer (mapname);

	if (!sv.active)
	{
		free (start);
		start = NULL;
		Con_Printf ("Couldn't load map\n");
		return;
	}
	sv.paused = true;		// pause until all clients connect
	sv.loadgame = true;

// load the light styles

	for (i = 0; i < MAX_LIGHTSTYLES; i++)
	{
		data = COM_ParseStringNewline (data);
		sv.lightstyles[i] = (const char *)Hunk_Strdup (com_token, "lightstyles");
	}

// load the edicts out of the savegame file
	entnum = -1;		// -1 is the globals
	while (*data)
	{
		data = COM_Parse (data);
		if (!com_token[0])
			break;		// end of file
		if (strcmp(com_token,"{"))
		{
			Sys_Error ("First token isn't a brace");
		}

		if (entnum == -1)
		{	// parse the global vars
			data = ED_ParseGlobals (data);
		}
		else
		{	// parse an edict
			ent = EDICT_NUM(entnum);
			if (entnum < sv.num_edicts) {
				ent->free = false;
				memset (&ent->v, 0, progs->entityfields * 4);
			}
			else {
				memset (ent, 0, pr_edict_size);
			}
			data = ED_ParseEdict (data, ent);

		// link it into the bsp tree
			if (!ent->free)
				SV_LinkEdict (ent, false);
		}

		entnum++;
	}

	sv.num_edicts = entnum;
	sv.time = time;

	free (start);
	start = NULL;

	for (i = 0; i < NUM_SPAWN_PARMS; i++)
		svs.clients->spawn_parms[i] = spawn_parms[i];

	if (cls.state != ca_dedicated)
	{
		CL_EstablishConnection ("local");
		Host_Reconnect_f ();
	}
}

//============================================================================

/*
======================
Host_Name_f
======================
*/
void Host_Name_f (void)
{
	char	newName[32];

	if (Cmd_Argc () == 1)
	{
		Con_Printf ("\"name\" is \"%s\"\n", cl_name.string);
		return;
	}
	if (Cmd_Argc () == 2)
		q_strlcpy(newName, Cmd_Argv(1), sizeof(newName));
	else
		q_strlcpy(newName, Cmd_Args(), sizeof(newName));
	newName[15] = 0;	// client_t structure actually says name[32].

	if (cmd_source == src_command)
	{
		if (Q_strcmp(cl_name.string, newName) == 0)
			return;
		Cvar_Set ("_cl_name", newName);
		if (cls.state == ca_connected)
			Cmd_ForwardToServer ();
		return;
	}

	if (host_client->name[0] && strcmp(host_client->name, "unconnected") )
	{
		if (Q_strcmp(host_client->name, newName) != 0)
			Con_Printf ("%s renamed to %s\n", host_client->name, newName);
	}
	Q_strcpy (host_client->name, newName);
	host_client->edict->v.netname = PR_SetEngineString(host_client->name);

// send notification to all clients

	MSG_WriteByte (&sv.reliable_datagram, svc_updatename);
	MSG_WriteByte (&sv.reliable_datagram, host_client - svs.clients);
	MSG_WriteString (&sv.reliable_datagram, host_client->name);
}

void Host_Say(qboolean teamonly)
{
	int		j;
	client_t	*client;
	client_t	*save;
	const char	*p;
	char		text[MAXCMDLINE], *p2;
	qboolean	quoted;
	qboolean	fromServer = false;

	if (cmd_source == src_command)
	{
		if (cls.state != ca_dedicated)
		{
			Cmd_ForwardToServer ();
			return;
		}
		fromServer = true;
		teamonly = false;
	}

	if (Cmd_Argc () < 2)
		return;

	save = host_client;

	p = Cmd_Args();
// remove quotes if present
	quoted = false;
	if (*p == '\"')
	{
		p++;
		quoted = true;
	}
// turn on color set 1
	if (!fromServer)
		q_snprintf (text, sizeof(text), "\001%s: %s", save->name, p);
	else
		q_snprintf (text, sizeof(text), "\001<%s> %s", hostname.string, p);

// check length & truncate if necessary
	j = (int) strlen(text);
	if (j >= (int) sizeof(text) - 1)
	{
		text[sizeof(text) - 2] = '\n';
		text[sizeof(text) - 1] = '\0';
	}
	else
	{
		p2 = text + j;
		while ((const char *)p2 > (const char *)text &&
			(p2[-1] == '\r' || p2[-1] == '\n' || (p2[-1] == '\"' && quoted)) )
		{
			if (p2[-1] == '\"' && quoted)
				quoted = false;
			p2[-1] = '\0';
			p2--;
		}
		p2[0] = '\n';
		p2[1] = '\0';
	}

	for (j = 0, client = svs.clients; j < svs.maxclients; j++, client++)
	{
		if (!client || !client->active || !client->spawned)
			continue;
		if (teamplay.value && teamonly && client->edict->v.team != save->edict->v.team)
			continue;
		host_client = client;
		SV_ClientPrintf("%s", text);
	}
	host_client = save;

	if (cls.state == ca_dedicated)
		Sys_Printf("%s", &text[1]);
}


void Host_Say_f(void)
{
	Host_Say(false);
}


void Host_Say_Team_f(void)
{
	Host_Say(true);
}


void Host_Tell_f(void)
{
	int		j;
	client_t	*client;
	client_t	*save;
	const char	*p;
	char		text[MAXCMDLINE], *p2;
	qboolean	quoted;

	if (cmd_source == src_command)
	{
		Cmd_ForwardToServer ();
		return;
	}

	if (Cmd_Argc () < 3)
		return;

	p = Cmd_Args();
// remove quotes if present
	quoted = false;
	if (*p == '\"')
	{
		p++;
		quoted = true;
	}
	q_snprintf (text, sizeof(text), "%s: %s", host_client->name, p);

// check length & truncate if necessary
	j = (int) strlen(text);
	if (j >= (int) sizeof(text) - 1)
	{
		text[sizeof(text) - 2] = '\n';
		text[sizeof(text) - 1] = '\0';
	}
	else
	{
		p2 = text + j;
		while ((const char *)p2 > (const char *)text &&
			(p2[-1] == '\r' || p2[-1] == '\n' || (p2[-1] == '\"' && quoted)) )
		{
			if (p2[-1] == '\"' && quoted)
				quoted = false;
			p2[-1] = '\0';
			p2--;
		}
		p2[0] = '\n';
		p2[1] = '\0';
	}

	save = host_client;
	for (j = 0, client = svs.clients; j < svs.maxclients; j++, client++)
	{
		if (!client->active || !client->spawned)
			continue;
		if (q_strcasecmp(client->name, Cmd_Argv(1)))
			continue;
		host_client = client;
		SV_ClientPrintf("%s", text);
		break;
	}
	host_client = save;
}


/*
==================
Host_Color_f
==================
*/
void Host_Color_f(void)
{
	int		top, bottom;
	int		playercolor;

	if (Cmd_Argc() == 1)
	{
		Con_Printf ("\"color\" is \"%i %i\"\n", ((int)cl_color.value) >> 4, ((int)cl_color.value) & 0x0f);
		Con_Printf ("color <0-13> [0-13]\n");
		return;
	}

	if (Cmd_Argc() == 2)
		top = bottom = atoi(Cmd_Argv(1));
	else
	{
		top = atoi(Cmd_Argv(1));
		bottom = atoi(Cmd_Argv(2));
	}

	top &= 15;
	if (top > 13)
		top = 13;
	bottom &= 15;
	if (bottom > 13)
		bottom = 13;

	playercolor = top*16 + bottom;

	if (cmd_source == src_command)
	{
		Cvar_SetValue ("_cl_color", playercolor);
		if (cls.state == ca_connected)
			Cmd_ForwardToServer ();
		return;
	}

	host_client->colors = playercolor;
	host_client->edict->v.team = bottom + 1;

// send notification to all clients
	MSG_WriteByte (&sv.reliable_datagram, svc_updatecolors);
	MSG_WriteByte (&sv.reliable_datagram, host_client - svs.clients);
	MSG_WriteByte (&sv.reliable_datagram, host_client->colors);
}

/*
==================
Host_Kill_f
==================
*/
void Host_Kill_f (void)
{
	if (cmd_source == src_command)
	{
		Cmd_ForwardToServer ();
		return;
	}

	if (sv_player->v.health <= 0)
	{
		SV_ClientPrintf ("Can't suicide -- allready dead!\n");
		return;
	}

	pr_global_struct->time = sv.time;
	pr_global_struct->self = EDICT_TO_PROG(sv_player);
	PR_ExecuteProgram (pr_global_struct->ClientKill);
}


/*
==================
Host_Pause_f
==================
*/
void Host_Pause_f (void)
{
//ericw -- demo pause support (inspired by MarkV)
	if (cls.demoplayback)
	{
		cls.demopaused = !cls.demopaused;
		cl.paused = cls.demopaused;
		return;
	}

	if (cmd_source == src_command)
	{
		Cmd_ForwardToServer ();
		return;
	}
	if (!pausable.value)
		SV_ClientPrintf ("Pause not allowed.\n");
	else
	{
		sv.paused ^= 1;

		if (sv.paused)
		{
			SV_BroadcastPrintf ("%s paused the game\n", PR_GetString(sv_player->v.netname));
		}
		else
		{
			SV_BroadcastPrintf ("%s unpaused the game\n",PR_GetString(sv_player->v.netname));
		}

	// send notification to all clients
		MSG_WriteByte (&sv.reliable_datagram, svc_setpause);
		MSG_WriteByte (&sv.reliable_datagram, sv.paused);
	}
}

//===========================================================================


/*
==================
Host_PreSpawn_f
==================
*/
void Host_PreSpawn_f (void)
{
	if (cmd_source == src_command)
	{
		Con_Printf ("prespawn is not valid from the console\n");
		return;
	}

	if (host_client->spawned)
	{
		Con_Printf ("prespawn not valid -- allready spawned\n");
		return;
	}

	SZ_Write (&host_client->message, sv.signon.data, sv.signon.cursize);
	MSG_WriteByte (&host_client->message, svc_signonnum);
	MSG_WriteByte (&host_client->message, 2);
	host_client->sendsignon = true;
}

/*
==================
Host_Spawn_f
==================
*/
void Host_Spawn_f (void)
{
	int		i;
	client_t	*client;
	edict_t	*ent;

	if (cmd_source == src_command)
	{
		Con_Printf ("spawn is not valid from the console\n");
		return;
	}

	if (host_client->spawned)
	{
		Con_Printf ("Spawn not valid -- allready spawned\n");
		return;
	}

// run the entrance script
	if (sv.loadgame)
	{	// loaded games are fully inited allready
		// if this is the last client to be connected, unpause
		sv.paused = false;
	}
	else
	{
		// set up the edict
		ent = host_client->edict;

		memset (&ent->v, 0, progs->entityfields * 4);
		ent->v.colormap = NUM_FOR_EDICT(ent);
		ent->v.team = (host_client->colors & 15) + 1;
		ent->v.netname = PR_SetEngineString(host_client->name);

		// copy spawn parms out of the client_t
		for (i=0 ; i< NUM_SPAWN_PARMS ; i++)
			(&pr_global_struct->parm1)[i] = host_client->spawn_parms[i];
		// call the spawn function
		pr_global_struct->time = sv.time;
		pr_global_struct->self = EDICT_TO_PROG(sv_player);
		PR_ExecuteProgram (pr_global_struct->ClientConnect);

		if ((Sys_DoubleTime() - NET_QSocketGetTime(host_client->netconnection)) <= sv.time)
			Sys_Printf ("%s entered the game\n", host_client->name);

		PR_ExecuteProgram (pr_global_struct->PutClientInServer);
	}


// send all current names, colors, and frag counts
	SZ_Clear (&host_client->message);

// send time of update
	MSG_WriteByte (&host_client->message, svc_time);
	MSG_WriteFloat (&host_client->message, sv.time);

	for (i = 0, client = svs.clients; i < svs.maxclients; i++, client++)
	{
		MSG_WriteByte (&host_client->message, svc_updatename);
		MSG_WriteByte (&host_client->message, i);
		MSG_WriteString (&host_client->message, client->name);
		MSG_WriteByte (&host_client->message, svc_updatefrags);
		MSG_WriteByte (&host_client->message, i);
		MSG_WriteShort (&host_client->message, client->old_frags);
		MSG_WriteByte (&host_client->message, svc_updatecolors);
		MSG_WriteByte (&host_client->message, i);
		MSG_WriteByte (&host_client->message, client->colors);
	}

// send all current light styles
	for (i = 0; i < MAX_LIGHTSTYLES; i++)
	{
		MSG_WriteByte (&host_client->message, svc_lightstyle);
		MSG_WriteByte (&host_client->message, (char)i);
		MSG_WriteString (&host_client->message, sv.lightstyles[i]);
	}

//
// send some stats
//
	MSG_WriteByte (&host_client->message, svc_updatestat);
	MSG_WriteByte (&host_client->message, STAT_TOTALSECRETS);
	MSG_WriteLong (&host_client->message, pr_global_struct->total_secrets);

	MSG_WriteByte (&host_client->message, svc_updatestat);
	MSG_WriteByte (&host_client->message, STAT_TOTALMONSTERS);
	MSG_WriteLong (&host_client->message, pr_global_struct->total_monsters);

	MSG_WriteByte (&host_client->message, svc_updatestat);
	MSG_WriteByte (&host_client->message, STAT_SECRETS);
	MSG_WriteLong (&host_client->message, pr_global_struct->found_secrets);

	MSG_WriteByte (&host_client->message, svc_updatestat);
	MSG_WriteByte (&host_client->message, STAT_MONSTERS);
	MSG_WriteLong (&host_client->message, pr_global_struct->killed_monsters);

//
// send a fixangle
// Never send a roll angle, because savegames can catch the server
// in a state where it is expecting the client to correct the angle
// and it won't happen if the game was just loaded, so you wind up
// with a permanent head tilt
	ent = EDICT_NUM( 1 + (host_client - svs.clients) );
	MSG_WriteByte (&host_client->message, svc_setangle);
	for (i = 0; i < 2; i++)
		MSG_WriteAngle (&host_client->message, ent->v.angles[i], sv.protocolflags );
	MSG_WriteAngle (&host_client->message, 0, sv.protocolflags );

	SV_WriteClientdataToMessage (sv_player, &host_client->message);

	MSG_WriteByte (&host_client->message, svc_signonnum);
	MSG_WriteByte (&host_client->message, 3);
	host_client->sendsignon = true;
}

/*
==================
Host_Begin_f
==================
*/
void Host_Begin_f (void)
{
	if (cmd_source == src_command)
	{
		Con_Printf ("begin is not valid from the console\n");
		return;
	}

	host_client->spawned = true;
}

//===========================================================================


/*
==================
Host_Kick_f

Kicks a user off of the server
==================
*/
void Host_Kick_f (void)
{
	const char	*who;
	const char	*message = NULL;
	client_t	*save;
	int		i;
	qboolean	byNumber = false;

	if (cmd_source == src_command)
	{
		if (!sv.active)
		{
			Cmd_ForwardToServer ();
			return;
		}
	}
	else if (pr_global_struct->deathmatch)
		return;

	save = host_client;

	if (Cmd_Argc() > 2 && Q_strcmp(Cmd_Argv(1), "#") == 0)
	{
		i = Q_atof(Cmd_Argv(2)) - 1;
		if (i < 0 || i >= svs.maxclients)
			return;
		if (!svs.clients[i].active)
			return;
		host_client = &svs.clients[i];
		byNumber = true;
	}
	else
	{
		for (i = 0, host_client = svs.clients; i < svs.maxclients; i++, host_client++)
		{
			if (!host_client->active)
				continue;
			if (q_strcasecmp(host_client->name, Cmd_Argv(1)) == 0)
				break;
		}
	}

	if (i < svs.maxclients)
	{
		if (cmd_source == src_command)
			if (cls.state == ca_dedicated)
				who = "Console";
			else
				who = cl_name.string;
		else
			who = save->name;

		// can't kick yourself!
		if (host_client == save)
			return;

		if (Cmd_Argc() > 2)
		{
			message = COM_Parse(Cmd_Args());
			if (byNumber)
			{
				message++;			// skip the #
				while (*message == ' ')		// skip white space
					message++;
				message += strlen(Cmd_Argv(2));	// skip the number
			}
			while (*message && *message == ' ')
				message++;
		}
		if (message)
			SV_ClientPrintf ("Kicked by %s: %s\n", who, message);
		else
			SV_ClientPrintf ("Kicked by %s\n", who);
		SV_DropClient (false);
	}

	host_client = save;
}

/*
===============================================================================

DEBUGGING TOOLS

===============================================================================
*/

/*
==================
Host_Give_f
==================
*/
void Host_Give_f (void)
{
	const char	*t;
	int	v;
	eval_t	*val;

	if (cmd_source == src_command)
	{
		Cmd_ForwardToServer ();
		return;
	}

	if (pr_global_struct->deathmatch)
		return;

	t = Cmd_Argv(1);
	v = atoi (Cmd_Argv(2));

	switch (t[0])
	{
	case '0':
	case '1':
	case '2':
	case '3':
	case '4':
	case '5':
	case '6':
	case '7':
	case '8':
	case '9':
		// MED 01/04/97 added hipnotic give stuff
		if (hipnotic)
		{
		    if (t[0] == '6')
		    {
			if (t[1] == 'a')
			    sv_player->v.items = (int)sv_player->v.items | HIT_PROXIMITY_GUN;
			else
			    sv_player->v.items = (int)sv_player->v.items | IT_GRENADE_LAUNCHER;
		    }
		    else if (t[0] == '9')
			sv_player->v.items = (int)sv_player->v.items | HIT_LASER_CANNON;
		    else if (t[0] == '0')
			sv_player->v.items = (int)sv_player->v.items | HIT_MJOLNIR;
		    else if (t[0] >= '2')
			sv_player->v.items = (int)sv_player->v.items | (IT_SHOTGUN << (t[0] - '2'));
		}
		else
		{
		    if (t[0] >= '2')
			sv_player->v.items = (int)sv_player->v.items | (IT_SHOTGUN << (t[0] - '2'));
		}
		break;

	case 's':
		if (rogue)
		{
		    val = GetEdictFieldValue(sv_player, "ammo_shells1");
		    if (val)
			val->_float = v;
		}
		sv_player->v.ammo_shells = v;
		break;

	case 'n':
		if (rogue)
		{
		    val = GetEdictFieldValue(sv_player, "ammo_nails1");
		    if (val)
		    {
			val->_float = v;
			if (sv_player->v.weapon <= IT_LIGHTNING)
			    sv_player->v.ammo_nails = v;
		    }
		}
		else
		{
		    sv_player->v.ammo_nails = v;
		}
		break;

	case 'l':
		if (rogue)
		{
		    val = GetEdictFieldValue(sv_player, "ammo_lava_nails");
		    if (val)
		    {
			val->_float = v;
			if (sv_player->v.weapon > IT_LIGHTNING)
			    sv_player->v.ammo_nails = v;
		    }
		}
		break;

	case 'r':
		if (rogue)
		{
		    val = GetEdictFieldValue(sv_player, "ammo_rockets1");
		    if (val)
		    {
			val->_float = v;
			if (sv_player->v.weapon <= IT_LIGHTNING)
			    sv_player->v.ammo_rockets = v;
		    }
		}
		else
		{
		    sv_player->v.ammo_rockets = v;
		}
		break;

	case 'm':
		if (rogue)
		{
		    val = GetEdictFieldValue(sv_player, "ammo_multi_rockets");
		    if (val)
		    {
			val->_float = v;
			if (sv_player->v.weapon > IT_LIGHTNING)
			    sv_player->v.ammo_rockets = v;
		    }
		}
		break;

	case 'h':
		sv_player->v.health = v;
		break;

	case 'c':
		if (rogue)
		{
		    val = GetEdictFieldValue(sv_player, "ammo_cells1");
		    if (val)
		    {
			val->_float = v;
			if (sv_player->v.weapon <= IT_LIGHTNING)
			    sv_player->v.ammo_cells = v;
		    }
		}
		else
		{
		    sv_player->v.ammo_cells = v;
		}
		break;

	case 'p':
		if (rogue)
		{
		    val = GetEdictFieldValue(sv_player, "ammo_plasma");
		    if (val)
		    {
			val->_float = v;
			if (sv_player->v.weapon > IT_LIGHTNING)
			    sv_player->v.ammo_cells = v;
		    }
		}
		break;

	//johnfitz -- give armour
	case 'a':
		if (v > 150)
		{
		    sv_player->v.armortype = 0.8;
		    sv_player->v.armorvalue = v;
		    sv_player->v.items = sv_player->v.items -
					((int)(sv_player->v.items) & (int)(IT_ARMOR1 | IT_ARMOR2 | IT_ARMOR3)) +
					IT_ARMOR3;
		}
		else if (v > 100)
		{
		    sv_player->v.armortype = 0.6;
		    sv_player->v.armorvalue = v;
		    sv_player->v.items = sv_player->v.items -
					((int)(sv_player->v.items) & (int)(IT_ARMOR1 | IT_ARMOR2 | IT_ARMOR3)) +
					IT_ARMOR2;
		}
		else if (v >= 0)
		{
		    sv_player->v.armortype = 0.3;
		    sv_player->v.armorvalue = v;
		    sv_player->v.items = sv_player->v.items -
					((int)(sv_player->v.items) & (int)(IT_ARMOR1 | IT_ARMOR2 | IT_ARMOR3)) +
					IT_ARMOR1;
		}
		break;
		//johnfitz
	}

	//johnfitz -- update currentammo to match new ammo (so statusbar updates correctly)
	switch ((int)(sv_player->v.weapon))
	{
	case IT_SHOTGUN:
	case IT_SUPER_SHOTGUN:
		sv_player->v.currentammo = sv_player->v.ammo_shells;
		break;
	case IT_NAILGUN:
	case IT_SUPER_NAILGUN:
	case RIT_LAVA_SUPER_NAILGUN:
		sv_player->v.currentammo = sv_player->v.ammo_nails;
		break;
	case IT_GRENADE_LAUNCHER:
	case IT_ROCKET_LAUNCHER:
	case RIT_MULTI_GRENADE:
	case RIT_MULTI_ROCKET:
		sv_player->v.currentammo = sv_player->v.ammo_rockets;
		break;
	case IT_LIGHTNING:
	case HIT_LASER_CANNON:
	case HIT_MJOLNIR:
		sv_player->v.currentammo = sv_player->v.ammo_cells;
		break;
	case RIT_LAVA_NAILGUN: //same as IT_AXE
		if (rogue)
			sv_player->v.currentammo = sv_player->v.ammo_nails;
		break;
	case RIT_PLASMA_GUN: //same as HIT_PROXIMITY_GUN
		if (rogue)
			sv_player->v.currentammo = sv_player->v.ammo_cells;
		if (hipnotic)
			sv_player->v.currentammo = sv_player->v.ammo_rockets;
		break;
	}
	//johnfitz
}

edict_t	*FindViewthing (void)
{
	int		i;
	edict_t	*e;

	for (i=0 ; i<sv.num_edicts ; i++)
	{
		e = EDICT_NUM(i);
		if ( !strcmp (PR_GetString(e->v.classname), "viewthing") )
			return e;
	}
	Con_Printf ("No viewthing on map\n");
	return NULL;
}

/*
==================
Host_Viewmodel_f
==================
*/
void Host_Viewmodel_f (void)
{
	edict_t	*e;
	qmodel_t	*m;

	e = FindViewthing ();
	if (!e)
		return;

	m = Mod_ForName (Cmd_Argv(1), false);
	if (!m)
	{
		Con_Printf ("Can't load %s\n", Cmd_Argv(1));
		return;
	}

	e->v.frame = 0;
	cl.model_precache[(int)e->v.modelindex] = m;
}

/*
==================
Host_Viewframe_f
==================
*/
void Host_Viewframe_f (void)
{
	edict_t	*e;
	int		f;
	qmodel_t	*m;

	e = FindViewthing ();
	if (!e)
		return;
	m = cl.model_precache[(int)e->v.modelindex];

	f = atoi(Cmd_Argv(1));
	if (f >= m->numframes)
		f = m->numframes - 1;

	e->v.frame = f;
}


void PrintFrameName (qmodel_t *m, int frame)
{
	aliashdr_t 			*hdr;
	maliasframedesc_t	*pframedesc;

	hdr = (aliashdr_t *)Mod_Extradata (m);
	if (!hdr)
		return;
	pframedesc = &hdr->frames[frame];

	Con_Printf ("frame %i: %s\n", frame, pframedesc->name);
}

/*
==================
Host_Viewnext_f
==================
*/
void Host_Viewnext_f (void)
{
	edict_t	*e;
	qmodel_t	*m;

	e = FindViewthing ();
	if (!e)
		return;
	m = cl.model_precache[(int)e->v.modelindex];

	e->v.frame = e->v.frame + 1;
	if (e->v.frame >= m->numframes)
		e->v.frame = m->numframes - 1;

	PrintFrameName (m, e->v.frame);
}

/*
==================
Host_Viewprev_f
==================
*/
void Host_Viewprev_f (void)
{
	edict_t	*e;
	qmodel_t	*m;

	e = FindViewthing ();
	if (!e)
		return;

	m = cl.model_precache[(int)e->v.modelindex];

	e->v.frame = e->v.frame - 1;
	if (e->v.frame < 0)
		e->v.frame = 0;

	PrintFrameName (m, e->v.frame);
}

/*
===============================================================================

DEMO LOOP CONTROL

===============================================================================
*/


/*
==================
Host_Startdemos_f
==================
*/
void Host_Startdemos_f (void)
{
	int		i, c;

	if (cls.state == ca_dedicated)
		return;

	c = Cmd_Argc() - 1;
	if (c > MAX_DEMOS)
	{
		Con_Printf ("Max %i demos in demoloop\n", MAX_DEMOS);
		c = MAX_DEMOS;
	}
	Con_Printf ("%i demo(s) in loop\n", c);

	for (i = 1; i < c + 1; i++)
		q_strlcpy (cls.demos[i-1], Cmd_Argv(i), sizeof(cls.demos[0]));

	if (!sv.active && cls.demonum != -1 && !cls.demoplayback)
	{
		cls.demonum = 0;
		if (!fitzmode)
		{  /* QuakeSpasm customization: */
			/* go straight to menu, no CL_NextDemo */
			cls.demonum = -1;
			Cbuf_InsertText("menu_main\n");
			return;
		}
		CL_NextDemo ();
	}
	else
	{
		cls.demonum = -1;
	}
}


/*
==================
Host_Demos_f

Return to looping demos
==================
*/
void Host_Demos_f (void)
{
	if (cls.state == ca_dedicated)
		return;
	if (cls.demonum == -1)
		cls.demonum = 1;
	CL_Disconnect_f ();
	CL_NextDemo ();
}

/*
==================
Host_Stopdemo_f

Return to looping demos
==================
*/
void Host_Stopdemo_f (void)
{
	if (cls.state == ca_dedicated)
		return;
	if (!cls.demoplayback)
		return;
	CL_StopPlayback ();
	CL_Disconnect ();
}

//=============================================================================

/*
==================
Host_InitCommands
==================
*/
void Host_InitCommands (void)
{
	Cmd_AddCommand ("maps", Host_Maps_f); //johnfitz
	Cmd_AddCommand ("mods", Host_Mods_f); //johnfitz
	Cmd_AddCommand ("games", Host_Mods_f); // as an alias to "mods" -- S.A. / QuakeSpasm
	Cmd_AddCommand ("mapname", Host_Mapname_f); //johnfitz
	Cmd_AddCommand ("randmap", Host_Randmap_f); //ericw

	Cmd_AddCommand ("status", Host_Status_f);
	Cmd_AddCommand ("quit", Host_Quit_f);
	Cmd_AddCommand ("god", Host_God_f);
	Cmd_AddCommand ("notarget", Host_Notarget_f);
	Cmd_AddCommand ("fly", Host_Fly_f);
	Cmd_AddCommand ("map", Host_Map_f);
	Cmd_AddCommand ("restart", Host_Restart_f);
	Cmd_AddCommand ("changelevel", Host_Changelevel_f);
	Cmd_AddCommand ("connect", Host_Connect_f);
	Cmd_AddCommand ("reconnect", Host_Reconnect_f);
	Cmd_AddCommand ("name", Host_Name_f);
	Cmd_AddCommand ("noclip", Host_Noclip_f);
	Cmd_AddCommand ("setpos", Host_SetPos_f); //QuakeSpasm

	Cmd_AddCommand ("say", Host_Say_f);
	Cmd_AddCommand ("say_team", Host_Say_Team_f);
	Cmd_AddCommand ("tell", Host_Tell_f);
	Cmd_AddCommand ("color", Host_Color_f);
	Cmd_AddCommand ("kill", Host_Kill_f);
	Cmd_AddCommand ("pause", Host_Pause_f);
	Cmd_AddCommand ("spawn", Host_Spawn_f);
	Cmd_AddCommand ("begin", Host_Begin_f);
	Cmd_AddCommand ("prespawn", Host_PreSpawn_f);
	Cmd_AddCommand ("kick", Host_Kick_f);
	Cmd_AddCommand ("ping", Host_Ping_f);
	Cmd_AddCommand ("load", Host_Loadgame_f);
	Cmd_AddCommand ("save", Host_Savegame_f);
	Cmd_AddCommand ("give", Host_Give_f);

	Cmd_AddCommand ("startdemos", Host_Startdemos_f);
	Cmd_AddCommand ("demos", Host_Demos_f);
	Cmd_AddCommand ("stopdemo", Host_Stopdemo_f);

	Cmd_AddCommand ("viewmodel", Host_Viewmodel_f);
	Cmd_AddCommand ("viewframe", Host_Viewframe_f);
	Cmd_AddCommand ("viewnext", Host_Viewnext_f);
	Cmd_AddCommand ("viewprev", Host_Viewprev_f);

	Cmd_AddCommand ("mcache", Mod_Print);
}

