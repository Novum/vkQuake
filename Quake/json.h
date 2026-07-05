/*
Copyright (C) 2022 A. Drexler

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

#ifndef _JSON_H_
#define _JSON_H_

typedef enum jsontype_t
{
	JSON_INVALID,
	JSON_OBJECT,
	JSON_ARRAY,
	JSON_STRING,
	JSON_NUMBER,
	JSON_BOOLEAN,
	JSON_NULL,
} jsontype_t;

typedef struct jsonentry_s jsonentry_t;

struct jsonentry_s
{
	union
	{
		const char *string;
		double		number;
		qboolean	boolean;
	};
	jsontype_t	 type;
	jsonentry_t *parent;
	jsonentry_t *firstchild;
	jsonentry_t *lastchild;
	jsonentry_t *next;
};

typedef struct json_s
{
	int			 numentries;
	jsonentry_t *root;
	const char	*strings;
} json_t;

json_t			  *JSON_Parse (const char *text);
void			   JSON_Free (json_t *json);
const jsonentry_t *JSON_Find (const jsonentry_t *entry, const char *name, jsontype_t type);
const char		  *JSON_FindString (const jsonentry_t *entry, const char *name);
const double	  *JSON_FindNumber (const jsonentry_t *entry, const char *name);
const qboolean	  *JSON_FindBoolean (const jsonentry_t *entry, const char *name);

#endif /* _JSON_H_ */
