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

static qboolean textmode;

cvar_t in_debugkeys = {"in_debugkeys", "0", CVAR_NONE};

qboolean joy_altmodifier_pressed = false;

// SDL Game Controller cvars
static cvar_t joy_deadzone_look = {"joy_deadzone_look", "0.175", CVAR_ARCHIVE_GAME};
static cvar_t joy_deadzone_move = {"joy_deadzone_move", "0.175", CVAR_ARCHIVE_GAME};
static cvar_t joy_outer_threshold_look = {"joy_outer_threshold_look", "0.02", CVAR_ARCHIVE_GAME};
static cvar_t joy_outer_threshold_move = {"joy_outer_threshold_move", "0.02", CVAR_ARCHIVE_GAME};
static cvar_t joy_deadzone_trigger = {"joy_deadzone_trigger", "0.2", CVAR_ARCHIVE_GAME};
static cvar_t joy_sensitivity_yaw = {"joy_sensitivity_yaw", "240", CVAR_ARCHIVE_GAME};
static cvar_t joy_sensitivity_pitch = {"joy_sensitivity_pitch", "130", CVAR_ARCHIVE_GAME};
static cvar_t joy_invert = {"joy_invert", "0", CVAR_ARCHIVE_GAME};
static cvar_t joy_exponent = {"joy_exponent", "2", CVAR_ARCHIVE_GAME};
static cvar_t joy_exponent_move = {"joy_exponent_move", "2", CVAR_ARCHIVE_GAME};
static cvar_t joy_swapmovelook = {"joy_swapmovelook", "0", CVAR_ARCHIVE_GAME};
static cvar_t joy_enable = {"joy_enable", "1", CVAR_ARCHIVE_GAME};
static cvar_t joy_always_active = {"joy_always_active", "0", CVAR_ARCHIVE_GAME};
static cvar_t joy_rumble = {"joy_rumble", "0.3", CVAR_ARCHIVE_GAME};
static cvar_t gyro_enable = {"gyro_enable", "1", CVAR_ARCHIVE_GAME};
static cvar_t gyro_mode = {"gyro_mode", "2", CVAR_ARCHIVE_GAME};
static cvar_t gyro_turning_axis = {"gyro_turning_axis", "0", CVAR_ARCHIVE_GAME};
static cvar_t gyro_yawsensitivity = {"gyro_yawsensitivity", "2.5", CVAR_ARCHIVE_GAME};
static cvar_t gyro_pitchsensitivity = {"gyro_pitchsensitivity", "2.5", CVAR_ARCHIVE_GAME};
static cvar_t gyro_calibration_x = {"gyro_calibration_x", "0", CVAR_ARCHIVE_GAME};
static cvar_t gyro_calibration_y = {"gyro_calibration_y", "0", CVAR_ARCHIVE_GAME};
static cvar_t gyro_calibration_z = {"gyro_calibration_z", "0", CVAR_ARCHIVE_GAME};
static cvar_t gyro_noise_thresh = {"gyro_noise_thresh", "1.5", CVAR_ARCHIVE_GAME};
static cvar_t joy_flick = {"joy_flick", "0", CVAR_ARCHIVE_GAME};
static cvar_t joy_flick_time = {"joy_flick_time", "0.125", CVAR_ARCHIVE_GAME};
static cvar_t joy_flick_recenter = {"joy_flick_recenter", "0", CVAR_ARCHIVE_GAME};
static cvar_t joy_flick_deadzone = {"joy_flick_deadzone", "0.9", CVAR_ARCHIVE_GAME};
static cvar_t joy_flick_noise_thresh = {"joy_flick_noise_thresh", "2", CVAR_ARCHIVE_GAME};
static cvar_t joy_flick_adjust_speed = {"joy_flick_adjust_speed", "30", CVAR_ARCHIVE_GAME};

#ifdef USE_SDL3
#define SDL3_GET_WINDOW (SDL_Window *)VID_GetWindow ()
#else
#define SDL3_GET_WINDOW

#define SDL_GamepadButton	 SDL_GameControllerButton
#define SDL_GamepadAxis		 SDL_GameControllerAxis
#define SDL_GetGamepadButton SDL_GameControllerGetButton
#define SDL_GetGamepadAxis	 SDL_GameControllerGetAxis

#define SDL_EVENT_MOUSE_MOTION SDL_MOUSEMOTION

#define SDL_GAMEPAD_BUTTON_COUNT SDL_CONTROLLER_BUTTON_MAX

#define SDL_GAMEPAD_AXIS_COUNT		   SDL_CONTROLLER_AXIS_MAX
#define SDL_GAMEPAD_AXIS_LEFTX		   SDL_CONTROLLER_AXIS_LEFTX
#define SDL_GAMEPAD_AXIS_LEFTY		   SDL_CONTROLLER_AXIS_LEFTY
#define SDL_GAMEPAD_AXIS_RIGHTX		   SDL_CONTROLLER_AXIS_RIGHTX
#define SDL_GAMEPAD_AXIS_RIGHTY		   SDL_CONTROLLER_AXIS_RIGHTY
#define SDL_GAMEPAD_AXIS_LEFT_TRIGGER  SDL_CONTROLLER_AXIS_TRIGGERLEFT
#define SDL_GAMEPAD_AXIS_RIGHT_TRIGGER SDL_CONTROLLER_AXIS_TRIGGERRIGHT

#define SDL_GAMEPAD_BUTTON_COUNT		  SDL_CONTROLLER_BUTTON_MAX
#define SDL_GAMEPAD_BUTTON_SOUTH		  SDL_CONTROLLER_BUTTON_A
#define SDL_GAMEPAD_BUTTON_EAST			  SDL_CONTROLLER_BUTTON_B
#define SDL_GAMEPAD_BUTTON_WEST			  SDL_CONTROLLER_BUTTON_X
#define SDL_GAMEPAD_BUTTON_NORTH		  SDL_CONTROLLER_BUTTON_Y
#define SDL_GAMEPAD_BUTTON_BACK			  SDL_CONTROLLER_BUTTON_BACK
#define SDL_GAMEPAD_BUTTON_START		  SDL_CONTROLLER_BUTTON_START
#define SDL_GAMEPAD_BUTTON_LEFT_STICK	  SDL_CONTROLLER_BUTTON_LEFTSTICK
#define SDL_GAMEPAD_BUTTON_RIGHT_STICK	  SDL_CONTROLLER_BUTTON_RIGHTSTICK
#define SDL_GAMEPAD_BUTTON_LEFT_SHOULDER  SDL_CONTROLLER_BUTTON_LEFTSHOULDER
#define SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER SDL_CONTROLLER_BUTTON_RIGHTSHOULDER
#define SDL_GAMEPAD_BUTTON_DPAD_UP		  SDL_CONTROLLER_BUTTON_DPAD_UP
#define SDL_GAMEPAD_BUTTON_DPAD_DOWN	  SDL_CONTROLLER_BUTTON_DPAD_DOWN
#define SDL_GAMEPAD_BUTTON_DPAD_LEFT	  SDL_CONTROLLER_BUTTON_DPAD_LEFT
#define SDL_GAMEPAD_BUTTON_DPAD_RIGHT	  SDL_CONTROLLER_BUTTON_DPAD_RIGHT
#define SDL_GAMEPAD_BUTTON_MISC1		  SDL_CONTROLLER_BUTTON_MISC1
#define SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1  SDL_CONTROLLER_BUTTON_PADDLE1
#define SDL_GAMEPAD_BUTTON_LEFT_PADDLE1	  SDL_CONTROLLER_BUTTON_PADDLE2
#define SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2  SDL_CONTROLLER_BUTTON_PADDLE3
#define SDL_GAMEPAD_BUTTON_LEFT_PADDLE2	  SDL_CONTROLLER_BUTTON_PADDLE4
#define SDL_GAMEPAD_BUTTON_TOUCHPAD		  SDL_CONTROLLER_BUTTON_TOUCHPAD

#define SDL3_GET_WINDOW
#endif

static qboolean no_mouse = false;
static qboolean gamepad_input_active;

void IN_SetGamepadInputActive (qboolean active)
{
	gamepad_input_active = active;
}

static qboolean IN_JoyActive (void)
{
	return joy_enable.value && joy_active_controller && (joy_always_active.value || gamepad_input_active);
}

/* total accumulated mouse movement since last frame,
   float because SDL3 reports relative motion in subpixel precision */
static float total_dx, total_dy = 0;

enum
{
	GYRO_BUTTON_IGNORED,
	GYRO_BUTTON_ENABLES,
	GYRO_BUTTON_DISABLES,
	GYRO_BUTTON_INVERTS_DIR
};

#define GYRO_CALIBRATION_SAMPLES 300
static float		gyro_yaw, gyro_pitch;
static float		gyro_accum[3];
static unsigned int gyro_calibration_samples;
static qboolean		gyro_present;
static qboolean		gyro_button_pressed;
static struct
{
	float yaw;
	float pitch;
	float yaw_delta;
	float previous_fraction;
	float previous_angle;
	float previous_scale;
} flick;

static void IN_ResetFlickState (void)
{
	memset (&flick, 0, sizeof (flick));
}

void IN_SetGyroAvailable (qboolean available)
{
	gyro_present = available;
	gyro_yaw = gyro_pitch = 0.f;
	if (!available)
	{
		gyro_calibration_samples = 0;
		gyro_button_pressed = false;
		IN_ResetFlickState ();
	}
}

static float IN_FilterGyroSample (float previous, float current)
{
	const float threshold = DEG2RAD (gyro_noise_thresh.value);
	const float difference = fabsf (current - previous);
	if (threshold > 0.f && difference < threshold)
	{
		const float fraction = difference / threshold;
		current = previous + (current - previous) * (0.01f + 0.99f * fraction * fraction);
	}
	return current;
}

void IN_GyroSample (const float sample[3])
{
	if (gyro_calibration_samples)
	{
		for (int i = 0; i < 3; i++)
			gyro_accum[i] += sample[i];
		if (--gyro_calibration_samples == 0)
		{
			const float scale = 1.f / GYRO_CALIBRATION_SAMPLES;
			Cvar_SetValueQuick (&gyro_calibration_x, gyro_accum[0] * scale);
			Cvar_SetValueQuick (&gyro_calibration_y, gyro_accum[1] * scale);
			Cvar_SetValueQuick (&gyro_calibration_z, gyro_accum[2] * scale);
			Con_Printf ("Gyro calibration finished\n");
		}
		return;
	}

	const float previous_yaw = gyro_yaw;
	const float previous_pitch = gyro_pitch;
	gyro_yaw = gyro_turning_axis.value ? -(sample[2] - gyro_calibration_z.value) : sample[1] - gyro_calibration_y.value;
	gyro_pitch = sample[0] - gyro_calibration_x.value;
	gyro_yaw = IN_FilterGyroSample (previous_yaw, gyro_yaw);
	gyro_pitch = IN_FilterGyroSample (previous_pitch, gyro_pitch);
}

static void IN_StartGyroCalibration_f (void)
{
	if (!gyro_present)
	{
		Con_Printf ("No gyro sensor available\n");
		return;
	}
	IN_Rumble (0, 0, 0);
	memset (gyro_accum, 0, sizeof (gyro_accum));
	gyro_calibration_samples = GYRO_CALIBRATION_SAMPLES;
	Con_Printf ("Calibrating gyro; keep the controller still...\n");
}

static void IN_GyroActionDown (void)
{
	gyro_button_pressed = true;
}

static void IN_GyroActionUp (void)
{
	gyro_button_pressed = false;
}

static float IN_ClampedFraction (float value, float min, float max)
{
	return CLAMP (0.f, (value - min) / (max - min), 1.f);
}

static void IN_JoyAltModifierDown (void)
{
	joy_altmodifier_pressed = true;
}

static void IN_JoyAltModifierUp (void)
{
	joy_altmodifier_pressed = false;
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

#ifdef USE_SDL3
	if (!SDL_SetWindowRelativeMouseMode ((SDL_Window *)VID_GetWindow (), true))
	{
		Con_Printf ("WARNING: SDL_SetWindowRelativeMouseMode(true) failed.\n");
	}
#else
	if (SDL_SetRelativeMouseMode (SDL_TRUE) != 0)
	{
		Con_Printf ("WARNING: SDL_SetRelativeMouseMode(SDL_TRUE) failed.\n");
	}
#endif

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
#ifdef USE_SDL3
		SDL_SetWindowRelativeMouseMode ((SDL_Window *)VID_GetWindow (), false);
#else
		SDL_SetRelativeMouseMode (SDL_FALSE);
#endif
	}

	/* discard all mouse events when input is deactivated */
	IN_BeginIgnoringMouseEvents ();
}

void IN_DeactivateForConsole (void)
{
	IN_Deactivate (true);
}

void IN_ScaleMouseCoords (float x, float y, int *outx, int *outy)
{
	SDL_Window *window = (SDL_Window *)VID_GetWindow ();
	int			w = 0, h = 0;

	SDL_GetWindowSize (window, &w, &h);
	if (w > 0 && h > 0)
	{
		x = x * vid.width / w;
		y = y * vid.height / h;
	}
	*outx = (int)x;
	*outy = (int)y;
}

void IN_GetMousePos (int *outx, int *outy)
{
#ifdef USE_SDL3
	float x, y;
#else
	int x, y;
#endif
	SDL_GetMouseState (&x, &y);
	IN_ScaleMouseCoords ((float)x, (float)y, outx, outy);
}

void IN_HideCursor ()
{
	if (no_mouse)
		return;

#ifdef USE_SDL3
	if (!SDL_SetWindowRelativeMouseMode ((SDL_Window *)VID_GetWindow (), true))
	{
		Con_Printf ("WARNING: SDL_SetWindowRelativeMouseMode(true) failed.\n");
	}
#else
	if (SDL_SetRelativeMouseMode (SDL_TRUE) != 0)
	{
		Con_Printf ("WARNING: SDL_SetRelativeMouseMode(SDL_TRUE) failed.\n");
	}
#endif
}

void IN_Init (void)
{
	textmode = Key_TextEntry ();

	if (textmode)
		SDL_StartTextInput (SDL3_GET_WINDOW);
	else
		SDL_StopTextInput (SDL3_GET_WINDOW);

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
	Cvar_RegisterVariable (&joy_always_active);
	Cvar_RegisterVariable (&joy_rumble);
	Cvar_RegisterVariable (&gyro_enable);
	Cvar_RegisterVariable (&gyro_mode);
	Cvar_RegisterVariable (&gyro_turning_axis);
	Cvar_RegisterVariable (&gyro_yawsensitivity);
	Cvar_RegisterVariable (&gyro_pitchsensitivity);
	Cvar_RegisterVariable (&gyro_calibration_x);
	Cvar_RegisterVariable (&gyro_calibration_y);
	Cvar_RegisterVariable (&gyro_calibration_z);
	Cvar_RegisterVariable (&gyro_noise_thresh);
	Cvar_RegisterVariable (&joy_flick);
	Cvar_RegisterVariable (&joy_flick_time);
	Cvar_RegisterVariable (&joy_flick_recenter);
	Cvar_RegisterVariable (&joy_flick_deadzone);
	Cvar_RegisterVariable (&joy_flick_noise_thresh);
	Cvar_RegisterVariable (&joy_flick_adjust_speed);
	Cmd_AddCommand ("gyro_calibrate", IN_StartGyroCalibration_f);
	Cmd_AddCommand ("+gyroaction", IN_GyroActionDown);
	Cmd_AddCommand ("-gyroaction", IN_GyroActionUp);

	Cmd_AddCommand ("+altmodifier", IN_JoyAltModifierDown);
	Cmd_AddCommand ("-altmodifier", IN_JoyAltModifierUp);

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

void IN_MouseMotion (float dx, float dy)
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
	qboolean buttondown[SDL_GAMEPAD_BUTTON_COUNT];
} joybuttonstate_t;

typedef struct axisstate_s
{
	float axisvalue[SDL_GAMEPAD_AXIS_COUNT]; // normalized to +-1
} joyaxisstate_t;

static joybuttonstate_t joy_buttonstate;
static joyaxisstate_t	joy_axisstate;

static double joy_buttontimer[SDL_GAMEPAD_BUTTON_COUNT];
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
static int IN_KeyForControllerButton (SDL_GamepadButton button)
{
	switch (button)
	{
	case SDL_GAMEPAD_BUTTON_SOUTH:
		return K_ABUTTON;
	case SDL_GAMEPAD_BUTTON_EAST:
		return K_BBUTTON;
	case SDL_GAMEPAD_BUTTON_WEST:
		return K_XBUTTON;
	case SDL_GAMEPAD_BUTTON_NORTH:
		return K_YBUTTON;
	case SDL_GAMEPAD_BUTTON_BACK:
		return K_TAB;
	case SDL_GAMEPAD_BUTTON_START:
		return K_ESCAPE;
	case SDL_GAMEPAD_BUTTON_LEFT_STICK:
		return K_LTHUMB;
	case SDL_GAMEPAD_BUTTON_RIGHT_STICK:
		return K_RTHUMB;
	case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:
		return K_LSHOULDER;
	case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER:
		return K_RSHOULDER;
	case SDL_GAMEPAD_BUTTON_DPAD_UP:
		return K_DPAD_UP;
	case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
		return K_DPAD_DOWN;
	case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
		return K_DPAD_LEFT;
	case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
		return K_DPAD_RIGHT;
	case SDL_GAMEPAD_BUTTON_MISC1:
		return K_MISC1;
	case SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1:
		return K_PADDLE1;
	case SDL_GAMEPAD_BUTTON_LEFT_PADDLE1:
		return K_PADDLE2;
	case SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2:
		return K_PADDLE3;
	case SDL_GAMEPAD_BUTTON_LEFT_PADDLE2:
		return K_PADDLE4;
	case SDL_GAMEPAD_BUTTON_TOUCHPAD:
		return K_TOUCHPAD;
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
				IN_SetGamepadInputActive (true);
				Key_Event (key, true);
			}
		}
		else
		{
			*timer = 0;
			IN_SetGamepadInputActive (true);
			Key_Event (key, false);
		}
	}
	else
	{
		if (isdown)
		{
			*timer = currenttime + 0.5;
			IN_SetGamepadInputActive (true);
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
	for (i = 0; i < SDL_GAMEPAD_BUTTON_COUNT; i++)
	{
		qboolean newstate = SDL_GetGamepadButton (joy_active_controller, (SDL_GamepadButton)i);
		qboolean oldstate = joy_buttonstate.buttondown[i];
		int		 key;

		joy_buttonstate.buttondown[i] = newstate;
		key = IN_KeyForControllerButton ((SDL_GamepadButton)i);
		if (key_dest != key_game && !M_WaitingForKeyBinding ())
		{
			if (key == K_DPAD_UP)
				key = K_UPARROW;
			else if (key == K_DPAD_DOWN)
				key = K_DOWNARROW;
			else if (key == K_DPAD_LEFT)
				key = K_LEFTARROW;
			else if (key == K_DPAD_RIGHT)
				key = K_RIGHTARROW;
		}

		// NOTE: This can cause a reentrant call of IN_Commands, via SCR_ModalMessage when confirming a new game.
		IN_JoyKeyEvent (oldstate, newstate, key, &joy_buttontimer[i]);
	}

	for (i = 0; i < SDL_GAMEPAD_AXIS_COUNT; i++)
	{
		newaxisstate.axisvalue[i] = SDL_GetGamepadAxis (joy_active_controller, (SDL_GamepadAxis)i) / 32768.0f;
	}

	if (key_dest == key_game && !gamepad_input_active)
	{
		joyaxis_t move_axis = {newaxisstate.axisvalue[SDL_GAMEPAD_AXIS_LEFTX], newaxisstate.axisvalue[SDL_GAMEPAD_AXIS_LEFTY]};
		joyaxis_t look_axis = {newaxisstate.axisvalue[SDL_GAMEPAD_AXIS_RIGHTX], newaxisstate.axisvalue[SDL_GAMEPAD_AXIS_RIGHTY]};
		if (joy_swapmovelook.value)
		{
			const joyaxis_t temp = move_axis;
			move_axis = look_axis;
			look_axis = temp;
		}
		if (IN_AxisMagnitude (look_axis) > joy_deadzone_look.value || IN_AxisMagnitude (move_axis) > joy_deadzone_move.value)
			IN_SetGamepadInputActive (true);
	}

	// emit emulated arrow keys so the analog sticks can be used in the menu
	if (key_dest != key_game)
	{
		IN_JoyKeyEvent (
			joy_axisstate.axisvalue[SDL_GAMEPAD_AXIS_LEFTX] < -stickthreshold, newaxisstate.axisvalue[SDL_GAMEPAD_AXIS_LEFTX] < -stickthreshold, K_LEFTARROW,
			&joy_emulatedkeytimer[0]);
		IN_JoyKeyEvent (
			joy_axisstate.axisvalue[SDL_GAMEPAD_AXIS_LEFTX] > stickthreshold, newaxisstate.axisvalue[SDL_GAMEPAD_AXIS_LEFTX] > stickthreshold, K_RIGHTARROW,
			&joy_emulatedkeytimer[1]);
		IN_JoyKeyEvent (
			joy_axisstate.axisvalue[SDL_GAMEPAD_AXIS_LEFTY] < -stickthreshold, newaxisstate.axisvalue[SDL_GAMEPAD_AXIS_LEFTY] < -stickthreshold, K_UPARROW,
			&joy_emulatedkeytimer[2]);
		IN_JoyKeyEvent (
			joy_axisstate.axisvalue[SDL_GAMEPAD_AXIS_LEFTY] > stickthreshold, newaxisstate.axisvalue[SDL_GAMEPAD_AXIS_LEFTY] > stickthreshold, K_DOWNARROW,
			&joy_emulatedkeytimer[3]);
	}

	// emit emulated keys for the analog triggers
	IN_JoyKeyEvent (
		joy_axisstate.axisvalue[SDL_GAMEPAD_AXIS_LEFT_TRIGGER] > triggerthreshold, newaxisstate.axisvalue[SDL_GAMEPAD_AXIS_LEFT_TRIGGER] > triggerthreshold,
		K_LTRIGGER, &joy_emulatedkeytimer[4]);
	IN_JoyKeyEvent (
		joy_axisstate.axisvalue[SDL_GAMEPAD_AXIS_RIGHT_TRIGGER] > triggerthreshold, newaxisstate.axisvalue[SDL_GAMEPAD_AXIS_RIGHT_TRIGGER] > triggerthreshold,
		K_RTRIGGER, &joy_emulatedkeytimer[5]);

	joy_axisstate = newaxisstate;

	if (IN_JoyActive () && IN_HasRumble () && !gyro_calibration_samples && joy_rumble.value > 0.f)
	{
		const float strength = CLAMP (0.f, joy_rumble.value, 1.f) * 65535.f;
		const float low = IN_ClampedFraction (S_GetLoFreqLevel (), 0.067f, 0.45f);
		float		high = IN_ClampedFraction (S_GetHiFreqLevel (), 0.061f, 0.45f);
		high *= high;
		IN_Rumble ((uint16_t)(low * strength), (uint16_t)(high * strength), 100);
	}
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

	moveRaw.x = joy_axisstate.axisvalue[SDL_GAMEPAD_AXIS_LEFTX];
	moveRaw.y = joy_axisstate.axisvalue[SDL_GAMEPAD_AXIS_LEFTY];
	lookRaw.x = joy_axisstate.axisvalue[SDL_GAMEPAD_AXIS_RIGHTX];
	lookRaw.y = joy_axisstate.axisvalue[SDL_GAMEPAD_AXIS_RIGHTY];

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

	if (joy_flick.value && gyro_present && gyro_enable.value)
	{
		float		   angle = anglemod (atan2f (lookRaw.y, lookRaw.x) * (180.f / M_PI) + 90.f);
		const float	   magnitude = IN_AxisMagnitude (lookRaw);
		const qboolean active = magnitude > joy_flick_deadzone.value;
		const qboolean was_active = flick.previous_scale > joy_flick_deadzone.value;

		if (active != was_active && !was_active)
		{
			flick.previous_fraction = 0.f;
			flick.yaw = angle > 180.f ? angle - 360.f : angle;
			flick.pitch = cl.viewangles[PITCH];
		}
		else if (active)
		{
			float delta = angle - flick.previous_angle;
			if (delta > 180.f)
				delta -= 360.f;
			else if (delta < -180.f)
				delta += 360.f;
			if (joy_flick_noise_thresh.value > 0.f)
			{
				float filter = fabsf (delta) / joy_flick_noise_thresh.value;
				if (filter < 1.f)
				{
					filter = 0.05f + 0.95f * filter * filter;
					delta *= filter;
					angle = anglemod (flick.previous_angle + delta);
				}
			}
			flick.yaw_delta += delta;
		}

		float delta = joy_flick_adjust_speed.value > 0.f ? flick.yaw_delta * q_min (1.0, host_rawframetime * joy_flick_adjust_speed.value) : flick.yaw_delta;
		if (fabsf (delta) > 0.01f)
		{
			cl.viewangles[YAW] -= delta;
			flick.yaw_delta -= delta;
		}

		float		fraction = joy_flick_time.value > 0.f ? CLAMP (0.f, flick.previous_fraction + host_rawframetime / joy_flick_time.value, 1.f) : 1.f;
		const float eased = 1.f - (1.f - fraction) * (1.f - fraction);
		const float previous_eased = 1.f - (1.f - flick.previous_fraction) * (1.f - flick.previous_fraction);
		delta = eased - previous_eased;
		cl.viewangles[YAW] -= flick.yaw * delta;
		cl.viewangles[PITCH] -= flick.pitch * delta * CLAMP (0.f, joy_flick_recenter.value, 1.f);

		flick.previous_scale = magnitude;
		flick.previous_angle = angle;
		flick.previous_fraction = fraction;
	}
	else
	{
		IN_ResetFlickState ();
		const double frame_time = host_rawframetime;
		cl.viewangles[YAW] -= lookEased.x * joy_sensitivity_yaw.value * frame_time;
		cl.viewangles[PITCH] += lookEased.y * joy_sensitivity_pitch.value * (joy_invert.value ? -1.0 : 1.0) * frame_time;

		if (lookEased.x != 0 || lookEased.y != 0)
			V_StopPitchDrift ();
	}

	/* johnfitz -- variable pitch clamping */
	if (cl.viewangles[PITCH] > cl_maxpitch.value)
		cl.viewangles[PITCH] = cl_maxpitch.value;
	if (cl.viewangles[PITCH] < cl_minpitch.value)
		cl.viewangles[PITCH] = cl_minpitch.value;
}

static void IN_GyroMove (void)
{
	float scale;
	if (!gyro_enable.value || !gyro_present || !IN_JoyActive () || gyro_calibration_samples || cl.paused || key_dest != key_game || CL_AngleLocked ())
		return;

	scale = (180.f / M_PI) * host_rawframetime;
	scale *= tanf (DEG2RAD (r_refdef.basefov) * 0.5f) / tanf (DEG2RAD (scr_fov.value) * 0.5f);
	switch ((int)gyro_mode.value)
	{
	case GYRO_BUTTON_ENABLES:
		if (!gyro_button_pressed)
			return;
		break;
	case GYRO_BUTTON_DISABLES:
		if (gyro_button_pressed)
			return;
		break;
	case GYRO_BUTTON_INVERTS_DIR:
		if (gyro_button_pressed)
			scale = -scale;
		break;
	default:
		break;
	}

	cl.viewangles[YAW] += scale * gyro_yaw * gyro_yawsensitivity.value;
	cl.viewangles[PITCH] -= scale * gyro_pitch * gyro_pitchsensitivity.value;
	V_StopPitchDrift ();
	cl.viewangles[PITCH] = CLAMP (cl_minpitch.value, cl.viewangles[PITCH], cl_maxpitch.value);
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
	IN_GyroMove ();
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
			SDL_StartTextInput (SDL3_GET_WINDOW);
			if (in_debugkeys.value)
				Con_Printf ("SDL_StartTextInput time: %g\n", Sys_DoubleTime ());
		}
		else
		{
			SDL_StopTextInput (SDL3_GET_WINDOW);
			if (in_debugkeys.value)
				Con_Printf ("SDL_StopTextInput time: %g\n", Sys_DoubleTime ());
		}
	}
}
