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

#ifndef __CDAUDIO_H
#define __CDAUDIO_H

int	CDAudio_Init (void);
int	CDAudio_Play (byte track, qboolean looping);
	/* returns 0 for success, -1 for failure. */
void	CDAudio_Stop (void);
void	CDAudio_Pause (void);
void	CDAudio_Resume (void);
void	CDAudio_Shutdown (void);
void	CDAudio_Update (void);

#endif	/* __CDAUDIO_H */

