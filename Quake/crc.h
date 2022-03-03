/*
Copyright (C) 1996-2001 Id Software, Inc.
Copyright (C) 2002-2009 John Fitzgibbons and others

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

#ifndef _QUAKE_CRC_H
#define _QUAKE_CRC_H

/* crc.h */

void           CRC_Init (unsigned short *crcvalue);
void           CRC_ProcessByte (unsigned short *crcvalue, byte data);
unsigned short CRC_Value (unsigned short crcvalue);
unsigned short CRC_Block (const byte *start, int count); // johnfitz -- texture crc

// additional hash functions...
unsigned Com_BlockChecksum (void *buffer, int length);
void     Com_BlockFullChecksum (void *buffer, int len, unsigned char *outbuf);

#endif /* _QUAKE_CRC_H */
