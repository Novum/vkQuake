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
#include "in_sdl.h"

#ifndef USE_SDL3

extern cvar_t in_debugkeys;

static SDL_JoystickID joy_active_instaceid = -1;
SDL_GameController	 *joy_active_controller = NULL;

void IN_StartupJoystick (void)
{
	char				controllerdb[MAX_OSPATH];
	SDL_GameController *gamecontroller;

	if (COM_CheckParm ("-nojoy"))
		return;

	if (SDL_InitSubSystem (SDL_INIT_GAMECONTROLLER) == -1)
	{
		Con_Warning ("could not initialize SDL Game Controller\n");
		return;
	}

	// Load additional SDL controller definitions from gamecontrollerdb.txt
	q_snprintf (controllerdb, sizeof (controllerdb), "%s/gamecontrollerdb.txt", com_basedir);
	int nummappings = SDL_GameControllerAddMappingsFromFile (controllerdb);
	if (nummappings > 0)
		Con_Printf ("%d mappings loaded from gamecontrollerdb.txt\n", nummappings);

	// Also try host_parms->userdir
	if (host_parms->userdir != host_parms->basedir)
	{
		q_snprintf (controllerdb, sizeof (controllerdb), "%s/gamecontrollerdb.txt", host_parms->userdir);
		nummappings = SDL_GameControllerAddMappingsFromFile (controllerdb);
		if (nummappings > 0)
			Con_Printf ("%d mappings loaded from gamecontrollerdb.txt\n", nummappings);
	}

	for (int i = 0; i < SDL_NumJoysticks (); i++)
	{
		const char *joyname = SDL_JoystickNameForIndex (i);
		if (SDL_IsGameController (i))
		{
			const char *controllername = SDL_GameControllerNameForIndex (i);
			gamecontroller = SDL_GameControllerOpen (i);
			if (gamecontroller)
			{
				Con_Printf ("detected controller: %s\n", controllername != NULL ? controllername : "NULL");

				joy_active_instaceid = SDL_JoystickInstanceID (SDL_GameControllerGetJoystick (gamecontroller));
				joy_active_controller = gamecontroller;
				break;
			}
			else
			{
				Con_Warning ("failed to open controller: %s\n", controllername != NULL ? controllername : "NULL");
			}
		}
		else
		{
			Con_Warning ("joystick missing controller mappings: %s\n", joyname != NULL ? joyname : "NULL");
		}
	}
}

void IN_ShutdownJoystick (void)
{
	SDL_QuitSubSystem (SDL_INIT_GAMECONTROLLER);
}

static void IN_DebugTextEvent (SDL_Event *event)
{
	Con_Printf ("SDL_TEXTINPUT '%s' time: %g\n", event->text.text, Sys_DoubleTime ());
}

static void IN_DebugKeyEvent (SDL_Event *event)
{
	const char *eventtype = (event->key.state == SDL_PRESSED) ? "SDL_KEYDOWN" : "SDL_KEYUP";
	Con_Printf (
		"%s scancode: '%s' keycode: '%s' time: %g\n", eventtype, SDL_GetScancodeName (event->key.keysym.scancode), SDL_GetKeyName (event->key.keysym.sym),
		Sys_DoubleTime ());
}

void IN_SendKeyEvents (void)
{
	SDL_Event event;
	int		  key;
	qboolean  down;

	while (SDL_PollEvent (&event))
	{
		switch (event.type)
		{
		case SDL_WINDOWEVENT:
			if (event.window.event == SDL_WINDOWEVENT_FOCUS_GAINED)
			{
				S_UnblockSound ();
				VID_FocusGained ();
			}
			else if (event.window.event == SDL_WINDOWEVENT_FOCUS_LOST)
			{
				S_BlockSound ();
				VID_FocusLost ();
			}
			else if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
			{
				vid.width = event.window.data1;
				vid.height = event.window.data2;
				vid.restart_next_frame = true;
				Cvar_FindVar ("scr_conscale")->callback (NULL);
			}
			break;
		case SDL_TEXTINPUT:
			if (in_debugkeys.value)
				IN_DebugTextEvent (&event);

			// SDL2: We use SDL_TEXTINPUT for typing in the console / chat.
			// SDL2 uses the local keyboard layout and handles modifiers
			// (shift for uppercase, etc.) for us.
			{
				unsigned char *ch;
				for (ch = (unsigned char *)event.text.text; *ch; ch++)
					if ((*ch & ~0x7F) == 0)
						Char_Event (*ch);
			}
			break;
		case SDL_KEYDOWN:
		case SDL_KEYUP:
			down = (event.key.state == SDL_PRESSED);

			if (in_debugkeys.value)
				IN_DebugKeyEvent (&event);

			// SDL2: we interpret the keyboard as the US layout, so keybindings
			// are based on key position, not the label on the key cap.
			key = IN_SDL_ScancodeToQuakeKey (event.key.keysym.scancode);

			// though we also pass along the key using the proper current layout for Y/N prompts
			Key_EventWithKeycode (key, down, event.key.keysym.sym);
			break;

		case SDL_MOUSEBUTTONDOWN:
		case SDL_MOUSEBUTTONUP:
			if (event.button.button < 1 || event.button.button > countof (buttonremap))
			{
				Con_Printf ("Ignored event for mouse button %d\n", event.button.button);
				break;
			}
			Key_Event (buttonremap[event.button.button - 1], event.button.state == SDL_PRESSED);
			break;

		case SDL_MOUSEWHEEL:
			if (event.wheel.y > 0)
			{
				Key_Event (K_MWHEELUP, true);
				Key_Event (K_MWHEELUP, false);
			}
			else if (event.wheel.y < 0)
			{
				Key_Event (K_MWHEELDOWN, true);
				Key_Event (K_MWHEELDOWN, false);
			}
			break;

		case SDL_MOUSEMOTION:
			IN_MouseMotion (event.motion.xrel, event.motion.yrel);
			break;

		case SDL_CONTROLLERDEVICEADDED:
			if (joy_active_instaceid == -1)
			{
				joy_active_controller = SDL_GameControllerOpen (event.cdevice.which);
				if (joy_active_controller == NULL)
					Con_DPrintf ("Couldn't open game controller\n");
				else
				{
					SDL_Joystick *joy;
					joy = SDL_GameControllerGetJoystick (joy_active_controller);
					joy_active_instaceid = SDL_JoystickInstanceID (joy);
				}
			}
			else
				Con_DPrintf ("Ignoring SDL_CONTROLLERDEVICEADDED\n");
			break;
		case SDL_CONTROLLERDEVICEREMOVED:
			if (joy_active_instaceid != -1 && event.cdevice.which == joy_active_instaceid)
			{
				SDL_GameControllerClose (joy_active_controller);
				joy_active_controller = NULL;
				joy_active_instaceid = -1;
			}
			else
				Con_DPrintf ("Ignoring SDL_CONTROLLERDEVICEREMOVED\n");
			break;
		case SDL_CONTROLLERDEVICEREMAPPED:
			Con_DPrintf ("Ignoring SDL_CONTROLLERDEVICEREMAPPED\n");
			break;

		case SDL_QUIT:
			CL_Disconnect ();
			Sys_Quit ();
			break;

		default:
			break;
		}
	}
}

static int SDLCALL IN_FilterMouseEvents (const SDL_Event *event)
{
	switch (event->type)
	{
	case SDL_MOUSEMOTION:
		// case SDL_MOUSEBUTTONDOWN:
		// case SDL_MOUSEBUTTONUP:
		return 0;
	}

	return 1;
}

static int SDLCALL IN_SDL_FilterMouseEvents (void *userdata, SDL_Event *event)
{
	return IN_FilterMouseEvents (event);
}

void IN_BeginIgnoringMouseEvents (void)
{
	SDL_EventFilter currentFilter = NULL;
	void		   *currentUserdata = NULL;
	SDL_GetEventFilter (&currentFilter, &currentUserdata);

	if (currentFilter != IN_SDL_FilterMouseEvents)
		SDL_SetEventFilter (IN_SDL_FilterMouseEvents, NULL);
}

void IN_EndIgnoringMouseEvents (void)
{
	SDL_EventFilter currentFilter;
	void		   *currentUserdata;
	if (SDL_GetEventFilter (&currentFilter, &currentUserdata) == SDL_TRUE)
		SDL_SetEventFilter (NULL, NULL);
}

#endif
