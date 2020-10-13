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
// cvar.c -- dynamic variable tracking

#include "quakedef.h"

static cvar_t	*cvar_vars;
static char	cvar_null_string[] = "";

//==============================================================================
//
//  USER COMMANDS
//
//==============================================================================

void Cvar_Reset (const char *name); //johnfitz

/*
============
Cvar_List_f -- johnfitz
============
*/
void Cvar_List_f (void)
{
	cvar_t	*cvar;
	const char 	*partial;
	int		len, count;

	if (Cmd_Argc() > 1)
	{
		partial = Cmd_Argv (1);
		len = Q_strlen(partial);
	}
	else
	{
		partial = NULL;
		len = 0;
	}

	count = 0;
	for (cvar = cvar_vars ; cvar ; cvar = cvar->next)
	{
		if (partial && Q_strncmp(partial, cvar->name, len))
		{
			continue;
		}
		Con_SafePrintf ("%s%s %s \"%s\"\n",
			(cvar->flags & CVAR_ARCHIVE) ? "*" : " ",
			(cvar->flags & CVAR_NOTIFY)  ? "s" : " ",
			cvar->name,
			cvar->string);
		count++;
	}

	Con_SafePrintf ("%i cvars", count);
	if (partial)
	{
		Con_SafePrintf (" beginning with \"%s\"", partial);
	}
	Con_SafePrintf ("\n");
}

/*
============
Cvar_Inc_f -- johnfitz
============
*/
void Cvar_Inc_f (void)
{
	switch (Cmd_Argc())
	{
	default:
	case 1:
		Con_Printf("inc <cvar> [amount] : increment cvar\n");
		break;
	case 2:
		Cvar_SetValue (Cmd_Argv(1), Cvar_VariableValue(Cmd_Argv(1)) + 1);
		break;
	case 3:
		Cvar_SetValue (Cmd_Argv(1), Cvar_VariableValue(Cmd_Argv(1)) + Q_atof(Cmd_Argv(2)));
		break;
	}
}

/*
============
Cvar_Toggle_f -- johnfitz
============
*/
void Cvar_Toggle_f (void)
{
	switch (Cmd_Argc())
	{
	default:
	case 1:
		Con_Printf("toggle <cvar> : toggle cvar\n");
		break;
	case 2:
		if (Cvar_VariableValue(Cmd_Argv(1)))
			Cvar_Set (Cmd_Argv(1), "0");
		else
			Cvar_Set (Cmd_Argv(1), "1");
		break;
	}
}

/*
============
Cvar_Cycle_f -- johnfitz
============
*/
void Cvar_Cycle_f (void)
{
	int i;

	if (Cmd_Argc() < 3)
	{
		Con_Printf("cycle <cvar> <value list>: cycle cvar through a list of values\n");
		return;
	}

	//loop through the args until you find one that matches the current cvar value.
	//yes, this will get stuck on a list that contains the same value twice.
	//it's not worth dealing with, and i'm not even sure it can be dealt with.
	for (i = 2; i < Cmd_Argc(); i++)
	{
		//zero is assumed to be a string, even though it could actually be zero.  The worst case
		//is that the first time you call this command, it won't match on zero when it should, but after that,
		//it will be comparing strings that all had the same source (the user) so it will work.
		if (Q_atof(Cmd_Argv(i)) == 0)
		{
			if (!strcmp(Cmd_Argv(i), Cvar_VariableString(Cmd_Argv(1))))
				break;
		}
		else
		{
			if (Q_atof(Cmd_Argv(i)) == Cvar_VariableValue(Cmd_Argv(1)))
				break;
		}
	}

	if (i == Cmd_Argc())
		Cvar_Set (Cmd_Argv(1), Cmd_Argv(2)); // no match
	else if (i + 1 == Cmd_Argc())
		Cvar_Set (Cmd_Argv(1), Cmd_Argv(2)); // matched last value in list
	else
		Cvar_Set (Cmd_Argv(1), Cmd_Argv(i+1)); // matched earlier in list
}

/*
============
Cvar_Reset_f -- johnfitz
============
*/
void Cvar_Reset_f (void)
{
	switch (Cmd_Argc())
	{
	default:
	case 1:
		Con_Printf ("reset <cvar> : reset cvar to default\n");
		break;
	case 2:
		Cvar_Reset (Cmd_Argv(1));
		break;
	}
}

/*
============
Cvar_ResetAll_f -- johnfitz
============
*/
void Cvar_ResetAll_f (void)
{
	cvar_t	*var;

	for (var = cvar_vars ; var ; var = var->next)
		Cvar_Reset (var->name);
}

/*
============
Cvar_ResetCfg_f -- QuakeSpasm
============
*/
void Cvar_ResetCfg_f (void)
{
	cvar_t	*var;

	for (var = cvar_vars ; var ; var = var->next)
		if (var->flags & CVAR_ARCHIVE) Cvar_Reset (var->name);
}

//==============================================================================
//
//  INIT
//
//==============================================================================

/*
============
Cvar_Init -- johnfitz
============
*/

void Cvar_Init (void)
{
	Cmd_AddCommand ("cvarlist", Cvar_List_f);
	Cmd_AddCommand ("toggle", Cvar_Toggle_f);
	Cmd_AddCommand ("cycle", Cvar_Cycle_f);
	Cmd_AddCommand ("inc", Cvar_Inc_f);
	Cmd_AddCommand ("reset", Cvar_Reset_f);
	Cmd_AddCommand ("resetall", Cvar_ResetAll_f);
	Cmd_AddCommand ("resetcfg", Cvar_ResetCfg_f);
}

//==============================================================================
//
//  CVAR FUNCTIONS
//
//==============================================================================

/*
============
Cvar_FindVar
============
*/
cvar_t *Cvar_FindVar (const char *var_name)
{
	cvar_t	*var;

	for (var = cvar_vars ; var ; var = var->next)
	{
		if (!Q_strcmp(var_name, var->name))
			return var;
	}

	return NULL;
}

cvar_t *Cvar_FindVarAfter (const char *prev_name, unsigned int with_flags)
{
	cvar_t	*var;

	if (*prev_name)
	{
		var = Cvar_FindVar (prev_name);
		if (!var)
			return NULL;
		var = var->next;
	}
	else
		var = cvar_vars;

	// search for the next cvar matching the needed flags
	while (var)
	{
		if ((var->flags & with_flags) || !with_flags)
			break;
		var = var->next;
	}
	return var;
}

/*
============
Cvar_LockVar
============
*/
void Cvar_LockVar (const char *var_name)
{
	cvar_t	*var = Cvar_FindVar (var_name);
	if (var)
		var->flags |= CVAR_LOCKED;
}

void Cvar_UnlockVar (const char *var_name)
{
	cvar_t	*var = Cvar_FindVar (var_name);
	if (var)
		var->flags &= ~CVAR_LOCKED;
}

void Cvar_UnlockAll (void)
{
	cvar_t	*var;

	for (var = cvar_vars ; var ; var = var->next)
	{
		var->flags &= ~CVAR_LOCKED;
	}
}

/*
============
Cvar_VariableValue
============
*/
float	Cvar_VariableValue (const char *var_name)
{
	cvar_t	*var;

	var = Cvar_FindVar (var_name);
	if (!var)
		return 0;
	return Q_atof (var->string);
}


/*
============
Cvar_VariableString
============
*/
const char *Cvar_VariableString (const char *var_name)
{
	cvar_t *var;

	var = Cvar_FindVar (var_name);
	if (!var)
		return cvar_null_string;
	return var->string;
}


/*
============
Cvar_CompleteVariable
============
*/
const char *Cvar_CompleteVariable (const char *partial)
{
	cvar_t	*cvar;
	int	len;

	len = Q_strlen(partial);
	if (!len)
		return NULL;

// check functions
	for (cvar = cvar_vars ; cvar ; cvar = cvar->next)
	{
		if (!Q_strncmp(partial, cvar->name, len))
			return cvar->name;
	}

	return NULL;
}

/*
============
Cvar_Reset -- johnfitz
============
*/
void Cvar_Reset (const char *name)
{
	cvar_t	*var;

	var = Cvar_FindVar (name);
	if (!var)
		Con_Printf ("variable \"%s\" not found\n", name);
	else
		Cvar_SetQuick (var, var->default_string);
}

void Cvar_SetQuick (cvar_t *var, const char *value)
{
	if (var->flags & (CVAR_ROM|CVAR_LOCKED))
		return;
	if (!(var->flags & CVAR_REGISTERED))
		return;

	if (!var->string)
		var->string = Z_Strdup (value);
	else
	{
		int	len;

		if (!strcmp(var->string, value))
			return;	// no change

		var->flags |= CVAR_CHANGED;
		len = Q_strlen (value);
		if (len != Q_strlen(var->string))
		{
			Z_Free ((void *)var->string);
			var->string = (char *) Z_Malloc (len + 1);
		}
		memcpy ((char *)var->string, value, len + 1);
	}

	var->value = Q_atof (var->string);

	//johnfitz -- save initial value for "reset" command
	if (!var->default_string)
		var->default_string = Z_Strdup (var->string);
	//johnfitz -- during initialization, update default too
	else if (!host_initialized)
	{
	//	Sys_Printf("changing default of %s: %s -> %s\n",
	//		   var->name, var->default_string, var->string);
		Z_Free ((void *)var->default_string);
		var->default_string = Z_Strdup (var->string);
	}
	//johnfitz

	if (var->callback)
		var->callback (var);
}

void Cvar_SetValueQuick (cvar_t *var, const float value)
{
	char	val[32], *ptr = val;

	if (value == (float)((int)value))
		q_snprintf (val, sizeof(val), "%i", (int)value);
	else
	{
		q_snprintf (val, sizeof(val), "%f", value);
		// kill trailing zeroes
		while (*ptr)
			ptr++;
		while (--ptr > val && *ptr == '0' && ptr[-1] != '.')
			*ptr = '\0';
	}

	Cvar_SetQuick (var, val);
}

/*
============
Cvar_Set
============
*/
void Cvar_Set (const char *var_name, const char *value)
{
	cvar_t		*var;

	var = Cvar_FindVar (var_name);
	if (!var)
	{	// there is an error in C code if this happens
		Con_Printf ("Cvar_Set: variable %s not found\n", var_name);
		return;
	}

	Cvar_SetQuick (var, value);
}

/*
============
Cvar_SetValue
============
*/
void Cvar_SetValue (const char *var_name, const float value)
{
	char	val[32], *ptr = val;

	if (value == (float)((int)value))
		q_snprintf (val, sizeof(val), "%i", (int)value);
	else
	{
		q_snprintf (val, sizeof(val), "%f", value);
		// kill trailing zeroes
		while (*ptr)
			ptr++;
		while (--ptr > val && *ptr == '0' && ptr[-1] != '.')
			*ptr = '\0';
	}

	Cvar_Set (var_name, val);
}

/*
============
Cvar_SetROM
============
*/
void Cvar_SetROM (const char *var_name, const char *value)
{
	cvar_t *var = Cvar_FindVar (var_name);
	if (var)
	{
		var->flags &= ~CVAR_ROM;
		Cvar_SetQuick (var, value);
		var->flags |= CVAR_ROM;
	}
}

/*
============
Cvar_SetValueROM
============
*/
void Cvar_SetValueROM (const char *var_name, const float value)
{
	cvar_t *var = Cvar_FindVar (var_name);
	if (var)
	{
		var->flags &= ~CVAR_ROM;
		Cvar_SetValueQuick (var, value);
		var->flags |= CVAR_ROM;
	}
}

/*
============
Cvar_RegisterVariable

Adds a freestanding variable to the variable list.
============
*/
void Cvar_RegisterVariable (cvar_t *variable)
{
	char	value[512];
	qboolean	set_rom;
	cvar_t	*cursor,*prev; //johnfitz -- sorted list insert

// first check to see if it has already been defined
	if (Cvar_FindVar (variable->name))
	{
		Con_Printf ("Can't register variable %s, already defined\n", variable->name);
		return;
	}

// check for overlap with a command
	if (Cmd_Exists (variable->name))
	{
		Con_Printf ("Cvar_RegisterVariable: %s is a command\n", variable->name);
		return;
	}

// link the variable in
	//johnfitz -- insert each entry in alphabetical order
	if (cvar_vars == NULL ||
	    strcmp(variable->name, cvar_vars->name) < 0) // insert at front
	{
		variable->next = cvar_vars;
		cvar_vars = variable;
	}
	else //insert later
	{
		prev = cvar_vars;
		cursor = cvar_vars->next;
		while (cursor && (strcmp(variable->name, cursor->name) > 0))
		{
			prev = cursor;
			cursor = cursor->next;
		}
		variable->next = prev->next;
		prev->next = variable;
	}
	//johnfitz
	variable->flags |= CVAR_REGISTERED;

// copy the value off, because future sets will Z_Free it
	q_strlcpy (value, variable->string, sizeof(value));
	variable->string = NULL;
	variable->default_string = NULL;

	if (!(variable->flags & CVAR_CALLBACK))
		variable->callback = NULL;

// set it through the function to be consistent
	set_rom = (variable->flags & CVAR_ROM);
	variable->flags &= ~CVAR_ROM;
	Cvar_SetQuick (variable, value);
	if (set_rom)
		variable->flags |= CVAR_ROM;
}

/*
============
Cvar_SetCallback

Set a callback function to the var
============
*/
void Cvar_SetCallback (cvar_t *var, cvarcallback_t func)
{
	var->callback = func;
	if (func)
		var->flags |= CVAR_CALLBACK;
	else	var->flags &= ~CVAR_CALLBACK;
}

/*
============
Cvar_Command

Handles variable inspection and changing from the console
============
*/
qboolean	Cvar_Command (void)
{
	cvar_t			*v;

// check variables
	v = Cvar_FindVar (Cmd_Argv(0));
	if (!v)
		return false;

// perform a variable print or set
	if (Cmd_Argc() == 1)
	{
		Con_Printf ("\"%s\" is \"%s\"\n", v->name, v->string);
		return true;
	}

	Cvar_Set (v->name, Cmd_Argv(1));
	return true;
}


/*
============
Cvar_WriteVariables

Writes lines containing "set variable value" for all variables
with the archive flag set to true.
============
*/
void Cvar_WriteVariables (FILE *f)
{
	cvar_t	*var;

	for (var = cvar_vars ; var ; var = var->next)
	{
		if (var->flags & CVAR_ARCHIVE)
			fprintf (f, "%s \"%s\"\n", var->name, var->string);
	}
}

