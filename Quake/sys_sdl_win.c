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
#include "steam.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <mmsystem.h>
#include <shlobj.h>
#include <objbase.h>

#include <sys/types.h>
#include <errno.h>
#include <io.h>
#include <direct.h>

#include <dbghelp.h>

qboolean isDedicated;

#ifndef INVALID_FILE_ATTRIBUTES
#define INVALID_FILE_ATTRIBUTES ((DWORD) - 1)
#endif
int Sys_FileType (const char *path)
{
	DWORD result = GetFileAttributes (path);

	if (result == INVALID_FILE_ATTRIBUTES)
		return FS_ENT_NONE;
	if (result & FILE_ATTRIBUTE_DIRECTORY)
		return FS_ENT_DIRECTORY;

	return FS_ENT_FILE;
}

static void UTF8ToWideString (const char *src, wchar_t *dst, size_t maxchars)
{
	if (!MultiByteToWideChar (CP_UTF8, 0, src, -1, dst, (int)maxchars))
		Sys_Error ("MultiByteToWideChar failed: %lu", GetLastError ());
}

static void WideStringToUTF8 (const wchar_t *src, char *dst, size_t maxbytes)
{
	if (!WideCharToMultiByte (CP_UTF8, 0, src, -1, dst, (int)maxbytes, NULL, NULL))
		Sys_Error ("WideCharToMultiByte failed: %lu", GetLastError ());
}

/*
==============================================================================
STORE INSTALL LOCATIONS (from Ironwail)
==============================================================================
*/

static qboolean Sys_GetRegistryString (HKEY root, const wchar_t *dir, const wchar_t *keyname, char *out, size_t maxchars)
{
	LSTATUS err;
	HKEY	key;
	WCHAR	wpath[MAX_PATH + 1];
	DWORD	size, type;

	if (!maxchars)
		return false;
	*out = 0;

	err = RegOpenKeyExW (root, dir, 0, KEY_READ, &key);
	if (err != ERROR_SUCCESS)
		return false;

	// Note: string might not contain a terminating null character
	// https://docs.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regqueryvalueexw#remarks

	err = RegQueryValueExW (key, keyname, NULL, &type, NULL, &size);
	if (err != ERROR_SUCCESS || type != REG_SZ || size > sizeof (wpath) - sizeof (wpath[0]))
	{
		RegCloseKey (key);
		return false;
	}

	err = RegQueryValueExW (key, keyname, NULL, &type, (BYTE *)wpath, &size);
	RegCloseKey (key);
	if (err != ERROR_SUCCESS || type != REG_SZ)
		return false;

	wpath[size / sizeof (wpath[0])] = 0;

	if (WideCharToMultiByte (CP_UTF8, 0, wpath, -1, out, (int)maxchars, NULL, NULL) != 0)
		return true;
	*out = 0;
	return false;
}

qboolean Sys_GetSteamDir (char *path, size_t pathsize)
{
	return Sys_GetRegistryString (HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath", path, pathsize);
}

static qboolean Sys_StripTrailingSlashes (char *path)
{
	size_t i = strlen (path);
	while (i > 0 && (path[i - 1] == '\\' || path[i - 1] == '/'))
		path[--i] = 0;
	return i > 0;
}

qboolean Sys_GetGOGQuakeDir (char *path, size_t pathsize)
{
	if (!Sys_GetRegistryString (HKEY_LOCAL_MACHINE, L"SOFTWARE\\Wow6432Node\\GOG.com\\Games\\1435828198", L"path", path, pathsize))
		return false;

	return Sys_StripTrailingSlashes (path);
}

qboolean Sys_GetGOGQuakeEnhancedDir (char *path, size_t pathsize)
{
	if (!Sys_GetRegistryString (HKEY_LOCAL_MACHINE, L"SOFTWARE\\Wow6432Node\\GOG.com\\Games\\1739637082", L"path", path, pathsize))
		return false;

	return Sys_StripTrailingSlashes (path);
}

// https://github.com/libsdl-org/SDL/blob/120c76c84bbce4c1bfed4e9eb74e10678bd83120/src/core/windows/SDL_windows.c#L88-L99
static HRESULT Sys_InitCOM (void)
{
	HRESULT hr = CoInitializeEx (NULL, COINIT_APARTMENTTHREADED);
	if (hr == RPC_E_CHANGED_MODE)
		hr = CoInitializeEx (NULL, COINIT_MULTITHREADED);

	/* S_FALSE means success, but someone else already initialized. */
	/* You still need to call CoUninitialize in this case! */
	if (hr == S_FALSE)
		return S_OK;

	return hr;
}

static qboolean Sys_GetKnownFolder (const KNOWNFOLDERID *base, const char *subdir, char *path, size_t pathsize)
{
	PWSTR	 wpath;
	HRESULT	 hr;
	qboolean ret;

	hr = Sys_InitCOM ();
	if (FAILED (hr))
		return false;

	hr = SHGetKnownFolderPath (base, 0, NULL, &wpath);
	if (FAILED (hr))
	{
		CoUninitialize ();
		return false;
	}

	ret = WideCharToMultiByte (CP_UTF8, 0, wpath, -1, path, (int)pathsize, NULL, NULL) != 0;
	CoTaskMemFree (wpath);
	CoUninitialize ();

	return ret && (size_t)q_strlcat (path, subdir, pathsize) < pathsize;
}

qboolean Sys_GetSteamAPILibraryPath (char *path, size_t pathsize, const steamgame_t *game)
{
#ifdef _WIN64
	char installdir[MAX_OSPATH];
	if (!Steam_ResolvePath (installdir, sizeof (installdir), game))
		return false;
	return (size_t)q_snprintf (path, pathsize, "%s/rerelease/steam_api64.dll", installdir) < pathsize;
#else
	return false;
#endif
}

qboolean Sys_GetEGSManifestDir (char *path, size_t pathsize)
{
	return Sys_GetKnownFolder (&FOLDERID_ProgramData, "\\Epic\\EpicGamesLauncher\\Data\\Manifests", path, pathsize);
}

const char *Sys_GetEGSLauncherData (void)
{
	char	path[MAX_OSPATH];
	char   *buf;
	FILE   *file;
	int64_t filesize;
	int		size;

	if (!Sys_GetKnownFolder (&FOLDERID_ProgramData, "\\Epic\\UnrealEngineLauncher\\LauncherInstalled.dat", path, sizeof (path)))
		return NULL;

	file = fopen (path, "rb");
	if (!file)
		return NULL;

	_fseeki64 (file, 0, SEEK_END);
	filesize = _ftelli64 (file);
	_fseeki64 (file, 0, SEEK_SET);

	if (filesize < 2 || filesize > (1 << 30))
	{
		fclose (file);
		return NULL;
	}

	size = (int)filesize;
	buf = (char *)Mem_Alloc (size + 1);
	if (!buf)
	{
		fclose (file);
		return NULL;
	}

	if (fread (buf, size, 1, file) != 1)
	{
		Mem_Free (buf);
		fclose (file);
		return NULL;
	}
	buf[size] = '\0';

	fclose (file);

	// Convert to UTF-8 if needed
	if ((byte)buf[0] == 0xff && (byte)buf[1] == 0xfe) // UTF-16 little-endian byte order mark
	{
		int	  size8;
		char *buf8;

		size8 = WideCharToMultiByte (CP_UTF8, 0, (WCHAR *)(buf + 2), size / 2 - 1, NULL, 0, NULL, NULL);
		if (size8 <= 0)
		{
			Mem_Free (buf);
			return NULL;
		}

		buf8 = (char *)Mem_Alloc (size8 + 1);
		if (!buf8)
		{
			Mem_Free (buf);
			return NULL;
		}

		if (WideCharToMultiByte (CP_UTF8, 0, (WCHAR *)(buf + 2), size / 2 - 1, buf8, size8, NULL, NULL) != size8)
		{
			Mem_Free (buf8);
			Mem_Free (buf);
			return NULL;
		}
		buf8[size8] = '\0';

		Mem_Free (buf);
		buf = buf8;
	}

	return buf;
}

/*
==============================================================================
DIRECTORY ENUMERATION (from Ironwail)
==============================================================================
*/

typedef struct winfindfile_s
{
	findfile_t		 base;
	WIN32_FIND_DATAW data;
	HANDLE			 handle;
} winfindfile_t;

static void Sys_FillFindData (winfindfile_t *find)
{
	WideStringToUTF8 (find->data.cFileName, find->base.name, countof (find->base.name));
	find->base.attribs = 0;
	if (find->data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
		find->base.attribs |= FA_DIRECTORY;
}

findfile_t *Sys_FindFirst (const char *dir, const char *ext)
{
	winfindfile_t	*ret;
	char			 pattern[MAX_OSPATH];
	wchar_t			 wpattern[MAX_PATH];
	HANDLE			 handle;
	WIN32_FIND_DATAW data;

	if (!ext)
		ext = "*";
	else if (*ext == '.')
		++ext;
	q_snprintf (pattern, sizeof (pattern), "%s/*.%s", dir, ext);

	UTF8ToWideString (pattern, wpattern, countof (wpattern));
	handle = FindFirstFileW (wpattern, &data);

	if (handle == INVALID_HANDLE_VALUE)
		return NULL;

	ret = (winfindfile_t *)Mem_Alloc (sizeof (winfindfile_t));
	if (!ret)
		Sys_Error ("Sys_FindFirst: out of memory");
	ret->handle = handle;
	ret->data = data;
	Sys_FillFindData (ret);

	return (findfile_t *)ret;
}

findfile_t *Sys_FindNext (findfile_t *find)
{
	winfindfile_t *wfind = (winfindfile_t *)find;
	if (!FindNextFileW (wfind->handle, &wfind->data))
	{
		Sys_FindClose (find);
		return NULL;
	}
	Sys_FillFindData (wfind);
	return find;
}

void Sys_FindClose (findfile_t *find)
{
	if (find)
	{
		winfindfile_t *wfind = (winfindfile_t *)find;
		FindClose (wfind->handle);
		Mem_Free (wfind);
	}
}

static HANDLE hinput, houtput;
static char	  cwd[1024];
static double counter_freq;

// DbgHelp initialization:
static bool				win32_DbgHelp_init_success = false;
static CRITICAL_SECTION win32_DbgHelp_lock;

static intptr_t win32_Dwarf_offset = 0;

COMPILE_TIME_ASSERT (CHECK_LARGE_FILE_SUPPORT, sizeof (long long) >= sizeof (qfileofs_t));

int Sys_fseek (FILE *file, qfileofs_t ofs, int origin)
{
	return _fseeki64 (file, (long long)ofs, origin);
}

qfileofs_t Sys_ftell (FILE *file)
{
	return (qfileofs_t)_ftelli64 (file);
}

static void Sys_GetBasedir (char *argv0, char *dst, size_t dstsize)
{
	char  *tmp;
	size_t rc;

	rc = GetCurrentDirectory (dstsize, dst);
	if (rc == 0 || rc > dstsize)
		Sys_Error ("Couldn't determine current directory");

	tmp = dst;
	while (*tmp != 0)
		tmp++;
	while (*tmp == 0 && tmp != dst)
	{
		--tmp;
		if (tmp != dst && (*tmp == '/' || *tmp == '\\'))
			*tmp = 0;
	}
}

typedef enum
{
	dpi_unaware = 0,
	dpi_system_aware = 1,
	dpi_monitor_aware = 2
} dpi_awareness;
typedef BOOL (WINAPI *SetProcessDPIAwareFunc) ();
typedef HRESULT (WINAPI *SetProcessDPIAwarenessFunc) (dpi_awareness value);

static void Sys_SetDPIAware (void)
{
	HMODULE					   hUser32, hShcore;
	SetProcessDPIAwarenessFunc setDPIAwareness;
	SetProcessDPIAwareFunc	   setDPIAware;

	/* Neither SDL 1.2 nor SDL 2.0.3 can handle the OS scaling our window.
	  (e.g. https://bugzilla.libsdl.org/show_bug.cgi?id=2713)
	  Call SetProcessDpiAwareness/SetProcessDPIAware to opt out of scaling.
	*/

	hShcore = LoadLibraryA ("Shcore.dll");
	hUser32 = LoadLibraryA ("user32.dll");
	setDPIAwareness = (SetProcessDPIAwarenessFunc)(hShcore ? GetProcAddress (hShcore, "SetProcessDpiAwareness") : NULL);
	setDPIAware = (SetProcessDPIAwareFunc)(hUser32 ? GetProcAddress (hUser32, "SetProcessDPIAware") : NULL);

	if (setDPIAwareness) /* Windows 8.1+ */
		setDPIAwareness (dpi_monitor_aware);
	else if (setDPIAware) /* Windows Vista-8.0 */
		setDPIAware ();

	if (hShcore)
		FreeLibrary (hShcore);
	if (hUser32)
		FreeLibrary (hUser32);
}

static void Sys_SetTimerResolution (void)
{
	/* Set OS timer resolution to 1ms.
	   Works around buffer underruns with directsound and SDL2, but also
	   will make Sleep()/SDL_Dleay() accurate to 1ms which should help framerate
	   stability.
	*/
	timeBeginPeriod (1);
}

void Sys_Init (void)
{
	Sys_FileInit ();

	Sys_SetTimerResolution ();
	Sys_SetDPIAware ();

	memset (cwd, 0, sizeof (cwd));
	Sys_GetBasedir (NULL, cwd, sizeof (cwd));
	host_parms->basedir = cwd;

	/* userdirs not really necessary for windows guys.
	 * can be done if necessary, though... */
	host_parms->userdir = host_parms->basedir; /* code elsewhere relies on this ! */

	if (isDedicated)
	{
		if (!AllocConsole ())
		{
			isDedicated = false; /* so that we have a graphical error dialog */
			Sys_Error ("Couldn't create dedicated server console");
		}

		hinput = GetStdHandle (STD_INPUT_HANDLE);
		houtput = GetStdHandle (STD_OUTPUT_HANDLE);
	}

	counter_freq = (double)SDL_GetPerformanceFrequency ();

	// DbgHelp one-time initialization:
	HANDLE process = GetCurrentProcess ();

	SymSetOptions (SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_FAIL_CRITICAL_ERRORS | SYMOPT_NO_PROMPTS | SYMOPT_DEFERRED_LOADS);

	if (SymInitialize (process, NULL, TRUE))
		win32_DbgHelp_init_success = true;

	InitializeCriticalSection (&win32_DbgHelp_lock);

	// MSYS2 DWARF debug info is only usable if the stack addresses are offseted
	// by win32_Dwarf_offset
	// We need to look for the executable to look for the original ImageBase in the binary
	// ifself BEFORE it got patched in the loaded image...
	// this is the address we would get by : objdump -p vkQuake.exe | grep ImageBase
	wchar_t path[MAX_OSPATH];

	if (GetModuleFileNameW (NULL, path, MAX_OSPATH))
	{
		HANDLE hSelfExecutable =
			CreateFileW (path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

		if (hSelfExecutable != INVALID_HANDLE_VALUE)
		{
			BYTE  peHeader[4096];
			DWORD read = 0;
			if (ReadFile (hSelfExecutable, peHeader, sizeof (peHeader), &read, NULL) && read >= sizeof (IMAGE_DOS_HEADER))
			{
				IMAGE_DOS_HEADER   *dos = (IMAGE_DOS_HEADER *)peHeader;
				IMAGE_NT_HEADERS64 *nt = (IMAGE_NT_HEADERS64 *)(peHeader + dos->e_lfanew);

				uintptr_t LoadBase = (uintptr_t)GetModuleHandle (NULL);

				if ((uintptr_t)nt->OptionalHeader.ImageBase >= LoadBase)
				{
					win32_Dwarf_offset = (intptr_t)((uintptr_t)nt->OptionalHeader.ImageBase - LoadBase);
				}
				else
				{
					win32_Dwarf_offset = -(intptr_t)(LoadBase - (uintptr_t)nt->OptionalHeader.ImageBase);
				}
			}
			CloseHandle (hSelfExecutable);
		}
	}
}

void Sys_mkdir (const char *path)
{
	if (CreateDirectory (path, NULL) != 0)
		return;
	if (GetLastError () != ERROR_ALREADY_EXISTS)
		Sys_Error ("Unable to create directory %s", path);
}

static const char errortxt1[] = "\nERROR-OUT BEGIN\n\n";
static const char errortxt2[] = "\nQUAKE ERROR: ";

void Sys_Error (const char *error, ...)
{
	if (!Tasks_IsWorker ())
		host_parms->errstate++;

	va_list argptr;
	DWORD	dummy;

	va_start (argptr, error);

	char *text = q_vstrcatf (NULL, error, argptr);

	va_end (argptr);

	Sys_DebugBreak ();

	if (!Sys_IsInDebugger ())
	{
		const char *captured_stack_trace = Sys_StackTrace ();

		text = q_strcatf (text, "\nSTACK TRACE:\n%s", captured_stack_trace);

		Mem_Free (captured_stack_trace);
	}

	if (!Tasks_IsWorker ())
		PR_SwitchQCVM (NULL);

	if (Tasks_IsWorker () || isDedicated)
		WriteFile (houtput, errortxt1, strlen (errortxt1), &dummy, NULL);
	/* SDL will put these into its own stderr log,
	   so print to stderr even in graphical mode. */
	fputs (errortxt1, stdout);
	fputs (errortxt2, stdout);

	Sys_Printf ("%s\n\n", text);

	if (!Tasks_IsWorker () && !isDedicated && !Sys_IsInDebugger ())
	{
		PL_ErrorDialog (text);
	}
	else
	{
		WriteFile (houtput, errortxt2, strlen (errortxt2), &dummy, NULL);
		WriteFile (houtput, text, strlen (text), &dummy, NULL);
		WriteFile (houtput, "\r\n", 2, &dummy, NULL);
		SDL_Delay (3000); /* show the console 3 more seconds */
	}

	Mem_Free (text);

	exit (1);
}

void Sys_Printf (const char *fmt, ...)
{
	va_list argptr;
	DWORD	dummy;

	va_start (argptr, fmt);

	char *output_buffer = q_vstrcatf (NULL, fmt, argptr);

	va_end (argptr);

	if (Tasks_IsWorker () || isDedicated)
	{
		WriteFile (houtput, output_buffer, strlen (output_buffer), &dummy, NULL);
	}
	else
	{
		/* SDL will put these into its own stdout log,
		   so print to stdout even in graphical mode. */
		fputs (output_buffer, stdout);
		OutputDebugStringA (output_buffer);
	}

	Mem_Free (output_buffer);
}

void Sys_Quit (void)
{
	Host_Shutdown ();

	if (isDedicated)
		FreeConsole ();

	exit (0);
}

double Sys_DoubleTime (void)
{
	return (double)SDL_GetPerformanceCounter () / counter_freq;
}

const char *Sys_ConsoleInput (void)
{
	static char con_text[256];
	static int	textlen;
	int			ch;
	DWORD		dummy, numread, numevents;

	TEMP_ALLOC (INPUT_RECORD, recs, 1024);

	for (;;)
	{
		if (GetNumberOfConsoleInputEvents (hinput, &numevents) == 0)
			Sys_Error ("Error getting # of console events");

		if (!numevents)
			break;

		if (ReadConsoleInput (hinput, recs, 1, &numread) == 0)
		{
			TEMP_FREE (recs);
			Sys_Error ("Error reading console input");
		}

		if (numread != 1)
		{
			TEMP_FREE (recs);
			Sys_Error ("Couldn't read console input");
		}

		if (recs[0].EventType == KEY_EVENT)
		{
			if (recs[0].Event.KeyEvent.bKeyDown == TRUE)
			{
				ch = recs[0].Event.KeyEvent.uChar.AsciiChar;
				if (ch && recs[0].Event.KeyEvent.dwControlKeyState & SHIFT_PRESSED)
				{
					BYTE keyboard[256] = {0};
					WORD output;
					keyboard[SHIFT_PRESSED] = 0x80;
					if (ToAscii (VkKeyScan (ch), 0, keyboard, &output, 0) == 1)
						ch = output;
				}

				switch (ch)
				{
				case '\r':
					WriteFile (houtput, "\r\n", 2, &dummy, NULL);

					if (textlen != 0)
					{
						con_text[textlen] = 0;
						textlen = 0;
						TEMP_FREE (recs);
						return con_text;
					}

					break;

				case '\b':
					WriteFile (houtput, "\b \b", 3, &dummy, NULL);
					if (textlen != 0)
						textlen--;

					break;

				default:
					if (ch >= ' ')
					{
						WriteFile (houtput, &ch, 1, &dummy, NULL);
						con_text[textlen] = ch;
						textlen = (textlen + 1) & 0xff;
					}

					break;
				}
			}
		}
	}
	TEMP_FREE (recs);
	return NULL;
}

void Sys_Sleep (unsigned long msecs)
{
	/*	Sleep (msecs);*/
	SDL_Delay (msecs);
}

void Sys_SendKeyEvents (void)
{
	IN_Commands (); // ericw -- allow joysticks to add keys so they can be used to confirm SCR_ModalMessage
	IN_SendKeyEvents ();
}

bool Sys_PinCurrentThread (int core_index)
{
	// valid for both MSVC and MINGW
	//  Open the thread with necessary access rights
	DWORD  dwThreadId = GetCurrentThreadId ();
	HANDLE hThreadAccess = OpenThread (THREAD_SET_INFORMATION | THREAD_QUERY_INFORMATION, FALSE, dwThreadId);
	if (hThreadAccess == NULL)
	{
		return false;
	}

	// Define the processor affinity mask, fold beyond DWORD_PTR bit size...
	// should allow setting to 64 different cores on 64 bits, should be enough for anybody....
	DWORD_PTR mask = ((DWORD_PTR)1 << ((DWORD_PTR)core_index % (sizeof (DWORD_PTR) * 8)));

	// Set the thread affinity
	DWORD_PTR prevAffinityMask = SetThreadAffinityMask (hThreadAccess, mask);
	if (prevAffinityMask == 0)
	{
		CloseHandle (hThreadAccess);
		return false;
	}

	// Close the thread handle
	CloseHandle (hThreadAccess);

	return true;
}

const char *Sys_StackTrace (void)
{
#define MAX_STACK_FRAMES 24

	if (!win32_DbgHelp_init_success)
		return "[Not available.]\n";

	char *output_buffer = NULL;

	HANDLE process = GetCurrentProcess ();

	void *stack[MAX_STACK_FRAMES];

	int nb_frames = (int)CaptureStackBackTrace (0, MAX_STACK_FRAMES, stack, NULL);

	// DbgHelp Sym* has internal state that must be protected
	EnterCriticalSection (&win32_DbgHelp_lock);

	for (int frame_index = 0; frame_index < nb_frames; frame_index++)
	{
		DWORD64 addr = (DWORD64)(stack[frame_index]);

		// + 1 for null termination
		// buffer overlays SYMBOL_INFO + Name string
		char		 symbol_buffer[sizeof (SYMBOL_INFO) + MAX_OSPATH + 1];
		PSYMBOL_INFO symbol = (PSYMBOL_INFO)symbol_buffer;

		symbol->SizeOfStruct = sizeof (SYMBOL_INFO);
		symbol->MaxNameLen = MAX_OSPATH;

		const char *symbol_name = "[no symbols]";
		const BOOL	pdb_symbol_available = SymFromAddr (process, addr, 0, symbol);

		if (pdb_symbol_available)
			symbol_name = symbol->Name;

		IMAGEHLP_LINE64 line;
		DWORD			displacement = 0;
		line.SizeOfStruct = sizeof (IMAGEHLP_LINE64);

		const BOOL pdb_file_and_line_available = SymGetLineFromAddr64 (process, addr, &displacement, &line);

		// 1. All information:
		if (pdb_file_and_line_available && pdb_symbol_available)
		{
			// we only want the short file name, not the full path:
			const char *last_sep = FIND_LAST_DIRSEP (line.FileName);

			output_buffer = q_strcatf (
				output_buffer, "%-2i: %s - %s:%i\n", frame_index, symbol_name, (const char *)(last_sep ? last_sep + 1 : line.FileName), (int)line.LineNumber);
		}
		// 2. File and line, but no symbols, display the address in its place.
		else if (pdb_file_and_line_available)
		{
			// we only want the short file name, not the full path:
			const char *last_sep = FIND_LAST_DIRSEP (line.FileName);

			output_buffer = q_strcatf (
				output_buffer, "%-2i: 0x%" PRIxPTR " - %s:%i\n", frame_index, (uintptr_t)stack[frame_index],
				(const char *)(last_sep ? last_sep + 1 : line.FileName), (int)line.LineNumber);
		}
		// 3. Symbol but no file and line
		else if (pdb_symbol_available)
		{
			output_buffer = q_strcatf (output_buffer, "%-2i: %s\n", frame_index, symbol_name);
		}
		// 4. No symbols, no file and lines, this is likely a MSYS2 DWARF binary:
		else
		{
			uintptr_t dwarf_va = (uintptr_t)((intptr_t)stack[frame_index] + win32_Dwarf_offset);
			// display on 1 line to pass to addr2line easily:
			output_buffer = q_strcatf (output_buffer, "0x%" PRIxPTR " ", dwarf_va);

			if (frame_index == nb_frames - 1)
				output_buffer = q_strcatf (output_buffer, "\n");
		}
	}

	LeaveCriticalSection (&win32_DbgHelp_lock);

	return output_buffer;

#undef MAX_STACK_FRAMES
}

bool Sys_IsInDebugger (void)
{
	// skip the pop-up when in a debugger:
	BOOL debugger_attached = FALSE;
	CheckRemoteDebuggerPresent (GetCurrentProcess (), &debugger_attached);

	return debugger_attached != 0;
}

void Sys_DebugBreak (void)
{
	if (Sys_IsInDebugger ())
		DebugBreak ();
}
