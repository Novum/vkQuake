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

#ifndef __CVAR_H__
#define __CVAR_H__

/*
cvar_t variables are used to hold scalar or string variables that can
be changed or displayed at the console or prog code as well as accessed
directly in C code.

it is sufficient to initialize a cvar_t with just the first two fields,
or you can add a ,true flag for variables that you want saved to the
configuration file when the game is quit:

cvar_t	r_draworder = {"r_draworder","1"};
cvar_t	scr_screensize = {"screensize","1",true};

Cvars must be registered before use, or they will have a 0 value instead
of the float interpretation of the string.
Generally, all cvar_t declarations should be registered in the apropriate
init function before any console commands are executed:

Cvar_RegisterVariable (&host_framerate);


C code usually just references a cvar in place:
if ( r_draworder.value )

It could optionally ask for the value to be looked up for a string name:
if (Cvar_VariableValue ("r_draworder"))

Interpreted prog code can access cvars with the cvar(name) or
cvar_set (name, value) internal functions:
teamplay = cvar("teamplay");
cvar_set ("registered", "1");

The user can access cvars from the console in two ways:
r_draworder		prints the current value
r_draworder 0		sets the current value to 0

Cvars are restricted from having the same names as commands to keep this
interface from being ambiguous.

*/

#define	CVAR_NONE		0
#define	CVAR_ARCHIVE		(1U << 0)	// if set, causes it to be saved to config
#define	CVAR_NOTIFY		(1U << 1)	// changes will be broadcasted to all players (q1)
#define	CVAR_SERVERINFO		(1U << 2)	// added to serverinfo will be sent to clients (q1/net_dgrm.c and qwsv)
#define	CVAR_USERINFO		(1U << 3)	// added to userinfo, will be sent to server (qwcl)
#define	CVAR_CHANGED		(1U << 4)
#define	CVAR_ROM		(1U << 6)
#define	CVAR_LOCKED		(1U << 8)	// locked temporarily
#define	CVAR_REGISTERED		(1U << 10)	// the var is added to the list of variables
#define	CVAR_CALLBACK		(1U << 16)	// var has a callback


typedef void (*cvarcallback_t) (struct cvar_s *);

typedef struct cvar_s
{
	const char	*name;
	const char	*string;
	unsigned int	flags;
	float		value;
	const char	*default_string; //johnfitz -- remember defaults for reset function
	cvarcallback_t	callback;
	struct cvar_s	*next;
} cvar_t;

void	Cvar_RegisterVariable (cvar_t *variable);
// registers a cvar that already has the name, string, and optionally
// the archive elements set.

void Cvar_SetCallback (cvar_t *var, cvarcallback_t func);
// set a callback function to the var

void	Cvar_Set (const char *var_name, const char *value);
// equivelant to "<name> <variable>" typed at the console

void	Cvar_SetValue (const char *var_name, const float value);
// expands value to a string and calls Cvar_Set

void	Cvar_SetROM (const char *var_name, const char *value);
void	Cvar_SetValueROM (const char *var_name, const float value);
// sets a CVAR_ROM variable from within the engine

void Cvar_SetQuick (cvar_t *var, const char *value);
void Cvar_SetValueQuick (cvar_t *var, const float value);
// these two accept a cvar pointer instead of a var name,
// but are otherwise identical to the "non-Quick" versions.
// the cvar MUST be registered.

float	Cvar_VariableValue (const char *var_name);
// returns 0 if not defined or non numeric

const char *Cvar_VariableString (const char *var_name);
// returns an empty string if not defined

qboolean Cvar_Command (void);
// called by Cmd_ExecuteString when Cmd_Argv(0) doesn't match a known
// command.  Returns true if the command was a variable reference that
// was handled. (print or change)

void	Cvar_WriteVariables (FILE *f);
// Writes lines containing "set variable value" for all variables
// with the CVAR_ARCHIVE flag set

cvar_t	*Cvar_FindVar (const char *var_name);
cvar_t	*Cvar_FindVarAfter (const char *prev_name, unsigned int with_flags);

void	Cvar_LockVar (const char *var_name);
void	Cvar_UnlockVar (const char *var_name);
void	Cvar_UnlockAll (void);

void	Cvar_Init (void);

const char	*Cvar_CompleteVariable (const char *partial);
// attempts to match a partial variable name for command line completion
// returns NULL if nothing fits

#endif	/* __CVAR_H__ */

