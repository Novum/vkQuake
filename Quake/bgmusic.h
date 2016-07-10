/*
 * Background music handling for Quakespasm (adapted from uHexen2)
 * Handles streaming music as raw sound samples and runs the midi driver
 *
 * Copyright (C) 1999-2005 Id Software, Inc.
 * Copyright (C) 2010-2012 O.Sezer <sezero@users.sourceforge.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#ifndef _BGMUSIC_H_
#define _BGMUSIC_H_

extern qboolean	bgmloop;
extern cvar_t	bgm_extmusic;

qboolean BGM_Init (void);
void BGM_Shutdown (void);

void BGM_Play (const char *filename);
void BGM_Stop (void);
void BGM_Update (void);
void BGM_Pause (void);
void BGM_Resume (void);

void BGM_PlayCDtrack (byte track, qboolean looping);

#endif	/* _BGMUSIC_H_ */

