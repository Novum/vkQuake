/*
Copyright (C) 2022 A. Drexler

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

// steam.c -- Steam/GOG/Epic Games Store config parsing, from Ironwail

#include "quakedef.h"
#include "q_ctype.h"
#include "steam.h"
#include "json.h"

#if defined(_WIN32) && defined(_MSC_VER)
// comctl32 v6 activation context: with this in the manifest, SDL_ShowMessageBox
// takes its native TaskDialogIndirect path instead of the hand-built dialog fallback
#pragma comment( \
	linker,      \
	"\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#endif

typedef struct vdbcontext_s vdbcontext_t;
typedef void (*vdbcallback_t) (vdbcontext_t *ctx, const char *key, const char *value);

struct vdbcontext_s
{
	void		 *userdata;
	vdbcallback_t callback;
	int			  depth;
	const char	 *path[256];
};

/*
========================
VDB_ParseString

Parses a quoted string (potentially with escape sequences)
========================
*/
static char *VDB_ParseString (char **buf)
{
	char *ret, *write;
	while (q_isspace (**buf))
		++*buf;

	if (**buf != '"')
		return NULL;

	write = ret = ++*buf;
	for (;;)
	{
		if (!**buf) // premature end of buffer
			return NULL;

		if (**buf == '\\') // escape sequence
		{
			++*buf;
			switch (**buf)
			{
			case '\'':
				*write++ = '\'';
				break;
			case '"':
				*write++ = '"';
				break;
			case '?':
				*write++ = '\?';
				break;
			case '\\':
				*write++ = '\\';
				break;
			case 'a':
				*write++ = '\a';
				break;
			case 'b':
				*write++ = '\b';
				break;
			case 'f':
				*write++ = '\f';
				break;
			case 'n':
				*write++ = '\n';
				break;
			case 'r':
				*write++ = '\r';
				break;
			case 't':
				*write++ = '\t';
				break;
			case 'v':
				*write++ = '\v';
				break;
			default: // unsupported sequence
				return NULL;
			}
			++*buf;
			continue;
		}

		if (**buf == '"') // end of quoted string
		{
			++*buf;
			*write = '\0';
			break;
		}

		*write++ = **buf;
		++*buf;
	}

	return ret;
}

/*
========================
VDB_ParseEntry

Parses either a simple key/value pair or a node
========================
*/
static qboolean VDB_ParseEntry (char **buf, vdbcontext_t *ctx)
{
	char *name;

	while (q_isspace (**buf))
		++*buf;
	if (!**buf) // end of buffer
		return true;

	name = VDB_ParseString (buf);
	if (!name)
		return false;

	while (q_isspace (**buf))
		++*buf;

	if (**buf == '"') // key-value pair
	{
		char *value = VDB_ParseString (buf);
		if (!value)
			return false;
		ctx->callback (ctx, name, value);
		return true;
	}

	if (**buf == '{') // node
	{
		++*buf;
		if (ctx->depth == countof (ctx->path))
			return false;
		ctx->path[ctx->depth++] = name;

		while (**buf)
		{
			while (q_isspace (**buf))
				++*buf;

			if (**buf == '}')
			{
				++*buf;
				--ctx->depth;
				break;
			}

			if (**buf == '"')
			{
				if (!VDB_ParseEntry (buf, ctx))
					return false;
				else
					continue;
			}

			if (**buf)
				return false;
		}

		return true;
	}

	return false;
}

/*
========================
VDB_Parse

Parses the given buffer in-place, calling the specified callback for each key/value pair
========================
*/
static qboolean VDB_Parse (char *buf, vdbcallback_t callback, void *userdata)
{
	vdbcontext_t ctx;
	ctx.userdata = userdata;
	ctx.callback = callback;
	ctx.depth = 0;

	while (*buf)
		if (!VDB_ParseEntry (&buf, &ctx))
			return false;

	return true;
}

/*
========================
Steam library folder config parsing

Examines all steam library folders and returns the path of the one containing the game with the given appid
========================
*/
typedef struct
{
	const char *appid;
	const char *current;
	const char *result;
} libsparser_t;

static void VDB_OnLibFolderProperty (vdbcontext_t *ctx, const char *key, const char *value)
{
	libsparser_t *parser = (libsparser_t *)ctx->userdata;
	int			  idx;

	if (ctx->depth >= 2 && !strcmp (ctx->path[0], "libraryfolders") && sscanf (ctx->path[1], "%d", &idx) == 1)
	{
		if (ctx->depth == 2)
		{
			if (!strcmp (key, "path"))
				parser->current = value;
		}
		else if (ctx->depth == 3 && !strcmp (key, parser->appid) && !strcmp (ctx->path[2], "apps"))
		{
			parser->result = parser->current;
		}
	}
}

/*
========================
Steam application manifest parsing

Finds the path relative to the library folder
========================
*/
typedef struct
{
	const char *result;
} acfparser_t;

static void ACF_OnManifestProperty (vdbcontext_t *ctx, const char *key, const char *value)
{
	acfparser_t *parser = (acfparser_t *)ctx->userdata;
	if (ctx->depth == 1 && !strcmp (key, "installdir") && !strcmp (ctx->path[0], "AppState"))
		parser->result = value;
}

/*
========================
Steam_IsValidPath

Returns true if the given path contains a valid Steam install
(based on the existence of a config/libraryfolders.vdf file)
========================
*/
qboolean Steam_IsValidPath (const char *path)
{
	char libpath[MAX_OSPATH];
	if ((size_t)q_snprintf (libpath, sizeof (libpath), "%s/config/libraryfolders.vdf", path) >= sizeof (libpath))
		return false;
	return Sys_FileType (libpath) == FS_ENT_FILE;
}

/*
========================
Steam_ReadLibFolders

Returns Mem_Alloc'ed buffer with Steam library folders config
========================
*/
static char *Steam_ReadLibFolders (void)
{
	char path[MAX_OSPATH];
	if (!Sys_GetSteamDir (path, sizeof (path)))
		return NULL;
	if ((size_t)q_strlcat (path, "/config/libraryfolders.vdf", sizeof (path)) >= sizeof (path))
		return NULL;
	return (char *)COM_LoadMallocFile_TextMode_OSPath (path, NULL);
}

/*
========================
Steam_FindGame

Finds the Steam library and subdirectory for the given appid
========================
*/
qboolean Steam_FindGame (steamgame_t *game, int appid)
{
	char		 appidstr[32], path[MAX_OSPATH];
	char		*steamcfg, *manifest;
	libsparser_t libparser;
	acfparser_t	 acfparser;
	size_t		 liblen, sublen;
	qboolean	 ret = false;

	game->appid = appid;
	game->subdir = NULL;
	game->library[0] = 0;

	steamcfg = Steam_ReadLibFolders ();
	if (!steamcfg)
	{
		Sys_Printf ("Steam library not found.\n");
		return false;
	}

	q_snprintf (appidstr, sizeof (appidstr), "%d", appid);
	memset (&libparser, 0, sizeof (libparser));
	libparser.appid = appidstr;
	if (!VDB_Parse (steamcfg, VDB_OnLibFolderProperty, &libparser) || !libparser.result)
	{
		Sys_Printf ("ERROR: Couldn't parse Steam library.\n");
		goto done_cfg;
	}

	if ((size_t)q_snprintf (path, sizeof (path), "%s/steamapps/appmanifest_%s.acf", libparser.result, appidstr) >= sizeof (path))
	{
		Sys_Printf ("ERROR: Couldn't read Steam manifest for app %s (path too long).\n", appidstr);
		goto done_cfg;
	}

	manifest = (char *)COM_LoadMallocFile_TextMode_OSPath (path, NULL);
	if (!manifest)
	{
		Sys_Printf ("ERROR: Couldn't read Steam manifest for app %s.\n", appidstr);
		goto done_cfg;
	}

	memset (&acfparser, 0, sizeof (acfparser));
	if (!VDB_Parse (manifest, ACF_OnManifestProperty, &acfparser) || !acfparser.result)
	{
		Sys_Printf ("ERROR: Couldn't parse Steam manifest for app %s.\n", appidstr);
		goto done_manifest;
	}

	liblen = strlen (libparser.result);
	sublen = strlen (acfparser.result);

	if (liblen + 1 + sublen + 1 > countof (game->library))
	{
		Sys_Printf ("ERROR: Path for Steam app %s is too long.\n", appidstr);
		goto done_manifest;
	}

	memcpy (game->library, libparser.result, liblen + 1);
	game->subdir = game->library + liblen + 1;
	memcpy (game->subdir, acfparser.result, sublen + 1);
	ret = true;

done_manifest:
	Mem_Free (manifest);
done_cfg:
	Mem_Free (steamcfg);

	return ret;
}

/*
========================
Steam_ResolvePath

Fills in the OS path where the game is installed
========================
*/
qboolean Steam_ResolvePath (char *path, size_t pathsize, const steamgame_t *game)
{
	return game->subdir && (size_t)q_snprintf (path, pathsize, "%s/steamapps/common/%s", game->library, game->subdir) < pathsize;
}

/*
========================
EGS_FindGame
========================
*/
qboolean EGS_FindGame (char *path, size_t pathsize, const char *nspace, const char *itemid, const char *appname)
{
	const char *launcherdata;
	char		manifestdir[MAX_OSPATH];
	findfile_t *find;

	launcherdata = Sys_GetEGSLauncherData ();
	if (launcherdata)
	{
		json_t *json = JSON_Parse (launcherdata);
		Mem_Free ((void *)launcherdata);
		if (json)
		{
			const jsonentry_t *list = JSON_Find (json->root, "InstallationList", JSON_ARRAY);
			if (list)
			{
				const jsonentry_t *item;
				for (item = list->firstchild; item; item = item->next)
				{
					const char *cur_nspace = JSON_FindString (item, "NamespaceId");
					const char *cur_itemid = JSON_FindString (item, "ItemId");
					const char *cur_appname = JSON_FindString (item, "AppName");
					const char *location = JSON_FindString (item, "InstallLocation");

					if (location && *location && cur_nspace && cur_itemid && cur_appname && strcmp (cur_nspace, nspace) == 0 &&
						strcmp (cur_itemid, itemid) == 0 && strcmp (cur_appname, appname) == 0)
					{
						q_strlcpy (path, location, pathsize);
						JSON_Free (json);
						return true;
					}
				}
			}
			JSON_Free (json);
		}
	}

	if (!Sys_GetEGSManifestDir (manifestdir, sizeof (manifestdir)))
		return false;

	for (find = Sys_FindFirst (manifestdir, "item"); find; find = Sys_FindNext (find))
	{
		char			filepath[MAX_OSPATH];
		char		   *manifest;
		const char	   *cur_nspace;
		const char	   *cur_itemid;
		const char	   *cur_appname;
		const char	   *location;
		const qboolean *incomplete;
		json_t		   *json;

		if (find->attribs & FA_DIRECTORY)
			continue;
		if ((size_t)q_snprintf (filepath, sizeof (filepath), "%s/%s", manifestdir, find->name) >= sizeof (filepath))
			continue;

		manifest = (char *)COM_LoadMallocFile_TextMode_OSPath (filepath, NULL);
		if (!manifest)
			continue;
		json = JSON_Parse (manifest);
		Mem_Free (manifest);
		if (!json)
			continue;

		cur_nspace = JSON_FindString (json->root, "CatalogNamespace");
		cur_itemid = JSON_FindString (json->root, "CatalogItemId");
		cur_appname = JSON_FindString (json->root, "AppName");
		location = JSON_FindString (json->root, "InstallLocation");
		incomplete = JSON_FindBoolean (json->root, "bIsIncompleteInstall");

		if (location && *location && (!incomplete || !*incomplete) && cur_nspace && cur_itemid && cur_appname && strcmp (cur_nspace, nspace) == 0 &&
			strcmp (cur_itemid, itemid) == 0 && strcmp (cur_appname, appname) == 0)
		{
			q_strlcpy (path, location, pathsize);
			JSON_Free (json);
			Sys_FindClose (find);
			return true;
		}

		JSON_Free (json);
	}

	return false;
}

/*
========================
ChooseQuakeFlavor

Shows a simple message box asking the user to choose
between the original version and the 2021 rerelease
========================
*/
quakeflavor_t ChooseQuakeFlavor (void)
{
#ifdef _WIN32
	// native task dialog with command links; requires comctl32 v6
	// with the comctl32 v6 manifest above, SDL renders this as a native task dialog
	static const SDL_MessageBoxButtonData buttons[] = {
		{SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, QUAKE_FLAVOR_REMASTERED, "Remastered"},
		{0, QUAKE_FLAVOR_ORIGINAL, "Original"},
	};
	SDL_MessageBoxData messagebox;
	int				   choice = -1;

	memset (&messagebox, 0, sizeof (messagebox));
	messagebox.buttons = buttons;
	messagebox.numbuttons = countof (buttons);
	messagebox.flags = SDL_MESSAGEBOX_BUTTONS_LEFT_TO_RIGHT;
	messagebox.title = "vkQuake";
	messagebox.message = "Which Quake version would you like to play?";

	if (SDL_ShowMessageBox (&messagebox, &choice) < 0)
	{
		Sys_Printf ("ChooseQuakeFlavor: %s\n", SDL_GetError ());
		return QUAKE_FLAVOR_REMASTERED;
	}

	if (choice == -1)
	{
		SDL_Quit ();
		exit (0);
	}

	return (quakeflavor_t)choice;
#else
	// FIXME: Original version can't be played on OS's with case-sensitive file systems
	// (due to id1 being named "Id1" and pak0.pak "PAK0.PAK")
	return QUAKE_FLAVOR_REMASTERED;
#endif
}
