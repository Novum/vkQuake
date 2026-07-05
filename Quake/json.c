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

#include "quakedef.h"
#include "json.h"
#include "json.h"

// https://github.com/zserge/jsmn
#define JSMN_PARENT_LINKS
#define JSMN_STATIC
#include "jsmn.h"

/*
==================
JSON_ReadHexDigit
==================
*/
static int JSON_ReadHexDigit (const char *str)
{
	if ((unsigned int)(*str - '0') < 10)
		return *str - '0';
	if ((unsigned int)(*str - 'a') < 6)
		return *str + (10 - 'a');
	if ((unsigned int)(*str - 'A') < 6)
		return *str + (10 - 'A');
	return -1;
}

/*
==================
JSON_ReadHexNumber
==================
*/
static qboolean JSON_ReadHexNumber (const char *str, int *out)
{
	int d0 = JSON_ReadHexDigit (str + 0);
	int d1 = JSON_ReadHexDigit (str + 1);
	int d2 = JSON_ReadHexDigit (str + 2);
	int d3 = JSON_ReadHexDigit (str + 3);
	if (d0 < 0 || d1 < 0 || d2 < 0 || d3 < 0)
		return false;
	*out = (d0 << 12) | (d1 << 8) | (d2 << 4) | d3;
	return true;
}

/*
==================
JSON_Unescape
==================
*/
static char *JSON_Unescape (char *dst, const char *src, int len)
{
	const char *srcend;

	for (srcend = src + len; src != srcend; src++)
	{
		if (*src != '\\')
		{
			*dst++ = *src;
			continue;
		}

		if (++src == srcend)
		{
			*dst++ = '\\';
			break;
		}

		switch (*src)
		{
		case '\"':
			*dst++ = '\"';
			break;
		case '\\':
			*dst++ = '\\';
			break;
		case 'b':
			*dst++ = '\b';
			break;
		case 'f':
			*dst++ = '\f';
			break;
		case 'n':
			*dst++ = '\n';
			break;
		case 'r':
			*dst++ = '\r';
			break;
		case 't':
			*dst++ = '\t';
			break;

		case 'u':
			if (srcend - src > 4)
			{
				int codepoint, lowsurrogate;
				if (JSON_ReadHexNumber (src + 1, &codepoint))
				{
					if (codepoint >= 0xd800 && codepoint < 0xdbff && srcend - src > 10 && src[5] == '\\' && src[6] == 'u' &&
						JSON_ReadHexNumber (src + 7, &lowsurrogate) && lowsurrogate >= 0xdc00 && lowsurrogate < 0xdfff)
					{
						codepoint -= 0xd800;
						lowsurrogate -= 0xdc00;
						codepoint = 0x10000 + (codepoint << 10) + lowsurrogate;
						src += 6;
					}
					dst += UTF8_WriteCodePoint (dst, 4, codepoint);
					src += 4;
					continue;
				}
			}
			// fall-through

		default:
			*dst++ = '\\';
			*dst++ = *src;
			break;
		}
	}

	*dst++ = '\0';

	return dst;
}

/*
==================
JSON_Parse
==================
*/
json_t *JSON_Parse (const char *text)
{
	json_t		*json;
	jsmn_parser	 parser;
	jsmntok_t	*tokens;
	jsonentry_t *entries;
	char		*strings;
	int			 i, len, numtokens;

	if (!text)
		return NULL;

	// fail if text starts with UTF-16 byte order mark
	if ((byte)text[0] == 0xFF && (byte)text[1] == 0xFE) // little-endian
		return NULL;
	if ((byte)text[0] == 0xFE && (byte)text[1] == 0xFF) // big-endian
		return NULL;

	// skip UTF-8 byte order mark
	if ((byte)text[0] == 0xEF && (byte)text[1] == 0xBB && (byte)text[2] == 0xBF)
		text += 3;

	len = strlen (text);
	jsmn_init (&parser);
	numtokens = jsmn_parse (&parser, text, len, NULL, 0);
	if (numtokens <= 0)
		return NULL;

	tokens = (jsmntok_t *)Mem_Alloc (sizeof (*tokens) * numtokens);
	if (!tokens)
		return NULL;

	jsmn_init (&parser);
	i = jsmn_parse (&parser, text, len, tokens, numtokens);
	if (i != numtokens)
	{
	free_tokens:
		Mem_Free (tokens);
		return NULL;
	}

	for (i = 0, len = 1; i < numtokens; i++)
		if (tokens[i].type == JSMN_STRING)
			len += tokens[i].end - tokens[i].start + 1;

	json = (json_t *)Mem_Alloc (sizeof (json_t) + sizeof (jsonentry_t) * numtokens + len);
	if (!json)
		goto free_tokens;
	entries = (jsonentry_t *)(json + 1);
	strings = (char *)(entries + numtokens);
	json->numentries = numtokens;
	json->root = entries;
	json->strings = strings;

	for (i = 0; i < numtokens; i++)
	{
		if (tokens[i].parent >= 0 && tokens[i].parent < i)
		{
			jsonentry_t *parent = &entries[tokens[i].parent];
			if (!parent->firstchild)
				parent->firstchild = &entries[i];
			else
				parent->lastchild->next = &entries[i];
			parent->lastchild = &entries[i];
		}

		if (tokens[i].type == JSMN_STRING)
		{
			len = tokens[i].end - tokens[i].start;
			entries[i].string = strings;
			strings = JSON_Unescape (strings, text + tokens[i].start, len);
		}

		if (tokens[i].type == JSMN_OBJECT)
			entries[i].type = JSON_OBJECT;
		else if (tokens[i].type == JSMN_ARRAY)
			entries[i].type = JSON_ARRAY;
		else if (tokens[i].type == JSMN_STRING)
			entries[i].type = JSON_STRING;
		else if (tokens[i].type == JSMN_PRIMITIVE)
		{
			const char *str = text + tokens[i].start;
			len = tokens[i].end - tokens[i].start;
			if (len > 0)
			{
				if (*str == 't')
				{
					entries[i].type = JSON_BOOLEAN;
					entries[i].boolean = true;
				}
				else if (*str == 'f')
				{
					entries[i].type = JSON_BOOLEAN;
					entries[i].boolean = false;
				}
				else if (*str == 'n')
				{
					entries[i].type = JSON_NULL;
					entries[i].boolean = false;
				}
				else
				{
					entries[i].type = JSON_NUMBER;
					entries[i].number = strtod (str, NULL);
				}
			}
		}
	}
	*strings++ = '\0';

	Mem_Free (tokens);

	return json;
}

/*
==================
JSON_Free
==================
*/
void JSON_Free (json_t *json)
{
	Mem_Free (json);
}

/*
==================
JSON_Find
==================
*/
const jsonentry_t *JSON_Find (const jsonentry_t *entry, const char *name, jsontype_t type)
{
	if (!entry)
		return NULL;
	for (entry = entry->firstchild; entry; entry = entry->next)
	{
		if (!entry->string || !entry->firstchild)
			continue;
		if (strcmp (entry->string, name) != 0)
			continue;
		if (entry->firstchild->type != type)
			return NULL;
		return entry->firstchild;
	}
	return NULL;
}

/*
==================
JSON_FindString
==================
*/
const char *JSON_FindString (const jsonentry_t *entry, const char *name)
{
	entry = JSON_Find (entry, name, JSON_STRING);
	return entry ? entry->string : NULL;
}

/*
==================
JSON_FindNumber
==================
*/
const double *JSON_FindNumber (const jsonentry_t *entry, const char *name)
{
	entry = JSON_Find (entry, name, JSON_NUMBER);
	return entry ? &entry->number : NULL;
}

/*
==================
JSON_FindBoolean
==================
*/
const qboolean *JSON_FindBoolean (const jsonentry_t *entry, const char *name)
{
	entry = JSON_Find (entry, name, JSON_BOOLEAN);
	return entry ? &entry->boolean : NULL;
}
