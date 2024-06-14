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

#ifndef _QUAKE_SYS_H
#define _QUAKE_SYS_H

// sys.h -- non-portable functions

void Sys_Init (void);

//
// file IO
//

typedef long long qfileofs_t;

int		   Sys_fseek (FILE *file, qfileofs_t ofs, int origin);
qfileofs_t Sys_ftell (FILE *file);
qfileofs_t Sys_filelength (FILE *f);

// returns the file size or -1 if file is not present.
// the file should be in BINARY mode for stupid OSs that care
qfileofs_t Sys_FileOpenRead (const char *path, int *hndl);

void Sys_MemFileOpenRead (const byte *memory, int size, int *hndl);

// Returns a file handle
int Sys_FileOpenWrite (const char *path);

void Sys_FileClose (int handle);
void Sys_FileSeek (int handle, int position);
int	 Sys_FileRead (int handle, void *dest, int count);
int	 Sys_FileWrite (int handle, const void *data, int count);
void Sys_mkdir (const char *path);

int Sys_FileType (const char *path);
/* returns an FS entity type, i.e. FS_ENT_FILE or FS_ENT_DIRECTORY.
 * returns FS_ENT_NONE (0) if no such file or directory is present. */

//
// system IO
//
FUNC_NORETURN void Sys_Quit (void);
FUNC_NORETURN void Sys_Error (const char *error, ...) FUNC_PRINTF (1, 2);
// an error will cause the entire program to exit

void Sys_Printf (const char *fmt, ...) FUNC_PRINTF (1, 2);
// send text to the console

double Sys_DoubleTime (void);

const char *Sys_ConsoleInput (void);

void Sys_Sleep (unsigned long msecs);
// yield for about 'msecs' milliseconds.

void Sys_SendKeyEvents (void);
// Perform Key_Event () callbacks until the input que is empty

#endif /* _QUAKE_SYS_H */
