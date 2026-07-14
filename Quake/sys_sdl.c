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
	bool		free;
	char	   *file_path;
	FILE	   *file;
	const byte *memory;
	qfileofs_t	pos;
	qfilesize_t size;
	bool		eof_condition;
} file_handle_t;

// Protects sys_handles when requesting a new handle / freeing a handle.
static SDL_Mutex *sys_handles_mutex;

#define MAX_HANDLES 32 /* johnfitz -- was 10 */
static file_handle_t sys_handles[MAX_HANDLES * (TASKS_MAX_WORKERS + 1)];

void Sys_FileInit (void)
{
	sys_handles_mutex = SDL_CreateMutex ();

	memset (sys_handles, 0x0, sizeof (sys_handles));

	for (int i = 1; i < countof (sys_handles); i++)
		sys_handles[i].free = true;
}

static int allocHandle (void)
{
	int i;

	SDL_LockMutex (sys_handles_mutex);

	// TBC : why skipping index 0 ? is it to make
	// 0 as an invalid handle value by design ?
	for (i = 1; i < countof (sys_handles); i++)
	{
		if (sys_handles[i].free)
		{
			// reset all fields
			memset (&(sys_handles[i]), 0x0, sizeof (file_handle_t));

			assert (!sys_handles[i].free);

			SDL_UnlockMutex (sys_handles_mutex);
			return i;
		}
	}
	SDL_UnlockMutex (sys_handles_mutex);
	Sys_Error ("out of handles");
	return -1;
}

static void freeHandle (int handle)
{
	SDL_LockMutex (sys_handles_mutex);

	sys_handles[handle].free = true;

	SDL_UnlockMutex (sys_handles_mutex);
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

	i = allocHandle ();
	f = Sys_fopen (path, "rb");

	if (!f)
	{
		freeHandle (i);
		*hndl = -1;
		retval = -1;
	}
	else
	{
		sys_handles[i].memory = NULL;

		sys_handles[i].file_path = (char *)Mem_Alloc (strlen (path) + 1);
		q_strlcpy (sys_handles[i].file_path, (const char *)path, strlen (path) + 1);

		sys_handles[i].file = f;
		sys_handles[i].pos = 0;
		sys_handles[i].eof_condition = false;
		*hndl = i;
		retval = Sys_filelength (f);
		sys_handles[i].size = retval;
	}

	return retval;
}

void Sys_MemFileOpenRead (const byte *memory, qfilesize_t size, int *hndl)
{
	int i = allocHandle ();

	sys_handles[i].file = NULL;
	sys_handles[i].file_path = NULL;
	sys_handles[i].memory = memory;
	sys_handles[i].size = size;
	// position to start reading from :
	sys_handles[i].pos = 0;
	sys_handles[i].eof_condition = false;
	*hndl = i;
}

int Sys_DuplicateHandle (int handle)
{
	FILE *new_file = NULL;

	if (sys_handles[handle].file)
	{
		new_file = Sys_fopen (sys_handles[handle].file_path, "rb");

		if (!new_file)
			return -1;
	}

	int new_handle = allocHandle ();

	// duplicate all data
	sys_handles[new_handle] = sys_handles[handle];

	// replace with the new file
	if (sys_handles[handle].file)
	{
		sys_handles[new_handle].file = new_file;

		// we want our own copy, will be freed in Sys_FileClose()
		sys_handles[new_handle].file_path = (char *)Mem_Alloc (strlen (sys_handles[handle].file_path) + 1);
		q_strlcpy (sys_handles[new_handle].file_path, (const char *)sys_handles[handle].file_path, strlen (sys_handles[handle].file_path) + 1);
	}

	// Re-Seek:
	Sys_FileSeek (new_handle, sys_handles[handle].pos);

	// Technically, the EOF condition of the original must be preserved,
	// even if duplicating a handle of an out-of bound file/memory pointer (sys_handles[handle].pos)
	// would be ludicrous in practice.

	return new_handle;
}

int Sys_FileOpenWrite (const char *path)
{
	FILE *f;
	int	  i;

	i = allocHandle ();
	f = Sys_fopen (path, "wb");

	if (!f)
		Sys_Error ("Error opening %s: %s", path, strerror (errno));

	sys_handles[i].file = f;

	sys_handles[i].file_path = (char *)Mem_Alloc (strlen (path) + 1);
	q_strlcpy (sys_handles[i].file_path, (const char *)path, strlen (path) + 1);

	sys_handles[i].size = Sys_filelength (f);
	// position to start writing from :
	sys_handles[i].pos = sys_handles[i].size;
	sys_handles[i].eof_condition = false;
	sys_handles[i].memory = NULL;
	return i;
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
		Mem_Free (sys_handles[handle].file_path);
	}

	freeHandle (handle);
}

int Sys_FileSeek (int handle, qfileofs_t position)
{
	// like fseek(), going beyond the actual file
	// without error is expected. Attempting to read afterwards however will trigger
	// an EOF condition.
	if (position >= 0)
	{
		if (sys_handles[handle].file)
		{
			Sys_fseek (sys_handles[handle].file, position, SEEK_SET);
		}

		sys_handles[handle].pos = position;

		return 0;
	}

	return 1;
}

bool Sys_feof (int handle)
{
	return sys_handles[handle].eof_condition;
}

int Sys_FileRead (int handle, void *dest, int count)
{
	if (count <= 0)
		return 0;

	// test EOF condition
	sys_handles[handle].eof_condition = sys_handles[handle].eof_condition || ((sys_handles[handle].size - sys_handles[handle].pos) <= 0) ? true : false;

	if (sys_handles[handle].eof_condition)
		return 0;

	qfilesize_t computed_read_count = q_min ((qfilesize_t)count, sys_handles[handle].size - sys_handles[handle].pos);

	computed_read_count = q_max (0, computed_read_count);

	if (sys_handles[handle].file)
	{
		// file-based:
		qfilesize_t fread_count = fread (dest, 1, count, sys_handles[handle].file);

		assert (computed_read_count == fread_count);
		// printf ("computed_read_count (%d) != fread_count(%d)\n", (int)computed_read_count, (int)fread_count);

		sys_handles[handle].pos += fread_count;
		// if partial read, triggers EOF
		sys_handles[handle].eof_condition = feof (sys_handles[handle].file);

		return fread_count;
	}
	else
	{
		// memory-based:
		memcpy (dest, sys_handles[handle].memory + sys_handles[handle].pos, computed_read_count);

		sys_handles[handle].pos += computed_read_count;

		// if partial read, triggers EOF
		sys_handles[handle].eof_condition = (computed_read_count < count);

		return computed_read_count;
	}

	return 0;
}

int Sys_fgetc (int handle)
{
	if (sys_handles[handle].eof_condition)
		return EOF;

	int next_byte_read = 0;

	if (Sys_FileRead (handle, (void *)&next_byte_read, 1) != 1)
	{
		assert (sys_handles[handle].eof_condition);
		return EOF;
	}

	return next_byte_read;
}

int Sys_FileWrite (int handle, const void *data, int count)
{
	assert (sys_handles[handle].file);

	const int effective_nb_write = fwrite (data, 1, count, sys_handles[handle].file);

	sys_handles[handle].pos += effective_nb_write;
	sys_handles[handle].size += effective_nb_write;

	return effective_nb_write;
}

#ifdef USE_SDL3
typedef struct folderselect_s
{
	char		 *dst;
	size_t		  dstsize;
	SDL_AtomicInt done;
	int			  result;
} folderselect_t;

static void SDLCALL Sys_FolderSelected (void *userdata, const char *const *filelist, int filter)
{
	folderselect_t *sel = (folderselect_t *)userdata;
	(void)filter;

	if (!filelist)
		sel->result = -1; // dialog could not be shown
	else if (!filelist[0])
		sel->result = 0; // cancelled
	else
	{
		q_strlcpy (sel->dst, filelist[0], sel->dstsize);
		sel->result = 1;
	}
	SDL_SetAtomicInt (&sel->done, 1);
}

int Sys_SelectFolder (const char *title, const char *default_location, char *dst, size_t dstsize)
{
	folderselect_t	 sel;
	SDL_PropertiesID props;

	memset (&sel, 0, sizeof (sel));
	sel.dst = dst;
	sel.dstsize = dstsize;

	props = SDL_CreateProperties ();
	SDL_SetStringProperty (props, SDL_PROP_FILE_DIALOG_TITLE_STRING, title);
	if (default_location && default_location[0])
		SDL_SetStringProperty (props, SDL_PROP_FILE_DIALOG_LOCATION_STRING, default_location);
	SDL_ShowFileDialogWithProperties (SDL_FILEDIALOG_OPENFOLDER, Sys_FolderSelected, &sel, props);
	SDL_DestroyProperties (props);

	// the callback can come from another thread; XDG portals on Linux need event pumping
	while (!SDL_GetAtomicInt (&sel.done))
	{
		SDL_PumpEvents ();
		SDL_Delay (10);
	}

	return sel.result;
}
#endif
