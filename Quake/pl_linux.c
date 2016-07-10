/*
Copyright (C) 1996-2001 Id Software, Inc.
Copyright (C) 2002-2005 John Fitzgibbons and others
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
#if defined(SDL_FRAMEWORK) || defined(NO_SDL_CONFIG)
#if defined(USE_SDL2)
#include <SDL2/SDL.h>
#else
#include <SDL/SDL.h>
#endif
#else
#include "SDL.h"
#endif

static const Uint8 bmp_bytes[] =
{
#include "qs_bmp.h"
};

void PL_SetWindowIcon (void)
{
	SDL_RWops	*rwop;
	SDL_Surface	*icon;
	Uint32		colorkey;

	/* SDL_RWFromConstMem() requires SDL >= 1.2.7 */
	rwop = SDL_RWFromConstMem(bmp_bytes, sizeof(bmp_bytes));
	if (rwop == NULL)
		return;
	icon = SDL_LoadBMP_RW(rwop, 1);
	if (icon == NULL)
		return;
	/* make pure magenta (#ff00ff) tranparent */
	colorkey = SDL_MapRGB(icon->format, 255, 0, 255);
#if defined(USE_SDL2)
	SDL_SetColorKey(icon, SDL_TRUE, colorkey);
	SDL_SetWindowIcon((SDL_Window*) VID_GetWindow(), icon);
#else
	SDL_SetColorKey(icon, SDL_SRCCOLORKEY, colorkey);
	SDL_WM_SetIcon(icon, NULL);
#endif
	SDL_FreeSurface(icon);
}

void PL_VID_Shutdown (void)
{
}

#define MAX_CLIPBOARDTXT	MAXCMDLINE	/* 256 */
char *PL_GetClipboardData (void)
{
	char *data = NULL;
#if defined(USE_SDL2)
	char *cliptext = SDL_GetClipboardText();

	if (cliptext != NULL)
	{
		size_t size = strlen(cliptext) + 1;
	/* this is intended for simple small text copies
	 * such as an ip address, etc:  do chop the size
	 * here, otherwise we may experience Z_Malloc()
	 * failures and all other not-oh-so-fun stuff. */
		size = q_min(MAX_CLIPBOARDTXT, size);
		data = (char *) Z_Malloc(size);
		q_strlcpy (data, cliptext, size);
	}
#endif

	return data;
}

void PL_ErrorDialog (const char *errorMsg)
{
}

