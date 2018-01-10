/*
Copyright (C) 1996-2001 Id Software, Inc.
Copyright (C) 2002-2009 John Fitzgibbons and others
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
#include "arch_def.h"

/* key up events are sent even if in console mode */

#define		HISTORY_FILE_NAME "history.txt"

char		key_lines[CMDLINES][MAXCMDLINE];

int		key_linepos;
int		key_insert;	//johnfitz -- insert key toggle (for editing)
double		key_blinktime; //johnfitz -- fudge cursor blinking to make it easier to spot in certain cases

int		edit_line = 0;
int		history_line = 0;

keydest_t	key_dest;

char		*keybindings[MAX_KEYS];
qboolean	consolekeys[MAX_KEYS];	// if true, can't be rebound while in console
qboolean	menubound[MAX_KEYS];	// if true, can't be rebound while in menu
qboolean	keydown[MAX_KEYS];

typedef struct
{
	const char	*name;
	int		keynum;
} keyname_t;

keyname_t keynames[] =
{
	{"TAB", K_TAB},
	{"ENTER", K_ENTER},
	{"ESCAPE", K_ESCAPE},
	{"SPACE", K_SPACE},
	{"BACKSPACE", K_BACKSPACE},
	{"UPARROW", K_UPARROW},
	{"DOWNARROW", K_DOWNARROW},
	{"LEFTARROW", K_LEFTARROW},
	{"RIGHTARROW", K_RIGHTARROW},

	{"ALT", K_ALT},
	{"CTRL", K_CTRL},
	{"SHIFT", K_SHIFT},

//	{"KP_NUMLOCK", K_KP_NUMLOCK},
	{"KP_SLASH", K_KP_SLASH},
	{"KP_STAR", K_KP_STAR},
	{"KP_MINUS", K_KP_MINUS},
	{"KP_HOME", K_KP_HOME},
	{"KP_UPARROW", K_KP_UPARROW},
	{"KP_PGUP", K_KP_PGUP},
	{"KP_PLUS", K_KP_PLUS},
	{"KP_LEFTARROW", K_KP_LEFTARROW},
	{"KP_5", K_KP_5},
	{"KP_RIGHTARROW", K_KP_RIGHTARROW},
	{"KP_END", K_KP_END},
	{"KP_DOWNARROW", K_KP_DOWNARROW},
	{"KP_PGDN", K_KP_PGDN},
	{"KP_ENTER", K_KP_ENTER},
	{"KP_INS", K_KP_INS},
	{"KP_DEL", K_KP_DEL},

	{"F1", K_F1},
	{"F2", K_F2},
	{"F3", K_F3},
	{"F4", K_F4},
	{"F5", K_F5},
	{"F6", K_F6},
	{"F7", K_F7},
	{"F8", K_F8},
	{"F9", K_F9},
	{"F10", K_F10},
	{"F11", K_F11},
	{"F12", K_F12},

	{"INS", K_INS},
	{"DEL", K_DEL},
	{"PGDN", K_PGDN},
	{"PGUP", K_PGUP},
	{"HOME", K_HOME},
	{"END", K_END},

	{"COMMAND", K_COMMAND},

	{"MOUSE1", K_MOUSE1},
	{"MOUSE2", K_MOUSE2},
	{"MOUSE3", K_MOUSE3},
	{"MOUSE4", K_MOUSE4},
	{"MOUSE5", K_MOUSE5},

	{"JOY1", K_JOY1},
	{"JOY2", K_JOY2},
	{"JOY3", K_JOY3},
	{"JOY4", K_JOY4},

	{"AUX1", K_AUX1},
	{"AUX2", K_AUX2},
	{"AUX3", K_AUX3},
	{"AUX4", K_AUX4},
	{"AUX5", K_AUX5},
	{"AUX6", K_AUX6},
	{"AUX7", K_AUX7},
	{"AUX8", K_AUX8},
	{"AUX9", K_AUX9},
	{"AUX10", K_AUX10},
	{"AUX11", K_AUX11},
	{"AUX12", K_AUX12},
	{"AUX13", K_AUX13},
	{"AUX14", K_AUX14},
	{"AUX15", K_AUX15},
	{"AUX16", K_AUX16},
	{"AUX17", K_AUX17},
	{"AUX18", K_AUX18},
	{"AUX19", K_AUX19},
	{"AUX20", K_AUX20},
	{"AUX21", K_AUX21},
	{"AUX22", K_AUX22},
	{"AUX23", K_AUX23},
	{"AUX24", K_AUX24},
	{"AUX25", K_AUX25},
	{"AUX26", K_AUX26},
	{"AUX27", K_AUX27},
	{"AUX28", K_AUX28},
	{"AUX29", K_AUX29},
	{"AUX30", K_AUX30},
	{"AUX31", K_AUX31},
	{"AUX32", K_AUX32},

	{"PAUSE", K_PAUSE},

	{"MWHEELUP", K_MWHEELUP},
	{"MWHEELDOWN", K_MWHEELDOWN},

	{"SEMICOLON", ';'},	// because a raw semicolon seperates commands

	{"BACKQUOTE", '`'},	// because a raw backquote may toggle the console
	{"TILDE", '~'},		// because a raw tilde may toggle the console

	{"LTHUMB", K_LTHUMB},
	{"RTHUMB", K_RTHUMB},
	{"LSHOULDER", K_LSHOULDER},
	{"RSHOULDER", K_RSHOULDER},
	{"ABUTTON", K_ABUTTON},
	{"BBUTTON", K_BBUTTON},
	{"XBUTTON", K_XBUTTON},
	{"YBUTTON", K_YBUTTON},
	{"LTRIGGER", K_LTRIGGER},
	{"RTRIGGER", K_RTRIGGER},

	{NULL,		0}
};

/*
==============================================================================

			LINE TYPING INTO THE CONSOLE

==============================================================================
*/

static void PasteToConsole (void)
{
	char *cbd, *p, *workline;
	int mvlen, inslen;

	if (key_linepos == MAXCMDLINE - 1)
		return;

	if ((cbd = PL_GetClipboardData()) == NULL)
		return;

	p = cbd;
	while (*p)
	{
		if (*p == '\n' || *p == '\r' || *p == '\b')
		{
			*p = 0;
			break;
		}
		p++;
	}

	inslen = (int) (p - cbd);
	if (inslen + key_linepos > MAXCMDLINE - 1)
		inslen = MAXCMDLINE - 1 - key_linepos;
	if (inslen <= 0) goto done;

	workline = key_lines[edit_line];
	workline += key_linepos;
	mvlen = (int) strlen(workline);
	if (mvlen + inslen + key_linepos > MAXCMDLINE - 1)
	{
		mvlen = MAXCMDLINE - 1 - key_linepos - inslen;
		if (mvlen < 0) mvlen = 0;
	}

	// insert the string
	if (mvlen != 0)
		memmove (workline + inslen, workline, mvlen);
	memcpy (workline, cbd, inslen);
	key_linepos += inslen;
	workline[mvlen + inslen] = '\0';
  done:
	Z_Free(cbd);
}

/*
====================
Key_Console -- johnfitz -- heavy revision

Interactive line editing and console scrollback
====================
*/
extern	char *con_text, key_tabpartial[MAXCMDLINE];
extern	int con_current, con_linewidth, con_vislines;

void Key_Console (int key)
{
	static	char current[MAXCMDLINE] = "";
	int	history_line_last;
	size_t		len;
	char *workline = key_lines[edit_line];

	switch (key)
	{
	case K_ENTER:
	case K_KP_ENTER:
		key_tabpartial[0] = 0;
		Cbuf_AddText (workline + 1);	// skip the prompt
		Cbuf_AddText ("\n");
		Con_Printf ("%s\n", workline);

		// If the last two lines are identical, skip storing this line in history 
		// by not incrementing edit_line
		if (strcmp(workline, key_lines[(edit_line-1)&31]))
			edit_line = (edit_line + 1) & 31;

		history_line = edit_line;
		key_lines[edit_line][0] = ']';
		key_lines[edit_line][1] = 0; //johnfitz -- otherwise old history items show up in the new edit line
		key_linepos = 1;
		if (cls.state == ca_disconnected)
			SCR_UpdateScreen (); // force an update, because the command may take some time
		return;

	case K_TAB:
		Con_TabComplete ();
		return;

	case K_BACKSPACE:
		key_tabpartial[0] = 0;
		if (key_linepos > 1)
		{
			workline += key_linepos - 1;
			if (workline[1])
			{
				len = strlen(workline);
				memmove (workline, workline + 1, len);
			}
			else	*workline = 0;
			key_linepos--;
		}
		return;

	case K_DEL:
		key_tabpartial[0] = 0;
		workline += key_linepos;
		if (*workline)
		{
			if (workline[1])
			{
				len = strlen(workline);
				memmove (workline, workline + 1, len);
			}
			else	*workline = 0;
		}
		return;

	case K_HOME:
		if (keydown[K_CTRL])
		{
			//skip initial empty lines
			int i, x;
			char *line;

			for (i = con_current - con_totallines + 1; i <= con_current; i++)
			{
				line = con_text + (i % con_totallines) * con_linewidth;
				for (x = 0; x < con_linewidth; x++)
				{
					if (line[x] != ' ')
						break;
				}
				if (x != con_linewidth)
					break;
			}
			con_backscroll = CLAMP(0, con_current-i%con_totallines-2, con_totallines-(glheight>>3)-1);
		}
		else	key_linepos = 1;
		return;

	case K_END:
		if (keydown[K_CTRL])
			con_backscroll = 0;
		else	key_linepos = strlen(workline);
		return;

	case K_PGUP:
	case K_MWHEELUP:
		con_backscroll += keydown[K_CTRL] ? ((con_vislines>>3) - 4) : 2;
		if (con_backscroll > con_totallines - (vid.height>>3) - 1)
			con_backscroll = con_totallines - (vid.height>>3) - 1;
		return;

	case K_PGDN:
	case K_MWHEELDOWN:
		con_backscroll -= keydown[K_CTRL] ? ((con_vislines>>3) - 4) : 2;
		if (con_backscroll < 0)
			con_backscroll = 0;
		return;

	case K_LEFTARROW:
		if (key_linepos > 1)
		{
			key_linepos--;
			key_blinktime = realtime;
		}
		return;

	case K_RIGHTARROW:
		len = strlen(workline);
		if ((int)len == key_linepos)
		{
			len = strlen(key_lines[(edit_line + 31) & 31]);
			if ((int)len <= key_linepos)
				return; // no character to get
			workline += key_linepos;
			*workline = key_lines[(edit_line + 31) & 31][key_linepos];
			workline[1] = 0;
			key_linepos++;
		}
		else
		{
			key_linepos++;
			key_blinktime = realtime;
		}
		return;

	case K_UPARROW:
		if (history_line == edit_line)
			Q_strcpy(current, workline);

		history_line_last = history_line;
		do
		{
			history_line = (history_line - 1) & 31;
		} while (history_line != edit_line && !key_lines[history_line][1]);

		if (history_line == edit_line)
		{
			history_line = history_line_last;
			return;
		}

		key_tabpartial[0] = 0;
		Q_strcpy(workline, key_lines[history_line]);
		key_linepos = Q_strlen(workline);
		return;

	case K_DOWNARROW:
		if (history_line == edit_line)
			return;

		key_tabpartial[0] = 0;

		do
		{
			history_line = (history_line + 1) & 31;
		} while (history_line != edit_line && !key_lines[history_line][1]);

		if (history_line == edit_line)
			Q_strcpy(workline, current);
		else	Q_strcpy(workline, key_lines[history_line]);
		key_linepos = Q_strlen(workline);
		return;

	case K_INS:
		if (keydown[K_SHIFT])		/* Shift-Ins paste */
			PasteToConsole();
		else	key_insert ^= 1;
		return;

	case 'v':
	case 'V':
#if defined(PLATFORM_OSX) || defined(PLATFORM_MAC)
		if (keydown[K_COMMAND]) {	/* Cmd+v paste (Mac-only) */
			PasteToConsole();
			return;
		}
#endif
		if (keydown[K_CTRL]) {		/* Ctrl+v paste */
			PasteToConsole();
			return;
		}
		break;

	case 'c':
	case 'C':
		if (keydown[K_CTRL]) {		/* Ctrl+C: abort the line -- S.A */
			Con_Printf ("%s\n", workline);
			workline[0] = ']';
			workline[1] = 0;
			key_linepos = 1;
			history_line= edit_line;
			return;
		}
		break;
	}
}

void Char_Console (int key)
{
	size_t		len;
	char *workline = key_lines[edit_line];

	if (key_linepos < MAXCMDLINE-1)
	{
		qboolean endpos = !workline[key_linepos];

		key_tabpartial[0] = 0; //johnfitz
		// if inserting, move the text to the right
		if (key_insert && !endpos)
		{
			workline[MAXCMDLINE - 2] = 0;
			workline += key_linepos;
			len = strlen(workline) + 1;
			memmove (workline + 1, workline, len);
			*workline = key;
		}
		else
		{
			workline += key_linepos;
			*workline = key;
			// null terminate if at the end
			if (endpos)
				workline[1] = 0;
		}
		key_linepos++;
	}
}

//============================================================================

qboolean	chat_team = false;
static char	chat_buffer[MAXCMDLINE];
static int	chat_bufferlen = 0;

const char *Key_GetChatBuffer (void)
{
	return chat_buffer;
}

int Key_GetChatMsgLen (void)
{
	return chat_bufferlen;
}

void Key_EndChat (void)
{
	key_dest = key_game;
	chat_bufferlen = 0;
	chat_buffer[0] = 0;
}

void Key_Message (int key)
{
	switch (key)
	{
	case K_ENTER:
	case K_KP_ENTER:
		if (chat_team)
			Cbuf_AddText ("say_team \"");
		else
			Cbuf_AddText ("say \"");
		Cbuf_AddText(chat_buffer);
		Cbuf_AddText("\"\n");

		Key_EndChat ();
		return;

	case K_ESCAPE:
		Key_EndChat ();
		return;

	case K_BACKSPACE:
		if (chat_bufferlen)
			chat_buffer[--chat_bufferlen] = 0;
		return;
	}
}

void Char_Message (int key)
{
	if (chat_bufferlen == sizeof(chat_buffer) - 1)
		return; // all full

	chat_buffer[chat_bufferlen++] = key;
	chat_buffer[chat_bufferlen] = 0;
}

//============================================================================


/*
===================
Key_StringToKeynum

Returns a key number to be used to index keybindings[] by looking at
the given string.  Single ascii characters return themselves, while
the K_* names are matched up.
===================
*/
int Key_StringToKeynum (const char *str)
{
	keyname_t	*kn;

	if (!str || !str[0])
		return -1;
	if (!str[1])
		return str[0];

	for (kn=keynames ; kn->name ; kn++)
	{
		if (!q_strcasecmp(str,kn->name))
			return kn->keynum;
	}
	return -1;
}

/*
===================
Key_KeynumToString

Returns a string (either a single ascii char, or a K_* name) for the
given keynum.
FIXME: handle quote special (general escape sequence?)
===================
*/
const char *Key_KeynumToString (int keynum)
{
	static	char	tinystr[2];
	keyname_t	*kn;

	if (keynum == -1)
		return "<KEY NOT FOUND>";
	if (keynum > 32 && keynum < 127)
	{	// printable ascii
		tinystr[0] = keynum;
		tinystr[1] = 0;
		return tinystr;
	}

	for (kn = keynames; kn->name; kn++)
	{
		if (keynum == kn->keynum)
			return kn->name;
	}

	return "<UNKNOWN KEYNUM>";
}


/*
===================
Key_SetBinding
===================
*/
void Key_SetBinding (int keynum, const char *binding)
{
	if (keynum == -1)
		return;

// free old bindings
	if (keybindings[keynum])
	{
		Z_Free (keybindings[keynum]);
		keybindings[keynum] = NULL;
	}

// allocate memory for new binding
	if (binding)
		keybindings[keynum] = Z_Strdup(binding);
}

/*
===================
Key_Unbind_f
===================
*/
void Key_Unbind_f (void)
{
	int	b;

	if (Cmd_Argc() != 2)
	{
		Con_Printf ("unbind <key> : remove commands from a key\n");
		return;
	}

	b = Key_StringToKeynum (Cmd_Argv(1));
	if (b == -1)
	{
		Con_Printf ("\"%s\" isn't a valid key\n", Cmd_Argv(1));
		return;
	}

	Key_SetBinding (b, NULL);
}

void Key_Unbindall_f (void)
{
	int	i;

	for (i = 0; i < MAX_KEYS; i++)
	{
		if (keybindings[i])
			Key_SetBinding (i, NULL);
	}
}

/*
============
Key_Bindlist_f -- johnfitz
============
*/
void Key_Bindlist_f (void)
{
	int	i, count;

	count = 0;
	for (i = 0; i < MAX_KEYS; i++)
	{
		if (keybindings[i] && *keybindings[i])
		{
			Con_SafePrintf ("   %s \"%s\"\n", Key_KeynumToString(i), keybindings[i]);
			count++;
		}
	}
	Con_SafePrintf ("%i bindings\n", count);
}

/*
===================
Key_Bind_f
===================
*/
void Key_Bind_f (void)
{
	int	i, c, b;
	char	cmd[1024];

	c = Cmd_Argc();

	if (c != 2 && c != 3)
	{
		Con_Printf ("bind <key> [command] : attach a command to a key\n");
		return;
	}
	b = Key_StringToKeynum (Cmd_Argv(1));
	if (b == -1)
	{
		Con_Printf ("\"%s\" isn't a valid key\n", Cmd_Argv(1));
		return;
	}

	if (c == 2)
	{
		if (keybindings[b])
			Con_Printf ("\"%s\" = \"%s\"\n", Cmd_Argv(1), keybindings[b] );
		else
			Con_Printf ("\"%s\" is not bound\n", Cmd_Argv(1) );
		return;
	}

// copy the rest of the command line
	cmd[0] = 0;
	for (i = 2; i < c; i++)
	{
		q_strlcat (cmd, Cmd_Argv(i), sizeof(cmd));
		if (i != (c-1))
			q_strlcat (cmd, " ", sizeof(cmd));
	}

	Key_SetBinding (b, cmd);
}

/*
============
Key_WriteBindings

Writes lines containing "bind key value"
============
*/
void Key_WriteBindings (FILE *f)
{
	int	i;

	// unbindall before loading stored bindings:
	if (cfg_unbindall.value)
		fprintf (f, "unbindall\n");
	for (i = 0; i < MAX_KEYS; i++)
	{
		if (keybindings[i] && *keybindings[i])
			fprintf (f, "bind \"%s\" \"%s\"\n", Key_KeynumToString(i), keybindings[i]);
	}
}


void History_Init (void)
{
	int i, c;
	FILE *hf;

	for (i = 0; i < CMDLINES; i++)
	{
		key_lines[i][0] = ']';
		key_lines[i][1] = 0;
	}
	key_linepos = 1;

	hf = fopen(va("%s/%s", host_parms->userdir, HISTORY_FILE_NAME), "rt");
	if (hf != NULL)
	{
		do
		{
			i = 1;
			do
			{
				c = fgetc(hf);
				key_lines[edit_line][i++] = c;
			} while (c != '\r' && c != '\n' && c != EOF && i < MAXCMDLINE);
			key_lines[edit_line][i - 1] = 0;
			edit_line = (edit_line + 1) & (CMDLINES - 1);
			/* for people using a windows-generated history file on unix: */
			if (c == '\r' || c == '\n')
			{
				do
					c = fgetc(hf);
				while (c == '\r' || c == '\n');
				if (c != EOF)
					ungetc(c, hf);
				else	c = 0; /* loop once more, otherwise last line is lost */
			}
		} while (c != EOF && edit_line < CMDLINES);
		fclose(hf);

		history_line = edit_line = (edit_line - 1) & (CMDLINES - 1);
		key_lines[edit_line][0] = ']';
		key_lines[edit_line][1] = 0;
	}
}

void History_Shutdown (void)
{
	int i;
	FILE *hf;

	hf = fopen(va("%s/%s", host_parms->userdir, HISTORY_FILE_NAME), "wt");
	if (hf != NULL)
	{
		i = edit_line;
		do
		{
			i = (i + 1) & (CMDLINES - 1);
		} while (i != edit_line && !key_lines[i][1]);

		while (i != edit_line && key_lines[i][1])
		{
			fprintf(hf, "%s\n", key_lines[i] + 1);
			i = (i + 1) & (CMDLINES - 1);
		}
		fclose(hf);
	}
}

/*
===================
Key_Init
===================
*/
void Key_Init (void)
{
	int	i;

	History_Init ();

	key_blinktime = realtime; //johnfitz

//
// initialize consolekeys[]
//
	for (i = 32; i < 127; i++) // ascii characters
		consolekeys[i] = true;
	consolekeys['`'] = false;
	consolekeys['~'] = false;
	consolekeys[K_TAB] = true;
	consolekeys[K_ENTER] = true;
	consolekeys[K_ESCAPE] = true;
	consolekeys[K_BACKSPACE] = true;
	consolekeys[K_UPARROW] = true;
	consolekeys[K_DOWNARROW] = true;
	consolekeys[K_LEFTARROW] = true;
	consolekeys[K_RIGHTARROW] = true;
	consolekeys[K_CTRL] = true;
	consolekeys[K_SHIFT] = true;
	consolekeys[K_INS] = true;
	consolekeys[K_DEL] = true;
	consolekeys[K_PGDN] = true;
	consolekeys[K_PGUP] = true;
	consolekeys[K_HOME] = true;
	consolekeys[K_END] = true;
	consolekeys[K_KP_NUMLOCK] = true;
	consolekeys[K_KP_SLASH] = true;
	consolekeys[K_KP_STAR] = true;
	consolekeys[K_KP_MINUS] = true;
	consolekeys[K_KP_HOME] = true;
	consolekeys[K_KP_UPARROW] = true;
	consolekeys[K_KP_PGUP] = true;
	consolekeys[K_KP_PLUS] = true;
	consolekeys[K_KP_LEFTARROW] = true;
	consolekeys[K_KP_5] = true;
	consolekeys[K_KP_RIGHTARROW] = true;
	consolekeys[K_KP_END] = true;
	consolekeys[K_KP_DOWNARROW] = true;
	consolekeys[K_KP_PGDN] = true;
	consolekeys[K_KP_ENTER] = true;
	consolekeys[K_KP_INS] = true;
	consolekeys[K_KP_DEL] = true;
#if defined(PLATFORM_OSX) || defined(PLATFORM_MAC)
	consolekeys[K_COMMAND] = true;
#endif
	consolekeys[K_MWHEELUP] = true;
	consolekeys[K_MWHEELDOWN] = true;

//
// initialize menubound[]
//
	menubound[K_ESCAPE] = true;
	for (i = 0; i < 12; i++)
		menubound[K_F1+i] = true;

//
// register our functions
//
	Cmd_AddCommand ("bindlist",Key_Bindlist_f); //johnfitz
	Cmd_AddCommand ("bind",Key_Bind_f);
	Cmd_AddCommand ("unbind",Key_Unbind_f);
	Cmd_AddCommand ("unbindall",Key_Unbindall_f);
}

static struct {
	qboolean active;
	int lastkey;
	int lastchar;
} key_inputgrab = { false, -1, -1 };

/*
===================
Key_BeginInputGrab
===================
*/
void Key_BeginInputGrab (void)
{
	Key_ClearStates ();

	key_inputgrab.active = true;
	key_inputgrab.lastkey = -1;
	key_inputgrab.lastchar = -1;

	IN_UpdateInputMode ();
}

/*
===================
Key_EndInputGrab
===================
*/
void Key_EndInputGrab (void)
{
	Key_ClearStates ();

	key_inputgrab.active = false;

	IN_UpdateInputMode ();
}

/*
===================
Key_GetGrabbedInput
===================
*/
void Key_GetGrabbedInput (int *lastkey, int *lastchar)
{
	if (lastkey)
		*lastkey = key_inputgrab.lastkey;
	if (lastchar)
		*lastchar = key_inputgrab.lastchar;
}

/*
===================
Key_Event

Called by the system between frames for both key up and key down events
Should NOT be called during an interrupt!
===================
*/
void Key_Event (int key, qboolean down)
{
	char	*kb;
	char	cmd[1024];

	if (key < 0 || key >= MAX_KEYS)
		return;

// handle fullscreen toggle
	if (down && (key == K_ENTER || key == K_KP_ENTER) && keydown[K_ALT])
	{
		VID_Toggle();
		return;
	}

// handle autorepeats and stray key up events
	if (down)
	{
		if (keydown[key])
		{
			if (key_dest == key_game && !con_forcedup)
				return; // ignore autorepeats in game mode
		}
		else if (key >= 200 && !keybindings[key])
			Con_Printf ("%s is unbound, hit F4 to set.\n", Key_KeynumToString(key));
	}
	else if (!keydown[key])
		return; // ignore stray key up events

	keydown[key] = down;

	if (key_inputgrab.active)
	{
		if (down)
			key_inputgrab.lastkey = key;
		return;
	}

// handle escape specialy, so the user can never unbind it
	if (key == K_ESCAPE)
	{
		if (!down)
			return;

		if (keydown[K_SHIFT])
		{
			Con_ToggleConsole_f();
			return;
		}

		switch (key_dest)
		{
		case key_message:
			Key_Message (key);
			break;
		case key_menu:
			M_Keydown (key);
			break;
		case key_game:
		case key_console:
			M_ToggleMenu_f ();
			break;
		default:
			Sys_Error ("Bad key_dest");
		}

		return;
	}

// key up events only generate commands if the game key binding is
// a button command (leading + sign).  These will occur even in console mode,
// to keep the character from continuing an action started before a console
// switch.  Button commands include the kenum as a parameter, so multiple
// downs can be matched with ups
	if (!down)
	{
		kb = keybindings[key];
		if (kb && kb[0] == '+')
		{
			sprintf (cmd, "-%s %i\n", kb+1, key);
			Cbuf_AddText (cmd);
		}
		return;
	}

// during demo playback, most keys bring up the main menu
	if (cls.demoplayback && down && consolekeys[key] && key_dest == key_game && key != K_TAB)
	{
		M_ToggleMenu_f ();
		return;
	}

// if not a consolekey, send to the interpreter no matter what mode is
	if ((key_dest == key_menu && menubound[key]) ||
	    (key_dest == key_console && !consolekeys[key]) ||
	    (key_dest == key_game && (!con_forcedup || !consolekeys[key])))
	{
		kb = keybindings[key];
		if (kb)
		{
			if (kb[0] == '+')
			{	// button commands add keynum as a parm
				sprintf (cmd, "%s %i\n", kb, key);
				Cbuf_AddText (cmd);
			}
			else
			{
				Cbuf_AddText (kb);
				Cbuf_AddText ("\n");
			}
		}
		return;
	}

	if (!down)
		return;		// other systems only care about key down events

	switch (key_dest)
	{
	case key_message:
		Key_Message (key);
		break;
	case key_menu:
		M_Keydown (key);
		break;

	case key_game:
	case key_console:
		Key_Console (key);
		break;
	default:
		Sys_Error ("Bad key_dest");
	}
}

/*
===================
Char_Event

Called by the backend when the user has input a character.
===================
*/
void Char_Event (int key)
{
	if (key < 32 || key > 126)
		return;

#if defined(PLATFORM_OSX) || defined(PLATFORM_MAC)
	if (keydown[K_COMMAND])
		return;
#endif
	if (keydown[K_CTRL])
		return;

	if (key_inputgrab.active)
	{
		key_inputgrab.lastchar = key;
		return;
	}

	switch (key_dest)
	{
	case key_message:
		Char_Message (key);
		break;
	case key_menu:
		M_Charinput (key);
		break;
	case key_game:
		if (!con_forcedup)
			break;
		/* fallthrough */
	case key_console:
		Char_Console (key);
		break;
	default:
		break;
	}
}

/*
===================
Key_TextEntry
===================
*/
qboolean Key_TextEntry (void)
{
	if (key_inputgrab.active)
		return true;

	switch (key_dest)
	{
	case key_message:
		return true;
	case key_menu:
		return M_TextEntry();
	case key_game:
		if (!con_forcedup)
			return false;
		/* fallthrough */
	case key_console:
		return true;
	default:
		return false;
	}
}

/*
===================
Key_ClearStates
===================
*/
void Key_ClearStates (void)
{
	int	i;

	for (i = 0; i < MAX_KEYS; i++)
	{
		if (keydown[i])
			Key_Event (i, false);
	}
}

/*
===================
Key_UpdateForDest
===================
*/
void Key_UpdateForDest (void)
{
	static qboolean forced = false;

	if (cls.state == ca_dedicated)
		return;

	switch (key_dest)
	{
	case key_console:
		if (forced && cls.state == ca_connected)
		{
			forced = false;
			IN_Activate();
			key_dest = key_game;
		}
		break;
	case key_game:
		if (cls.state != ca_connected)
		{
			forced = true;
			IN_Deactivate(modestate == MS_WINDOWED);
			key_dest = key_console;
			break;
		}
	/* fallthrough */
	default:
		forced = false;
		break;
	}
}

