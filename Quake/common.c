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

// common.c -- misc functions used in client and server

#include "quakedef.h"
#include "q_ctype.h"
#include <errno.h>

#include "miniz.h"

static char *largv[MAX_NUM_ARGVS + 1];
static char	 argvdummy[] = " ";

int safemode;

cvar_t registered = {"registered", "1", CVAR_ROM};				 /* set to correct value in COM_CheckRegistered() */
cvar_t cmdline = {"cmdline", "", CVAR_ROM /*|CVAR_SERVERINFO*/}; /* sending cmdline upon CCREQ_RULE_INFO is evil */

static qboolean com_modified; // set true if using non-id files

qboolean fitzmode;
qboolean multiuser;

static void COM_Path_f (void);

// if a packfile directory differs from this, it is assumed to be hacked
#define PAK0_COUNT		339	  /* id1/pak0.pak - v1.0x */
#define PAK0_CRC_V100	13900 /* id1/pak0.pak - v1.00 */
#define PAK0_CRC_V101	62751 /* id1/pak0.pak - v1.01 */
#define PAK0_CRC_V106	32981 /* id1/pak0.pak - v1.06 */
#define PAK0_CRC		(PAK0_CRC_V106)
#define PAK0_COUNT_V091 308	  /* id1/pak0.pak - v0.91/0.92, not supported */
#define PAK0_CRC_V091	28804 /* id1/pak0.pak - v0.91/0.92, not supported */

THREAD_LOCAL char com_token[1024];
int				  com_argc;
char			**com_argv;

#define CMDLINE_LENGTH 256 /* johnfitz -- mirrored in cmd.c */
char com_cmdline[CMDLINE_LENGTH];

qboolean standard_quake = true, rogue, hipnotic;

extern const unsigned char vkquake_pak[];
extern const int		   vkquake_pak_size;
extern const int		   vkquake_pak_decompressed_size;

/*

All of Quake's data access is through a hierchal file system, but the contents
of the file system can be transparently merged from several sources.

The "base directory" is the path to the directory holding the quake.exe and all
game directories.  The sys_* files pass this to host_init in quakeparms_t->basedir.
This can be overridden with the "-basedir" command line parm to allow code
debugging in a different directory.  The base directory is only used during
filesystem initialization.

The "game directory" is the first tree on the search path and directory that all
generated files (savegames, screenshots, demos, config files) will be saved to.
This can be overridden with the "-game" command line parameter.  The game
directory can never be changed while quake is executing.  This is a precacution
against having a malicious server instruct clients to write files over areas they
shouldn't.

The "cache directory" is only used during development to save network bandwidth,
especially over ISDN / T1 lines.  If there is a cache directory specified, when
a file is found by the normal search path, it will be mirrored into the cache
directory, then opened there.

FIXME:
The file "parms.txt" will be read out of the game directory and appended to the
current command line arguments to allow different games to initialize startup
parms differently.  This could be used to add a "-sspeed 22050" for the high
quality sound edition.  Because they are added at the end, they will not
override an explicit setting on the original command line.

*/

//============================================================================

// ClearLink is used for new headnodes
void ClearLink (link_t *l)
{
	l->prev = l->next = l;
}

void RemoveLink (link_t *l)
{
	l->next->prev = l->prev;
	l->prev->next = l->next;
}

void InsertLinkBefore (link_t *l, link_t *before)
{
	l->next = before;
	l->prev = before->prev;
	l->prev->next = l;
	l->next->prev = l;
}

void InsertLinkAfter (link_t *l, link_t *after)
{
	l->next = after->next;
	l->prev = after;
	l->prev->next = l;
	l->next->prev = l;
}

/*
============================================================================

					LIBRARY REPLACEMENT FUNCTIONS

============================================================================
*/

int q_strcasecmp (const char *s1, const char *s2)
{
	const char *p1 = s1;
	const char *p2 = s2;
	char		c1, c2;

	if (p1 == p2)
		return 0;

	do
	{
		c1 = q_tolower (*p1++);
		c2 = q_tolower (*p2++);
		if (c1 == '\0')
			break;
	} while (c1 == c2);

	return (int)(c1 - c2);
}

int q_strncasecmp (const char *s1, const char *s2, size_t n)
{
	const char *p1 = s1;
	const char *p2 = s2;
	char		c1, c2;

	if (p1 == p2 || n == 0)
		return 0;

	do
	{
		c1 = q_tolower (*p1++);
		c2 = q_tolower (*p2++);
		if (c1 == '\0' || c1 != c2)
			break;
	} while (--n > 0);

	return (int)(c1 - c2);
}

// spike -- grabbed this from fte, because its useful to me
char *q_strcasestr (const char *haystack, const char *needle)
{
	int c1, c2, c2f;
	int i;
	c2f = *needle;
	if (c2f >= 'a' && c2f <= 'z')
		c2f -= ('a' - 'A');
	if (!c2f)
		return (char *)haystack;
	while (1)
	{
		c1 = *haystack;
		if (!c1)
			return NULL;
		if (c1 >= 'a' && c1 <= 'z')
			c1 -= ('a' - 'A');
		if (c1 == c2f)
		{
			for (i = 1;; i++)
			{
				c1 = haystack[i];
				c2 = needle[i];
				if (c1 >= 'a' && c1 <= 'z')
					c1 -= ('a' - 'A');
				if (c2 >= 'a' && c2 <= 'z')
					c2 -= ('a' - 'A');
				if (!c2)
					return (char *)haystack; // end of needle means we found a complete match
				if (!c1)					 // end of haystack means we can't possibly find needle in it any more
					return NULL;
				if (c1 != c2) // mismatch means no match starting at haystack[0]
					break;
			}
		}
		haystack++;
	}
	return NULL; // didn't find it
}

char *q_strlwr (char *str)
{
	char *c;
	c = str;
	while (*c)
	{
		*c = q_tolower (*c);
		c++;
	}
	return str;
}

char *q_strupr (char *str)
{
	char *c;
	c = str;
	while (*c)
	{
		*c = q_toupper (*c);
		c++;
	}
	return str;
}

char *q_strdup (const char *str)
{
	size_t len = strlen (str) + 1;
	char  *newstr = (char *)Mem_Alloc (len);
	memcpy (newstr, str, len);
	return newstr;
}

/* platform dependant (v)snprintf function names: */
#if defined(_WIN32)
#define snprintf_func  _snprintf
#define vsnprintf_func _vsnprintf
#else
#define snprintf_func  snprintf
#define vsnprintf_func vsnprintf
#endif

int q_vsnprintf (char *str, size_t size, const char *format, va_list args)
{
	int ret;

	ret = vsnprintf_func (str, size, format, args);

	if (ret < 0)
		ret = (int)size;
	if (size == 0) /* no buffer */
		return ret;
	if ((size_t)ret >= size)
		str[size - 1] = '\0';

	return ret;
}

int q_snprintf (char *str, size_t size, const char *format, ...)
{
	int		ret;
	va_list argptr;

	va_start (argptr, format);
	ret = q_vsnprintf (str, size, format, argptr);
	va_end (argptr);

	return ret;
}

int wildcmp (const char *wild, const char *string)
{ // case-insensitive string compare with wildcards. returns true for a match.
	while (*string)
	{
		if (*wild == '*')
		{
			if (*string == '/' || *string == '\\')
			{
				//* terminates if we get a match on the char following it, or if its a \ or / char
				wild++;
				continue;
			}
			if (wildcmp (wild + 1, string))
				return true;
			string++;
		}
		else if ((q_tolower (*wild) == q_tolower (*string)) || (*wild == '?'))
		{
			// this char matches
			wild++;
			string++;
		}
		else
		{
			// failure
			return false;
		}
	}

	while (*wild == '*')
	{
		wild++;
	}
	return !*wild;
}

void Info_RemoveKey (char *info, const char *key)
{ // only shrinks, so no need for max size.
	size_t keylen = strlen (key);

	while (*info)
	{
		char *l = info;
		if (*info++ != '\\')
			break; // error / end-of-string

		if (!strncmp (info, key, keylen) && info[keylen] == '\\')
		{
			// skip the key name
			info += keylen + 1;
			// this is the old value for the key. skip over it
			while (*info && *info != '\\')
				info++;

			// okay, we found it. strip it out now.
			memmove (l, info, strlen (info) + 1);
			return;
		}
		else
		{
			// skip the key
			while (*info && *info != '\\')
				info++;

			// validate that its a value now
			if (*info++ != '\\')
				break; // error
			// skip the value
			while (*info && *info != '\\')
				info++;
		}
	}
}
void Info_SetKey (char *info, size_t infosize, const char *key, const char *val)
{
	size_t keylen = strlen (key);
	size_t vallen = strlen (val);

	Info_RemoveKey (info, key);

	if (vallen)
	{
		char *o = info + strlen (info);
		char *e = info + infosize - 1;

		if (!*key || strchr (key, '\\') || strchr (val, '\\'))
			Con_Warning ("Info_SetKey(%s): invalid key/value\n", key);
		else if (o + 2 + keylen + vallen >= e)
			Con_Warning ("Info_SetKey(%s): length exceeds max\n", key);
		else
		{
			*o++ = '\\';
			memcpy (o, key, keylen);
			o += keylen;
			*o++ = '\\';
			memcpy (o, val, vallen);
			o += vallen;

			*o = 0;
		}
	}
}
const char *Info_GetKey (const char *info, const char *key, char *out, size_t outsize)
{
	const char *r = out;
	size_t		keylen = strlen (key);

	outsize--;

	while (*info)
	{
		if (*info++ != '\\')
			break; // error / end-of-string

		if (!strncmp (info, key, keylen) && info[keylen] == '\\')
		{
			// skip the key name
			info += keylen + 1;
			// this is the value for the key. copy it out
			while (*info && *info != '\\' && outsize-- > 0)
				*out++ = *info++;
			break;
		}
		else
		{
			// skip the key
			while (*info && *info != '\\')
				info++;

			// validate that its a value now
			if (*info++ != '\\')
				break; // error
			// skip the value
			while (*info && *info != '\\')
				info++;
		}
	}
	*out = 0;
	return r;
}

void Info_Enumerate (const char *info, void (*cb) (void *ctx, const char *key, const char *value), void *cbctx)
{
	char   key[1024];
	char   val[1024];
	size_t kl, vl;
	while (*info)
	{
		kl = vl = 0;
		if (*info++ != '\\')
			break; // error / end-of-string

		// skip the key
		while (*info && *info != '\\')
		{
			if (kl < sizeof (key) - 1)
				key[kl++] = *info;
			info++;
		}

		// validate that its a value now
		if (*info++ != '\\')
			break; // error
		// skip the value
		while (*info && *info != '\\')
		{
			if (vl < sizeof (val) - 1)
				val[vl++] = *info;
			info++;
		}

		key[kl] = 0;
		val[vl] = 0;
		cb (cbctx, key, val);
	}
}
static void Info_Print_Callback (void *ctx, const char *key, const char *val)
{
	Con_Printf ("%20s: %s\n", key, val);
}
void Info_Print (const char *info)
{
	Info_Enumerate (info, Info_Print_Callback, NULL);
}
/*
============================================================================

					BYTE ORDER FUNCTIONS

============================================================================
*/

short ShortSwap (short l)
{
	byte b1, b2;

	b1 = l & 255;
	b2 = (l >> 8) & 255;

	return ((unsigned short)b1 << 8) + b2;
}

short ShortNoSwap (short l)
{
	return l;
}

int LongSwap (int l)
{
	byte b1, b2, b3, b4;

	b1 = l & 255;
	b2 = (l >> 8) & 255;
	b3 = (l >> 16) & 255;
	b4 = (l >> 24) & 255;

	return ((unsigned int)b1 << 24) + ((unsigned int)b2 << 16) + ((unsigned int)b3 << 8) + b4;
}

int LongNoSwap (int l)
{
	return l;
}

float FloatSwap (float f)
{
	union
	{
		float f;
		byte  b[4];
	} dat1, dat2;

	dat1.f = f;
	dat2.b[0] = dat1.b[3];
	dat2.b[1] = dat1.b[2];
	dat2.b[2] = dat1.b[1];
	dat2.b[3] = dat1.b[0];
	return dat2.f;
}

float FloatNoSwap (float f)
{
	return f;
}

short (*BigShort) (short l) = ShortSwap;
short (*LittleShort) (short l) = ShortNoSwap;
int (*BigLong) (int l) = LongSwap;
int (*LittleLong) (int l) = LongNoSwap;
float (*BigFloat) (float l) = FloatSwap;
float (*LittleFloat) (float l) = FloatNoSwap;

/*
==============================================================================

			MESSAGE IO FUNCTIONS

Handles byte ordering and avoids alignment errors
==============================================================================
*/

//
// writing functions
//

void MSG_WriteChar (sizebuf_t *sb, int c)
{
	byte *buf;

#ifdef PARANOID
	if (c < -128 || c > 127)
		Sys_Error ("MSG_WriteChar: range error");
#endif

	buf = (byte *)SZ_GetSpace (sb, 1);
	buf[0] = c;
}

void MSG_WriteByte (sizebuf_t *sb, int c)
{
	byte *buf;

#ifdef PARANOID
	if (c < 0 || c > 255)
		Sys_Error ("MSG_WriteByte: range error");
#endif

	buf = (byte *)SZ_GetSpace (sb, 1);
	buf[0] = c;
}

void MSG_WriteShort (sizebuf_t *sb, int c)
{
	byte *buf;

#ifdef PARANOID
	if (c < ((short)0x8000) || c > (short)0x7fff)
		Sys_Error ("MSG_WriteShort: range error");
#endif

	buf = (byte *)SZ_GetSpace (sb, 2);
	buf[0] = c & 0xff;
	buf[1] = c >> 8;
}

void MSG_WriteLong (sizebuf_t *sb, int c)
{
	byte *buf;

	buf = (byte *)SZ_GetSpace (sb, 4);
	buf[0] = c & 0xff;
	buf[1] = (c >> 8) & 0xff;
	buf[2] = (c >> 16) & 0xff;
	buf[3] = c >> 24;
}

void MSG_WriteFloat (sizebuf_t *sb, float f)
{
	union
	{
		float f;
		int	  l;
	} dat;

	dat.f = f;
	dat.l = LittleLong (dat.l);

	SZ_Write (sb, &dat.l, 4);
}

void MSG_WriteString (sizebuf_t *sb, const char *s)
{
	if (!s)
		SZ_Write (sb, "", 1);
	else
		SZ_Write (sb, s, strlen (s) + 1);
}
void MSG_WriteStringUnterminated (sizebuf_t *sb, const char *s)
{
	SZ_Write (sb, s, strlen (s));
}

// johnfitz -- original behavior, 13.3 fixed point coords, max range +-4096
void MSG_WriteCoord16 (sizebuf_t *sb, float f)
{
	MSG_WriteShort (sb, Q_rint (f * 8));
}

// johnfitz -- 16.8 fixed point coords, max range +-32768
void MSG_WriteCoord24 (sizebuf_t *sb, float f)
{
	MSG_WriteShort (sb, f);
	MSG_WriteByte (sb, (int)(f * 255) % 255);
}

// johnfitz -- 32-bit float coords
void MSG_WriteCoord32f (sizebuf_t *sb, float f)
{
	MSG_WriteFloat (sb, f);
}

void MSG_WriteCoord (sizebuf_t *sb, float f, unsigned int flags)
{
	if (flags & PRFL_FLOATCOORD)
		MSG_WriteFloat (sb, f);
	else if (flags & PRFL_INT32COORD)
		MSG_WriteLong (sb, Q_rint (f * 16));
	else if (flags & PRFL_24BITCOORD)
		MSG_WriteCoord24 (sb, f);
	else
		MSG_WriteCoord16 (sb, f);
}

void MSG_WriteAngle (sizebuf_t *sb, float f, unsigned int flags)
{
	if (flags & PRFL_FLOATANGLE)
		MSG_WriteFloat (sb, f);
	else if (flags & PRFL_SHORTANGLE)
		MSG_WriteShort (sb, Q_rint (f * 65536.0 / 360.0) & 65535);
	else
		MSG_WriteByte (sb, Q_rint (f * 256.0 / 360.0) & 255); // johnfitz -- use Q_rint instead of (int)	}
}

// johnfitz -- for PROTOCOL_FITZQUAKE
void MSG_WriteAngle16 (sizebuf_t *sb, float f, unsigned int flags)
{
	if (flags & PRFL_FLOATANGLE)
		MSG_WriteFloat (sb, f);
	else
		MSG_WriteShort (sb, Q_rint (f * 65536.0 / 360.0) & 65535);
}
// johnfitz

// spike -- for PEXT2_REPLACEMENTDELTAS
void MSG_WriteEntity (sizebuf_t *sb, unsigned int entnum, unsigned int pext2)
{
	// high short, low byte
	if (entnum > 0x7fff && (pext2 & PEXT2_REPLACEMENTDELTAS))
	{
		MSG_WriteShort (sb, 0x8000 | (entnum >> 8));
		MSG_WriteByte (sb, entnum & 0xff);
	}
	else
		MSG_WriteShort (sb, entnum);
}

//
// reading functions
//
int		 msg_readcount;
qboolean msg_badread;

void MSG_BeginReading (void)
{
	msg_readcount = 0;
	msg_badread = false;
}

// returns -1 and sets msg_badread if no more characters are available
int MSG_ReadChar (void)
{
	int c;

	if (msg_readcount + 1 > net_message.cursize)
	{
		msg_badread = true;
		return -1;
	}

	c = (signed char)net_message.data[msg_readcount];
	msg_readcount++;

	return c;
}

int MSG_ReadByte (void)
{
	int c;

	if (msg_readcount + 1 > net_message.cursize)
	{
		msg_badread = true;
		return -1;
	}

	c = (unsigned char)net_message.data[msg_readcount];
	msg_readcount++;

	return c;
}

int MSG_ReadShort (void)
{
	int c;

	if (msg_readcount + 2 > net_message.cursize)
	{
		msg_badread = true;
		return -1;
	}

	c = (short)(net_message.data[msg_readcount] + (net_message.data[msg_readcount + 1] << 8));

	msg_readcount += 2;

	return c;
}

int MSG_ReadLong (void)
{
	uint32_t c;

	if (msg_readcount + 4 > net_message.cursize)
	{
		msg_badread = true;
		return -1;
	}

	c = (uint32_t)net_message.data[msg_readcount] + ((uint32_t)(net_message.data[msg_readcount + 1]) << 8) +
		((uint32_t)(net_message.data[msg_readcount + 2]) << 16) + ((uint32_t)(net_message.data[msg_readcount + 3]) << 24);

	msg_readcount += 4;

	return c;
}

float MSG_ReadFloat (void)
{
	union
	{
		byte  b[4];
		float f;
		int	  l;
	} dat;

	dat.b[0] = net_message.data[msg_readcount];
	dat.b[1] = net_message.data[msg_readcount + 1];
	dat.b[2] = net_message.data[msg_readcount + 2];
	dat.b[3] = net_message.data[msg_readcount + 3];
	msg_readcount += 4;

	dat.l = LittleLong (dat.l);

	return dat.f;
}

const char *MSG_ReadString (void)
{
	static char string[2048];
	int			c;
	size_t		l;

	l = 0;
	do
	{
		c = MSG_ReadByte ();
		if (c == -1 || c == 0)
			break;
		string[l] = c;
		l++;
	} while (l < sizeof (string) - 1);

	string[l] = 0;

	return string;
}

// johnfitz -- original behavior, 13.3 fixed point coords, max range +-4096
float MSG_ReadCoord16 (void)
{
	return MSG_ReadShort () * (1.0 / 8);
}

// johnfitz -- 16.8 fixed point coords, max range +-32768
float MSG_ReadCoord24 (void)
{
	return MSG_ReadShort () + MSG_ReadByte () * (1.0 / 255);
}

// johnfitz -- 32-bit float coords
float MSG_ReadCoord32f (void)
{
	return MSG_ReadFloat ();
}

float MSG_ReadCoord (unsigned int flags)
{
	if (flags & PRFL_FLOATCOORD)
		return MSG_ReadFloat ();
	else if (flags & PRFL_INT32COORD)
		return MSG_ReadLong () * (1.0 / 16.0);
	else if (flags & PRFL_24BITCOORD)
		return MSG_ReadCoord24 ();
	else
		return MSG_ReadCoord16 ();
}

float MSG_ReadAngle (unsigned int flags)
{
	if (flags & PRFL_FLOATANGLE)
		return MSG_ReadFloat ();
	else if (flags & PRFL_SHORTANGLE)
		return MSG_ReadShort () * (360.0 / 65536);
	else
		return MSG_ReadChar () * (360.0 / 256);
}

// johnfitz -- for PROTOCOL_FITZQUAKE
float MSG_ReadAngle16 (unsigned int flags)
{
	if (flags & PRFL_FLOATANGLE)
		return MSG_ReadFloat (); // make sure
	else
		return MSG_ReadShort () * (360.0 / 65536);
}
// johnfitz

unsigned int MSG_ReadEntity (unsigned int pext2)
{
	unsigned int e = (unsigned short)MSG_ReadShort ();
	if (pext2 & PEXT2_REPLACEMENTDELTAS)
	{
		if (e & 0x8000)
		{
			e = (e & 0x7fff) << 8;
			e |= MSG_ReadByte ();
		}
	}
	return e;
}

//===========================================================================

void SZ_Alloc (sizebuf_t *buf, int startsize)
{
	if (startsize < 256)
		startsize = 256;
	buf->data = (byte *)Mem_Alloc (startsize);
	buf->maxsize = startsize;
	buf->cursize = 0;
}

void SZ_Free (sizebuf_t *buf)
{
	Mem_Free (buf->data);
	buf->data = NULL;
	buf->maxsize = 0;
	buf->cursize = 0;
}

void SZ_Clear (sizebuf_t *buf)
{
	buf->cursize = 0;
	buf->overflowed = false;
}

void *SZ_GetSpace (sizebuf_t *buf, int length)
{
	void *data;

	if (buf->cursize + length > buf->maxsize)
	{
		if (!buf->allowoverflow)
			Host_Error ("SZ_GetSpace: overflow without allowoverflow set"); // ericw -- made Host_Error to be less annoying

		if (length > buf->maxsize)
			Sys_Error ("SZ_GetSpace: %i is > full buffer size", length);

		Con_Printf ("SZ_GetSpace: overflow\n");
		SZ_Clear (buf);
		buf->overflowed = true;
	}

	data = buf->data + buf->cursize;
	buf->cursize += length;

	return data;
}

void SZ_Write (sizebuf_t *buf, const void *data, int length)
{
	memcpy (SZ_GetSpace (buf, length), data, length);
}

void SZ_Print (sizebuf_t *buf, const char *data)
{
	int len = strlen (data) + 1;

	if (buf->data[buf->cursize - 1])
	{ /* no trailing 0 */
		memcpy ((byte *)SZ_GetSpace (buf, len), data, len);
	}
	else
	{ /* write over trailing 0 */
		memcpy ((byte *)SZ_GetSpace (buf, len - 1) - 1, data, len);
	}
}

//============================================================================

/*
============
COM_SkipPath
============
*/
const char *COM_SkipPath (const char *pathname)
{
	const char *last;

	last = pathname;
	while (*pathname)
	{
		if (*pathname == '/')
			last = pathname + 1;
		pathname++;
	}
	return last;
}

/*
============
COM_StripExtension
============
*/
void COM_StripExtension (const char *in, char *out, size_t outsize)
{
	int length;

	if (!*in)
	{
		*out = '\0';
		return;
	}
	if (in != out) /* copy when not in-place editing */
		q_strlcpy (out, in, outsize);
	length = (int)strlen (out) - 1;
	while (length > 0 && out[length] != '.')
	{
		--length;
		if (out[length] == '/' || out[length] == '\\')
			return; /* no extension */
	}
	if (length > 0)
		out[length] = '\0';
}

/*
============
COM_FileGetExtension - doesn't return NULL
============
*/
const char *COM_FileGetExtension (const char *in)
{
	const char *src;
	size_t		len;

	len = strlen (in);
	if (len < 2) /* nothing meaningful */
		return "";

	src = in + len - 1;
	while (src != in && src[-1] != '.')
		src--;
	if (src == in || strchr (src, '/') != NULL || strchr (src, '\\') != NULL)
		return ""; /* no extension, or parent directory has a dot */

	return src;
}

/*
============
COM_ExtractExtension
============
*/
void COM_ExtractExtension (const char *in, char *out, size_t outsize)
{
	const char *ext = COM_FileGetExtension (in);
	if (!*ext)
		*out = '\0';
	else
		q_strlcpy (out, ext, outsize);
}

/*
============
COM_FileBase
take 'somedir/otherdir/filename.ext',
write only 'filename' to the output
============
*/
void COM_FileBase (const char *in, char *out, size_t outsize)
{
	const char *dot, *slash, *s;

	s = in;
	slash = in;
	dot = NULL;
	while (*s)
	{
		if (*s == '/')
			slash = s + 1;
		if (*s == '.')
			dot = s;
		s++;
	}
	if (dot == NULL)
		dot = s;

	if (dot - slash < 2)
		q_strlcpy (out, "?model?", outsize);
	else
	{
		size_t len = dot - slash;
		if (len >= outsize)
			len = outsize - 1;
		memcpy (out, slash, len);
		out[len] = '\0';
	}
}

/*
==================
COM_DefaultExtension
if path doesn't have a .EXT, append extension
(extension should include the leading ".")
==================
*/
#if 0 /* can be dangerous */
void COM_DefaultExtension (char *path, const char *extension, size_t len)
{
	char	*src;

	if (!*path) return;
	src = path + strlen(path) - 1;

	while (*src != '/' && *src != '\\' && src != path)
	{
		if (*src == '.')
			return; // it has an extension
		src--;
	}

	q_strlcat(path, extension, len);
}
#endif

/*
==================
COM_AddExtension
if path extension doesn't match .EXT, append it
(extension should include the leading ".")
==================
*/
void COM_AddExtension (char *path, const char *extension, size_t len)
{
	if (strcmp (COM_FileGetExtension (path), extension + 1) != 0)
		q_strlcat (path, extension, len);
}

/*
==============
COM_Parse

Parse a token out of a string
==============
*/
const char *COM_Parse (const char *data)
{
	int c;
	int len;

	len = 0;
	com_token[0] = 0;

	if (!data)
		return NULL;

// skip whitespace
skipwhite:
	while ((c = *data) <= ' ')
	{
		if (c == 0)
			return NULL; // end of file
		data++;
	}

	// skip // comments
	if (c == '/' && data[1] == '/')
	{
		while (*data && *data != '\n')
			data++;
		goto skipwhite;
	}

	// skip /*..*/ comments
	if (c == '/' && data[1] == '*')
	{
		data += 2;
		while (*data && !(*data == '*' && data[1] == '/'))
			data++;
		if (*data)
			data += 2;
		goto skipwhite;
	}

	// handle quoted strings specially
	if (c == '\"')
	{
		data++;
		while (1)
		{
			if ((c = *data) != 0)
				++data;
			if (c == '\"' || !c)
			{
				com_token[len] = 0;
				return data;
			}
			com_token[len] = c;
			len++;
		}
	}

	// parse single characters
	if (c == '{' || c == '}' || c == '(' || c == ')' || c == '\'' || c == ':')
	{
		com_token[len] = c;
		len++;
		com_token[len] = 0;
		return data + 1;
	}

	// parse a regular word
	do
	{
		com_token[len] = c;
		data++;
		len++;
		c = *data;
		/* commented out the check for ':' so that ip:port works */
		if (c == '{' || c == '}' || c == '(' || c == ')' || c == '\'' /* || c == ':' */)
			break;
	} while (c > 32);

	com_token[len] = 0;
	return data;
}

/*
================
COM_CheckParm

Returns the position (1 to argc-1) in the program's argument list
where the given parameter apears, or 0 if not present
================
*/
int COM_CheckParmNext (int last, const char *parm)
{
	int i;

	for (i = last + 1; i < com_argc; i++)
	{
		if (!com_argv[i])
			continue; // NEXTSTEP sometimes clears appkit vars.
		if (!strcmp (parm, com_argv[i]))
			return i;
	}

	return 0;
}
int COM_CheckParm (const char *parm)
{
	return COM_CheckParmNext (0, parm);
}

/*
================
COM_CheckRegistered

Looks for the pop.txt file and verifies it.
Sets the "registered" cvar.
Immediately exits out if an alternate game was attempted to be started without
being registered.
================
*/
static void COM_CheckRegistered (void)
{
	int h;
	int i;

	COM_OpenFile ("gfx/pop.lmp", &h, NULL);

	if (h == -1)
	{
		Cvar_SetROM ("registered", "0");
		Con_Printf ("Playing shareware version.\n");
		if (com_modified)
			Sys_Error (
				"You must have the registered version to use modified games.\n\n"
				"Basedir is: %s\n\n"
				"Check that this has an " GAMENAME " subdirectory containing pak0.pak and pak1.pak, "
				"or use the -basedir command-line option to specify another directory.",
				com_basedir);
		return;
	}

	COM_CloseFile (h);

	for (i = 0; com_cmdline[i]; i++)
	{
		if (com_cmdline[i] != ' ')
			break;
	}

	Cvar_SetROM ("cmdline", &com_cmdline[i]);
	Cvar_SetROM ("registered", "1");
	Con_Printf ("Playing registered version.\n");
}

/*
================
COM_InitArgv
================
*/
void COM_InitArgv (int argc, char **argv)
{
	int i, j, n;

	// reconstitute the command line for the cmdline externally visible cvar
	n = 0;

	for (j = 0; (j < MAX_NUM_ARGVS) && (j < argc); j++)
	{
		i = 0;

		while ((n < (CMDLINE_LENGTH - 1)) && argv[j][i])
		{
			com_cmdline[n++] = argv[j][i++];
		}

		if (n < (CMDLINE_LENGTH - 1))
			com_cmdline[n++] = ' ';
		else
			break;
	}

	if (n > 0 && com_cmdline[n - 1] == ' ')
		com_cmdline[n - 1] = 0; // johnfitz -- kill the trailing space

	Con_Printf ("Command line: %s\n", com_cmdline);

	for (com_argc = 0; (com_argc < MAX_NUM_ARGVS) && (com_argc < argc); com_argc++)
	{
		largv[com_argc] = argv[com_argc];
		if (!strcmp ("-safe", argv[com_argc]))
			safemode = 1;
	}

	largv[com_argc] = argvdummy;
	com_argv = largv;

	if (COM_CheckParm ("-rogue"))
	{
		rogue = true;
		standard_quake = false;
	}

	if (COM_CheckParm ("-hipnotic") || COM_CheckParm ("-quoth")) // johnfitz -- "-quoth" support
	{
		hipnotic = true;
		standard_quake = false;
	}
}

entity_state_t nullentitystate;
static void	   COM_SetupNullState (void)
{
	// the null state has some specific default values
	//	nullentitystate.drawflags = /*SCALE_ORIGIN_ORIGIN*/96;
	nullentitystate.colormod[0] = 32;
	nullentitystate.colormod[1] = 32;
	nullentitystate.colormod[2] = 32;
	//	nullentitystate.glowmod[0] = 32;
	//	nullentitystate.glowmod[1] = 32;
	//	nullentitystate.glowmod[2] = 32;
	nullentitystate.colormap = 0;
	nullentitystate.alpha = ENTALPHA_DEFAULT; // fte has 255 by default, with 0 for invisible. fitz uses 1 for invisible, 0 default, and 255=full alpha
	nullentitystate.scale = ENTSCALE_DEFAULT;
	//	nullentitystate.solidsize = 0;//ES_SOLID_BSP;
	nullentitystate.solidsize = ES_SOLID_NOT;
}

/*
================
COM_Init
================
*/
void COM_Init (void)
{
	uint32_t uint_value = 0x12345678;
	uint8_t	 bytes[4];
	memcpy (bytes, &uint_value, sizeof (uint32_t));

	/*    U N I X */

	/*
	BE_ORDER:  12 34 56 78
		   U  N  I  X

	LE_ORDER:  78 56 34 12
		   X  I  N  U

	PDP_ORDER: 34 12 78 56
		   N  U  X  I
	*/
	if (bytes[0] != 0x78 || bytes[1] != 0x56 || bytes[2] != 0x34 || bytes[3] != 0x12)
		Sys_Error ("Unsupported endianism. Only little endian is supported");

	if (COM_CheckParm ("-fitz"))
		fitzmode = true;

	if (COM_CheckParm ("-validation"))
		vulkan_globals.validation = true;

	if (COM_CheckParm ("-multiuser"))
		multiuser = true;

	COM_SetupNullState ();
}

/*
============
va

does a varargs printf into a temp buffer. cycles between
4 different static buffers. the number of buffers cycled
is defined in VA_NUM_BUFFS.
FIXME: make this buffer size safe someday
============
*/
#define VA_NUM_BUFFS 4
#define VA_BUFFERLEN 1024

static char *get_va_buffer (void)
{
	static THREAD_LOCAL char va_buffers[VA_NUM_BUFFS][VA_BUFFERLEN];
	static THREAD_LOCAL int	 buffer_idx = 0;
	buffer_idx = (buffer_idx + 1) & (VA_NUM_BUFFS - 1);
	return va_buffers[buffer_idx];
}

char *va (const char *format, ...)
{
	va_list argptr;
	char   *va_buf;

	va_buf = get_va_buffer ();
	va_start (argptr, format);
	q_vsnprintf (va_buf, VA_BUFFERLEN, format, argptr);
	va_end (argptr);

	return va_buf;
}

/*
=============================================================================

QUAKE FILESYSTEM

=============================================================================
*/

THREAD_LOCAL int com_filesize;

//
// on-disk pakfile
//
typedef struct
{
	char name[56];
	int	 filepos, filelen;
} dpackfile_t;

typedef struct
{
	char id[4];
	int	 dirofs;
	int	 dirlen;
} dpackheader_t;

#define MAX_FILES_IN_PACK 2048

char			 com_gamenames[1024]; // eg: "hipnotic;quoth;warp" ... no id1
char			 com_gamedir[MAX_OSPATH];
char			 com_basedir[MAX_OSPATH];
THREAD_LOCAL int file_from_pak; // ZOID: global indicating that file came from a pak

searchpath_t *com_searchpaths;
searchpath_t *com_base_searchpaths;

/*
============
COM_Path_f
============
*/
static void COM_Path_f (void)
{
	searchpath_t *s;

	Con_Printf ("Current search path:\n");
	for (s = com_searchpaths; s; s = s->next)
	{
		if (s->pack)
		{
			Con_Printf ("%s (%i files)\n", s->pack->filename, s->pack->numfiles);
		}
		else
			Con_Printf ("%s\n", s->filename);
	}
}

/*
============
COM_WriteFile

The filename will be prefixed by the current game directory
============
*/
void COM_WriteFile (const char *filename, const void *data, int len)
{
	int	 handle;
	char name[MAX_OSPATH];

	Sys_mkdir (com_gamedir); // johnfitz -- if we've switched to a nonexistant gamedir, create it now so we don't crash

	q_snprintf (name, sizeof (name), "%s/%s", com_gamedir, filename);

	handle = Sys_FileOpenWrite (name);
	if (handle == -1)
	{
		Sys_Printf ("COM_WriteFile: failed on %s\n", name);
		return;
	}

	Sys_Printf ("COM_WriteFile: %s\n", name);
	Sys_FileWrite (handle, data, len);
	Sys_FileClose (handle);
}

/*
============
COM_CreatePath
============
*/
void COM_CreatePath (char *path)
{
	char *ofs;

	for (ofs = path + 1; *ofs; ofs++)
	{
		if (*ofs == '/')
		{ // create the directory
			*ofs = 0;
			Sys_mkdir (path);
			*ofs = '/';
		}
	}
}

/*
================
COM_filelength
================
*/
long COM_filelength (FILE *f)
{
	long pos, end;

	pos = ftell (f);
	fseek (f, 0, SEEK_END);
	end = ftell (f);
	fseek (f, pos, SEEK_SET);

	return end;
}

/*
===========
COM_FindFile

Finds the file in the search path.
Sets com_filesize and one of handle or file
If neither of file or handle is set, this
can be used for detecting a file's presence.
===========
*/
static int COM_FindFile (const char *filename, int *handle, FILE **file, unsigned int *path_id)
{
	searchpath_t *search;
	char		  netpath[MAX_OSPATH];
	pack_t		 *pak;
	int			  i;
	qboolean	  is_config = !q_strcasecmp (filename, "config.cfg"), found = false;

	if (file && handle)
		Sys_Error ("COM_FindFile: both handle and file set");

	file_from_pak = 0;

	//
	// search through the path, one element at a time
	//
	for (search = com_searchpaths; search; search = search->next)
	{
		if (search->pack) /* look through all the pak file elements */
		{
			pak = search->pack;
			for (i = 0; i < pak->numfiles; i++)
			{
				if (strcmp (pak->files[i].name, filename) != 0)
					continue;
				// found it!
				com_filesize = pak->files[i].filelen;
				file_from_pak = 1;
				if (path_id)
					*path_id = search->path_id;
				if (handle)
				{
					*handle = pak->handle;
					Sys_FileSeek (pak->handle, pak->files[i].filepos);
					return com_filesize;
				}
				else if (file)
				{ /* open a new file on the pakfile */
					*file = fopen (pak->filename, "rb");
					if (*file)
						fseek (*file, pak->files[i].filepos, SEEK_SET);
					return com_filesize;
				}
				else /* for COM_FileExists() */
				{
					return com_filesize;
				}
			}
		}
		else /* check a file in the directory tree */
		{
			if (!registered.value)
			{ /* if not a registered version, don't ever go beyond base */
				if (strchr (filename, '/') || strchr (filename, '\\'))
					continue;
			}

			if (is_config)
			{
				q_snprintf (netpath, sizeof (netpath), "%s/" CONFIG_NAME, search->filename);
				if (Sys_FileType (netpath) & FS_ENT_FILE)
					found = true;
			}

			if (!found)
			{
				q_snprintf (netpath, sizeof (netpath), "%s/%s", search->filename, filename);
				if (!(Sys_FileType (netpath) & FS_ENT_FILE))
					continue;
			}

			if (path_id)
				*path_id = search->path_id;
			if (handle)
			{
				com_filesize = Sys_FileOpenRead (netpath, &i);
				*handle = i;
				return com_filesize;
			}
			else if (file)
			{
				*file = fopen (netpath, "rb");
				com_filesize = (*file == NULL) ? -1 : COM_filelength (*file);
				return com_filesize;
			}
			else
			{
				return 0; /* dummy valid value for COM_FileExists() */
			}
		}
	}

	if (strcmp (COM_FileGetExtension (filename), "pcx") != 0 && strcmp (COM_FileGetExtension (filename), "tga") != 0 &&
		strcmp (COM_FileGetExtension (filename), "lit") != 0 && strcmp (COM_FileGetExtension (filename), "vis") != 0 &&
		strcmp (COM_FileGetExtension (filename), "ent") != 0)
		Con_DPrintf ("FindFile: can't find %s\n", filename);
	else
		Con_DPrintf2 ("FindFile: can't find %s\n", filename);

	if (handle)
		*handle = -1;
	if (file)
		*file = NULL;
	com_filesize = -1;
	return com_filesize;
}

/*
===========
COM_FileExists

Returns whether the file is found in the quake filesystem.
===========
*/
qboolean COM_FileExists (const char *filename, unsigned int *path_id)
{
	int ret = COM_FindFile (filename, NULL, NULL, path_id);
	return (ret == -1) ? false : true;
}

/*
===========
COM_OpenFile

filename never has a leading slash, but may contain directory walks
returns a handle and a length
it may actually be inside a pak file
===========
*/
int COM_OpenFile (const char *filename, int *handle, unsigned int *path_id)
{
	return COM_FindFile (filename, handle, NULL, path_id);
}

/*
===========
COM_FOpenFile

If the requested file is inside a packfile, a new FILE * will be opened
into the file.
===========
*/
int COM_FOpenFile (const char *filename, FILE **file, unsigned int *path_id)
{
	return COM_FindFile (filename, NULL, file, path_id);
}

/*
============
COM_CloseFile

If it is a pak file handle, don't really close it
============
*/
void COM_CloseFile (int h)
{
	searchpath_t *s;

	for (s = com_searchpaths; s; s = s->next)
		if (s->pack && s->pack->handle == h)
			return;

	Sys_FileClose (h);
}

/*
============
COM_LoadFile

Filename are reletive to the quake directory.
Allways appends a 0 byte.
============
*/
byte *COM_LoadFile (const char *path, unsigned int *path_id)
{
	int	  h;
	byte *buf;
	char  base[32];
	int	  len;

	buf = NULL; // quiet compiler warning

	// look for it in the filesystem or pack files
	len = COM_OpenFile (path, &h, path_id);
	if (h == -1)
		return NULL;

	// extract the filename base name for hunk tag
	COM_FileBase (path, base, sizeof (base));

	buf = (byte *)Mem_AllocNonZero (len + 1);

	if (!buf)
		Sys_Error ("COM_LoadFile: not enough space for %s", path);

	((byte *)buf)[len] = 0;

	Sys_FileRead (h, buf, len);
	COM_CloseFile (h);

	return buf;
}

byte *COM_LoadMallocFile_TextMode_OSPath (const char *path, long *len_out)
{
	FILE *f;
	byte *data;
	long  len, actuallen;

	// ericw -- this is used by Host_Loadgame_f. Translate CRLF to LF on load games,
	// othewise multiline messages have a garbage character at the end of each line.
	// TODO: could handle in a way that allows loading CRLF savegames on mac/linux
	// without the junk characters appearing.
	f = fopen (path, "rt");
	if (f == NULL)
		return NULL;

	len = COM_filelength (f);
	if (len < 0)
	{
		fclose (f);
		return NULL;
	}

	data = (byte *)Mem_AllocNonZero (len + 1);
	if (data == NULL)
	{
		fclose (f);
		return NULL;
	}

	// (actuallen < len) if CRLF to LF translation was performed
	actuallen = fread (data, 1, len, f);
	if (ferror (f))
	{
		fclose (f);
		Mem_Free (data);
		return NULL;
	}
	data[actuallen] = '\0';

	if (len_out != NULL)
		*len_out = actuallen;
	fclose (f);
	return data;
}

const char *COM_ParseIntNewline (const char *buffer, int *value)
{
	int consumed = 0;
	sscanf (buffer, "%i\n%n", value, &consumed);
	return buffer + consumed;
}

const char *COM_ParseFloatNewline (const char *buffer, float *value)
{
	int consumed = 0;
	sscanf (buffer, "%f\n%n", value, &consumed);
	return buffer + consumed;
}

const char *COM_ParseStringNewline (const char *buffer)
{
	int consumed = 0;
	com_token[0] = '\0';
	sscanf (buffer, "%1023s\n%n", com_token, &consumed);
	return buffer + consumed;
}

/*
=================
COM_LoadPackFile -- johnfitz -- modified based on topaz's tutorial

Takes an explicit (not game tree related) path to a pak file.

Loads the header and directory, adding the files at the beginning
of the list so they override previous pack files.
=================
*/
static pack_t *COM_LoadPackFile (const char *packfile, int packhandle)
{
	dpackheader_t  header;
	int			   i;
	packfile_t	  *newfiles;
	int			   numpackfiles;
	pack_t		  *pack;
	dpackfile_t	   info[MAX_FILES_IN_PACK];
	unsigned short crc;

	Sys_FileRead (packhandle, (void *)&header, sizeof (header));
	if (header.id[0] != 'P' || header.id[1] != 'A' || header.id[2] != 'C' || header.id[3] != 'K')
		Sys_Error ("%s is not a packfile", packfile);

	header.dirofs = LittleLong (header.dirofs);
	header.dirlen = LittleLong (header.dirlen);

	numpackfiles = header.dirlen / sizeof (dpackfile_t);

	if (header.dirlen < 0 || header.dirofs < 0)
	{
		Sys_Error ("Invalid packfile %s (dirlen: %i, dirofs: %i)", packfile, header.dirlen, header.dirofs);
	}
	if (!numpackfiles)
	{
		Sys_Printf ("WARNING: %s has no files, ignored\n", packfile);
		Sys_FileClose (packhandle);
		return NULL;
	}
	if (numpackfiles > MAX_FILES_IN_PACK)
		Sys_Error ("%s has %i files", packfile, numpackfiles);

	if (numpackfiles != PAK0_COUNT)
		com_modified = true; // not the original file

	newfiles = (packfile_t *)Mem_Alloc (numpackfiles * sizeof (packfile_t));

	Sys_FileSeek (packhandle, header.dirofs);
	Sys_FileRead (packhandle, (void *)info, header.dirlen);

	// crc the directory to check for modifications
	CRC_Init (&crc);
	for (i = 0; i < header.dirlen; i++)
		CRC_ProcessByte (&crc, ((byte *)info)[i]);
	if (crc != PAK0_CRC_V106 && crc != PAK0_CRC_V101 && crc != PAK0_CRC_V100)
		com_modified = true;

	// parse the directory
	for (i = 0; i < numpackfiles; i++)
	{
		q_strlcpy (newfiles[i].name, info[i].name, sizeof (newfiles[i].name));
		newfiles[i].filepos = LittleLong (info[i].filepos);
		newfiles[i].filelen = LittleLong (info[i].filelen);
	}

	pack = (pack_t *)Mem_Alloc (sizeof (pack_t));
	q_strlcpy (pack->filename, packfile, sizeof (pack->filename));
	pack->handle = packhandle;
	pack->numfiles = numpackfiles;
	pack->files = newfiles;

	// Sys_Printf ("Added packfile %s (%i files)\n", packfile, numpackfiles);
	return pack;
}

const char *COM_GetGameNames (qboolean full)
{
	if (full)
	{
		if (*com_gamenames)
			return va ("%s;%s", GAMENAME, com_gamenames);
		else
			return GAMENAME;
	}
	return com_gamenames;
	//	return COM_SkipPath(com_gamedir);
}

// if either contain id1 then that gets ignored
qboolean COM_GameDirMatches (const char *tdirs)
{
	int			gnl = strlen (GAMENAME);
	const char *odirs = COM_GetGameNames (false);

	// ignore any core paths.
	if (!strncmp (tdirs, GAMENAME, gnl) && (tdirs[gnl] == ';' || !tdirs[gnl]))
	{
		tdirs += gnl;
		if (*tdirs == ';')
			tdirs++;
	}
	if (!strncmp (odirs, GAMENAME, gnl) && (odirs[gnl] == ';' || !odirs[gnl]))
	{
		odirs += gnl;
		if (*odirs == ';')
			odirs++;
	}
	// skip any qw in there from quakeworld (remote servers should really be skipping this, unless its maybe the only one in the path).
	if (!strncmp (tdirs, "qw;", 3) || !strcmp (tdirs, "qw"))
	{
		tdirs += 2;
		if (*tdirs == ';')
			tdirs++;
	}
	if (!strncmp (odirs, "qw;", 3) || !strcmp (odirs, "qw")) // need to cope with ourselves setting it that way too, just in case.
	{
		odirs += 2;
		if (*odirs == ';')
			odirs++;
	}

	// okay, now check it properly
	if (!strcmp (odirs, tdirs))
		return true;
	return false;
}

/*
=================
COM_AddGameDirectory -- johnfitz -- modified based on topaz's tutorial
=================
*/
static void COM_AddGameDirectory (const char *dir)
{
	const char	 *base = com_basedir;
	int			  i, packhandle;
	unsigned int  path_id;
	searchpath_t *search;
	pack_t		 *pak;
	char		  pakfile[MAX_OSPATH];
	qboolean	  been_here = false;
	static byte	 *vkquake_pak_extracted;

	if (*com_gamenames)
		q_strlcat (com_gamenames, ";", sizeof (com_gamenames));
	q_strlcat (com_gamenames, dir, sizeof (com_gamenames));

	// quakespasm enables mission pack flags automatically,
	// so e.g. -game rogue works without breaking the hud
	if (!q_strcasecmp (dir, "rogue"))
	{
		rogue = true;
		standard_quake = false;
	}
	if (!q_strcasecmp (dir, "hipnotic") || !q_strcasecmp (dir, "quoth"))
	{
		hipnotic = true;
		standard_quake = false;
	}

	q_strlcpy (com_gamedir, va ("%s/%s", base, dir), sizeof (com_gamedir));

	// assign a path_id to this game directory
	if (com_searchpaths)
		path_id = com_searchpaths->path_id << 1;
	else
		path_id = 1U;

_add_path:
	// add the directory to the search path
	search = (searchpath_t *)Mem_Alloc (sizeof (searchpath_t));
	search->path_id = path_id;
	q_strlcpy (search->filename, com_gamedir, sizeof (search->filename));
	search->next = com_searchpaths;
	com_searchpaths = search;

	// add any pak files in the format pak0.pak pak1.pak, ...
	for (i = 0;; i++)
	{
		q_snprintf (pakfile, sizeof (pakfile), "%s/pak%i.pak", com_gamedir, i);
		if (Sys_FileOpenRead (pakfile, &packhandle) == -1)
			break;
		pak = COM_LoadPackFile (pakfile, packhandle);
		if (pak)
		{
			search = (searchpath_t *)Mem_Alloc (sizeof (searchpath_t));
			search->path_id = path_id;
			search->pack = pak;
			search->next = com_searchpaths;
			com_searchpaths = search;
		}

		if ((i == 0) && (path_id == 1) && !fitzmode)
		{
			size_t vkquake_pak_size_compressed = vkquake_pak_size, vkquake_pak_size_extracted = vkquake_pak_decompressed_size;
			if (!vkquake_pak_extracted)
			{
				tinfl_decompressor inflator;
				tinfl_init (&inflator);
				vkquake_pak_extracted = Mem_Alloc (vkquake_pak_size_extracted);
				if (TINFL_STATUS_DONE != tinfl_decompress (
											 &inflator, vkquake_pak, &vkquake_pak_size_compressed, vkquake_pak_extracted, vkquake_pak_extracted,
											 &vkquake_pak_size_extracted, TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF))
					Sys_Error ("Error extracting embedded pack");
			}
			qboolean pak0_modified = com_modified;
			Sys_MemFileOpenRead (vkquake_pak_extracted, vkquake_pak_size_extracted, &packhandle);
			pak = COM_LoadPackFile ("vkquake.pak", packhandle);
			search = (searchpath_t *)Mem_Alloc (sizeof (searchpath_t));
			search->path_id = path_id;
			search->pack = pak;
			search->next = com_searchpaths;
			com_searchpaths = search;
			com_modified = pak0_modified;
		}

		if (!pak)
			break;
	}

	if (!been_here && host_parms->userdir != host_parms->basedir)
	{
		been_here = true;
		q_strlcpy (com_gamedir, va ("%s/%s", host_parms->userdir, dir), sizeof (com_gamedir));
		Sys_mkdir (com_gamedir);
		goto _add_path;
	}
}

void COM_ResetGameDirectories (const char *newdirs)
{
	char		 *newgamedirs = q_strdup (newdirs);
	char		 *newpath, *path;
	searchpath_t *search;
	// Kill the extra game if it is loaded
	while (com_searchpaths != com_base_searchpaths)
	{
		if (com_searchpaths->pack)
		{
			Sys_FileClose (com_searchpaths->pack->handle);
			Mem_Free (com_searchpaths->pack->files);
			Mem_Free (com_searchpaths->pack);
		}
		search = com_searchpaths->next;
		Mem_Free (com_searchpaths);
		com_searchpaths = search;
	}
	hipnotic = false;
	rogue = false;
	standard_quake = true;
	// wipe the list of mod gamedirs
	*com_gamenames = 0;
	// reset this too
	q_strlcpy (com_gamedir, va ("%s/%s", (host_parms->userdir != host_parms->basedir) ? host_parms->userdir : com_basedir, GAMENAME), sizeof (com_gamedir));

	for (newpath = newgamedirs; newpath && *newpath;)
	{
		char *e = strchr (newpath, ';');
		if (e)
			*e++ = 0;

		if (!q_strcasecmp (GAMENAME, newpath))
			path = NULL;
		else
		{
			for (path = newgamedirs; path < newpath; path += strlen (path) + 1)
			{
				if (!q_strcasecmp (path, newpath))
					break;
			}
		}

		if (path == newpath) // not already loaded
			COM_AddGameDirectory (newpath);
		newpath = e;
	}
	Mem_Free (newgamedirs);
}

qboolean COM_ModForbiddenChars (const char *p)
{
	return !*p || !strcmp (p, ".") || strstr (p, "..") || strstr (p, "/") || strstr (p, "\\") || strstr (p, ":") || strstr (p, "\"") || strstr (p, ";");
}

//==============================================================================
// johnfitz -- dynamic gamedir stuff -- modified by QuakeSpasm team.
//==============================================================================
static void COM_Game_f (void)
{
	if (Cmd_Argc () > 1)
	{
		int	 i, pri;
		char paths[1024];

		if (!registered.value) // disable shareware quake
		{
			Con_Printf ("You must have the registered version to use modified games\n");
			return;
		}

		*paths = 0;
		q_strlcat (paths, GAMENAME, sizeof (paths));
		for (pri = 0; pri <= 1; pri++)
		{
			for (i = 1; i < Cmd_Argc (); i++)
			{
				const char *p = Cmd_Argv (i);
				if (!*p)
					p = GAMENAME;
				if (pri == 0)
				{
					if (*p != '-')
						continue;
					p++;
				}
				else if (*p == '-')
					continue;

				if (COM_ModForbiddenChars (p))
				{
					Con_Printf ("gamedir should be a single directory name, not a path\n");
					return;
				}

				if (!q_strcasecmp (p, GAMENAME))
					continue; // don't add id1, its not interesting enough.

				if (*paths)
					q_strlcat (paths, ";", sizeof (paths));
				q_strlcat (paths, p, sizeof (paths));
			}
		}

		if (!q_strcasecmp (paths, COM_GetGameNames (true)))
		{
			Con_Printf ("\"game\" is already \"%s\"\n", COM_GetGameNames (true));
			return;
		}

		com_modified = true;

		// Kill the server
		CL_Disconnect ();
		cls.demonum = -1;
		Host_ShutdownServer (true);

		SCR_CenterPrintClear ();

		// Write config file
		Host_WriteConfiguration ();

		COM_ResetGameDirectories (paths);

		// clear out and reload appropriate data
		Mod_ResetAll ();
		Sky_ClearAll ();
		if (!isDedicated)
		{
			TexMgr_NewGame ();
			Draw_NewGame ();
			R_NewGame ();
			M_NewGame ();
		}
		ExtraMaps_NewGame ();
		Host_Resetdemos ();
		DemoList_Rebuild ();
		SaveList_Rebuild ();
		S_ClearAll ();

		Con_Printf ("\"game\" changed to \"%s\"\n", COM_GetGameNames (true));

		VID_Lock ();
		Cbuf_AddText ("exec quake.rc\n");
		Cbuf_AddText ("vid_unlock\n");
	}
	else // Diplay the current gamedir
		Con_Printf ("\"game\" is \"%s\"\n", COM_GetGameNames (true));
}

/*
=================
COM_InitFilesystem
=================
*/
void COM_InitFilesystem (void) // johnfitz -- modified based on topaz's tutorial
{
	int			i, j;
	const char *p;

	Cvar_RegisterVariable (&registered);
	Cvar_RegisterVariable (&cmdline);
	Cmd_AddCommand ("path", COM_Path_f);
	Cmd_AddCommand ("game", COM_Game_f); // johnfitz

	i = COM_CheckParm ("-basedir");
	if (i && i < com_argc - 1)
		q_strlcpy (com_basedir, com_argv[i + 1], sizeof (com_basedir));
	else
		q_strlcpy (com_basedir, host_parms->basedir, sizeof (com_basedir));

	j = strlen (com_basedir);
	if (j < 1)
		Sys_Error ("Bad argument to -basedir");
	if ((com_basedir[j - 1] == '\\') || (com_basedir[j - 1] == '/'))
		com_basedir[j - 1] = 0;

	i = COM_CheckParmNext (i, "-basegame");
	if (i)
	{ //-basegame:
		// a) replaces all hardcoded dirs (read: alternative to id1)
		// b) isn't flushed on normal gamedir switches (like id1).
		com_modified = true; // shouldn't be relevant when not using id content... but we don't really know.
		for (;; i = COM_CheckParmNext (i, "-basegame"))
		{
			if (!i || i >= com_argc - 1)
				break;

			p = com_argv[i + 1];
			if (COM_ModForbiddenChars (p))
				Sys_Error ("gamedir should be a single directory name, not a path\n");
			if (p != NULL)
				COM_AddGameDirectory (p);
		}
	}
	else
	{
		// start up with GAMENAME by default (id1)
		COM_AddGameDirectory (GAMENAME);
	}

	/* this is the end of our base searchpath:
	 * any set gamedirs, such as those from -game command line
	 * arguments or by the 'game' console command will be freed
	 * up to here upon a new game command. */
	com_base_searchpaths = com_searchpaths;
	COM_ResetGameDirectories ("");

	// add mission pack requests (only one should be specified)
	if (COM_CheckParm ("-rogue"))
		COM_AddGameDirectory ("rogue");
	if (COM_CheckParm ("-hipnotic"))
		COM_AddGameDirectory ("hipnotic");
	if (COM_CheckParm ("-quoth"))
		COM_AddGameDirectory ("quoth");

	for (i = 0;;)
	{
		i = COM_CheckParmNext (i, "-game");
		if (!i || i >= com_argc - 1)
			break;

		p = com_argv[i + 1];
		if (COM_ModForbiddenChars (p))
			Sys_Error ("gamedir should be a single directory name, not a path\n");
		com_modified = true;
		if (p != NULL)
			COM_AddGameDirectory (p);
	}

	COM_CheckRegistered ();
}

/* The following FS_*() stdio replacements are necessary if one is
 * to perform non-sequential reads on files reopened on pak files
 * because we need the bookkeeping about file start/end positions.
 * Allocating and filling in the fshandle_t structure is the users'
 * responsibility when the file is initially opened. */

size_t FS_fread (void *ptr, size_t size, size_t nmemb, fshandle_t *fh)
{
	long   byte_size;
	long   bytes_read;
	size_t nmemb_read;

	if (!fh)
	{
		errno = EBADF;
		return 0;
	}
	if (!ptr)
	{
		errno = EFAULT;
		return 0;
	}
	if (!size || !nmemb)
	{ /* no error, just zero bytes wanted */
		errno = 0;
		return 0;
	}

	byte_size = nmemb * size;
	if (byte_size > fh->length - fh->pos) /* just read to end */
		byte_size = fh->length - fh->pos;
	bytes_read = fread (ptr, 1, byte_size, fh->file);
	fh->pos += bytes_read;

	/* fread() must return the number of elements read,
	 * not the total number of bytes. */
	nmemb_read = bytes_read / size;
	/* even if the last member is only read partially
	 * it is counted as a whole in the return value. */
	if (bytes_read % size)
		nmemb_read++;

	return nmemb_read;
}

int FS_fseek (fshandle_t *fh, long offset, int whence)
{
	/* I don't care about 64 bit off_t or fseeko() here.
	 * the quake/hexen2 file system is 32 bits, anyway. */
	int ret;

	if (!fh)
	{
		errno = EBADF;
		return -1;
	}

	/* the relative file position shouldn't be smaller
	 * than zero or bigger than the filesize. */
	switch (whence)
	{
	case SEEK_SET:
		break;
	case SEEK_CUR:
		offset += fh->pos;
		break;
	case SEEK_END:
		offset = fh->length + offset;
		break;
	default:
		errno = EINVAL;
		return -1;
	}

	if (offset < 0)
	{
		errno = EINVAL;
		return -1;
	}

	if (offset > fh->length) /* just seek to end */
		offset = fh->length;

	ret = fseek (fh->file, fh->start + offset, SEEK_SET);
	if (ret < 0)
		return ret;

	fh->pos = offset;
	return 0;
}

int FS_fclose (fshandle_t *fh)
{
	if (!fh)
	{
		errno = EBADF;
		return -1;
	}
	return fclose (fh->file);
}

long FS_ftell (fshandle_t *fh)
{
	if (!fh)
	{
		errno = EBADF;
		return -1;
	}
	return fh->pos;
}

void FS_rewind (fshandle_t *fh)
{
	if (!fh)
		return;
	clearerr (fh->file);
	fseek (fh->file, fh->start, SEEK_SET);
	fh->pos = 0;
}

int FS_feof (fshandle_t *fh)
{
	if (!fh)
	{
		errno = EBADF;
		return -1;
	}
	if (fh->pos >= fh->length)
		return -1;
	return 0;
}

int FS_ferror (fshandle_t *fh)
{
	if (!fh)
	{
		errno = EBADF;
		return -1;
	}
	return ferror (fh->file);
}

int FS_fgetc (fshandle_t *fh)
{
	if (!fh)
	{
		errno = EBADF;
		return EOF;
	}
	if (fh->pos >= fh->length)
		return EOF;
	fh->pos += 1;
	return fgetc (fh->file);
}

char *FS_fgets (char *s, int size, fshandle_t *fh)
{
	char *ret;

	if (FS_feof (fh))
		return NULL;

	if (size > (fh->length - fh->pos) + 1)
		size = (fh->length - fh->pos) + 1;

	ret = fgets (s, size, fh->file);
	fh->pos = ftell (fh->file) - fh->start;

	return ret;
}

long FS_filelength (fshandle_t *fh)
{
	if (!fh)
	{
		errno = EBADF;
		return -1;
	}
	return fh->length;
}

#ifdef PSET_SCRIPT
// for compat with dpp7 protocols, and mods that cba to precache things.
void COM_Effectinfo_Enumerate (int (*cb) (const char *pname))
{
	int				   i;
	const char		  *f, *e;
	char			  *buf;
	static const char *dpnames[] = {"TE_GUNSHOT",		"TE_GUNSHOTQUAD",
									"TE_SPIKE",			"TE_SPIKEQUAD",
									"TE_SUPERSPIKE",	"TE_SUPERSPIKEQUAD",
									"TE_WIZSPIKE",		"TE_KNIGHTSPIKE",
									"TE_EXPLOSION",		"TE_EXPLOSIONQUAD",
									"TE_TAREXPLOSION",	"TE_TELEPORT",
									"TE_LAVASPLASH",	"TE_SMALLFLASH",
									"TE_FLAMEJET",		"EF_FLAME",
									"TE_BLOOD",			"TE_SPARK",
									"TE_PLASMABURN",	"TE_TEI_G3",
									"TE_TEI_SMOKE",		"TE_TEI_BIGEXPLOSION",
									"TE_TEI_PLASMAHIT", "EF_STARDUST",
									"TR_ROCKET",		"TR_GRENADE",
									"TR_BLOOD",			"TR_WIZSPIKE",
									"TR_SLIGHTBLOOD",	"TR_KNIGHTSPIKE",
									"TR_VORESPIKE",		"TR_NEHAHRASMOKE",
									"TR_NEXUIZPLASMA",	"TR_GLOWTRAIL",
									"SVC_PARTICLE",		NULL};

	buf = (char *)COM_LoadFile ("effectinfo.txt", NULL);
	if (!buf)
		return;

	for (i = 0; dpnames[i]; i++)
		cb (dpnames[i]);

	for (f = buf; f; f = e)
	{
		e = COM_Parse (f);
		if (!strcmp (com_token, "effect"))
		{
			e = COM_Parse (e);
			cb (com_token);
		}
		while (e && *e && *e != '\n')
			e++;
	}
	Mem_Free (buf);
}
#endif

/*
============================================================================
								LOCALIZATION
============================================================================
*/
typedef struct
{
	char *key;
	char *value;
} locentry_t;

typedef struct
{
	int			numentries;
	int			maxnumentries;
	int			numindices;
	unsigned   *indices;
	locentry_t *entries;
	char	   *text;
} localization_t;

static localization_t localization;

/*
================
COM_HashString
Computes the FNV-1a hash of string str
================
*/
unsigned COM_HashString (const char *str)
{
	unsigned hash = 0x811c9dc5u;
	while (*str)
	{
		hash ^= *str++;
		hash *= 0x01000193u;
	}
	return hash;
}

static size_t mz_zip_file_read_func (void *opaque, mz_uint64 ofs, void *buf, size_t n)
{
	if (SDL_RWseek ((SDL_RWops *)opaque, (Sint64)ofs, RW_SEEK_SET) < 0)
		return 0;
	return SDL_RWread ((SDL_RWops *)opaque, buf, 1, n);
}

/*
================
LOC_LoadFile
================
*/
void LOC_LoadFile (const char *file)
{
	char  path[1024];
	int	  i, lineno;
	char *cursor;

	SDL_RWops	  *rw = NULL;
	Sint64		   sz;
	mz_zip_archive archive;
	size_t		   size = 0;

	// clear existing data
	if (localization.text)
	{
		Mem_Free (localization.text);
		localization.text = NULL;
	}
	localization.numentries = 0;
	localization.numindices = 0;

	if (!file || !*file)
		return;

	Con_Printf ("\nLanguage initialization\n");

	memset (&archive, 0, sizeof (archive));
	q_snprintf (path, sizeof (path), "%s/%s", com_basedir, file);
	rw = SDL_RWFromFile (path, "rb");
#if defined(DO_USERDIRS)
	if (!rw)
	{
		q_snprintf (path, sizeof (path), "%s/%s", host_parms->userdir, file);
		rw = SDL_RWFromFile (path, "rb");
	}
#endif
	if (!rw)
	{
		q_snprintf (path, sizeof (path), "%s/QuakeEX.kpf", com_basedir);
		rw = SDL_RWFromFile (path, "rb");
#if defined(DO_USERDIRS)
		if (!rw)
		{
			q_snprintf (path, sizeof (path), "%s/QuakeEX.kpf", host_parms->userdir);
			rw = SDL_RWFromFile (path, "rb");
		}
#endif
		if (!rw)
			goto fail;
		sz = SDL_RWsize (rw);
		if (sz <= 0)
			goto fail;
		archive.m_pRead = mz_zip_file_read_func;
		archive.m_pIO_opaque = rw;
		if (!mz_zip_reader_init (&archive, sz, 0))
			goto fail;
		localization.text = (char *)mz_zip_reader_extract_file_to_heap (&archive, file, &size, 0);
		if (!localization.text)
			goto fail;
		mz_zip_reader_end (&archive);
		SDL_RWclose (rw);
		localization.text = (char *)Mem_Realloc (localization.text, size + 1);
		localization.text[size] = 0;
	}
	else
	{
		sz = SDL_RWsize (rw);
		if (sz <= 0)
			goto fail;
		localization.text = (char *)Mem_Alloc (sz + 1);
		if (!localization.text)
		{
		fail:
			mz_zip_reader_end (&archive);
			if (rw)
				SDL_RWclose (rw);
			Con_Printf ("Couldn't load '%s'\nfrom '%s'\n", file, com_basedir);
			return;
		}
		SDL_RWread (rw, localization.text, 1, sz);
		SDL_RWclose (rw);
	}

	cursor = localization.text;

	// skip BOM
	if ((unsigned char)(cursor[0]) == 0xEF && (unsigned char)(cursor[1]) == 0xBB && (unsigned char)(cursor[2]) == 0xBF)
		cursor += 3;

	lineno = 0;
	while (*cursor)
	{
		char *line, *equals;

		lineno++;

		// skip leading whitespace
		while (q_isblank (*cursor))
			++cursor;

		line = cursor;
		equals = NULL;
		// find line end and first equals sign, if any
		while (*cursor && *cursor != '\n')
		{
			if (*cursor == '=' && !equals)
				equals = cursor;
			cursor++;
		}

		if (line[0] == '/')
		{
			if (line[1] != '/')
				Con_DPrintf ("LOC_LoadFile: malformed comment on line %d\n", lineno);
		}
		else if (equals)
		{
			char	   *key_end = equals;
			qboolean	leading_quote;
			qboolean	trailing_quote;
			locentry_t *entry;
			char	   *value_src;
			char	   *value_dst;
			char	   *value;

			// trim whitespace before equals sign
			while (key_end != line && q_isspace (key_end[-1]))
				key_end--;
			*key_end = 0;

			value = equals + 1;
			// skip whitespace after equals sign
			while (value != cursor && q_isspace (*value))
				value++;

			leading_quote = (*value == '\"');
			trailing_quote = false;
			value += leading_quote;

			// transform escape sequences in-place
			value_src = value;
			value_dst = value;
			while (value_src != cursor)
			{
				if (*value_src == '\\' && value_src + 1 != cursor)
				{
					char c = value_src[1];
					value_src += 2;
					switch (c)
					{
					case 'n':
						*value_dst++ = '\n';
						break;
					case 't':
						*value_dst++ = '\t';
						break;
					case 'v':
						*value_dst++ = '\v';
						break;
					case 'b':
						*value_dst++ = '\b';
						break;
					case 'f':
						*value_dst++ = '\f';
						break;

					case '"':
					case '\'':
						*value_dst++ = c;
						break;

					default:
						Con_Printf ("LOC_LoadFile: unrecognized escape sequence \\%c on line %d\n", c, lineno);
						*value_dst++ = c;
						break;
					}
					continue;
				}

				if (*value_src == '\"')
				{
					trailing_quote = true;
					*value_dst = 0;
					break;
				}

				*value_dst++ = *value_src++;
			}

			// if not a quoted string, trim trailing whitespace
			if (!trailing_quote)
			{
				while (value_dst != value && q_isblank (value_dst[-1]))
				{
					*value_dst = 0;
					value_dst--;
				}
			}

			if (localization.numentries == localization.maxnumentries)
			{
				// grow by 50%
				localization.maxnumentries += localization.maxnumentries >> 1;
				localization.maxnumentries = q_max (localization.maxnumentries, 32);
				localization.entries = (locentry_t *)Mem_Realloc (localization.entries, sizeof (*localization.entries) * localization.maxnumentries);
			}

			entry = &localization.entries[localization.numentries++];
			entry->key = line;
			entry->value = value;
		}

		if (*cursor)
			*cursor++ = 0; // terminate line and advance to next
	}

	// hash all entries

	localization.numindices = localization.numentries * 2; // 50% load factor
	if (localization.numindices == 0)
	{
		Con_Printf ("No localized strings in file '%s'\n", file);
		return;
	}

	localization.indices = (unsigned *)Mem_Realloc (localization.indices, localization.numindices * sizeof (*localization.indices));
	memset (localization.indices, 0, localization.numindices * sizeof (*localization.indices));

	for (i = 0; i < localization.numentries; i++)
	{
		locentry_t *entry = &localization.entries[i];
		unsigned	pos = COM_HashString (entry->key) % localization.numindices, end = pos;

		for (;;)
		{
			if (!localization.indices[pos])
			{
				localization.indices[pos] = i + 1;
				break;
			}

			++pos;
			if (pos == localization.numindices)
				pos = 0;

			if (pos == end)
				Sys_Error ("LOC_LoadFile failed");
		}
	}

	Con_Printf ("Loaded %d strings from '%s'\n", localization.numentries, file);
}

/*
================
LOC_Init
================
*/
void LOC_Init (void)
{
	LOC_LoadFile ("localization/loc_english.txt");
}

/*
================
LOC_Shutdown
================
*/
void LOC_Shutdown (void)
{
	Mem_Free (localization.indices);
	Mem_Free (localization.entries);
	Mem_Free (localization.text);
}

/*
================
LOC_GetRawString

Returns localized string if available, or NULL otherwise
================
*/
const char *LOC_GetRawString (const char *key)
{
	unsigned pos, end;

	if (!localization.numindices || !key || !*key || *key != '$')
		return NULL;
	key++;

	pos = COM_HashString (key) % localization.numindices;
	end = pos;

	do
	{
		unsigned	idx = localization.indices[pos];
		locentry_t *entry;
		if (!idx)
			return NULL;

		entry = &localization.entries[idx - 1];
		if (!strcmp (entry->key, key))
			return entry->value;

		++pos;
		if (pos == localization.numindices)
			pos = 0;
	} while (pos != end);

	return NULL;
}

/*
================
LOC_GetString

Returns localized string if available, or input string otherwise
================
*/
const char *LOC_GetString (const char *key)
{
	const char *value = LOC_GetRawString (key);
	return value ? value : key;
}

/*
================
LOC_ParseArg

Returns argument index (>= 0) and advances the string if it starts with a placeholder ({} or {N}),
otherwise returns a negative value and leaves the pointer unchanged
================
*/
static int LOC_ParseArg (const char **pstr)
{
	int			arg;
	const char *str = *pstr;

	// opening brace
	if (*str != '{')
		return -1;
	++str;

	// optional index, defaulting to 0
	arg = 0;
	while (q_isdigit (*str))
		arg = arg * 10 + *str++ - '0';

	// closing brace
	if (*str != '}')
		return -1;
	*pstr = ++str;

	return arg;
}

/*
================
LOC_HasPlaceholders
================
*/
qboolean LOC_HasPlaceholders (const char *str)
{
	if (!localization.numindices)
		return false;
	while (*str)
	{
		if (LOC_ParseArg (&str) >= 0)
			return true;
		str++;
	}
	return false;
}

/*
================
LOC_Format

Replaces placeholders (of the form {} or {N}) with the corresponding arguments

Returns number of written chars, excluding the NUL terminator
If len > 0, output is always NUL-terminated
================
*/
size_t LOC_Format (const char *format, const char *(*getarg_fn) (int idx, void *userdata), void *userdata, char *out, size_t len)
{
	size_t written = 0;
	int	   numargs = 0;

	if (!len)
	{
		Con_DPrintf ("LOC_Format: no output space\n");
		return 0;
	}
	--len; // reserve space for the terminator

	while (*format && written < len)
	{
		const char *insert;
		size_t		space_left;
		size_t		insert_len;
		int			argindex = LOC_ParseArg (&format);

		if (argindex < 0)
		{
			out[written++] = *format++;
			continue;
		}

		insert = getarg_fn (argindex, userdata);
		space_left = len - written;
		insert_len = strlen (insert);

		if (insert_len > space_left)
		{
			Con_DPrintf ("LOC_Format: overflow at argument #%d\n", numargs);
			insert_len = space_left;
		}

		memcpy (out + written, insert, insert_len);
		written += insert_len;
	}

	if (*format)
		Con_DPrintf ("LOC_Format: overflow\n");

	out[written] = 0;

	return written;
}
