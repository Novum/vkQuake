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

static qboolean textmode;

static cvar_t in_debugkeys = {"in_debugkeys", "0", CVAR_NONE};

// SDL2 Game Controller cvars
cvar_t joy_deadzone_look = {"joy_deadzone_look", "0.175", CVAR_ARCHIVE};
cvar_t joy_deadzone_move = {"joy_deadzone_move", "0.175", CVAR_ARCHIVE};
cvar_t joy_outer_threshold_look = {"joy_outer_threshold_look", "0.02", CVAR_ARCHIVE};
cvar_t joy_outer_threshold_move = {"joy_outer_threshold_move", "0.02", CVAR_ARCHIVE};
cvar_t joy_deadzone_trigger = {"joy_deadzone_trigger", "0.2", CVAR_ARCHIVE};
cvar_t joy_sensitivity_yaw = {"joy_sensitivity_yaw", "240", CVAR_ARCHIVE};
cvar_t joy_sensitivity_pitch = {"joy_sensitivity_pitch", "130", CVAR_ARCHIVE};
cvar_t joy_invert = {"joy_invert", "0", CVAR_ARCHIVE};
cvar_t joy_exponent = {"joy_exponent", "2", CVAR_ARCHIVE};
cvar_t joy_exponent_move = {"joy_exponent_move", "2", CVAR_ARCHIVE};
cvar_t joy_swapmovelook = {"joy_swapmovelook", "0", CVAR_ARCHIVE};
cvar_t joy_enable = {"joy_enable", "1", CVAR_ARCHIVE};

static SDL_JoystickID	   joy_active_instaceid = -1;
static SDL_GameController *joy_active_controller = NULL;

static qboolean no_mouse = false;

static int buttonremap[] = {
	K_MOUSE1, K_MOUSE3, /* right button		*/
	K_MOUSE2,			/* middle button	*/
	K_MOUSE4, K_MOUSE5};

/* total accumulated mouse movement since last frame */
static int total_dx, total_dy = 0;

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

static int SDLCALL IN_SDL2_FilterMouseEvents (void *userdata, SDL_Event *event)
{
	return IN_FilterMouseEvents (event);
}

static void IN_BeginIgnoringMouseEvents (void)
{
	SDL_EventFilter currentFilter = NULL;
	void		   *currentUserdata = NULL;
	SDL_GetEventFilter (&currentFilter, &currentUserdata);

	if (currentFilter != IN_SDL2_FilterMouseEvents)
		SDL_SetEventFilter (IN_SDL2_FilterMouseEvents, NULL);
}

static void IN_EndIgnoringMouseEvents (void)
{
	SDL_EventFilter currentFilter;
	void		   *currentUserdata;
	if (SDL_GetEventFilter (&currentFilter, &currentUserdata) == SDL_TRUE)
		SDL_SetEventFilter (NULL, NULL);
}

void IN_Activate (void)
{
	if (no_mouse)
		return;

#ifdef __APPLE__
	{
		// Work around https://github.com/sezero/quakespasm/issues/48
		int width, height;
		SDL_GetWindowSize ((SDL_Window *)VID_GetWindow (), &width, &height);
		SDL_WarpMouseInWindow ((SDL_Window *)VID_GetWindow (), width / 2, height / 2);
	}
#endif

	if (SDL_SetRelativeMouseMode (SDL_TRUE) != 0)
	{
		Con_Printf ("WARNING: SDL_SetRelativeMouseMode(SDL_TRUE) failed.\n");
	}

	IN_EndIgnoringMouseEvents ();

	total_dx = 0;
	total_dy = 0;
}

void IN_Deactivate (qboolean free_cursor)
{
	if (no_mouse)
		return;

	if (free_cursor)
	{
		SDL_SetRelativeMouseMode (SDL_FALSE);
	}

	/* discard all mouse events when input is deactivated */
	IN_BeginIgnoringMouseEvents ();
}

void IN_HideCursor ()
{
	if (no_mouse)
		return;

	if (SDL_SetRelativeMouseMode (SDL_TRUE) != 0)
	{
		Con_Printf ("WARNING: SDL_SetRelativeMouseMode(SDL_TRUE) failed.\n");
	}
}

void IN_StartupJoystick (void)
{
	int					i;
	int					nummappings;
	char				controllerdb[MAX_OSPATH];
	SDL_GameController *gamecontroller;

	if (COM_CheckParm ("-nojoy"))
		return;

	if (SDL_InitSubSystem (SDL_INIT_GAMECONTROLLER) == -1)
	{
		Con_Warning ("could not initialize SDL Game Controller\n");
		return;
	}

	// Load additional SDL2 controller definitions from gamecontrollerdb.txt
	q_snprintf (controllerdb, sizeof (controllerdb), "%s/gamecontrollerdb.txt", com_basedir);
	nummappings = SDL_GameControllerAddMappingsFromFile (controllerdb);
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

	for (i = 0; i < SDL_NumJoysticks (); i++)
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

void IN_Init (void)
{
	textmode = Key_TextEntry ();

	if (textmode)
		SDL_StartTextInput ();
	else
		SDL_StopTextInput ();

	if (safemode || COM_CheckParm ("-nomouse"))
	{
		no_mouse = true;
		/* discard all mouse events when input is deactivated */
		IN_BeginIgnoringMouseEvents ();
	}

	Cvar_RegisterVariable (&in_debugkeys);
	Cvar_RegisterVariable (&joy_sensitivity_yaw);
	Cvar_RegisterVariable (&joy_sensitivity_pitch);
	Cvar_RegisterVariable (&joy_deadzone_look);
	Cvar_RegisterVariable (&joy_deadzone_move);
	Cvar_RegisterVariable (&joy_outer_threshold_look);
	Cvar_RegisterVariable (&joy_outer_threshold_move);
	Cvar_RegisterVariable (&joy_deadzone_trigger);
	Cvar_RegisterVariable (&joy_invert);
	Cvar_RegisterVariable (&joy_exponent);
	Cvar_RegisterVariable (&joy_exponent_move);
	Cvar_RegisterVariable (&joy_swapmovelook);
	Cvar_RegisterVariable (&joy_enable);

	IN_Activate ();
	IN_StartupJoystick ();
}

void IN_Shutdown (void)
{
	IN_Deactivate (true);
	IN_ShutdownJoystick ();
}

extern cvar_t cl_maxpitch; /* johnfitz -- variable pitch clamping */
extern cvar_t cl_minpitch; /* johnfitz -- variable pitch clamping */
extern cvar_t scr_fov;

void IN_MouseMotion (int dx, int dy)
{
	if (cls.state != ca_connected || cls.signon != SIGNONS || key_dest != key_game || CL_AngleLocked ())
	{
		total_dx = 0;
		total_dy = 0;
		return;
	}
	total_dx += dx;
	total_dy += dy;
}

typedef struct joyaxis_s
{
	float x;
	float y;
} joyaxis_t;

typedef struct joy_buttonstate_s
{
	qboolean buttondown[SDL_CONTROLLER_BUTTON_MAX];
} joybuttonstate_t;

typedef struct axisstate_s
{
	float axisvalue[SDL_CONTROLLER_AXIS_MAX]; // normalized to +-1
} joyaxisstate_t;

static joybuttonstate_t joy_buttonstate;
static joyaxisstate_t	joy_axisstate;

static double joy_buttontimer[SDL_CONTROLLER_BUTTON_MAX];
static double joy_emulatedkeytimer[6];

/*
================
IN_AxisMagnitude

Returns the vector length of the given joystick axis
================
*/
static vec_t IN_AxisMagnitude (joyaxis_t axis)
{
	vec_t magnitude = sqrtf ((axis.x * axis.x) + (axis.y * axis.y));
	return magnitude;
}

/*
================
IN_ApplyEasing

assumes axis values are in [-1, 1] and the vector magnitude has been clamped at 1.
Raises the axis values to the given exponent, keeping signs.
================
*/
static joyaxis_t IN_ApplyEasing (joyaxis_t axis, float exponent)
{
	joyaxis_t result = {0};
	vec_t	  eased_magnitude;
	vec_t	  magnitude = IN_AxisMagnitude (axis);

	if (magnitude == 0)
		return result;

	eased_magnitude = powf (magnitude, exponent);

	result.x = axis.x * (eased_magnitude / magnitude);
	result.y = axis.y * (eased_magnitude / magnitude);
	return result;
}

/*
================

IN_ApplyDeadzone

in: raw joystick axis values converted to floats in +-1
out: applies a circular inner deadzone and a circular outer threshold and clamps the magnitude at 1
	 (my 360 controller is slightly non-circular and the stick travels further on the diagonals)

deadzone is expected to satisfy 0 < deadzone < 1 - outer_threshold
outer_threshold is expected to satisfy 0 < outer_threshold < 1 - deadzone

from https://github.com/jeremiah-sypult/Quakespasm-Rift
and adapted from http://www.third-helix.com/2013/04/12/doing-thumbstick-dead-zones-right.html
================
*/
static joyaxis_t IN_ApplyDeadzone (joyaxis_t axis, float deadzone, float outer_threshold)
{
	joyaxis_t result = {0};
	vec_t	  magnitude = IN_AxisMagnitude (axis);

	if (magnitude > deadzone)
	{
		const vec_t new_magnitude = q_min (1.0, (magnitude - deadzone) / (1.0 - deadzone - outer_threshold));
		const vec_t scale = new_magnitude / magnitude;
		result.x = axis.x * scale;
		result.y = axis.y * scale;
	}

	return result;
}

/*
================
IN_KeyForControllerButton
================
*/
static int IN_KeyForControllerButton (SDL_GameControllerButton button)
{
	switch (button)
	{
	case SDL_CONTROLLER_BUTTON_A:
		return K_ABUTTON;
	case SDL_CONTROLLER_BUTTON_B:
		return K_BBUTTON;
	case SDL_CONTROLLER_BUTTON_X:
		return K_XBUTTON;
	case SDL_CONTROLLER_BUTTON_Y:
		return K_YBUTTON;
	case SDL_CONTROLLER_BUTTON_BACK:
		return K_TAB;
	case SDL_CONTROLLER_BUTTON_START:
		return K_ESCAPE;
	case SDL_CONTROLLER_BUTTON_LEFTSTICK:
		return K_LTHUMB;
	case SDL_CONTROLLER_BUTTON_RIGHTSTICK:
		return K_RTHUMB;
	case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:
		return K_LSHOULDER;
	case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
		return K_RSHOULDER;
	case SDL_CONTROLLER_BUTTON_DPAD_UP:
		return K_UPARROW;
	case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
		return K_DOWNARROW;
	case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
		return K_LEFTARROW;
	case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
		return K_RIGHTARROW;
	default:
		return 0;
	}
}

/*
================
IN_JoyKeyEvent

Sends a Key_Event if a unpressed -> pressed or pressed -> unpressed transition occurred,
and generates key repeats if the button is held down.

Adapted from DarkPlaces by lordhavoc
================
*/
static void IN_JoyKeyEvent (qboolean wasdown, qboolean isdown, int key, double *timer)
{
	// we can't use `realtime` for key repeats because it is not monotomic
	const double currenttime = Sys_DoubleTime ();

	if (wasdown)
	{
		if (isdown)
		{
			if (currenttime >= *timer)
			{
				*timer = currenttime + 0.1;
				Key_Event (key, true);
			}
		}
		else
		{
			*timer = 0;
			Key_Event (key, false);
		}
	}
	else
	{
		if (isdown)
		{
			*timer = currenttime + 0.5;
			Key_Event (key, true);
		}
	}
}

/*
================
IN_Commands

Emit key events for game controller buttons, including emulated buttons for analog sticks/triggers
================
*/
void IN_Commands (void)
{
	joyaxisstate_t newaxisstate;
	int			   i;
	const float	   stickthreshold = 0.9;
	const float	   triggerthreshold = joy_deadzone_trigger.value;

	if (!joy_enable.value)
		return;

	if (!joy_active_controller)
		return;

	// emit key events for controller buttons
	for (i = 0; i < SDL_CONTROLLER_BUTTON_MAX; i++)
	{
		qboolean newstate = SDL_GameControllerGetButton (joy_active_controller, (SDL_GameControllerButton)i);
		qboolean oldstate = joy_buttonstate.buttondown[i];

		joy_buttonstate.buttondown[i] = newstate;

		// NOTE: This can cause a reentrant call of IN_Commands, via SCR_ModalMessage when confirming a new game.
		IN_JoyKeyEvent (oldstate, newstate, IN_KeyForControllerButton ((SDL_GameControllerButton)i), &joy_buttontimer[i]);
	}

	for (i = 0; i < SDL_CONTROLLER_AXIS_MAX; i++)
	{
		newaxisstate.axisvalue[i] = SDL_GameControllerGetAxis (joy_active_controller, (SDL_GameControllerAxis)i) / 32768.0f;
	}

	// emit emulated arrow keys so the analog sticks can be used in the menu
	if (key_dest != key_game)
	{
		IN_JoyKeyEvent (
			joy_axisstate.axisvalue[SDL_CONTROLLER_AXIS_LEFTX] < -stickthreshold, newaxisstate.axisvalue[SDL_CONTROLLER_AXIS_LEFTX] < -stickthreshold,
			K_LEFTARROW, &joy_emulatedkeytimer[0]);
		IN_JoyKeyEvent (
			joy_axisstate.axisvalue[SDL_CONTROLLER_AXIS_LEFTX] > stickthreshold, newaxisstate.axisvalue[SDL_CONTROLLER_AXIS_LEFTX] > stickthreshold,
			K_RIGHTARROW, &joy_emulatedkeytimer[1]);
		IN_JoyKeyEvent (
			joy_axisstate.axisvalue[SDL_CONTROLLER_AXIS_LEFTY] < -stickthreshold, newaxisstate.axisvalue[SDL_CONTROLLER_AXIS_LEFTY] < -stickthreshold,
			K_UPARROW, &joy_emulatedkeytimer[2]);
		IN_JoyKeyEvent (
			joy_axisstate.axisvalue[SDL_CONTROLLER_AXIS_LEFTY] > stickthreshold, newaxisstate.axisvalue[SDL_CONTROLLER_AXIS_LEFTY] > stickthreshold,
			K_DOWNARROW, &joy_emulatedkeytimer[3]);
	}

	// emit emulated keys for the analog triggers
	IN_JoyKeyEvent (
		joy_axisstate.axisvalue[SDL_CONTROLLER_AXIS_TRIGGERLEFT] > triggerthreshold, newaxisstate.axisvalue[SDL_CONTROLLER_AXIS_TRIGGERLEFT] > triggerthreshold,
		K_LTRIGGER, &joy_emulatedkeytimer[4]);
	IN_JoyKeyEvent (
		joy_axisstate.axisvalue[SDL_CONTROLLER_AXIS_TRIGGERRIGHT] > triggerthreshold,
		newaxisstate.axisvalue[SDL_CONTROLLER_AXIS_TRIGGERRIGHT] > triggerthreshold, K_RTRIGGER, &joy_emulatedkeytimer[5]);

	joy_axisstate = newaxisstate;
}

/*
================
IN_JoyMove
================
*/
void IN_JoyMove (usercmd_t *cmd)
{
	float		  speed;
	joyaxis_t	  moveRaw, moveDeadzone, moveEased;
	joyaxis_t	  lookRaw, lookDeadzone, lookEased;
	extern cvar_t sv_maxspeed;

	if (!joy_enable.value)
		return;

	if (!joy_active_controller)
		return;

	if (cl.paused || key_dest != key_game)
		return;

	moveRaw.x = joy_axisstate.axisvalue[SDL_CONTROLLER_AXIS_LEFTX];
	moveRaw.y = joy_axisstate.axisvalue[SDL_CONTROLLER_AXIS_LEFTY];
	lookRaw.x = joy_axisstate.axisvalue[SDL_CONTROLLER_AXIS_RIGHTX];
	lookRaw.y = joy_axisstate.axisvalue[SDL_CONTROLLER_AXIS_RIGHTY];

	if (joy_swapmovelook.value)
	{
		joyaxis_t temp = moveRaw;
		moveRaw = lookRaw;
		lookRaw = temp;
	}

	moveDeadzone = IN_ApplyDeadzone (moveRaw, joy_deadzone_move.value, joy_outer_threshold_move.value);
	lookDeadzone = IN_ApplyDeadzone (lookRaw, joy_deadzone_look.value, joy_outer_threshold_look.value);

	moveEased = IN_ApplyEasing (moveDeadzone, joy_exponent_move.value);
	lookEased = IN_ApplyEasing (lookDeadzone, joy_exponent.value);

	if ((in_speed.state & 1) ^ (cl_alwaysrun.value != 0.0 || cl_forwardspeed.value >= sv_maxspeed.value))
		// running
		speed = sv_maxspeed.value;
	else if (cl_forwardspeed.value >= sv_maxspeed.value)
		// not running, with always run = vanilla
		speed = q_min (sv_maxspeed.value, cl_forwardspeed.value / cl_movespeedkey.value);
	else
		// not running, with always run = off or quakespasm
		speed = cl_forwardspeed.value;

	cmd->sidemove += speed * moveEased.x;
	cmd->forwardmove -= speed * moveEased.y;

	if (CL_AngleLocked ())
		return;

	cl.viewangles[YAW] -= lookEased.x * joy_sensitivity_yaw.value * host_frametime;
	cl.viewangles[PITCH] += lookEased.y * joy_sensitivity_pitch.value * (joy_invert.value ? -1.0 : 1.0) * host_frametime;

	if (lookEased.x != 0 || lookEased.y != 0)
		V_StopPitchDrift ();

	/* johnfitz -- variable pitch clamping */
	if (cl.viewangles[PITCH] > cl_maxpitch.value)
		cl.viewangles[PITCH] = cl_maxpitch.value;
	if (cl.viewangles[PITCH] < cl_minpitch.value)
		cl.viewangles[PITCH] = cl_minpitch.value;
}

void IN_MouseMove (usercmd_t *cmd)
{
	float dmx, dmy;
	float sens;

	sens = tan (DEG2RAD (r_refdef.basefov) * 0.5f) / tan (DEG2RAD (scr_fov.value) * 0.5f);
	sens *= sensitivity.value;

	dmx = total_dx * sens;
	dmy = total_dy * sens;

	total_dx = 0;
	total_dy = 0;

	// do pause check after resetting total_d* so mouse movements during pause don't accumulate
	if (cl.paused || key_dest != key_game)
		return;

	if ((in_strafe.state & 1) || (lookstrafe.value && (in_mlook.state & 1)))
		cmd->sidemove_accumulator += m_side.value * dmx;
	else
		cl.viewangles[YAW] -= m_yaw.value * dmx;

	if (in_mlook.state & 1)
	{
		if (dmx || dmy)
			V_StopPitchDrift ();
	}

	if ((in_mlook.state & 1) && !(in_strafe.state & 1))
	{
		cl.viewangles[PITCH] += m_pitch.value * dmy;
		/* johnfitz -- variable pitch clamping */
		if (cl.viewangles[PITCH] > cl_maxpitch.value)
			cl.viewangles[PITCH] = cl_maxpitch.value;
		if (cl.viewangles[PITCH] < cl_minpitch.value)
			cl.viewangles[PITCH] = cl_minpitch.value;
	}
	else
	{
		if ((in_strafe.state & 1) && noclip_anglehack)
			cmd->upmove_accumulator -= m_forward.value * dmy;
		else
			cmd->forwardmove_accumulator -= m_forward.value * dmy;
	}
}

void IN_Move (usercmd_t *cmd)
{
	// We only want the latest joystick movements
	cmd->forwardmove = 0;
	cmd->sidemove = 0;
	cmd->upmove = 0;

	IN_JoyMove (cmd);
	IN_MouseMove (cmd);
}

void IN_ClearStates (void) {}

void IN_UpdateInputMode (void)
{
	qboolean want_textmode = Key_TextEntry ();
	if (textmode != want_textmode)
	{
		textmode = want_textmode;
		if (in_debugkeys.value)
			Con_Printf ("SDL_EnableUNICODE %d time: %g\n", textmode, Sys_DoubleTime ());
		if (textmode)
		{
			SDL_StartTextInput ();
			if (in_debugkeys.value)
				Con_Printf ("SDL_StartTextInput time: %g\n", Sys_DoubleTime ());
		}
		else
		{
			SDL_StopTextInput ();
			if (in_debugkeys.value)
				Con_Printf ("SDL_StopTextInput time: %g\n", Sys_DoubleTime ());
		}
	}
}

static inline int IN_SDL2_ScancodeToQuakeKey (SDL_Scancode scancode)
{
	switch (scancode)
	{
	case SDL_SCANCODE_TAB:
		return K_TAB;
	case SDL_SCANCODE_RETURN:
		return K_ENTER;
	case SDL_SCANCODE_RETURN2:
		return K_ENTER;
	case SDL_SCANCODE_ESCAPE:
		return K_ESCAPE;
	case SDL_SCANCODE_SPACE:
		return K_SPACE;

	case SDL_SCANCODE_A:
		return 'a';
	case SDL_SCANCODE_B:
		return 'b';
	case SDL_SCANCODE_C:
		return 'c';
	case SDL_SCANCODE_D:
		return 'd';
	case SDL_SCANCODE_E:
		return 'e';
	case SDL_SCANCODE_F:
		return 'f';
	case SDL_SCANCODE_G:
		return 'g';
	case SDL_SCANCODE_H:
		return 'h';
	case SDL_SCANCODE_I:
		return 'i';
	case SDL_SCANCODE_J:
		return 'j';
	case SDL_SCANCODE_K:
		return 'k';
	case SDL_SCANCODE_L:
		return 'l';
	case SDL_SCANCODE_M:
		return 'm';
	case SDL_SCANCODE_N:
		return 'n';
	case SDL_SCANCODE_O:
		return 'o';
	case SDL_SCANCODE_P:
		return 'p';
	case SDL_SCANCODE_Q:
		return 'q';
	case SDL_SCANCODE_R:
		return 'r';
	case SDL_SCANCODE_S:
		return 's';
	case SDL_SCANCODE_T:
		return 't';
	case SDL_SCANCODE_U:
		return 'u';
	case SDL_SCANCODE_V:
		return 'v';
	case SDL_SCANCODE_W:
		return 'w';
	case SDL_SCANCODE_X:
		return 'x';
	case SDL_SCANCODE_Y:
		return 'y';
	case SDL_SCANCODE_Z:
		return 'z';

	case SDL_SCANCODE_1:
		return '1';
	case SDL_SCANCODE_2:
		return '2';
	case SDL_SCANCODE_3:
		return '3';
	case SDL_SCANCODE_4:
		return '4';
	case SDL_SCANCODE_5:
		return '5';
	case SDL_SCANCODE_6:
		return '6';
	case SDL_SCANCODE_7:
		return '7';
	case SDL_SCANCODE_8:
		return '8';
	case SDL_SCANCODE_9:
		return '9';
	case SDL_SCANCODE_0:
		return '0';

	case SDL_SCANCODE_MINUS:
		return '-';
	case SDL_SCANCODE_EQUALS:
		return '=';
	case SDL_SCANCODE_LEFTBRACKET:
		return '[';
	case SDL_SCANCODE_RIGHTBRACKET:
		return ']';
	case SDL_SCANCODE_BACKSLASH:
		return '\\';
	case SDL_SCANCODE_NONUSHASH:
		return '#';
	case SDL_SCANCODE_SEMICOLON:
		return ';';
	case SDL_SCANCODE_APOSTROPHE:
		return '\'';
	case SDL_SCANCODE_GRAVE:
		return '`';
	case SDL_SCANCODE_COMMA:
		return ',';
	case SDL_SCANCODE_PERIOD:
		return '.';
	case SDL_SCANCODE_SLASH:
		return '/';
	case SDL_SCANCODE_NONUSBACKSLASH:
		return '\\';

	case SDL_SCANCODE_BACKSPACE:
		return K_BACKSPACE;
	case SDL_SCANCODE_UP:
		return K_UPARROW;
	case SDL_SCANCODE_DOWN:
		return K_DOWNARROW;
	case SDL_SCANCODE_LEFT:
		return K_LEFTARROW;
	case SDL_SCANCODE_RIGHT:
		return K_RIGHTARROW;

	case SDL_SCANCODE_LALT:
		return K_ALT;
	case SDL_SCANCODE_RALT:
		return K_ALT;
	case SDL_SCANCODE_LCTRL:
		return K_CTRL;
	case SDL_SCANCODE_RCTRL:
		return K_CTRL;
	case SDL_SCANCODE_LSHIFT:
		return K_SHIFT;
	case SDL_SCANCODE_RSHIFT:
		return K_SHIFT;

	case SDL_SCANCODE_F1:
		return K_F1;
	case SDL_SCANCODE_F2:
		return K_F2;
	case SDL_SCANCODE_F3:
		return K_F3;
	case SDL_SCANCODE_F4:
		return K_F4;
	case SDL_SCANCODE_F5:
		return K_F5;
	case SDL_SCANCODE_F6:
		return K_F6;
	case SDL_SCANCODE_F7:
		return K_F7;
	case SDL_SCANCODE_F8:
		return K_F8;
	case SDL_SCANCODE_F9:
		return K_F9;
	case SDL_SCANCODE_F10:
		return K_F10;
	case SDL_SCANCODE_F11:
		return K_F11;
	case SDL_SCANCODE_F12:
		return K_F12;
	case SDL_SCANCODE_INSERT:
		return K_INS;
	case SDL_SCANCODE_DELETE:
		return K_DEL;
	case SDL_SCANCODE_PAGEDOWN:
		return K_PGDN;
	case SDL_SCANCODE_PAGEUP:
		return K_PGUP;
	case SDL_SCANCODE_HOME:
		return K_HOME;
	case SDL_SCANCODE_END:
		return K_END;

	case SDL_SCANCODE_NUMLOCKCLEAR:
		return K_KP_NUMLOCK;
	case SDL_SCANCODE_KP_DIVIDE:
		return K_KP_SLASH;
	case SDL_SCANCODE_KP_MULTIPLY:
		return K_KP_STAR;
	case SDL_SCANCODE_KP_MINUS:
		return K_KP_MINUS;
	case SDL_SCANCODE_KP_7:
		return K_KP_HOME;
	case SDL_SCANCODE_KP_8:
		return K_KP_UPARROW;
	case SDL_SCANCODE_KP_9:
		return K_KP_PGUP;
	case SDL_SCANCODE_KP_PLUS:
		return K_KP_PLUS;
	case SDL_SCANCODE_KP_4:
		return K_KP_LEFTARROW;
	case SDL_SCANCODE_KP_5:
		return K_KP_5;
	case SDL_SCANCODE_KP_6:
		return K_KP_RIGHTARROW;
	case SDL_SCANCODE_KP_1:
		return K_KP_END;
	case SDL_SCANCODE_KP_2:
		return K_KP_DOWNARROW;
	case SDL_SCANCODE_KP_3:
		return K_KP_PGDN;
	case SDL_SCANCODE_KP_ENTER:
		return K_KP_ENTER;
	case SDL_SCANCODE_KP_0:
		return K_KP_INS;
	case SDL_SCANCODE_KP_PERIOD:
		return K_KP_DEL;

	case SDL_SCANCODE_LGUI:
		return K_COMMAND;
	case SDL_SCANCODE_RGUI:
		return K_COMMAND;

	case SDL_SCANCODE_PAUSE:
		return K_PAUSE;

	default:
		return 0;
	}
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
			key = IN_SDL2_ScancodeToQuakeKey (event.key.keysym.scancode);

			// though we also pass along the key using the proper current layout for Y/N prompts
			Key_EventWithKeycode (key, down, event.key.keysym.sym);
			break;

		case SDL_MOUSEBUTTONDOWN:
		case SDL_MOUSEBUTTONUP:
			if (event.button.button < 1 || event.button.button > sizeof (buttonremap) / sizeof (buttonremap[0]))
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
