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
#define	K_TAB			9
#define	K_ENTER			13
#define	K_ESCAPE		27
#define	K_SPACE			32

// normal keys should be passed as lowercased ascii

#define	K_BACKSPACE		127
#define	K_UPARROW		128
#define	K_DOWNARROW		129
#define	K_LEFTARROW		130
#define	K_RIGHTARROW	131

#define	K_ALT			132
#define	K_CTRL			133
#define	K_SHIFT			134
#define	K_F1			135
#define	K_F2			136
#define	K_F3			137
#define	K_F4			138
#define	K_F5			139
#define	K_F6			140
#define	K_F7			141
#define	K_F8			142
#define	K_F9			143
#define	K_F10			144
#define	K_F11			145
#define	K_F12			146
#define	K_INS			147
#define	K_DEL			148
#define	K_PGDN			149
#define	K_PGUP			150
#define	K_HOME			151
#define	K_END			152

#define	K_KP_NUMLOCK		153
#define	K_KP_SLASH		154
#define	K_KP_STAR		155
#define	K_KP_MINUS		156
#define	K_KP_HOME		157
#define	K_KP_UPARROW		158
#define	K_KP_PGUP		159
#define	K_KP_PLUS		160
#define	K_KP_LEFTARROW		161
#define	K_KP_5			162
#define	K_KP_RIGHTARROW		163
#define	K_KP_END		164
#define	K_KP_DOWNARROW		165
#define	K_KP_PGDN		166
#define	K_KP_ENTER		167
#define	K_KP_INS		168
#define	K_KP_DEL		169

#define	K_COMMAND		170

#define K_PAUSE			255

//
// mouse buttons generate virtual keys
//
#define	K_MOUSE1		200
#define	K_MOUSE2		201
#define	K_MOUSE3		202

//
// joystick buttons
//
#define	K_JOY1			203
#define	K_JOY2			204
#define	K_JOY3			205
#define	K_JOY4			206
// aux keys are for multi-buttoned joysticks to generate so they can use
// the normal binding process
// aux29-32: reserved for the HAT (POV) switch motion
#define	K_AUX1			207
#define	K_AUX2			208
#define	K_AUX3			209
#define	K_AUX4			210
#define	K_AUX5			211
#define	K_AUX6			212
#define	K_AUX7			213
#define	K_AUX8			214
#define	K_AUX9			215
#define	K_AUX10			216
#define	K_AUX11			217
#define	K_AUX12			218
#define	K_AUX13			219
#define	K_AUX14			220
#define	K_AUX15			221
#define	K_AUX16			222
#define	K_AUX17			223
#define	K_AUX18			224
#define	K_AUX19			225
#define	K_AUX20			226
#define	K_AUX21			227
#define	K_AUX22			228
#define	K_AUX23			229
#define	K_AUX24			230
#define	K_AUX25			231
#define	K_AUX26			232
#define	K_AUX27			233
#define	K_AUX28			234
#define	K_AUX29			235
#define	K_AUX30			236
#define	K_AUX31			237
#define	K_AUX32			238

// JACK: Intellimouse(c) Mouse Wheel Support

#define K_MWHEELUP		239
#define K_MWHEELDOWN		240

// thumb buttons
#define K_MOUSE4		241
#define K_MOUSE5		242

// SDL2 game controller keys
#define K_LTHUMB		243
#define K_RTHUMB		244
#define K_LSHOULDER		245
#define K_RSHOULDER		246
#define K_ABUTTON		247
#define K_BBUTTON		248
#define K_XBUTTON		249
#define K_YBUTTON		250
#define K_LTRIGGER		251
#define K_RTRIGGER		252

#define	MAX_KEYS		256

#define	MAXCMDLINE	256

typedef enum {key_game, key_console, key_message, key_menu} keydest_t;

extern keydest_t	key_dest;
extern	char	*keybindings[MAX_KEYS];

#define		CMDLINES 64

extern	char	key_lines[CMDLINES][MAXCMDLINE];
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
void Char_Event (int key);
qboolean Key_TextEntry (void);

void Key_SetBinding (int keynum, const char *binding);
const char *Key_KeynumToString (int keynum);
void Key_WriteBindings (FILE *f);

void Key_EndChat (void);
const char *Key_GetChatBuffer (void);
int Key_GetChatMsgLen (void);

void History_Init (void);
void History_Shutdown (void);

#endif	/* _QUAKE_KEYS_H */

