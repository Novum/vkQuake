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

#ifndef _QUAKE_PLATFORM_H
#define _QUAKE_PLATFORM_H

/* platform dependent way to set the window icon */
void PL_SetWindowIcon(void);

/* platform dependent cleanup */
void PL_VID_Shutdown (void);

/* retrieve text from the clipboard (returns Z_Malloc()'ed data) */
char *PL_GetClipboardData (void);

/* show an error dialog */
void PL_ErrorDialog(const char *text);

#endif	/* _QUAKE_PLATFORM_H */

