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
#include <sys/stat.h>
#ifndef _WIN32
#include <dirent.h>
#else
#include <windows.h>
#endif

extern cvar_t pausable;
extern cvar_t autoload;
extern cvar_t autofastload;

int current_skill;

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
	Host_ShutdownServer (false);

	Sys_Quit ();
}

//==============================================================================
// johnfitz -- extramaps management
//==============================================================================

/*
==================
FileList_Add
==================
*/
static void FileList_Add (const char *name, filelist_item_t **list)
{
	filelist_item_t *item, *cursor, *prev;

	// ignore duplicate
	for (item = *list; item; item = item->next)
	{
		if (!strcmp (name, item->name))
			return;
	}

	item = (filelist_item_t *)Mem_Alloc (sizeof (filelist_item_t));
	q_strlcpy (item->name, name, sizeof (item->name));

	// insert each entry in alphabetical order
	if (*list == NULL || q_strcasecmp (item->name, (*list)->name) < 0) // insert at front
	{
		item->next = *list;
		*list = item;
	}
	else // insert later
	{
		prev = *list;
		cursor = (*list)->next;
		while (cursor && (q_strcasecmp (item->name, cursor->name) > 0))
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
		Mem_Free (*list);
		*list = blah;
	}
}

filelist_item_t *extralevels;

void FileList_Init (char *path, char *ext, int minsize, filelist_item_t **list)
{
#ifdef _WIN32
	WIN32_FIND_DATA fdat;
	HANDLE			fhnd;
#else
	DIR			  *dir_p;
	struct dirent *dir_t;
#endif
	char		  filestring[MAX_OSPATH];
	char		  filename[32];
	char		  ignorepakdir[32];
	searchpath_t *search;
	pack_t		 *pak;
	int			  i;
	searchpath_t  multiuser_saves;

	if (multiuser && !strcmp (ext, "sav"))
	{
		char *pref_path = SDL_GetPrefPath ("vkQuake", COM_GetGameNames (true));
		strcpy (multiuser_saves.filename, pref_path);
		SDL_free (pref_path);
		multiuser_saves.next = com_searchpaths;
	}
	else
		multiuser_saves.next = NULL;

	// we don't want to list the files in id1 pakfiles,
	// because these are not "add-on" files
	q_snprintf (ignorepakdir, sizeof (ignorepakdir), "/%s/", GAMENAME);

	for (search = (multiuser_saves.next ? &multiuser_saves : com_searchpaths); search; search = search->next)
	{
		if (*search->filename) // directory
		{
#ifdef _WIN32
			q_snprintf (filestring, sizeof (filestring), "%s/%s*.%s", search->filename, path, ext);
			fhnd = FindFirstFile (filestring, &fdat);
			if (fhnd == INVALID_HANDLE_VALUE)
				goto next;
			do
			{
				COM_StripExtension (fdat.cFileName, filename, sizeof (filename));
				FileList_Add (filename, list);
			} while (FindNextFile (fhnd, &fdat));
			FindClose (fhnd);
#else
			q_snprintf (filestring, sizeof (filestring), "%s/%s", search->filename, path);
			dir_p = opendir (filestring);
			if (dir_p == NULL)
				goto next;
			while ((dir_t = readdir (dir_p)) != NULL)
			{
				if (q_strcasecmp (COM_FileGetExtension (dir_t->d_name), ext) != 0)
					continue;
				COM_StripExtension (dir_t->d_name, filename, sizeof (filename));
				FileList_Add (filename, list);
			}
			closedir (dir_p);
#endif
		next:
			if (!strcmp (ext, "sav") && (!multiuser || search != &multiuser_saves)) // only game dir for savegames
				break;
		}
		else // pakfile
		{
			if (!strstr (search->pack->filename, ignorepakdir))
			{ // don't list standard id maps
				for (i = 0, pak = search->pack; i < pak->numfiles; i++)
				{
					if (!strcmp (COM_FileGetExtension (pak->files[i].name), ext))
					{
						if (pak->files[i].filelen > minsize)
						{ // don't list files under minsize (ammo boxes etc for maps)
							COM_StripExtension (pak->files[i].name + strlen (path), filename, sizeof (filename));
							FileList_Add (filename, list);
						}
					}
				}
			}
		}
	}
}

static void ExtraMaps_Clear (void)
{
	FileList_Clear (&extralevels);
}

void ExtraMaps_Init (void)
{
	FileList_Init ("maps/", "bsp", 32 * 1024, &extralevels);
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
static void Host_Maps_f (void)
{
	int				 i;
	filelist_item_t *level;

	for (level = extralevels, i = 0; level; level = level->next, i++)
		Con_SafePrintf ("   %s\n", level->name);

	if (i)
		Con_SafePrintf ("%i map(s)\n", i);
	else
		Con_SafePrintf ("no maps found\n");
}

//==============================================================================
// johnfitz -- modlist management
//==============================================================================

filelist_item_t *modlist;

static void Modlist_Add (const char *name)
{
	struct stat maps_info;
	if ((strlen (name) == 3) && (tolower (name[0]) == 'i') && (tolower (name[1]) == 'd') && (name[2] == '1'))
		return;
	if (COM_ModForbiddenChars (name))
		return;
	char pak_path[MAX_OSPATH];
	char progs_path[MAX_OSPATH];
	char csprogs_path[MAX_OSPATH];
	char maps_path[MAX_OSPATH];
	q_snprintf (pak_path, sizeof (pak_path), "%s/%s/pak0.pak", com_basedir, name);
	q_snprintf (progs_path, sizeof (progs_path), "%s/%s/progs.dat", com_basedir, name);
	q_snprintf (csprogs_path, sizeof (csprogs_path), "%s/%s/csprogs.dat", com_basedir, name);
	q_snprintf (maps_path, sizeof (maps_path), "%s/%s/maps", com_basedir, name);
	FILE *pak_file = fopen (pak_path, "rb");
	FILE *progs_file = fopen (progs_path, "rb");
	FILE *csprogs_file = fopen (csprogs_path, "rb");
	if (pak_file || progs_file || csprogs_file || (stat (maps_path, &maps_info) == 0 && maps_info.st_mode & S_IFDIR))
		FileList_Add (name, &modlist);
	if (pak_file)
		fclose (pak_file);
	if (progs_file)
		fclose (progs_file);
	if (csprogs_file)
		fclose (csprogs_file);
}

#ifdef _WIN32
void Modlist_Init (void)
{
	WIN32_FIND_DATA fdat;
	HANDLE			fhnd;
	DWORD			attribs;
	char			dir_string[MAX_OSPATH], mod_string[MAX_OSPATH];

	q_snprintf (dir_string, sizeof (dir_string), "%s/*", com_basedir);
	fhnd = FindFirstFile (dir_string, &fdat);
	if (fhnd == INVALID_HANDLE_VALUE)
		return;

	do
	{
		if (!strcmp (fdat.cFileName, ".") || !strcmp (fdat.cFileName, ".."))
			continue;
		q_snprintf (mod_string, sizeof (mod_string), "%s/%s", com_basedir, fdat.cFileName);
		attribs = GetFileAttributes (mod_string);
		if (attribs != INVALID_FILE_ATTRIBUTES && (attribs & FILE_ATTRIBUTE_DIRECTORY))
		{
			Modlist_Add (fdat.cFileName);
		}
	} while (FindNextFile (fhnd, &fdat));

	FindClose (fhnd);
}
#else
void Modlist_Init (void)
{
	DIR *dir_p, *mod_dir_p;
	struct dirent *dir_t;
	char dir_string[MAX_OSPATH], mod_string[MAX_OSPATH];

	q_snprintf (dir_string, sizeof (dir_string), "%s/", com_basedir);
	dir_p = opendir (dir_string);
	if (dir_p == NULL)
		return;

	while ((dir_t = readdir (dir_p)) != NULL)
	{
		if (!strcmp (dir_t->d_name, ".") || !strcmp (dir_t->d_name, ".."))
			continue;
		if (!q_strcasecmp (COM_FileGetExtension (dir_t->d_name), "app")) // skip .app bundles on macOS
			continue;
		q_snprintf (mod_string, sizeof (mod_string), "%s%s/", dir_string, dir_t->d_name);
		mod_dir_p = opendir (mod_string);
		if (mod_dir_p == NULL)
			continue;
		Modlist_Add (dir_t->d_name);
		closedir (mod_dir_p);
	}

	closedir (dir_p);
}
#endif

//==============================================================================
// ericw -- demo list management
//==============================================================================

filelist_item_t *demolist;

static void DemoList_Clear (void)
{
	FileList_Clear (&demolist);
}

void DemoList_Rebuild (void)
{
	DemoList_Clear ();
	DemoList_Init ();
}

void DemoList_Init (void)
{
	FileList_Init ("", "dem", 0, &demolist);
}

//==============================================================================
// savegame list management
//==============================================================================

filelist_item_t *savelist;

static void SaveList_Clear (void)
{
	FileList_Clear (&savelist);
}

void SaveList_Rebuild (void)
{
	SaveList_Clear ();
	SaveList_Init ();
}

void SaveList_Init (void)
{
	FileList_Init ("", "sav", 0, &savelist);
}

/*
==================
Host_Mods_f -- johnfitz

list all potential mod directories (contain either a pak file or a progs.dat)
==================
*/
static void Host_Mods_f (void)
{
	int				 i;
	filelist_item_t *mod;

	for (mod = modlist, i = 0; mod; mod = mod->next, i++)
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
static void Host_Mapname_f (void)
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
static void Host_Status_f (void)
{
	void (*print_fn) (const char *fmt, ...) FUNCP_PRINTF (1, 2);
	client_t *client;
	int		  seconds;
	int		  minutes;
	int		  hours = 0;
	int		  j, i;

	qhostaddr_t addresses[32];
	int			numaddresses;

	if (cmd_source != src_client)
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
	print_fn ("version: " ENGINE_NAME_AND_VER "\n");

	numaddresses = NET_ListAddresses (addresses, sizeof (addresses) / sizeof (addresses[0]));
	for (i = 0; i < numaddresses; i++)
	{
		if (*addresses[i] == '[')
			print_fn ("ipv6:    %s\n", addresses[i]); // Spike -- FIXME: we should really have ports displayed here or something
		else
			print_fn ("tcp/ip:  %s\n", addresses[i]); // Spike -- FIXME: we should really have ports displayed here or something
	}
	if (ipv4Available)
		print_fn ("tcp/ip:  %s\n", my_ipv4_address); // Spike -- FIXME: we should really have ports displayed here or something
	if (ipv6Available)
		print_fn ("ipv6:    %s\n", my_ipv6_address);
	if (ipxAvailable)
		print_fn ("ipx:     %s\n", my_ipx_address);
	print_fn ("map:     %s\n", sv.name);
	print_fn ("players: %i active (%i max)\n\n", net_activeconnections, svs.maxclients);
	for (j = 0, client = svs.clients; j < svs.maxclients; j++, client++)
	{
		if (!client->active)
			continue;
		if (client->netconnection)
			seconds = (int)(net_time - NET_QSocketGetTime (client->netconnection));
		else
			seconds = 0;
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
		print_fn ("#%-2u %-16.16s  %3i  %2i:%02i:%02i\n", j + 1, client->name, (int)client->edict->v.frags, hours, minutes, seconds);
		if (cmd_source != src_client)
			print_fn ("   %s\n", client->netconnection ? NET_QSocketGetTrueAddressString (client->netconnection) : "botclient");
		else
			print_fn ("   %s\n", client->netconnection ? NET_QSocketGetMaskedAddressString (client->netconnection) : "botclient");
	}
}

/*
==================
Host_God_f

Sets client to godmode
==================
*/
static void Host_God_f (void)
{
	if (cmd_source != src_client)
	{
		Cmd_ForwardToServer ();
		return;
	}

	if (pr_global_struct->deathmatch)
		return;

	// johnfitz -- allow user to explicitly set god mode to on or off
	switch (Cmd_Argc ())
	{
	case 1:
		sv_player->v.flags = (int)sv_player->v.flags ^ FL_GODMODE;
		if (!((int)sv_player->v.flags & FL_GODMODE))
			SV_ClientPrintf ("godmode OFF\n");
		else
			SV_ClientPrintf ("godmode ON\n");
		break;
	case 2:
		if (atof (Cmd_Argv (1)))
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
		Con_Printf ("god [value] : toggle god mode. values: 0 = off, 1 = on\n");
		break;
	}
	// johnfitz
}

/*
==================
Host_Notarget_f
==================
*/
static void Host_Notarget_f (void)
{
	if (cmd_source != src_client)
	{
		Cmd_ForwardToServer ();
		return;
	}

	if (pr_global_struct->deathmatch)
		return;

	// johnfitz -- allow user to explicitly set notarget to on or off
	switch (Cmd_Argc ())
	{
	case 1:
		sv_player->v.flags = (int)sv_player->v.flags ^ FL_NOTARGET;
		if (!((int)sv_player->v.flags & FL_NOTARGET))
			SV_ClientPrintf ("notarget OFF\n");
		else
			SV_ClientPrintf ("notarget ON\n");
		break;
	case 2:
		if (atof (Cmd_Argv (1)))
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
		Con_Printf ("notarget [value] : toggle notarget mode. values: 0 = off, 1 = on\n");
		break;
	}
	// johnfitz
}

qboolean noclip_anglehack;

/*
==================
Host_Noclip_f
==================
*/
static void Host_Noclip_f (void)
{
	if (cmd_source != src_client)
	{
		Cmd_ForwardToServer ();
		return;
	}

	if (pr_global_struct->deathmatch)
		return;

	// johnfitz -- allow user to explicitly set noclip to on or off
	switch (Cmd_Argc ())
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
		if (atof (Cmd_Argv (1)))
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
		Con_Printf ("noclip [value] : toggle noclip mode. values: 0 = off, 1 = on\n");
		break;
	}
	// johnfitz
}

/*
====================
Host_SetPos_f

adapted from fteqw, originally by Alex Shadowalker
====================
*/
static void Host_SetPos_f (void)
{
	if (cmd_source != src_client)
	{
		Cmd_ForwardToServer ();
		return;
	}

	if (pr_global_struct->deathmatch)
		return;

	if (Cmd_Argc () != 7 && Cmd_Argc () != 4)
	{
		SV_ClientPrintf ("usage:\n");
		SV_ClientPrintf ("   setpos <x> <y> <z>\n");
		SV_ClientPrintf ("   setpos <x> <y> <z> <pitch> <yaw> <roll>\n");
		SV_ClientPrintf ("current values:\n");
		SV_ClientPrintf (
			"   %i %i %i %i %i %i\n", (int)sv_player->v.origin[0], (int)sv_player->v.origin[1], (int)sv_player->v.origin[2], (int)sv_player->v.v_angle[0],
			(int)sv_player->v.v_angle[1], (int)sv_player->v.v_angle[2]);
		return;
	}

	if (sv_player->v.movetype != MOVETYPE_NOCLIP)
	{
		noclip_anglehack = true;
		sv_player->v.movetype = MOVETYPE_NOCLIP;
		SV_ClientPrintf ("noclip ON\n");
	}

	// make sure they're not going to whizz away from it
	sv_player->v.velocity[0] = 0;
	sv_player->v.velocity[1] = 0;
	sv_player->v.velocity[2] = 0;

	sv_player->v.origin[0] = atof (Cmd_Argv (1));
	sv_player->v.origin[1] = atof (Cmd_Argv (2));
	sv_player->v.origin[2] = atof (Cmd_Argv (3));

	if (Cmd_Argc () == 7)
	{
		sv_player->v.angles[0] = atof (Cmd_Argv (4));
		sv_player->v.angles[1] = atof (Cmd_Argv (5));
		sv_player->v.angles[2] = atof (Cmd_Argv (6));
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
static void Host_Fly_f (void)
{
	if (cmd_source != src_client)
	{
		Cmd_ForwardToServer ();
		return;
	}

	if (pr_global_struct->deathmatch)
		return;

	// johnfitz -- allow user to explicitly set noclip to on or off
	switch (Cmd_Argc ())
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
		if (atof (Cmd_Argv (1)))
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
		Con_Printf ("fly [value] : toggle fly mode. values: 0 = off, 1 = on\n");
		break;
	}
	// johnfitz
}

/*
==================
Host_Ping_f

==================
*/
static void Host_Ping_f (void)
{
	int		  i, j;
	float	  total;
	client_t *client;

	if (cmd_source != src_client)
	{
		Cmd_ForwardToServer ();
		return;
	}

	SV_ClientPrintf ("Client ping times:\n");
	for (i = 0, client = svs.clients; i < svs.maxclients; i++, client++)
	{
		if (!client->spawned || !client->netconnection)
			continue;
		total = 0;
		for (j = 0; j < NUM_PING_TIMES; j++)
			total += client->ping_times[j];
		total /= NUM_PING_TIMES;
		SV_ClientPrintf ("%4i %s\n", (int)(total * 1000), client->name);
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
static void Host_Map_f (void)
{
	int	 i;
	char name[MAX_QPATH], *p;

	if (Cmd_Argc () < 2) // no map name given
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

	cls.demonum = -1; // stop demo loop in case this fails

	CL_Disconnect ();
	Host_ShutdownServer (false);

	if (cls.state != ca_dedicated)
		IN_Activate ();
	key_dest = key_game; // remove console or menu
	SCR_BeginLoadingPlaque ();

	svs.serverflags = 0; // haven't completed an episode yet
	q_strlcpy (name, Cmd_Argv (1), sizeof (name));
	// remove (any) trailing ".bsp" from mapname -- S.A.
	p = strstr (name, ".bsp");
	if (p && p[4] == '\0')
		*p = '\0';
	PR_SwitchQCVM (&sv.qcvm);
	SV_SpawnServer (name);
	PR_SwitchQCVM (NULL);
	if (!sv.active)
		return;

	if (cls.state != ca_dedicated)
	{
		memset (cls.spawnparms, 0, MAX_MAPSTRING);
		for (i = 2; i < Cmd_Argc (); i++)
		{
			q_strlcat (cls.spawnparms, Cmd_Argv (i), MAX_MAPSTRING);
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
static void Host_Randmap_f (void)
{
	int				 i, randlevel, numlevels;
	filelist_item_t *level;

	if (cmd_source != src_command)
		return;

	for (level = extralevels, numlevels = 0; level; level = level->next)
		numlevels++;

	if (numlevels == 0)
	{
		Con_Printf ("no maps\n");
		return;
	}

	randlevel = (rand () % numlevels);

	for (level = extralevels, i = 0; level; level = level->next, i++)
	{
		if (i == randlevel)
		{
			Con_Printf ("Starting map %s...\n", level->name);
			Cbuf_AddText (va ("map %s\n", level->name));
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
static void Host_Changelevel_f (void)
{
	char level[MAX_QPATH];

	if (Cmd_Argc () != 2)
	{
		Con_Printf ("changelevel <levelname> : continue game on a new level\n");
		return;
	}
	if (!sv.active || cls.demoplayback)
	{
		Con_Printf ("Only the server may changelevel\n");
		return;
	}

	if (autoload.value && sv.lastsave[0] && !q_strcasecmp (sv.name, Cmd_Argv (1)) && current_skill == (int)(skill.value + 0.5) && svs.maxclients == 1 &&
		!cl.intermission && svs.clients[0].active && (svs.clients[0].edict->v.health <= 0))
	{
		Cbuf_AddText ("\nrestart\n");
		return;
	}

	// johnfitz -- check for client having map before anything else
	q_snprintf (level, sizeof (level), "maps/%s.bsp", Cmd_Argv (1));
	if (!COM_FileExists (level, NULL))
		Host_Error ("cannot find map %s", level);
	// johnfitz

	if (cls.state != ca_dedicated)
		IN_Activate ();	 // -- S.A.
	key_dest = key_game; // remove console or menu
	PR_SwitchQCVM (&sv.qcvm);
	SV_SaveSpawnparms ();
	q_strlcpy (level, Cmd_Argv (1), sizeof (level));
	SV_SpawnServer (level);
	PR_SwitchQCVM (NULL);
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
static void Host_Restart_f (void)
{
	char mapname[MAX_QPATH];

	if (cls.demoplayback || !sv.active)
		return;

	if (cmd_source != src_command)
		return;

	if (autoload.value && sv.lastsave[0] && q_strcasecmp (Cmd_Argv (1), "noload") && q_strcasecmp (Cmd_Argv (1), "force"))
	{
		Cbuf_AddText (va ("-use;-jump;-attack;%sload \"%s\"\n", autoload.value >= 2 ? "fast" : "", sv.lastsave));
		svs.changelevel_issued = false;
		return;
	}

	q_strlcpy (mapname, sv.name, sizeof (mapname)); // mapname gets cleared in spawnserver
	PR_SwitchQCVM (&sv.qcvm);
	SV_SpawnServer (mapname);
	PR_SwitchQCVM (NULL);
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
static void Host_Reconnect_f (void)
{
	if (cls.demoplayback) // cross-map demo playback fix from Baker
		return;

	if (key_dest == key_game)
		IN_Activate ();
	SCR_BeginLoadingPlaque ();
	cls.signon = 0; // need new connection messages
}

/*
=====================
Host_Connect_f

User command to connect to server
=====================
*/
static void Host_Connect_f (void)
{
	char name[MAX_QPATH];

	cls.demonum = -1; // stop demo loop in case this fails
	if (cls.demoplayback)
	{
		CL_StopPlayback ();
		CL_Disconnect ();
	}
	q_strlcpy (name, Cmd_Argv (1), sizeof (name));
	CL_EstablishConnection (name);
	Host_Reconnect_f ();
}

/*
===============================================================================

LOAD / SAVE GAME

===============================================================================
*/

#define SAVEGAME_VERSION 5

/*
===============
Host_SavegameComment

Writes a SAVEGAME_COMMENT_LENGTH character comment describing the current
===============
*/
static void Host_SavegameComment (char *text)
{
	int	  i;
	char  kills[20];
	char *p1, *p2;

	for (i = 0; i < SAVEGAME_COMMENT_LENGTH; i++)
		text[i] = ' ';

	// Remove CR/LFs from level name to avoid broken saves, e.g. with autumn_sp map:
	// https://celephais.net/board/view_thread.php?id=60452&start=3666
	p1 = strchr (cl.levelname, '\n');
	p2 = strchr (cl.levelname, '\r');
	if (p1 != NULL)
		*p1 = 0;
	if (p2 != NULL)
		*p2 = 0;

	memcpy (text, cl.levelname, q_min (strlen (cl.levelname), 22)); // johnfitz -- only copy 22 chars.
	q_snprintf (kills, sizeof (kills), "kills:%3i/%3i", cl.stats[STAT_MONSTERS], cl.stats[STAT_TOTALMONSTERS]);
	memcpy (text + 22, kills, strlen (kills));
	// convert space to _ to make stdio happy
	for (i = 0; i < SAVEGAME_COMMENT_LENGTH; i++)
	{
		if (text[i] == ' ')
			text[i] = '_';
	}
	if (p1 != NULL)
		*p1 = '\n';
	if (p2 != NULL)
		*p2 = '\r';
	text[SAVEGAME_COMMENT_LENGTH] = '\0';
}

/*
===============
Host_Savegame_f
===============
*/
static void Host_Savegame_f (void)
{
	char  name[MAX_OSPATH];
	FILE *f;
	int	  i;
	char  comment[SAVEGAME_COMMENT_LENGTH + 1];

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

	if (Cmd_Argc () != 2)
	{
		Con_Printf ("save <savename> : save a game\n");
		return;
	}

	if (strstr (Cmd_Argv (1), ".."))
	{
		Con_Printf ("Relative pathnames are not allowed.\n");
		return;
	}

	for (i = 0; i < svs.maxclients; i++)
	{
		if (svs.clients[i].active && (svs.clients[i].edict->v.health <= 0))
		{
			Con_Printf ("Can't savegame with a dead player\n");
			return;
		}
	}

	if (multiuser)
	{
		char *save_path = SDL_GetPrefPath ("vkQuake", COM_GetGameNames (true));
		q_snprintf (name, sizeof (name), "%s%s", save_path, Cmd_Argv (1));
		SDL_free (save_path);
	}
	else
		q_snprintf (name, sizeof (name), "%s/%s", com_gamedir, Cmd_Argv (1));
	COM_AddExtension (name, ".sav", sizeof (name));

	Con_Printf ("Saving game to %s...\n", name);
	f = fopen (name, "w");
	if (!f)
	{
		Con_Printf ("ERROR: couldn't open.\n");
		return;
	}

	PR_SwitchQCVM (&sv.qcvm);

	fprintf (f, "%i\n", SAVEGAME_VERSION);
	Host_SavegameComment (comment);
	fprintf (f, "%s\n", comment);
	for (i = 0; i < NUM_BASIC_SPAWN_PARMS; i++)
		fprintf (f, "%f\n", svs.clients->spawn_parms[i]);
	fprintf (f, "%d\n", current_skill);
	fprintf (f, "%s\n", sv.name);
	fprintf (f, "%f\n", qcvm->time);

	// write the light styles
	for (i = 0; i < MAX_LIGHTSTYLES; i++)
	{
		if (sv.lightstyles[i])
			fprintf (f, "%s\n", sv.lightstyles[i]);
		else
			fprintf (f, "m\n");
	}

	ED_WriteGlobals (f);
	for (i = 0; i < qcvm->num_edicts; i++)
		ED_Write (f, EDICT_NUM (i));
	for (; i < qcvm->min_edicts; i++)
		fprintf (f, "{\n}\n");

	// add extra info (lightstyles, precaches, etc) in a way that's supposed to be compatible with DP.
	// sidenote - this provides extended lightstyles and support for late precaches
	// it does NOT protect against spawnfunc precache changes - we would need to include makestatics here too (and optionally baselines, or just recalculate
	// those).
	fprintf (f, "/*\n");
	fprintf (f, "// QuakeSpasm extended savegame\n");
	for (i = MAX_LIGHTSTYLES; i < MAX_LIGHTSTYLES; i++)
	{
		if (sv.lightstyles[i])
			fprintf (f, "sv.lightstyles %i \"%s\"\n", i, sv.lightstyles[i]);
	}
	for (i = 1; i < MAX_MODELS; i++)
	{
		if (sv.model_precache[i])
			fprintf (f, "sv.model_precache %i \"%s\"\n", i, sv.model_precache[i]);
	}
	for (i = 1; i < MAX_SOUNDS; i++)
	{
		if (sv.sound_precache[i])
			fprintf (f, "sv.sound_precache %i \"%s\"\n", i, sv.sound_precache[i]);
	}
	for (i = 1; i < MAX_PARTICLETYPES; i++)
	{
		if (sv.particle_precache[i])
			fprintf (f, "sv.particle_precache %i \"%s\"\n", i, sv.particle_precache[i]);
	}

	fprintf (f, "sv.serverflags %i\n", svs.serverflags);
	for (i = NUM_BASIC_SPAWN_PARMS; i < NUM_TOTAL_SPAWN_PARMS; i++)
	{
		if (svs.clients->spawn_parms[i])
			fprintf (f, "spawnparm %i \"%f\"\n", i + 1, svs.clients->spawn_parms[i]);
	}

	const char *fog_cmd = Fog_GetFogCommand (true);
	if (fog_cmd)
		fprintf (f, "%s", &fog_cmd[1]);

	const char *sky_cmd = Sky_GetSkyCommand (true);
	if (sky_cmd)
		fprintf (f, "%s", &sky_cmd[1]);

	fprintf (f, "*/\n");

	fclose (f);
	Con_Printf ("done.\n");
	PR_SwitchQCVM (NULL);
	SaveList_Rebuild ();

	if (strlen (Cmd_Argv (1)) < sizeof (sv.lastsave) - 1)
		strcpy (sv.lastsave, Cmd_Argv (1));
}

static void Send_Spawn_Info (client_t *c, qboolean loadgame)
{
	int		  i;
	client_t *client;
	edict_t	 *ent;

	// send all current names, colors, and frag counts
	SZ_Clear (&c->message);

	// send time of update
	MSG_WriteByte (&c->message, svc_time);
	MSG_WriteFloat (&c->message, qcvm->time);
	if (c->protocol_pext2 & PEXT2_PREDINFO)
		MSG_WriteShort (&c->message, (c->lastmovemessage & 0xffff));

	for (i = 0, client = svs.clients; i < svs.maxclients; i++, client++)
	{
		if (!client->knowntoqc)
			continue;

		MSG_WriteByte (&c->message, svc_updatename);
		MSG_WriteByte (&c->message, i);
		MSG_WriteString (&c->message, client->name);
		MSG_WriteByte (&c->message, svc_updatecolors);
		MSG_WriteByte (&c->message, i);
		MSG_WriteByte (&c->message, client->colors);

		MSG_WriteByte (&c->message, svc_updatefrags);
		MSG_WriteByte (&c->message, i);
		MSG_WriteShort (&c->message, client->old_frags);
	}

	// send all current light styles
	for (i = 0; i < MAX_LIGHTSTYLES; i++)
	{
		MSG_WriteByte (&c->message, svc_lightstyle);
		MSG_WriteByte (&c->message, (char)i);
		MSG_WriteString (&c->message, sv.lightstyles[i]);
	}

	//
	// send some stats
	//
	MSG_WriteByte (&c->message, svc_updatestat);
	MSG_WriteByte (&c->message, STAT_TOTALSECRETS);
	MSG_WriteLong (&c->message, pr_global_struct->total_secrets);

	MSG_WriteByte (&c->message, svc_updatestat);
	MSG_WriteByte (&c->message, STAT_TOTALMONSTERS);
	MSG_WriteLong (&c->message, pr_global_struct->total_monsters);

	MSG_WriteByte (&c->message, svc_updatestat);
	MSG_WriteByte (&c->message, STAT_SECRETS);
	MSG_WriteLong (&c->message, pr_global_struct->found_secrets);

	MSG_WriteByte (&c->message, svc_updatestat);
	MSG_WriteByte (&c->message, STAT_MONSTERS);
	MSG_WriteLong (&c->message, pr_global_struct->killed_monsters);

	//
	// send a fixangle
	// Never send a roll angle, because savegames can catch the server
	// in a state where it is expecting the client to correct the angle
	// and it won't happen if the game was just loaded, so you wind up
	// with a permanent head tilt
	ent = EDICT_NUM (1 + (c - svs.clients));
	MSG_WriteByte (&c->message, svc_setangle);
	for (i = 0; i < 2; i++)
		if (loadgame)
			MSG_WriteAngle (&c->message, ent->v.v_angle[i], sv.protocolflags);
		else
			MSG_WriteAngle (&c->message, ent->v.angles[i], sv.protocolflags);
	MSG_WriteAngle (&c->message, 0, sv.protocolflags);

	if (!(c->protocol_pext2 & PEXT2_REPLACEMENTDELTAS))
		SV_WriteClientdataToMessage (c, &c->message);
}

/*
===============
Host_Loadgame_f
===============
*/
static void Host_Loadgame_f (void)
{
	static char *start;

	char		name[MAX_OSPATH];
	char		mapname[MAX_QPATH];
	float		time, tfloat;
	const char *data;
	int			i;
	edict_t	   *ent;
	int			entnum, lastusedent;
	int			version;
	float		spawn_parms[NUM_TOTAL_SPAWN_PARMS];
	qboolean	was_recording = cls.demorecording;
	int			old_skill = current_skill;
	qboolean	fastload = !!strstr (Cmd_Argv (0), "fast") || autofastload.value;

	if (cmd_source != src_command)
		return;

	if (Cmd_Argc () != 2)
	{
		Con_Printf ("%s <savename> : load a game\n", Cmd_Argv (0));
		return;
	}

	if (strstr (Cmd_Argv (1), ".."))
	{
		Con_Printf ("Relative pathnames are not allowed.\n");
		return;
	}

	cls.demonum = -1; // stop demo loop in case this fails

	char	*save_path = multiuser ? SDL_GetPrefPath ("vkQuake", COM_GetGameNames (true)) : NULL;
	qboolean loadable = false;
	for (int j = (multiuser ? 0 : 1); j < 2; ++j)
	{
		if (j == 0)
			q_snprintf (name, sizeof (name), "%s%s", save_path, Cmd_Argv (1));
		else
			q_snprintf (name, sizeof (name), "%s/%s", com_gamedir, Cmd_Argv (1));
		COM_AddExtension (name, ".sav", sizeof (name));

		// avoid leaking if the previous Host_Loadgame_f failed with a Host_Error
		if (start != NULL)
			Mem_Free (start);

		start = (char *)COM_LoadMallocFile_TextMode_OSPath (name, NULL);
		if (start)
		{
			loadable = true;
			break;
		}
	}
	SDL_free (save_path);

	if (!loadable)
	{
		SCR_EndLoadingPlaque ();
		Con_Printf ("ERROR: couldn't open.\n");
		return;
	}

	// we can't call SCR_BeginLoadingPlaque, because too much stack space has
	// been used.  The menu calls it before stuffing loadgame command
	//	SCR_BeginLoadingPlaque ();

	Con_Printf ("Loading game from %s...\n", name);

	data = start;
	data = COM_ParseIntNewline (data, &version);
	if (version != SAVEGAME_VERSION)
	{
		Mem_Free (start);
		start = NULL;
		Host_Error ("Savegame is version %i, not %i", version, SAVEGAME_VERSION);
		return;
	}
	data = COM_ParseStringNewline (data);
	for (i = 0; i < NUM_BASIC_SPAWN_PARMS; i++)
		data = COM_ParseFloatNewline (data, &spawn_parms[i]);
	for (; i < NUM_TOTAL_SPAWN_PARMS; i++)
		spawn_parms[i] = 0;
	// this silliness is so we can load 1.06 save files, which have float skill values
	data = COM_ParseFloatNewline (data, &tfloat);
	current_skill = (int)(tfloat + 0.1);
	Cvar_SetValue ("skill", (float)current_skill);

	data = COM_ParseStringNewline (data);
	q_strlcpy (mapname, com_token, sizeof (mapname));
	data = COM_ParseFloatNewline (data, &time);

	if (fastload && (!sv.active || cls.signon != SIGNONS || svs.maxclients != 1))
	{
		Con_Printf ("Not in a local singleplayer game - can't fastload\n");
		fastload = 0;
	}
	if (fastload && (strcmp (mapname, sv.name) || current_skill != old_skill))
	{
		Con_Printf ("Can't fastload (%s skill %d vs %s skill %d)\n", mapname, current_skill, sv.name, old_skill);
		fastload = 0;
	}
	if (fastload && cl.intermission)
	{
		// we could if we reset cl.intermission and the music, but some mods still struggle
		Con_Printf ("Can't fastload during an intermission\n");
		fastload = 0;
	}

	if (!fastload)
		CL_Disconnect_f ();
	else if (cls.demorecording) // demo playback can't deal with backward timestamps, so record a map change
		CL_Stop_f ();

	PR_SwitchQCVM (&sv.qcvm);
	if (!fastload)
		SV_SpawnServer (mapname);

	if (!sv.active)
	{
		PR_SwitchQCVM (NULL);
		Mem_Free (start);
		start = NULL;
		SCR_EndLoadingPlaque ();
		Con_Printf ("Couldn't load map\n");
		return;
	}
	if (!fastload)
	{
		sv.paused = true; // pause until all clients connect
		sv.loadgame = true;
	}
	else
		S_StopAllSounds (true, true); // do this before parsing the edicts, since that may take a while

	if (was_recording)
		CL_Resume_Record (fastload);

	// load the light styles
	for (i = 0; i < MAX_LIGHTSTYLES; i++)
	{
		data = COM_ParseStringNewline (data);
		sv.lightstyles[i] = (const char *)q_strdup (com_token);
	}

	if (fastload) // can be done for normal loads too, but keep the previous behavior
		PR_ClearEdictStrings ();

	// load the edicts out of the savegame file
	qcvm->time = 0;			   // mark freed edicts for immediate reuse
	entnum = lastusedent = -1; // -1 is the globals
	while (*data)
	{
		while (*data == ' ' || *data == '\r' || *data == '\n')
			data++;
		if (data[0] == '/' && data[1] == '*' && (data[2] == '\r' || data[2] == '\n'))
		{ // looks like an extended saved game
			char	   *end;
			const char *ext;
			ext = data + 2;
			while ((end = strchr (ext, '\n')))
			{
				*end = 0;
				ext = COM_Parse (ext);
				if (!strcmp (com_token, "sv.lightstyles"))
				{
					int idx;
					ext = COM_Parse (ext);
					idx = atoi (com_token);
					ext = COM_Parse (ext);
					if (idx >= 0 && idx < MAX_LIGHTSTYLES)
					{
						if (*com_token)
							sv.lightstyles[idx] = (const char *)q_strdup (com_token);
						else
							sv.lightstyles[idx] = NULL;
					}
				}
				else if (!strcmp (com_token, "sv.model_precache"))
				{
					int idx;
					ext = COM_Parse (ext);
					idx = atoi (com_token);
					ext = COM_Parse (ext);
					if (idx >= 1 && idx < MAX_MODELS)
					{
						sv.model_precache[idx] = (const char *)q_strdup (com_token);
						sv.models[idx] = Mod_ForName (sv.model_precache[idx], idx == 1);
						// if (idx == 1)
						//	sv.worldmodel = sv.models[idx];
					}
				}
				else if (!strcmp (com_token, "sv.sound_precache"))
				{
					int idx;
					ext = COM_Parse (ext);
					idx = atoi (com_token);
					ext = COM_Parse (ext);
					if (idx >= 1 && idx < MAX_MODELS)
						sv.sound_precache[idx] = (const char *)q_strdup (com_token);
				}
				else if (!strcmp (com_token, "sv.particle_precache"))
				{
					int idx;
					ext = COM_Parse (ext);
					idx = atoi (com_token);
					ext = COM_Parse (ext);
					if (idx >= 1 && idx < MAX_PARTICLETYPES)
					{
						Mem_Free (sv.particle_precache[idx]);
						sv.particle_precache[idx] = (const char *)q_strdup (com_token);
					}
				}
				else if (!strcmp (com_token, "sv.serverflags") || !strcmp (com_token, "svs.serverflags"))
				{
					int fl;
					ext = COM_Parse (ext);
					fl = atoi (com_token);
					svs.serverflags = fl;
				}
				else if (!strcmp (com_token, "spawnparm"))
				{
					int idx;
					ext = COM_Parse (ext);
					idx = atoi (com_token);
					ext = COM_Parse (ext);
					if (idx >= 1 && idx <= NUM_TOTAL_SPAWN_PARMS)
						spawn_parms[idx - 1] = atof (com_token);
				}
				else if (!strcmp (com_token, "fog") && fastload)
				{
					float d, r, g, b;
					ext = COM_Parse (ext);
					d = atof (com_token);
					ext = COM_Parse (ext);
					r = atof (com_token);
					ext = COM_Parse (ext);
					g = atof (com_token);
					ext = COM_Parse (ext);
					b = atof (com_token);
					Fog_Update (d, r, g, b, 0.0f);
				}
				else if (!strcmp (com_token, "sky") && fastload)
				{
					ext = COM_Parse (ext);
					Sky_LoadSkyBox (com_token);
				}
				else if (!strcmp (com_token, "skyfog") && fastload)
				{
					ext = COM_Parse (ext);
					Sky_SetSkyfog (atof (com_token));
				}
				*end = '\n';
				ext = end + 1;
			}
		}

		data = COM_Parse (data);
		if (!com_token[0])
			break; // end of file
		if (strcmp (com_token, "{"))
		{
			Host_Error ("First token isn't a brace");
		}

		if (entnum == -1)
		{ // parse the global vars
			data = ED_ParseGlobals (data);
		}
		else
		{ // parse an edict
			ent = EDICT_NUM (entnum);
			if (entnum < qcvm->num_edicts)
			{
				if (ent->free)
					ED_RemoveFromFreeList (ent);
				ent->free = false;
				ent->next_free = NULL;
				ent->prev_free = NULL;
				memset (&ent->v, 0, qcvm->progs->entityfields * 4);
			}
			else
			{
				memset (ent, 0, qcvm->edict_size);
				ent->baseline = nullentitystate;
			}
			data = ED_ParseEdict (data, ent);

			// link it into the bsp tree
			if (!ent->free)
			{
				SV_LinkEdict (ent, false);
				lastusedent = entnum;
			}
		}

		entnum++;
	}

	for (i = lastusedent + 1; i < q_max (qcvm->num_edicts, entnum); i++)
	{
		ED_Free (EDICT_NUM (i));
		ED_RemoveFromFreeList (EDICT_NUM (i));
		memset (EDICT_NUM (i), 0, qcvm->edict_size);
	}
	qcvm->time = time;
	qcvm->num_edicts = lastusedent + 1;

	if (fastload)
	{
		sv.lastchecktime = 0.0;
		memset (cl_dlights, 0, sizeof (cl_dlights));
		memset (cl_temp_entities, 0, sizeof (cl_temp_entities));
		memset (cl_beams, 0, sizeof (cl_beams));
		V_ResetBlend ();
		Fog_ResetFade ();
		R_ClearParticles ();
#ifdef PSET_SCRIPT
		PScript_ClearParticles (false);
#endif
		SCR_CenterPrintClear ();

		Send_Spawn_Info (svs.clients, true);
	}

	Mem_Free (start);
	start = NULL;

	for (i = 0; i < NUM_TOTAL_SPAWN_PARMS; i++)
		svs.clients->spawn_parms[i] = spawn_parms[i];

	PR_SwitchQCVM (NULL);

	if (cls.state != ca_dedicated && !fastload)
	{
		CL_EstablishConnection ("local");
		Host_Reconnect_f ();
	}
	else
		SCR_EndLoadingPlaque ();

	if (strlen (Cmd_Argv (1)) < sizeof (sv.lastsave) - 1)
		strcpy (sv.lastsave, Cmd_Argv (1));
}

//============================================================================

/*
======================
Host_Name_f
======================
*/
static void Host_Name_f (void)
{
	char newName[32];

	if (Cmd_Argc () == 1)
	{
		Con_Printf ("\"name\" is \"%s\"\n", cl_name.string);
		return;
	}
	if (Cmd_Argc () == 2)
		q_strlcpy (newName, Cmd_Argv (1), sizeof (newName));
	else
		q_strlcpy (newName, Cmd_Args (), sizeof (newName));
	newName[15] = 0; // client_t structure actually says name[32].

	if (cmd_source != src_client)
	{
		if (strcmp (cl_name.string, newName) == 0)
			return;
		Cvar_Set ("_cl_name", newName);
		if (cls.state == ca_connected)
			Cmd_ForwardToServer ();
		return;
	}

	if (host_client->name[0] && strcmp (host_client->name, "unconnected"))
	{
		if (strcmp (host_client->name, newName) != 0)
			Con_Printf ("%s renamed to %s\n", host_client->name, newName);
	}
	strcpy (host_client->name, newName);
	host_client->edict->v.netname = PR_SetEngineString (host_client->name);

	// send notification to all clients
	MSG_WriteByte (&sv.reliable_datagram, svc_updatename);
	MSG_WriteByte (&sv.reliable_datagram, host_client - svs.clients);
	MSG_WriteString (&sv.reliable_datagram, host_client->name);
}

static void Host_Say (qboolean teamonly)
{
	int			j;
	client_t   *client;
	client_t   *save;
	const char *p;
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

	p = Cmd_Args ();
	// remove quotes if present
	quoted = false;
	if (*p == '\"')
	{
		p++;
		quoted = true;
	}
	// turn on color set 1
	if (!fromServer)
		q_snprintf (text, sizeof (text), "\001%s: %s", save->name, p);
	else
		q_snprintf (text, sizeof (text), "\001<%s> %s", hostname.string, p);

	// check length & truncate if necessary
	j = (int)strlen (text);
	if (j >= (int)sizeof (text) - 1)
	{
		text[sizeof (text) - 2] = '\n';
		text[sizeof (text) - 1] = '\0';
	}
	else
	{
		p2 = text + j;
		while ((const char *)p2 > (const char *)text && (p2[-1] == '\r' || p2[-1] == '\n' || (p2[-1] == '\"' && quoted)))
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
		SV_ClientPrintf ("%s", text);
	}
	host_client = save;

	if (cls.state == ca_dedicated)
		Sys_Printf ("%s", &text[1]);
}

static void Host_Say_f (void)
{
	Host_Say (false);
}

static void Host_Say_Team_f (void)
{
	Host_Say (true);
}

static void Host_Tell_f (void)
{
	int			j;
	client_t   *client;
	client_t   *save;
	const char *p;
	char		text[MAXCMDLINE], *p2;
	qboolean	quoted;

	if (cmd_source != src_client)
	{
		Cmd_ForwardToServer ();
		return;
	}

	if (Cmd_Argc () < 3)
		return;

	p = Cmd_Args ();
	// remove quotes if present
	quoted = false;
	if (*p == '\"')
	{
		p++;
		quoted = true;
	}
	q_snprintf (text, sizeof (text), "%s: %s", host_client->name, p);

	// check length & truncate if necessary
	j = (int)strlen (text);
	if (j >= (int)sizeof (text) - 1)
	{
		text[sizeof (text) - 2] = '\n';
		text[sizeof (text) - 1] = '\0';
	}
	else
	{
		p2 = text + j;
		while ((const char *)p2 > (const char *)text && (p2[-1] == '\r' || p2[-1] == '\n' || (p2[-1] == '\"' && quoted)))
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
		if (q_strcasecmp (client->name, Cmd_Argv (1)))
			continue;
		host_client = client;
		SV_ClientPrintf ("%s", text);
		break;
	}
	host_client = save;
}

/*
==================
Host_Color_f
==================
*/
static void Host_Color_f (void)
{
	int top, bottom;
	int playercolor;

	if (Cmd_Argc () == 1)
	{
		Con_Printf ("\"color\" is \"%i %i\"\n", ((int)cl_color.value) >> 4, ((int)cl_color.value) & 0x0f);
		Con_Printf ("color <0-13> [0-13]\n");
		return;
	}

	if (Cmd_Argc () == 2)
		top = bottom = atoi (Cmd_Argv (1));
	else
	{
		top = atoi (Cmd_Argv (1));
		bottom = atoi (Cmd_Argv (2));
	}

	top &= 15;
	if (top > 13)
		top = 13;
	bottom &= 15;
	if (bottom > 13)
		bottom = 13;

	playercolor = top * 16 + bottom;

	if (cmd_source != src_client)
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
static void Host_Kill_f (void)
{
	if (cmd_source != src_client)
	{
		Cmd_ForwardToServer ();
		return;
	}

	if (sv_player->v.health <= 0)
	{
		SV_ClientPrintf ("Can't suicide -- already dead!\n");
		return;
	}

	pr_global_struct->time = qcvm->time;
	pr_global_struct->self = EDICT_TO_PROG (sv_player);
	PR_ExecuteProgram (pr_global_struct->ClientKill);
}

/*
==================
Host_Pause_f
==================
*/
static void Host_Pause_f (void)
{
	// ericw -- demo pause support (inspired by MarkV)
	if (cls.demoplayback)
	{
		cls.demopaused = !cls.demopaused;
		cl.paused = cls.demopaused;
		if (cls.demospeed == 0.f && !cls.demopaused)
			cls.demospeed = 1.f;
		return;
	}

	if (cmd_source != src_client)
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
			SV_BroadcastPrintf ("%s paused the game\n", PR_GetString (sv_player->v.netname));
		}
		else
		{
			SV_BroadcastPrintf ("%s unpaused the game\n", PR_GetString (sv_player->v.netname));
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
static void Host_PreSpawn_f (void)
{
	if (cmd_source != src_client)
	{
		Con_Printf ("prespawn is not valid from the console\n");
		return;
	}

	if (host_client->spawned)
	{
		Con_Printf ("prespawn not valid -- already spawned\n");
		return;
	}

	// will start splurging out prespawn data
	host_client->sendsignon = 2;
	host_client->signonidx = 0;
}

/*
==================
Host_Spawn_f
==================
*/
static void Host_Spawn_f (void)
{
	int		 i;
	edict_t *ent;

	if (cmd_source != src_client)
	{
		Con_Printf ("spawn is not valid from the console\n");
		return;
	}

	if (host_client->spawned)
	{
		Con_Printf ("Spawn not valid -- already spawned\n");
		return;
	}

	host_client->knowntoqc = true;
	host_client->lastmovetime = qcvm->time;
	// run the entrance script
	if (sv.loadgame)
	{ // loaded games are fully inited already
		// if this is the last client to be connected, unpause
		sv.paused = false;
	}
	else
	{
		// set up the edict
		ent = host_client->edict;

		memset (&ent->v, 0, qcvm->progs->entityfields * 4);
		ent->v.colormap = NUM_FOR_EDICT (ent);
		ent->v.team = (host_client->colors & 15) + 1;
		ent->v.netname = PR_SetEngineString (host_client->name);

		// copy spawn parms out of the client_t
		for (i = 0; i < NUM_BASIC_SPAWN_PARMS; i++)
			(&pr_global_struct->parm1)[i] = host_client->spawn_parms[i];
		if (pr_checkextension.value)
		{ // extended spawn parms
			for (; i < NUM_TOTAL_SPAWN_PARMS; i++)
			{
				ddef_t *g = ED_FindGlobal (va ("parm%i", i + 1));
				if (g)
					qcvm->globals[g->ofs] = host_client->spawn_parms[i];
			}
		}
		// call the spawn function
		pr_global_struct->time = qcvm->time;
		pr_global_struct->self = EDICT_TO_PROG (sv_player);
		PR_ExecuteProgram (pr_global_struct->ClientConnect);

		if ((Sys_DoubleTime () - NET_QSocketGetTime (host_client->netconnection)) <= qcvm->time)
			Sys_Printf ("%s entered the game\n", host_client->name);

		PR_ExecuteProgram (pr_global_struct->PutClientInServer);
	}

	Send_Spawn_Info (host_client, sv.loadgame);

	MSG_WriteByte (&host_client->message, svc_signonnum);
	MSG_WriteByte (&host_client->message, 3);
	host_client->sendsignon = true;
}

/*
==================
Host_Begin_f
==================
*/
static void Host_Begin_f (void)
{
	if (cmd_source != src_client)
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
static void Host_Kick_f (void)
{
	const char *who;
	const char *message = NULL;
	client_t   *save;
	int			i;
	qboolean	byNumber = false;

	if (cmd_source != src_client)
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

	if (Cmd_Argc () > 2 && strcmp (Cmd_Argv (1), "#") == 0)
	{
		i = atof (Cmd_Argv (2)) - 1;
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
			if (q_strcasecmp (host_client->name, Cmd_Argv (1)) == 0)
				break;
		}
	}

	if (i < svs.maxclients)
	{
		if (cmd_source != src_client)
			if (cls.state == ca_dedicated)
				who = "Console";
			else
				who = cl_name.string;
		else
			who = save->name;

		// can't kick yourself!
		if (host_client == save)
			return;

		if (Cmd_Argc () > 2)
		{
			message = COM_Parse (Cmd_Args ());
			if (byNumber)
			{
				message++;				// skip the #
				while (*message == ' ') // skip white space
					message++;
				message += strlen (Cmd_Argv (2)); // skip the number
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
static void Host_Give_f (void)
{
	const char *t;
	int			v;
	eval_t	   *val;

	if (cmd_source != src_client)
	{
		Cmd_ForwardToServer ();
		return;
	}

	if (pr_global_struct->deathmatch)
		return;

	t = Cmd_Argv (1);
	v = atoi (Cmd_Argv (2));

	if (strcmp (t, "all") == 0)
	{
		for (int i = 0; i < 9; ++i)
		{
			if (hipnotic)
				sv_player->v.items = (int)sv_player->v.items | HIT_PROXIMITY_GUN | HIT_LASER_CANNON | HIT_MJOLNIR;
			for (i = 0; i <= 9; ++i)
				sv_player->v.items = (int)sv_player->v.items | (IT_SHOTGUN << i);
			sv_player->v.items = sv_player->v.items - ((int)(sv_player->v.items) & (int)(IT_ARMOR1 | IT_ARMOR2 | IT_ARMOR3)) + IT_ARMOR3;
			sv_player->v.items = (int)sv_player->v.items | (int)(IT_KEY1 | IT_KEY2);
			sv_player->v.ammo_shells = 999;
			sv_player->v.ammo_nails = 999;
			sv_player->v.ammo_rockets = 999;
			sv_player->v.ammo_cells = 999;
			sv_player->v.armortype = 0.8;
			sv_player->v.armorvalue = 200;
		}
	}
	else
	{
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
				val = GetEdictFieldValue (sv_player, ED_FindFieldOffset ("ammo_shells1"));
				if (val)
					val->_float = v;
			}
			sv_player->v.ammo_shells = v;
			break;

		case 'n':
			if (rogue)
			{
				val = GetEdictFieldValue (sv_player, ED_FindFieldOffset ("ammo_nails1"));
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
				val = GetEdictFieldValue (sv_player, ED_FindFieldOffset ("ammo_lava_nails"));
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
				val = GetEdictFieldValue (sv_player, ED_FindFieldOffset ("ammo_rockets1"));
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
				val = GetEdictFieldValue (sv_player, ED_FindFieldOffset ("ammo_multi_rockets"));
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
				val = GetEdictFieldValue (sv_player, ED_FindFieldOffset ("ammo_cells1"));
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
				val = GetEdictFieldValue (sv_player, ED_FindFieldOffset ("ammo_plasma"));
				if (val)
				{
					val->_float = v;
					if (sv_player->v.weapon > IT_LIGHTNING)
						sv_player->v.ammo_cells = v;
				}
			}
			break;

		// johnfitz -- give armour
		case 'a':
			if (v > 150)
			{
				sv_player->v.armortype = 0.8;
				sv_player->v.armorvalue = v;
				sv_player->v.items = sv_player->v.items - ((int)(sv_player->v.items) & (int)(IT_ARMOR1 | IT_ARMOR2 | IT_ARMOR3)) + IT_ARMOR3;
			}
			else if (v > 100)
			{
				sv_player->v.armortype = 0.6;
				sv_player->v.armorvalue = v;
				sv_player->v.items = sv_player->v.items - ((int)(sv_player->v.items) & (int)(IT_ARMOR1 | IT_ARMOR2 | IT_ARMOR3)) + IT_ARMOR2;
			}
			else if (v >= 0)
			{
				sv_player->v.armortype = 0.3;
				sv_player->v.armorvalue = v;
				sv_player->v.items = sv_player->v.items - ((int)(sv_player->v.items) & (int)(IT_ARMOR1 | IT_ARMOR2 | IT_ARMOR3)) + IT_ARMOR1;
			}
			break;
			// johnfitz

		case 'k':
			sv_player->v.items = (int)sv_player->v.items | (int)(IT_KEY1 | IT_KEY2);
			break;
		}
	}

	// johnfitz -- update currentammo to match new ammo (so statusbar updates correctly)
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
	case RIT_LAVA_NAILGUN: // same as IT_AXE
		if (rogue)
			sv_player->v.currentammo = sv_player->v.ammo_nails;
		break;
	case RIT_PLASMA_GUN: // same as HIT_PROXIMITY_GUN
		if (rogue)
			sv_player->v.currentammo = sv_player->v.ammo_cells;
		if (hipnotic)
			sv_player->v.currentammo = sv_player->v.ammo_rockets;
		break;
	}
	// johnfitz
}

static edict_t *FindViewthing (void)
{
	int		 i;
	edict_t *e = NULL;

	PR_SwitchQCVM (&sv.qcvm);
	i = qcvm->num_edicts;

	if (i == qcvm->num_edicts)
	{
		for (i = 0; i < qcvm->num_edicts; i++)
		{
			e = EDICT_NUM (i);
			if (!strcmp (PR_GetString (e->v.classname), "viewthing"))
				break;
		}
	}

	if (i == qcvm->num_edicts)
	{
		for (i = 0; i < qcvm->num_edicts; i++)
		{
			e = EDICT_NUM (i);
			if (!strcmp (PR_GetString (e->v.classname), "info_player_start"))
				break;
		}
	}

	if (i == qcvm->num_edicts)
	{
		e = NULL;
		Con_Printf ("No viewthing on map\n");
	}

	PR_SwitchQCVM (NULL);
	return e;
}

/*
==================
Host_Viewmodel_f
==================
*/
static void Host_Viewmodel_f (void)
{
	edict_t	 *e;
	qmodel_t *m;

	e = FindViewthing ();
	if (!e)
		return;

	if (!*Cmd_Argv (1))
		m = NULL;
	else
	{
		m = Mod_ForName (Cmd_Argv (1), false);
		if (!m)
		{
			Con_Printf ("Can't load %s\n", Cmd_Argv (1));
			return;
		}
	}

	PR_SwitchQCVM (&sv.qcvm);
	e->v.modelindex = m ? SV_Precache_Model (m->name) : 0;
	e->v.model = PR_SetEngineString (sv.model_precache[(int)e->v.modelindex]);
	e->v.frame = 0;
	PR_SwitchQCVM (NULL);
}

/*
==================
Host_Viewframe_f
==================
*/
static void Host_Viewframe_f (void)
{
	edict_t	 *e;
	int		  f;
	qmodel_t *m;

	e = FindViewthing ();
	if (!e)
		return;
	m = cl.model_precache[(int)e->v.modelindex];
	if (m)
	{
		f = atoi (Cmd_Argv (1));
		if (f >= m->numframes)
			f = m->numframes - 1;

		e->v.frame = f;
	}
}

static void PrintFrameName (qmodel_t *m, int frame)
{
	aliashdr_t		  *hdr;
	maliasframedesc_t *pframedesc;

	hdr = (aliashdr_t *)Mod_Extradata (m);
	if (!hdr || m->type != mod_alias)
		return;
	pframedesc = &hdr->frames[frame];

	Con_Printf ("frame %i: %s\n", frame, pframedesc->name);
}

/*
==================
Host_Viewnext_f
==================
*/
static void Host_Viewnext_f (void)
{
	edict_t	 *e;
	qmodel_t *m;

	e = FindViewthing ();
	if (!e)
		return;
	m = cl.model_precache[(int)e->v.modelindex];
	if (m)
	{
		e->v.frame = e->v.frame + 1;
		if (e->v.frame >= m->numframes)
			e->v.frame = m->numframes - 1;

		PrintFrameName (m, e->v.frame);
	}
}

/*
==================
Host_Viewprev_f
==================
*/
static void Host_Viewprev_f (void)
{
	edict_t	 *e;
	qmodel_t *m;

	e = FindViewthing ();
	if (!e)
		return;

	m = cl.model_precache[(int)e->v.modelindex];
	if (m)
	{
		e->v.frame = e->v.frame - 1;
		if (e->v.frame < 0)
			e->v.frame = 0;

		PrintFrameName (m, e->v.frame);
	}
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
static void Host_Startdemos_f (void)
{
	int i, c;

	if (cls.state == ca_dedicated)
		return;

	c = Cmd_Argc () - 1;
	if (c > MAX_DEMOS)
	{
		Con_Printf ("Max %i demos in demoloop\n", MAX_DEMOS);
		c = MAX_DEMOS;
	}
	Con_Printf ("%i demo(s) in loop\n", c);

	for (i = 1; i < c + 1; i++)
		q_strlcpy (cls.demos[i - 1], Cmd_Argv (i), sizeof (cls.demos[0]));

	if (!sv.active && cls.demonum != -1 && !cls.demoplayback)
	{
		cls.demonum = 0;
		if (!fitzmode && !cl_startdemos.value)
		{ /* QuakeSpasm customization: */
			/* go straight to menu, no CL_NextDemo */
			cls.demonum = -1;
			Cbuf_InsertText ("menu_main\n");
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
static void Host_Demos_f (void)
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
static void Host_Stopdemo_f (void)
{
	if (cls.state == ca_dedicated)
		return;
	if (!cls.demoplayback)
		return;
	CL_StopPlayback ();
	CL_Disconnect ();
}

/*
==================
Host_Resetdemos

Clear looping demo list (called on game change)
==================
*/
void Host_Resetdemos (void)
{
	memset (cls.demos, 0, sizeof (cls.demos));
	cls.demonum = 0;
}

//=============================================================================

/*
==================
Host_InitCommands
==================
*/
void Host_InitCommands (void)
{
	Cmd_AddCommand ("maps", Host_Maps_f);		// johnfitz
	Cmd_AddCommand ("mods", Host_Mods_f);		// johnfitz
	Cmd_AddCommand ("games", Host_Mods_f);		// as an alias to "mods" -- S.A. / QuakeSpasm
	Cmd_AddCommand ("mapname", Host_Mapname_f); // johnfitz
	Cmd_AddCommand ("randmap", Host_Randmap_f); // ericw

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
	Cmd_AddCommand ("setpos", Host_SetPos_f); // QuakeSpasm

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
	Cmd_AddCommand ("fastload", Host_Loadgame_f);
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
