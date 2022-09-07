/*
Copyright (C) 1996-2001 Id Software, Inc.
Copyright (C) 2002-2009 John Fitzgibbons and others
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

#ifndef _QUAKE_SCREEN_H
#define _QUAKE_SCREEN_H

// screen.h

void SCR_Init (void);
void SCR_LoadPics (void);

void SCR_UpdateScreen (qboolean use_tasks);
void SCR_UpdateZoom (void);

void SCR_CenterPrintClear (void);
void SCR_CenterPrint (const char *str);

void SCR_BeginLoadingPlaque (void);
void SCR_EndLoadingPlaque (void);

int SCR_ModalMessage (const char *text, float timeout); // johnfitz -- added timeout

void SCR_UpdateRelativeScale ();

extern float scr_con_current;
extern float scr_conlines; // lines of console to display

extern int sb_lines;

extern qboolean scr_disabled_for_loading;

extern cvar_t scr_viewsize;

extern cvar_t scr_sbaralpha; // johnfitz

extern cvar_t scr_menuscale;
extern cvar_t scr_sbarscale;
extern cvar_t scr_conwidth;
extern cvar_t scr_conscale;
extern cvar_t scr_relativescale;
extern cvar_t scr_scale;
extern cvar_t scr_crosshairscale;
// johnfitz

#endif /* _QUAKE_SCREEN_H */
