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
// sv_edict.c -- entity dictionary

#include "quakedef.h"

int type_size[8] = {
	1, // ev_void
	1, // sizeof(string_t) / 4		// ev_string
	1, // ev_float
	3, // ev_vector
	1, // ev_entity
	1, // ev_field
	1, // sizeof(func_t) / 4		// ev_function
	1  // sizeof(void *) / 4		// ev_pointer
};

static ddef_t *ED_FieldAtOfs (int ofs);

cvar_t nomonsters = {"nomonsters", "0", CVAR_NONE};
cvar_t gamecfg = {"gamecfg", "0", CVAR_NONE};
cvar_t scratch1 = {"scratch1", "0", CVAR_NONE};
cvar_t scratch2 = {"scratch2", "0", CVAR_NONE};
cvar_t scratch3 = {"scratch3", "0", CVAR_NONE};
cvar_t scratch4 = {"scratch4", "0", CVAR_NONE};
cvar_t savedgamecfg = {"savedgamecfg", "0", CVAR_ARCHIVE};
cvar_t saved1 = {"saved1", "0", CVAR_ARCHIVE};
cvar_t saved2 = {"saved2", "0", CVAR_ARCHIVE};
cvar_t saved3 = {"saved3", "0", CVAR_ARCHIVE};
cvar_t saved4 = {"saved4", "0", CVAR_ARCHIVE};

/*
=================
ED_Alloc

Either finds a free edict, or allocates a new one.
Try to avoid reusing an entity that was recently freed, because it
can cause the client to think the entity morphed into something else
instead of being removed and recreated, which can cause interpolated
angles and bad trails.
=================
*/
edict_t *ED_Alloc (void)
{
	edict_t *e = qcvm->free_edicts_head;
	if (e && ((e->freetime < 2) || (qcvm->time - e->freetime) > 0.5))
	{
		assert (e->free);
		memset (&e->v, 0, qcvm->progs->entityfields * 4);
		e->free = false;
		if (e == qcvm->free_edicts_tail)
		{
			assert (e->next_free == NULL);
			qcvm->free_edicts_tail = NULL;
		}
		else
			assert (e->next_free);
		qcvm->free_edicts_head = e->next_free;
		assert (!e->prev_free);
		if (e->next_free)
			e->next_free->prev_free = NULL;
		e->next_free = NULL;
		return e;
	}

	if (qcvm->num_edicts == qcvm->max_edicts) // johnfitz -- use sv.max_edicts instead of MAX_EDICTS
		Host_Error ("ED_Alloc: no free edicts (max_edicts is %i)", qcvm->max_edicts);

	e = EDICT_NUM (qcvm->num_edicts++);
	e->baseline = nullentitystate;
	return e;
}

/*
=================
ED_Free

Marks the edict as free
FIXME: walk all entities and NULL out references to this entity
=================
*/
void ED_Free (edict_t *ed)
{
	if (ed->free)
	{
		// Assert that this isn't linked to any area
		assert (!ed->area.prev);
		return;
	}

	SV_UnlinkEdict (ed); // unlink from world bsp

	ed->free = true;
	ed->v.model = 0;
	ed->v.takedamage = 0;
	ed->v.modelindex = 0;
	ed->v.colormap = 0;
	ed->v.skin = 0;
	ed->v.frame = 0;
	VectorCopy (vec3_origin, ed->v.origin);
	VectorCopy (vec3_origin, ed->v.angles);
	ed->v.nextthink = -1;
	ed->v.solid = 0;
	ed->alpha = ENTALPHA_DEFAULT; // johnfitz -- reset alpha for next entity

	ed->freetime = qcvm->time;

	assert (ed->next_free == NULL);
	assert (ed->prev_free == NULL);
	if (qcvm->free_edicts_head == NULL)
	{
		assert (!qcvm->free_edicts_tail);
		qcvm->free_edicts_head = ed;
		qcvm->free_edicts_tail = ed;
	}
	else
	{
		ed->prev_free = qcvm->free_edicts_tail;
		qcvm->free_edicts_tail->next_free = ed;
		qcvm->free_edicts_tail = ed;
	}
}

/*
=================
ED_RemoveFromFreeList

Used at load time to place edicts at a specifit spot, and to trim qcvm->num_edicts
=================
*/
void ED_RemoveFromFreeList (edict_t *ed)
{
	assert (ed->free);

	if (qcvm->free_edicts_head == ed)
	{
		assert (!ed->prev_free);
		qcvm->free_edicts_head = ed->next_free;
	}
	if (qcvm->free_edicts_tail == ed)
	{
		assert (!ed->next_free);
		qcvm->free_edicts_tail = ed->prev_free;
	}
	if (ed->prev_free)
		ed->prev_free->next_free = ed->next_free;
	if (ed->next_free)
		ed->next_free->prev_free = ed->prev_free;
}

//===========================================================================

/*
============
ED_GlobalAtOfs
============
*/
static ddef_t *ED_GlobalAtOfs (int ofs)
{
	ddef_t *def;
	int		i;

	for (i = 0; i < qcvm->progs->numglobaldefs; i++)
	{
		def = &qcvm->globaldefs[i];
		if (def->ofs == ofs)
			return def;
	}
	return NULL;
}

/*
============
ED_FieldAtOfs
============
*/
static ddef_t *ED_FieldAtOfs (int ofs)
{
	ddef_t *def;
	int		i;

	for (i = 0; i < qcvm->progs->numfielddefs; i++)
	{
		def = &qcvm->fielddefs[i];
		if (def->ofs == ofs)
			return def;
	}
	return NULL;
}

/*
============
ED_FindField
============
*/
ddef_t *ED_FindField (const char *name)
{
	ddef_t **def_ptr = HashMap_Lookup (ddef_t *, qcvm->fielddefs_map, &name);
	if (def_ptr)
		return *def_ptr;
	return NULL;
}

/*
 */
int ED_FindFieldOffset (const char *name)
{
	ddef_t *def = ED_FindField (name);
	if (!def)
		return -1;
	return def->ofs;
}

/*
============
ED_FindGlobal
============
*/
ddef_t *ED_FindGlobal (const char *name)
{
	ddef_t **def_ptr = HashMap_Lookup (ddef_t *, qcvm->globaldefs_map, &name);
	if (def_ptr)
		return *def_ptr;
	return NULL;
}

/*
============
ED_FindFunction
============
*/
dfunction_t *ED_FindFunction (const char *fn_name)
{
	dfunction_t **func_ptr = HashMap_Lookup (dfunction_t *, qcvm->function_map, &fn_name);
	if (func_ptr)
		return *func_ptr;
	return NULL;
}

/*
============
GetEdictFieldValue
============
*/
eval_t *GetEdictFieldValue (edict_t *ed, int fldofs)
{
	if (fldofs < 0)
		return NULL;

	return (eval_t *)((char *)&ed->v + fldofs * 4);
}

/*
============
PR_ValueString
(etype_t type, eval_t *val)

Returns a string describing *data in a type specific manner
=============
*/
static const char *PR_ValueString (int type, eval_t *val)
{
	static char	 line[512];
	ddef_t		*def;
	dfunction_t *f;

	type &= ~DEF_SAVEGLOBAL;

	switch (type)
	{
	case ev_string:
		q_snprintf (line, sizeof (line), "%s", PR_GetString (val->string));
		break;
	case ev_entity:
		q_snprintf (line, sizeof (line), "entity %i", NUM_FOR_EDICT (PROG_TO_EDICT (val->edict)));
		break;
	case ev_function:
		f = qcvm->functions + val->function;
		q_snprintf (line, sizeof (line), "%s()", PR_GetString (f->s_name));
		break;
	case ev_field:
		def = ED_FieldAtOfs (val->_int);
		q_snprintf (line, sizeof (line), ".%s", PR_GetString (def->s_name));
		break;
	case ev_void:
		q_snprintf (line, sizeof (line), "void");
		break;
	case ev_float:
		q_snprintf (line, sizeof (line), "%5.1f", val->_float);
		break;
	case ev_ext_integer:
		q_snprintf (line, sizeof (line), "%i", val->_int);
		break;
	case ev_vector:
		q_snprintf (line, sizeof (line), "'%5.1f %5.1f %5.1f'", val->vector[0], val->vector[1], val->vector[2]);
		break;
	case ev_pointer:
		q_snprintf (line, sizeof (line), "pointer");
		break;
	default:
		q_snprintf (line, sizeof (line), "bad type %i", type);
		break;
	}

	return line;
}

/*
============
PR_UglyValueString
(etype_t type, eval_t *val)

Returns a string describing *data in a type specific manner
Easier to parse than PR_ValueString
=============
*/
const char *PR_UglyValueString (int type, eval_t *val)
{
	static char	 line[1024];
	ddef_t		*def;
	dfunction_t *f;

	type &= ~DEF_SAVEGLOBAL;

	switch (type)
	{
	case ev_string:
		q_snprintf (line, sizeof (line), "%s", PR_GetString (val->string));
		break;
	case ev_entity:
		q_snprintf (line, sizeof (line), "%i", NUM_FOR_EDICT (PROG_TO_EDICT (val->edict)));
		break;
	case ev_function:
		f = qcvm->functions + val->function;
		q_snprintf (line, sizeof (line), "%s", PR_GetString (f->s_name));
		break;
	case ev_field:
		def = ED_FieldAtOfs (val->_int);
		q_snprintf (line, sizeof (line), "%s", PR_GetString (def->s_name));
		break;
	case ev_void:
		q_snprintf (line, sizeof (line), "void");
		break;
	case ev_float:
		q_snprintf (line, sizeof (line), "%f", val->_float);
		break;
	case ev_ext_integer:
		q_snprintf (line, sizeof (line), "%i", val->_int);
		break;
	case ev_vector:
		q_snprintf (line, sizeof (line), "%f %f %f", val->vector[0], val->vector[1], val->vector[2]);
		break;
	default:
		q_snprintf (line, sizeof (line), "bad type %i", type);
		break;
	}

	return line;
}

/*
============
PR_GlobalString

Returns a string with a description and the contents of a global,
padded to 20 field width
============
*/
const char *PR_GlobalString (int ofs)
{
	static char line[512];
	const char *s;
	int			i;
	ddef_t	   *def;
	void	   *val;

	val = (void *)&qcvm->globals[ofs];
	def = ED_GlobalAtOfs (ofs);
	if (!def)
		q_snprintf (line, sizeof (line), "%i(?)", ofs);
	else
	{
		s = PR_ValueString (def->type, (eval_t *)val);
		q_snprintf (line, sizeof (line), "%i(%s)%s", ofs, PR_GetString (def->s_name), s);
	}

	i = strlen (line);
	for (; i < 20; i++)
		strcat (line, " ");
	strcat (line, " ");

	return line;
}

const char *PR_GlobalStringNoContents (int ofs)
{
	static char line[512];
	int			i;
	ddef_t	   *def;

	def = ED_GlobalAtOfs (ofs);
	if (!def)
		q_snprintf (line, sizeof (line), "%i(?)", ofs);
	else
		q_snprintf (line, sizeof (line), "%i(%s)", ofs, PR_GetString (def->s_name));

	i = strlen (line);
	for (; i < 20; i++)
		strcat (line, " ");
	strcat (line, " ");

	return line;
}

/*
=============
ED_Print

For debugging
=============
*/
void ED_Print (edict_t *ed)
{
	ddef_t	   *d;
	int		   *v;
	int			i, j, l;
	const char *name;
	int			type;

	if (ed->free)
	{
		Con_SafePrintf ("FREE\n");
		return;
	}

	Con_SafePrintf ("\nEDICT %i:\n", NUM_FOR_EDICT (ed)); // johnfitz -- was Con_Printf
	for (i = 1; i < qcvm->progs->numfielddefs; i++)
	{
		d = &qcvm->fielddefs[i];
		name = PR_GetString (d->s_name);
		l = strlen (name);
		if (l > 1 && name[l - 2] == '_')
			continue; // skip _x, _y, _z vars

		v = (int *)((char *)&ed->v + d->ofs * 4);

		// if the value is still all 0, skip the field
		type = d->type & ~DEF_SAVEGLOBAL;

		for (j = 0; j < type_size[type]; j++)
		{
			if (v[j])
				break;
		}
		if (j == type_size[type])
			continue;

		Con_SafePrintf ("%-14s %s\n", name, PR_ValueString (d->type, (eval_t *)v)); // johnfitz -- was Con_Printf
	}
}

/*
=============
ED_Write

For savegames
=============
*/
void ED_Write (FILE *f, edict_t *ed)
{
	ddef_t *d;
	int	   *v;
	int		i;
	int		type;

	if (ed->free)
	{
		fprintf (f, "{\n}\n");
		return;
	}

	fprintf (f, "{\n");

	for (i = 1; i < qcvm->progs->numfielddefs; i++)
	{
		d = &qcvm->fielddefs[i];
		type = d->type;
		assert (!!(type & DEF_SAVEGLOBAL) == (strlen (PR_GetString (d->s_name)) > 1 && PR_GetString (d->s_name)[strlen (PR_GetString (d->s_name)) - 2] == '_'));
		if (type & DEF_SAVEGLOBAL)
			continue;

		v = (int *)((char *)&ed->v + d->ofs * 4);

		// if the value is still all 0, skip the field
		assert (type < 8 && ((type == ev_vector && type_size[type] == 3) || (type != ev_vector && type_size[type] == 1)));
		if (type != ev_vector && !v[0])
			continue;
		if (type == ev_vector && !v[0] && !v[1] && !v[2])
			continue;

		fprintf (f, "\"%s\" \"%s\"\n", PR_GetString (d->s_name), PR_UglyValueString (d->type, (eval_t *)v));
	}

	// johnfitz -- save entity alpha manually when progs.dat doesn't know about alpha
	if (qcvm->extfields.alpha < 0 && ed->alpha != ENTALPHA_DEFAULT)
		fprintf (f, "\"alpha\" \"%f\"\n", ENTALPHA_TOSAVE (ed->alpha));
	// johnfitz

	fprintf (f, "}\n");
}

void ED_PrintNum (int ent)
{
	ED_Print (EDICT_NUM (ent));
}

/*
=============
ED_PrintEdicts

For debugging, prints all the entities in the current server
=============
*/
void ED_PrintEdicts (void)
{
	int i;

	if (!sv.active)
		return;

	PR_SwitchQCVM (&sv.qcvm);
	Con_Printf ("%i entities\n", qcvm->num_edicts);
	for (i = 0; i < qcvm->num_edicts; i++)
		ED_PrintNum (i);
	PR_SwitchQCVM (NULL);
}

/*
=============
ED_PrintEdict_f

For debugging, prints a single edicy
=============
*/
static void ED_PrintEdict_f (void)
{
	int i;

	if (!sv.active)
		return;

	i = atoi (Cmd_Argv (1));
	PR_SwitchQCVM (&sv.qcvm);
	if (i < 0 || i >= qcvm->num_edicts)
		Con_Printf ("Bad edict number\n");
	else
	{
		if (Cmd_Argc () == 2 || svs.maxclients != 1) // edict N
			ED_PrintNum (i);
		else // edict N FLD ...
		{
			ddef_t *def = ED_FindField (Cmd_Argv (2));
			if (!def)
				Con_Printf ("Field %s not defined\n", Cmd_Argv (2));
			else if (Cmd_Argc () < 4)
				Con_Printf (
					"Edict %u.%s==%s\n", i, PR_GetString (def->s_name),
					PR_UglyValueString (def->type & ~DEF_SAVEGLOBAL, (eval_t *)((char *)&EDICT_NUM (i)->v + def->ofs * 4)));
			else
				ED_ParseEpair ((void *)&EDICT_NUM (i)->v, def, Cmd_Argv (3), false);
		}
	}
	PR_SwitchQCVM (NULL);
}

/*
=============
ED_Count

For debugging
=============
*/
static void ED_Count (void)
{
	edict_t *ent;
	int		 i, active, models, solid, step;

	if (!sv.active)
		return;

	PR_SwitchQCVM (&sv.qcvm);
	active = models = solid = step = 0;
	for (i = 0; i < qcvm->num_edicts; i++)
	{
		ent = EDICT_NUM (i);
		if (ent->free)
			continue;
		active++;
		if (ent->v.solid)
			solid++;
		if (ent->v.model)
			models++;
		if (ent->v.movetype == MOVETYPE_STEP)
			step++;
	}

	Con_Printf ("num_edicts:%3i\n", qcvm->num_edicts);
	Con_Printf ("active    :%3i\n", active);
	Con_Printf ("view      :%3i\n", models);
	Con_Printf ("touch     :%3i\n", solid);
	Con_Printf ("step      :%3i\n", step);
	PR_SwitchQCVM (NULL);
}

/*
==============================================================================

ARCHIVING GLOBALS

FIXME: need to tag constants, doesn't really work
==============================================================================
*/

/*
=============
ED_WriteGlobals
=============
*/
void ED_WriteGlobals (FILE *f)
{
	ddef_t	   *def;
	int			i;
	const char *name;
	int			type;

	fprintf (f, "{\n");
	for (i = 0; i < qcvm->progs->numglobaldefs; i++)
	{
		def = &qcvm->globaldefs[i];
		type = def->type;
		if (!(def->type & DEF_SAVEGLOBAL))
			continue;
		type &= ~DEF_SAVEGLOBAL;

		if (type != ev_string && type != ev_float && type != ev_ext_integer && type != ev_entity)
			continue;

		name = PR_GetString (def->s_name);
		fprintf (f, "\"%s\" ", name);
		fprintf (f, "\"%s\"\n", PR_UglyValueString (type, (eval_t *)&qcvm->globals[def->ofs]));
	}
	fprintf (f, "}\n");
}

/*
=============
ED_ParseGlobals
=============
*/
const char *ED_ParseGlobals (const char *data)
{
	char	keyname[64];
	ddef_t *key;

	while (1)
	{
		// parse key
		data = COM_Parse (data);
		if (com_token[0] == '}')
			break;
		if (!data)
			Host_Error ("ED_ParseEntity: EOF without closing brace");

		q_strlcpy (keyname, com_token, sizeof (keyname));

		// parse value
		data = COM_Parse (data);
		if (!data)
			Host_Error ("ED_ParseEntity: EOF without closing brace");

		if (com_token[0] == '}')
			Host_Error ("ED_ParseEntity: closing brace without data");

		key = ED_FindGlobal (keyname);
		if (!key)
		{
			Con_Printf ("'%s' is not a global\n", keyname);
			continue;
		}

		if (!ED_ParseEpair ((void *)qcvm->globals, key, com_token, false))
			Host_Error ("ED_ParseGlobals: parse error");
	}
	return data;
}

//============================================================================

/*
=============
ED_NewString
=============
*/
static string_t ED_NewString (const char *string)
{
	char	*new_p;
	int		 i, l;
	string_t num;

	l = strlen (string) + 1;
	num = PR_AllocString (l, &new_p);

	for (i = 0; i < l; i++)
	{
		if (string[i] == '\\' && i < l - 1)
		{
			i++;
			if (string[i] == 'n')
				*new_p++ = '\n';
			else
				*new_p++ = '\\';
		}
		else
			*new_p++ = string[i];
	}

	return num;
}
static void ED_RezoneString (string_t *ref, const char *str)
{
	char  *buf;
	size_t len = strlen (str) + 1;
	size_t id;

	if (*ref)
	{ // if the reference is already a zoned string then free it first.
		id = -1 - *ref;
		if (id < qcvm->knownzonesize && (qcvm->knownzone[id >> 3] & (1u << (id & 7))))
		{ // okay, it was zoned.
			qcvm->knownzone[id >> 3] &= ~(1u << (id & 7));
			buf = (char *)PR_GetString (*ref);
			PR_ClearEngineString (*ref);
			Mem_Free (buf);
		}
		//		else
		//			Con_Warning("ED_RezoneString: string wasn't strzoned\n");	//warnings would trigger from the default cvar value that autocvars are
		// initialised with
	}

	buf = Mem_Alloc (len);
	memcpy (buf, str, len);
	id = -1 - (*ref = PR_SetEngineString (buf));
	// make sure its flagged as zoned so we can clean up properly after.
	if (id >= qcvm->knownzonesize)
	{
		int old_size = (qcvm->knownzonesize + 7) >> 3;
		qcvm->knownzonesize = (id + 32) & ~7;
		int new_size = (qcvm->knownzonesize + 7) >> 3;
		qcvm->knownzone = Mem_Realloc (qcvm->knownzone, new_size);
		memset (qcvm->knownzone + old_size, 0, new_size - old_size);
	}
	qcvm->knownzone[id >> 3] |= 1u << (id & 7);
}

/*
=============
ED_ParseEval

Can parse either fields or globals
returns false if error
=============
*/
qboolean ED_ParseEpair (void *base, ddef_t *key, const char *s, qboolean zoned)
{
	int			 i;
	char		 string[128];
	ddef_t		*def;
	char		*v, *w;
	char		*end;
	void		*d;
	dfunction_t *func;

	d = (void *)((int *)base + key->ofs);

	switch (key->type & ~DEF_SAVEGLOBAL)
	{
	case ev_string:
		if (zoned) // zoned version allows us to change the strings more freely
			ED_RezoneString ((string_t *)d, s);
		else
			*(string_t *)d = ED_NewString (s);
		break;

	case ev_float:
		*(float *)d = atof (s);
		break;

	case ev_ext_integer:
		*(int *)d = atoi (s);
		break;

	case ev_vector:
		q_strlcpy (string, s, sizeof (string));
		end = (char *)string + strlen (string);
		v = string;
		w = string;

		for (i = 0; i < 3 && (w <= end); i++) // ericw -- added (w <= end) check
		{
			// set v to the next space (or 0 byte), and change that char to a 0 byte
			while (*v && *v != ' ')
				v++;
			*v = 0;
			((float *)d)[i] = atof (w);
			w = v = v + 1;
		}
		// ericw -- fill remaining elements to 0 in case we hit the end of string
		// before reading 3 floats.
		if (i < 3)
		{
			Con_DWarning ("Avoided reading garbage for \"%s\" \"%s\"\n", PR_GetString (key->s_name), s);
			for (; i < 3; i++)
				((float *)d)[i] = 0.0f;
		}
		break;

	case ev_entity:
		if (!strncmp (s, "entity ", 7)) // Spike: putentityfieldstring/etc should be able to cope with etos's weirdness.
			s += 7;
		*(int *)d = EDICT_TO_PROG (EDICT_NUM (atoi (s)));
		break;

	case ev_field:
		def = ED_FindField (s);
		if (!def)
		{
			// johnfitz -- HACK -- suppress error becuase fog/sky fields might not be mentioned in defs.qc
			if (strncmp (s, "sky", 3) && strcmp (s, "fog"))
				Con_DPrintf ("Can't find field %s\n", s);
			return false;
		}
		*(int *)d = G_INT (def->ofs);
		break;

	case ev_function:
		func = ED_FindFunction (s);
		if (!func)
		{
			Con_Printf ("Can't find function %s\n", s);
			return false;
		}
		*(func_t *)d = func - qcvm->functions;
		break;

	default:
		break;
	}
	return true;
}

/*
====================
ED_ParseEdict

Parses an edict out of the given string, returning the new position
ed should be a properly initialized empty edict.
Used for initial level load and for savegames.
====================
*/
const char *ED_ParseEdict (const char *data, edict_t *ent)
{
	ddef_t	*key;
	char	 keyname[256];
	qboolean anglehack, init;
	int		 n;

	init = false;

	// clear it
	if (ent != qcvm->edicts) // hack
		memset (&ent->v, 0, qcvm->progs->entityfields * 4);

	// go through all the dictionary pairs
	while (1)
	{
		// parse key
		data = COM_Parse (data);
		if (com_token[0] == '}')
			break;
		if (!data)
			Host_Error ("ED_ParseEntity: EOF without closing brace");

		// anglehack is to allow QuakeEd to write single scalar angles
		// and allow them to be turned into vectors. (FIXME...)
		if (!strcmp (com_token, "angle"))
		{
			strcpy (com_token, "angles");
			anglehack = true;
		}
		else
			anglehack = false;

		// FIXME: change light to _light to get rid of this hack
		if (!strcmp (com_token, "light"))
			strcpy (com_token, "light_lev"); // hack for single light def

		q_strlcpy (keyname, com_token, sizeof (keyname));

		// another hack to fix keynames with trailing spaces
		n = strlen (keyname);
		while (n && keyname[n - 1] == ' ')
		{
			keyname[n - 1] = 0;
			n--;
		}

		// parse value
		data = COM_Parse (data);
		if (!data)
			Host_Error ("ED_ParseEntity: EOF without closing brace");

		if (com_token[0] == '}')
			Host_Error ("ED_ParseEntity: closing brace without data");

		init = true;

		// keynames with a leading underscore are used for utility comments,
		// and are immediately discarded by quake
		if (keyname[0] == '_')
			continue;

		// johnfitz -- hack to support .alpha even when progs.dat doesn't know about it
		if (!strcmp (keyname, "alpha"))
			ent->alpha = ENTALPHA_ENCODE (atof (com_token));
		// johnfitz

		key = ED_FindField (keyname);
		if (!key)
		{
#ifdef PSET_SCRIPT
			eval_t *val;
			if (!strcmp (keyname, "traileffect") && qcvm == &sv.qcvm && sv.state == ss_loading)
			{
				if ((val = GetEdictFieldValue (ent, qcvm->extfields.traileffectnum)))
					val->_float = PF_SV_ForceParticlePrecache (com_token);
			}
			else if (!strcmp (keyname, "emiteffect") && qcvm == &sv.qcvm && sv.state == ss_loading)
			{
				if ((val = GetEdictFieldValue (ent, qcvm->extfields.emiteffectnum)))
					val->_float = PF_SV_ForceParticlePrecache (com_token);
			}
			// johnfitz -- HACK -- suppress error becuase fog/sky/alpha fields might not be mentioned in defs.qc
			else
#endif
				if (strncmp (keyname, "sky", 3) && strcmp (keyname, "fog") && strcmp (keyname, "alpha"))
				Con_DPrintf ("\"%s\" is not a field\n", keyname); // johnfitz -- was Con_Printf
			continue;
		}

		if (anglehack)
		{
			char temp[32];
			assert (sizeof (com_token) >= sizeof (temp));
			q_snprintf (temp, sizeof (temp), "0 %s 0", com_token);
			memcpy (com_token, temp, sizeof (temp));
		}

		if (!ED_ParseEpair ((void *)&ent->v, key, com_token, qcvm != &sv.qcvm))
			Host_Error ("ED_ParseEdict: parse error");
	}

	if (!init)
		ED_Free (ent);

	return data;
}

/*
================
ED_LoadFromFile

The entities are directly placed in the array, rather than allocated with
ED_Alloc, because otherwise an error loading the map would have entity
number references out of order.

Creates a server's entity / program execution context by
parsing textual entity definitions out of an ent file.

Used for both fresh maps and savegame loads.  A fresh map would also need
to call ED_CallSpawnFunctions () to let the objects initialize themselves.
================
*/
void ED_LoadFromFile (const char *data)
{
	dfunction_t *func;
	edict_t		*ent = NULL;
	int			 inhibit = 0;
	int			 usingspawnfunc = 0;

	pr_global_struct->time = qcvm->time;

	// parse ents
	while (1)
	{
		// parse the opening brace
		data = COM_Parse (data);
		if (!data)
			break;
		if (com_token[0] != '{')
			Host_Error ("ED_LoadFromFile: found %s when expecting {", com_token);

		if (!ent)
			ent = EDICT_NUM (0);
		else
			ent = ED_Alloc ();
		data = ED_ParseEdict (data, ent);

		// remove things from different skill levels or deathmatch
		if (deathmatch.value)
		{
			if (((int)ent->v.spawnflags & SPAWNFLAG_NOT_DEATHMATCH))
			{
				ED_Free (ent);
				inhibit++;
				continue;
			}
		}
		else if (
			(current_skill == 0 && ((int)ent->v.spawnflags & SPAWNFLAG_NOT_EASY)) || (current_skill == 1 && ((int)ent->v.spawnflags & SPAWNFLAG_NOT_MEDIUM)) ||
			(current_skill >= 2 && ((int)ent->v.spawnflags & SPAWNFLAG_NOT_HARD)))
		{
			ED_Free (ent);
			inhibit++;
			continue;
		}

		//
		// immediately call spawn function
		//
		if (!ent->v.classname)
		{
			Con_SafePrintf ("No classname for:\n"); // johnfitz -- was Con_Printf
			ED_Print (ent);
			ED_Free (ent);
			continue;
		}

		// look for the spawn function
		//
		func = ED_FindFunction (va ("spawnfunc_%s", PR_GetString (ent->v.classname)));
		if (func)
		{
			if (!usingspawnfunc++)
				Con_DPrintf2 ("Using DP_SV_SPAWNFUNC_PREFIX\n");
		}
		else
			func = ED_FindFunction (PR_GetString (ent->v.classname));

		if (!func)
		{
			Con_SafePrintf ("No spawn function for:\n"); // johnfitz -- was Con_Printf
			ED_Print (ent);
			ED_Free (ent);
			continue;
		}

		pr_global_struct->self = EDICT_TO_PROG (ent);
		PR_ExecuteProgram (func - qcvm->functions);
	}

	Con_DPrintf ("%i entities inhibited\n", inhibit);
}

#ifndef PR_SwitchQCVM
qcvm_t		 *qcvm;
globalvars_t *pr_global_struct;
void		  PR_SwitchQCVM (qcvm_t *nvm)
{
	if (qcvm && nvm)
		Sys_Error ("PR_SwitchQCVM: A qcvm was already active");
	qcvm = nvm;
	if (qcvm)
		pr_global_struct = (globalvars_t *)qcvm->globals;
	else
		pr_global_struct = NULL;
}
#endif

void PR_ClearProgs (qcvm_t *vm)
{
	qcvm_t *oldvm = qcvm;
	if (!vm->progs)
		return; // wasn't loaded.
	qcvm = NULL;
	PR_SwitchQCVM (vm);
	PR_ShutdownExtensions ();

	if (qcvm->knownstrings)
	{
		for (int i = 0; i < qcvm->numknownstrings; ++i)
			if (qcvm->knownstringsowned[i])
				Mem_Free (qcvm->knownstrings[i]);
		Mem_Free ((void *)qcvm->knownstrings);
		Mem_Free (qcvm->knownstringsowned);
	}
	Mem_Free (qcvm->edicts); // ericw -- sv.edicts switched to use malloc()
	if (qcvm->fielddefs != (ddef_t *)((byte *)qcvm->progs + qcvm->progs->ofs_fielddefs))
		Mem_Free (qcvm->fielddefs);
	Mem_Free (qcvm->progs); // spike -- pr_progs switched to use malloc (so menuqc doesn't end up stuck on the early hunk nor wiped on every map change)
	HashMap_Destroy (qcvm->function_map);
	HashMap_Destroy (qcvm->fielddefs_map);
	HashMap_Destroy (qcvm->globaldefs_map);
	memset (qcvm, 0, sizeof (*qcvm));

	qcvm = NULL;
	PR_SwitchQCVM (oldvm);
}

// makes sure extension fields are actually registered so they can be used for mappers without qc changes. eg so scale can be used.
static void PR_MergeEngineFieldDefs (void)
{
	struct
	{
		const char *fname;
		etype_t		type;
		int			newidx;
	} extrafields[] = {
		// table of engine fields to add. we'll be using ED_FindFieldOffset for these later.
		// this is useful for fields that should be defined for mappers which are not defined by the mod.
		// future note: mutators will need to edit the mutator's globaldefs table too. remember to handle vectors and their 3 globals too.
		{"alpha", ev_float},		  // just because we can (though its already handled in a weird hacky way)
		{"scale", ev_float},		  // hurrah for being able to rescale entities.
		{"emiteffectnum", ev_float},  // constantly emitting particles, even without moving.
		{"traileffectnum", ev_float}, // custom effect for trails
									  //{"glow_size",		ev_float},	//deprecated particle trail rubbish
									  //{"glow_color",	ev_float},	//deprecated particle trail rubbish
		{"tag_entity", ev_float},	  // for setattachment to not bug out when omitted.
		{"tag_index", ev_float},	  // for setattachment to not bug out when omitted.
		{"modelflags", ev_float},	  // deprecated rubbish to fill the high 8 bits of effects.
									  //{"vw_index",		ev_float},	//modelindex2
									  //{"pflags",		ev_float},	//for rtlights
									  //{"drawflags",		ev_float},	//hexen2 compat
									  //{"abslight",		ev_float},	//hexen2 compat
		{"colormod", ev_vector},	  // lighting tints
									  //{"glowmod",		ev_vector},	//fullbright tints
									  //{"fatness",		ev_float},	//bloated rendering...
									  //{"gravitydir",	ev_vector},	//says which direction gravity should act for this ent...

	};
	int			 maxofs = qcvm->progs->entityfields;
	int			 maxdefs = qcvm->progs->numfielddefs;
	unsigned int j, a;

	// figure out where stuff goes
	for (j = 0; j < countof (extrafields); j++)
	{
		extrafields[j].newidx = ED_FindFieldOffset (extrafields[j].fname);
		if (extrafields[j].newidx < 0)
		{
			extrafields[j].newidx = maxofs;
			maxdefs++;
			if (extrafields[j].type == ev_vector)
				maxdefs += 3;
			maxofs += type_size[extrafields[j].type];
		}
	}

	if (maxdefs != qcvm->progs->numfielddefs)
	{ // we now know how many entries we need to add...
		ddef_t *olddefs = qcvm->fielddefs;
		qcvm->fielddefs = Mem_Alloc (maxdefs * sizeof (*qcvm->fielddefs));
		memcpy (qcvm->fielddefs, olddefs, qcvm->progs->numfielddefs * sizeof (*qcvm->fielddefs));
		if (olddefs != (ddef_t *)((byte *)qcvm->progs + qcvm->progs->ofs_fielddefs))
			Mem_Free (olddefs);

		// allocate the extra defs
		for (j = 0; j < countof (extrafields); j++)
		{
			if (extrafields[j].newidx >= qcvm->progs->entityfields && extrafields[j].newidx < maxofs)
			{ // looks like its new. make sure ED_FindField can find it.
				qcvm->fielddefs[qcvm->progs->numfielddefs].ofs = extrafields[j].newidx;
				qcvm->fielddefs[qcvm->progs->numfielddefs].type = extrafields[j].type;
				qcvm->fielddefs[qcvm->progs->numfielddefs].s_name = ED_NewString (extrafields[j].fname);
				const ddef_t *def_ptr = &qcvm->fielddefs[qcvm->progs->numfielddefs];
				HashMap_Insert (qcvm->fielddefs_map, &extrafields[j].fname, &def_ptr);
				qcvm->progs->numfielddefs++;

				if (extrafields[j].type == ev_vector)
				{ // vectors are weird and annoying.
					for (a = 0; a < 3; a++)
					{
						qcvm->fielddefs[qcvm->progs->numfielddefs].ofs = extrafields[j].newidx + a;
						qcvm->fielddefs[qcvm->progs->numfielddefs].type = ev_float | DEF_SAVEGLOBAL;
						const char *fielddef_name = va ("%s_%c", extrafields[j].fname, 'x' + a);
						qcvm->fielddefs[qcvm->progs->numfielddefs].s_name = ED_NewString (fielddef_name);
						const ddef_t *def_ptr_v = &qcvm->fielddefs[qcvm->progs->numfielddefs];
						HashMap_Insert (qcvm->fielddefs_map, &fielddef_name, &def_ptr_v);
						qcvm->progs->numfielddefs++;
					}
				}
			}
		}
		qcvm->progs->entityfields = maxofs;
	}
}

/*
===============
PR_HasGlobal
===============
*/
static qboolean PR_HasGlobal (const char *name, float value)
{
	ddef_t *g = ED_FindGlobal (name);
	return g && (g->type & ~DEF_SAVEGLOBAL) == ev_float && G_FLOAT (g->ofs) == value;
}

/*
===============
PR_FindSupportedEffects

Disables Quake 2021 release effects flags when not present in progs.dat to avoid conflicts
(e.g. Arcane Dimensions uses bit 32 for its explosions, same as EF_QEX_PENTALIGHT)
===============
*/
static void PR_FindSupportedEffects (void)
{
	if (qcvm == &sv.qcvm)
	{
		qboolean isqex = PR_HasGlobal ("EF_QUADLIGHT", EF_QEX_QUADLIGHT) &&
						 (PR_HasGlobal ("EF_PENTLIGHT", EF_QEX_PENTALIGHT) || PR_HasGlobal ("EF_PENTALIGHT", EF_QEX_PENTALIGHT));
		sv.effectsmask = isqex ? -1 : -1 & ~(EF_QEX_QUADLIGHT | EF_QEX_PENTALIGHT | EF_QEX_CANDLELIGHT);
	}
}

/* for 2021 re-release */
typedef struct
{
	const char *name;
	int			first_statement;
	int			patch_statement;
} exbuiltin_t;

/*
===============
PR_PatchRereleaseBuiltins

for 2021 re-release
===============
*/
static const exbuiltin_t exbuiltins[] = {
	/* Update-1 adds the following builtins with new ids. Patch them to use old indices.
	 * (https://steamcommunity.com/games/2310/announcements/detail/2943653788150871156) */
	{"centerprint", -90, -73},
	{"bprint", -91, -23},
	{"sprint", -92, -24},
	{NULL, 0, 0} /* end-of-list. */
};

static void PR_PatchRereleaseBuiltins (void)
{
	const exbuiltin_t *ex = exbuiltins;
	dfunction_t		  *f;

	for (; ex->name != NULL; ++ex)
	{
		f = ED_FindFunction (ex->name);
		if (f && f->first_statement == ex->first_statement)
			f->first_statement = ex->patch_statement;
	}
}

/*
===============
PR_LoadProgs
===============
*/
qboolean PR_LoadProgs (const char *filename, qboolean fatal, unsigned int needcrc, builtin_t *builtins, size_t numbuiltins)
{
	int i;

	PR_ClearProgs (qcvm); // just in case.

	qcvm->progs = (dprograms_t *)COM_LoadFile (filename, NULL);
	if (!qcvm->progs)
		return false;

	qcvm->progssize = com_filesize;
	CRC_Init (&qcvm->progscrc);
	for (i = 0; i < com_filesize; i++)
		CRC_ProcessByte (&qcvm->progscrc, ((byte *)qcvm->progs)[i]);
	qcvm->progshash = Com_BlockChecksum (qcvm->progs, com_filesize);

	// byte swap the header
	for (i = 0; i < (int)sizeof (*qcvm->progs) / 4; i++)
		((int *)qcvm->progs)[i] = LittleLong (((int *)qcvm->progs)[i]);

	if (qcvm->progs->version != PROG_VERSION)
	{
		if (fatal)
			Host_Error ("%s has wrong version number (%i should be %i)", filename, qcvm->progs->version, PROG_VERSION);
		else
		{
			Con_Printf ("%s ABI set not supported\n", filename);
			qcvm->progs = NULL;
			return false;
		}
	}
	if (qcvm->progs->crc != needcrc)
	{
		if (fatal)
			Host_Error ("%s system vars have been modified, progdefs.h is out of date", filename);
		else
		{
			switch (qcvm->progs->crc)
			{
			case 22390: // full csqc
				Con_Printf ("%s - full csqc is not supported\n", filename);
				break;
			case 52195: // dp csqc
				Con_Printf ("%s - obsolete csqc is not supported\n", filename);
				break;
			case 54730: // quakeworld
				Con_Printf ("%s - quakeworld gamecode is not supported\n", filename);
				break;
			case 26940: // prerelease
				Con_Printf ("%s - prerelease gamecode is not supported\n", filename);
				break;
			case 32401: // tenebrae
				Con_Printf ("%s - tenebrae gamecode is not supported\n", filename);
				break;
			case 38488: // hexen2 release
			case 26905: // hexen2 mission pack
			case 14046: // hexen2 demo
				Con_Printf ("%s - hexen2 gamecode is not supported\n", filename);
				break;
			// case 5927: //nq PROGHEADER_CRC as above. shouldn't happen, obviously.
			default:
				Con_Printf ("%s system vars are not supported\n", filename);
				break;
			}
			qcvm->progs = NULL;
			return false;
		}
	}
	Con_DPrintf ("%s occupies %iK.\n", filename, com_filesize / 1024);

	qcvm->functions = (dfunction_t *)((byte *)qcvm->progs + qcvm->progs->ofs_functions);
	qcvm->strings = (char *)qcvm->progs + qcvm->progs->ofs_strings;
	if (qcvm->progs->ofs_strings + qcvm->progs->numstrings >= com_filesize)
		Host_Error ("%s strings go past end of file\n", filename);

	qcvm->globaldefs = (ddef_t *)((byte *)qcvm->progs + qcvm->progs->ofs_globaldefs);
	qcvm->fielddefs = (ddef_t *)((byte *)qcvm->progs + qcvm->progs->ofs_fielddefs);
	qcvm->statements = (dstatement_t *)((byte *)qcvm->progs + qcvm->progs->ofs_statements);

	qcvm->globals = (float *)((byte *)qcvm->progs + qcvm->progs->ofs_globals);
	pr_global_struct = (globalvars_t *)qcvm->globals;

	qcvm->stringssize = qcvm->progs->numstrings;

	// byte swap the lumps
	for (i = 0; i < qcvm->progs->numstatements; i++)
	{
		qcvm->statements[i].op = LittleShort (qcvm->statements[i].op);
		qcvm->statements[i].a = LittleShort (qcvm->statements[i].a);
		qcvm->statements[i].b = LittleShort (qcvm->statements[i].b);
		qcvm->statements[i].c = LittleShort (qcvm->statements[i].c);
	}

	for (i = 0; i < qcvm->progs->numfunctions; i++)
	{
		qcvm->functions[i].first_statement = LittleLong (qcvm->functions[i].first_statement);
		qcvm->functions[i].parm_start = LittleLong (qcvm->functions[i].parm_start);
		qcvm->functions[i].s_name = LittleLong (qcvm->functions[i].s_name);
		qcvm->functions[i].s_file = LittleLong (qcvm->functions[i].s_file);
		qcvm->functions[i].numparms = LittleLong (qcvm->functions[i].numparms);
		qcvm->functions[i].locals = LittleLong (qcvm->functions[i].locals);
	}
	// Just to be sure: Reverse insert because there can be duplicates and we want
	// to match linear search with hash lookup (find first)
	qcvm->function_map = HashMap_Create (const char *, dfunction_t *, &HashStr, &HashStrCmp);
	HashMap_Reserve (qcvm->function_map, qcvm->progs->numfunctions);
	for (i = qcvm->progs->numfunctions - 1; i >= 0; --i)
	{
		const char		  *func_name = PR_GetString (qcvm->functions[i].s_name);
		const dfunction_t *func_ptr = &qcvm->functions[i];
		HashMap_Insert (qcvm->function_map, &func_name, &func_ptr);
	}

	for (i = 0; i < qcvm->progs->numglobaldefs; i++)
	{
		qcvm->globaldefs[i].type = LittleShort (qcvm->globaldefs[i].type);
		qcvm->globaldefs[i].ofs = LittleShort (qcvm->globaldefs[i].ofs);
		qcvm->globaldefs[i].s_name = LittleLong (qcvm->globaldefs[i].s_name);
	}
	qcvm->globaldefs_map = HashMap_Create (const char *, ddef_t *, &HashStr, &HashStrCmp);
	HashMap_Reserve (qcvm->globaldefs_map, qcvm->progs->numglobaldefs);
	for (i = qcvm->progs->numglobaldefs - 1; i >= 0; --i)
	{
		const char	 *globaldef_name = PR_GetString (qcvm->globaldefs[i].s_name);
		const ddef_t *def_ptr = &qcvm->globaldefs[i];
		HashMap_Insert (qcvm->globaldefs_map, &globaldef_name, &def_ptr);
	}

	for (i = 0; i < qcvm->progs->numfielddefs; i++)
	{
		qcvm->fielddefs[i].type = LittleShort (qcvm->fielddefs[i].type);
		if (qcvm->fielddefs[i].type & DEF_SAVEGLOBAL)
			Host_Error ("PR_LoadProgs: pr_fielddefs[i].type & DEF_SAVEGLOBAL");
		qcvm->fielddefs[i].ofs = LittleShort (qcvm->fielddefs[i].ofs);
		qcvm->fielddefs[i].s_name = LittleLong (qcvm->fielddefs[i].s_name);
	}
	qcvm->fielddefs_map = HashMap_Create (const char *, ddef_t *, &HashStr, &HashStrCmp);
	HashMap_Reserve (qcvm->fielddefs_map, qcvm->progs->numfielddefs + 11); // up to 7 scalar + 1 vector engine autofields
	for (i = qcvm->progs->numfielddefs - 1; i >= 0; --i)
	{
		const char	 *fielddef_name = PR_GetString (qcvm->fielddefs[i].s_name);
		const ddef_t *def_ptr = &qcvm->fielddefs[i];
		HashMap_Insert (qcvm->fielddefs_map, &fielddef_name, &def_ptr);
		const size_t len = strlen (fielddef_name);
		if (len > 1 && fielddef_name[len - 2] == '_')
			qcvm->fielddefs[i].type |= DEF_SAVEGLOBAL;
	}

	for (i = 0; i < qcvm->progs->numglobals; i++)
		((int *)qcvm->globals)[i] = LittleLong (((int *)qcvm->globals)[i]);

	memcpy (qcvm->builtins, builtins, numbuiltins * sizeof (qcvm->builtins[0]));
	qcvm->numbuiltins = numbuiltins;

	// spike: detect extended fields from progs
	PR_MergeEngineFieldDefs ();
#define QCEXTFIELD(n, t) qcvm->extfields.n = ED_FindFieldOffset (#n);
	QCEXTFIELDS_ALL
	QCEXTFIELDS_GAME
	QCEXTFIELDS_SS
#undef QCEXTFIELD

	qcvm->edict_size = qcvm->progs->entityfields * 4 + sizeof (edict_t) - sizeof (entvars_t);
	// round off to next highest whole word address (esp for Alpha)
	// this ensures that pointers in the engine data area are always
	// properly aligned
	qcvm->edict_size += sizeof (void *) - 1;
	qcvm->edict_size &= ~(sizeof (void *) - 1);

	PR_SetEngineString ("");
	PR_EnableExtensions (qcvm->globaldefs);
	PR_PatchRereleaseBuiltins ();
	PR_FindSupportedEffects ();

	qcvm->progsstrings = qcvm->numknownstrings;
	return true;
}

/*
===============
PR_Init
===============
*/
void PR_Init (void)
{
	Cmd_AddCommand ("edict", ED_PrintEdict_f);
	Cmd_AddCommand ("edicts", ED_PrintEdicts);
	Cmd_AddCommand ("edictcount", ED_Count);
	Cmd_AddCommand ("profile", PR_Profile_f);
	Cmd_AddCommand ("pr_dumpplatform", PR_DumpPlatform_f);
	Cvar_RegisterVariable (&nomonsters);
	Cvar_RegisterVariable (&gamecfg);
	Cvar_RegisterVariable (&scratch1);
	Cvar_RegisterVariable (&scratch2);
	Cvar_RegisterVariable (&scratch3);
	Cvar_RegisterVariable (&scratch4);
	Cvar_RegisterVariable (&savedgamecfg);
	Cvar_RegisterVariable (&saved1);
	Cvar_RegisterVariable (&saved2);
	Cvar_RegisterVariable (&saved3);
	Cvar_RegisterVariable (&saved4);

	PR_InitExtensions ();
}

edict_t *EDICT_NUM (int n)
{
	if (n < 0 || n >= qcvm->max_edicts)
		Host_Error ("EDICT_NUM: bad number %i", n);
	return (edict_t *)((byte *)qcvm->edicts + (n)*qcvm->edict_size);
}

int NUM_FOR_EDICT (edict_t *e)
{
	int b;

	b = (byte *)e - (byte *)qcvm->edicts;
	b = b / qcvm->edict_size;

	if (b < 0 || b >= qcvm->num_edicts)
		Host_Error ("NUM_FOR_EDICT: bad pointer");
	return b;
}

//===========================================================================

#define PR_STRING_ALLOCSLOTS 256

static void PR_AllocStringSlots (void)
{
	qcvm->maxknownstrings += PR_STRING_ALLOCSLOTS;
	Con_DPrintf2 ("PR_AllocStringSlots: realloc'ing for %d slots\n", qcvm->maxknownstrings);
	qcvm->knownstrings = (const char **)Mem_Realloc ((void *)qcvm->knownstrings, qcvm->maxknownstrings * sizeof (char *));
	qcvm->knownstringsowned = (qboolean *)Mem_Realloc ((void *)qcvm->knownstringsowned, qcvm->maxknownstrings * sizeof (qboolean));
}

const char *PR_GetString (int num)
{
	if (num >= 0 && num < qcvm->stringssize)
		return qcvm->strings + num;
	else if (num < 0 && num >= -qcvm->numknownstrings)
	{
		if (!qcvm->knownstrings[-1 - num])
		{
			Host_Error ("PR_GetString: attempt to get a non-existant string %d\n", num);
			return "";
		}
		return qcvm->knownstrings[-1 - num];
	}
	else
	{
		return qcvm->strings;
		Host_Error ("PR_GetString: invalid string offset %d\n", num);
		return "";
	}
}

void PR_ClearEngineString (int num)
{
	if (num < 0 && num >= -qcvm->numknownstrings)
	{
		num = -1 - num;
		if (qcvm->knownstringsowned[num])
		{
			SAFE_FREE (qcvm->knownstrings[num]);
			qcvm->knownstringsowned[num] = false;
		}
		else
			qcvm->knownstrings[num] = NULL;
		if (qcvm->freeknownstrings > num)
			qcvm->freeknownstrings = num;
	}
}

int PR_SetEngineString (const char *s)
{
	int i;

	if (!s)
		return 0;
#if 0 /* can't: sv.model_precache & sv.sound_precache points to pr_strings */
	if (s >= qcvm->strings && s <= qcvm->strings + qcvm->stringssize)
		Host_Error("PR_SetEngineString: \"%s\" in pr_strings area\n", s);
#else
	if (s >= qcvm->strings && s <= qcvm->strings + qcvm->stringssize - 2)
		return (int)(s - qcvm->strings);
#endif
	for (i = 0; i < qcvm->numknownstrings; i++)
	{
		if (qcvm->knownstrings[i] == s)
			return -1 - i;
	}
	// new unknown engine string
	// Con_DPrintf ("PR_SetEngineString: new engine string %p\n", s);
	for (i = qcvm->freeknownstrings;; i++)
	{
		if (i < qcvm->numknownstrings)
		{
			if (qcvm->knownstrings[i])
				continue;
		}
		else
		{
			if (i >= qcvm->maxknownstrings)
				PR_AllocStringSlots ();
			qcvm->numknownstrings++;
		}
		break;
	}
	qcvm->freeknownstrings = i + 1;
	qcvm->knownstrings[i] = s;
	qcvm->knownstringsowned[i] = false;
	return -1 - i;
}

int PR_AllocString (int size, char **ptr)
{
	int i;

	if (!size)
		return 0;

	for (i = qcvm->freeknownstrings;; i++)
	{
		if (i < qcvm->numknownstrings)
		{
			if (qcvm->knownstrings[i])
				continue;
		}
		else
		{
			if (i >= qcvm->maxknownstrings)
				PR_AllocStringSlots ();
			qcvm->numknownstrings++;
		}
		break;
	}
	qcvm->freeknownstrings = i + 1;
	qcvm->knownstrings[i] = (char *)Mem_Alloc (size);
	qcvm->knownstringsowned[i] = true;
	if (ptr)
		*ptr = (char *)qcvm->knownstrings[i];
	return -1 - i;
}

void PR_ClearEdictStrings ()
{
	for (int i = qcvm->progsstrings; i < qcvm->numknownstrings; ++i)
		if (qcvm->knownstringsowned[i])
		{
			SAFE_FREE (qcvm->knownstrings[i]);
			qcvm->knownstringsowned[i] = false;
		}

#ifndef _DEBUG
	// do not reuse slots in debug builds to help catch stale references
	qcvm->freeknownstrings = qcvm->progsstrings;
#endif
}
