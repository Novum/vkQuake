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

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <mmsystem.h>

#include <sys/types.h>
#include <errno.h>
#include <io.h>
#include <direct.h>

qboolean isDedicated;

#ifndef INVALID_FILE_ATTRIBUTES
#define INVALID_FILE_ATTRIBUTES ((DWORD)-1)
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

static HANDLE hinput, houtput;
static char	  cwd[1024];
static double counter_freq;

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
	va_list argptr;
	char	text[1024];
	DWORD	dummy;

	host_parms->errstate++;

	va_start (argptr, error);
	q_vsnprintf (text, sizeof (text), error, argptr);
	va_end (argptr);

	PR_SwitchQCVM (NULL);

	if (isDedicated)
		WriteFile (houtput, errortxt1, strlen (errortxt1), &dummy, NULL);
	/* SDL will put these into its own stderr log,
	   so print to stderr even in graphical mode. */
	fputs (errortxt1, stderr);
	fputs (errortxt2, stderr);
	fputs (text, stderr);
	fputs ("\n\n", stderr);
	if (!isDedicated)
		PL_ErrorDialog (text);
	else
	{
		WriteFile (houtput, errortxt2, strlen (errortxt2), &dummy, NULL);
		WriteFile (houtput, text, strlen (text), &dummy, NULL);
		WriteFile (houtput, "\r\n", 2, &dummy, NULL);
		SDL_Delay (3000); /* show the console 3 more seconds */
	}

#ifdef _DEBUG
	__debugbreak ();
#endif

	exit (1);
}

void Sys_Printf (const char *fmt, ...)
{
	va_list argptr;
	char	text[1024];
	DWORD	dummy;

	va_start (argptr, fmt);
	q_vsnprintf (text, sizeof (text), fmt, argptr);
	va_end (argptr);

	if (isDedicated)
	{
		WriteFile (houtput, text, strlen (text), &dummy, NULL);
	}
	else
	{
		/* SDL will put these into its own stdout log,
		   so print to stdout even in graphical mode. */
		fputs (text, stdout);
		OutputDebugStringA (text);
	}
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
	static char	 con_text[256];
	static int	 textlen;
	INPUT_RECORD recs[1024];
	int			 ch;
	DWORD		 dummy, numread, numevents;

	for (;;)
	{
		if (GetNumberOfConsoleInputEvents (hinput, &numevents) == 0)
			Sys_Error ("Error getting # of console events");

		if (!numevents)
			break;

		if (ReadConsoleInput (hinput, recs, 1, &numread) == 0)
			Sys_Error ("Error reading console input");

		if (numread != 1)
			Sys_Error ("Couldn't read console input");

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
