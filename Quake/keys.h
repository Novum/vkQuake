/*
Copyright (C) 1996-2001 Id Software, Inc.
Copyright (C) 2002-2009 John Fitzgibbons and others
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

#ifndef _QUAKE_KEYS_H
#define _QUAKE_KEYS_H

//
// these are the key numbers that should be passed to Key_Event
//
// clang-format off

typedef enum keycode_t
{
	K_TAB				= 9,
	K_ENTER				= 13,
	K_ESCAPE			= 27,
	K_SPACE				= 32,

// normal keys should be passed as lowercased ascii

	K_BACKSPACE			= 127,
	K_UPARROW,
	K_DOWNARROW,
	K_LEFTARROW,
	K_RIGHTARROW,

	K_ALT,
	K_CTRL,
	K_SHIFT,
	K_F1,
	K_F2,
	K_F3,
	K_F4,
	K_F5,
	K_F6,
	K_F7,
	K_F8,
	K_F9,
	K_F10,
	K_F11,
	K_F12,
	K_INS,
	K_DEL,
	K_PGDN,
	K_PGUP,
	K_HOME,
	K_END,

	K_KP_NUMLOCK,
	K_KP_SLASH,
	K_KP_STAR,
	K_KP_MINUS,
	K_KP_HOME,
	K_KP_UPARROW,
	K_KP_PGUP,
	K_KP_PLUS,
	K_KP_LEFTARROW,
	K_KP_5,
	K_KP_RIGHTARROW,
	K_KP_END,
	K_KP_DOWNARROW,
	K_KP_PGDN,
	K_KP_ENTER,
	K_KP_INS,
	K_KP_DEL,

	K_COMMAND,

	K_CAPSLOCK,
	K_SCROLLLOCK,
	K_PRINTSCREEN,

//
// mouse buttons generate virtual keys
//
	K_MOUSE1			= 200,
	K_MOUSE2,
	K_MOUSE3,

// thumb buttons
	K_MOUSE4,
	K_MOUSE5,

// JACK: Intellimouse(c) Mouse Wheel Support
	K_MWHEELUP,
	K_MWHEELDOWN,

// SDL2 game controller keys
	K_LTHUMB,
	K_RTHUMB,
	K_LSHOULDER,
	K_RSHOULDER,
	K_ABUTTON,
	K_BBUTTON,
	K_XBUTTON,
	K_YBUTTON,
	K_LTRIGGER,
	K_RTRIGGER,
	K_MISC1,
	K_PADDLE1,
	K_PADDLE2,
	K_PADDLE3,
	K_PADDLE4,
	K_TOUCHPAD,

	K_PAUSE,

	NUM_KEYCODES,
} keycode_t;

// clang-format on

#define	MAX_KEYS		256

#define	MAXCMDLINE	256

typedef enum {key_game, key_console, key_message, key_menu} keydest_t;
typedef enum textmode_t
{
	TEXTMODE_OFF,		// no char events
	TEXTMODE_ON,		// char events, show on-screen keyboard
	TEXTMODE_NOPOPUP,	// char events, don't show on-screen keyboard
} textmode_t;

extern keydest_t	key_dest;
extern	char	*keybindings[MAX_KEYS];

#define		CMDLINES 64

extern	char	key_lines[CMDLINES][MAXCMDLINE];
extern	char	key_tabhint[MAXCMDLINE];
extern	int		edit_line;
extern	int		key_linepos;
extern	int		key_insert;
extern	double		key_blinktime;

extern	qboolean	chat_team;

void Key_Init (void);
void Key_ClearStates (void);
void Key_UpdateForDest (void);

void Key_BeginInputGrab (void);
void Key_EndInputGrab (void);
void Key_GetGrabbedInput (int *lastkey, int *lastchar);

void Key_Event (int key, qboolean down);
void Key_EventWithKeycode (int key, qboolean down, int keycode);
void Char_Event (int key);
textmode_t Key_TextEntry (void);

void Key_SetBinding (int keynum, const char *binding);
int Key_GetKeysForCommand (const char *command, int *keys, int maxkeys);
const char *Key_KeynumToString (int keynum);
void Key_WriteBindings (FILE *f);

void Key_EndChat (void);
const char *Key_GetChatBuffer (void);
int Key_GetChatMsgLen (void);

void History_Init (void);
void History_Shutdown (void);

#endif	/* _QUAKE_KEYS_H */

