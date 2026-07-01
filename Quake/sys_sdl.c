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
	qfileofs_t	pos;
	qfilesize_t size;
} file_handle_t;

// Protects sys_handles when requesting a new hnadle / freeing a handle.
static SDL_Mutex *sys_handles_mutex;

#define MAX_HANDLES 256 /* johnfitz -- was 10 */
static file_handle_t sys_handles[MAX_HANDLES * (TASKS_MAX_WORKERS + 1)];

static int findhandle (void)
{
	int i;

	SDL_LockMutex (sys_handles_mutex);

	for (i = 1; i < countof (sys_handles); i++)
	{
		if (!sys_handles[i].file && !sys_handles[i].memory)
		{
			SDL_UnlockMutex (sys_handles_mutex);
			return i;
		}
	}
	SDL_UnlockMutex (sys_handles_mutex);
	Sys_Error ("out of handles");
	return -1;
}

void Sys_FileInit (void)
{
	sys_handles_mutex = SDL_CreateMutex ();
}

qfilesize_t Sys_filelength (FILE *f)
{
	qfileofs_t pos, end;

	pos = Sys_ftell (f);
	Sys_fseek (f, 0, SEEK_END);
	end = Sys_ftell (f);
	Sys_fseek (f, pos, SEEK_SET);

	return (qfilesize_t)end;
}

qfilesize_t Sys_FileOpenRead (const char *path, int *hndl)
{
	FILE	   *f;
	int			i;
	qfilesize_t retval;

	i = findhandle ();
	f = fopen (path, "rb");

	if (!f)
	{
		*hndl = -1;
		retval = -1;
	}
	else
	{
		sys_handles[i].memory = NULL;
		sys_handles[i].file = f;
		sys_handles[i].pos = 0;
		*hndl = i;
		retval = Sys_filelength (f);
		sys_handles[i].size = retval;
	}

	return retval;
}

void Sys_MemFileOpenRead (const byte *memory, qfilesize_t size, int *hndl)
{
	int i = findhandle ();

	sys_handles[i].file = NULL;
	sys_handles[i].memory = memory;
	sys_handles[i].size = size;
	// position to start reading from :
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
	sys_handles[i].size = Sys_filelength (f);
	// position to start writing from :
	sys_handles[i].pos = sys_handles[i].size;
	return i;
}

qfilesize_t Sys_FileSize (int handle)
{
	return sys_handles[handle].size;
}

qfileofs_t Sys_FilePos (int handle)
{
	return sys_handles[handle].pos;
}

void Sys_FileClose (int handle)
{
	if (sys_handles[handle].file)
	{
		fclose (sys_handles[handle].file);
	}

	SDL_LockMutex (sys_handles_mutex);

	sys_handles[handle].file = NULL;
	sys_handles[handle].memory = NULL;
	sys_handles[handle].pos = 0;
	sys_handles[handle].size = 0;

	SDL_UnlockMutex (sys_handles_mutex);
}

int Sys_FileSeek (int handle, qfileofs_t position)
{
	if (position >= 0 && position < (qfileofs_t)sys_handles[handle].size)
	{
		if (sys_handles[handle].file)
		{
			Sys_fseek (sys_handles[handle].file, position, SEEK_SET);
		}

		sys_handles[handle].pos = position;

		return 0;
	}

	return -1;
}

bool Sys_FileIsEOF (int handle)
{
	return sys_handles[handle].pos >= sys_handles[handle].size;
}

int Sys_FileRead (int handle, void *dest, int count)
{
	if (sys_handles[handle].file)
	{
		const int max_file_read_count = fread (dest, 1, count, sys_handles[handle].file);
		sys_handles[handle].pos += max_file_read_count;
		return max_file_read_count;
	}
	else
	{
		// clamp the max file read to the max size of the memory buffer.
		qfilesize_t max_file_read_count = q_min ((qfilesize_t)count, sys_handles[handle].size - sys_handles[handle].pos);

		if (max_file_read_count > 0)
		{
			memcpy (dest, sys_handles[handle].memory + sys_handles[handle].pos, max_file_read_count);
			sys_handles[handle].pos += max_file_read_count;
		}

		if (max_file_read_count < 0)
			max_file_read_count = 0;

		return (int)max_file_read_count;
	}
	return 0;
}

int Sys_FileWrite (int handle, const void *data, int count)
{
	assert (sys_handles[handle].file);

	const int effective_nb_write = fwrite (data, 1, count, sys_handles[handle].file);

	sys_handles[handle].pos += effective_nb_write;
	sys_handles[handle].size += effective_nb_write;

	return effective_nb_write;
}
