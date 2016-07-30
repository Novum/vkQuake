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

static char	*largv[MAX_NUM_ARGVS + 1];
static char	argvdummy[] = " ";

int		safemode;

cvar_t	registered = {"registered","1",CVAR_ROM}; /* set to correct value in COM_CheckRegistered() */
cvar_t	cmdline = {"cmdline","",CVAR_ROM/*|CVAR_SERVERINFO*/}; /* sending cmdline upon CCREQ_RULE_INFO is evil */

static qboolean		com_modified;	// set true if using non-id files

qboolean		fitzmode;

static void COM_Path_f (void);

// if a packfile directory differs from this, it is assumed to be hacked
#define PAK0_COUNT		339	/* id1/pak0.pak - v1.0x */
#define PAK0_CRC_V100		13900	/* id1/pak0.pak - v1.00 */
#define PAK0_CRC_V101		62751	/* id1/pak0.pak - v1.01 */
#define PAK0_CRC_V106		32981	/* id1/pak0.pak - v1.06 */
#define PAK0_CRC	(PAK0_CRC_V106)
#define PAK0_COUNT_V091		308	/* id1/pak0.pak - v0.91/0.92, not supported */
#define PAK0_CRC_V091		28804	/* id1/pak0.pak - v0.91/0.92, not supported */

char	com_token[1024];
int		com_argc;
char	**com_argv;

#define CMDLINE_LENGTH	256		/* johnfitz -- mirrored in cmd.c */
char	com_cmdline[CMDLINE_LENGTH];

qboolean standard_quake = true, rogue, hipnotic;

// this graphic needs to be in the pak file to use registered features
static unsigned short pop[] =
{
	0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,
	0x0000,0x0000,0x6600,0x0000,0x0000,0x0000,0x6600,0x0000,
	0x0000,0x0066,0x0000,0x0000,0x0000,0x0000,0x0067,0x0000,
	0x0000,0x6665,0x0000,0x0000,0x0000,0x0000,0x0065,0x6600,
	0x0063,0x6561,0x0000,0x0000,0x0000,0x0000,0x0061,0x6563,
	0x0064,0x6561,0x0000,0x0000,0x0000,0x0000,0x0061,0x6564,
	0x0064,0x6564,0x0000,0x6469,0x6969,0x6400,0x0064,0x6564,
	0x0063,0x6568,0x6200,0x0064,0x6864,0x0000,0x6268,0x6563,
	0x0000,0x6567,0x6963,0x0064,0x6764,0x0063,0x6967,0x6500,
	0x0000,0x6266,0x6769,0x6a68,0x6768,0x6a69,0x6766,0x6200,
	0x0000,0x0062,0x6566,0x6666,0x6666,0x6666,0x6562,0x0000,
	0x0000,0x0000,0x0062,0x6364,0x6664,0x6362,0x0000,0x0000,
	0x0000,0x0000,0x0000,0x0062,0x6662,0x0000,0x0000,0x0000,
	0x0000,0x0000,0x0000,0x0061,0x6661,0x0000,0x0000,0x0000,
	0x0000,0x0000,0x0000,0x0000,0x6500,0x0000,0x0000,0x0000,
	0x0000,0x0000,0x0000,0x0000,0x6400,0x0000,0x0000,0x0000
};

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

int q_strcasecmp(const char * s1, const char * s2)
{
	const char * p1 = s1;
	const char * p2 = s2;
	char c1, c2;

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

int q_strncasecmp(const char *s1, const char *s2, size_t n)
{
	const char * p1 = s1;
	const char * p2 = s2;
	char c1, c2;

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

char *q_strlwr (char *str)
{
	char	*c;
	c = str;
	while (*c)
	{
		*c = q_tolower(*c);
		c++;
	}
	return str;
}

char *q_strupr (char *str)
{
	char	*c;
	c = str;
	while (*c)
	{
		*c = q_toupper(*c);
		c++;
	}
	return str;
}

/* platform dependant (v)snprintf function names: */
#if defined(_WIN32)
#define	snprintf_func		_snprintf
#define	vsnprintf_func		_vsnprintf
#else
#define	snprintf_func		snprintf
#define	vsnprintf_func		vsnprintf
#endif

int q_vsnprintf(char *str, size_t size, const char *format, va_list args)
{
	int		ret;

	ret = vsnprintf_func (str, size, format, args);

	if (ret < 0)
		ret = (int)size;
	if (size == 0)	/* no buffer */
		return ret;
	if ((size_t)ret >= size)
		str[size - 1] = '\0';

	return ret;
}

int q_snprintf (char *str, size_t size, const char *format, ...)
{
	int		ret;
	va_list		argptr;

	va_start (argptr, format);
	ret = q_vsnprintf (str, size, format, argptr);
	va_end (argptr);

	return ret;
}

void Q_memset (void *dest, int fill, size_t count)
{
	size_t		i;

	if ( (((size_t)dest | count) & 3) == 0)
	{
		count >>= 2;
		fill = fill | (fill<<8) | (fill<<16) | (fill<<24);
		for (i = 0; i < count; i++)
			((int *)dest)[i] = fill;
	}
	else
		for (i = 0; i < count; i++)
			((byte *)dest)[i] = fill;
}

void Q_memcpy (void *dest, const void *src, size_t count)
{
	size_t		i;

	if (( ( (size_t)dest | (size_t)src | count) & 3) == 0 )
	{
		count >>= 2;
		for (i = 0; i < count; i++)
			((int *)dest)[i] = ((int *)src)[i];
	}
	else
		for (i = 0; i < count; i++)
			((byte *)dest)[i] = ((byte *)src)[i];
}

int Q_memcmp (const void *m1, const void *m2, size_t count)
{
	while(count)
	{
		count--;
		if (((byte *)m1)[count] != ((byte *)m2)[count])
			return -1;
	}
	return 0;
}

void Q_strcpy (char *dest, const char *src)
{
	while (*src)
	{
		*dest++ = *src++;
	}
	*dest++ = 0;
}

void Q_strncpy (char *dest, const char *src, int count)
{
	while (*src && count--)
	{
		*dest++ = *src++;
	}
	if (count)
		*dest++ = 0;
}

int Q_strlen (const char *str)
{
	int		count;

	count = 0;
	while (str[count])
		count++;

	return count;
}

char *Q_strrchr(const char *s, char c)
{
	int len = Q_strlen(s);
	s += len;
	while (len--)
	{
		if (*--s == c)
			return (char *)s;
	}
	return NULL;
}

void Q_strcat (char *dest, const char *src)
{
	dest += Q_strlen(dest);
	Q_strcpy (dest, src);
}

int Q_strcmp (const char *s1, const char *s2)
{
	while (1)
	{
		if (*s1 != *s2)
			return -1;		// strings not equal
		if (!*s1)
			return 0;		// strings are equal
		s1++;
		s2++;
	}

	return -1;
}

int Q_strncmp (const char *s1, const char *s2, int count)
{
	while (1)
	{
		if (!count--)
			return 0;
		if (*s1 != *s2)
			return -1;		// strings not equal
		if (!*s1)
			return 0;		// strings are equal
		s1++;
		s2++;
	}

	return -1;
}

int Q_atoi (const char *str)
{
	int		val;
	int		sign;
	int		c;

	if (*str == '-')
	{
		sign = -1;
		str++;
	}
	else
		sign = 1;

	val = 0;

//
// check for hex
//
	if (str[0] == '0' && (str[1] == 'x' || str[1] == 'X') )
	{
		str += 2;
		while (1)
		{
			c = *str++;
			if (c >= '0' && c <= '9')
				val = (val<<4) + c - '0';
			else if (c >= 'a' && c <= 'f')
				val = (val<<4) + c - 'a' + 10;
			else if (c >= 'A' && c <= 'F')
				val = (val<<4) + c - 'A' + 10;
			else
				return val*sign;
		}
	}

//
// check for character
//
	if (str[0] == '\'')
	{
		return sign * str[1];
	}

//
// assume decimal
//
	while (1)
	{
		c = *str++;
		if (c <'0' || c > '9')
			return val*sign;
		val = val*10 + c - '0';
	}

	return 0;
}


float Q_atof (const char *str)
{
	double		val;
	int		sign;
	int		c;
	int	decimal, total;

	if (*str == '-')
	{
		sign = -1;
		str++;
	}
	else
		sign = 1;

	val = 0;

//
// check for hex
//
	if (str[0] == '0' && (str[1] == 'x' || str[1] == 'X') )
	{
		str += 2;
		while (1)
		{
			c = *str++;
			if (c >= '0' && c <= '9')
				val = (val*16) + c - '0';
			else if (c >= 'a' && c <= 'f')
				val = (val*16) + c - 'a' + 10;
			else if (c >= 'A' && c <= 'F')
				val = (val*16) + c - 'A' + 10;
			else
				return val*sign;
		}
	}

//
// check for character
//
	if (str[0] == '\'')
	{
		return sign * str[1];
	}

//
// assume decimal
//
	decimal = -1;
	total = 0;
	while (1)
	{
		c = *str++;
		if (c == '.')
		{
			decimal = total;
			continue;
		}
		if (c <'0' || c > '9')
			break;
		val = val*10 + c - '0';
		total++;
	}

	if (decimal == -1)
		return val*sign;
	while (total > decimal)
	{
		val /= 10;
		total--;
	}

	return val*sign;
}

/*
============================================================================

					BYTE ORDER FUNCTIONS

============================================================================
*/

qboolean	host_bigendian;

short	(*BigShort) (short l);
short	(*LittleShort) (short l);
int	(*BigLong) (int l);
int	(*LittleLong) (int l);
float	(*BigFloat) (float l);
float	(*LittleFloat) (float l);

short ShortSwap (short l)
{
	byte	b1, b2;

	b1 = l&255;
	b2 = (l>>8)&255;

	return (b1<<8) + b2;
}

short ShortNoSwap (short l)
{
	return l;
}

int LongSwap (int l)
{
	byte	b1, b2, b3, b4;

	b1 = l&255;
	b2 = (l>>8)&255;
	b3 = (l>>16)&255;
	b4 = (l>>24)&255;

	return ((int)b1<<24) + ((int)b2<<16) + ((int)b3<<8) + b4;
}

int LongNoSwap (int l)
{
	return l;
}

float FloatSwap (float f)
{
	union
	{
		float	f;
		byte	b[4];
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
	byte	*buf;

#ifdef PARANOID
	if (c < -128 || c > 127)
		Sys_Error ("MSG_WriteChar: range error");
#endif

	buf = (byte *) SZ_GetSpace (sb, 1);
	buf[0] = c;
}

void MSG_WriteByte (sizebuf_t *sb, int c)
{
	byte	*buf;

#ifdef PARANOID
	if (c < 0 || c > 255)
		Sys_Error ("MSG_WriteByte: range error");
#endif

	buf = (byte *) SZ_GetSpace (sb, 1);
	buf[0] = c;
}

void MSG_WriteShort (sizebuf_t *sb, int c)
{
	byte	*buf;

#ifdef PARANOID
	if (c < ((short)0x8000) || c > (short)0x7fff)
		Sys_Error ("MSG_WriteShort: range error");
#endif

	buf = (byte *) SZ_GetSpace (sb, 2);
	buf[0] = c&0xff;
	buf[1] = c>>8;
}

void MSG_WriteLong (sizebuf_t *sb, int c)
{
	byte	*buf;

	buf = (byte *) SZ_GetSpace (sb, 4);
	buf[0] = c&0xff;
	buf[1] = (c>>8)&0xff;
	buf[2] = (c>>16)&0xff;
	buf[3] = c>>24;
}

void MSG_WriteFloat (sizebuf_t *sb, float f)
{
	union
	{
		float	f;
		int	l;
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
		SZ_Write (sb, s, Q_strlen(s)+1);
}

//johnfitz -- original behavior, 13.3 fixed point coords, max range +-4096
void MSG_WriteCoord16 (sizebuf_t *sb, float f)
{
	MSG_WriteShort (sb, Q_rint(f*8));
}

//johnfitz -- 16.8 fixed point coords, max range +-32768
void MSG_WriteCoord24 (sizebuf_t *sb, float f)
{
	MSG_WriteShort (sb, f);
	MSG_WriteByte (sb, (int)(f*255)%255);
}

//johnfitz -- 32-bit float coords
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
	else MSG_WriteCoord16 (sb, f);
}

void MSG_WriteAngle (sizebuf_t *sb, float f, unsigned int flags)
{
	if (flags & PRFL_FLOATANGLE)
		MSG_WriteFloat (sb, f);
	else if (flags & PRFL_SHORTANGLE)
		MSG_WriteShort (sb, Q_rint(f * 65536.0 / 360.0) & 65535);
	else MSG_WriteByte (sb, Q_rint(f * 256.0 / 360.0) & 255); //johnfitz -- use Q_rint instead of (int)	}
}

//johnfitz -- for PROTOCOL_FITZQUAKE
void MSG_WriteAngle16 (sizebuf_t *sb, float f, unsigned int flags)
{
	if (flags & PRFL_FLOATANGLE)
		MSG_WriteFloat (sb, f);
	else MSG_WriteShort (sb, Q_rint(f * 65536.0 / 360.0) & 65535);
}
//johnfitz

//
// reading functions
//
int		msg_readcount;
qboolean	msg_badread;

void MSG_BeginReading (void)
{
	msg_readcount = 0;
	msg_badread = false;
}

// returns -1 and sets msg_badread if no more characters are available
int MSG_ReadChar (void)
{
	int	c;

	if (msg_readcount+1 > net_message.cursize)
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
	int	c;

	if (msg_readcount+1 > net_message.cursize)
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
	int	c;

	if (msg_readcount+2 > net_message.cursize)
	{
		msg_badread = true;
		return -1;
	}

	c = (short)(net_message.data[msg_readcount]
			+ (net_message.data[msg_readcount+1]<<8));

	msg_readcount += 2;

	return c;
}

int MSG_ReadLong (void)
{
	int	c;

	if (msg_readcount+4 > net_message.cursize)
	{
		msg_badread = true;
		return -1;
	}

	c = net_message.data[msg_readcount]
			+ (net_message.data[msg_readcount+1]<<8)
			+ (net_message.data[msg_readcount+2]<<16)
			+ (net_message.data[msg_readcount+3]<<24);

	msg_readcount += 4;

	return c;
}

float MSG_ReadFloat (void)
{
	union
	{
		byte	b[4];
		float	f;
		int	l;
	} dat;

	dat.b[0] = net_message.data[msg_readcount];
	dat.b[1] = net_message.data[msg_readcount+1];
	dat.b[2] = net_message.data[msg_readcount+2];
	dat.b[3] = net_message.data[msg_readcount+3];
	msg_readcount += 4;

	dat.l = LittleLong (dat.l);

	return dat.f;
}

const char *MSG_ReadString (void)
{
	static char	string[2048];
	int		c;
	size_t		l;

	l = 0;
	do
	{
		c = MSG_ReadByte ();
		if (c == -1 || c == 0)
			break;
		string[l] = c;
		l++;
	} while (l < sizeof(string) - 1);

	string[l] = 0;

	return string;
}

//johnfitz -- original behavior, 13.3 fixed point coords, max range +-4096
float MSG_ReadCoord16 (void)
{
	return MSG_ReadShort() * (1.0/8);
}

//johnfitz -- 16.8 fixed point coords, max range +-32768
float MSG_ReadCoord24 (void)
{
	return MSG_ReadShort() + MSG_ReadByte() * (1.0/255);
}

//johnfitz -- 32-bit float coords
float MSG_ReadCoord32f (void)
{
	return MSG_ReadFloat();
}

float MSG_ReadCoord (unsigned int flags)
{
	if (flags & PRFL_FLOATCOORD)
		return MSG_ReadFloat ();
	else if (flags & PRFL_INT32COORD)
		return MSG_ReadLong () * (1.0 / 16.0);
	else if (flags & PRFL_24BITCOORD)
		return MSG_ReadCoord24 ();
	else return MSG_ReadCoord16 ();
}

float MSG_ReadAngle (unsigned int flags)
{
	if (flags & PRFL_FLOATANGLE)
		return MSG_ReadFloat ();
	else if (flags & PRFL_SHORTANGLE)
		return MSG_ReadShort () * (360.0 / 65536);
	else return MSG_ReadChar () * (360.0 / 256);
}

//johnfitz -- for PROTOCOL_FITZQUAKE
float MSG_ReadAngle16 (unsigned int flags)
{
	if (flags & PRFL_FLOATANGLE)
		return MSG_ReadFloat ();	// make sure
	else return MSG_ReadShort () * (360.0 / 65536);
}
//johnfitz

//===========================================================================

void SZ_Alloc (sizebuf_t *buf, int startsize)
{
	if (startsize < 256)
		startsize = 256;
	buf->data = (byte *) Hunk_AllocName (startsize, "sizebuf");
	buf->maxsize = startsize;
	buf->cursize = 0;
}


void SZ_Free (sizebuf_t *buf)
{
//	Z_Free (buf->data);
//	buf->data = NULL;
//	buf->maxsize = 0;
	buf->cursize = 0;
}

void SZ_Clear (sizebuf_t *buf)
{
	buf->cursize = 0;
}

void *SZ_GetSpace (sizebuf_t *buf, int length)
{
	void	*data;

	if (buf->cursize + length > buf->maxsize)
	{
		if (!buf->allowoverflow)
			Sys_Error ("SZ_GetSpace: overflow without allowoverflow set");

		if (length > buf->maxsize)
			Sys_Error ("SZ_GetSpace: %i is > full buffer size", length);

		buf->overflowed = true;
		Con_Printf ("SZ_GetSpace: overflow");
		SZ_Clear (buf);
	}

	data = buf->data + buf->cursize;
	buf->cursize += length;

	return data;
}

void SZ_Write (sizebuf_t *buf, const void *data, int length)
{
	Q_memcpy (SZ_GetSpace(buf,length),data,length);
}

void SZ_Print (sizebuf_t *buf, const char *data)
{
	int		len = Q_strlen(data) + 1;

	if (buf->data[buf->cursize-1])
	{	/* no trailing 0 */
		Q_memcpy ((byte *)SZ_GetSpace(buf, len  )  , data, len);
	}
	else
	{	/* write over trailing 0 */
		Q_memcpy ((byte *)SZ_GetSpace(buf, len-1)-1, data, len);
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
	const char	*last;

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
	int	length;

	if (!*in)
	{
		*out = '\0';
		return;
	}
	if (in != out)	/* copy when not in-place editing */
		q_strlcpy (out, in, outsize);
	length = (int)strlen(out) - 1;
	while (length > 0 && out[length] != '.')
	{
		--length;
		if (out[length] == '/' || out[length] == '\\')
			return;	/* no extension */
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
	const char	*src;
	size_t		len;

	len = strlen(in);
	if (len < 2)	/* nothing meaningful */
		return "";

	src = in + len - 1;
	while (src != in && src[-1] != '.')
		src--;
	if (src == in || strchr(src, '/') != NULL || strchr(src, '\\') != NULL)
		return "";	/* no extension, or parent directory has a dot */

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
	if (! *ext)
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
	const char	*dot, *slash, *s;

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
		size_t	len = dot - slash;
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
	if (strcmp(COM_FileGetExtension(path), extension + 1) != 0)
		q_strlcat(path, extension, len);
}


/*
==============
COM_Parse

Parse a token out of a string
==============
*/
const char *COM_Parse (const char *data)
{
	int		c;
	int		len;

	len = 0;
	com_token[0] = 0;

	if (!data)
		return NULL;

// skip whitespace
skipwhite:
	while ((c = *data) <= ' ')
	{
		if (c == 0)
			return NULL;	// end of file
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
	if (c == '{' || c == '}'|| c == '('|| c == ')' || c == '\'' || c == ':')
	{
		com_token[len] = c;
		len++;
		com_token[len] = 0;
		return data+1;
	}

// parse a regular word
	do
	{
		com_token[len] = c;
		data++;
		len++;
		c = *data;
		/* commented out the check for ':' so that ip:port works */
		if (c == '{' || c == '}'|| c == '('|| c == ')' || c == '\''/* || c == ':' */)
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
int COM_CheckParm (const char *parm)
{
	int		i;

	for (i = 1; i < com_argc; i++)
	{
		if (!com_argv[i])
			continue;		// NEXTSTEP sometimes clears appkit vars.
		if (!Q_strcmp (parm,com_argv[i]))
			return i;
	}

	return 0;
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
	int		h;
	unsigned short	check[128];
	int		i;

	COM_OpenFile("gfx/pop.lmp", &h, NULL);

	if (h == -1)
	{
		Cvar_SetROM ("registered", "0");
		Con_Printf ("Playing shareware version.\n");
		if (com_modified)
			Sys_Error ("You must have the registered version to use modified games");
		return;
	}

	Sys_FileRead (h, check, sizeof(check));
	COM_CloseFile (h);

	for (i = 0; i < 128; i++)
	{
		if (pop[i] != (unsigned short)BigShort (check[i]))
			Sys_Error ("Corrupted data file.");
	}

	for (i = 0; com_cmdline[i]; i++)
	{
		if (com_cmdline[i]!= ' ')
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
	int		i, j, n;

// reconstitute the command line for the cmdline externally visible cvar
	n = 0;

	for (j = 0; (j<MAX_NUM_ARGVS) && (j< argc); j++)
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

	if (n > 0 && com_cmdline[n-1] == ' ')
		com_cmdline[n-1] = 0; //johnfitz -- kill the trailing space

	Con_Printf("Command line: %s\n", com_cmdline);

	for (com_argc = 0; (com_argc < MAX_NUM_ARGVS) && (com_argc < argc); com_argc++)
	{
		largv[com_argc] = argv[com_argc];
		if (!Q_strcmp ("-safe", argv[com_argc]))
			safemode = 1;
	}

	largv[com_argc] = argvdummy;
	com_argv = largv;

	if (COM_CheckParm ("-rogue"))
	{
		rogue = true;
		standard_quake = false;
	}

	if (COM_CheckParm ("-hipnotic") || COM_CheckParm ("-quoth")) //johnfitz -- "-quoth" support
	{
		hipnotic = true;
		standard_quake = false;
	}
}

/*
================
Test_f -- johnfitz
================
*/
#ifdef _DEBUG
static void FitzTest_f (void)
{
}
#endif

/*
================
COM_Init
================
*/
void COM_Init (void)
{
	int	i = 0x12345678;
		/*    U N I X */

	/*
	BE_ORDER:  12 34 56 78
		   U  N  I  X

	LE_ORDER:  78 56 34 12
		   X  I  N  U

	PDP_ORDER: 34 12 78 56
		   N  U  X  I
	*/
	if ( *(char *)&i == 0x12 )
		host_bigendian = true;
	else if ( *(char *)&i == 0x78 )
		host_bigendian = false;
	else /* if ( *(char *)&i == 0x34 ) */
		Sys_Error ("Unsupported endianism.");

	if (host_bigendian)
	{
		BigShort = ShortNoSwap;
		LittleShort = ShortSwap;
		BigLong = LongNoSwap;
		LittleLong = LongSwap;
		BigFloat = FloatNoSwap;
		LittleFloat = FloatSwap;
	}
	else /* assumed LITTLE_ENDIAN. */
	{
		BigShort = ShortSwap;
		LittleShort = ShortNoSwap;
		BigLong = LongSwap;
		LittleLong = LongNoSwap;
		BigFloat = FloatSwap;
		LittleFloat = FloatNoSwap;
	}

	if (COM_CheckParm("-fitz"))
		fitzmode = true;
#ifdef _DEBUG
	Cmd_AddCommand ("fitztest", FitzTest_f); //johnfitz
#endif
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
#define	VA_NUM_BUFFS	4
#define	VA_BUFFERLEN	1024

static char *get_va_buffer(void)
{
	static char va_buffers[VA_NUM_BUFFS][VA_BUFFERLEN];
	static int buffer_idx = 0;
	buffer_idx = (buffer_idx + 1) & (VA_NUM_BUFFS - 1);
	return va_buffers[buffer_idx];
}

char *va (const char *format, ...)
{
	va_list		argptr;
	char		*va_buf;

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

int	com_filesize;


//
// on-disk pakfile
//
typedef struct
{
	char	name[56];
	int		filepos, filelen;
} dpackfile_t;

typedef struct
{
	char	id[4];
	int		dirofs;
	int		dirlen;
} dpackheader_t;

#define MAX_FILES_IN_PACK	2048

char	com_gamedir[MAX_OSPATH];
char	com_basedir[MAX_OSPATH];
int	file_from_pak;		// ZOID: global indicating that file came from a pak

searchpath_t	*com_searchpaths;
searchpath_t	*com_base_searchpaths;

/*
============
COM_Path_f
============
*/
static void COM_Path_f (void)
{
	searchpath_t	*s;

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
	int		handle;
	char	name[MAX_OSPATH];

	Sys_mkdir (com_gamedir); //johnfitz -- if we've switched to a nonexistant gamedir, create it now so we don't crash

	q_snprintf (name, sizeof(name), "%s/%s", com_gamedir, filename);

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
	char	*ofs;

	for (ofs = path + 1; *ofs; ofs++)
	{
		if (*ofs == '/')
		{	// create the directory
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
	long		pos, end;

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
static int COM_FindFile (const char *filename, int *handle, FILE **file,
							unsigned int *path_id)
{
	searchpath_t	*search;
	char		netpath[MAX_OSPATH];
	pack_t		*pak;
	int		i, findtime;

	if (file && handle)
		Sys_Error ("COM_FindFile: both handle and file set");

	file_from_pak = 0;

//
// search through the path, one element at a time
//
	for (search = com_searchpaths; search; search = search->next)
	{
		if (search->pack)	/* look through all the pak file elements */
		{
			pak = search->pack;
			for (i = 0; i < pak->numfiles; i++)
			{
				if (strcmp(pak->files[i].name, filename) != 0)
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
		else	/* check a file in the directory tree */
		{
			if (!registered.value)
			{ /* if not a registered version, don't ever go beyond base */
				if ( strchr (filename, '/') || strchr (filename,'\\'))
					continue;
			}

			q_snprintf (netpath, sizeof(netpath), "%s/%s",search->filename, filename);
			findtime = Sys_FileTime (netpath);
			if (findtime == -1)
				continue;

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

	if (strcmp(COM_FileGetExtension(filename), "pcx") != 0
		&& strcmp(COM_FileGetExtension(filename), "tga") != 0
		&& strcmp(COM_FileGetExtension(filename), "lit") != 0
		&& strcmp(COM_FileGetExtension(filename), "ent") != 0)
		Con_DPrintf ("FindFile: can't find %s\n", filename);
	else	Con_DPrintf2("FindFile: can't find %s\n", filename);
		// Log pcx, tga, lit, ent misses only if (developer.value >= 2)

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
	searchpath_t	*s;

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
#define	LOADFILE_ZONE		0
#define	LOADFILE_HUNK		1
#define	LOADFILE_TEMPHUNK	2
#define	LOADFILE_CACHE		3
#define	LOADFILE_STACK		4
#define	LOADFILE_MALLOC		5

static byte	*loadbuf;
static cache_user_t *loadcache;
static int	loadsize;

byte *COM_LoadFile (const char *path, int usehunk, unsigned int *path_id)
{
	int		h;
	byte	*buf;
	char	base[32];
	int		len;

	buf = NULL;	// quiet compiler warning

// look for it in the filesystem or pack files
	len = COM_OpenFile (path, &h, path_id);
	if (h == -1)
		return NULL;

// extract the filename base name for hunk tag
	COM_FileBase (path, base, sizeof(base));

	switch (usehunk)
	{
	case LOADFILE_HUNK:
		buf = (byte *) Hunk_AllocName (len+1, base);
		break;
	case LOADFILE_TEMPHUNK:
		buf = (byte *) Hunk_TempAlloc (len+1);
		break;
	case LOADFILE_ZONE:
		buf = (byte *) Z_Malloc (len+1);
		break;
	case LOADFILE_CACHE:
		buf = (byte *) Cache_Alloc (loadcache, len+1, base);
		break;
	case LOADFILE_STACK:
		if (len < loadsize)
			buf = loadbuf;
		else
			buf = (byte *) Hunk_TempAlloc (len+1);
		break;
	case LOADFILE_MALLOC:
		buf = (byte *) malloc (len+1);
		break;
	default:
		Sys_Error ("COM_LoadFile: bad usehunk");
	}

	if (!buf)
		Sys_Error ("COM_LoadFile: not enough space for %s", path);

	((byte *)buf)[len] = 0;

	Sys_FileRead (h, buf, len);
	COM_CloseFile (h);

	return buf;
}

byte *COM_LoadHunkFile (const char *path, unsigned int *path_id)
{
	return COM_LoadFile (path, LOADFILE_HUNK, path_id);
}

byte *COM_LoadZoneFile (const char *path, unsigned int *path_id)
{
	return COM_LoadFile (path, LOADFILE_ZONE, path_id);
}

byte *COM_LoadTempFile (const char *path, unsigned int *path_id)
{
	return COM_LoadFile (path, LOADFILE_TEMPHUNK, path_id);
}

void COM_LoadCacheFile (const char *path, struct cache_user_s *cu, unsigned int *path_id)
{
	loadcache = cu;
	COM_LoadFile (path, LOADFILE_CACHE, path_id);
}

// uses temp hunk if larger than bufsize
byte *COM_LoadStackFile (const char *path, void *buffer, int bufsize, unsigned int *path_id)
{
	byte	*buf;

	loadbuf = (byte *)buffer;
	loadsize = bufsize;
	buf = COM_LoadFile (path, LOADFILE_STACK, path_id);

	return buf;
}

// returns malloc'd memory
byte *COM_LoadMallocFile (const char *path, unsigned int *path_id)
{
	return COM_LoadFile (path, LOADFILE_MALLOC, path_id);
}


/*
=================
COM_LoadPackFile -- johnfitz -- modified based on topaz's tutorial

Takes an explicit (not game tree related) path to a pak file.

Loads the header and directory, adding the files at the beginning
of the list so they override previous pack files.
=================
*/
static pack_t *COM_LoadPackFile (const char *packfile)
{
	dpackheader_t	header;
	int		i;
	packfile_t	*newfiles;
	int		numpackfiles;
	pack_t		*pack;
	int		packhandle;
	dpackfile_t	info[MAX_FILES_IN_PACK];
	unsigned short	crc;

	if (Sys_FileOpenRead (packfile, &packhandle) == -1)
		return NULL;

	Sys_FileRead (packhandle, (void *)&header, sizeof(header));
	if (header.id[0] != 'P' || header.id[1] != 'A' || header.id[2] != 'C' || header.id[3] != 'K')
		Sys_Error ("%s is not a packfile", packfile);

	header.dirofs = LittleLong (header.dirofs);
	header.dirlen = LittleLong (header.dirlen);

	numpackfiles = header.dirlen / sizeof(dpackfile_t);

	if (header.dirlen < 0 || header.dirofs < 0)
	{
		Sys_Error ("Invalid packfile %s (dirlen: %i, dirofs: %i)",
					packfile, header.dirlen, header.dirofs);
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
		com_modified = true;	// not the original file

	newfiles = (packfile_t *) Z_Malloc(numpackfiles * sizeof(packfile_t));

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
		q_strlcpy (newfiles[i].name, info[i].name, sizeof(newfiles[i].name));
		newfiles[i].filepos = LittleLong(info[i].filepos);
		newfiles[i].filelen = LittleLong(info[i].filelen);
	}

	pack = (pack_t *) Z_Malloc (sizeof (pack_t));
	q_strlcpy (pack->filename, packfile, sizeof(pack->filename));
	pack->handle = packhandle;
	pack->numfiles = numpackfiles;
	pack->files = newfiles;

	//Sys_Printf ("Added packfile %s (%i files)\n", packfile, numpackfiles);
	return pack;
}

/*
=================
COM_AddGameDirectory -- johnfitz -- modified based on topaz's tutorial
=================
*/
static void COM_AddGameDirectory (const char *base, const char *dir)
{
	int i;
	unsigned int path_id;
	searchpath_t *search;
	pack_t *pak, *qspak;
	char pakfile[MAX_OSPATH];
	qboolean been_here = false;

	q_strlcpy (com_gamedir, va("%s/%s", base, dir), sizeof(com_gamedir));

	// assign a path_id to this game directory
	if (com_searchpaths)
		path_id = com_searchpaths->path_id << 1;
	else	path_id = 1U;

_add_path:
	// add the directory to the search path
	search = (searchpath_t *) Z_Malloc(sizeof(searchpath_t));
	search->path_id = path_id;
	q_strlcpy (search->filename, com_gamedir, sizeof(search->filename));
	search->next = com_searchpaths;
	com_searchpaths = search;

	// add any pak files in the format pak0.pak pak1.pak, ...
	for (i = 0; ; i++)
	{
		q_snprintf (pakfile, sizeof(pakfile), "%s/pak%i.pak", com_gamedir, i);
		pak = COM_LoadPackFile (pakfile);
		if (i != 0 || path_id != 1 || fitzmode)
			qspak = NULL;
		else {
			qboolean old = com_modified;
			if (been_here) base = host_parms->userdir;
			q_snprintf (pakfile, sizeof(pakfile), "%s/quakespasm.pak", base);
			qspak = COM_LoadPackFile (pakfile);
			com_modified = old;
		}
		if (pak) {
			search = (searchpath_t *) Z_Malloc(sizeof(searchpath_t));
			search->path_id = path_id;
			search->pack = pak;
			search->next = com_searchpaths;
			com_searchpaths = search;
		}
		if (qspak) {
			search = (searchpath_t *) Z_Malloc(sizeof(searchpath_t));
			search->path_id = path_id;
			search->pack = qspak;
			search->next = com_searchpaths;
			com_searchpaths = search;
		}
		if (!pak) break;
	}

	if (!been_here && host_parms->userdir != host_parms->basedir)
	{
		been_here = true;
		q_strlcpy(com_gamedir, va("%s/%s", host_parms->userdir, dir), sizeof(com_gamedir));
		Sys_mkdir(com_gamedir);
		goto _add_path;
	}
}

//==============================================================================
//johnfitz -- dynamic gamedir stuff -- modified by QuakeSpasm team.
//==============================================================================
void ExtraMaps_NewGame (void);
static void COM_Game_f (void)
{
	if (Cmd_Argc() > 1)
	{
		const char *p = Cmd_Argv(1);
		const char *p2 = Cmd_Argv(2);
		searchpath_t *search;

		if (!registered.value) //disable shareware quake
		{
			Con_Printf("You must have the registered version to use modified games\n");
			return;
		}

		if (!*p || !strcmp(p, ".") || strstr(p, "..") || strstr(p, "/") || strstr(p, "\\") || strstr(p, ":"))
		{
			Con_Printf ("gamedir should be a single directory name, not a path\n");
			return;
		}

		if (*p2)
		{
			if (strcmp(p2,"-hipnotic") && strcmp(p2,"-rogue") && strcmp(p2,"-quoth")) {
				Con_Printf ("invalid mission pack argument to \"game\"\n");
				return;
			}
			if (!q_strcasecmp(p, GAMENAME)) {
				Con_Printf ("no mission pack arguments to %s game\n", GAMENAME);
				return;
			}
		}

		if (!q_strcasecmp(p, COM_SkipPath(com_gamedir))) //no change
		{
			if (com_searchpaths->path_id > 1) { //current game not id1
				if (*p2 && com_searchpaths->path_id == 2) {
					// rely on QuakeSpasm extension treating '-game missionpack'
					// as '-missionpack', otherwise would be a mess
					if (!q_strcasecmp(p, &p2[1]))
						goto _same;
					Con_Printf("reloading game \"%s\" with \"%s\" support\n", p, &p2[1]);
				}
				else if (!*p2 && com_searchpaths->path_id > 2)
					Con_Printf("reloading game \"%s\" without mission pack support\n", p);
				else goto _same;
			}
			else { _same:
				Con_Printf("\"game\" is already \"%s\"\n", COM_SkipPath(com_gamedir));
				return;
			}
		}

		com_modified = true;

		//Kill the server
		CL_Disconnect ();
		Host_ShutdownServer(true);

		//Write config file
		Host_WriteConfiguration ();

		//Kill the extra game if it is loaded
		while (com_searchpaths != com_base_searchpaths)
		{
			if (com_searchpaths->pack)
			{
				Sys_FileClose (com_searchpaths->pack->handle);
				Z_Free (com_searchpaths->pack->files);
				Z_Free (com_searchpaths->pack);
			}
			search = com_searchpaths->next;
			Z_Free (com_searchpaths);
			com_searchpaths = search;
		}
		hipnotic = false;
		rogue = false;
		standard_quake = true;

		if (q_strcasecmp(p, GAMENAME)) //game is not id1
		{
			if (*p2) {
				COM_AddGameDirectory (com_basedir, &p2[1]);
				standard_quake = false;
				if (!strcmp(p2,"-hipnotic") || !strcmp(p2,"-quoth"))
					hipnotic = true;
				else if (!strcmp(p2,"-rogue"))
					rogue = true;
				if (q_strcasecmp(p, &p2[1])) //don't load twice
					COM_AddGameDirectory (com_basedir, p);
			}
			else {
				COM_AddGameDirectory (com_basedir, p);
				// QuakeSpasm extension: treat '-game missionpack' as '-missionpack'
				if (!q_strcasecmp(p,"hipnotic") || !q_strcasecmp(p,"quoth")) {
					hipnotic = true;
					standard_quake = false;
				}
				else if (!q_strcasecmp(p,"rogue")) {
					rogue = true;
					standard_quake = false;
				}
			}
		}
		else // just update com_gamedir
		{
			q_snprintf (com_gamedir, sizeof(com_gamedir), "%s/%s",
					(host_parms->userdir != host_parms->basedir)?
						   host_parms->userdir : com_basedir,
					GAMENAME);
		}

		//clear out and reload appropriate data
		Cache_Flush ();
		Mod_ResetAll();
		if (!isDedicated)
		{
			TexMgr_NewGame ();
			Draw_NewGame ();
			R_NewGame ();
		}
		ExtraMaps_NewGame ();
		DemoList_Rebuild ();

		Con_Printf("\"game\" changed to \"%s\"\n", COM_SkipPath(com_gamedir));
		Con_Printf("enter \"exec quake.rc\" to load new configs\n");
		//Cbuf_InsertText ("exec quake.rc\n");
	}
	else //Diplay the current gamedir
		Con_Printf("\"game\" is \"%s\"\n", COM_SkipPath(com_gamedir));
}

/*
=================
COM_InitFilesystem
=================
*/
void COM_InitFilesystem (void) //johnfitz -- modified based on topaz's tutorial
{
	int i, j;

	Cvar_RegisterVariable (&registered);
	Cvar_RegisterVariable (&cmdline);
	Cmd_AddCommand ("path", COM_Path_f);
	Cmd_AddCommand ("game", COM_Game_f); //johnfitz

	i = COM_CheckParm ("-basedir");
	if (i && i < com_argc-1)
		q_strlcpy (com_basedir, com_argv[i + 1], sizeof(com_basedir));
	else
		q_strlcpy (com_basedir, host_parms->basedir, sizeof(com_basedir));

	j = strlen (com_basedir);
	if (j < 1) Sys_Error("Bad argument to -basedir");
	if ((com_basedir[j-1] == '\\') || (com_basedir[j-1] == '/'))
		com_basedir[j-1] = 0;

	// start up with GAMENAME by default (id1)
	COM_AddGameDirectory (com_basedir, GAMENAME);

	/* this is the end of our base searchpath:
	 * any set gamedirs, such as those from -game command line
	 * arguments or by the 'game' console command will be freed
	 * up to here upon a new game command. */
	com_base_searchpaths = com_searchpaths;

	// add mission pack requests (only one should be specified)
	if (COM_CheckParm ("-rogue"))
		COM_AddGameDirectory (com_basedir, "rogue");
	if (COM_CheckParm ("-hipnotic"))
		COM_AddGameDirectory (com_basedir, "hipnotic");
	if (COM_CheckParm ("-quoth"))
		COM_AddGameDirectory (com_basedir, "quoth");


	i = COM_CheckParm ("-game");
	if (i && i < com_argc-1)
	{
		const char *p = com_argv[i + 1];
		if (!*p || !strcmp(p, ".") || strstr(p, "..") || strstr(p, "/") || strstr(p, "\\") || strstr(p, ":"))
			Sys_Error ("gamedir should be a single directory name, not a path\n");
		com_modified = true;
		// don't load mission packs twice
		if (COM_CheckParm ("-rogue") && !q_strcasecmp(p, "rogue")) p = NULL;
		if (COM_CheckParm ("-hipnotic") && !q_strcasecmp(p, "hipnotic")) p = NULL;
		if (COM_CheckParm ("-quoth") && !q_strcasecmp(p, "quoth")) p = NULL;
		if (p != NULL) {
			COM_AddGameDirectory (com_basedir, p);
			// QuakeSpasm extension: treat '-game missionpack' as '-missionpack'
			if (!q_strcasecmp(p,"rogue")) {
				rogue = true;
				standard_quake = false;
			}
			if (!q_strcasecmp(p,"hipnotic") || !q_strcasecmp(p,"quoth")) {
				hipnotic = true;
				standard_quake = false;
			}
		}
	}

	COM_CheckRegistered ();
}


/* The following FS_*() stdio replacements are necessary if one is
 * to perform non-sequential reads on files reopened on pak files
 * because we need the bookkeeping about file start/end positions.
 * Allocating and filling in the fshandle_t structure is the users'
 * responsibility when the file is initially opened. */

size_t FS_fread(void *ptr, size_t size, size_t nmemb, fshandle_t *fh)
{
	long byte_size;
	long bytes_read;
	size_t nmemb_read;

	if (!fh) {
		errno = EBADF;
		return 0;
	}
	if (!ptr) {
		errno = EFAULT;
		return 0;
	}
	if (!size || !nmemb) {	/* no error, just zero bytes wanted */
		errno = 0;
		return 0;
	}

	byte_size = nmemb * size;
	if (byte_size > fh->length - fh->pos)	/* just read to end */
		byte_size = fh->length - fh->pos;
	bytes_read = fread(ptr, 1, byte_size, fh->file);
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

int FS_fseek(fshandle_t *fh, long offset, int whence)
{
/* I don't care about 64 bit off_t or fseeko() here.
 * the quake/hexen2 file system is 32 bits, anyway. */
	int ret;

	if (!fh) {
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

	if (offset < 0) {
		errno = EINVAL;
		return -1;
	}

	if (offset > fh->length)	/* just seek to end */
		offset = fh->length;

	ret = fseek(fh->file, fh->start + offset, SEEK_SET);
	if (ret < 0)
		return ret;

	fh->pos = offset;
	return 0;
}

int FS_fclose(fshandle_t *fh)
{
	if (!fh) {
		errno = EBADF;
		return -1;
	}
	return fclose(fh->file);
}

long FS_ftell(fshandle_t *fh)
{
	if (!fh) {
		errno = EBADF;
		return -1;
	}
	return fh->pos;
}

void FS_rewind(fshandle_t *fh)
{
	if (!fh) return;
	clearerr(fh->file);
	fseek(fh->file, fh->start, SEEK_SET);
	fh->pos = 0;
}

int FS_feof(fshandle_t *fh)
{
	if (!fh) {
		errno = EBADF;
		return -1;
	}
	if (fh->pos >= fh->length)
		return -1;
	return 0;
}

int FS_ferror(fshandle_t *fh)
{
	if (!fh) {
		errno = EBADF;
		return -1;
	}
	return ferror(fh->file);
}

int FS_fgetc(fshandle_t *fh)
{
	if (!fh) {
		errno = EBADF;
		return EOF;
	}
	if (fh->pos >= fh->length)
		return EOF;
	fh->pos += 1;
	return fgetc(fh->file);
}

char *FS_fgets(char *s, int size, fshandle_t *fh)
{
	char *ret;

	if (FS_feof(fh))
		return NULL;

	if (size > (fh->length - fh->pos) + 1)
		size = (fh->length - fh->pos) + 1;

	ret = fgets(s, size, fh->file);
	fh->pos = ftell(fh->file) - fh->start;

	return ret;
}

long FS_filelength (fshandle_t *fh)
{
	if (!fh) {
		errno = EBADF;
		return -1;
	}
	return fh->length;
}

