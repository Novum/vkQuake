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
#include "sys.h"

#include <errno.h>

typedef struct file_handle_s
{
	FILE	   *file;
	const byte *memory;
	int			pos;
	int			size;
} file_handle_t;

#define MAX_HANDLES 32 /* johnfitz -- was 10 */
static file_handle_t sys_handles[MAX_HANDLES];

static int findhandle (void)
{
	int i;

	for (i = 1; i < MAX_HANDLES; i++)
	{
		if (!sys_handles[i].file && !sys_handles[i].memory)
			return i;
	}
	Sys_Error ("out of handles");
	return -1;
}

static long Sys_filelength (FILE *f)
{
	long pos, end;

	pos = ftell (f);
	fseek (f, 0, SEEK_END);
	end = ftell (f);
	fseek (f, pos, SEEK_SET);

	return end;
}

int Sys_FileOpenRead (const char *path, int *hndl)
{
	FILE *f;
	int	  i, retval;

	i = findhandle ();
	f = fopen (path, "rb");

	if (!f)
	{
		*hndl = -1;
		retval = -1;
	}
	else
	{
		sys_handles[i].file = f;
		*hndl = i;
		retval = Sys_filelength (f);
	}

	return retval;
}

void Sys_MemFileOpenRead (const byte *memory, int size, int *hndl)
{
	int i = findhandle ();

	sys_handles[i].memory = memory;
	sys_handles[i].size = size;
	sys_handles[i].pos = 0;
	*hndl = i;
}

int Sys_FileOpenWrite (const char *path)
{
	FILE *f;
	int	  i;

	i = findhandle ();
	f = fopen (path, "wb");

	if (!f)
		Sys_Error ("Error opening %s: %s", path, strerror (errno));

	sys_handles[i].file = f;
	return i;
}

void Sys_FileClose (int handle)
{
	if (sys_handles[handle].file)
	{
		fclose (sys_handles[handle].file);
		sys_handles[handle].file = NULL;
	}
	else
		sys_handles[handle].memory = NULL;
}

void Sys_FileSeek (int handle, int position)
{
	if (sys_handles[handle].file)
		fseek (sys_handles[handle].file, position, SEEK_SET);
	else
		sys_handles[handle].pos = position;
}

int Sys_FileRead (int handle, void *dest, int count)
{
	if (sys_handles[handle].file)
		return fread (dest, 1, count, sys_handles[handle].file);
	else
	{
		memcpy (dest, sys_handles[handle].memory + sys_handles[handle].pos, count);
		sys_handles[handle].pos += count;
		return count;
	}
}

int Sys_FileWrite (int handle, const void *data, int count)
{
	assert (sys_handles[handle].file);
	return fwrite (data, 1, count, sys_handles[handle].file);
}
