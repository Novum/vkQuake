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
// wad.c

#include "quakedef.h"

int			wad_numlumps;
lumpinfo_t *wad_lumps;
byte	   *wad_base = NULL;

void SwapPic (qpic_t *pic);

/*
==================
W_CleanupName

Lowercases name and pads with spaces and a terminating 0 to the length of
lumpinfo_t->name.
Used so lumpname lookups can proceed rapidly by comparing 4 chars at a time
Space padding is so names can be printed nicely in tables.
Can safely be performed in place.
==================
*/
void W_CleanupName (const char *in, char *out)
{
	int i;
	int c;

	for (i = 0; i < 16; i++)
	{
		c = in[i];
		if (!c)
			break;

		if (c >= 'A' && c <= 'Z')
			c += ('a' - 'A');
		out[i] = c;
	}

	for (; i < 16; i++)
		out[i] = 0;
}

/*
====================
W_LoadWadFile
====================
*/
void W_LoadWadFile (void) // johnfitz -- filename is now hard-coded for honesty
{
	lumpinfo_t *lump_p;
	wadinfo_t  *header;
	int			i;
	int			infotableofs;
	const char *filename = WADFILENAME;

	// johnfitz -- modified to use malloc
	// TODO: use cache_alloc
	if (wad_base)
		Mem_Free (wad_base);
	wad_base = COM_LoadFile (filename, NULL);
	if (!wad_base)
		Sys_Error (
			"W_LoadWadFile: couldn't load %s\n\n"
			"Basedir is: %s\n\n"
			"Check that this has an " GAMENAME " subdirectory containing pak0.pak and pak1.pak, "
			"or use the -basedir command-line option to specify another directory.",
			filename, com_basedir);

	header = (wadinfo_t *)wad_base;

	if (header->identification[0] != 'W' || header->identification[1] != 'A' || header->identification[2] != 'D' || header->identification[3] != '2')
	{
		Con_Printf ("Wad file %s doesn't have WAD2 id\n", filename);
		wad_numlumps = 0;
		infotableofs = 0;
	}
	else
	{
		wad_numlumps = LittleLong (header->numlumps);
		infotableofs = LittleLong (header->infotableofs);
	}
	wad_lumps = (lumpinfo_t *)(wad_base + infotableofs);
	if (infotableofs < 0 || infotableofs + wad_numlumps * sizeof (lumpinfo_t) > (size_t)com_filesize)
	{
		Con_Printf ("Wad file %s header extends beyond end of file\n", filename);
		wad_numlumps = 0;
	}

	for (i = 0, lump_p = wad_lumps; i < wad_numlumps; i++, lump_p++)
	{
		lump_p->filepos = LittleLong (lump_p->filepos);
		lump_p->size = LittleLong (lump_p->size);
		if (lump_p->filepos + lump_p->size > com_filesize && !(lump_p->filepos + LittleLong (lump_p->disksize) > com_filesize))
			lump_p->size = LittleLong (lump_p->disksize);
		if (lump_p->filepos < 0 || lump_p->size < 0 || lump_p->filepos + lump_p->size > com_filesize)
		{
			if (lump_p->filepos > com_filesize || lump_p->size < 0)
			{
				Con_Printf ("Wad file %s lump \"%.16s\" begins %lli bytes beyond end of wad\n", filename, lump_p->name, lump_p->filepos - com_filesize);
				lump_p->filepos = 0;
				lump_p->size = q_max (0, lump_p->size - lump_p->filepos);
			}
			else
			{
				Con_Printf (
					"Wad file %s lump \"%.16s\" extends %lli bytes beyond end of wad (lump size: %u)\n", filename, lump_p->name,
					(lump_p->filepos + lump_p->size) - com_filesize, lump_p->size);
				lump_p->size = q_max (0, lump_p->size - lump_p->filepos);
			}
		}
		W_CleanupName (lump_p->name, lump_p->name); // CAUTION: in-place editing!!! The endian fixups too.
		if (lump_p->type == TYP_QPIC)
			SwapPic ((qpic_t *)(wad_base + lump_p->filepos));
	}
}

/*
=============
W_GetLumpinfo
=============
*/
static lumpinfo_t *W_GetLumpinfo (const char *name)
{
	int			i;
	lumpinfo_t *lump_p;
	char		clean[16];

	W_CleanupName (name, clean);

	for (lump_p = wad_lumps, i = 0; i < wad_numlumps; i++, lump_p++)
	{
		if (!strcmp (clean, lump_p->name))
			return lump_p;
	}

	return NULL;
}

void *W_GetLumpName (const char *name, lumpinfo_t **out_info) // Spike: so caller can verify that the qpic was written properly.
{
	lumpinfo_t *lump;

	lump = W_GetLumpinfo (name);

	if (!lump)
		return NULL; // johnfitz

	*out_info = lump;
	return (void *)(wad_base + lump->filepos);
}

/*
=============================================================================

automatic byte swapping

=============================================================================
*/

void SwapPic (qpic_t *pic)
{
	pic->width = LittleLong (pic->width);
	pic->height = LittleLong (pic->height);
}
