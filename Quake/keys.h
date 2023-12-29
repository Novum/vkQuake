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
	K_UPARROW			= 128,
	K_DOWNARROW			= 129,
	K_LEFTARROW			= 130,
	K_RIGHTARROW		= 131,

	K_ALT				= 132,
	K_CTRL				= 133,
	K_SHIFT				= 134,
	K_F1				= 135,
	K_F2				= 136,
	K_F3				= 137,
	K_F4				= 138,
	K_F5				= 139,
	K_F6				= 140,
	K_F7				= 141,
	K_F8				= 142,
	K_F9				= 143,
	K_F10				= 144,
	K_F11				= 145,
	K_F12				= 146,
	K_INS				= 147,
	K_DEL				= 148,
	K_PGDN				= 149,
	K_PGUP				= 150,
	K_HOME				= 151,
	K_END				= 152,

	K_KP_NUMLOCK		= 153,
	K_KP_SLASH			= 154,
	K_KP_STAR			= 155,
	K_KP_MINUS			= 156,
	K_KP_HOME			= 157,
	K_KP_UPARROW		= 158,
	K_KP_PGUP			= 159,
	K_KP_PLUS			= 160,
	K_KP_LEFTARROW		= 161,
	K_KP_5				= 162,
	K_KP_RIGHTARROW		= 163,
	K_KP_END			= 164,
	K_KP_DOWNARROW		= 165,
	K_KP_PGDN			= 166,
	K_KP_ENTER			= 167,
	K_KP_INS			= 168,
	K_KP_DEL			= 169,

	K_COMMAND			= 170,

	K_CAPSLOCK			= 171,
	K_SCROLLLOCK		= 172,
	K_PRINTSCREEN		= 173,

	K_PAUSE				= 255,

//
// mouse buttons generate virtual keys
//
	K_MOUSE1			= 200,
	K_MOUSE2			= 201,
	K_MOUSE3			= 202,

//
// joystick buttons
//
	K_JOY1				= 203,
	K_JOY2				= 204,
	K_JOY3				= 205,
	K_JOY4				= 206,
// aux keys are for multi-buttoned joysticks to generate so they can use
// the normal binding process
// aux29-32: reserved for the HAT (POV) switch motion
	K_AUX1				= 207,
	K_AUX2				= 208,
	K_AUX3				= 209,
	K_AUX4				= 210,
	K_AUX5				= 211,
	K_AUX6				= 212,
	K_AUX7				= 213,
	K_AUX8				= 214,
	K_AUX9				= 215,
	K_AUX10				= 216,
	K_AUX11				= 217,
	K_AUX12				= 218,
	K_AUX13				= 219,
	K_AUX14				= 220,
	K_AUX15				= 221,
	K_AUX16				= 222,
	K_AUX17				= 223,
	K_AUX18				= 224,
	K_AUX19				= 225,
	K_AUX20				= 226,
	K_AUX21				= 227,
	K_AUX22				= 228,
	K_AUX23				= 229,
	K_AUX24				= 230,
	K_AUX25				= 231,
	K_AUX26				= 232,
	K_AUX27				= 233,
	K_AUX28				= 234,
	K_AUX29				= 235,
	K_AUX30				= 236,
	K_AUX31				= 237,
	K_AUX32				= 238,

// JACK: Intellimouse(c) Mouse Wheel Support

	K_MWHEELUP			= 239,
	K_MWHEELDOWN		= 240,

// thumb buttons
	K_MOUSE4			= 241,
	K_MOUSE5			= 242,

// SDL2 game controller keys
	K_LTHUMB			= 243,
	K_RTHUMB			= 244,
	K_LSHOULDER			= 245,
	K_RSHOULDER			= 246,
	K_ABUTTON			= 247,
	K_BBUTTON			= 248,
	K_XBUTTON			= 249,
	K_YBUTTON			= 250,
	K_LTRIGGER			= 251,
	K_RTRIGGER			= 252,
} keycode_t;

// clang-format on

#define MAX_KEYS 256

#define MAXCMDLINE 256

typedef enum
{
	key_game,
	key_console,
	key_message,
	key_menu
} keydest_t;

extern keydest_t key_dest;
extern char		*keybindings[MAX_KEYS];

#define CMDLINES 64

extern char	  key_lines[CMDLINES][MAXCMDLINE];
extern int	  edit_line;
extern int	  key_linepos;
extern int	  key_insert;
extern double key_blinktime;

extern qboolean chat_team;

void Key_Init (void);
void Key_ClearStates (void);
void Key_UpdateForDest (void);

void Key_BeginInputGrab (void);
void Key_EndInputGrab (void);
void Key_GetGrabbedInput (int *lastkey, int *lastchar);

void	 Key_Event (int key, qboolean down);
void	 Key_EventWithKeycode (int key, qboolean down, int keycode);
void	 Char_Event (int key);
qboolean Key_TextEntry (void);

void		Key_SetBinding (int keynum, const char *binding);
const char *Key_KeynumToString (int keynum);
void		Key_WriteBindings (FILE *f);

void		Key_EndChat (void);
const char *Key_GetChatBuffer (void);
int			Key_GetChatMsgLen (void);

void History_Init (void);
void History_Shutdown (void);

#endif /* _QUAKE_KEYS_H */
