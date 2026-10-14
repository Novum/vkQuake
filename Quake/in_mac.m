/*
Copyright (C) 2026 vkQuake developers

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
See the GNU General Public License for more details.

The GCMouse integration is adapted from Simple DirectMedia Layer:
Copyright (C) 1997-2026 Sam Lantinga <slouken@libsdl.org>

This software is provided 'as-is', without any express or implied
warranty. In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:
1. The origin of this software must not be misrepresented; you must not
   claim that you wrote the original software. If you use this software
   in a product, an acknowledgment in the product documentation would be
   appreciated but is not required.
2. Altered source versions must be plainly marked as such, and must not be
   misrepresented as being the original software.
3. This notice may not be removed or altered from any source distribution.
*/

// q_stdinc.h must define true/false before Apple's headers include stdbool.h.
#include "quakedef.h"
#include "in_sdl.h"

#import <Foundation/Foundation.h>
#import <GameController/GameController.h>

// Motion-only adaptation of SDL's GCMouse backend:
// https://github.com/libsdl-org/SDL/pull/14741
// SDL still handles buttons, scrolling, cursor position and capture.
static cvar_t		   in_macrawmouse = {"in_macrawmouse", "1", CVAR_ARCHIVE};
static NSMutableArray *raw_mice;
static id			   connect_observer;
static id			   disconnect_observer;

qboolean IN_MacRawMouseActive (void)
{
	if (!in_macrawmouse.value || ![raw_mice count])
		return false;

	SDL_Window *window = (SDL_Window *)VID_GetWindow ();
	if (!window || SDL_GetKeyboardFocus () != window)
		return false;

#ifdef USE_SDL3
	return SDL_GetWindowRelativeMouseMode (window);
#else
	return SDL_GetRelativeMouseMode ();
#endif
}

static void IN_ConnectMacRawMouse (GCMouse *mouse) API_AVAILABLE (macos (11.0))
{
	if (!raw_mice || [raw_mice containsObject:mouse])
		return;

	[raw_mice addObject:mouse];
	// All callbacks and observers run on the game thread. Never access engine
	// state from GameController's default background handler queue.
	mouse.handlerQueue = dispatch_get_main_queue ();
	mouse.mouseInput.mouseMovedHandler = ^(GCMouseInput *input, float dx, float dy) {
	  if (IN_MacRawMouseActive ())
	  {
		  IN_SetGamepadInputActive (false);
		  IN_MouseMotion (dx, -dy);
	  }
	};
}

void IN_InitMacRawMouse (void)
{
	Cvar_RegisterVariable (&in_macrawmouse);
#ifdef USE_SDL3
	// SDL's 3.5 development series (leading to 3.6) owns GCMouse handlers.
	// Do not replace them when using development or newer stable libraries.
	if (SDL_GetVersion () >= SDL_VERSIONNUM (3, 5, 0))
		return;
#endif

	@autoreleasepool
	{
		// Match SDL's macOS 14 minimum: GCMouse motion/button events are broken
		// on some MacBooks running Monterey and Ventura.
		if (@available (macOS 14.0, *))
		{
			raw_mice = [[NSMutableArray alloc] init];
			NSNotificationCenter *center = [NSNotificationCenter defaultCenter];
			connect_observer = [center addObserverForName:GCMouseDidConnectNotification
												   object:nil
													queue:[NSOperationQueue mainQueue]
											   usingBlock:^(NSNotification *note) {
												 IN_ConnectMacRawMouse (note.object);
											   }];
			disconnect_observer = [center addObserverForName:GCMouseDidDisconnectNotification
													  object:nil
													   queue:[NSOperationQueue mainQueue]
												  usingBlock:^(NSNotification *note) {
													if (!raw_mice)
														return;
													GCMouse *mouse = note.object;
													mouse.mouseInput.mouseMovedHandler = nil;
													[raw_mice removeObject:mouse];
												  }];
			for (GCMouse *mouse in [GCMouse mice])
				IN_ConnectMacRawMouse (mouse);
		}
	}
}

void IN_ShutdownMacRawMouse (void)
{
	@autoreleasepool
	{
		NSNotificationCenter *center = [NSNotificationCenter defaultCenter];
		if (connect_observer)
			[center removeObserver:connect_observer];
		if (disconnect_observer)
			[center removeObserver:disconnect_observer];
		connect_observer = nil;
		disconnect_observer = nil;
		if (@available (macOS 14.0, *))
		{
			for (GCMouse *mouse in raw_mice)
				mouse.mouseInput.mouseMovedHandler = nil;
		}
		[raw_mice release];
		raw_mice = nil;
	}
}
