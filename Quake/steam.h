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

#ifndef _STEAM_H_
#define _STEAM_H_

#define QUAKE_STEAM_APPID 2310

#define QUAKE_EGS_NAMESPACE "f57987ad149c43b3a7a66a7f10828f92"
#define QUAKE_EGS_ITEM_ID	"19e3c0be6d6c4d4b84b1bc2248f94b43"
#define QUAKE_EGS_APP_NAME	"18161d3ef68e4166968036626d173f25"

typedef enum
{
	QUAKE_FLAVOR_ORIGINAL,
	QUAKE_FLAVOR_REMASTERED,
} quakeflavor_t;

typedef struct steamgame_s
{
	int	  appid;
	char *subdir;
	char  library[MAX_OSPATH];
} steamgame_t;

qboolean Steam_IsValidPath (const char *path);
qboolean Steam_FindGame (steamgame_t *game, int appid);
qboolean Steam_ResolvePath (char *path, size_t pathsize, const steamgame_t *game);

qboolean EGS_FindGame (char *path, size_t pathsize, const char *nspace, const char *itemid, const char *appname);

quakeflavor_t ChooseQuakeFlavor (void);

#endif /*_STEAM_H */
