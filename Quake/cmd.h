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

#ifndef _QUAKE_CMD_H
#define _QUAKE_CMD_H

// cmd.h -- Command buffer and command execution

//===========================================================================

/*

Any number of commands can be added in a frame, from several different sources.
Most commands come from either keybindings or console line input, but remote
servers can also send across commands and entire text files can be execed.

The + command line options are also added to the command buffer.

The game starts with a Cbuf_AddText ("exec quake.rc\n"); Cbuf_Execute ();

*/

void Cbuf_Init (void);
// allocates an initial text buffer that will grow as needed

void Cbuf_AddTextLen (const char *text, int l);
void Cbuf_AddText (const char *text);
// as new commands are generated from the console or keybindings,
// the text is added to the end of the command buffer.

void Cbuf_InsertText (const char *text);
// when a command wants to issue other commands immediately, the text is
// inserted at the beginning of the buffer, before any remaining unexecuted
// commands.

void Cbuf_Execute (void);
// Pulls off \n terminated lines of text from the command buffer and sends
// them through Cmd_ExecuteString.  Stops when the buffer is empty.
// Normally called once per frame, but may be explicitly invoked.
// Do not call inside a command function!

void Cbuf_Waited (void);
//In vanilla, the 'wait' command is used by both input configs and servers.
//mods do hacky stuff like syncing waits to StartFrame calls.
//thankfully, c2s packets and server logic can both happen at the same intervals.
//so wait sets a flag to inhibit execution of more commands, and we only clear it once we've run a network frame.
//so this function lets the cbuf know when to clear the flag again (instead of part of cbuf_execute).

//===========================================================================

/*

Command execution takes a null terminated string, breaks it into tokens,
then searches for a command or variable that matches the first token.

Commands can come from three sources, but the handler functions may choose
to dissallow the action or forward it to a remote server if the source is
not apropriate.

*/

typedef enum
{
	src_client,		// came in over a net connection as a clc_stringcmd
					// host_client will be valid during this state.
	src_command,	// from the command buffer
	src_server		// from a svc_stufftext
} cmd_source_t;
extern	cmd_source_t	cmd_source;

typedef void (*xcommand_t) (void);
typedef struct cmd_function_s
{
	struct cmd_function_s	*next;
	const char		*name;
	xcommand_t		function;
	cmd_source_t	srctype;
	qboolean		dynamic;
} cmd_function_t;

void	Cmd_Init (void);

cmd_function_t *Cmd_AddCommand2 (const char *cmd_name, xcommand_t function, cmd_source_t srctype);
void Cmd_RemoveCommand (cmd_function_t *cmd);
// called by the init functions of other parts of the program to
// register commands and functions to call for them.
// The cmd_name is referenced later, so it should not be in temp memory
#define Cmd_AddCommand(cmdname,func) Cmd_AddCommand2(cmdname,func,src_command)				//regular console commands
#define Cmd_AddCommand_ClientCommand(cmdname,func) Cmd_AddCommand2(cmdname,func,src_client)	//command is meant to be safe for anyone to execute.
#define Cmd_AddCommand_ServerCommand(cmdname,func) Cmd_AddCommand2(cmdname,func,src_server)	//command came from a server
#define Cmd_AddCommand_Console Cmd_AddCommand	//to make the disabiguation more obvious

qboolean Cmd_AliasExists (const char *aliasname);
qboolean Cmd_Exists (const char *cmd_name);
// used by the cvar code to check for cvar / command name overlap

const char	*Cmd_CompleteCommand (const char *partial);
// attempts to match a partial command for automatic command line completion
// returns NULL if nothing fits

int		Cmd_Argc (void);
const char	*Cmd_Argv (int arg);
const char	*Cmd_Args (void);
// The functions that execute commands get their parameters with these
// functions. Cmd_Argv () will return an empty string, not a NULL
// if arg > argc, so string operations are allways safe.

int Cmd_CheckParm (const char *parm);
// Returns the position (1 to argc-1) in the command's argument list
// where the given parameter apears, or 0 if not present

void Cmd_TokenizeString (const char *text);
// Takes a null terminated string.  Does not need to be /n terminated.
// breaks the string up into arg tokens.

qboolean	Cmd_ExecuteString (const char *text, cmd_source_t src);
// Parses a single line of text into arguments and tries to execute it.
// The text can come from the command buffer, a remote client, or stdin.

void	Cmd_ForwardToServer (void);
// adds the current command line as a clc_stringcmd to the client message.
// things like godmode, noclip, etc, are commands directed to the server,
// so when they are typed in at the console, they will need to be forwarded.

void	Cmd_Print (const char *text);
// used by command functions to send output to either the graphics console or
// passed as a print message to the client

#endif	/* _QUAKE_CMD_H */

