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
void Sys_FileInit (void);
void Sys_Init (void);

//
// file IO : support huge files > 2GB (qfilesize_t), and huge file seeks > 2**31 bytes (qfileofs_t)
//
typedef long long qfilesize_t;
typedef long long qfileofs_t;

// return 0 on success
// origin is SEEK_CUR / SEEK_END / SEEK_SET
int Sys_fseek (FILE *file, qfileofs_t ofs, int origin);

qfileofs_t	Sys_ftell (FILE *file);
qfilesize_t Sys_filelength (FILE *f);

int Sys_fgetc (int handle);

// returns the file size or -1 if file is not present.
// the file should be in BINARY mode for stupid OSs that care
qfilesize_t Sys_FileOpenRead (const char *path, int *hndl);

void Sys_MemFileOpenRead (const byte *memory, qfilesize_t size, int *hndl);

// Duplicate a (read) handle : make a copy of a given handle designating the same resource,
// to independently seek and read from the original handle.
int Sys_DuplicateHandle (int handle);

// Returns a file handle
int Sys_FileOpenWrite (const char *path);

// same as Sys_filelength, but for handles.
qfilesize_t Sys_FileSize (int handle);

// same as Sys_ftell, but for handles.
qfileofs_t Sys_FilePos (int handle);

void Sys_FileClose (int handle);

// Sys_FileSeek return 0 on success like fseek().
int Sys_FileSeek (int handle, qfileofs_t position);

// fgetc() but for handles. Return EOF if
// we attenpt to read beyound the end of file.
int Sys_fgetc (int handle);

// feof() but for handles. Return true if we are in EOF condition. (Valid only for read file)
bool Sys_feof (int handle);

// A single File read/write is limited to 2**31 bytes, should be enough in all cases.
int Sys_FileRead (int handle, void *dest, int count);
int Sys_FileWrite (int handle, const void *data, int count);

void Sys_mkdir (const char *path);

/* returns an FS entity type, i.e. FS_ENT_FILE or FS_ENT_DIRECTORY.
 * returns FS_ENT_NONE (0) if no such file or directory is present. */
int Sys_FileType (const char *path);

//
// directory enumeration (from Ironwail)
//
typedef enum
{
	FA_DIRECTORY = 1,
} fileattribs_t;

typedef struct findfile_s
{
	fileattribs_t attribs;
	char		  name[MAX_OSPATH];
} findfile_t;

findfile_t *Sys_FindFirst (const char *dir, const char *ext);
findfile_t *Sys_FindNext (findfile_t *find);

// Only needs to be called manually when breaking out of the loop,
// otherwise the last Sys_FindNext will also close the handle
void Sys_FindClose (findfile_t *find);

//
// store install locations (from Ironwail); all return false/NULL when not found
//
qboolean	Sys_GetSteamDir (char *path, size_t pathsize);
qboolean	Sys_GetGOGQuakeDir (char *path, size_t pathsize);
qboolean	Sys_GetGOGQuakeEnhancedDir (char *path, size_t pathsize);
qboolean	Sys_GetEGSManifestDir (char *path, size_t pathsize);
const char *Sys_GetEGSLauncherData (void); // Mem_Alloc'ed buffer, caller Mem_Frees

// user dir of the official rerelease client (downloaded add-ons live there);
// steamlibrary is only needed on Linux (proton prefix) and may be NULL
qboolean Sys_GetNightdiveUserDir (char *path, size_t pathsize, const char *steamlibrary);

#ifdef USE_SDL3
// folder picker (SDL3 file dialog); starts at default_location if non-NULL,
// returns false when cancelled
qboolean Sys_SelectFolder (const char *title, const char *default_location, char *dst, size_t dstsize);
#endif

//
// system IO
//
FUNC_NORETURN void Sys_Quit (void);

// An error that will cause the entire program to exit. Can be safely called from any thread.
FUNC_NORETURN void Sys_Error (const char *error, ...) FUNC_PRINTF (1, 2);

// Print a message on the system console (NOT the Quake console !). Can be safely called from any thread.
void Sys_Printf (const char *fmt, ...) FUNC_PRINTF (1, 2);

double Sys_DoubleTime (void);

const char *Sys_ConsoleInput (void);

// yield for about 'msecs' milliseconds.
void Sys_Sleep (unsigned long msecs);

// Perform Key_Event () callbacks until the input que is empty
void Sys_SendKeyEvents (void);

// Pin the calling Thread to core core_index, return true if succcessfull
bool Sys_PinCurrentThread (int core_index);

// Return the stack trace at the point of this call.
// This string is allocated by Mem_Alloc
const char *Sys_StackTrace (void);

// Return true if we are running in a debugger
bool Sys_IsInDebugger (void);

// Break in a debugger if we are running in one, else do nothing
void Sys_DebugBreak (void);

#endif /* _QUAKE_SYS_H */
