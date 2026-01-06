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
#include <stdio.h>

static void Sys_AtExit (void)
{
	SDL_Quit ();
}

static void Sys_InitSDL (void)
{
#ifdef USE_SDL3
	int version = SDL_GetVersion ();
	int major = SDL_VERSIONNUM_MAJOR (version);
	int minor = SDL_VERSIONNUM_MINOR (version);
	int patch = SDL_VERSIONNUM_MICRO (version);
#else
	SDL_version version;
	SDL_GetVersion (&version);
	int major = version.major;
	int minor = version.minor;
	int patch = version.patch;
#endif

	Sys_Printf ("Using SDL version %i.%i.%i\n", major, minor, patch);

#ifdef USE_SDL3
	const bool initialized = SDL_Init (0);
#else
	const bool initialized = SDL_Init (0) >= 0;
#endif

	if (!initialized)
	{
		Sys_Error ("Couldn't init SDL: %s", SDL_GetError ());
	}

#ifdef _DEBUG
#ifdef USE_SDL3
	SDL_SetLogPriorities (SDL_LOG_PRIORITY_DEBUG);
#else
	SDL_LogSetAllPriority (SDL_LOG_PRIORITY_DEBUG);
#endif
#endif

	atexit (Sys_AtExit);
}

static quakeparms_t parms;

int main (int argc, char *argv[])
{
	double time, oldtime, newtime;

	host_parms = &parms;
	parms.basedir = ".";

	parms.argc = argc;
	parms.argv = argv;

	parms.errstate = 0;

	COM_InitArgv (parms.argc, parms.argv);

	isDedicated = (COM_CheckParm ("-dedicated") != 0);

	Sys_InitSDL ();

	Sys_Init ();

#ifdef USE_SDL3
	Sys_Printf ("Detected %d CPUs.\n", SDL_GetNumLogicalCPUCores ());
#else
	Sys_Printf ("Detected %d CPUs.\n", SDL_GetCPUCount ());
#endif
	Sys_Printf ("Initializing %s\n", ENGINE_NAME_AND_VER);
#if defined(__clang_version__)
	Sys_Printf ("Built with Clang " __clang_version__ "\n");
#elif defined(__GNUC__)
	Sys_Printf ("Built with GCC %u.%u.%u\n", __GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__);
#elif defined(_MSC_FULL_VER)
	Sys_Printf ("Built with Microsoft C %u\n", _MSC_FULL_VER);
#else
	Sys_Printf ("Built with unknown compiler\n");
#endif

	Sys_Printf ("Host_Init\n");
	Host_Init ();

	oldtime = Sys_DoubleTime ();
	if (isDedicated)
	{
		while (1)
		{
			newtime = Sys_DoubleTime ();
			time = newtime - oldtime;

			while (time < sys_ticrate.value)
			{
				SDL_Delay (1);
				newtime = Sys_DoubleTime ();
				time = newtime - oldtime;
			}

			Host_Frame (time);
			oldtime = newtime;
		}
	}
	else
		while (1)
		{
			/* If we have no input focus at all, sleep a bit */
			if ((!listening && !VID_HasMouseOrInputFocus ()) || cl.paused)
			{
				SDL_Delay (16);
			}
			/* If we're minimised, sleep a bit more */
			if (!listening && VID_IsMinimized ())
				SDL_Delay (32);
			newtime = Sys_DoubleTime ();
			time = newtime - oldtime;

			Host_Frame (time);

			oldtime = newtime;
		}

	return 0;
}
