/* vim: set tabstop=4: */
/*
Copyright (C) 1996-1997 Id Software, Inc.
Copyright (C) 2016      Spike

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

// provides a few convienience extensions, primarily builtins, but also autocvars.
// Also note the set+seta features.

#include "quakedef.h"
#include "q_ctype.h"

extern void PF_bprint (void);
extern void PF_sprint (void);
extern void PF_centerprint (void);
extern void PF_sv_finalefinished (void);
extern void PF_sv_CheckPlayerEXFlags (void);
extern void PF_sv_walkpathtogoal (void);
extern void PF_sv_localsound (void);

static float PR_GetVMScale (void)
{
	// sigh, this is horrible (divides glwidth)
	float s = CLAMP (1.0, scr_sbarscale.value, (float)glwidth / 320.0);
	return s;
}

// there's a few different aproaches to tempstrings...
// the lame way is to just have a single one (vanilla).
// the only slightly less lame way is to just cycle between 16 or so (most engines).
// one funky way is to allocate a single large buffer and just concatenate it for more tempstring space. don't forget to resize (dp).
// alternatively, just allocate them persistently and purge them only when there appear to be no more references to it (fte). makes strzone redundant.

extern cvar_t sv_gameplayfix_setmodelrealbox, r_fteparticles;
cvar_t pr_checkextension = {"pr_checkextension", "1", CVAR_NONE}; // spike - enables qc extensions. if 0 then they're ALL BLOCKED! MWAHAHAHA! *cough* *splutter*
static int pr_ext_warned_particleeffectnum;						  // so these only spam once per map

static void *PR_FindExtGlobal (int type, const char *name);
void		 SV_CheckVelocity (edict_t *ent);

typedef enum multicast_e
{
	MULTICAST_ALL_U,
	MULTICAST_PHS_U,
	MULTICAST_PVS_U,
	MULTICAST_ALL_R,
	MULTICAST_PHS_R,
	MULTICAST_PVS_R,

	MULTICAST_ONE_U,
	MULTICAST_ONE_R,
	MULTICAST_INIT
} multicast_t;
static void SV_Multicast (multicast_t to, float *org, int msg_entity, unsigned int requireext2);

#define RETURN_EDICT(e) (((int *)qcvm->globals)[OFS_RETURN] = EDICT_TO_PROG (e))

int PR_MakeTempString (const char *val)
{
	char *tmp = PR_GetTempString ();
	q_strlcpy (tmp, val, STRINGTEMP_LENGTH);
	return PR_SetEngineString (tmp);
}

#define ishex(c) ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))
static int dehex (char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'A' && c <= 'F')
		return c - ('A' - 10);
	if (c >= 'a' && c <= 'f')
		return c - ('a' - 10);
	return 0;
}
// returns the next char...
struct markup_s
{
	const unsigned char *txt;
	vec4_t				 tint;	 // predefined colour that applies to the entire string
	vec4_t				 colour; // colour for the specific glyph in question
	unsigned char		 mask;
};
void PR_Markup_Begin (struct markup_s *mu, const char *text, float *rgb, float alpha)
{
	if (*text == '\1' || *text == '\2')
	{
		mu->mask = 128;
		text++;
	}
	else
		mu->mask = 0;
	mu->txt = (const unsigned char *)text;
	VectorCopy (rgb, mu->tint);
	mu->tint[3] = alpha;
	VectorCopy (rgb, mu->colour);
	mu->colour[3] = alpha;
}
int PR_Markup_Parse (struct markup_s *mu)
{
	static const vec4_t q3rgb[10] = {{0.00, 0.00, 0.00, 1.0}, {1.00, 0.33, 0.33, 1.0}, {0.00, 1.00, 0.33, 1.0}, {1.00, 1.00, 0.33, 1.0},
									 {0.33, 0.33, 1.00, 1.0}, {0.33, 1.00, 1.00, 1.0}, {1.00, 0.33, 1.00, 1.0}, {1.00, 1.00, 1.00, 1.0},
									 {1.00, 1.00, 1.00, 0.5}, {0.50, 0.50, 0.50, 1.0}};
	unsigned int		c;
	const float		   *f;
	while ((c = *mu->txt))
	{
		if (c == '^' && pr_checkextension.value)
		{ // parse markup like FTE/DP might.
			switch (mu->txt[1])
			{
			case '^': // doubled up char for escaping.
				mu->txt++;
				break;
			case '0': // black
			case '1': // red
			case '2': // green
			case '3': // yellow
			case '4': // blue
			case '5': // cyan
			case '6': // magenta
			case '7': // white
			case '8': // white+half-alpha
			case '9': // grey
				f = q3rgb[mu->txt[1] - '0'];
				mu->colour[0] = mu->tint[0] * f[0];
				mu->colour[1] = mu->tint[1] * f[1];
				mu->colour[2] = mu->tint[2] * f[2];
				mu->colour[3] = mu->tint[3] * f[3];
				mu->txt += 2;
				continue;
			case 'h': // toggle half-alpha
				if (mu->colour[3] != mu->tint[3] * 0.5)
					mu->colour[3] = mu->tint[3] * 0.5;
				else
					mu->colour[3] = mu->tint[3];
				mu->txt += 2;
				continue;
			case 'd': // reset to defaults (fixme: should reset ^m without resetting \1)
				mu->colour[0] = mu->tint[0];
				mu->colour[1] = mu->tint[1];
				mu->colour[2] = mu->tint[2];
				mu->colour[3] = mu->tint[3];
				mu->mask = 0;
				mu->txt += 2;
				break;
			case 'b': // blink
			case 's': // modstack push
			case 'r': // modstack restore
				mu->txt += 2;
				continue;
			case 'x': // RGB 12-bit colour
				if (ishex (mu->txt[2]) && ishex (mu->txt[3]) && ishex (mu->txt[4]))
				{
					mu->colour[0] = mu->tint[0] * dehex (mu->txt[2]) / 15.0;
					mu->colour[1] = mu->tint[1] * dehex (mu->txt[3]) / 15.0;
					mu->colour[2] = mu->tint[2] * dehex (mu->txt[4]) / 15.0;
					mu->txt += 5;
					continue;
				}
				break; // malformed
			case '[':  // start fte's ^[text\key\value\key\value^] links
			case ']':  // end link
				break; // fixme... skip the keys, recolour properly, etc
					   //				txt+=2;
					   //				continue;
			case '&':
				if ((ishex (mu->txt[2]) || mu->txt[2] == '-') && (ishex (mu->txt[3]) || mu->txt[3] == '-'))
				{ // ignore fte's fore/back ansi colours
					mu->txt += 4;
					continue;
				}
				break; // malformed
			case 'a':  // alternate charset (read: masked)...
			case 'm':  // toggle masking.
				mu->txt += 2;
				mu->mask ^= 128;
				continue;
			case 'U': // ucs-2 unicode codepoint
				if (ishex (mu->txt[2]) && ishex (mu->txt[3]) && ishex (mu->txt[4]) && ishex (mu->txt[5]))
				{
					c = (dehex (mu->txt[2]) << 12) | (dehex (mu->txt[3]) << 8) | (dehex (mu->txt[4]) << 4) | dehex (mu->txt[5]);
					mu->txt += 6;

					if (c >= 0xe000 && c <= 0xe0ff)
						c &= 0xff; // private-use 0xE0XX maps to quake's chars
					else if (c >= 0x20 && c <= 0x7f)
						c &= 0x7f; // ascii is okay too.
					else
						c = '?'; // otherwise its some unicode char that we don't know how to handle.
					return c;
				}
				break; // malformed
			case '{':  // full unicode codepoint, for chars up to 0x10ffff
				mu->txt += 2;
				c = 0; // no idea
				while (*mu->txt)
				{
					if (*mu->txt == '}')
					{
						mu->txt++;
						break;
					}
					if (!ishex (*mu->txt))
						break;
					c <<= 4;
					c |= dehex (*mu->txt++);
				}

				if (c >= 0xe000 && c <= 0xe0ff)
					c &= 0xff; // private-use 0xE0XX maps to quake's chars
				else if (c >= 0x20 && c <= 0x7f)
					c &= 0x7f; // ascii is okay too.
				// it would be nice to include a table to de-accent latin scripts, as well as translate cyrilic somehow, but not really necessary.
				else
					c = '?'; // otherwise its some unicode char that we don't know how to handle.
				return c;
			}
		}

		// regular char
		mu->txt++;
		return c | mu->mask;
	}
	return 0;
}

#define D(typestr, desc) typestr, desc

// #define fixme

// maths stuff
static void PF_Sin (void)
{
	G_FLOAT (OFS_RETURN) = sin (G_FLOAT (OFS_PARM0));
}
static void PF_asin (void)
{
	G_FLOAT (OFS_RETURN) = asin (G_FLOAT (OFS_PARM0));
}
static void PF_Cos (void)
{
	G_FLOAT (OFS_RETURN) = cos (G_FLOAT (OFS_PARM0));
}
static void PF_acos (void)
{
	G_FLOAT (OFS_RETURN) = acos (G_FLOAT (OFS_PARM0));
}
static void PF_tan (void)
{
	G_FLOAT (OFS_RETURN) = tan (G_FLOAT (OFS_PARM0));
}
static void PF_atan (void)
{
	G_FLOAT (OFS_RETURN) = atan (G_FLOAT (OFS_PARM0));
}
static void PF_atan2 (void)
{
	G_FLOAT (OFS_RETURN) = atan2 (G_FLOAT (OFS_PARM0), G_FLOAT (OFS_PARM1));
}
static void PF_Sqrt (void)
{
	G_FLOAT (OFS_RETURN) = sqrt (G_FLOAT (OFS_PARM0));
}
static void PF_pow (void)
{
	G_FLOAT (OFS_RETURN) = pow (G_FLOAT (OFS_PARM0), G_FLOAT (OFS_PARM1));
}
static void PF_Logarithm (void)
{
	// log2(v) = ln(v)/ln(2)
	double r;
	r = log (G_FLOAT (OFS_PARM0));
	if (qcvm->argc > 1)
		r /= log (G_FLOAT (OFS_PARM1));
	G_FLOAT (OFS_RETURN) = r;
}
static void PF_mod (void)
{
	float a = G_FLOAT (OFS_PARM0);
	float n = G_FLOAT (OFS_PARM1);

	if (n == 0)
	{
		Con_DWarning ("PF_mod: mod by zero\n");
		G_FLOAT (OFS_RETURN) = 0;
	}
	else
	{
		// because QC is inherantly floaty, lets use floats.
		G_FLOAT (OFS_RETURN) = a - (n * (int)(a / n));
	}
}
static void PF_min (void)
{
	float r = G_FLOAT (OFS_PARM0);
	int	  i;
	for (i = 1; i < qcvm->argc; i++)
	{
		if (r > G_FLOAT (OFS_PARM0 + i * 3))
			r = G_FLOAT (OFS_PARM0 + i * 3);
	}
	G_FLOAT (OFS_RETURN) = r;
}
static void PF_max (void)
{
	float r = G_FLOAT (OFS_PARM0);
	int	  i;
	for (i = 1; i < qcvm->argc; i++)
	{
		if (r < G_FLOAT (OFS_PARM0 + i * 3))
			r = G_FLOAT (OFS_PARM0 + i * 3);
	}
	G_FLOAT (OFS_RETURN) = r;
}
static void PF_bound (void)
{
	float minval = G_FLOAT (OFS_PARM0);
	float curval = G_FLOAT (OFS_PARM1);
	float maxval = G_FLOAT (OFS_PARM2);
	if (curval > maxval)
		curval = maxval;
	if (curval < minval)
		curval = minval;
	G_FLOAT (OFS_RETURN) = curval;
}
static void PF_anglemod (void)
{
	float v = G_FLOAT (OFS_PARM0);

	while (v >= 360)
		v = v - 360;
	while (v < 0)
		v = v + 360;

	G_FLOAT (OFS_RETURN) = v;
}
static void PF_bitshift (void)
{
	int bitmask = G_FLOAT (OFS_PARM0);
	int shift = G_FLOAT (OFS_PARM1);
	if (shift < 0)
		bitmask >>= -shift;
	else
		bitmask <<= shift;
	G_FLOAT (OFS_RETURN) = bitmask;
}
static void PF_crossproduct (void)
{
	CrossProduct (G_VECTOR (OFS_PARM0), G_VECTOR (OFS_PARM1), G_VECTOR (OFS_RETURN));
}
static void PF_vectorvectors (void)
{
	VectorCopy (G_VECTOR (OFS_PARM0), pr_global_struct->v_forward);
	VectorNormalize (pr_global_struct->v_forward);
	if (!pr_global_struct->v_forward[0] && !pr_global_struct->v_forward[1])
	{
		if (pr_global_struct->v_forward[2])
			pr_global_struct->v_right[1] = -1;
		else
			pr_global_struct->v_right[1] = 0;
		pr_global_struct->v_right[0] = pr_global_struct->v_right[2] = 0;
	}
	else
	{
		pr_global_struct->v_right[0] = pr_global_struct->v_forward[1];
		pr_global_struct->v_right[1] = -pr_global_struct->v_forward[0];
		pr_global_struct->v_right[2] = 0;
		VectorNormalize (pr_global_struct->v_right);
	}
	CrossProduct (pr_global_struct->v_right, pr_global_struct->v_forward, pr_global_struct->v_up);
}
static void PF_ext_vectoangles (void)
{ // alternative version of the original builtin, that can deal with roll angles too, by accepting an optional second argument for 'up'.
	float *value1, *up;

	value1 = G_VECTOR (OFS_PARM0);
	if (qcvm->argc >= 2)
		up = G_VECTOR (OFS_PARM1);
	else
		up = NULL;

	VectorAngles (value1, up, G_VECTOR (OFS_RETURN));
	G_VECTOR (OFS_RETURN)[PITCH] *= -1; // this builtin is for use with models. models have an inverted pitch. consistency with makevectors would never do!
}

// string stuff
static void PF_strlen (void)
{ // FIXME: doesn't try to handle utf-8
	const char *s = G_STRING (OFS_PARM0);
	G_FLOAT (OFS_RETURN) = strlen (s);
}
static void PF_strcat (void)
{
	int	   i;
	char  *out = PR_GetTempString ();
	size_t s;

	out[0] = 0;
	s = 0;
	for (i = 0; i < qcvm->argc; i++)
	{
		s = q_strlcat (out, G_STRING ((OFS_PARM0 + i * 3)), STRINGTEMP_LENGTH);
		if (s >= STRINGTEMP_LENGTH)
		{
			Con_Warning ("PF_strcat: overflow (string truncated)\n");
			break;
		}
	}

	G_INT (OFS_RETURN) = PR_SetEngineString (out);
}
static void PF_substring (void)
{
	int			start, length, slen;
	const char *s;
	char	   *string;

	s = G_STRING (OFS_PARM0);
	start = G_FLOAT (OFS_PARM1);
	length = G_FLOAT (OFS_PARM2);

	slen = strlen (s); // utf-8 should use chars, not bytes.

	if (start < 0)
		start = slen + start;
	if (length < 0)
		length = slen - start + (length + 1);
	if (start < 0)
	{
		//	length += start;
		start = 0;
	}

	if (start >= slen || length <= 0)
	{
		G_INT (OFS_RETURN) = PR_SetEngineString ("");
		return;
	}

	slen -= start;
	if (length > slen)
		length = slen;
	// utf-8 should switch to bytes now.
	s += start;

	if (length >= STRINGTEMP_LENGTH)
	{
		length = STRINGTEMP_LENGTH - 1;
		Con_Warning ("PF_substring: truncation\n");
	}

	string = PR_GetTempString ();
	memcpy (string, s, length);
	string[length] = '\0';
	G_INT (OFS_RETURN) = PR_SetEngineString (string);
}
/*our zoned strings implementation is somewhat specific to quakespasm, so good luck porting*/
static void PF_strzone (void)
{
	char	   *buf;
	size_t		len = 0;
	const char *s[8];
	size_t		l[8];
	int			i;
	size_t		id;

	for (i = 0; i < qcvm->argc; i++)
	{
		s[i] = G_STRING (OFS_PARM0 + i * 3);
		l[i] = strlen (s[i]);
		len += l[i];
	}
	len++; /*for the null*/

	buf = Mem_Alloc (len);
	G_INT (OFS_RETURN) = PR_SetEngineString (buf);
	id = -1 - G_INT (OFS_RETURN);
	if (id >= qcvm->knownzonesize)
	{
		int old_size = (qcvm->knownzonesize + 7) >> 3;
		qcvm->knownzonesize = (id + 32) & ~7;
		int new_size = (qcvm->knownzonesize + 7) >> 3;
		qcvm->knownzone = Mem_Realloc (qcvm->knownzone, new_size);
		memset (qcvm->knownzone + old_size, 0, new_size - old_size);
	}
	qcvm->knownzone[id >> 3] |= 1u << (id & 7);

	for (i = 0; i < qcvm->argc; i++)
	{
		memcpy (buf, s[i], l[i]);
		buf += l[i];
	}
	*buf = '\0';
}
static void PF_strunzone (void)
{
	size_t		id;
	const char *foo = G_STRING (OFS_PARM0);

	if (!G_INT (OFS_PARM0))
		return; // don't bug out if they gave a null string
	id = -1 - G_INT (OFS_PARM0);
	if (id < qcvm->knownzonesize && (qcvm->knownzone[id >> 3] & (1u << (id & 7))))
	{
		qcvm->knownzone[id >> 3] &= ~(1u << (id & 7));
		PR_ClearEngineString (G_INT (OFS_PARM0));
		Mem_Free ((void *)foo);
	}
	else
		Con_Warning ("PF_strunzone: string wasn't strzoned\n");
}
static void PR_UnzoneAll (void)
{ // called to clean up all zoned strings.
	while (qcvm->knownzonesize-- > 0)
	{
		size_t id = qcvm->knownzonesize;
		if (qcvm->knownzone[id >> 3] & (1u << (id & 7)))
		{
			string_t s = -1 - (int)id;
			char	*ptr = (char *)PR_GetString (s);
			PR_ClearEngineString (s);
			Mem_Free (ptr);
		}
	}
	if (qcvm->knownzone)
		Mem_Free (qcvm->knownzone);
	qcvm->knownzonesize = 0;
	qcvm->knownzone = NULL;
}
static qboolean qc_isascii (unsigned int u)
{
	if (u < 256) // should be just \n and 32-127, but we don't actually support any actual unicode and we don't really want to make things worse.
		return true;
	return false;
}
static void PF_str2chr (void)
{
	const char *instr = G_STRING (OFS_PARM0);
	int			ofs = (qcvm->argc > 1) ? G_FLOAT (OFS_PARM1) : 0;

	if (ofs < 0)
		ofs = strlen (instr) + ofs;

	if (ofs && (ofs < 0 || ofs > (int)strlen (instr)))
		G_FLOAT (OFS_RETURN) = '\0';
	else
		G_FLOAT (OFS_RETURN) = (unsigned char)instr[ofs];
}
static void PF_chr2str (void)
{
	char *ret = PR_GetTempString (), *out;
	int	  i;
	for (i = 0, out = ret; out - ret < STRINGTEMP_LENGTH - 6 && i < qcvm->argc; i++)
	{
		unsigned int u = G_FLOAT (OFS_PARM0 + i * 3);
		if (u >= 0xe000 && u < 0xe100)
			*out++ = (unsigned char)u; // quake chars.
		else if (qc_isascii (u))
			*out++ = u;
		else
			*out++ = '?'; // no unicode support
	}
	*out = 0;
	G_INT (OFS_RETURN) = PR_SetEngineString (ret);
}
// part of PF_strconv
static int chrconv_number (int i, int base, int conv)
{
	i -= base;
	switch (conv)
	{
	default:
	case 5:
	case 6:
	case 0:
		break;
	case 1:
		base = '0';
		break;
	case 2:
		base = '0' + 128;
		break;
	case 3:
		base = '0' - 30;
		break;
	case 4:
		base = '0' + 128 - 30;
		break;
	}
	return i + base;
}
// part of PF_strconv
static int chrconv_punct (int i, int base, int conv)
{
	i -= base;
	switch (conv)
	{
	default:
	case 0:
		break;
	case 1:
		base = 0;
		break;
	case 2:
		base = 128;
		break;
	}
	return i + base;
}
// part of PF_strconv
static int chrchar_alpha (int i, int basec, int baset, int convc, int convt, int charnum)
{
	// convert case and colour seperatly...

	i -= baset + basec;
	switch (convt)
	{
	default:
	case 0:
		break;
	case 1:
		baset = 0;
		break;
	case 2:
		baset = 128;
		break;

	case 5:
	case 6:
		baset = 128 * ((charnum & 1) == (convt - 5));
		break;
	}

	switch (convc)
	{
	default:
	case 0:
		break;
	case 1:
		basec = 'a';
		break;
	case 2:
		basec = 'A';
		break;
	}
	return i + basec + baset;
}
// FTE_STRINGS
// bulk convert a string. change case or colouring.
static void PF_strconv (void)
{
	int					 ccase = G_FLOAT (OFS_PARM0);	 // 0 same, 1 lower, 2 upper
	int					 redalpha = G_FLOAT (OFS_PARM1); // 0 same, 1 white, 2 red,  5 alternate, 6 alternate-alternate
	int					 rednum = G_FLOAT (OFS_PARM2);	 // 0 same, 1 white, 2 red, 3 redspecial, 4 whitespecial, 5 alternate, 6 alternate-alternate
	const unsigned char *string = (const unsigned char *)PF_VarString (3);
	int					 len = strlen ((const char *)string);
	int					 i;
	unsigned char		*resbuf = (unsigned char *)PR_GetTempString ();
	unsigned char		*result = resbuf;

	// UTF-8-FIXME: cope with utf+^U etc

	if (len >= STRINGTEMP_LENGTH)
		len = STRINGTEMP_LENGTH - 1;

	for (i = 0; i < len; i++, string++, result++) // should this be done backwards?
	{
		if (*string >= '0' && *string <= '9') // normal numbers...
			*result = chrconv_number (*string, '0', rednum);
		else if (*string >= '0' + 128 && *string <= '9' + 128)
			*result = chrconv_number (*string, '0' + 128, rednum);
		else if (*string >= '0' + 128 - 30 && *string <= '9' + 128 - 30)
			*result = chrconv_number (*string, '0' + 128 - 30, rednum);
		else if (*string >= '0' - 30 && *string <= '9' - 30)
			*result = chrconv_number (*string, '0' - 30, rednum);

		else if (*string >= 'a' && *string <= 'z') // normal numbers...
			*result = chrchar_alpha (*string, 'a', 0, ccase, redalpha, i);
		else if (*string >= 'A' && *string <= 'Z') // normal numbers...
			*result = chrchar_alpha (*string, 'A', 0, ccase, redalpha, i);
		else if (*string >= 'a' + 128 && *string <= 'z' + 128) // normal numbers...
			*result = chrchar_alpha (*string, 'a', 128, ccase, redalpha, i);
		else if (*string >= 'A' + 128 && *string <= 'Z' + 128) // normal numbers...
			*result = chrchar_alpha (*string, 'A', 128, ccase, redalpha, i);

		else if ((*string & 127) < 16 || !redalpha) // special chars..
			*result = *string;
		else if (*string < 128)
			*result = chrconv_punct (*string, 0, redalpha);
		else
			*result = chrconv_punct (*string, 128, redalpha);
	}
	*result = '\0';

	G_INT (OFS_RETURN) = PR_SetEngineString ((char *)resbuf);
}
static void PF_strpad (void)
{
	char	   *destbuf = PR_GetTempString ();
	char	   *dest = destbuf;
	int			pad = G_FLOAT (OFS_PARM0);
	const char *src = PF_VarString (1);

	// UTF-8-FIXME: pad is chars not bytes...

	if (pad < 0)
	{ // pad left
		pad = -pad - strlen (src);
		if (pad >= STRINGTEMP_LENGTH)
			pad = STRINGTEMP_LENGTH - 1;
		if (pad < 0)
			pad = 0;

		q_strlcpy (dest + pad, src, STRINGTEMP_LENGTH - pad);
		while (pad)
		{
			dest[--pad] = ' ';
		}
	}
	else
	{ // pad right
		if (pad >= STRINGTEMP_LENGTH)
			pad = STRINGTEMP_LENGTH - 1;
		pad -= strlen (src);
		if (pad < 0)
			pad = 0;

		q_strlcpy (dest, src, STRINGTEMP_LENGTH);
		dest += strlen (dest);

		while (pad-- > 0)
			*dest++ = ' ';
		*dest = '\0';
	}

	G_INT (OFS_RETURN) = PR_SetEngineString (destbuf);
}
static void PF_infoadd (void)
{
	const char *info = G_STRING (OFS_PARM0);
	const char *key = G_STRING (OFS_PARM1);
	const char *value = PF_VarString (2);
	char	   *destbuf = PR_GetTempString (), *o = destbuf, *e = destbuf + STRINGTEMP_LENGTH - 1;

	size_t keylen = strlen (key);
	size_t valuelen = strlen (value);
	if (!*key)
	{ // error
		G_INT (OFS_RETURN) = G_INT (OFS_PARM0);
		return;
	}

	// copy the string to the output, stripping the named key
	while (*info)
	{
		const char *l = info;
		if (*info++ != '\\')
			break; // error / end-of-string

		if (!strncmp (info, key, keylen) && info[keylen] == '\\')
		{
			// skip the key name
			info += keylen + 1;
			// this is the old value for the key. skip over it
			while (*info && *info != '\\')
				info++;
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

			// copy them over
			if (o + (info - l) >= e)
				break; // exceeds maximum length
			while (l < info)
				*o++ = *l++;
		}
	}

	if (*info)
		Con_Warning ("PF_infoadd: invalid source info\n");
	else if (!*value)
		; // nothing needed
	else if (!*key || strchr (key, '\\') || strchr (value, '\\'))
		Con_Warning ("PF_infoadd: invalid key/value\n");
	else if (o + 2 + keylen + valuelen >= e)
		Con_Warning ("PF_infoadd: length exceeds max\n");
	else
	{
		*o++ = '\\';
		memcpy (o, key, keylen);
		o += keylen;
		*o++ = '\\';
		memcpy (o, value, valuelen);
		o += valuelen;
	}

	*o = 0;
	G_INT (OFS_RETURN) = PR_SetEngineString (destbuf);
}
static void PF_infoget (void)
{
	const char *info = G_STRING (OFS_PARM0);
	const char *key = G_STRING (OFS_PARM1);
	size_t		keylen = strlen (key);
	while (*info)
	{
		if (*info++ != '\\')
			break; // error / end-of-string

		if (!strncmp (info, key, keylen) && info[keylen] == '\\')
		{
			char *destbuf = PR_GetTempString (), *o = destbuf, *e = destbuf + STRINGTEMP_LENGTH - 1;

			// skip the key name
			info += keylen + 1;
			// this is the old value for the key. copy it to the result
			while (*info && *info != '\\' && o < e)
				*o++ = *info++;
			*o++ = 0;

			// success!
			G_INT (OFS_RETURN) = PR_SetEngineString (destbuf);
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
	G_INT (OFS_RETURN) = 0;
}
static void PF_strncmp (void)
{
	const char *a = G_STRING (OFS_PARM0);
	const char *b = G_STRING (OFS_PARM1);

	if (qcvm->argc > 2)
	{
		int len = G_FLOAT (OFS_PARM2);
		int aofs = qcvm->argc > 3 ? G_FLOAT (OFS_PARM3) : 0;
		int bofs = qcvm->argc > 4 ? G_FLOAT (OFS_PARM4) : 0;
		if (aofs < 0 || (aofs && aofs > (int)strlen (a)))
			aofs = strlen (a);
		if (bofs < 0 || (bofs && bofs > (int)strlen (b)))
			bofs = strlen (b);
		G_FLOAT (OFS_RETURN) = strncmp (a + aofs, b, len);
	}
	else
		G_FLOAT (OFS_RETURN) = strcmp (a, b);
}
static void PF_strncasecmp (void)
{
	const char *a = G_STRING (OFS_PARM0);
	const char *b = G_STRING (OFS_PARM1);

	if (qcvm->argc > 2)
	{
		int len = G_FLOAT (OFS_PARM2);
		int aofs = qcvm->argc > 3 ? G_FLOAT (OFS_PARM3) : 0;
		int bofs = qcvm->argc > 4 ? G_FLOAT (OFS_PARM4) : 0;
		if (aofs < 0 || (aofs && aofs > (int)strlen (a)))
			aofs = strlen (a);
		if (bofs < 0 || (bofs && bofs > (int)strlen (b)))
			bofs = strlen (b);
		G_FLOAT (OFS_RETURN) = q_strncasecmp (a + aofs, b, len);
	}
	else
		G_FLOAT (OFS_RETURN) = q_strcasecmp (a, b);
}
static void PF_strstrofs (void)
{
	const char *instr = G_STRING (OFS_PARM0);
	const char *match = G_STRING (OFS_PARM1);
	int			firstofs = (qcvm->argc > 2) ? G_FLOAT (OFS_PARM2) : 0;

	if (firstofs && (firstofs < 0 || firstofs > (int)strlen (instr)))
	{
		G_FLOAT (OFS_RETURN) = -1;
		return;
	}

	match = strstr (instr + firstofs, match);
	if (!match)
		G_FLOAT (OFS_RETURN) = -1;
	else
		G_FLOAT (OFS_RETURN) = match - instr;
}
static void PF_strtrim (void)
{
	const char *str = G_STRING (OFS_PARM0);
	const char *end;
	char	   *news;
	size_t		len;

	// figure out the new start
	while (*str == ' ' || *str == '\t' || *str == '\n' || *str == '\r')
		str++;

	// figure out the new end.
	end = str + strlen (str);
	while (end > str && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\n' || end[-1] == '\r'))
		end--;

	// copy that substring into a tempstring.
	len = end - str;
	if (len >= STRINGTEMP_LENGTH)
		len = STRINGTEMP_LENGTH - 1;

	news = PR_GetTempString ();
	memcpy (news, str, len);
	news[len] = 0;

	G_INT (OFS_RETURN) = PR_SetEngineString (news);
}
static void PF_strreplace (void)
{
	char	   *resultbuf = PR_GetTempString ();
	char	   *result = resultbuf;
	const char *search = G_STRING (OFS_PARM0);
	const char *replace = G_STRING (OFS_PARM1);
	const char *subject = G_STRING (OFS_PARM2);
	int			searchlen = strlen (search);
	int			replacelen = strlen (replace);

	if (searchlen)
	{
		while (*subject && result < resultbuf + STRINGTEMP_LENGTH - replacelen - 2)
		{
			if (!strncmp (subject, search, searchlen))
			{
				subject += searchlen;
				memcpy (result, replace, replacelen);
				result += replacelen;
			}
			else
				*result++ = *subject++;
		}
		*result = 0;
		G_INT (OFS_RETURN) = PR_SetEngineString (resultbuf);
	}
	else
		G_INT (OFS_RETURN) = PR_SetEngineString (subject);
}
static void PF_strireplace (void)
{
	char	   *resultbuf = PR_GetTempString ();
	char	   *result = resultbuf;
	const char *search = G_STRING (OFS_PARM0);
	const char *replace = G_STRING (OFS_PARM1);
	const char *subject = G_STRING (OFS_PARM2);
	int			searchlen = strlen (search);
	int			replacelen = strlen (replace);

	if (searchlen)
	{
		while (*subject && result < resultbuf + sizeof (resultbuf) - replacelen - 2)
		{
			// UTF-8-FIXME: case insensitivity is awkward...
			if (!q_strncasecmp (subject, search, searchlen))
			{
				subject += searchlen;
				memcpy (result, replace, replacelen);
				result += replacelen;
			}
			else
				*result++ = *subject++;
		}
		*result = 0;
		G_INT (OFS_RETURN) = PR_SetEngineString (resultbuf);
	}
	else
		G_INT (OFS_RETURN) = PR_SetEngineString (subject);
}

static void PF_sprintf_internal (const char *s, int firstarg, char *outbuf, int outbuflen)
{
	const char	*s0;
	char		*o = outbuf, *end = outbuf + outbuflen, *err;
	int			 width, precision, thisarg, flags;
	char		 formatbuf[16];
	char		*f;
	int			 argpos = firstarg;
	int			 isfloat;
	static int	 dummyivec[3] = {0, 0, 0};
	static float dummyvec[3] = {0, 0, 0};

#define PRINTF_ALTERNATE	 1
#define PRINTF_ZEROPAD		 2
#define PRINTF_LEFT			 4
#define PRINTF_SPACEPOSITIVE 8
#define PRINTF_SIGNPOSITIVE	 16

	formatbuf[0] = '%';

#define GETARG_FLOAT(a)		(((a) >= firstarg && (a) < qcvm->argc) ? (G_FLOAT (OFS_PARM0 + 3 * (a))) : 0)
#define GETARG_VECTOR(a)	(((a) >= firstarg && (a) < qcvm->argc) ? (G_VECTOR (OFS_PARM0 + 3 * (a))) : dummyvec)
#define GETARG_INT(a)		(((a) >= firstarg && (a) < qcvm->argc) ? (G_INT (OFS_PARM0 + 3 * (a))) : 0)
#define GETARG_INTVECTOR(a) (((a) >= firstarg && (a) < qcvm->argc) ? ((int *)G_VECTOR (OFS_PARM0 + 3 * (a))) : dummyivec)
#define GETARG_STRING(a)	(((a) >= firstarg && (a) < qcvm->argc) ? (G_STRING (OFS_PARM0 + 3 * (a))) : "")

	for (;;)
	{
		s0 = s;
		switch (*s)
		{
		case 0:
			goto finished;
		case '%':
			++s;

			if (*s == '%')
				goto verbatim;

			// complete directive format:
			// %3$*1$.*2$ld

			width = -1;
			precision = -1;
			thisarg = -1;
			flags = 0;
			isfloat = -1;

			// is number following?
			if (*s >= '0' && *s <= '9')
			{
				width = strtol (s, &err, 10);
				if (!err)
				{
					Con_Warning ("PF_sprintf: bad format string: %s\n", s0);
					goto finished;
				}
				if (*err == '$')
				{
					thisarg = width + (firstarg - 1);
					width = -1;
					s = err + 1;
				}
				else
				{
					if (*s == '0')
					{
						flags |= PRINTF_ZEROPAD;
						if (width == 0)
							width = -1; // it was just a flag
					}
					s = err;
				}
			}

			if (width < 0)
			{
				for (;;)
				{
					switch (*s)
					{
					case '#':
						flags |= PRINTF_ALTERNATE;
						break;
					case '0':
						flags |= PRINTF_ZEROPAD;
						break;
					case '-':
						flags |= PRINTF_LEFT;
						break;
					case ' ':
						flags |= PRINTF_SPACEPOSITIVE;
						break;
					case '+':
						flags |= PRINTF_SIGNPOSITIVE;
						break;
					default:
						goto noflags;
					}
					++s;
				}
			noflags:
				if (*s == '*')
				{
					++s;
					if (*s >= '0' && *s <= '9')
					{
						width = strtol (s, &err, 10);
						if (!err || *err != '$')
						{
							Con_Warning ("PF_sprintf: invalid format string: %s\n", s0);
							goto finished;
						}
						s = err + 1;
					}
					else
						width = argpos++;
					width = GETARG_FLOAT (width);
					if (width < 0)
					{
						flags |= PRINTF_LEFT;
						width = -width;
					}
				}
				else if (*s >= '0' && *s <= '9')
				{
					width = strtol (s, &err, 10);
					if (!err)
					{
						Con_Warning ("PF_sprintf: invalid format string: %s\n", s0);
						goto finished;
					}
					s = err;
					if (width < 0)
					{
						flags |= PRINTF_LEFT;
						width = -width;
					}
				}
				// otherwise width stays -1
			}

			if (*s == '.')
			{
				++s;
				if (*s == '*')
				{
					++s;
					if (*s >= '0' && *s <= '9')
					{
						precision = strtol (s, &err, 10);
						if (!err || *err != '$')
						{
							Con_Warning ("PF_sprintf: invalid format string: %s\n", s0);
							goto finished;
						}
						s = err + 1;
					}
					else
						precision = argpos++;
					precision = GETARG_FLOAT (precision);
				}
				else if (*s >= '0' && *s <= '9')
				{
					precision = strtol (s, &err, 10);
					if (!err)
					{
						Con_Warning ("PF_sprintf: invalid format string: %s\n", s0);
						goto finished;
					}
					s = err;
				}
				else
				{
					Con_Warning ("PF_sprintf: invalid format string: %s\n", s0);
					goto finished;
				}
			}

			for (;;)
			{
				switch (*s)
				{
				case 'h':
					isfloat = 1;
					break;
				case 'l':
					isfloat = 0;
					break;
				case 'L':
					isfloat = 0;
					break;
				case 'j':
					break;
				case 'z':
					break;
				case 't':
					break;
				default:
					goto nolength;
				}
				++s;
			}
		nolength:

			// now s points to the final directive char and is no longer changed
			if (*s == 'p' || *s == 'P')
			{
				//%p is slightly different from %x.
				// always 8-bytes wide with 0 padding, always ints.
				flags |= PRINTF_ZEROPAD;
				if (width < 0)
					width = 8;
				if (isfloat < 0)
					isfloat = 0;
			}
			else if (*s == 'i')
			{
				//%i defaults to ints, not floats.
				if (isfloat < 0)
					isfloat = 0;
			}

			// assume floats, not ints.
			if (isfloat < 0)
				isfloat = 1;

			if (thisarg < 0)
				thisarg = argpos++;

			if (o < end - 1)
			{
				f = &formatbuf[1];
				if (*s != 's' && *s != 'c')
					if (flags & PRINTF_ALTERNATE)
						*f++ = '#';
				if (flags & PRINTF_ZEROPAD)
					*f++ = '0';
				if (flags & PRINTF_LEFT)
					*f++ = '-';
				if (flags & PRINTF_SPACEPOSITIVE)
					*f++ = ' ';
				if (flags & PRINTF_SIGNPOSITIVE)
					*f++ = '+';
				*f++ = '*';
				if (precision >= 0)
				{
					*f++ = '.';
					*f++ = '*';
				}
				if (*s == 'p')
					*f++ = 'x';
				else if (*s == 'P')
					*f++ = 'X';
				else if (*s == 'S')
					*f++ = 's';
				else
					*f++ = *s;
				*f++ = 0;

				if (width < 0) // not set
					width = 0;

				switch (*s)
				{
				case 'd':
				case 'i':
					if (precision < 0) // not set
						q_snprintf (o, end - o, formatbuf, width, (isfloat ? (int)GETARG_FLOAT (thisarg) : (int)GETARG_INT (thisarg)));
					else
						q_snprintf (o, end - o, formatbuf, width, precision, (isfloat ? (int)GETARG_FLOAT (thisarg) : (int)GETARG_INT (thisarg)));
					o += strlen (o);
					break;
				case 'o':
				case 'u':
				case 'x':
				case 'X':
				case 'p':
				case 'P':
					if (precision < 0) // not set
						q_snprintf (o, end - o, formatbuf, width, (isfloat ? (unsigned int)GETARG_FLOAT (thisarg) : (unsigned int)GETARG_INT (thisarg)));
					else
						q_snprintf (
							o, end - o, formatbuf, width, precision, (isfloat ? (unsigned int)GETARG_FLOAT (thisarg) : (unsigned int)GETARG_INT (thisarg)));
					o += strlen (o);
					break;
				case 'e':
				case 'E':
				case 'f':
				case 'F':
				case 'g':
				case 'G':
					if (precision < 0) // not set
						q_snprintf (o, end - o, formatbuf, width, (isfloat ? (double)GETARG_FLOAT (thisarg) : (double)GETARG_INT (thisarg)));
					else
						q_snprintf (o, end - o, formatbuf, width, precision, (isfloat ? (double)GETARG_FLOAT (thisarg) : (double)GETARG_INT (thisarg)));
					o += strlen (o);
					break;
				case 'v':
				case 'V':
					f[-2] += 'g' - 'v';
					if (precision < 0) // not set
						q_snprintf (
							o, end - o, va ("%s %s %s", /* NESTED SPRINTF IS NESTED */ formatbuf, formatbuf, formatbuf), width,
							(isfloat ? (double)GETARG_VECTOR (thisarg)[0] : (double)GETARG_INTVECTOR (thisarg)[0]), width,
							(isfloat ? (double)GETARG_VECTOR (thisarg)[1] : (double)GETARG_INTVECTOR (thisarg)[1]), width,
							(isfloat ? (double)GETARG_VECTOR (thisarg)[2] : (double)GETARG_INTVECTOR (thisarg)[2]));
					else
						q_snprintf (
							o, end - o, va ("%s %s %s", /* NESTED SPRINTF IS NESTED */ formatbuf, formatbuf, formatbuf), width, precision,
							(isfloat ? (double)GETARG_VECTOR (thisarg)[0] : (double)GETARG_INTVECTOR (thisarg)[0]), width, precision,
							(isfloat ? (double)GETARG_VECTOR (thisarg)[1] : (double)GETARG_INTVECTOR (thisarg)[1]), width, precision,
							(isfloat ? (double)GETARG_VECTOR (thisarg)[2] : (double)GETARG_INTVECTOR (thisarg)[2]));
					o += strlen (o);
					break;
				case 'c':
					// UTF-8-FIXME: figure it out yourself
					//							if(flags & PRINTF_ALTERNATE)
					{
						if (precision < 0) // not set
							q_snprintf (o, end - o, formatbuf, width, (isfloat ? (unsigned int)GETARG_FLOAT (thisarg) : (unsigned int)GETARG_INT (thisarg)));
						else
							q_snprintf (
								o, end - o, formatbuf, width, precision, (isfloat ? (unsigned int)GETARG_FLOAT (thisarg) : (unsigned int)GETARG_INT (thisarg)));
						o += strlen (o);
					}
					/*							else
												{
													unsigned int c = (isfloat ? (unsigned int) GETARG_FLOAT(thisarg) : (unsigned int)
					   GETARG_INT(thisarg)); char charbuf16[16]; const char *buf = u8_encodech(c, NULL, charbuf16); if(!buf) buf = ""; if(precision < 0)
					   // not set precision = end - o - 1; o += u8_strpad(o, end - o, buf, (flags & PRINTF_LEFT) != 0, width, precision);
												}
					*/
					break;
				case 'S':
				{ // tokenizable string
					const char *quotedarg = GETARG_STRING (thisarg);

					// try and escape it... hopefully it won't get truncated by precision limits...
					char   quotedbuf[65536];
					size_t l;
					l = strlen (quotedarg);
					if (strchr (quotedarg, '\"') || strchr (quotedarg, '\n') || strchr (quotedarg, '\r') || l + 3 >= sizeof (quotedbuf))
					{ // our escapes suck...
						Con_Warning ("PF_sprintf: unable to safely escape arg: %s\n", s0);
						quotedarg = "";
					}
					quotedbuf[0] = '\"';
					memcpy (quotedbuf + 1, quotedarg, l);
					quotedbuf[1 + l] = '\"';
					quotedbuf[1 + l + 1] = 0;
					quotedarg = quotedbuf;

					// UTF-8-FIXME: figure it out yourself
					//								if(flags & PRINTF_ALTERNATE)
					{
						if (precision < 0) // not set
							q_snprintf (o, end - o, formatbuf, width, quotedarg);
						else
							q_snprintf (o, end - o, formatbuf, width, precision, quotedarg);
						o += strlen (o);
					}
/*								else
								{
									if(precision < 0) // not set
										precision = end - o - 1;
									o += u8_strpad(o, end - o, quotedarg, (flags & PRINTF_LEFT) != 0, width, precision);
								}
*/							}
break;
case 's':
	// UTF-8-FIXME: figure it out yourself
	//							if(flags & PRINTF_ALTERNATE)
	{
		if (precision < 0) // not set
			q_snprintf (o, end - o, formatbuf, width, GETARG_STRING (thisarg));
		else
			q_snprintf (o, end - o, formatbuf, width, precision, GETARG_STRING (thisarg));
		o += strlen (o);
	}
	/*							else
								{
									if(precision < 0) // not set
										precision = end - o - 1;
									o += u8_strpad(o, end - o, GETARG_STRING(thisarg), (flags & PRINTF_LEFT) != 0, width, precision);
								}
	*/
	break;
default:
	Con_Warning ("PF_sprintf: invalid format string: %s\n", s0);
	goto finished;
}
			}
			++s;
			break;
		default:
		verbatim:
			if (o < end - 1)
*o++ = *s;
			s++;
			break;
		}
	}
finished:
	*o = 0;
}

static void PF_sprintf (void)
{
	char *outbuf = PR_GetTempString ();
	PF_sprintf_internal (G_STRING (OFS_PARM0), 1, outbuf, STRINGTEMP_LENGTH);
	G_INT (OFS_RETURN) = PR_SetEngineString (outbuf);
}

// string tokenizing (gah)
#define MAXQCTOKENS 64
static struct
{
	char		*token;
	unsigned int start;
	unsigned int end;
} qctoken[MAXQCTOKENS];
static unsigned int qctoken_count;

static void tokenize_flush (void)
{
	while (qctoken_count > 0)
	{
		qctoken_count--;
		Mem_Free (qctoken[qctoken_count].token);
	}
	qctoken_count = 0;
}

static void PF_ArgC (void)
{
	G_FLOAT (OFS_RETURN) = qctoken_count;
}

static int tokenizeqc (const char *str, qboolean dpfuckage)
{
	// FIXME: if dpfuckage, then we should handle punctuation specially, as well as /*.
	const char *start = str;
	while (qctoken_count > 0)
	{
		qctoken_count--;
		Mem_Free (qctoken[qctoken_count].token);
	}
	qctoken_count = 0;
	while (qctoken_count < MAXQCTOKENS)
	{
		/*skip whitespace here so the token's start is accurate*/
		while (*str && *(const unsigned char *)str <= ' ')
			str++;

		if (!*str)
			break;

		qctoken[qctoken_count].start = str - start;
		//		if (dpfuckage)
		//			str = COM_ParseDPFuckage(str);
		//		else
		str = COM_Parse (str);
		if (!str)
			break;

		qctoken[qctoken_count].token = q_strdup (com_token);

		qctoken[qctoken_count].end = str - start;
		qctoken_count++;
	}
	return qctoken_count;
}

/*KRIMZON_SV_PARSECLIENTCOMMAND added these two - note that for compatibility with DP, this tokenize builtin is veeery vauge and doesn't match the console*/
static void PF_Tokenize (void)
{
	G_FLOAT (OFS_RETURN) = tokenizeqc (G_STRING (OFS_PARM0), true);
}

static void PF_tokenize_console (void)
{
	G_FLOAT (OFS_RETURN) = tokenizeqc (G_STRING (OFS_PARM0), false);
}

static void PF_tokenizebyseparator (void)
{
	const char *str = G_STRING (OFS_PARM0);
	const char *sep[7];
	int			seplen[7];
	int			seps = 0, s;
	const char *start = str;
	int			tlen;
	qboolean	found = true;

	while (seps < qcvm->argc - 1 && seps < 7)
	{
		sep[seps] = G_STRING (OFS_PARM1 + seps * 3);
		seplen[seps] = strlen (sep[seps]);
		seps++;
	}

	tokenize_flush ();

	qctoken[qctoken_count].start = 0;
	if (*str)
		for (;;)
		{
			found = false;
			/*see if its a separator*/
			if (!*str)
			{
qctoken[qctoken_count].end = str - start;
found = true;
			}
			else
			{
for (s = 0; s < seps; s++)
{
	if (!strncmp (str, sep[s], seplen[s]))
	{
		qctoken[qctoken_count].end = str - start;
		str += seplen[s];
		found = true;
		break;
	}
}
			}
			/*it was, split it out*/
			if (found)
			{
tlen = qctoken[qctoken_count].end - qctoken[qctoken_count].start;
qctoken[qctoken_count].token = Mem_Alloc (tlen + 1);
memcpy (qctoken[qctoken_count].token, start + qctoken[qctoken_count].start, tlen);
qctoken[qctoken_count].token[tlen] = 0;

qctoken_count++;

if (*str && qctoken_count < MAXQCTOKENS)
	qctoken[qctoken_count].start = str - start;
else
	break;
			}
			str++;
		}
	G_FLOAT (OFS_RETURN) = qctoken_count;
}

static void PF_argv_start_index (void)
{
	int idx = G_FLOAT (OFS_PARM0);

	/*negative indexes are relative to the end*/
	if (idx < 0)
		idx += qctoken_count;

	if ((unsigned int)idx >= qctoken_count)
		G_FLOAT (OFS_RETURN) = -1;
	else
		G_FLOAT (OFS_RETURN) = qctoken[idx].start;
}

static void PF_argv_end_index (void)
{
	int idx = G_FLOAT (OFS_PARM0);

	/*negative indexes are relative to the end*/
	if (idx < 0)
		idx += qctoken_count;

	if ((unsigned int)idx >= qctoken_count)
		G_FLOAT (OFS_RETURN) = -1;
	else
		G_FLOAT (OFS_RETURN) = qctoken[idx].end;
}

static void PF_ArgV (void)
{
	int idx = G_FLOAT (OFS_PARM0);

	/*negative indexes are relative to the end*/
	if (idx < 0)
		idx += qctoken_count;

	if ((unsigned int)idx >= qctoken_count)
		G_INT (OFS_RETURN) = 0;
	else
	{
		char *ret = PR_GetTempString ();
		q_strlcpy (ret, qctoken[idx].token, STRINGTEMP_LENGTH);
		G_INT (OFS_RETURN) = PR_SetEngineString (ret);
	}
}

// conversions (mostly string)
static void PF_strtoupper (void)
{
	const char *in = G_STRING (OFS_PARM0);
	char	   *out, *result = PR_GetTempString ();
	for (out = result; *in && out < result + STRINGTEMP_LENGTH - 1;)
		*out++ = q_toupper (*in++);
	*out = 0;
	G_INT (OFS_RETURN) = PR_SetEngineString (result);
}
static void PF_strtolower (void)
{
	const char *in = G_STRING (OFS_PARM0);
	char	   *out, *result = PR_GetTempString ();
	for (out = result; *in && out < result + STRINGTEMP_LENGTH - 1;)
		*out++ = q_tolower (*in++);
	*out = 0;
	G_INT (OFS_RETURN) = PR_SetEngineString (result);
}
#include <time.h>
static void PF_strftime (void)
{
	const char *in = G_STRING (OFS_PARM1);
	char	   *result = PR_GetTempString ();

	time_t	   curtime;
	struct tm *tm;

	curtime = time (NULL);

	if (G_FLOAT (OFS_PARM0))
		tm = localtime (&curtime);
	else
		tm = gmtime (&curtime);

#ifdef _WIN32
	// msvc sucks. this is a crappy workaround.
	if (!strcmp (in, "%R"))
		in = "%H:%M";
	else if (!strcmp (in, "%F"))
		in = "%Y-%m-%d";
#endif

	strftime (result, STRINGTEMP_LENGTH, in, tm);

	G_INT (OFS_RETURN) = PR_SetEngineString (result);
}
static void PF_stof (void)
{
	G_FLOAT (OFS_RETURN) = atof (G_STRING (OFS_PARM0));
}
static void PF_stov (void)
{
	const char *s = G_STRING (OFS_PARM0);
	s = COM_Parse (s);
	G_VECTOR (OFS_RETURN)[0] = atof (com_token);
	s = COM_Parse (s);
	G_VECTOR (OFS_RETURN)[1] = atof (com_token);
	s = COM_Parse (s);
	G_VECTOR (OFS_RETURN)[2] = atof (com_token);
}
static void PF_stoi (void)
{
	G_INT (OFS_RETURN) = atoi (G_STRING (OFS_PARM0));
}
static void PF_itos (void)
{
	char *result = PR_GetTempString ();
	q_snprintf (result, STRINGTEMP_LENGTH, "%i", G_INT (OFS_PARM0));
	G_INT (OFS_RETURN) = PR_SetEngineString (result);
}
static void PF_etos (void)
{ // yes, this is lame
	char *result = PR_GetTempString ();
	q_snprintf (result, STRINGTEMP_LENGTH, "entity %i", G_EDICTNUM (OFS_PARM0));
	G_INT (OFS_RETURN) = PR_SetEngineString (result);
}
static void PF_stoh (void)
{
	G_INT (OFS_RETURN) = strtoul (G_STRING (OFS_PARM0), NULL, 16);
}
static void PF_htos (void)
{
	char *result = PR_GetTempString ();
	q_snprintf (result, STRINGTEMP_LENGTH, "%x", G_INT (OFS_PARM0));
	G_INT (OFS_RETURN) = PR_SetEngineString (result);
}
static void PF_ftoi (void)
{
	G_INT (OFS_RETURN) = G_FLOAT (OFS_PARM0);
}
static void PF_itof (void)
{
	G_FLOAT (OFS_RETURN) = G_INT (OFS_PARM0);
}

// collision stuff
static void PF_tracebox (void)
{ // alternative version of traceline that just passes on two extra args. trivial really.
	float	*v1, *mins, *maxs, *v2;
	trace_t	 trace;
	int		 nomonsters;
	edict_t *ent;

	v1 = G_VECTOR (OFS_PARM0);
	mins = G_VECTOR (OFS_PARM1);
	maxs = G_VECTOR (OFS_PARM2);
	v2 = G_VECTOR (OFS_PARM3);
	nomonsters = G_FLOAT (OFS_PARM4);
	ent = G_EDICT (OFS_PARM5);

	/* FIXME FIXME FIXME: Why do we hit this with certain progs.dat ?? */
	if (developer.value)
	{
		if (IS_NAN (v1[0]) || IS_NAN (v1[1]) || IS_NAN (v1[2]) || IS_NAN (v2[0]) || IS_NAN (v2[1]) || IS_NAN (v2[2]))
		{
			Con_Warning ("NAN in traceline:\nv1(%f %f %f) v2(%f %f %f)\nentity %d\n", v1[0], v1[1], v1[2], v2[0], v2[1], v2[2], NUM_FOR_EDICT (ent));
		}
	}

	if (IS_NAN (v1[0]) || IS_NAN (v1[1]) || IS_NAN (v1[2]))
		v1[0] = v1[1] = v1[2] = 0;
	if (IS_NAN (v2[0]) || IS_NAN (v2[1]) || IS_NAN (v2[2]))
		v2[0] = v2[1] = v2[2] = 0;

	trace = SV_Move (v1, mins, maxs, v2, nomonsters, ent);

	pr_global_struct->trace_allsolid = trace.allsolid;
	pr_global_struct->trace_startsolid = trace.startsolid;
	pr_global_struct->trace_fraction = trace.fraction;
	pr_global_struct->trace_inwater = trace.inwater;
	pr_global_struct->trace_inopen = trace.inopen;
	VectorCopy (trace.endpos, pr_global_struct->trace_endpos);
	VectorCopy (trace.plane.normal, pr_global_struct->trace_plane_normal);
	pr_global_struct->trace_plane_dist = trace.plane.dist;
	if (trace.ent)
		pr_global_struct->trace_ent = EDICT_TO_PROG (trace.ent);
	else
		pr_global_struct->trace_ent = EDICT_TO_PROG (qcvm->edicts);
}
static void PF_TraceToss (void)
{
	extern cvar_t sv_maxvelocity, sv_gravity;
	int			  i;
	float		  gravity;
	vec3_t		  move, end;
	trace_t		  trace;
	eval_t		 *val;

	vec3_t origin, velocity;

	edict_t *tossent, *ignore;
	tossent = G_EDICT (OFS_PARM0);
	if (tossent == qcvm->edicts)
		Con_Warning ("tracetoss: can not use world entity\n");
	ignore = G_EDICT (OFS_PARM1);

	val = GetEdictFieldValue (tossent, qcvm->extfields.gravity);
	if (val && val->_float)
		gravity = val->_float;
	else
		gravity = 1;
	gravity *= sv_gravity.value * 0.05;

	VectorCopy (tossent->v.origin, origin);
	VectorCopy (tossent->v.velocity, velocity);

	SV_CheckVelocity (tossent);

	for (i = 0; i < 200; i++) // LordHavoc: sanity check; never trace more than 10 seconds
	{
		velocity[2] -= gravity;
		VectorScale (velocity, 0.05, move);
		VectorAdd (origin, move, end);
		trace = SV_Move (origin, tossent->v.mins, tossent->v.maxs, end, MOVE_NORMAL, tossent);
		VectorCopy (trace.endpos, origin);

		if (trace.fraction < 1 && trace.ent && trace.ent != ignore)
			break;

		if (VectorLength (velocity) > sv_maxvelocity.value)
		{
			//			Con_DPrintf("Slowing %s\n", PR_GetString(w->progs, tossent->v->classname));
			VectorScale (velocity, sv_maxvelocity.value / VectorLength (velocity), velocity);
		}
	}

	trace.fraction = 0; // not relevant

	// and return those as globals.
	pr_global_struct->trace_allsolid = trace.allsolid;
	pr_global_struct->trace_startsolid = trace.startsolid;
	pr_global_struct->trace_fraction = trace.fraction;
	pr_global_struct->trace_inwater = trace.inwater;
	pr_global_struct->trace_inopen = trace.inopen;
	VectorCopy (trace.endpos, pr_global_struct->trace_endpos);
	VectorCopy (trace.plane.normal, pr_global_struct->trace_plane_normal);
	pr_global_struct->trace_plane_dist = trace.plane.dist;
	if (trace.ent)
		pr_global_struct->trace_ent = EDICT_TO_PROG (trace.ent);
	else
		pr_global_struct->trace_ent = EDICT_TO_PROG (qcvm->edicts);
}

// model stuff
void		SetMinMaxSize (edict_t *e, float *minvec, float *maxvec, qboolean rotate);
static void PF_sv_setmodelindex (void)
{
	edict_t		*e = G_EDICT (OFS_PARM0);
	unsigned int newidx = G_FLOAT (OFS_PARM1);
	qmodel_t	*mod = qcvm->GetModel (newidx);
	e->v.model = (newidx < MAX_MODELS) ? PR_SetEngineString (sv.model_precache[newidx]) : 0;
	e->v.modelindex = newidx;

	if (mod)
	// johnfitz -- correct physics cullboxes for bmodels
	{
		if (mod->type == mod_brush || !sv_gameplayfix_setmodelrealbox.value)
			SetMinMaxSize (e, mod->clipmins, mod->clipmaxs, true);
		else
			SetMinMaxSize (e, mod->mins, mod->maxs, true);
	}
	// johnfitz
	else
		SetMinMaxSize (e, vec3_origin, vec3_origin, true);
}
static void PF_cl_setmodelindex (void)
{
	edict_t	 *e = G_EDICT (OFS_PARM0);
	int		  newidx = G_FLOAT (OFS_PARM1);
	qmodel_t *mod = qcvm->GetModel (newidx);
	e->v.model = mod ? PR_SetEngineString (mod->name) : 0; // FIXME: is this going to cause issues with vid_restart?
	e->v.modelindex = newidx;

	if (mod)
	// johnfitz -- correct physics cullboxes for bmodels
	{
		if (mod->type == mod_brush || !sv_gameplayfix_setmodelrealbox.value)
			SetMinMaxSize (e, mod->clipmins, mod->clipmaxs, true);
		else
			SetMinMaxSize (e, mod->mins, mod->maxs, true);
	}
	// johnfitz
	else
		SetMinMaxSize (e, vec3_origin, vec3_origin, true);
}

static void PF_frameforname (void)
{
	unsigned int modelindex = G_FLOAT (OFS_PARM0);
	const char	*framename = G_STRING (OFS_PARM1);
	qmodel_t	*mod = qcvm->GetModel (modelindex);
	aliashdr_t	*alias;

	G_FLOAT (OFS_RETURN) = -1;
	if (mod && mod->type == mod_alias && (alias = Mod_Extradata (mod)))
	{
		int i;
		for (i = 0; i < alias->numframes; i++)
		{
			if (!strcmp (alias->frames[i].name, framename))
			{
G_FLOAT (OFS_RETURN) = i;
break;
			}
		}
	}
}
static void PF_frametoname (void)
{
	unsigned int modelindex = G_FLOAT (OFS_PARM0);
	unsigned int framenum = G_FLOAT (OFS_PARM1);
	qmodel_t	*mod = qcvm->GetModel (modelindex);
	aliashdr_t	*alias;

	if (mod && mod->type == mod_alias && (alias = Mod_Extradata (mod)) && framenum < (unsigned int)alias->numframes)
		G_INT (OFS_RETURN) = PR_SetEngineString (alias->frames[framenum].name);
	else
		G_INT (OFS_RETURN) = 0;
}
static void PF_frameduration (void)
{
	unsigned int modelindex = G_FLOAT (OFS_PARM0);
	unsigned int framenum = G_FLOAT (OFS_PARM1);
	qmodel_t	*mod = qcvm->GetModel (modelindex);
	aliashdr_t	*alias;

	if (mod && mod->type == mod_alias && (alias = Mod_Extradata (mod)) && framenum < (unsigned int)alias->numframes)
		G_FLOAT (OFS_RETURN) = alias->frames[framenum].numposes * alias->frames[framenum].interval;
}
static void PF_getsurfacenumpoints (void)
{
	edict_t		*ed = G_EDICT (OFS_PARM0);
	unsigned int surfidx = G_FLOAT (OFS_PARM1);
	unsigned int modelindex = ed->v.modelindex;
	qmodel_t	*mod = qcvm->GetModel (modelindex);

	if (mod && mod->type == mod_brush && !mod->needload && surfidx < (unsigned int)mod->nummodelsurfaces)
	{
		surfidx += mod->firstmodelsurface;
		G_FLOAT (OFS_RETURN) = mod->surfaces[surfidx].numedges;
	}
	else
		G_FLOAT (OFS_RETURN) = 0;
}
static mvertex_t *PF_getsurfacevertex (qmodel_t *mod, msurface_t *surf, unsigned int vert)
{
	signed int edge = mod->surfedges[vert + surf->firstedge];
	if (edge >= 0)
		return &mod->vertexes[mod->edges[edge].v[0]];
	else
		return &mod->vertexes[mod->edges[-edge].v[1]];
}
static void PF_getsurfacepoint (void)
{
	edict_t		*ed = G_EDICT (OFS_PARM0);
	unsigned int surfidx = G_FLOAT (OFS_PARM1);
	unsigned int point = G_FLOAT (OFS_PARM2);
	qmodel_t	*mod = qcvm->GetModel (ed->v.modelindex);

	if (mod && mod->type == mod_brush && !mod->needload && surfidx < (unsigned int)mod->nummodelsurfaces &&
		point < (unsigned int)mod->surfaces[surfidx].numedges)
	{
		mvertex_t *v = PF_getsurfacevertex (mod, &mod->surfaces[surfidx + mod->firstmodelsurface], point);
		VectorCopy (v->position, G_VECTOR (OFS_RETURN));
	}
	else
	{
		G_FLOAT (OFS_RETURN + 0) = 0;
		G_FLOAT (OFS_RETURN + 1) = 0;
		G_FLOAT (OFS_RETURN + 2) = 0;
	}
}
static void PF_getsurfacenumtriangles (void)
{ // for q3bsp compat (which this engine doesn't support, so its fairly simple)
	edict_t		*ed = G_EDICT (OFS_PARM0);
	unsigned int surfidx = G_FLOAT (OFS_PARM1);
	qmodel_t	*mod = qcvm->GetModel (ed->v.modelindex);

	if (mod && mod->type == mod_brush && !mod->needload && surfidx < (unsigned int)mod->nummodelsurfaces)
		G_FLOAT (OFS_RETURN) = (mod->surfaces[surfidx + mod->firstmodelsurface].numedges - 2); // q1bsp is only triangle fans
	else
		G_FLOAT (OFS_RETURN) = 0;
}
static void PF_getsurfacetriangle (void)
{ // for q3bsp compat (which this engine doesn't support, so its fairly simple)
	edict_t		*ed = G_EDICT (OFS_PARM0);
	unsigned int surfidx = G_FLOAT (OFS_PARM1);
	unsigned int triangleidx = G_FLOAT (OFS_PARM2);
	qmodel_t	*mod = qcvm->GetModel (ed->v.modelindex);

	if (mod && mod->type == mod_brush && !mod->needload && surfidx < (unsigned int)mod->nummodelsurfaces &&
		triangleidx < (unsigned int)mod->surfaces[surfidx].numedges - 2)
	{
		G_FLOAT (OFS_RETURN + 0) = 0;
		G_FLOAT (OFS_RETURN + 1) = triangleidx + 1;
		G_FLOAT (OFS_RETURN + 2) = triangleidx + 2;
	}
	else
	{
		G_FLOAT (OFS_RETURN + 0) = 0;
		G_FLOAT (OFS_RETURN + 1) = 0;
		G_FLOAT (OFS_RETURN + 2) = 0;
	}
}
static void PF_getsurfacenormal (void)
{
	edict_t		*ed = G_EDICT (OFS_PARM0);
	unsigned int surfidx = G_FLOAT (OFS_PARM1);
	qmodel_t	*mod = qcvm->GetModel (ed->v.modelindex);

	if (mod && mod->type == mod_brush && !mod->needload && surfidx < (unsigned int)mod->nummodelsurfaces)
	{
		surfidx += mod->firstmodelsurface;
		VectorCopy (mod->surfaces[surfidx].plane->normal, G_VECTOR (OFS_RETURN));
		if (mod->surfaces[surfidx].flags & SURF_PLANEBACK)
			VectorInverse (G_VECTOR (OFS_RETURN));
	}
	else
		G_FLOAT (OFS_RETURN) = 0;
}
static void PF_getsurfacetexture (void)
{
	edict_t		*ed = G_EDICT (OFS_PARM0);
	unsigned int surfidx = G_FLOAT (OFS_PARM1);
	qmodel_t	*mod = qcvm->GetModel (ed->v.modelindex);

	if (mod && mod->type == mod_brush && !mod->needload && surfidx < (unsigned int)mod->nummodelsurfaces)
	{
		surfidx += mod->firstmodelsurface;
		G_INT (OFS_RETURN) = PR_SetEngineString (mod->surfaces[surfidx].texinfo->texture->name);
	}
	else
		G_INT (OFS_RETURN) = 0;
}

#define TriangleNormal(a, b, c, n)                                                           \
	((n)[0] = ((a)[1] - (b)[1]) * ((c)[2] - (b)[2]) - ((a)[2] - (b)[2]) * ((c)[1] - (b)[1]), \
	 (n)[1] = ((a)[2] - (b)[2]) * ((c)[0] - (b)[0]) - ((a)[0] - (b)[0]) * ((c)[2] - (b)[2]), \
	 (n)[2] = ((a)[0] - (b)[0]) * ((c)[1] - (b)[1]) - ((a)[1] - (b)[1]) * ((c)[0] - (b)[0]))
static float getsurface_clippointpoly (qmodel_t *model, msurface_t *surf, vec3_t point, vec3_t bestcpoint, float bestdist, float *distsquare)
{
	int		   e, edge;
	vec3_t	   edgedir, edgenormal, cpoint, temp;
	mvertex_t *v1, *v2;
	float	   dist = DotProduct (point, surf->plane->normal) - surf->plane->dist;
	// don't care about SURF_PLANEBACK, the maths works out the same.

	*distsquare = dist * dist;

	if (*distsquare < bestdist)
	{ // within a specific range
		// make sure it's within the poly
		VectorMA (point, dist, surf->plane->normal, cpoint);
		for (e = surf->firstedge + surf->numedges; e > surf->firstedge; edge++)
		{
			edge = model->surfedges[--e];
			if (edge < 0)
			{
v1 = &model->vertexes[model->edges[-edge].v[0]];
v2 = &model->vertexes[model->edges[-edge].v[1]];
			}
			else
			{
v2 = &model->vertexes[model->edges[edge].v[0]];
v1 = &model->vertexes[model->edges[edge].v[1]];
			}

			VectorSubtract (v1->position, v2->position, edgedir);
			CrossProduct (edgedir, surf->plane->normal, edgenormal);
			if (!(surf->flags & SURF_PLANEBACK))
			{
VectorSubtract (vec3_origin, edgenormal, edgenormal);
			}
			VectorNormalize (edgenormal);

			dist = DotProduct (v1->position, edgenormal) - DotProduct (cpoint, edgenormal);
			if (dist < 0)
VectorMA (cpoint, dist, edgenormal, cpoint);
		}

		VectorSubtract (cpoint, point, temp);
		dist = DotProduct (temp, temp);
		if (dist < bestdist)
		{
			bestdist = dist;
			VectorCopy (cpoint, bestcpoint);
		}
	}
	return bestdist;
}

#define NEARSURFACE_MAXDIST				256
#define NEARSURFACE_CACHEDIST			384
#define NEARSURFACE_CACHESIZE			65536
#define NEARSURFACE_CACHEHITDISTSQUARED ((NEARSURFACE_CACHEDIST - NEARSURFACE_MAXDIST) * (NEARSURFACE_CACHEDIST - NEARSURFACE_MAXDIST))
int		 nearsurface_cache[NEARSURFACE_CACHESIZE];
int		 nearsurface_cache_entries;
vec3_t	 nearsurface_cache_point;
qboolean nearsurface_cache_valid;

// #438 float(entity e, vector p) getsurfacenearpoint (DP_QC_GETSURFACE)
static void PF_getsurfacenearpoint (void)
{
	qmodel_t   *model;
	edict_t	   *ent;
	msurface_t *surf;
	int			i;
	float	   *point;
	float		distsquare;
	qboolean	cached = false;
	qboolean	cacheable;

	vec3_t cpoint = {0, 0, 0};
	float  bestdist, dist;
	int	   bestsurf = -1;

	ent = G_EDICT (OFS_PARM0);
	point = G_VECTOR (OFS_PARM1);

	G_FLOAT (OFS_RETURN) = -1;

	model = qcvm->GetModel (ent->v.modelindex);

	if (!model || model->type != mod_brush || model->needload)
		return;

	bestdist = NEARSURFACE_MAXDIST;
	cacheable = ent->v.modelindex == 1;

	// all polies, we can skip parts. special case.
	surf = model->surfaces + model->firstmodelsurface;

	if (cacheable && nearsurface_cache_valid)
	{
		vec3_t cache_distance;
		VectorSubtract (point, nearsurface_cache_point, cache_distance);
		if (DotProduct (cache_distance, cache_distance) < NEARSURFACE_CACHEHITDISTSQUARED) // cached list can be used
		{
			for (i = 0; i < nearsurface_cache_entries; i++)
			{
dist = getsurface_clippointpoly (model, surf + nearsurface_cache[i], point, cpoint, bestdist, &distsquare);
if (dist < bestdist)
{
	bestdist = dist;
	bestsurf = nearsurface_cache[i];
}
			}
			cached = true;
		}
	}

	if (!cached)
	{
		if (cacheable)
		{
			nearsurface_cache_valid = true;
			memcpy (nearsurface_cache_point, point, sizeof (vec3_t));
			nearsurface_cache_entries = 0;
		}
		for (i = 0; i < model->nummodelsurfaces; i++, surf++)
		{
			dist = getsurface_clippointpoly (model, surf, point, cpoint, bestdist, &distsquare);
			if (dist < bestdist)
			{
bestdist = dist;
bestsurf = i;
			}
			if (cacheable && distsquare < NEARSURFACE_CACHEDIST * NEARSURFACE_CACHEDIST)
			{
if (nearsurface_cache_entries == NEARSURFACE_CACHESIZE)
	nearsurface_cache_valid = false;
else
	nearsurface_cache[nearsurface_cache_entries++] = i;
			}
		}
	}
#if 0 /* test cache by comparing with uncached exhaustive search */
	else if (cacheable)
	{
		int   cached_bestsurf = bestsurf;
		float cached_bestdist = bestdist;
		bestsurf = -1;
		bestdist = NEARSURFACE_MAXDIST;

		for (i = 0; i < model->nummodelsurfaces; i++, surf++)
		{
			dist = getsurface_clippointpoly (model, surf, point, cpoint, bestdist, &distsquare);
			if (dist < bestdist)
			{
				bestdist = dist;
				bestsurf = i;
			}
		}
		if (bestsurf != cached_bestsurf || bestdist != cached_bestdist)
			Con_Warning ("CACHE MISMATCH %d != %d, %f != %f\n", bestsurf, cached_bestsurf, bestdist, cached_bestdist);
	}
#endif

	G_FLOAT (OFS_RETURN) = bestsurf;
}

// #439 vector(entity e, float s, vector p) getsurfaceclippedpoint (DP_QC_GETSURFACE)
static void PF_getsurfaceclippedpoint (void)
{
	qmodel_t   *model;
	edict_t	   *ent;
	msurface_t *surf;
	float	   *point;
	int			surfnum;
	float		distsquared;

	float *result = G_VECTOR (OFS_RETURN);

	ent = G_EDICT (OFS_PARM0);
	surfnum = G_FLOAT (OFS_PARM1);
	point = G_VECTOR (OFS_PARM2);

	VectorCopy (point, result);

	model = qcvm->GetModel (ent->v.modelindex);

	if (!model || model->type != mod_brush || model->needload)
		return;
	if (surfnum >= model->nummodelsurfaces)
		return;

	// all polies, we can skip parts. special case.
	surf = model->surfaces + model->firstmodelsurface + surfnum;
	getsurface_clippointpoly (model, surf, point, result, FLT_MAX, &distsquared);
}

static void PF_getsurfacepointattribute (void)
{
	edict_t		*ed = G_EDICT (OFS_PARM0);
	unsigned int surfidx = G_FLOAT (OFS_PARM1);
	unsigned int point = G_FLOAT (OFS_PARM2);
	unsigned int attribute = G_FLOAT (OFS_PARM3);

	qmodel_t *mod = qcvm->GetModel (ed->v.modelindex);

	if (mod && mod->type == mod_brush && !mod->needload && surfidx < (unsigned int)mod->nummodelsurfaces &&
		point < (unsigned int)mod->surfaces[mod->firstmodelsurface + surfidx].numedges)
	{
		msurface_t *fa = &mod->surfaces[surfidx + mod->firstmodelsurface];
		mvertex_t  *v = PF_getsurfacevertex (mod, fa, point);
		switch (attribute)
		{
		default:
			Con_Warning ("PF_getsurfacepointattribute: attribute %u not supported\n", attribute);
			G_FLOAT (OFS_RETURN + 0) = 0;
			G_FLOAT (OFS_RETURN + 1) = 0;
			G_FLOAT (OFS_RETURN + 2) = 0;
			break;
		case 0: // xyz coord
			VectorCopy (v->position, G_VECTOR (OFS_RETURN));
			break;
		case 1: // s dir
		case 2: // t dir
		{
			// figure out how similar to the normal it is, and negate any influence, so that its perpendicular
			float sc = -DotProduct (fa->plane->normal, fa->texinfo->vecs[attribute - 1]);
			VectorMA (fa->texinfo->vecs[attribute - 1], sc, fa->plane->normal, G_VECTOR (OFS_RETURN));
			VectorNormalize (G_VECTOR (OFS_RETURN));
		}
		break;
		case 3: // normal
			VectorCopy (fa->plane->normal, G_VECTOR (OFS_RETURN));
			if (fa->flags & SURF_PLANEBACK)
VectorInverse (G_VECTOR (OFS_RETURN));
			break;
		case 4: // st coord
			G_FLOAT (OFS_RETURN + 0) = (DotProduct (v->position, fa->texinfo->vecs[0]) + fa->texinfo->vecs[0][3]) / fa->texinfo->texture->width;
			G_FLOAT (OFS_RETURN + 1) = (DotProduct (v->position, fa->texinfo->vecs[1]) + fa->texinfo->vecs[1][3]) / fa->texinfo->texture->height;
			G_FLOAT (OFS_RETURN + 2) = 0;
			break;
		case 5: // lmst coord, not actually very useful
			G_FLOAT (OFS_RETURN + 0) =
				(DotProduct (v->position, fa->texinfo->vecs[0]) + fa->texinfo->vecs[0][3] - fa->texturemins[0] + (fa->light_s + .5)) / LMBLOCK_WIDTH;
			G_FLOAT (OFS_RETURN + 1) =
				(DotProduct (v->position, fa->texinfo->vecs[1]) + fa->texinfo->vecs[1][3] - fa->texturemins[1] + (fa->light_t + .5)) / LMBLOCK_HEIGHT;
			G_FLOAT (OFS_RETURN + 2) = 0;
			break;
		case 6: // colour
			G_FLOAT (OFS_RETURN + 0) = 1;
			G_FLOAT (OFS_RETURN + 1) = 1;
			G_FLOAT (OFS_RETURN + 2) = 1;
			break;
		}
	}
	else
	{
		G_FLOAT (OFS_RETURN + 0) = 0;
		G_FLOAT (OFS_RETURN + 1) = 0;
		G_FLOAT (OFS_RETURN + 2) = 0;
	}
}
static void PF_sv_getlight (void)
{
	qmodel_t		   *om = cl.worldmodel;
	float			   *point = G_VECTOR (OFS_PARM0);
	static lightcache_t lc;

	cl.worldmodel = qcvm->worldmodel; // R_LightPoint is really clientside, so if its called from ssqc then try to make things work regardless
									  // FIXME: d_lightstylevalue isn't set on dedicated servers

	// FIXME: seems like quakespasm doesn't do lits for model lighting, so we won't either.
	vec3_t lightcolor;
	G_FLOAT (OFS_RETURN + 0) = G_FLOAT (OFS_RETURN + 1) = G_FLOAT (OFS_RETURN + 2) = R_LightPoint (point, 0.f, &lc, &lightcolor) / 255.0;

	cl.worldmodel = om;
}
#define PF_cl_getlight PF_sv_getlight

// server/client stuff
static void PF_checkcommand (void)
{
	const char *name = G_STRING (OFS_PARM0);
	if (Cmd_Exists (name))
		G_FLOAT (OFS_RETURN) = 1;
	else if (Cmd_AliasExists (name))
		G_FLOAT (OFS_RETURN) = 2;
	else if (Cvar_FindVar (name))
		G_FLOAT (OFS_RETURN) = 3;
	else
		G_FLOAT (OFS_RETURN) = 0;
}
static void PF_clientcommand (void)
{
	edict_t		*ed = G_EDICT (OFS_PARM0);
	const char	*str = G_STRING (OFS_PARM1);
	unsigned int i = NUM_FOR_EDICT (ed) - 1;
	if (i < (unsigned int)svs.maxclients && svs.clients[i].active)
	{
		client_t *ohc = host_client;
		host_client = &svs.clients[i];
		Cmd_ExecuteString (str, src_client);
		host_client = ohc;
	}
	else
		Con_Printf ("PF_clientcommand: not a client\n");
}
static void PF_clienttype (void)
{
	edict_t		*ed = G_EDICT (OFS_PARM0);
	unsigned int i = NUM_FOR_EDICT (ed) - 1;
	if (i >= (unsigned int)svs.maxclients)
	{
		G_FLOAT (OFS_RETURN) = 3; // CLIENTTYPE_NOTACLIENT
		return;
	}
	if (svs.clients[i].active)
	{
		if (svs.clients[i].netconnection)
			G_FLOAT (OFS_RETURN) = 1; // CLIENTTYPE_REAL;
		else
			G_FLOAT (OFS_RETURN) = 2; // CLIENTTYPE_BOT;
	}
	else
		G_FLOAT (OFS_RETURN) = 0; // CLIENTTYPE_DISCONNECTED;
}
static void PF_spawnclient (void)
{
	edict_t		*ent;
	unsigned int i;
	if (svs.maxclients)
		for (i = svs.maxclients; i-- > 0;)
		{
			if (!svs.clients[i].active)
			{
svs.clients[i].netconnection = NULL; // botclients have no net connection, obviously.
SV_ConnectClient (i);
svs.clients[i].spawned = true;
ent = svs.clients[i].edict;
memset (&ent->v, 0, qcvm->progs->entityfields * 4);
ent->v.colormap = NUM_FOR_EDICT (ent);
ent->v.team = (svs.clients[i].colors & 15) + 1;
ent->v.netname = PR_SetEngineString (svs.clients[i].name);
RETURN_EDICT (ent);
return;
			}
		}
	RETURN_EDICT (qcvm->edicts);
}
static void PF_dropclient (void)
{
	edict_t		*ed = G_EDICT (OFS_PARM0);
	unsigned int i = NUM_FOR_EDICT (ed) - 1;
	if (i < (unsigned int)svs.maxclients && svs.clients[i].active)
	{ // FIXME: should really set a flag or something, to avoid recursion issues.
		client_t *ohc = host_client;
		host_client = &svs.clients[i];
		SV_DropClient (false);
		host_client = ohc;
	}
}

// console/cvar stuff
static void PF_print (void)
{
	int i;
	for (i = 0; i < qcvm->argc; i++)
		Con_Printf ("%s", G_STRING (OFS_PARM0 + i * 3));
}
static void PF_cvar_string (void)
{
	const char *name = G_STRING (OFS_PARM0);
	cvar_t	   *var = Cvar_FindVar (name);
	if (var && var->string)
	{
		// cvars can easily change values.
		// this would result in leaks/exploits/slowdowns if the qc spams calls to cvar_string+changes.
		// so keep performance consistent, even if this is going to be slower.
		char *temp = PR_GetTempString ();
		q_strlcpy (temp, var->string, STRINGTEMP_LENGTH);
		G_INT (OFS_RETURN) = PR_SetEngineString (temp);
	}
	else if (!strcmp (name, "game"))
	{ // game looks like a cvar in most other respects (and is a cvar in fte). let cvar_string work on it as a way to find out the current gamedir.
		char *temp = PR_GetTempString ();
		q_strlcpy (temp, COM_GetGameNames (true), STRINGTEMP_LENGTH);
		G_INT (OFS_RETURN) = PR_SetEngineString (temp);
	}
	else
		G_INT (OFS_RETURN) = 0;
}
static void PF_cvar_defstring (void)
{
	const char *name = G_STRING (OFS_PARM0);
	cvar_t	   *var = Cvar_FindVar (name);
	if (var && var->default_string)
		G_INT (OFS_RETURN) = PR_SetEngineString (var->default_string);
	else
		G_INT (OFS_RETURN) = 0;
}
static void PF_cvar_type (void)
{
	const char *str = G_STRING (OFS_PARM0);
	int			ret = 0;
	cvar_t	   *v;

	v = Cvar_FindVar (str);
	if (v)
	{
		ret |= 1; // CVAR_EXISTS
		if (v->flags & CVAR_ARCHIVE)
			ret |= 2; // CVAR_TYPE_SAVED
					  //		if(v->flags & CVAR_NOTFROMSERVER)
					  //			ret |= 4; // CVAR_TYPE_PRIVATE
		if (!(v->flags & CVAR_USERDEFINED))
			ret |= 8; // CVAR_TYPE_ENGINE
					  //		if (v->description)
					  //			ret |= 16; // CVAR_TYPE_HASDESCRIPTION
	}
	G_FLOAT (OFS_RETURN) = ret;
}
static void PF_cvar_description (void)
{ // quakespasm does not support cvar descriptions. we provide this stub to avoid crashes.
	G_INT (OFS_RETURN) = 0;
}
static void PF_registercvar (void)
{
	const char *name = G_STRING (OFS_PARM0);
	const char *value = (qcvm->argc > 1) ? G_STRING (OFS_PARM0) : "";
	Cvar_Create (name, value);
}

// temp entities + networking
static void PF_WriteString2 (void)
{ // writes a string without the null. a poor-man's strcat.
	const char *string = G_STRING (OFS_PARM0);
	SZ_Write (WriteDest (), string, strlen (string));
}
static void PF_WriteFloat (void)
{ // curiously, this was missing in vanilla.
	MSG_WriteFloat (WriteDest (), G_FLOAT (OFS_PARM0));
}
static void PF_sv_te_blooddp (void)
{ // blood is common enough that we should emulate it for when engines do actually support it.
	float *org = G_VECTOR (OFS_PARM0);
	float *dir = G_VECTOR (OFS_PARM1);
	float  color = 73;
	float  count = G_FLOAT (OFS_PARM2);
	SV_StartParticle (org, dir, color, count);
}
static void PF_sv_te_bloodqw (void)
{ // qw tried to strip a lot.
	float *org = G_VECTOR (OFS_PARM0);
	float *dir = vec3_origin;
	float  color = 73;
	float  count = G_FLOAT (OFS_PARM1) * 20;
	SV_StartParticle (org, dir, color, count);
}
static void PF_sv_te_lightningblood (void)
{ // a qw builtin, to replace particle.
	float *org = G_VECTOR (OFS_PARM0);
	vec3_t dir = {0, 0, -100};
	float  color = 20;
	float  count = 225;
	SV_StartParticle (org, dir, color, count);
}
static void PF_sv_te_spike (void)
{
	float *org = G_VECTOR (OFS_PARM0);
	MSG_WriteByte (&sv.datagram, svc_temp_entity);
	MSG_WriteByte (&sv.datagram, TE_SPIKE);
	MSG_WriteCoord (&sv.datagram, org[0], sv.protocolflags);
	MSG_WriteCoord (&sv.datagram, org[1], sv.protocolflags);
	MSG_WriteCoord (&sv.datagram, org[2], sv.protocolflags);
	SV_Multicast (MULTICAST_PVS_U, org, 0, 0);
}
static void PF_cl_te_spike (void)
{
	float *pos = G_VECTOR (OFS_PARM0);

	if (PScript_RunParticleEffectTypeString (pos, NULL, 1, "TE_SPIKE"))
		R_RunParticleEffect (pos, vec3_origin, 0, 10);
	if (rand () % 5)
		S_StartSound (-1, 0, S_PrecacheSound ("weapons/tink1.wav"), pos, 1, 1);
	else
	{
		int rnd = rand () & 3;
		if (rnd == 1)
			S_StartSound (-1, 0, S_PrecacheSound ("weapons/ric1.wav"), pos, 1, 1);
		else if (rnd == 2)
			S_StartSound (-1, 0, S_PrecacheSound ("weapons/ric2.wav"), pos, 1, 1);
		else
			S_StartSound (-1, 0, S_PrecacheSound ("weapons/ric3.wav"), pos, 1, 1);
	}
}
static void PF_sv_te_superspike (void)
{
	float *org = G_VECTOR (OFS_PARM0);
	MSG_WriteByte (&sv.datagram, svc_temp_entity);
	MSG_WriteByte (&sv.datagram, TE_SUPERSPIKE);
	MSG_WriteCoord (&sv.datagram, org[0], sv.protocolflags);
	MSG_WriteCoord (&sv.datagram, org[1], sv.protocolflags);
	MSG_WriteCoord (&sv.datagram, org[2], sv.protocolflags);
	SV_Multicast (MULTICAST_PVS_U, org, 0, 0);
}
static void PF_cl_te_superspike (void)
{
	float *pos = G_VECTOR (OFS_PARM0);

	if (PScript_RunParticleEffectTypeString (pos, NULL, 1, "TE_SUPERSPIKE"))
		R_RunParticleEffect (pos, vec3_origin, 0, 20);

	if (rand () % 5)
		S_StartSound (-1, 0, S_PrecacheSound ("weapons/tink1.wav"), pos, 1, 1);
	else
	{
		int rnd = rand () & 3;
		if (rnd == 1)
			S_StartSound (-1, 0, S_PrecacheSound ("weapons/ric1.wav"), pos, 1, 1);
		else if (rnd == 2)
			S_StartSound (-1, 0, S_PrecacheSound ("weapons/ric2.wav"), pos, 1, 1);
		else
			S_StartSound (-1, 0, S_PrecacheSound ("weapons/ric3.wav"), pos, 1, 1);
	}
}
static void PF_sv_te_gunshot (void)
{
	float *org = G_VECTOR (OFS_PARM0);
	// float count = G_FLOAT(OFS_PARM1)*20;
	MSG_WriteByte (&sv.datagram, svc_temp_entity);
	MSG_WriteByte (&sv.datagram, TE_GUNSHOT);
	MSG_WriteCoord (&sv.datagram, org[0], sv.protocolflags);
	MSG_WriteCoord (&sv.datagram, org[1], sv.protocolflags);
	MSG_WriteCoord (&sv.datagram, org[2], sv.protocolflags);
	SV_Multicast (MULTICAST_PVS_U, org, 0, 0);
}
static void PF_cl_te_gunshot (void)
{
	float *pos = G_VECTOR (OFS_PARM0);

	int rnd = 20;
	if (PScript_RunParticleEffectTypeString (pos, NULL, rnd, "TE_GUNSHOT"))
		R_RunParticleEffect (pos, vec3_origin, 0, rnd);
}
static void PF_sv_te_explosion (void)
{
	float *org = G_VECTOR (OFS_PARM0);
	MSG_WriteByte (&sv.datagram, svc_temp_entity);
	MSG_WriteByte (&sv.datagram, TE_EXPLOSION);
	MSG_WriteCoord (&sv.datagram, org[0], sv.protocolflags);
	MSG_WriteCoord (&sv.datagram, org[1], sv.protocolflags);
	MSG_WriteCoord (&sv.datagram, org[2], sv.protocolflags);
	SV_Multicast (MULTICAST_PHS_U, org, 0, 0);
}
static void PF_cl_te_explosion (void)
{
	float *pos = G_VECTOR (OFS_PARM0);

	dlight_t *dl;
	if (PScript_RunParticleEffectTypeString (pos, NULL, 1, "TE_EXPLOSION"))
		R_ParticleExplosion (pos);
	dl = CL_AllocDlight (0);
	VectorCopy (pos, dl->origin);
	dl->radius = 350;
	dl->die = cl.time + 0.5;
	dl->decay = 300;
	S_StartSound (-1, 0, S_PrecacheSound ("weapons/r_exp3.wav"), pos, 1, 1);
}
static void PF_sv_te_tarexplosion (void)
{
	float *org = G_VECTOR (OFS_PARM0);
	MSG_WriteByte (&sv.datagram, svc_temp_entity);
	MSG_WriteByte (&sv.datagram, TE_TAREXPLOSION);
	MSG_WriteCoord (&sv.datagram, org[0], sv.protocolflags);
	MSG_WriteCoord (&sv.datagram, org[1], sv.protocolflags);
	MSG_WriteCoord (&sv.datagram, org[2], sv.protocolflags);
	SV_Multicast (MULTICAST_PHS_U, org, 0, 0);
}
static void PF_cl_te_tarexplosion (void)
{
	float *pos = G_VECTOR (OFS_PARM0);

	if (PScript_RunParticleEffectTypeString (pos, NULL, 1, "TE_TAREXPLOSION"))
		R_BlobExplosion (pos);
	S_StartSound (-1, 0, S_PrecacheSound ("weapons/r_exp3.wav"), pos, 1, 1);
}
static void PF_sv_te_lightning1 (void)
{
	edict_t *ed = G_EDICT (OFS_PARM0);
	float	*start = G_VECTOR (OFS_PARM1);
	float	*end = G_VECTOR (OFS_PARM2);
	MSG_WriteByte (&sv.datagram, svc_temp_entity);
	MSG_WriteByte (&sv.datagram, TE_LIGHTNING1);
	MSG_WriteShort (&sv.datagram, NUM_FOR_EDICT (ed));
	MSG_WriteCoord (&sv.datagram, start[0], sv.protocolflags);
	MSG_WriteCoord (&sv.datagram, start[1], sv.protocolflags);
	MSG_WriteCoord (&sv.datagram, start[2], sv.protocolflags);
	MSG_WriteCoord (&sv.datagram, end[0], sv.protocolflags);
	MSG_WriteCoord (&sv.datagram, end[1], sv.protocolflags);
	MSG_WriteCoord (&sv.datagram, end[2], sv.protocolflags);
	SV_Multicast (MULTICAST_PHS_U, start, 0, 0);
}
static void PF_cl_te_lightning1 (void)
{
	edict_t *ed = G_EDICT (OFS_PARM0);
	float	*start = G_VECTOR (OFS_PARM1);
	float	*end = G_VECTOR (OFS_PARM2);

	CL_UpdateBeam (Mod_ForName ("progs/bolt.mdl", true), "TE_LIGHTNING1", "TE_LIGHTNING1_END", -NUM_FOR_EDICT (ed), start, end);
}
static void PF_sv_te_lightning2 (void)
{
	edict_t *ed = G_EDICT (OFS_PARM0);
	float	*start = G_VECTOR (OFS_PARM1);
	float	*end = G_VECTOR (OFS_PARM2);
	MSG_WriteByte (&sv.datagram, svc_temp_entity);
	MSG_WriteByte (&sv.datagram, TE_LIGHTNING2);
	MSG_WriteShort (&sv.datagram, NUM_FOR_EDICT (ed));
	MSG_WriteCoord (&sv.datagram, start[0], sv.protocolflags);
	MSG_WriteCoord (&sv.datagram, start[1], sv.protocolflags);
	MSG_WriteCoord (&sv.datagram, start[2], sv.protocolflags);
	MSG_WriteCoord (&sv.datagram, end[0], sv.protocolflags);
	MSG_WriteCoord (&sv.datagram, end[1], sv.protocolflags);
	MSG_WriteCoord (&sv.datagram, end[2], sv.protocolflags);
	SV_Multicast (MULTICAST_PHS_U, start, 0, 0);
}
static void PF_cl_te_lightning2 (void)
{
	edict_t *ed = G_EDICT (OFS_PARM0);
	float	*start = G_VECTOR (OFS_PARM1);
	float	*end = G_VECTOR (OFS_PARM2);

	CL_UpdateBeam (Mod_ForName ("progs/bolt2.mdl", true), "TE_LIGHTNING2", "TE_LIGHTNING2_END", -NUM_FOR_EDICT (ed), start, end);
}
static void PF_sv_te_wizspike (void)
{
	float *org = G_VECTOR (OFS_PARM0);
	MSG_WriteByte (&sv.datagram, svc_temp_entity);
	MSG_WriteByte (&sv.datagram, TE_WIZSPIKE);
	MSG_WriteCoord (&sv.datagram, org[0], sv.protocolflags);
	MSG_WriteCoord (&sv.datagram, org[1], sv.protocolflags);
	MSG_WriteCoord (&sv.datagram, org[2], sv.protocolflags);
	SV_Multicast (MULTICAST_PHS_U, org, 0, 0);
}
static void PF_cl_te_wizspike (void)
{
	float *pos = G_VECTOR (OFS_PARM0);
	S_StartSound (-1, 0, S_PrecacheSound ("wizard/hit.wav"), pos, 1, 1);
}
static void PF_sv_te_knightspike (void)
{
	float *org = G_VECTOR (OFS_PARM0);
	MSG_WriteByte (&sv.datagram, svc_temp_entity);
	MSG_WriteByte (&sv.datagram, TE_KNIGHTSPIKE);
	MSG_WriteCoord (&sv.datagram, org[0], sv.protocolflags);
	MSG_WriteCoord (&sv.datagram, org[1], sv.protocolflags);
	MSG_WriteCoord (&sv.datagram, org[2], sv.protocolflags);
	SV_Multicast (MULTICAST_PHS_U, org, 0, 0);
}
static void PF_cl_te_knightspike (void)
{
	float *pos = G_VECTOR (OFS_PARM0);
	S_StartSound (-1, 0, S_PrecacheSound ("hknight/hit.wav"), pos, 1, 1);
}
static void PF_sv_te_lightning3 (void)
{
	edict_t *ed = G_EDICT (OFS_PARM0);
	float	*start = G_VECTOR (OFS_PARM1);
	float	*end = G_VECTOR (OFS_PARM2);
	MSG_WriteByte (&sv.datagram, svc_temp_entity);
	MSG_WriteByte (&sv.datagram, TE_LIGHTNING3);
	MSG_WriteShort (&sv.datagram, NUM_FOR_EDICT (ed));
	MSG_WriteCoord (&sv.datagram, start[0], sv.protocolflags);
	MSG_WriteCoord (&sv.datagram, start[1], sv.protocolflags);
	MSG_WriteCoord (&sv.datagram, start[2], sv.protocolflags);
	MSG_WriteCoord (&sv.datagram, end[0], sv.protocolflags);
	MSG_WriteCoord (&sv.datagram, end[1], sv.protocolflags);
	MSG_WriteCoord (&sv.datagram, end[2], sv.protocolflags);
	SV_Multicast (MULTICAST_PHS_U, start, 0, 0);
}
static void PF_cl_te_lightning3 (void)
{
	edict_t *ed = G_EDICT (OFS_PARM0);
	float	*start = G_VECTOR (OFS_PARM1);
	float	*end = G_VECTOR (OFS_PARM2);

	CL_UpdateBeam (Mod_ForName ("progs/bolt3.mdl", true), "TE_LIGHTNING3", "TE_LIGHTNING3_END", -NUM_FOR_EDICT (ed), start, end);
}
static void PF_sv_te_lavasplash (void)
{
	float *org = G_VECTOR (OFS_PARM0);
	MSG_WriteByte (&sv.datagram, svc_temp_entity);
	MSG_WriteByte (&sv.datagram, TE_LAVASPLASH);
	MSG_WriteCoord (&sv.datagram, org[0], sv.protocolflags);
	MSG_WriteCoord (&sv.datagram, org[1], sv.protocolflags);
	MSG_WriteCoord (&sv.datagram, org[2], sv.protocolflags);
	SV_Multicast (MULTICAST_PHS_U, org, 0, 0);
}
static void PF_cl_te_lavasplash (void) {}
static void PF_sv_te_teleport (void)
{
	float *org = G_VECTOR (OFS_PARM0);
	MSG_WriteByte (&sv.multicast, svc_temp_entity);
	MSG_WriteByte (&sv.multicast, TE_TELEPORT);
	MSG_WriteCoord (&sv.multicast, org[0], sv.protocolflags);
	MSG_WriteCoord (&sv.multicast, org[1], sv.protocolflags);
	MSG_WriteCoord (&sv.multicast, org[2], sv.protocolflags);
	SV_Multicast (MULTICAST_PHS_U, org, 0, 0);
}
static void PF_cl_te_teleport (void) {}
static void PF_sv_te_explosion2 (void)
{
	float *org = G_VECTOR (OFS_PARM0);
	int	   palstart = G_FLOAT (OFS_PARM1);
	int	   palcount = G_FLOAT (OFS_PARM1);
	MSG_WriteByte (&sv.multicast, svc_temp_entity);
	MSG_WriteByte (&sv.multicast, TE_EXPLOSION2);
	MSG_WriteCoord (&sv.multicast, org[0], sv.protocolflags);
	MSG_WriteCoord (&sv.multicast, org[1], sv.protocolflags);
	MSG_WriteCoord (&sv.multicast, org[2], sv.protocolflags);
	MSG_WriteByte (&sv.multicast, palstart);
	MSG_WriteByte (&sv.multicast, palcount);
	SV_Multicast (MULTICAST_PHS_U, org, 0, 0);
}
static void PF_cl_te_explosion2 (void)
{
	float	 *pos = G_VECTOR (OFS_PARM0);
	dlight_t *dl;

	dl = CL_AllocDlight (0);
	VectorCopy (pos, dl->origin);
	dl->radius = 350;
	dl->die = cl.time + 0.5;
	dl->decay = 300;
	S_StartSound (-1, 0, S_PrecacheSound ("weapons/r_exp3.wav"), pos, 1, 1);
}
static void PF_sv_te_beam (void)
{
	edict_t *ed = G_EDICT (OFS_PARM0);
	float	*start = G_VECTOR (OFS_PARM1);
	float	*end = G_VECTOR (OFS_PARM2);
	MSG_WriteByte (&sv.multicast, svc_temp_entity);
	MSG_WriteByte (&sv.multicast, TE_BEAM);
	MSG_WriteShort (&sv.multicast, NUM_FOR_EDICT (ed));
	MSG_WriteCoord (&sv.multicast, start[0], sv.protocolflags);
	MSG_WriteCoord (&sv.multicast, start[1], sv.protocolflags);
	MSG_WriteCoord (&sv.multicast, start[2], sv.protocolflags);
	MSG_WriteCoord (&sv.multicast, end[0], sv.protocolflags);
	MSG_WriteCoord (&sv.multicast, end[1], sv.protocolflags);
	MSG_WriteCoord (&sv.multicast, end[2], sv.protocolflags);
	SV_Multicast (MULTICAST_PHS_U, start, 0, 0);
}
static void PF_cl_te_beam (void)
{
	edict_t *ed = G_EDICT (OFS_PARM0);
	float	*start = G_VECTOR (OFS_PARM1);
	float	*end = G_VECTOR (OFS_PARM2);

	CL_UpdateBeam (Mod_ForName ("progs/beam.mdl", true), "TE_BEAM", "TE_BEAM_END", -NUM_FOR_EDICT (ed), start, end);
}
#ifdef PSET_SCRIPT
static void PF_sv_te_particlerain (void)
{
	float *min = G_VECTOR (OFS_PARM0);
	float *max = G_VECTOR (OFS_PARM1);
	float *velocity = G_VECTOR (OFS_PARM2);
	float  count = G_FLOAT (OFS_PARM3);
	float  colour = G_FLOAT (OFS_PARM4);

	if (count < 1)
		return;
	if (count > 65535)
		count = 65535;

	MSG_WriteByte (&sv.multicast, svc_temp_entity);
	MSG_WriteByte (&sv.multicast, TEDP_PARTICLERAIN);
	MSG_WriteCoord (&sv.multicast, min[0], sv.protocolflags);
	MSG_WriteCoord (&sv.multicast, min[1], sv.protocolflags);
	MSG_WriteCoord (&sv.multicast, min[2], sv.protocolflags);
	MSG_WriteCoord (&sv.multicast, max[0], sv.protocolflags);
	MSG_WriteCoord (&sv.multicast, max[1], sv.protocolflags);
	MSG_WriteCoord (&sv.multicast, max[2], sv.protocolflags);
	MSG_WriteCoord (&sv.multicast, velocity[0], sv.protocolflags);
	MSG_WriteCoord (&sv.multicast, velocity[1], sv.protocolflags);
	MSG_WriteCoord (&sv.multicast, velocity[2], sv.protocolflags);
	MSG_WriteShort (&sv.multicast, count);
	MSG_WriteByte (&sv.multicast, colour);

	SV_Multicast (MULTICAST_ALL_U, NULL, 0, PEXT2_REPLACEMENTDELTAS);
}
static void PF_sv_te_particlesnow (void)
{
	float *min = G_VECTOR (OFS_PARM0);
	float *max = G_VECTOR (OFS_PARM1);
	float *velocity = G_VECTOR (OFS_PARM2);
	float  count = G_FLOAT (OFS_PARM3);
	float  colour = G_FLOAT (OFS_PARM4);

	if (count < 1)
		return;
	if (count > 65535)
		count = 65535;

	MSG_WriteByte (&sv.multicast, svc_temp_entity);
	MSG_WriteByte (&sv.multicast, TEDP_PARTICLESNOW);
	MSG_WriteCoord (&sv.multicast, min[0], sv.protocolflags);
	MSG_WriteCoord (&sv.multicast, min[1], sv.protocolflags);
	MSG_WriteCoord (&sv.multicast, min[2], sv.protocolflags);
	MSG_WriteCoord (&sv.multicast, max[0], sv.protocolflags);
	MSG_WriteCoord (&sv.multicast, max[1], sv.protocolflags);
	MSG_WriteCoord (&sv.multicast, max[2], sv.protocolflags);
	MSG_WriteCoord (&sv.multicast, velocity[0], sv.protocolflags);
	MSG_WriteCoord (&sv.multicast, velocity[1], sv.protocolflags);
	MSG_WriteCoord (&sv.multicast, velocity[2], sv.protocolflags);
	MSG_WriteShort (&sv.multicast, count);
	MSG_WriteByte (&sv.multicast, colour);

	SV_Multicast (MULTICAST_ALL_U, NULL, 0, PEXT2_REPLACEMENTDELTAS);
}
#else
#define PF_sv_te_particlerain PF_void_stub
#define PF_sv_te_particlesnow PF_void_stub
#endif
#define PF_sv_te_bloodshower	PF_void_stub
#define PF_sv_te_explosionrgb	PF_void_stub
#define PF_sv_te_particlecube	PF_void_stub
#define PF_sv_te_spark			PF_void_stub
#define PF_sv_te_gunshotquad	PF_sv_te_gunshot
#define PF_sv_te_spikequad		PF_sv_te_spike
#define PF_sv_te_superspikequad PF_sv_te_superspike
#define PF_sv_te_explosionquad	PF_sv_te_explosion
#define PF_sv_te_smallflash		PF_void_stub
#define PF_sv_te_customflash	PF_void_stub
#define PF_sv_te_plasmaburn		PF_sv_te_tarexplosion
#define PF_sv_effect			PF_void_stub

static void PF_sv_pointsound (void)
{
	float	   *origin = G_VECTOR (OFS_PARM0);
	const char *sample = G_STRING (OFS_PARM1);
	float		volume = G_FLOAT (OFS_PARM2);
	float		attenuation = G_FLOAT (OFS_PARM3);
	SV_StartSound (qcvm->edicts, origin, 0, sample, volume, attenuation);
}
static void PF_cl_pointsound (void)
{
	float	   *origin = G_VECTOR (OFS_PARM0);
	const char *sample = G_STRING (OFS_PARM1);
	float		volume = G_FLOAT (OFS_PARM2);
	float		attenuation = G_FLOAT (OFS_PARM3);
	S_StartSound (0, 0, S_PrecacheSound (sample), origin, volume, attenuation);
}
static void PF_cl_soundlength (void)
{
	const char *sample = G_STRING (OFS_PARM0);
	sfx_t	   *sfx = S_PrecacheSound (sample);
	sfxcache_t *sc;
	G_FLOAT (OFS_RETURN) = 0;
	if (sfx)
	{
		sc = S_LoadSound (sfx);
		if (sc)
			G_FLOAT (OFS_RETURN) = (double)sc->length / sc->speed;
	}
}
static void PF_cl_localsound (void)
{
	const char *sample = G_STRING (OFS_PARM0);
	float		channel = (qcvm->argc > 1) ? G_FLOAT (OFS_PARM1) : -1;
	float		volume = (qcvm->argc > 2) ? G_FLOAT (OFS_PARM2) : 1;

	// FIXME: svc_setview or map changes can break sound replacements here.
	S_StartSound (cl.viewentity, channel, S_PrecacheSound (sample), vec3_origin, volume, 0);
}
// file stuff

static void PF_whichpack (void)
{
	const char	*fname = G_STRING (OFS_PARM0); // uses native paths, as this isn't actually reading anything.
	unsigned int path_id;
	if (COM_FileExists (fname, &path_id))
	{
		// FIXME: quakespasm reports which gamedir the file is in, but paks are hidden.
		// I'm too lazy to rewrite COM_FindFile, so I'm just going to hack something small to get the gamedir, just not the paks

		searchpath_t *path;
		for (path = com_searchpaths; path; path = path->next)
			if (!path->pack && path->path_id == path_id)
break; // okay, this one looks like one we can report

		// sandbox it by stripping the basedir
		fname = path->filename;
		if (!strncmp (fname, com_basedir, strlen (com_basedir)))
			fname += strlen (com_basedir);
		else
			fname = "?"; // no idea where this came from. something is screwing with us.
		while (*fname == '/' || *fname == '\\')
			fname++; // small cleanup, just in case
		G_INT (OFS_RETURN) = PR_SetEngineString (fname);
	}
	else
		G_INT (OFS_RETURN) = 0;
}

// string buffers

struct strbuf
{
	qcvm_t		*owningvm;
	char	   **strings;
	unsigned int used;
	unsigned int allocated;
};

#define BUFSTRBASE	  1
#define NUMSTRINGBUFS 64u
static struct strbuf strbuflist[NUMSTRINGBUFS];

static void PF_buf_shutdown (void)
{
	unsigned int i, bufno;

	for (bufno = 0; bufno < NUMSTRINGBUFS; bufno++)
	{
		if (strbuflist[bufno].owningvm != qcvm)
			continue;

		for (i = 0; i < strbuflist[bufno].used; i++)
			Mem_Free (strbuflist[bufno].strings[i]);
		Mem_Free (strbuflist[bufno].strings);

		strbuflist[bufno].owningvm = NULL;
		strbuflist[bufno].strings = NULL;
		strbuflist[bufno].used = 0;
		strbuflist[bufno].allocated = 0;
	}
}

// #440 float() buf_create (DP_QC_STRINGBUFFERS)
static void PF_buf_create (void)
{
	unsigned int i;

	const char *type = ((qcvm->argc > 0) ? G_STRING (OFS_PARM0) : "string");
	//	unsigned int flags = ((pr_argc>1)?G_FLOAT(OFS_PARM1):1);

	if (!q_strcasecmp (type, "string"))
		;
	else
	{
		G_FLOAT (OFS_RETURN) = -1;
		return;
	}

	// flags&1 == saved. apparently.

	for (i = 0; i < NUMSTRINGBUFS; i++)
	{
		if (!strbuflist[i].owningvm)
		{
			strbuflist[i].owningvm = qcvm;
			strbuflist[i].used = 0;
			strbuflist[i].allocated = 0;
			strbuflist[i].strings = NULL;
			G_FLOAT (OFS_RETURN) = i + BUFSTRBASE;
			return;
		}
	}
	G_FLOAT (OFS_RETURN) = -1;
}
// #441 void(float bufhandle) buf_del (DP_QC_STRINGBUFFERS)
static void PF_buf_del (void)
{
	unsigned int i;
	unsigned int bufno = G_FLOAT (OFS_PARM0) - BUFSTRBASE;

	if (bufno >= NUMSTRINGBUFS)
		return;
	if (!strbuflist[bufno].owningvm)
		return;

	for (i = 0; i < strbuflist[bufno].used; i++)
	{
		if (strbuflist[bufno].strings[i])
			Mem_Free (strbuflist[bufno].strings[i]);
	}
	Mem_Free (strbuflist[bufno].strings);

	strbuflist[bufno].strings = NULL;
	strbuflist[bufno].used = 0;
	strbuflist[bufno].allocated = 0;

	strbuflist[bufno].owningvm = NULL;
}
// #442 float(float bufhandle) buf_getsize (DP_QC_STRINGBUFFERS)
static void PF_buf_getsize (void)
{
	int bufno = G_FLOAT (OFS_PARM0) - BUFSTRBASE;

	if ((unsigned int)bufno >= NUMSTRINGBUFS)
		return;
	if (!strbuflist[bufno].owningvm)
		return;

	G_FLOAT (OFS_RETURN) = strbuflist[bufno].used;
}
// #443 void(float bufhandle_from, float bufhandle_to) buf_copy (DP_QC_STRINGBUFFERS)
static void PF_buf_copy (void)
{
	unsigned int buffrom = G_FLOAT (OFS_PARM0) - BUFSTRBASE;
	unsigned int bufto = G_FLOAT (OFS_PARM1) - BUFSTRBASE;
	unsigned int i;

	if (bufto == buffrom) // err...
		return;
	if (buffrom >= NUMSTRINGBUFS)
		return;
	if (!strbuflist[buffrom].owningvm)
		return;
	if (bufto >= NUMSTRINGBUFS)
		return;
	if (!strbuflist[bufto].owningvm)
		return;

	// obliterate any and all existing data.
	for (i = 0; i < strbuflist[bufto].used; i++)
		if (strbuflist[bufto].strings[i])
			Mem_Free (strbuflist[bufto].strings[i]);
	Mem_Free (strbuflist[bufto].strings);

	// copy new data over.
	strbuflist[bufto].used = strbuflist[bufto].allocated = strbuflist[buffrom].used;
	strbuflist[bufto].strings = Mem_Alloc (strbuflist[buffrom].used * sizeof (char *));
	for (i = 0; i < strbuflist[buffrom].used; i++)
		strbuflist[bufto].strings[i] = strbuflist[buffrom].strings[i] ? q_strdup (strbuflist[buffrom].strings[i]) : NULL;
}
static int PF_buf_sort_sortprefixlen;
static int PF_buf_sort_ascending (const void *a, const void *b)
{
	return strncmp (*(char *const *)a, *(char *const *)b, PF_buf_sort_sortprefixlen);
}
static int PF_buf_sort_descending (const void *b, const void *a)
{
	return strncmp (*(char *const *)a, *(char *const *)b, PF_buf_sort_sortprefixlen);
}
// #444 void(float bufhandle, float sortprefixlen, float backward) buf_sort (DP_QC_STRINGBUFFERS)
static void PF_buf_sort (void)
{
	int			 bufno = G_FLOAT (OFS_PARM0) - BUFSTRBASE;
	int			 sortprefixlen = G_FLOAT (OFS_PARM1);
	int			 backwards = G_FLOAT (OFS_PARM2);
	unsigned int s, d;
	char	   **strings;

	if ((unsigned int)bufno >= NUMSTRINGBUFS)
		return;
	if (!strbuflist[bufno].owningvm)
		return;

	if (sortprefixlen <= 0)
		sortprefixlen = 0x7fffffff;

	// take out the nulls first, to avoid weird/crashy sorting
	for (s = 0, d = 0, strings = strbuflist[bufno].strings; s < strbuflist[bufno].used;)
	{
		if (!strings[s])
		{
			s++;
			continue;
		}
		strings[d++] = strings[s++];
	}
	strbuflist[bufno].used = d;

	// no nulls now, sort it.
	PF_buf_sort_sortprefixlen = sortprefixlen; // eww, a global. burn in hell.
	if (backwards)							   // z first
		qsort (strings, strbuflist[bufno].used, sizeof (char *), PF_buf_sort_descending);
	else // a first
		qsort (strings, strbuflist[bufno].used, sizeof (char *), PF_buf_sort_ascending);
}
// #445 string(float bufhandle, string glue) buf_implode (DP_QC_STRINGBUFFERS)
static void PF_buf_implode (void)
{
	int			 bufno = G_FLOAT (OFS_PARM0) - BUFSTRBASE;
	const char	*glue = G_STRING (OFS_PARM1);
	unsigned int gluelen = strlen (glue);
	unsigned int retlen, l, i;
	char	   **strings;
	char		*ret;

	if ((unsigned int)bufno >= NUMSTRINGBUFS)
		return;
	if (!strbuflist[bufno].owningvm)
		return;

	// count neededlength
	strings = strbuflist[bufno].strings;
	/*
	for (i = 0, retlen = 0; i < strbuflist[bufno].used; i++)
	{
		if (strings[i])
		{
			if (retlen)
				retlen += gluelen;
			retlen += strlen(strings[i]);
		}
	}
	ret = malloc(retlen+1);*/

	// generate the output
	ret = PR_GetTempString ();
	for (i = 0, retlen = 0; i < strbuflist[bufno].used; i++)
	{
		if (strings[i])
		{
			if (retlen)
			{
if (retlen + gluelen + 1 > STRINGTEMP_LENGTH)
{
	Con_Printf ("PF_buf_implode: tempstring overflow\n");
	break;
}
memcpy (ret + retlen, glue, gluelen);
retlen += gluelen;
			}
			l = strlen (strings[i]);
			if (retlen + l + 1 > STRINGTEMP_LENGTH)
			{
Con_Printf ("PF_buf_implode: tempstring overflow\n");
break;
			}
			memcpy (ret + retlen, strings[i], l);
			retlen += l;
		}
	}

	// add the null and return
	ret[retlen] = 0;
	G_INT (OFS_RETURN) = PR_SetEngineString (ret);
}
// #446 string(float bufhandle, float string_index) bufstr_get (DP_QC_STRINGBUFFERS)
static void PF_bufstr_get (void)
{
	unsigned int bufno = G_FLOAT (OFS_PARM0) - BUFSTRBASE;
	unsigned int index = G_FLOAT (OFS_PARM1);
	char		*ret;

	if (bufno >= NUMSTRINGBUFS)
	{
		G_INT (OFS_RETURN) = 0;
		return;
	}
	if (!strbuflist[bufno].owningvm)
	{
		G_INT (OFS_RETURN) = 0;
		return;
	}

	if (index >= strbuflist[bufno].used)
	{
		G_INT (OFS_RETURN) = 0;
		return;
	}

	if (strbuflist[bufno].strings[index])
	{
		ret = PR_GetTempString ();
		q_strlcpy (ret, strbuflist[bufno].strings[index], STRINGTEMP_LENGTH);
		G_INT (OFS_RETURN) = PR_SetEngineString (ret);
	}
	else
		G_INT (OFS_RETURN) = 0;
}
// #447 void(float bufhandle, float string_index, string str) bufstr_set (DP_QC_STRINGBUFFERS)
static void PF_bufstr_set (void)
{
	unsigned int bufno = G_FLOAT (OFS_PARM0) - BUFSTRBASE;
	unsigned int index = G_FLOAT (OFS_PARM1);
	const char	*string = G_STRING (OFS_PARM2);
	unsigned int oldcount;

	if ((unsigned int)bufno >= NUMSTRINGBUFS)
		return;
	if (!strbuflist[bufno].owningvm)
		return;

	if (index >= strbuflist[bufno].allocated)
	{
		oldcount = strbuflist[bufno].allocated;
		strbuflist[bufno].allocated = (index + 256);
		strbuflist[bufno].strings = Mem_Realloc (strbuflist[bufno].strings, strbuflist[bufno].allocated * sizeof (char *));
		memset (strbuflist[bufno].strings + oldcount, 0, (strbuflist[bufno].allocated - oldcount) * sizeof (char *));
	}
	if (strbuflist[bufno].strings[index])
		Mem_Free (strbuflist[bufno].strings[index]);
	strbuflist[bufno].strings[index] = Mem_Alloc (strlen (string) + 1);
	strcpy (strbuflist[bufno].strings[index], string);

	if (index >= strbuflist[bufno].used)
		strbuflist[bufno].used = index + 1;
}

static int PF_bufstr_add_internal (unsigned int bufno, const char *string, int appendonend)
{
	unsigned int index;
	if (appendonend)
	{
		// add on end
		index = strbuflist[bufno].used;
	}
	else
	{
		// find a hole
		for (index = 0; index < strbuflist[bufno].used; index++)
			if (!strbuflist[bufno].strings[index])
break;
	}

	// expand it if needed
	if (index >= strbuflist[bufno].allocated)
	{
		unsigned int oldcount;
		oldcount = strbuflist[bufno].allocated;
		strbuflist[bufno].allocated = (index + 256);
		strbuflist[bufno].strings = Mem_Realloc (strbuflist[bufno].strings, strbuflist[bufno].allocated * sizeof (char *));
		memset (strbuflist[bufno].strings + oldcount, 0, (strbuflist[bufno].allocated - oldcount) * sizeof (char *));
	}

	// add in the new string.
	if (strbuflist[bufno].strings[index])
		Mem_Free (strbuflist[bufno].strings[index]);
	strbuflist[bufno].strings[index] = Mem_Alloc (strlen (string) + 1);
	strcpy (strbuflist[bufno].strings[index], string);

	if (index >= strbuflist[bufno].used)
		strbuflist[bufno].used = index + 1;

	return index;
}

// #448 float(float bufhandle, string str, float order) bufstr_add (DP_QC_STRINGBUFFERS)
static void PF_bufstr_add (void)
{
	size_t		bufno = G_FLOAT (OFS_PARM0) - BUFSTRBASE;
	const char *string = G_STRING (OFS_PARM1);
	qboolean	ordered = G_FLOAT (OFS_PARM2);

	if ((unsigned int)bufno >= NUMSTRINGBUFS)
		return;
	if (!strbuflist[bufno].owningvm)
		return;

	G_FLOAT (OFS_RETURN) = PF_bufstr_add_internal (bufno, string, ordered);
}
// #449 void(float bufhandle, float string_index) bufstr_free (DP_QC_STRINGBUFFERS)
static void PF_bufstr_free (void)
{
	size_t bufno = G_FLOAT (OFS_PARM0) - BUFSTRBASE;
	size_t index = G_FLOAT (OFS_PARM1);

	if ((unsigned int)bufno >= NUMSTRINGBUFS)
		return;
	if (!strbuflist[bufno].owningvm)
		return;

	if (index >= strbuflist[bufno].used)
		return; // not valid anyway.

	if (strbuflist[bufno].strings[index])
		Mem_Free (strbuflist[bufno].strings[index]);
	strbuflist[bufno].strings[index] = NULL;
}

static void PF_buf_cvarlist (void)
{
	size_t		 bufno = G_FLOAT (OFS_PARM0) - BUFSTRBASE;
	const char	*pattern = G_STRING (OFS_PARM1);
	const char	*antipattern = G_STRING (OFS_PARM2);
	unsigned int i;
	cvar_t		*var;
	int			 plen = strlen (pattern), alen = strlen (antipattern);
	qboolean	 pwc = strchr (pattern, '*') || strchr (pattern, '?'), awc = strchr (antipattern, '*') || strchr (antipattern, '?');

	if ((unsigned int)bufno >= NUMSTRINGBUFS)
		return;
	if (!strbuflist[bufno].owningvm)
		return;

	// obliterate any and all existing data.
	for (i = 0; i < strbuflist[bufno].used; i++)
		if (strbuflist[bufno].strings[i])
			Mem_Free (strbuflist[bufno].strings[i]);
	if (strbuflist[bufno].strings)
		Mem_Free (strbuflist[bufno].strings);
	strbuflist[bufno].used = strbuflist[bufno].allocated = 0;

	for (var = Cvar_FindVarAfter ("", CVAR_NONE); var; var = var->next)
	{
		if (plen && (pwc ? !wildcmp (pattern, var->name) : strncmp (var->name, pattern, plen)))
			continue;
		if (alen && (awc ? wildcmp (antipattern, var->name) : !strncmp (var->name, antipattern, alen)))
			continue;

		PF_bufstr_add_internal (bufno, var->name, true);
	}

	qsort (strbuflist[bufno].strings, strbuflist[bufno].used, sizeof (char *), PF_buf_sort_ascending);
}

// entity stuff
static void PF_WasFreed (void)
{
	edict_t *ed = G_EDICT (OFS_PARM0);
	G_FLOAT (OFS_RETURN) = ed->free;
}
static void PF_copyentity (void)
{
	edict_t *src = G_EDICT (OFS_PARM0);
	edict_t *dst = (qcvm->argc < 2) ? ED_Alloc () : G_EDICT (OFS_PARM1);
	if (src->free || dst->free)
		Con_Printf ("PF_copyentity: entity is free\n");
	memcpy (&dst->v, &src->v, qcvm->edict_size - sizeof (entvars_t));
	dst->alpha = src->alpha;
	dst->sendinterval = src->sendinterval;
	SV_LinkEdict (dst, false);

	G_INT (OFS_RETURN) = EDICT_TO_PROG (dst);
}
static void PF_edict_for_num (void)
{
	G_INT (OFS_RETURN) = EDICT_TO_PROG (EDICT_NUM (G_FLOAT (OFS_PARM0)));
}
static void PF_num_for_edict (void)
{
	G_FLOAT (OFS_RETURN) = G_EDICTNUM (OFS_PARM0);
}
static void PF_findchain (void)
{
	edict_t	   *ent, *chain;
	int			i, f;
	const char *s, *t;
	int			cfld;

	chain = (edict_t *)qcvm->edicts;

	ent = NEXT_EDICT (qcvm->edicts);
	f = G_INT (OFS_PARM0);
	s = G_STRING (OFS_PARM1);
	if (qcvm->argc > 2)
		cfld = G_INT (OFS_PARM2);
	else
		cfld = &ent->v.chain - (int *)&ent->v;

	for (i = 1; i < qcvm->num_edicts; i++, ent = NEXT_EDICT (ent))
	{
		if (ent->free)
			continue;
		t = E_STRING (ent, f);
		if (strcmp (s, t))
			continue;
		((int *)&ent->v)[cfld] = EDICT_TO_PROG (chain);
		chain = ent;
	}

	RETURN_EDICT (chain);
}
static void PF_findfloat (void)
{
	int		 e;
	int		 f;
	float	 s, t;
	edict_t *ed;

	e = G_EDICTNUM (OFS_PARM0);
	f = G_INT (OFS_PARM1);
	s = G_FLOAT (OFS_PARM2);

	for (e++; e < qcvm->num_edicts; e++)
	{
		ed = EDICT_NUM (e);
		if (ed->free)
			continue;
		t = E_FLOAT (ed, f);
		if (t == s)
		{
			RETURN_EDICT (ed);
			return;
		}
	}

	RETURN_EDICT (qcvm->edicts);
}
static void PF_findchainfloat (void)
{
	edict_t *ent, *chain;
	int		 i, f;
	float	 s, t;
	int		 cfld;

	chain = (edict_t *)qcvm->edicts;

	ent = NEXT_EDICT (qcvm->edicts);
	f = G_INT (OFS_PARM0);
	s = G_FLOAT (OFS_PARM1);
	if (qcvm->argc > 2)
		cfld = G_INT (OFS_PARM2);
	else
		cfld = &ent->v.chain - (int *)&ent->v;

	for (i = 1; i < qcvm->num_edicts; i++, ent = NEXT_EDICT (ent))
	{
		if (ent->free)
			continue;
		t = E_FLOAT (ent, f);
		if (s != t)
			continue;
		((int *)&ent->v)[cfld] = EDICT_TO_PROG (chain);
		chain = ent;
	}

	RETURN_EDICT (chain);
}
static void PF_findflags (void)
{
	int		 e;
	int		 f;
	int		 s, t;
	edict_t *ed;

	e = G_EDICTNUM (OFS_PARM0);
	f = G_INT (OFS_PARM1);
	s = G_FLOAT (OFS_PARM2);

	for (e++; e < qcvm->num_edicts; e++)
	{
		ed = EDICT_NUM (e);
		if (ed->free)
			continue;
		t = E_FLOAT (ed, f);
		if (t & s)
		{
			RETURN_EDICT (ed);
			return;
		}
	}

	RETURN_EDICT (qcvm->edicts);
}
static void PF_findchainflags (void)
{
	edict_t *ent, *chain;
	int		 i, f;
	int		 s, t;
	int		 cfld;

	chain = (edict_t *)qcvm->edicts;
	ent = NEXT_EDICT (qcvm->edicts);

	f = G_INT (OFS_PARM0);
	s = G_FLOAT (OFS_PARM1);
	if (qcvm->argc > 2)
		cfld = G_INT (OFS_PARM2);
	else
		cfld = &ent->v.chain - (int *)&ent->v;

	for (i = 1; i < qcvm->num_edicts; i++, ent = NEXT_EDICT (ent))
	{
		if (ent->free)
			continue;
		t = E_FLOAT (ent, f);
		if (!(s & t))
			continue;
		((int *)&ent->v)[cfld] = EDICT_TO_PROG (chain);
		chain = ent;
	}

	RETURN_EDICT (chain);
}
static void PF_numentityfields (void)
{
	G_FLOAT (OFS_RETURN) = qcvm->progs->numfielddefs;
}
static void PF_findentityfield (void)
{
	ddef_t *fld = ED_FindField (G_STRING (OFS_PARM0));
	if (fld)
		G_FLOAT (OFS_RETURN) = fld - qcvm->fielddefs;
	else
		G_FLOAT (OFS_RETURN) = 0; // the first field is meant to be some dummy placeholder. or it could be modelindex...
}
static void PF_entityfieldref (void)
{
	unsigned int fldidx = G_FLOAT (OFS_PARM0);
	if (fldidx >= (unsigned int)qcvm->progs->numfielddefs)
		G_INT (OFS_RETURN) = 0;
	else
		G_INT (OFS_RETURN) = qcvm->fielddefs[fldidx].ofs;
}
static void PF_entityfieldname (void)
{
	unsigned int fldidx = G_FLOAT (OFS_PARM0);
	if (fldidx < (unsigned int)qcvm->progs->numfielddefs)
		G_INT (OFS_RETURN) = qcvm->fielddefs[fldidx].s_name;
	else
		G_INT (OFS_RETURN) = 0;
}
static void PF_entityfieldtype (void)
{
	unsigned int fldidx = G_FLOAT (OFS_PARM0);
	if (fldidx >= (unsigned int)qcvm->progs->numfielddefs)
		G_FLOAT (OFS_RETURN) = ev_void;
	else
		G_FLOAT (OFS_RETURN) = qcvm->fielddefs[fldidx].type;
}
static void PF_getentfldstr (void)
{
	unsigned int fldidx = G_FLOAT (OFS_PARM0);
	edict_t		*ent = G_EDICT (OFS_PARM1);
	if (fldidx < (unsigned int)qcvm->progs->numfielddefs)
	{
		char	   *ret = PR_GetTempString ();
		const char *val = PR_UglyValueString (qcvm->fielddefs[fldidx].type, (eval_t *)((float *)&ent->v + qcvm->fielddefs[fldidx].ofs));
		q_strlcpy (ret, val, STRINGTEMP_LENGTH);
		G_INT (OFS_RETURN) = PR_SetEngineString (ret);
	}
	else
		G_INT (OFS_RETURN) = 0;
}
static void PF_putentfldstr (void)
{
	unsigned int fldidx = G_FLOAT (OFS_PARM0);
	edict_t		*ent = G_EDICT (OFS_PARM1);
	const char	*value = G_STRING (OFS_PARM2);
	if (fldidx < (unsigned int)qcvm->progs->numfielddefs)
		G_FLOAT (OFS_RETURN) = ED_ParseEpair ((void *)&ent->v, qcvm->fielddefs + fldidx, value, true);
	else
		G_FLOAT (OFS_RETURN) = false;
}

static void PF_parseentitydata (void)
{
	edict_t		*ed = G_EDICT (OFS_PARM0);
	const char	*data = G_STRING (OFS_PARM1), *end;
	unsigned int offset = (qcvm->argc > 2) ? G_FLOAT (OFS_PARM2) : 0;
	if (offset)
	{
		unsigned int len = strlen (data);
		if (offset > len)
			offset = len;
	}
	if (!data[offset])
		G_FLOAT (OFS_RETURN) = 0;
	else
	{
		end = COM_Parse (data + offset);
		if (!strcmp (com_token, "{"))
		{
			end = ED_ParseEdict (end, ed);
			G_FLOAT (OFS_RETURN) = end - data;
		}
		else
			G_FLOAT (OFS_RETURN) = 0; // doesn't look like an ent to me.
	}
}

static void PF_callfunction (void)
{
	dfunction_t *fnc;
	const char	*fname;
	if (!qcvm->argc)
		return;
	qcvm->argc--;
	fname = G_STRING (OFS_PARM0 + qcvm->argc * 3);
	fnc = ED_FindFunction (fname);
	if (fnc && fnc->first_statement > 0)
	{
		PR_ExecuteProgram (fnc - qcvm->functions);
	}
}
static void PF_isfunction (void)
{
	const char *fname = G_STRING (OFS_PARM0);
	G_FLOAT (OFS_RETURN) = ED_FindFunction (fname) ? true : false;
}

// other stuff
static void PF_gettime (void)
{
	int timer = (qcvm->argc > 0) ? G_FLOAT (OFS_PARM0) : 0;
	switch (timer)
	{
	default:
		Con_DPrintf ("PF_gettime: unsupported timer %i\n", timer);
	case 0: // cached time at start of frame
		G_FLOAT (OFS_RETURN) = realtime;
		break;
	case 1: // actual time
		G_FLOAT (OFS_RETURN) = Sys_DoubleTime ();
		break;
		// case 2:	//highres.. looks like time into the frame. no idea
		// case 3:	//uptime
		// case 4:	//cd track
		// case 5:	//client simtime
	}
}
#define STRINGIFY2(x) #x
#define STRINGIFY(x)  STRINGIFY2 (x)
static void PF_infokey_internal (qboolean returnfloat)
{
	unsigned int ent = G_EDICTNUM (OFS_PARM0);
	const char	*key = G_STRING (OFS_PARM1);
	const char	*r;
	char		 buf[1024];
	if (!ent)
	{
		if (!strcmp (key, "*version"))
		{
			q_snprintf (buf, sizeof (buf), ENGINE_NAME_AND_VER);
			r = buf;
		}
		else
			r = NULL;
	}
	else if (ent <= (unsigned int)svs.maxclients && svs.clients[ent - 1].active)
	{
		client_t *client = &svs.clients[ent - 1];
		r = buf;
		if (!strcmp (key, "ip"))
			r = NET_QSocketGetTrueAddressString (client->netconnection);
		else if (!strcmp (key, "ping"))
		{
			float		 total = 0;
			unsigned int j;
			for (j = 0; j < NUM_PING_TIMES; j++)
total += client->ping_times[j];
			total /= NUM_PING_TIMES;
			q_snprintf (buf, sizeof (buf), "%f", total);
		}
		else if (!strcmp (key, "protocol"))
		{
			switch (sv.protocol)
			{
			case PROTOCOL_NETQUAKE:
r = "quake";
break;
			case PROTOCOL_FITZQUAKE:
r = "fitz666";
break;
			case PROTOCOL_RMQ:
r = "rmq999";
break;
			default:
r = "";
break;
			}
		}
		else if (!strcmp (key, "name"))
			r = client->name;
		else if (!strcmp (key, "topcolor"))
			q_snprintf (buf, sizeof (buf), "%u", client->colors >> 4);
		else if (!strcmp (key, "bottomcolor"))
			q_snprintf (buf, sizeof (buf), "%u", client->colors & 15);
		else if (!strcmp (key, "team")) // nq doesn't really do teams. qw does though. yay compat?
			q_snprintf (buf, sizeof (buf), "t%u", (client->colors & 15) + 1);
		else if (!strcmp (key, "*VIP"))
			r = "";
		else if (!strcmp (key, "*spectator"))
			r = "";
		else if (!strcmp (key, "csqcactive"))
			r = "0";
		else
			r = NULL;
	}
	else
		r = NULL;

	if (returnfloat)
	{
		if (r)
			G_FLOAT (OFS_RETURN) = atof (r);
		else
			G_FLOAT (OFS_RETURN) = 0;
	}
	else
	{
		if (r)
		{
			char *temp = PR_GetTempString ();
			q_strlcpy (temp, r, STRINGTEMP_LENGTH);
			G_INT (OFS_RETURN) = PR_SetEngineString (temp);
		}
		else
			G_INT (OFS_RETURN) = 0;
	}
}
static void PF_infokey_s (void)
{
	PF_infokey_internal (false);
}
static void PF_infokey_f (void)
{
	PF_infokey_internal (true);
}

static void PF_multicast_internal (qboolean reliable, byte *pvs, unsigned int requireext2)
{
	unsigned int i;
	int			 cluster;
	mleaf_t		*playerleaf;
	if (!pvs)
	{
		if (!requireext2)
			SZ_Write ((reliable ? &sv.reliable_datagram : &sv.datagram), sv.multicast.data, sv.multicast.cursize);
		else
		{
			for (i = 0; i < (unsigned int)svs.maxclients; i++)
			{
if (!svs.clients[i].active)
	continue;
if (!(svs.clients[i].protocol_pext2 & requireext2))
	continue;
SZ_Write ((reliable ? &svs.clients[i].message : &svs.clients[i].datagram), sv.multicast.data, sv.multicast.cursize);
			}
		}
	}
	else
	{
		for (i = 0; i < (unsigned int)svs.maxclients; i++)
		{
			if (!svs.clients[i].active)
continue;

			if (requireext2 && !(svs.clients[i].protocol_pext2 & requireext2))
continue;

			// figure out which cluster (read: pvs index) to use.
			playerleaf = Mod_PointInLeaf (svs.clients[i].edict->v.origin, qcvm->worldmodel);
			cluster = playerleaf - qcvm->worldmodel->leafs;
			cluster--; // pvs is 1-based, leaf 0 is discarded.
			if (cluster < 0 || (pvs[cluster >> 3] & (1 << (cluster & 7))))
			{
// they can see it. add it in to whichever buffer is appropriate.
if (reliable)
	SZ_Write (&svs.clients[i].message, sv.multicast.data, sv.multicast.cursize);
else
	SZ_Write (&svs.clients[i].datagram, sv.multicast.data, sv.multicast.cursize);
			}
		}
	}
}
// FIXME: shouldn't really be using pext2, but we don't track the earlier extensions, and it should be safe enough.
static void SV_Multicast (multicast_t to, float *org, int msg_entity, unsigned int requireext2)
{
	unsigned int i;

	if (to == MULTICAST_INIT && sv.state != ss_loading)
	{
		SZ_Write (&sv.signon, sv.multicast.data, sv.multicast.cursize);
		to = MULTICAST_ALL_R; // and send to players that are already on
	}

	switch (to)
	{
	case MULTICAST_INIT:
		SZ_Write (&sv.signon, sv.multicast.data, sv.multicast.cursize);
		break;
	case MULTICAST_ALL_R:
	case MULTICAST_ALL_U:
		PF_multicast_internal (to == MULTICAST_ALL_R, NULL, requireext2);
		break;
	case MULTICAST_PHS_R:
	case MULTICAST_PHS_U:
		// we don't support phs, that would require lots of pvs decompression+merging stuff, and many q1bsps have a LOT of leafs.
		PF_multicast_internal (to == MULTICAST_PHS_R, NULL /*Mod_LeafPHS(Mod_PointInLeaf(org, qcvm->worldmodel), qcvm->worldmodel)*/, requireext2);
		break;
	case MULTICAST_PVS_R:
	case MULTICAST_PVS_U:
		PF_multicast_internal (to == MULTICAST_PVS_R, Mod_LeafPVS (Mod_PointInLeaf (org, qcvm->worldmodel), qcvm->worldmodel), requireext2);
		break;
	case MULTICAST_ONE_R:
	case MULTICAST_ONE_U:
		i = msg_entity - 1;
		if (i >= (unsigned int)svs.maxclients)
			break;
		// a unicast, which ignores pvs.
		//(unlike vanilla this allows unicast unreliables, so woo)
		if (svs.clients[i].active)
		{
			SZ_Write (((to == MULTICAST_ONE_R) ? &svs.clients[i].message : &svs.clients[i].datagram), sv.multicast.data, sv.multicast.cursize);
		}
		break;
	default:
		break;
	}
	SZ_Clear (&sv.multicast);
}
static void PF_multicast (void)
{
	float	   *org = G_VECTOR (OFS_PARM0);
	multicast_t to = G_FLOAT (OFS_PARM1);
	SV_Multicast (to, org, NUM_FOR_EDICT (PROG_TO_EDICT (pr_global_struct->msg_entity)), 0);
}
static void PF_randomvector (void)
{
	vec3_t temp;
	do
	{
		temp[0] = (rand () & 32767) * (2.0 / 32767.0) - 1.0;
		temp[1] = (rand () & 32767) * (2.0 / 32767.0) - 1.0;
		temp[2] = (rand () & 32767) * (2.0 / 32767.0) - 1.0;
	} while (DotProduct (temp, temp) >= 1);
	VectorCopy (temp, G_VECTOR (OFS_RETURN));
}
static void PF_checkextension (void);
static void PF_checkbuiltin (void);
static void PF_builtinsupported (void);

static void PF_uri_escape (void)
{
	static const char *hex = "0123456789ABCDEF";

	char				*result = PR_GetTempString ();
	char				*o = result;
	const unsigned char *s = (const unsigned char *)G_STRING (OFS_PARM0);
	*result = 0;
	while (*s && o < result + STRINGTEMP_LENGTH - 4)
	{
		if ((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') || (*s >= '0' && *s <= '9') || *s == '.' || *s == '-' || *s == '_')
			*o++ = *s++;
		else
		{
			*o++ = '%';
			*o++ = hex[*s >> 4];
			*o++ = hex[*s & 0xf];
			s++;
		}
	}
	*o = 0;
	G_INT (OFS_RETURN) = PR_SetEngineString (result);
}
static void PF_uri_unescape (void)
{
	const char	 *s = G_STRING (OFS_PARM0), *i;
	char		 *resultbuf = PR_GetTempString (), *o;
	unsigned char hex;
	i = s;
	o = resultbuf;
	while (*i && o < resultbuf + STRINGTEMP_LENGTH - 2)
	{
		if (*i == '%')
		{
			hex = 0;
			if (i[1] >= 'A' && i[1] <= 'F')
hex += i[1] - 'A' + 10;
			else if (i[1] >= 'a' && i[1] <= 'f')
hex += i[1] - 'a' + 10;
			else if (i[1] >= '0' && i[1] <= '9')
hex += i[1] - '0';
			else
			{
*o++ = *i++;
continue;
			}
			hex <<= 4;
			if (i[2] >= 'A' && i[2] <= 'F')
hex += i[2] - 'A' + 10;
			else if (i[2] >= 'a' && i[2] <= 'f')
hex += i[2] - 'a' + 10;
			else if (i[2] >= '0' && i[2] <= '9')
hex += i[2] - '0';
			else
			{
*o++ = *i++;
continue;
			}
			*o++ = hex;
			i += 3;
		}
		else
			*o++ = *i++;
	}
	*o = 0;
	G_INT (OFS_RETURN) = PR_SetEngineString (resultbuf);
}
static void PF_crc16 (void)
{
	qboolean	insens = G_FLOAT (OFS_PARM0);
	const char *str = PF_VarString (1);
	size_t		len = strlen (str);

	if (insens)
	{
		unsigned short crc;

		CRC_Init (&crc);
		while (len--)
			CRC_ProcessByte (&crc, q_tolower (*str++));
		G_FLOAT (OFS_RETURN) = crc;
	}
	else
		G_FLOAT (OFS_RETURN) = CRC_Block ((const byte *)str, len);
}
static void PF_digest_hex (void)
{
	const char		  *hashtype = G_STRING (OFS_PARM0);
	const byte		  *data = (const byte *)PF_VarString (1);
	size_t			   len = strlen ((const char *)data);
	static const char *hex = "0123456789ABCDEF";
	char			  *resultbuf;
	byte			   hashdata[20];

	if (!strcmp (hashtype, "CRC16"))
	{
		int crc = CRC_Block ((const byte *)data, len);
		hashdata[0] = crc & 0xff;
		hashdata[1] = (crc >> 8) & 0xff;
		len = 2;
	}
	else if (!strcmp (hashtype, "MD4"))
	{
		Com_BlockFullChecksum ((void *)data, len, hashdata);
		len = 16;
	}
	else
	{
		Con_Printf ("PF_digest_hex: Unsupported digest %s\n", hashtype);
		G_INT (OFS_RETURN) = 0;
		return;
	}

	resultbuf = PR_GetTempString ();
	G_INT (OFS_RETURN) = PR_SetEngineString (resultbuf);
	data = hashdata;
	while (len-- > 0)
	{
		*resultbuf++ = hex[*data >> 4];
		*resultbuf++ = hex[*data & 0xf];
		data++;
	}
	*resultbuf = 0;
}

static void PF_strlennocol (void)
{
	int				r = 0;
	struct markup_s mu;

	PR_Markup_Begin (&mu, G_STRING (OFS_PARM0), vec3_origin, 1);
	while (PR_Markup_Parse (&mu))
		r++;
	G_FLOAT (OFS_RETURN) = r;
}
static void PF_strdecolorize (void)
{
	int				l, c;
	char		   *r = PR_GetTempString ();
	struct markup_s mu;

	PR_Markup_Begin (&mu, G_STRING (OFS_PARM0), vec3_origin, 1);
	for (l = 0; l < STRINGTEMP_LENGTH - 1; l++)
	{
		c = PR_Markup_Parse (&mu);
		if (!c)
			break;
		r[l] = c;
	}
	r[l] = 0;

	G_INT (OFS_RETURN) = PR_SetEngineString (r);
}
static void PF_setattachment (void)
{
	edict_t	   *ent = G_EDICT (OFS_PARM0);
	edict_t	   *tagent = G_EDICT (OFS_PARM1);
	const char *tagname = G_STRING (OFS_PARM2);
	eval_t	   *val;

	if (*tagname)
	{
		// we don't support md3s, or any skeletal formats, so all tag names are logically invalid for us.
		Con_DWarning ("PF_setattachment: tag %s not found\n", tagname);
	}

	if ((val = GetEdictFieldValue (ent, qcvm->extfields.tag_entity)))
		val->edict = EDICT_TO_PROG (tagent);
	if ((val = GetEdictFieldValue (ent, qcvm->extfields.tag_index)))
		val->_float = 0;
}

static struct svcustomstat_s *PR_CustomStat (int idx, int type)
{
	size_t i;
	if (idx < 0 || idx >= MAX_CL_STATS)
		return NULL;
	switch (type)
	{
	case ev_ext_integer:
	case ev_float:
	case ev_vector:
	case ev_entity:
		break;
	default:
		return NULL;
	}

	for (i = 0; i < sv.numcustomstats; i++)
	{
		if (sv.customstats[i].idx == idx && (sv.customstats[i].type == ev_string) == (type == ev_string))
			break;
	}
	if (i == sv.numcustomstats)
		sv.numcustomstats++;
	sv.customstats[i].idx = idx;
	sv.customstats[i].type = type;
	sv.customstats[i].fld = 0;
	sv.customstats[i].ptr = NULL;
	return &sv.customstats[i];
}
static void PF_clientstat (void)
{
	int					   idx = G_FLOAT (OFS_PARM0);
	int					   type = G_FLOAT (OFS_PARM1);
	int					   fldofs = G_INT (OFS_PARM2);
	struct svcustomstat_s *stat = PR_CustomStat (idx, type);
	if (!stat)
		return;
	stat->fld = fldofs;
}
static void PF_isbackbuffered (void)
{
	unsigned int plnum = G_EDICTNUM (OFS_PARM0) - 1;
	G_FLOAT (OFS_RETURN) = true; // assume the connection is clogged.
	if (plnum > (unsigned int)svs.maxclients)
		return; // make error?
	if (!svs.clients[plnum].active)
		return; // empty slot
	if (svs.clients[plnum].message.cursize > DATAGRAM_MTU)
		return;
	G_FLOAT (OFS_RETURN) = false; // okay to spam with more reliables.
}

#ifdef PSET_SCRIPT
int PF_SV_ForceParticlePrecache (const char *s)
{
	unsigned int i;
	for (i = 1; i < MAX_PARTICLETYPES; i++)
	{
		if (sv.particle_precache[i] && !strcmp (sv.particle_precache[i], s))
			return i;
	}

	for (i = 1; i < MAX_PARTICLETYPES; i++)
	{
		if (!sv.particle_precache[i])
		{
			if (sv.state != ss_loading)
			{
MSG_WriteByte (&sv.multicast, svcdp_precache);
MSG_WriteShort (&sv.multicast, i | 0x4000);
MSG_WriteString (&sv.multicast, s);
SV_Multicast (MULTICAST_ALL_R, NULL, 0, PEXT2_REPLACEMENTDELTAS); // FIXME
			}

			sv.particle_precache[i] = q_strdup (s); // weirdness to avoid issues with tempstrings
			return i;
		}
	}
	return 0;
}
static void PF_sv_particleeffectnum (void)
{
	const char	 *s;
	unsigned int  i;
	extern cvar_t r_particledesc;

	s = G_STRING (OFS_PARM0);
	G_FLOAT (OFS_RETURN) = 0;
	//	PR_CheckEmptyString (s);

	if (!*s)
		return;

	if (!sv.particle_precache[1] && (!strncmp (s, "effectinfo.", 11) || strstr (r_particledesc.string, "effectinfo")))
		COM_Effectinfo_Enumerate (PF_SV_ForceParticlePrecache);

	for (i = 1; i < MAX_PARTICLETYPES; i++)
	{
		if (sv.particle_precache[i] && !strcmp (sv.particle_precache[i], s))
		{
			if (sv.state != ss_loading && !pr_checkextension.value)
			{
if (pr_ext_warned_particleeffectnum++ < 3)
	Con_Warning ("PF_sv_particleeffectnum(%s): Precache should only be done in spawn functions\n", s);
			}
			G_FLOAT (OFS_RETURN) = i;
			return;
		}
	}

	for (i = 1; i < MAX_PARTICLETYPES; i++)
	{
		if (!sv.particle_precache[i])
		{
			if (sv.state != ss_loading)
			{
if (pr_ext_warned_particleeffectnum++ < 3)
	Con_Warning ("PF_sv_particleeffectnum(%s): Precache should only be done in spawn functions\n", s);

MSG_WriteByte (&sv.multicast, svcdp_precache);
MSG_WriteShort (&sv.multicast, i | 0x4000);
MSG_WriteString (&sv.multicast, s);
SV_Multicast (MULTICAST_ALL_R, NULL, 0, PEXT2_REPLACEMENTDELTAS);
			}

			sv.particle_precache[i] = q_strdup (s); // weirdness to avoid issues with tempstrings
			G_FLOAT (OFS_RETURN) = i;
			return;
		}
	}
	PR_RunError ("PF_sv_particleeffectnum: overflow");
}
static void PF_sv_trailparticles (void)
{
	int	   efnum;
	int	   ednum;
	float *start = G_VECTOR (OFS_PARM2);
	float *end = G_VECTOR (OFS_PARM3);

	/*DP gets this wrong, lets try to be compatible*/
	if ((unsigned int)G_INT (OFS_PARM1) >= MAX_EDICTS * (unsigned int)qcvm->edict_size)
	{
		ednum = G_EDICTNUM (OFS_PARM0);
		efnum = G_FLOAT (OFS_PARM1);
	}
	else
	{
		efnum = G_FLOAT (OFS_PARM0);
		ednum = G_EDICTNUM (OFS_PARM1);
	}

	if (efnum <= 0)
		return;

	MSG_WriteByte (&sv.multicast, svcdp_trailparticles);
	MSG_WriteShort (&sv.multicast, ednum);
	MSG_WriteShort (&sv.multicast, efnum);
	MSG_WriteCoord (&sv.multicast, start[0], sv.protocolflags);
	MSG_WriteCoord (&sv.multicast, start[1], sv.protocolflags);
	MSG_WriteCoord (&sv.multicast, start[2], sv.protocolflags);
	MSG_WriteCoord (&sv.multicast, end[0], sv.protocolflags);
	MSG_WriteCoord (&sv.multicast, end[1], sv.protocolflags);
	MSG_WriteCoord (&sv.multicast, end[2], sv.protocolflags);

	SV_Multicast (MULTICAST_PHS_U, start, 0, PEXT2_REPLACEMENTDELTAS);
}
static void PF_sv_pointparticles (void)
{
	int	   efnum = G_FLOAT (OFS_PARM0);
	float *org = G_VECTOR (OFS_PARM1);
	float *vel = (qcvm->argc < 3) ? vec3_origin : G_VECTOR (OFS_PARM2);
	int	   count = (qcvm->argc < 4) ? 1 : G_FLOAT (OFS_PARM3);

	if (efnum <= 0)
		return;
	if (count > 65535)
		count = 65535;
	if (count < 1)
		return;

	if (count == 1 && !vel[0] && !vel[1] && !vel[2])
	{
		MSG_WriteByte (&sv.multicast, svcdp_pointparticles1);
		MSG_WriteShort (&sv.multicast, efnum);
		MSG_WriteCoord (&sv.multicast, org[0], sv.protocolflags);
		MSG_WriteCoord (&sv.multicast, org[1], sv.protocolflags);
		MSG_WriteCoord (&sv.multicast, org[2], sv.protocolflags);
	}
	else
	{
		MSG_WriteByte (&sv.multicast, svcdp_pointparticles);
		MSG_WriteShort (&sv.multicast, efnum);
		MSG_WriteCoord (&sv.multicast, org[0], sv.protocolflags);
		MSG_WriteCoord (&sv.multicast, org[1], sv.protocolflags);
		MSG_WriteCoord (&sv.multicast, org[2], sv.protocolflags);
		MSG_WriteCoord (&sv.multicast, vel[0], sv.protocolflags);
		MSG_WriteCoord (&sv.multicast, vel[1], sv.protocolflags);
		MSG_WriteCoord (&sv.multicast, vel[2], sv.protocolflags);
		MSG_WriteShort (&sv.multicast, count);
	}

	SV_Multicast (MULTICAST_PVS_U, org, 0, PEXT2_REPLACEMENTDELTAS);
}

int PF_CL_ForceParticlePrecache (const char *s)
{
	int i;

	// check if an ssqc one already exists with that name
	for (i = 1; i < MAX_PARTICLETYPES; i++)
	{
		if (!cl.particle_precache[i].name)
			break; // nope, no more known
		if (!strcmp (cl.particle_precache[i].name, s))
			return i;
	}

	// nope, check for a csqc one, and allocate if needed
	for (i = 1; i < MAX_PARTICLETYPES; i++)
	{
		if (!cl.local_particle_precache[i].name)
		{
			cl.local_particle_precache[i].name = q_strdup (s); // weirdness to avoid issues with tempstrings
			cl.local_particle_precache[i].index = PScript_FindParticleType (cl.local_particle_precache[i].name);
			return -i;
		}
		if (!strcmp (cl.local_particle_precache[i].name, s))
			return -i;
	}

	// err... too many. bum.
	return 0;
}
int PF_CL_GetParticle (int idx)
{ // negatives are csqc-originated particles, positives are ssqc-originated, for consistency allowing networking of particles as identifiers
	if (!idx)
		return P_INVALID;
	if (idx < 0)
	{
		idx = -idx;
		if (idx >= MAX_PARTICLETYPES)
			return P_INVALID;
		return cl.local_particle_precache[idx].index;
	}
	else
	{
		if (idx >= MAX_PARTICLETYPES)
			return P_INVALID;
		return cl.particle_precache[idx].index;
	}
}

static void PF_cl_particleeffectnum (void)
{
	const char *s;

	s = G_STRING (OFS_PARM0);
	G_FLOAT (OFS_RETURN) = 0;
	//	PR_CheckEmptyString (s);

	if (!*s)
		return;

	G_FLOAT (OFS_RETURN) = PF_CL_ForceParticlePrecache (s);
	if (!G_FLOAT (OFS_RETURN))
		PR_RunError ("PF_cl_particleeffectnum: overflow");
}
static void PF_cl_trailparticles (void)
{
	int		 efnum;
	edict_t *ent;
	float	*start = G_VECTOR (OFS_PARM2);
	float	*end = G_VECTOR (OFS_PARM3);

	if ((unsigned int)G_INT (OFS_PARM1) >= MAX_EDICTS * (unsigned int)qcvm->edict_size)
	{ /*DP gets this wrong, lets try to be compatible*/
		ent = G_EDICT (OFS_PARM0);
		efnum = G_FLOAT (OFS_PARM1);
	}
	else
	{
		efnum = G_FLOAT (OFS_PARM0);
		ent = G_EDICT (OFS_PARM1);
	}

	if (efnum <= 0)
		return;
	efnum = PF_CL_GetParticle (efnum);
	PScript_ParticleTrail (start, end, efnum, host_frametime, -NUM_FOR_EDICT (ent), NULL, NULL /*&ent->trailstate*/);
}
static void PF_cl_pointparticles (void)
{
	int	   efnum = G_FLOAT (OFS_PARM0);
	float *org = G_VECTOR (OFS_PARM1);
	float *vel = (qcvm->argc < 3) ? vec3_origin : G_VECTOR (OFS_PARM2);
	int	   count = (qcvm->argc < 4) ? 1 : G_FLOAT (OFS_PARM3);

	if (efnum <= 0)
		return;
	if (count < 1)
		return;
	efnum = PF_CL_GetParticle (efnum);
	PScript_RunParticleEffectState (org, vel, count, efnum, NULL);
}
#else
#define PF_sv_particleeffectnum PF_void_stub
#define PF_sv_trailparticles	PF_void_stub
#define PF_sv_pointparticles	PF_void_stub
#define PF_cl_particleeffectnum PF_void_stub
#define PF_cl_trailparticles	PF_void_stub
#define PF_cl_pointparticles	PF_void_stub
#endif

static void PF_cl_getstat_int (void)
{
	int stnum = G_FLOAT (OFS_PARM0);
	if (stnum < 0 || stnum > countof (cl.stats))
		G_INT (OFS_RETURN) = 0;
	else
		G_INT (OFS_RETURN) = cl.stats[stnum];
}
static void PF_cl_getstat_float (void)
{
	int stnum = G_FLOAT (OFS_PARM0);
	if (stnum < 0 || stnum > countof (cl.stats))
		G_FLOAT (OFS_RETURN) = 0;
	else if (qcvm->argc > 1)
	{
		int firstbit = G_FLOAT (OFS_PARM1);
		int bitcount = G_FLOAT (OFS_PARM2);
		G_FLOAT (OFS_RETURN) = (cl.stats[stnum] >> firstbit) & ((1 << bitcount) - 1);
	}
	else
		G_FLOAT (OFS_RETURN) = cl.statsf[stnum];
}
static void PF_cl_getstat_string (void)
{
	int stnum = G_FLOAT (OFS_PARM0);
	if (stnum < 0 || stnum > countof (cl.statss) || !cl.statss[stnum])
		G_INT (OFS_RETURN) = 0;
	else
	{
		char *result = PR_GetTempString ();
		q_strlcpy (result, cl.statss[stnum], STRINGTEMP_LENGTH);
		G_INT (OFS_RETURN) = PR_SetEngineString (result);
	}
}

static struct
{
	char		 name[MAX_QPATH];
	unsigned int flags;
	qpic_t		*pic;
}			 *qcpics;
static size_t numqcpics;
static size_t maxqcpics;
void		  PR_ReloadPics (qboolean purge)
{
	numqcpics = 0;

	Mem_Free (qcpics);
	qcpics = NULL;
	maxqcpics = 0;
}
#define PICFLAG_AUTO   0		 // value used when no flags known
#define PICFLAG_WAD	   (1u << 0) // name matches that of a wad lump
#define PICFLAG_WRAP   (1u << 2) // make sure npot stuff doesn't break wrapping.
#define PICFLAG_MIPMAP (1u << 3) // disable use of scrap...
#define PICFLAG_BLOCK  (1u << 9) // wait until the texture is fully loaded.
#define PICFLAG_NOLOAD (1u << 31)
static qpic_t *DrawQC_CachePic (const char *picname, unsigned int flags)
{ // okay, so this is silly. we've ended up with 3 different cache levels. qcpics, pics, and images.
	size_t		 i;
	unsigned int texflags;
	for (i = 0; i < numqcpics; i++)
	{ // binary search? something more sane?
		if (!strcmp (picname, qcpics[i].name))
		{
			if (qcpics[i].pic)
return qcpics[i].pic;
			break;
		}
	}

	if (strlen (picname) >= MAX_QPATH)
		return NULL; // too long. get lost.

	if (flags & PICFLAG_NOLOAD)
		return NULL; // its a query, not actually needed.

	if (i + 1 > maxqcpics)
	{
		maxqcpics = i + 32;
		qcpics = Mem_Realloc (qcpics, maxqcpics * sizeof (*qcpics));
	}

	strcpy (qcpics[i].name, picname);
	qcpics[i].flags = flags;
	qcpics[i].pic = NULL;

	texflags = TEXPREF_ALPHA | TEXPREF_PAD | TEXPREF_NOPICMIP;
	if (flags & PICFLAG_WRAP)
		texflags &= ~TEXPREF_PAD; // don't allow padding if its going to need to wrap (even if we don't enable clamp-to-edge normally). I just hope we have
								  // npot support.
	if (flags & PICFLAG_MIPMAP)
		texflags |= TEXPREF_MIPMAP;

	// try to load it from a wad if applicable.
	// the extra gfx/ crap is because DP insists on it for wad images. and its a nightmare to get things working in all engines if we don't accept that quirk
	// too.
	if (flags & PICFLAG_WAD)
		qcpics[i].pic = Draw_PicFromWad2 (picname + (strncmp (picname, "gfx/", 4) ? 0 : 4), texflags);
	else if (!strncmp (picname, "gfx/", 4) && !strchr (picname + 4, '.'))
		qcpics[i].pic = Draw_PicFromWad2 (picname + 4, texflags);

	// okay, not a wad pic, try and load a lmp/tga/etc
	if (!qcpics[i].pic)
		qcpics[i].pic = Draw_TryCachePic (picname, texflags);

	if (i == numqcpics)
		numqcpics++;

	return qcpics[i].pic;
}
extern gltexture_t *char_texture;
static void			DrawQC_CharacterQuad (cb_context_t *cbx, float x, float y, int num, float w, float h, float *rgb, float alpha)
{
	float	 size = 0.0625;
	float	 frow = (num >> 4) * size;
	float	 fcol = (num & 15) * size;
	int		 i;
	qboolean alpha_blend = alpha < 1.0f;
	size = 0.0624; // avoid rounding errors...

	VkBuffer	   buffer;
	VkDeviceSize   buffer_offset;
	basicvertex_t *vertices = (basicvertex_t *)R_VertexAllocate (6 * sizeof (basicvertex_t), &buffer, &buffer_offset);

	basicvertex_t corner_verts[4];
	memset (&corner_verts, 255, sizeof (corner_verts));

	corner_verts[0].position[0] = x;
	corner_verts[0].position[1] = y;
	corner_verts[0].position[2] = 0.0f;
	corner_verts[0].texcoord[0] = fcol;
	corner_verts[0].texcoord[1] = frow;

	corner_verts[1].position[0] = x + w;
	corner_verts[1].position[1] = y;
	corner_verts[1].position[2] = 0.0f;
	corner_verts[1].texcoord[0] = fcol + size;
	corner_verts[1].texcoord[1] = frow;

	corner_verts[2].position[0] = x + w;
	corner_verts[2].position[1] = y + h;
	corner_verts[2].position[2] = 0.0f;
	corner_verts[2].texcoord[0] = fcol + size;
	corner_verts[2].texcoord[1] = frow + size;

	corner_verts[3].position[0] = x;
	corner_verts[3].position[1] = y + h;
	corner_verts[3].position[2] = 0.0f;
	corner_verts[3].texcoord[0] = fcol;
	corner_verts[3].texcoord[1] = frow + size;

	for (i = 0; i < 4; ++i)
	{
		corner_verts[i].color[0] = rgb[0] * 255.0f;
		corner_verts[i].color[1] = rgb[1] * 255.0f;
		corner_verts[i].color[2] = rgb[2] * 255.0f;
		corner_verts[i].color[3] = alpha * 255.0f;
	}

	vertices[0] = corner_verts[0];
	vertices[1] = corner_verts[1];
	vertices[2] = corner_verts[2];
	vertices[3] = corner_verts[2];
	vertices[4] = corner_verts[3];
	vertices[5] = corner_verts[0];

	vulkan_globals.vk_cmd_bind_vertex_buffers (cbx->cb, 0, 1, &buffer, &buffer_offset);
	if (alpha_blend)
		R_BindPipeline (cbx, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_globals.basic_blend_pipeline[cbx->render_pass_index]);
	else
		R_BindPipeline (cbx, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_globals.basic_alphatest_pipeline[cbx->render_pass_index]);
	vulkan_globals.vk_cmd_bind_descriptor_sets (
		cbx->cb, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_globals.basic_pipeline_layout.handle, 0, 1, &char_texture->descriptor_set, 0, NULL);
	vulkan_globals.vk_cmd_draw (cbx->cb, 6, 1, 0, 0);
}
static void PF_cl_drawcharacter (void)
{
	extern gltexture_t *char_texture;

	float *pos = G_VECTOR (OFS_PARM0);
	int	   charcode = (int)G_FLOAT (OFS_PARM1) & 0xff;
	float *size = G_VECTOR (OFS_PARM2);
	float *rgb = G_VECTOR (OFS_PARM3);
	float  alpha = G_FLOAT (OFS_PARM4);

	if (charcode == 32)
		return; // don't waste time on spaces

	DrawQC_CharacterQuad (vulkan_globals.secondary_cb_contexts[SCBX_GUI], pos[0], pos[1], charcode, size[0], size[1], rgb, alpha);
}

static void PF_cl_drawrawstring (void)
{
	float	   *pos = G_VECTOR (OFS_PARM0);
	const char *text = G_STRING (OFS_PARM1);
	float	   *size = G_VECTOR (OFS_PARM2);
	float	   *rgb = G_VECTOR (OFS_PARM3);
	float		alpha = G_FLOAT (OFS_PARM4);

	float x = pos[0];
	int	  c;

	if (!*text)
		return; // don't waste time on spaces

	while ((c = *text++))
	{
		DrawQC_CharacterQuad (vulkan_globals.secondary_cb_contexts[SCBX_GUI], x, pos[1], c, size[0], size[1], rgb, alpha);
		x += size[0];
	}
}
static void PF_cl_drawstring (void)
{
	float	   *pos = G_VECTOR (OFS_PARM0);
	const char *text = G_STRING (OFS_PARM1);
	float	   *size = G_VECTOR (OFS_PARM2);
	float	   *rgb = G_VECTOR (OFS_PARM3);
	float		alpha = G_FLOAT (OFS_PARM4);

	float			x = pos[0];
	struct markup_s mu;
	int				c;

	if (!*text)
		return; // don't waste time on spaces

	PR_Markup_Begin (&mu, text, rgb, alpha);

	while ((c = PR_Markup_Parse (&mu)))
	{
		DrawQC_CharacterQuad (vulkan_globals.secondary_cb_contexts[SCBX_GUI], x, pos[1], c, size[0], size[1], rgb, alpha);
		x += size[0];
	}
}
static void PF_cl_stringwidth (void)
{
	static const float defaultfontsize[] = {8, 8};
	const char		  *text = G_STRING (OFS_PARM0);
	qboolean		   usecolours = G_FLOAT (OFS_PARM1);
	const float		  *fontsize = (qcvm->argc > 2) ? G_VECTOR (OFS_PARM2) : defaultfontsize;
	struct markup_s	   mu;
	int				   r = 0;

	if (!usecolours)
		r = strlen (text);
	else
	{
		PR_Markup_Begin (&mu, text, vec3_origin, 1);
		while (PR_Markup_Parse (&mu))
		{
			r += 1;
		}
	}

	// primitive and lame, but hey.
	G_FLOAT (OFS_RETURN) = fontsize[0] * r;
}

static void PF_cl_drawsetclip (void)
{
	float s = PR_GetVMScale ();

	float x = G_FLOAT (OFS_PARM0) * s;
	float y = G_FLOAT (OFS_PARM1) * s;
	float w = G_FLOAT (OFS_PARM2) * s;
	float h = G_FLOAT (OFS_PARM3) * s;

	VkRect2D render_area;
	render_area.offset.x = x;
	render_area.offset.y = y;
	render_area.extent.width = w;
	render_area.extent.height = h;
	vkCmdSetScissor (vulkan_globals.secondary_cb_contexts[SCBX_GUI][0].cb, 0, 1, &render_area);
}
static void PF_cl_drawresetclip (void)
{
	VkRect2D render_area;
	render_area.offset.x = 0;
	render_area.offset.y = 0;
	render_area.extent.width = vid.width;
	render_area.extent.height = vid.height;
	vkCmdSetScissor (vulkan_globals.secondary_cb_contexts[SCBX_GUI][0].cb, 0, 1, &render_area);
}

static void PF_cl_precachepic (void)
{
	const char	*name = G_STRING (OFS_PARM0);
	unsigned int flags = G_FLOAT (OFS_PARM1);

	G_INT (OFS_RETURN) = G_INT (OFS_PARM0); // return input string, for convienience

	if (!DrawQC_CachePic (name, flags) && (flags & PICFLAG_BLOCK))
		G_INT (OFS_RETURN) = 0; // return input string, for convienience
}
static void PF_cl_iscachedpic (void)
{
	const char *name = G_STRING (OFS_PARM0);
	if (DrawQC_CachePic (name, PICFLAG_NOLOAD))
		G_FLOAT (OFS_RETURN) = true;
	else
		G_FLOAT (OFS_RETURN) = false;
}

static void PF_cl_drawpic (void)
{
	float  *pos = G_VECTOR (OFS_PARM0);
	qpic_t *pic = DrawQC_CachePic (G_STRING (OFS_PARM1), PICFLAG_AUTO);
	float  *size = G_VECTOR (OFS_PARM2);
	float  *rgb = G_VECTOR (OFS_PARM3);
	float	alpha = G_FLOAT (OFS_PARM4);

	if (pic)
		Draw_SubPic (vulkan_globals.secondary_cb_contexts[SCBX_GUI], pos[0], pos[1], size[0], size[1], pic, 0, 0, 1, 1, rgb, alpha);
}

static void PF_cl_getimagesize (void)
{
	qpic_t *pic = DrawQC_CachePic (G_STRING (OFS_PARM0), PICFLAG_AUTO);
	if (pic)
		G_VECTORSET (OFS_RETURN, pic->width, pic->height, 0);
	else
		G_VECTORSET (OFS_RETURN, 0, 0, 0);
}

static void PF_cl_drawsubpic (void)
{
	float  *pos = G_VECTOR (OFS_PARM0);
	float  *size = G_VECTOR (OFS_PARM1);
	qpic_t *pic = DrawQC_CachePic (G_STRING (OFS_PARM2), PICFLAG_AUTO);
	float  *srcpos = G_VECTOR (OFS_PARM3);
	float  *srcsize = G_VECTOR (OFS_PARM4);
	float  *rgb = G_VECTOR (OFS_PARM5);
	float	alpha = G_FLOAT (OFS_PARM6);

	if (pic)
		Draw_SubPic (
			vulkan_globals.secondary_cb_contexts[SCBX_GUI], pos[0], pos[1], size[0], size[1], pic, srcpos[0], srcpos[1], srcsize[0], srcsize[1], rgb, alpha);
}

static void PF_cl_drawfill (void)
{
	int	   i;
	float *pos = G_VECTOR (OFS_PARM0);
	float *size = G_VECTOR (OFS_PARM1);
	float *rgb = G_VECTOR (OFS_PARM2);
	float  alpha = G_FLOAT (OFS_PARM3);

	VkBuffer	   buffer;
	VkDeviceSize   buffer_offset;
	basicvertex_t *vertices = (basicvertex_t *)R_VertexAllocate (6 * sizeof (basicvertex_t), &buffer, &buffer_offset);

	basicvertex_t corner_verts[4];
	memset (&corner_verts, 255, sizeof (corner_verts));

	corner_verts[0].position[0] = pos[0];
	corner_verts[0].position[1] = pos[1];
	corner_verts[0].position[2] = 0.0f;

	corner_verts[1].position[0] = pos[0] + size[0];
	corner_verts[1].position[1] = pos[1];
	corner_verts[1].position[2] = 0.0f;

	corner_verts[2].position[0] = pos[0] + size[0];
	corner_verts[2].position[1] = pos[1] + size[1];
	corner_verts[2].position[2] = 0.0f;

	corner_verts[3].position[0] = pos[0];
	corner_verts[3].position[1] = pos[1] + size[1];
	corner_verts[3].position[2] = 0.0f;

	for (i = 0; i < 4; ++i)
	{
		corner_verts[i].color[0] = rgb[0] * 255.0f;
		corner_verts[i].color[1] = rgb[1] * 255.0f;
		corner_verts[i].color[2] = rgb[2] * 255.0f;
		corner_verts[i].color[3] = alpha * 255.0f;
	}

	vertices[0] = corner_verts[0];
	vertices[1] = corner_verts[1];
	vertices[2] = corner_verts[2];
	vertices[3] = corner_verts[2];
	vertices[4] = corner_verts[3];
	vertices[5] = corner_verts[0];

	cb_context_t *cbx = vulkan_globals.secondary_cb_contexts[SCBX_GUI];
	vulkan_globals.vk_cmd_bind_vertex_buffers (cbx->cb, 0, 1, &buffer, &buffer_offset);
	R_BindPipeline (cbx, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_globals.basic_notex_blend_pipeline[cbx->render_pass_index]);
	vulkan_globals.vk_cmd_bind_descriptor_sets (
		cbx->cb, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_globals.basic_pipeline_layout.handle, 0, 1, &char_texture->descriptor_set, 0, NULL);
	vulkan_globals.vk_cmd_draw (cbx->cb, 6, 1, 0, 0);
}

void PF_cl_playerkey_internal (int player, const char *key, qboolean retfloat)
{
	char		buf[1024];
	const char *ret = buf;
	extern int	fragsort[MAX_SCOREBOARD];
	extern int	scoreboardlines;
	if (player < 0 && player >= -scoreboardlines)
		player = fragsort[-1 - player];
	if (player < 0 || player >= MAX_SCOREBOARD)
		ret = NULL;
	else if (!strcmp (key, "viewentity"))
		q_snprintf (buf, sizeof (buf), "%i", player + 1); // hack for DP compat. always returned even when the slot is empty (so long as its valid).
	else if (!*cl.scores[player].name)
		ret = NULL;
	else if (!strcmp (key, "name"))
		ret = cl.scores[player].name;
	else if (!strcmp (key, "frags"))
		q_snprintf (buf, sizeof (buf), "%i", cl.scores[player].frags);
	else if (!strcmp (key, "ping"))
		q_snprintf (buf, sizeof (buf), "%i", cl.scores[player].ping);
	else if (!strcmp (key, "pl"))
		ret = NULL; // unknown
	else if (!strcmp (key, "entertime"))
		q_snprintf (buf, sizeof (buf), "%g", cl.scores[player].entertime);
	else if (!strcmp (key, "topcolor_rgb"))
	{
		int	  color = cl.scores[player].colors >> 4;
		byte *pal = (byte *)(d_8to24table + (color * 16 + 8));
		q_snprintf (buf, sizeof (buf), "%g %g %g", pal[0] / 255.0, pal[1] / 255.0, pal[2] / 255.0);
	}
	else if (!strcmp (key, "bottomcolor_rgb"))
	{
		int	  color = cl.scores[player].colors & 0xf;
		byte *pal = (byte *)(d_8to24table + (color * 16 + 8));
		q_snprintf (buf, sizeof (buf), "%g %g %g", pal[0] / 255.0, pal[1] / 255.0, pal[2] / 255.0);
	}
	else if (!strcmp (key, "topcolor"))
		ret = va ("%i", cl.scores[player].colors >> 4);
	else if (!strcmp (key, "bottomcolor"))
		ret = va ("%i", cl.scores[player].colors & 0xf);
	else if (!strcmp (key, "team")) // quakeworld uses team infokeys to decide teams (instead of colours). but NQ never did, so that's fun. Lets allow mods to
									// use either so that they can favour QW and let the engine hide differences .
		q_snprintf (buf, sizeof (buf), "%i", (cl.scores[player].colors & 0xf) + 1);
	else if (!strcmp (key, "userid"))
		ret = NULL; // unknown
	else
	{
		ret = Info_GetKey (cl.scores[player].userinfo, key, buf, sizeof (buf));
		if (!*ret)
			ret = NULL;
	}

	if (retfloat)
		G_FLOAT (OFS_RETURN) = ret ? atof (ret) : 0;
	else
		G_INT (OFS_RETURN) = ret ? PR_MakeTempString (ret) : 0;
}

static void PF_cl_playerkey_s (void)
{
	int			playernum = G_FLOAT (OFS_PARM0);
	const char *keyname = G_STRING (OFS_PARM1);
	PF_cl_playerkey_internal (playernum, keyname, false);
}

static void PF_cl_playerkey_f (void)
{
	int			playernum = G_FLOAT (OFS_PARM0);
	const char *keyname = G_STRING (OFS_PARM1);
	PF_cl_playerkey_internal (playernum, keyname, true);
}

static void PF_cl_registercommand (void)
{
	const char *cmdname = G_STRING (OFS_PARM0);
	Cmd_AddCommand (cmdname, NULL);
}
static void PF_uri_get (void)
{
	G_VECTORSET (OFS_RETURN, 0, 0, 0);
}

static void PF_touchtriggers (void)
{
	edict_t *e;
	float	*org;

	e = (qcvm->argc > 0) ? G_EDICT (OFS_PARM0) : G_EDICT (pr_global_struct->self);
	if (qcvm->argc > 1)
	{
		org = G_VECTOR (OFS_PARM1);
		VectorCopy (org, e->v.origin);
	}
	SV_LinkEdict (e, true);
}

static void PF_checkpvs (void)
{
	float	*org = G_VECTOR (OFS_PARM0);
	edict_t *ed = G_EDICT (OFS_PARM1);

	mleaf_t		*leaf = Mod_PointInLeaf (org, qcvm->worldmodel);
	byte		*pvs = Mod_LeafPVS (leaf, qcvm->worldmodel); // johnfitz -- worldmodel as a parameter
	unsigned int i;

	for (i = 0; i < ed->num_leafs; i++)
	{
		if (pvs[ed->leafnums[i] >> 3] & (1 << (ed->leafnums[i] & 7)))
		{
			G_FLOAT (OFS_RETURN) = true;
			return;
		}
	}

	G_FLOAT (OFS_RETURN) = false;
}

// A quick note on number ranges.
// 0: automatically assigned. more complicated, but no conflicts over numbers, just names...
//    NOTE: #0 is potentially ambiguous - vanilla will interpret it as instruction 0 (which is normally reserved) rather than a builtin.
//          if such functions were actually used, this would cause any 64bit engines that switched to unsigned types to crash due to an underflow.
//          we do some sneaky hacks to avoid changes to the vm... because we're evil.
// 0-199: free for all.
// 200-299: fte's random crap
// 300-399: csqc's random crap
// 400+: dp's random crap
// clang-format off
static struct
{
	const char *name;
	builtin_t ssqcfunc;
	builtin_t csqcfunc;
	int documentednumber;
	const char *typestr;
	const char *desc;
	int number;
} extensionbuiltins[] = 
#define PF_NoSSQC NULL
#define PF_NoCSQC NULL
{
	{"vectoangles2",				PF_ext_vectoangles,				PF_ext_vectoangles,				51,	    D("vector(vector fwd, optional vector up)", "Returns the angles (+x=UP) required to orient an entity to look in the given direction. The 'up' argument is required if you wish to set a roll angle, otherwise it will be limited to just monster-style turning.")},
	{"sin",							PF_Sin,							PF_Sin,							60,		"float(float angle)"},	//60
	{"cos",							PF_Cos,							PF_Cos,							61,		"float(float angle)"},	//61
	{"sqrt",						PF_Sqrt,						PF_Sqrt,						62,		"float(float value)"},	//62
	{"tracetoss",					PF_TraceToss,					PF_TraceToss,					64,		"void(entity ent, entity ignore)"},
	{"etos",						PF_etos,						PF_etos,						65,		"string(entity ent)"},
	{"etof",						PF_num_for_edict,				PF_num_for_edict,				0, 		"float(entity ent)"},
	{"ftoe",						PF_edict_for_num,				PF_edict_for_num,				0, 		"entity(float ent)"},
	{"infokey",						PF_infokey_s,					PF_NoCSQC,						80,		D("string(entity e, string key)", "If e is world, returns the field 'key' from either the serverinfo or the localinfo. If e is a player, returns the value of 'key' from the player's userinfo string. There are a few special exceptions, like 'ip' which is not technically part of the userinfo.")},	//80
	{"infokeyf",					PF_infokey_f,					PF_NoCSQC,						0,		D("float(entity e, string key)", "Identical to regular infokey, but returns it as a float instead of creating new tempstrings.")},	//80
	{"stof",						PF_stof,						PF_stof,						81,		"float(string)"},	//81
	{"multicast",					PF_multicast,					PF_NoCSQC,						82,		D("#define unicast(pl,reli) do{msg_entity = pl; multicast('0 0 0', reli?MULITCAST_ONE_R:MULTICAST_ONE);}while(0)\n"
																											"void(vector where, float set)", "Once the MSG_MULTICAST network message buffer has been filled with data, this builtin is used to dispatch it to the given target, filtering by pvs for reduced network bandwidth.")},	//82
	{"tracebox",					PF_tracebox,					PF_tracebox,					90,		D("void(vector start, vector mins, vector maxs, vector end, float nomonsters, entity ent)", "Exactly like traceline, but a box instead of a uselessly thin point. Acceptable sizes are limited by bsp format, q1bsp has strict acceptable size values.")},
	{"randomvec",					PF_randomvector,				PF_randomvector,				91,		D("vector()", "Returns a vector with random values. Each axis is independantly a value between -1 and 1 inclusive.")},
	{"getlight",					PF_sv_getlight,					PF_cl_getlight,					92,		"vector(vector org)"},// (DP_QC_GETLIGHT),
	{"registercvar",				PF_registercvar,				PF_registercvar,				93,		D("float(string cvarname, string defaultvalue)", "Creates a new cvar on the fly. If it does not already exist, it will be given the specified value. If it does exist, this is a no-op.\nThis builtin has the limitation that it does not apply to configs or commandlines. Such configs will need to use the set or seta command causing this builtin to be a noop.\nIn engines that support it, you will generally find the autocvar feature easier and more efficient to use.")},
	{"min",							PF_min,							PF_min,							94,		D("float(float a, float b, ...)", "Returns the lowest value of its arguments.")},// (DP_QC_MINMAXBOUND)
	{"max",							PF_max,							PF_max,							95,		D("float(float a, float b, ...)", "Returns the highest value of its arguments.")},// (DP_QC_MINMAXBOUND)
	{"bound",						PF_bound,						PF_bound,						96,		D("float(float minimum, float val, float maximum)", "Returns val, unless minimum is higher, or maximum is less.")},// (DP_QC_MINMAXBOUND)
	{"pow",							PF_pow,							PF_pow,							97,		"float(float value, float exp)"},
	{"findfloat",					PF_findfloat,					PF_findfloat,					98,		D("#define findentity findfloat\nentity(entity start, .__variant fld, __variant match)", "Equivelent to the find builtin, but instead of comparing strings contents, this builtin compares the raw values. This builtin requires multiple calls in order to scan all entities - set start to the previous call's return value.\nworld is returned when there are no more entities.")},	// #98 (DP_QC_FINDFLOAT)
	{"checkextension",				PF_checkextension,				PF_checkextension,				99,		D("float(string extname)", "Checks for an extension by its name (eg: checkextension(\"FRIK_FILE\") says that its okay to go ahead and use strcat).\nUse cvar(\"pr_checkextension\") to see if this builtin exists.")},	// #99	//darkplaces system - query a string to see if the mod supports X Y and Z.
	{"checkbuiltin",				PF_checkbuiltin,				PF_checkbuiltin,				0,		D("float(__variant funcref)", "Checks to see if the specified builtin is supported/mapped. This is intended as a way to check for #0 functions, allowing for simple single-builtin functions.")},
	{"builtin_find",				PF_builtinsupported,			PF_builtinsupported,			100,	D("float(string builtinname)", "Looks to see if the named builtin is valid, and returns the builtin number it exists at.")},	// #100	//per builtin system.
	{"anglemod",					PF_anglemod,					PF_anglemod,					102,	"float(float value)"},	//telejano
	{"strlen",						PF_strlen,						PF_strlen,						114,	"float(string s)"},	// (FRIK_FILE)
	{"strcat",						PF_strcat,						PF_strcat,						115,	"string(string s1, optional string s2, optional string s3, optional string s4, optional string s5, optional string s6, optional string s7, optional string s8)"},	// (FRIK_FILE)
	{"substring",					PF_substring,					PF_substring,					116,	"string(string s, float start, float length)"},	// (FRIK_FILE)
	{"stov",						PF_stov,						PF_stov,						117,	"vector(string s)"},	// (FRIK_FILE)
	{"strzone",						PF_strzone,						PF_strzone,						118,	D("string(string s, ...)", "Create a semi-permanent copy of a string that only becomes invalid once strunzone is called on the string (instead of when the engine assumes your string has left scope).")},	// (FRIK_FILE)
	{"strunzone",					PF_strunzone,					PF_strunzone,					119,	D("void(string s)", "Destroys a string that was allocated by strunzone. Further references to the string MAY crash the game.")},	// (FRIK_FILE)
	{"tokenize_menuqc",				PF_Tokenize,					PF_Tokenize,					0,		"float(string s)"},
	{"localsound",					PF_NoSSQC,						PF_cl_localsound,				177,	D("void(string soundname, optional float channel, optional float volume)", "Plays a sound... locally... probably best not to call this from ssqc. Also disables reverb.")},//	#177
	{"bitshift",					PF_bitshift,					PF_bitshift,					218,	"float(float number, float quantity)"},
	{"te_lightningblood",			PF_sv_te_lightningblood,		NULL,							219,	"void(vector org)"},
	{"strstrofs",					PF_strstrofs,					PF_strstrofs,					221,	D("float(string s1, string sub, optional float startidx)", "Returns the 0-based offset of sub within the s1 string, or -1 if sub is not in s1.\nIf startidx is set, this builtin will ignore matches before that 0-based offset.")},
	{"str2chr",						PF_str2chr,						PF_str2chr,						222,	D("float(string str, float index)", "Retrieves the character value at offset 'index'.")},
	{"chr2str",						PF_chr2str,						PF_chr2str,						223,	D("string(float chr, ...)", "The input floats are considered character values, and are concatenated.")},
	{"strconv",						PF_strconv,						PF_strconv,						224,	D("string(float ccase, float redalpha, float redchars, string str, ...)", "Converts quake chars in the input string amongst different representations.\nccase specifies the new case for letters.\n 0: not changed.\n 1: forced to lower case.\n 2: forced to upper case.\nredalpha and redchars switch between colour ranges.\n 0: no change.\n 1: Forced white.\n 2: Forced red.\n 3: Forced gold(low) (numbers only).\n 4: Forced gold (high) (numbers only).\n 5+6: Forced to white and red alternately.\nYou should not use this builtin in combination with UTF-8.")},
	{"strpad",						PF_strpad,						PF_strpad,						225,	D("string(float pad, string str1, ...)", "Pads the string with spaces, to ensure its a specific length (so long as a fixed-width font is used, anyway). If pad is negative, the spaces are added on the left. If positive the padding is on the right.")},	//will be moved
	{"infoadd",						PF_infoadd,						PF_infoadd,						226,	D("string(infostring old, string key, string value)", "Returns a new tempstring infostring with the named value changed (or added if it was previously unspecified). Key and value may not contain the \\ character.")},
	{"infoget",						PF_infoget,						PF_infoget,						227,	D("string(infostring info, string key)", "Reads a named value from an infostring. The returned value is a tempstring")},
	{"strncmp",						PF_strncmp,						PF_strncmp,						228,	D("#define strcmp strncmp\nfloat(string s1, string s2, optional float len, optional float s1ofs, optional float s2ofs)", "Compares up to 'len' chars in the two strings. s1ofs allows you to treat s2 as a substring to compare against, or should be 0.\nReturns 0 if the two strings are equal, a negative value if s1 appears numerically lower, and positive if s1 appears numerically higher.")},
	{"strcasecmp",					PF_strncasecmp,					PF_strncasecmp,					229,	D("float(string s1, string s2)",  "Compares the two strings without case sensitivity.\nReturns 0 if they are equal. The sign of the return value may be significant, but should not be depended upon.")},
	{"strncasecmp",					PF_strncasecmp,					PF_strncasecmp,					230,	D("float(string s1, string s2, float len, optional float s1ofs, optional float s2ofs)", "Compares up to 'len' chars in the two strings without case sensitivity. s1ofs allows you to treat s2 as a substring to compare against, or should be 0.\nReturns 0 if they are equal. The sign of the return value may be significant, but should not be depended upon.")},
	{"strtrim",						PF_strtrim,						PF_strtrim,						0,		D("string(string s)", "Trims the whitespace from the start+end of the string.")},
	{"clientstat",					PF_clientstat,					PF_NoCSQC,						232,	D("void(float num, float type, .__variant fld)", "Specifies what data to use in order to send various stats, in a client-specific way.\n'num' should be a value between 32 and 127, other values are reserved.\n'type' must be set to one of the EV_* constants, one of EV_FLOAT, EV_STRING, EV_INTEGER, EV_ENTITY.\nfld must be a reference to the field used, each player will be sent only their own copy of these fields.")},	//EXT_CSQC
	{"isbackbuffered",				PF_isbackbuffered,				PF_NoCSQC,						234,	D("float(entity player)", "Returns if the given player's network buffer will take multiple network frames in order to clear. If this builtin returns non-zero, you should delay or reduce the amount of reliable (and also unreliable) data that you are sending to that client.")},
	{"te_bloodqw",					PF_sv_te_bloodqw,				NULL,							239,	"void(vector org, float count)"},
	{"checkpvs",					PF_checkpvs,					PF_checkpvs,					240,	"float(vector viewpos, entity entity)"},
	{"mod",							PF_mod,							PF_mod,							245,	"float(float a, float n)"},
	{"stoi",						PF_stoi,						PF_stoi,						259,	D("int(string)", "Converts the given string into a true integer. Base 8, 10, or 16 is determined based upon the format of the string.")},
	{"itos",						PF_itos,						PF_itos,						260,	D("string(int)", "Converts the passed true integer into a base10 string.")},
	{"stoh",						PF_stoh,						PF_stoh,						261,	D("int(string)", "Reads a base-16 string (with or without 0x prefix) as an integer. Bugs out if given a base 8 or base 10 string. :P")},
	{"htos",						PF_htos,						PF_htos,						262,	D("string(int)", "Formats an integer as a base16 string, with leading 0s and no prefix. Always returns 8 characters.")},
	{"ftoi",						PF_ftoi,						PF_ftoi,						0,		D("int(float)", "Converts the given float into a true integer without depending on extended qcvm instructions.")},
	{"itof",						PF_itof,						PF_itof,						0,		D("float(int)", "Converts the given true integer into a float without depending on extended qcvm instructions.")},
	{"crossproduct",				PF_crossproduct,				PF_crossproduct,				0,		D("#ifndef dotproduct\n#define dotproduct(v1,v2) ((vector)(v1)*(vector)(v2))\n#endif\nvector(vector v1, vector v2)", "Small helper function to calculate the crossproduct of two vectors.")},
	{"frameforname",				PF_frameforname,				PF_frameforname,				276,	D("float(float modidx, string framename)", "Looks up a framegroup from a model by name, avoiding the need for hardcoding. Returns -1 on error.")},// (FTE_CSQC_SKELETONOBJECTS)
	{"frameduration",				PF_frameduration,				PF_frameduration,				277,	D("float(float modidx, float framenum)", "Retrieves the duration (in seconds) of the specified framegroup.")},// (FTE_CSQC_SKELETONOBJECTS)
	{"touchtriggers",				PF_touchtriggers,				PF_touchtriggers,				279,	D("void(optional entity ent, optional vector neworigin)", "Triggers a touch events between self and every SOLID_TRIGGER entity that it is in contact with. This should typically just be the triggers touch functions. Also optionally updates the origin of the moved entity.")},//
	{"WriteFloat",					PF_WriteFloat,					PF_NoCSQC,						280,	"void(float buf, float fl)"},
	{"frametoname",					PF_frametoname,					PF_frametoname,					284,	"string(float modidx, float framenum)"},
	{"checkcommand",				PF_checkcommand,				PF_checkcommand,				294,	D("float(string name)", "Checks to see if the supplied name is a valid command, cvar, or alias. Returns 0 if it does not exist.")},
	{"iscachedpic",					PF_NoSSQC,						PF_cl_iscachedpic,				316,	D("float(string name)", "Checks to see if the image is currently loaded. Engines might lie, or cache between maps.")},// (EXT_CSQC)
	{"precache_pic",				PF_NoSSQC,						PF_cl_precachepic,				317,	D("string(string name, optional float flags)", "Forces the engine to load the named image. If trywad is specified, the specified name must any lack path and extension.")},// (EXT_CSQC)
	{"drawgetimagesize",			PF_NoSSQC,						PF_cl_getimagesize,				318,	D("#define draw_getimagesize drawgetimagesize\nvector(string picname)", "Returns the dimensions of the named image. Images specified with .lmp should give the original .lmp's dimensions even if texture replacements use a different resolution.")},// (EXT_CSQC)
	{"drawcharacter",				PF_NoSSQC,						PF_cl_drawcharacter,			320,	D("float(vector position, float character, vector size, vector rgb, float alpha, optional float drawflag)", "Draw the given quake character at the given position.\nIf flag&4, the function will consider the char to be a unicode char instead (or display as a ? if outside the 32-127 range).\nsize should normally be something like '8 8 0'.\nrgb should normally be '1 1 1'\nalpha normally 1.\nSoftware engines may assume the named defaults.\nNote that ALL text may be rescaled on the X axis due to variable width fonts. The X axis may even be ignored completely.")},// (EXT_CSQC, [EXT_CSQC_???])
	{"drawrawstring",				PF_NoSSQC,						PF_cl_drawrawstring,			321,	D("float(vector position, string text, vector size, vector rgb, float alpha, optional float drawflag)", "Draws the specified string without using any markup at all, even in engines that support it.\nIf UTF-8 is globally enabled in the engine, then that encoding is used (without additional markup), otherwise it is raw quake chars.\nSoftware engines may assume a size of '8 8 0', rgb='1 1 1', alpha=1, flag&3=0, but it is not an error to draw out of the screen.")},// (EXT_CSQC, [EXT_CSQC_???])
	{"drawpic",						PF_NoSSQC,						PF_cl_drawpic,					322,	D("float(vector position, string pic, vector size, vector rgb, float alpha, optional float drawflag)", "Draws an shader within the given 2d screen box. Software engines may omit support for rgb+alpha, but must support rescaling, and must clip to the screen without crashing.")},// (EXT_CSQC, [EXT_CSQC_???])
	{"drawfill",					PF_NoSSQC,						PF_cl_drawfill,					323,	D("float(vector position, vector size, vector rgb, float alpha, optional float drawflag)", "Draws a solid block over the given 2d box, with given colour, alpha, and blend mode (specified via flags).\nflags&3=0 simple blend.\nflags&3=1 additive blend")},// (EXT_CSQC, [EXT_CSQC_???])
	{"drawsetcliparea",				PF_NoSSQC,						PF_cl_drawsetclip,				324,	D("void(float x, float y, float width, float height)", "Specifies a 2d clipping region (aka: scissor test). 2d draw calls will all be clipped to this 2d box, the area outside will not be modified by any 2d draw call (even 2d polygons).")},// (EXT_CSQC_???)
	{"drawresetcliparea",			PF_NoSSQC,						PF_cl_drawresetclip,			325,	D("void(void)", "Reverts the scissor/clip area to the whole screen.")},// (EXT_CSQC_???)
	{"drawstring",					PF_NoSSQC,						PF_cl_drawstring,				326,	D("float(vector position, string text, vector size, vector rgb, float alpha, float drawflag)", "Draws a string, interpreting markup and recolouring as appropriate.")},// #326
	{"stringwidth",					PF_NoSSQC,						PF_cl_stringwidth,				327,	D("float(string text, float usecolours, vector fontsize='8 8')", "Calculates the width of the screen in virtual pixels. If usecolours is 1, markup that does not affect the string width will be ignored. Will always be decoded as UTF-8 if UTF-8 is globally enabled.\nIf the char size is not specified, '8 8 0' will be assumed.")},// EXT_CSQC_'DARKPLACES'
	{"drawsubpic",					PF_NoSSQC,						PF_cl_drawsubpic,				328,	D("void(vector pos, vector sz, string pic, vector srcpos, vector srcsz, vector rgb, float alpha, optional float drawflag)", "Draws a rescaled subsection of an image to the screen.")},// #328 EXT_CSQC_'DARKPLACES'
	{"getstati",					PF_NoSSQC,						PF_cl_getstat_int,				330,	D("#define getstati_punf(stnum) (float)(__variant)getstati(stnum)\nint(float stnum)", "Retrieves the numerical value of the given EV_INTEGER or EV_ENTITY stat. Use getstati_punf if you wish to type-pun a float stat as an int to avoid truncation issues in DP.")},// (EXT_CSQC)
	{"getstatf",					PF_NoSSQC,						PF_cl_getstat_float,			331,	D("#define getstatbits getstatf\nfloat(float stnum, optional float firstbit, optional float bitcount)", "Retrieves the numerical value of the given EV_FLOAT stat. If firstbit and bitcount are specified, retrieves the upper bits of the STAT_ITEMS stat (converted into a float, so there are no VM dependancies).")},// (EXT_CSQC)
	{"getstats",					PF_NoSSQC,						PF_cl_getstat_string,			332,	D("string(float stnum)", "Retrieves the value of the given EV_STRING stat, as a tempstring.\nString stats use a separate pool of stats from numeric ones.\n")},
	{"setmodelindex",				PF_sv_setmodelindex,			PF_cl_setmodelindex,			333,	D("void(entity e, float mdlindex)", "Sets a model by precache index instead of by name. Otherwise identical to setmodel.")},//
	{"particleeffectnum",			PF_sv_particleeffectnum,		PF_cl_particleeffectnum,		335,	D("float(string effectname)", "Precaches the named particle effect. If your effect name is of the form 'foo.bar' then particles/foo.cfg will be loaded by the client if foo.bar was not already defined.\nDifferent engines will have different particle systems, this specifies the QC API only.")},// (EXT_CSQC)
	{"trailparticles",				PF_sv_trailparticles,			PF_cl_trailparticles,			336,	D("void(float effectnum, entity ent, vector start, vector end)", "Draws the given effect between the two named points. If ent is not world, distances will be cached in the entity in order to avoid framerate dependancies. The entity is not otherwise used.")},// (EXT_CSQC),
	{"pointparticles",				PF_sv_pointparticles,			PF_cl_pointparticles,			337,	D("void(float effectnum, vector origin, optional vector dir, optional float count)", "Spawn a load of particles from the given effect at the given point traveling or aiming along the direction specified. The number of particles are scaled by the count argument.")},// (EXT_CSQC)
	{"print",						PF_print,						PF_print,						339,	D("void(string s, ...)", "Unconditionally print on the local system's console, even in ssqc (doesn't care about the value of the developer cvar).")},//(EXT_CSQC)
	{"getplayerkeyvalue",			NULL,							PF_cl_playerkey_s,				348,	D("string(float playernum, string keyname)", "Look up a player's userinfo, to discover things like their name, topcolor, bottomcolor, skin, team, *ver.\nAlso includes scoreboard info like frags, ping, pl, userid, entertime, as well as voipspeaking and voiploudness.")},// (EXT_CSQC)
	{"getplayerkeyfloat",			NULL,							PF_cl_playerkey_f,				0,		D("float(float playernum, string keyname, optional float assumevalue)", "Cheaper version of getplayerkeyvalue that avoids the need for so many tempstrings.")},
	{"registercommand",				NULL,							PF_cl_registercommand,			352,	D("void(string cmdname)", "Register the given console command, for easy console use.\nConsole commands that are later used will invoke CSQC_ConsoleCommand.")},//(EXT_CSQC)
	{"wasfreed",					PF_WasFreed,					PF_WasFreed,					353,	D("float(entity ent)", "Quickly check to see if the entity is currently free. This function is only valid during the two-second non-reuse window, after that it may give bad results. Try one second to make it more robust.")},//(EXT_CSQC) (should be availabe on server too)
	{"copyentity",					PF_copyentity,					PF_copyentity,					400,	D("entity(entity from, optional entity to)", "Copies all fields from one entity to another.")},// (DP_QC_COPYENTITY)
	{"findchain",					PF_findchain,					PF_findchain,					402,	"entity(.string field, string match, optional .entity chainfield)"},// (DP_QC_FINDCHAIN)
	{"findchainfloat",				PF_findchainfloat,				PF_findchainfloat,				403,	"entity(.float fld, float match, optional .entity chainfield)"},// (DP_QC_FINDCHAINFLOAT)
	{"te_blood",					PF_sv_te_blooddp,				NULL,							405,	"void(vector org, vector dir, float count)"},// #405 te_blood
	{"te_particlerain",				PF_sv_te_particlerain,			NULL,							409,	"void(vector mincorner, vector maxcorner, vector vel, float howmany, float color)"},// (DP_TE_PARTICLERAIN)
	{"te_particlesnow",				PF_sv_te_particlesnow,			NULL,							410,	"void(vector mincorner, vector maxcorner, vector vel, float howmany, float color)"},// (DP_TE_PARTICLESNOW)
	{"te_gunshot",					PF_sv_te_gunshot,				PF_cl_te_gunshot,				418,	"void(vector org, optional float count)"},// #418 te_gunshot
	{"te_spike",					PF_sv_te_spike,					PF_cl_te_spike,					419,	"void(vector org)"},// #419 te_spike
	{"te_superspike",				PF_sv_te_superspike,			PF_cl_te_superspike,			420,	"void(vector org)"},// #420 te_superspike
	{"te_explosion",				PF_sv_te_explosion,				PF_cl_te_explosion,				421,	"void(vector org)"},// #421 te_explosion
	{"te_tarexplosion",				PF_sv_te_tarexplosion,			PF_cl_te_tarexplosion,			422,	"void(vector org)"},// #422 te_tarexplosion
	{"te_wizspike",					PF_sv_te_wizspike,				PF_cl_te_wizspike,				423,	"void(vector org)"},// #423 te_wizspike
	{"te_knightspike",				PF_sv_te_knightspike,			PF_cl_te_knightspike,			424,	"void(vector org)"},// #424 te_knightspike
	{"te_lavasplash",				PF_sv_te_lavasplash,			PF_cl_te_lavasplash,			425,	"void(vector org)"},// #425 te_lavasplash
	{"te_teleport",					PF_sv_te_teleport,				PF_cl_te_teleport,				426,	"void(vector org)"},// #426 te_teleport
	{"te_explosion2",				PF_sv_te_explosion2,			PF_cl_te_explosion2,			427,	"void(vector org, float color, float colorlength)"},// #427 te_explosion2
	{"te_lightning1",				PF_sv_te_lightning1,			PF_cl_te_lightning1,			428,	"void(entity own, vector start, vector end)"},// #428 te_lightning1
	{"te_lightning2",				PF_sv_te_lightning2,			PF_cl_te_lightning2,			429,	"void(entity own, vector start, vector end)"},// #429 te_lightning2
	{"te_lightning3",				PF_sv_te_lightning3,			PF_cl_te_lightning3,			430,	"void(entity own, vector start, vector end)"},// #430 te_lightning3
	{"te_beam",						PF_sv_te_beam,					PF_cl_te_beam,					431,	"void(entity own, vector start, vector end)"},// #431 te_beam
	{"vectorvectors",				PF_vectorvectors,				PF_vectorvectors,				432,	"void(vector dir)"},// (DP_QC_VECTORVECTORS)
	{"getsurfacenumpoints",			PF_getsurfacenumpoints,			PF_getsurfacenumpoints,			434,	"float(entity e, float s)"},// (DP_QC_GETSURFACE)
	{"getsurfacepoint",				PF_getsurfacepoint,				PF_getsurfacepoint,				435,	"vector(entity e, float s, float n)"},// (DP_QC_GETSURFACE)
	{"getsurfacenormal",			PF_getsurfacenormal,			PF_getsurfacenormal,			436,	"vector(entity e, float s)"},// (DP_QC_GETSURFACE)
	{"getsurfacetexture",			PF_getsurfacetexture,			PF_getsurfacetexture,			437,	"string(entity e, float s)"},// (DP_QC_GETSURFACE)
	{"getsurfacenearpoint",			PF_getsurfacenearpoint,			PF_getsurfacenearpoint,			438,	"float(entity e, vector p)"},// (DP_QC_GETSURFACE)
	{"getsurfaceclippedpoint",		PF_getsurfaceclippedpoint,		PF_getsurfaceclippedpoint,		439,	"vector(entity e, float s, vector p)"},// (DP_QC_GETSURFACE)
	{"clientcommand",				PF_clientcommand,				PF_NoCSQC,						440,	"void(entity e, string s)"},// (KRIMZON_SV_PARSECLIENTCOMMAND)
	{"tokenize",					PF_Tokenize,					PF_Tokenize,					441,	"float(string s)"},// (KRIMZON_SV_PARSECLIENTCOMMAND)
	{"argv",						PF_ArgV,						PF_ArgV,						442,	"string(float n)"},// (KRIMZON_SV_PARSECLIENTCOMMAND
	{"argc",						PF_ArgC,						PF_ArgC,						0,		"float()"},
	{"setattachment",				PF_setattachment,				PF_setattachment,				443,	"void(entity e, entity tagentity, string tagname)", ""},// (DP_GFX_QUAKE3MODELTAGS)
	{"cvar_string",					PF_cvar_string,					PF_cvar_string,					448,	 "string(string cvarname)"},//DP_QC_CVAR_STRING
	{"findflags",					PF_findflags,					PF_findflags,					449,	"entity(entity start, .float fld, float match)"},//DP_QC_FINDFLAGS
	{"findchainflags",				PF_findchainflags,				PF_findchainflags,				450,	"entity(.float fld, float match, optional .entity chainfield)"},//DP_QC_FINDCHAINFLAGS
	{"dropclient",					PF_dropclient,					PF_NoCSQC,						453,	"void(entity player)"},//DP_SV_BOTCLIENT
	{"spawnclient",					PF_spawnclient,					PF_NoCSQC,						454,	"entity()", "Spawns a dummy player entity.\nNote that such dummy players will be carried from one map to the next.\nWarning: DP_SV_CLIENTCOLORS DP_SV_CLIENTNAME are not implemented in quakespasm, so use KRIMZON_SV_PARSECLIENTCOMMAND's clientcommand builtin to change the bot's name/colours/skin/team/etc, in the same way that clients would ask."},//DP_SV_BOTCLIENT
	{"clienttype",					PF_clienttype,					PF_NoCSQC,						455,	"float(entity client)"},//botclient
	{"WriteUnterminatedString",		PF_WriteString2,				PF_NoCSQC,						456,	"void(float target, string str)"},	//writestring but without the null terminator. makes things a little nicer.
	{"edict_num",					PF_edict_for_num,				PF_edict_for_num,				459,	"entity(float entnum)"},//DP_QC_EDICT_NUM
	{"buf_create",					PF_buf_create,					PF_buf_create,					460,	"strbuf()"},//DP_QC_STRINGBUFFERS
	{"buf_del",						PF_buf_del,						PF_buf_del,						461,	"void(strbuf bufhandle)"},//DP_QC_STRINGBUFFERS
	{"buf_getsize",					PF_buf_getsize,					PF_buf_getsize,					462,	"float(strbuf bufhandle)"},//DP_QC_STRINGBUFFERS
	{"buf_copy",					PF_buf_copy,					PF_buf_copy,					463,	"void(strbuf bufhandle_from, strbuf bufhandle_to)"},//DP_QC_STRINGBUFFERS
	{"buf_sort",					PF_buf_sort,					PF_buf_sort,					464,	"void(strbuf bufhandle, float sortprefixlen, float backward)"},//DP_QC_STRINGBUFFERS
	{"buf_implode",					PF_buf_implode,					PF_buf_implode,					465,	"string(strbuf bufhandle, string glue)"},//DP_QC_STRINGBUFFERS
	{"bufstr_get",					PF_bufstr_get,					PF_bufstr_get,					466,	"string(strbuf bufhandle, float string_index)"},//DP_QC_STRINGBUFFERS
	{"bufstr_set",					PF_bufstr_set,					PF_bufstr_set,					467,	"void(strbuf bufhandle, float string_index, string str)"},//DP_QC_STRINGBUFFERS
	{"bufstr_add",					PF_bufstr_add,					PF_bufstr_add,					468,	"float(strbuf bufhandle, string str, float order)"},//DP_QC_STRINGBUFFERS
	{"bufstr_free",					PF_bufstr_free,					PF_bufstr_free,					469,	"void(strbuf bufhandle, float string_index)"},//DP_QC_STRINGBUFFERS
	{"asin",						PF_asin,						PF_asin,						471,	"float(float s)"},//DP_QC_ASINACOSATANATAN2TAN
	{"acos",						PF_acos,						PF_acos,						472,	"float(float c)"},//DP_QC_ASINACOSATANATAN2TAN
	{"atan",						PF_atan,						PF_atan,						473,	"float(float t)"},//DP_QC_ASINACOSATANATAN2TAN
	{"atan2",						PF_atan2,						PF_atan2,						474,	"float(float c, float s)"},//DP_QC_ASINACOSATANATAN2TAN
	{"tan",							PF_tan,							PF_tan,							475,	"float(float a)"},//DP_QC_ASINACOSATANATAN2TAN
	{"strlennocol",					PF_strlennocol,					PF_strlennocol,					476,	D("float(string s)", "Returns the number of characters in the string after any colour codes or other markup has been parsed.")},//DP_QC_STRINGCOLORFUNCTIONS
	{"strdecolorize",				PF_strdecolorize,				PF_strdecolorize,				477,	D("string(string s)", "Flattens any markup/colours, removing them from the string.")},//DP_QC_STRINGCOLORFUNCTIONS
	{"strftime",					PF_strftime,					PF_strftime,					478,	"string(float uselocaltime, string format, ...)"},	//DP_QC_STRFTIME
	{"tokenizebyseparator",			PF_tokenizebyseparator,			PF_tokenizebyseparator,			479,	"float(string s, string separator1, ...)"},	//DP_QC_TOKENIZEBYSEPARATOR
	{"strtolower",					PF_strtolower,					PF_strtolower,					480,	"string(string s)"},	//DP_QC_STRING_CASE_FUNCTIONS
	{"strtoupper",					PF_strtoupper,					PF_strtoupper,					481,	"string(string s)"},	//DP_QC_STRING_CASE_FUNCTIONS
	{"cvar_defstring",				PF_cvar_defstring,				PF_cvar_defstring,				482,	"string(string s)"},	//DP_QC_CVAR_DEFSTRING
	{"pointsound",					PF_sv_pointsound,				PF_cl_pointsound,				483,	"void(vector origin, string sample, float volume, float attenuation)"},//DP_SV_POINTSOUND
	{"strreplace",					PF_strreplace,					PF_strreplace,					484,	"string(string search, string replace, string subject)"},//DP_QC_STRREPLACE
	{"strireplace",					PF_strireplace,					PF_strireplace,					485,	"string(string search, string replace, string subject)"},//DP_QC_STRREPLACE
	{"getsurfacepointattribute",	PF_getsurfacepointattribute,	PF_getsurfacepointattribute,	486,	"vector(entity e, float s, float n, float a)"},//DP_QC_GETSURFACEPOINTATTRIBUTE
	{"crc16",						PF_crc16,						PF_crc16,						494,	"float(float caseinsensitive, string s, ...)"},//DP_QC_CRC16
	{"cvar_type",					PF_cvar_type,					PF_cvar_type,					495,	"float(string name)"},//DP_QC_CVAR_TYPE
	{"numentityfields",				PF_numentityfields,				PF_numentityfields,				496,	D("float()", "Gives the number of named entity fields. Note that this is not the size of an entity, but rather just the number of unique names (ie: vectors use 4 names rather than 3).")},//DP_QC_ENTITYDATA
	{"findentityfield",				PF_findentityfield,				PF_findentityfield,				0,		D("float(string fieldname)", "Find a field index by name.")},
	{"entityfieldref",				PF_entityfieldref,				PF_entityfieldref,				0,		D("typedef .__variant field_t;\nfield_t(float fieldnum)", "Returns a field value that can be directly used to read entity fields. Be sure to validate the type with entityfieldtype before using.")},//DP_QC_ENTITYDATA
	{"entityfieldname",				PF_entityfieldname,				PF_entityfieldname,				497,	D("string(float fieldnum)", "Retrieves the name of the given entity field.")},//DP_QC_ENTITYDATA
	{"entityfieldtype",				PF_entityfieldtype,				PF_entityfieldtype,				498,	D("float(float fieldnum)", "Provides information about the type of the field specified by the field num. Returns one of the EV_ values.")},//DP_QC_ENTITYDATA
	{"getentityfieldstring",		PF_getentfldstr,				PF_getentfldstr,				499,	"string(float fieldnum, entity ent)"},//DP_QC_ENTITYDATA
	{"putentityfieldstring",		PF_putentfldstr,				PF_putentfldstr,				500,	"float(float fieldnum, entity ent, string s)"},//DP_QC_ENTITYDATA
	{"whichpack",					PF_whichpack,					PF_whichpack,					503,	D("string(string filename, optional float makereferenced)", "Returns the pak file name that contains the file specified. progs/player.mdl will generally return something like 'pak0.pak'. If makereferenced is true, clients will automatically be told that the returned package should be pre-downloaded and used, even if allow_download_refpackages is not set.")},//DP_QC_WHICHPACK
	{"uri_escape",					PF_uri_escape,					PF_uri_escape,					510,	"string(string in)"},//DP_QC_URI_ESCAPE
	{"uri_unescape",				PF_uri_unescape,				PF_uri_unescape,				511,	"string(string in)"},//DP_QC_URI_ESCAPE
	{"num_for_edict",				PF_num_for_edict,				PF_num_for_edict,				512,	"float(entity ent)"},//DP_QC_NUM_FOR_EDICT
	{"uri_get",						PF_uri_get,						PF_uri_get,						513,	"float(string uril, float id, optional string postmimetype, optional string postdata)", "stub."},//DP_QC_URI_GET
	{"tokenize_console",			PF_tokenize_console,			PF_tokenize_console,			514,	D("float(string str)", "Tokenize a string exactly as the console's tokenizer would do so. The regular tokenize builtin became bastardized for convienient string parsing, which resulted in a large disparity that can be exploited to bypass checks implemented in a naive SV_ParseClientCommand function, therefore you can use this builtin to make sure it exactly matches.")},
	{"argv_start_index",			PF_argv_start_index,			PF_argv_start_index,			515,	D("float(float idx)", "Returns the character index that the tokenized arg started at.")},
	{"argv_end_index",				PF_argv_end_index,				PF_argv_end_index,				516,	D("float(float idx)", "Returns the character index that the tokenized arg stopped at.")},
	{"buf_cvarlist",				PF_buf_cvarlist,				PF_buf_cvarlist,				517,	D("void(strbuf strbuf, string pattern, string antipattern)", "Populates the strbuf with a list of known cvar names.")},
	{"cvar_description",			PF_cvar_description,			PF_cvar_description,			518,	D("string(string cvarname)", "Retrieves the description of a cvar, which might be useful for tooltips or help files. This may still not be useful.")},
	{"gettime",						PF_gettime,						PF_gettime,						519,	"float(optional float timetype)"},
	{"log",							PF_Logarithm,					PF_Logarithm,					532,	D("float(float v, optional float base)", "Determines the logarithm of the input value according to the specified base. This can be used to calculate how much something was shifted by.")},
	{"soundlength",					PF_NoSSQC,						PF_cl_soundlength,				534,	D("float(string sample)", "Provides a way to query the duration of a sound sample, allowing you to set up a timer to chain samples.")},
	{"callfunction",				PF_callfunction,				PF_callfunction,				605,	D("void(.../*, string funcname*/)", "Invokes the named function. The function name is always passed as the last parameter and must always be present. The others are passed to the named function as-is")},
	{"isfunction",					PF_isfunction,					PF_isfunction,					607,	D("float(string s)", "Returns true if the named function exists and can be called with the callfunction builtin.")},
	{"parseentitydata",				PF_parseentitydata,				PF_parseentitydata,				613,	D("float(entity e, string s, optional float offset)", "Reads a single entity's fields into an already-spawned entity. s should contain field pairs like in a saved game: {\"foo1\" \"bar\" \"foo2\" \"5\"}. Returns <=0 on failure, otherwise returns the offset in the string that was read to.")},
	{"sprintf",						PF_sprintf,						PF_sprintf,						627,	"string(string fmt, ...)"},
	{"getsurfacenumtriangles",		PF_getsurfacenumtriangles,		PF_getsurfacenumtriangles,		628,	"float(entity e, float s)"},
	{"getsurfacetriangle",			PF_getsurfacetriangle,			PF_getsurfacetriangle,			629,	"vector(entity e, float s, float n)"},
	{"digest_hex",					PF_digest_hex,					PF_digest_hex,					639,	"string(string digest, string data, ...)"},
	// Quake 2021 rerelease update 3
	{"ex_centerprint",				PF_centerprint,					PF_NoCSQC,						0,		"void(entity client, string s, ...)"},
	{"ex_bprint",					PF_bprint,						PF_NoCSQC,						0,		"void(string s, ...)"},
	{"ex_sprint",					PF_sprint,						PF_NoCSQC,						0,		"void(entity client, string s, ...)"},
	{"ex_finaleFinished",			PF_sv_finalefinished,			PF_NoCSQC,						0,		"float()"},
	{"ex_CheckPlayerEXFlags",		PF_sv_CheckPlayerEXFlags,		PF_NoCSQC,						0,		"float(entity playerEnt)"},
	{"ex_walkpathtogoal",			PF_sv_walkpathtogoal,			PF_NoCSQC,						0,		"float(float movedist, vector goal)"},
	{"ex_localsound",				PF_sv_localsound,				PF_NoCSQC,						0,		"void(entity client, string sample)"},
	{"ex_draw_point",				PF_Fixme,						PF_NoCSQC,						0,		"void(vector point, float colormap, float lifetime, float depthtest)"},
	{"ex_draw_line",				PF_Fixme,						PF_NoCSQC,						0,		"void(vector start, vector end, float colormap, float lifetime, float depthtest)"},
	{"ex_draw_arrow",				PF_Fixme,						PF_NoCSQC,						0,		"void(vector start, vector end, float colormap, float size, float lifetime, float depthtest)"},
	{"ex_draw_ray",					PF_Fixme,						PF_NoCSQC,						0,		"void(vector start, vector direction, float length, float colormap, float size, float lifetime, float depthtest)"},
	{"ex_draw_circle",				PF_Fixme,						PF_NoCSQC,						0,		"void(vector origin, float radius, float colormap, float lifetime, float depthtest)"},
	{"ex_draw_bounds",				PF_Fixme,						PF_NoCSQC,						0,		"void(vector min, vector max, float colormap, float lifetime, float depthtest)"},
	{"ex_draw_worldtext",			PF_Fixme,						PF_NoCSQC,						0,		"void(string s, vector origin, float size, float lifetime, float depthtest)"},
	{"ex_draw_sphere",				PF_Fixme,						PF_NoCSQC,						0,		"void(vector origin, float radius, float colormap, float lifetime, float depthtest)"},
	{"ex_draw_cylinder",			PF_Fixme,						PF_NoCSQC,						0,		"void(vector origin, float halfHeight, float radius, float colormap, float lifetime, float depthtest)"},
	{"ex_bot_movetopoint",			PF_Fixme,						PF_NoCSQC,						0,		"float(entity bot, vector point)"},
	{"ex_bot_followentity",			PF_Fixme,						PF_NoCSQC,						0,		"float(entity bot, entity goal)"},
};
// clang-format on

qboolean PR_Can_Particles (unsigned int prot, unsigned int pext1, unsigned int pext2)
{
	if (r_fteparticles.value == 0)
		return false;
	if (pext2 || (pext1 & PEXT1_CSQC))
		return true; // a bit different, but works
	else
		return false; // sorry. don't report it as supported.
}
qboolean PR_Can_Ent_Alpha (unsigned int prot, unsigned int pext1, unsigned int pext2)
{
	if (prot != PROTOCOL_NETQUAKE)
		return true; // most base protocols support it
	else if (pext2 & PEXT2_REPLACEMENTDELTAS)
		return true; // as does fte's extensions
	else
		return false; // sorry. don't report it as supported.
}
qboolean PR_Can_Ent_ColorMod (unsigned int prot, unsigned int pext1, unsigned int pext2)
{
	if (pext2 & PEXT2_REPLACEMENTDELTAS)
		return true; // as does fte's extensions
	else
		return false; // sorry. don't report it as supported.
}
qboolean PR_Can_Ent_Scale (unsigned int prot, unsigned int pext1, unsigned int pext2)
{
	if (prot == PROTOCOL_RMQ)
		return true; // some base protocols support it
	else if (pext2 & PEXT2_REPLACEMENTDELTAS)
		return true; // as does fte's extensions
	else
		return false; // sorry. don't report it as supported.
}
static struct
{
	const char *name;
	qboolean (*checkextsupported) (unsigned int prot, unsigned int pext1, unsigned int pext2);
} qcextensions[] = {
	{"DP_CON_SET"},
	{"DP_CON_SETA"},
	//	{"DP_EF_NOSHADOW"},
	{"DP_ENT_ALPHA", PR_Can_Ent_Alpha}, // already in quakespasm, supposedly.
	{"DP_ENT_COLORMOD", PR_Can_Ent_ColorMod},
	{"DP_ENT_SCALE", PR_Can_Ent_Scale},
	{"DP_ENT_TRAILEFFECTNUM", PR_Can_Particles},
	//	{"DP_INPUTBUTTONS"},
	{"DP_QC_ASINACOSATANATAN2TAN"},
	{"DP_QC_COPYENTITY"},
	{"DP_QC_CRC16"},
	{"DP_QC_CVAR_DEFSTRING"},
	{"DP_QC_CVAR_STRING"},
	{"DP_QC_CVAR_TYPE"},
	{"DP_QC_EDICT_NUM"},
	{"DP_QC_ENTITYDATA"},
	{"DP_QC_ETOS"},
	{"DP_QC_FINDCHAIN"},
	{"DP_QC_FINDCHAINFLAGS"},
	{"DP_QC_FINDCHAINFLOAT"},
	{"DP_QC_FINDFLAGS"},
	{"DP_QC_FINDFLOAT"},
	{"DP_QC_GETLIGHT"},
	{"DP_QC_GETSURFACE"},
	{"DP_QC_GETSURFACETRIANGLE"},
	{"DP_QC_GETSURFACEPOINTATTRIBUTE"},
	{"DP_QC_MINMAXBOUND"},
	{"DP_QC_MULTIPLETEMPSTRINGS"},
	{"DP_QC_RANDOMVEC"},
	{"DP_QC_SINCOSSQRTPOW"},
	{"DP_QC_SPRINTF"},
	{"DP_QC_STRFTIME"},
	{"DP_QC_STRING_CASE_FUNCTIONS"},
	{"DP_QC_STRINGBUFFERS"},
	{"DP_QC_STRINGCOLORFUNCTIONS"},
	{"DP_QC_STRREPLACE"},
	{"DP_QC_TOKENIZEBYSEPARATOR"},
	{"DP_QC_TRACEBOX"},
	{"DP_QC_TRACETOSS"},
	{"DP_QC_TRACE_MOVETYPES"},
	{"DP_QC_URI_ESCAPE"},
	{"DP_QC_VECTOANGLES_WITH_ROLL"},
	{"DP_QC_VECTORVECTORS"},
	{"DP_QC_WHICHPACK"},
	{"DP_VIEWZOOM"},
	{"DP_REGISTERCVAR"},
	{"DP_SV_BOTCLIENT"},
	{"DP_SV_DROPCLIENT"},
	{"DP_SV_POINTSOUND"},
	{"DP_SV_PRINT"},
	{"DP_SV_SPAWNFUNC_PREFIX"},
	{"DP_SV_WRITEUNTERMINATEDSTRING"},
#ifdef PSET_SCRIPT
	{"DP_TE_PARTICLERAIN", PR_Can_Particles},
	{"DP_TE_PARTICLESNOW", PR_Can_Particles},
#endif
	{"DP_TE_STANDARDEFFECTBUILTINS"},
	{"EXT_BITSHIFT"},
//	{"FTE_ENT_SKIN_CONTENTS"}, // SOLID_BSP&&skin==CONTENTS_FOO changes CONTENTS_SOLID to CONTENTS_FOO, allowing you to swim in moving ents without qc hacks,
//							   // as well as correcting view cshifts etc.
#ifdef PSET_SCRIPT
	{"FTE_PART_SCRIPT"},
	{"FTE_PART_NAMESPACES"},
#ifdef PSET_SCRIPT_EFFECTINFO
	{"FTE_PART_NAMESPACE_EFFECTINFO"},
#endif
#endif
	{"FTE_QC_CHECKCOMMAND"},
	{"FTE_QC_CROSSPRODUCT"},
	{"FTE_QC_INFOKEY"},
	{"FTE_QC_INTCONV"},
	{"FTE_QC_MULTICAST"},
	{"FTE_STRINGS"},
#ifdef PSET_SCRIPT
	{"FTE_SV_POINTPARTICLES", PR_Can_Particles},
#endif
	{"KRIMZON_SV_PARSECLIENTCOMMAND"},
	{"ZQ_QC_STRINGS"},
};

static void PF_checkextension (void)
{
	const char	*extname = G_STRING (OFS_PARM0);
	unsigned int i;
	cvar_t		*v;
	char		*cvn;
	for (i = 0; i < countof (qcextensions); i++)
	{
		if (!strcmp (extname, qcextensions[i].name))
		{
			if (qcextensions[i].checkextsupported)
			{
unsigned int		prot, pext1, pext2;
extern int			sv_protocol;
extern unsigned int sv_protocol_pext1;
extern unsigned int sv_protocol_pext2;
extern cvar_t		cl_nopext;
if (sv.active || qcvm == &sv.qcvm)
{ // server or client+server
	prot = sv_protocol;
	pext1 = sv_protocol_pext1;
	pext2 = sv_protocol_pext2;

	// if the server seems to be set up for singleplayer then filter by client settings. otherwise just assume the best.
	if (!isDedicated && svs.maxclients == 1 && cl_nopext.value)
		pext1 = pext2 = 0;
}
else if (cls.state == ca_connected)
{ // client only (or demo)
	prot = cl.protocol;
	pext1 = cl.protocol_pext1;
	pext2 = cl.protocol_pext2;
}
else
{ // menuqc? ooer
	prot = 0;
	pext1 = 0;
	pext2 = 0;
}
if (!qcextensions[i].checkextsupported (prot, pext1, pext2))
{
	if (!pr_checkextension.value)
		Con_Printf ("Mod queried extension %s, but not enabled\n", extname);
	G_FLOAT (OFS_RETURN) = false;
	return;
}
			}

			cvn = va ("pr_ext_%s", qcextensions[i].name);
			for (i = 0; cvn[i]; i++)
if (cvn[i] >= 'A' && cvn[i] <= 'Z')
	cvn[i] = 'a' + (cvn[i] - 'A');
			v = Cvar_Create (cvn, "1");
			if (v && !v->value)
			{
if (!pr_checkextension.value)
	Con_Printf ("Mod queried extension %s, but blocked by cvar\n", extname);
G_FLOAT (OFS_RETURN) = false;
return;
			}
			if (!pr_checkextension.value)
Con_Printf ("Mod found extension %s\n", extname);
			G_FLOAT (OFS_RETURN) = true;
			return;
		}
	}
	if (!pr_checkextension.value)
		Con_DPrintf ("Mod tried extension %s\n", extname);
	G_FLOAT (OFS_RETURN) = false;
}

static void PF_builtinsupported (void)
{
	const char	*biname = G_STRING (OFS_PARM0);
	unsigned int i;
	for (i = 0; i < sizeof (extensionbuiltins) / sizeof (extensionbuiltins[0]); i++)
	{
		if (!strcmp (extensionbuiltins[i].name, biname))
		{
			G_FLOAT (OFS_RETURN) = extensionbuiltins[i].number;
		}
	}
	G_FLOAT (OFS_RETURN) = 0;
}

static void PF_checkbuiltin (void)
{
	func_t funcref = G_INT (OFS_PARM0);
	if ((unsigned int)funcref < (unsigned int)qcvm->progs->numfunctions)
	{
		dfunction_t *fnc = &qcvm->functions[(unsigned int)funcref];
		//		const char *funcname = PR_GetString(fnc->s_name);
		int			 binum = -fnc->first_statement;
		unsigned int i;

		// qc defines the function at least. nothing weird there...
		if (binum > 0 && binum < qcvm->numbuiltins)
		{
			if (qcvm->builtins[binum] == PF_Fixme)
			{
G_FLOAT (OFS_RETURN) = false; // the builtin with that number isn't defined.
for (i = 0; i < sizeof (extensionbuiltins) / sizeof (extensionbuiltins[0]); i++)
{
	if (extensionbuiltins[i].number == binum)
	{ // but it will be defined if its actually executed.
		if (extensionbuiltins[i].desc && !strncmp (extensionbuiltins[i].desc, "stub.", 5))
			G_FLOAT (OFS_RETURN) = false; // pretend it won't work if it probably won't be useful
		else if ((qcvm == &cl.qcvm && !extensionbuiltins[i].csqcfunc) || (qcvm == &sv.qcvm && !extensionbuiltins[i].ssqcfunc))
			G_FLOAT (OFS_RETURN) = false; // works, but not in this module
		else
			G_FLOAT (OFS_RETURN) = true;
		break;
	}
}
			}
			else
			{
G_FLOAT (OFS_RETURN) = true; // its defined, within the sane range, mapped, everything. all looks good.
							 // we should probably go through the available builtins and validate that the qc's name matches what would be expected
							 // this is really intended more for builtins defined as #0 though, in such cases, mismatched assumptions are impossible.
			}
		}
		else
			G_FLOAT (OFS_RETURN) = false; // not a valid builtin (#0 builtins get remapped at load, even if the builtin is activated then)
	}
	else
	{ // not valid somehow.
		G_FLOAT (OFS_RETURN) = false;
	}
}

void PF_Fixme (void)
{
	// interrogate the vm to try to figure out exactly which builtin they just tried to execute.
	dstatement_t *st = &qcvm->statements[qcvm->xstatement];
	eval_t		 *glob = (eval_t *)&qcvm->globals[st->a];
	if ((unsigned int)glob->function < (unsigned int)qcvm->progs->numfunctions)
	{
		dfunction_t *fnc = &qcvm->functions[(unsigned int)glob->function];
		const char	*funcname = PR_GetString (fnc->s_name);
		int			 binum = -fnc->first_statement;
		unsigned int i;
		if (binum >= 0)
		{
			// find an extension with the matching number
			for (i = 0; i < sizeof (extensionbuiltins) / sizeof (extensionbuiltins[0]); i++)
			{
int num = extensionbuiltins[i].number;
if (num == binum)
{ // set it up so we're faster next time
	builtin_t bi = NULL;
	if (qcvm == &sv.qcvm)
		bi = extensionbuiltins[i].ssqcfunc;
	else if (qcvm == &cl.qcvm)
		bi = extensionbuiltins[i].csqcfunc;
	if (!bi)
		continue;

	num = extensionbuiltins[i].documentednumber;
	if (!pr_checkextension.value || (extensionbuiltins[i].desc && !strncmp (extensionbuiltins[i].desc, "stub.", 5)))
		Con_Warning ("Mod is using builtin #%u - %s\n", num, extensionbuiltins[i].name);
	else
		Con_DPrintf2 ("Mod uses builtin #%u - %s\n", num, extensionbuiltins[i].name);
	qcvm->builtins[binum] = bi;
	qcvm->builtins[binum]();
	return;
}
			}

			PR_RunError ("unimplemented builtin #%i - %s", binum, funcname);
		}
	}
	PR_RunError ("PF_Fixme: not a builtin...");
}

// called at map end
void PR_ShutdownExtensions (void)
{
	PR_UnzoneAll ();
	PF_buf_shutdown ();
	tokenize_flush ();
	pr_ext_warned_particleeffectnum = 0;
}

func_t PR_FindExtFunction (const char *entryname)
{ // depends on 0 being an invalid function,
	dfunction_t *func = ED_FindFunction (entryname);
	if (func)
		return func - qcvm->functions;
	return 0;
}
static void *PR_FindExtGlobal (int type, const char *name)
{
	ddef_t *def = ED_FindGlobal (name);
	if (def && (def->type & ~DEF_SAVEGLOBAL) == type && def->ofs < qcvm->progs->numglobals)
		return qcvm->globals + def->ofs;
	return NULL;
}

void PR_AutoCvarChanged (cvar_t *var)
{
	char   *n;
	ddef_t *glob;
	qcvm_t *oldqcvm = qcvm;
	PR_SwitchQCVM (NULL);
	if (sv.active)
	{
		PR_SwitchQCVM (&sv.qcvm);
		n = va ("autocvar_%s", var->name);
		glob = ED_FindGlobal (n);
		if (glob)
		{
			if (!ED_ParseEpair ((void *)qcvm->globals, glob, var->string, true))
Con_Warning ("EXT: Unable to configure %s\n", n);
		}
		PR_SwitchQCVM (NULL);
	}
	if (cl.qcvm.globals)
	{
		PR_SwitchQCVM (&cl.qcvm);
		n = va ("autocvar_%s", var->name);
		glob = ED_FindGlobal (n);
		if (glob)
		{
			if (!ED_ParseEpair ((void *)qcvm->globals, glob, var->string, true))
Con_Warning ("EXT: Unable to configure %s\n", n);
		}
		PR_SwitchQCVM (NULL);
	}
	PR_SwitchQCVM (oldqcvm);
}

void PR_InitExtensions (void)
{
	size_t i, g, m;
	// this only needs to be done once. because we're evil.
	// it should help slightly with the 'documentation' above at least.
	g = m = sizeof (qcvm->builtins) / sizeof (qcvm->builtins[0]);
	for (i = 0; i < sizeof (extensionbuiltins) / sizeof (extensionbuiltins[0]); i++)
	{
		if (extensionbuiltins[i].documentednumber)
			extensionbuiltins[i].number = extensionbuiltins[i].documentednumber;
		else
			extensionbuiltins[i].number = --g;
	}
}

// called at map start
void PR_EnableExtensions (ddef_t *pr_globaldefs)
{
	unsigned int i, j;
	unsigned int numautocvars = 0;

	for (i = qcvm->numbuiltins; i < countof (qcvm->builtins); i++)
		qcvm->builtins[i] = PF_Fixme;
	qcvm->numbuiltins = i;
	if (!pr_checkextension.value && qcvm == &sv.qcvm)
	{
		Con_DPrintf ("not enabling qc extensions\n");
		return;
	}

#define QCEXTFUNC(n, t) qcvm->extfuncs.n = PR_FindExtFunction (#n);
	QCEXTFUNCS_COMMON

	// replace standard builtins with new replacement extended ones and selectively populate references to module-specific entrypoints.
	if (qcvm == &cl.qcvm)
	{ // csqc
		for (i = 0; i < sizeof (extensionbuiltins) / sizeof (extensionbuiltins[0]); i++)
		{
			int num = (extensionbuiltins[i].documentednumber);
			if (num && extensionbuiltins[i].csqcfunc && qcvm->builtins[num] != PF_Fixme)
qcvm->builtins[num] = extensionbuiltins[i].csqcfunc;
		}

		QCEXTFUNCS_GAME
		QCEXTFUNCS_CS
	}
	else if (qcvm == &sv.qcvm)
	{ // ssqc
		for (i = 0; i < sizeof (extensionbuiltins) / sizeof (extensionbuiltins[0]); i++)
		{
			int num = (extensionbuiltins[i].documentednumber);
			if (num && extensionbuiltins[i].ssqcfunc && qcvm->builtins[num] != PF_Fixme)
qcvm->builtins[num] = extensionbuiltins[i].ssqcfunc;
		}

		QCEXTFUNCS_GAME
		QCEXTFUNCS_SV
	}
#undef QCEXTFUNC

#define QCEXTGLOBAL_FLOAT(n)  qcvm->extglobals.n = PR_FindExtGlobal (ev_float, #n);
#define QCEXTGLOBAL_INT(n)	  qcvm->extglobals.n = PR_FindExtGlobal (ev_ext_integer, #n);
#define QCEXTGLOBAL_VECTOR(n) qcvm->extglobals.n = PR_FindExtGlobal (ev_vector, #n);
	QCEXTGLOBALS_COMMON
	QCEXTGLOBALS_GAME
	QCEXTGLOBALS_CSQC
#undef QCEXTGLOBAL_FLOAT
#undef QCEXTGLOBAL_INT
#undef QCEXTGLOBAL_VECTOR

	// any #0 functions are remapped to their builtins here, so we don't have to tweak the VM in an obscure potentially-breaking way.
	for (i = 0; i < (unsigned int)qcvm->progs->numfunctions; i++)
	{
		if (qcvm->functions[i].first_statement == 0 && qcvm->functions[i].s_name && !qcvm->functions[i].parm_start && !qcvm->functions[i].locals)
		{
			const char *name = PR_GetString (qcvm->functions[i].s_name);
			for (j = 0; j < sizeof (extensionbuiltins) / sizeof (extensionbuiltins[0]); j++)
			{
if (!strcmp (extensionbuiltins[j].name, name))
{ // okay, map it
	qcvm->functions[i].first_statement = -extensionbuiltins[j].number;
	break;
}
			}
		}
	}

	// autocvars
	for (i = 0; i < (unsigned int)qcvm->progs->numglobaldefs; i++)
	{
		const char *n = PR_GetString (qcvm->globaldefs[i].s_name);
		if (!strncmp (n, "autocvar_", 9))
		{
			// really crappy approach
			cvar_t *var = Cvar_Create (n + 9, PR_UglyValueString (qcvm->globaldefs[i].type, (eval_t *)(qcvm->globals + qcvm->globaldefs[i].ofs)));
			numautocvars++;
			if (!var)
continue; // name conflicts with a command?

			if (!ED_ParseEpair ((void *)qcvm->globals, &pr_globaldefs[i], var->string, true))
Con_Warning ("EXT: Unable to configure %s\n", n);
			var->flags |= CVAR_AUTOCVAR;
		}
	}
	if (numautocvars)
		Con_DPrintf2 ("Found %i autocvars\n", numautocvars);

	nearsurface_cache_valid = false;
}

void PR_DumpPlatform_f (void)
{
	char		 name[MAX_OSPATH];
	FILE		*f;
	const char	*outname = NULL;
	unsigned int i, j;
	enum
	{
		SS = 1,
		CS = 2,
		MN = 4,
	};
	unsigned int targs = 0;
	for (i = 1; i < (unsigned)Cmd_Argc ();)
	{
		const char *arg = Cmd_Argv (i++);
		if (!strncmp (arg, "-O", 2))
		{
			if (arg[2])
outname = arg + 2;
			else
outname = Cmd_Argv (i++);
		}
		else if (!q_strcasecmp (arg, "-Tcs"))
			targs |= CS;
		else if (!q_strcasecmp (arg, "-Tss"))
			targs |= SS;
		else if (!q_strcasecmp (arg, "-Tmenu"))
			targs |= MN;
		else
		{
			Con_Printf ("%s: Unknown argument\n", Cmd_Argv (0));
			return;
		}
	}
	if (!outname)
		outname = ((targs == 2) ? "qscsextensions" : "qsextensions");
	if (!targs)
		targs = SS | CS;

	if (strstr (outname, ".."))
		return;
	q_snprintf (name, sizeof (name), "%s/src/%s", com_gamedir, outname);
	COM_AddExtension (name, ".qc", sizeof (name));

	f = fopen (name, "w");
	if (!f)
	{
		Con_Printf ("%s: Couldn't write %s\n", Cmd_Argv (0), name);
		return;
	}
	Con_Printf ("%s: Writing %s\n", Cmd_Argv (0), name);

	fprintf (
		f,
		"/*\n"
		"Extensions file for " ENGINE_NAME_AND_VER "\n"
		"This file is auto-generated by %s %s.\n"
		"You will probably need to use FTEQCC to compile this.\n"
		"*/\n",
		Cmd_Argv (0), Cmd_Args () ? Cmd_Args () : "with no args");

	fprintf (
		f, "\n\n//This file only supports csqc, so including this file in some other situation is a user error\n"
		   "#if defined(QUAKEWORLD) || defined(MENU)\n"
		   "#error Mixed up module defs\n"
		   "#endif\n");
	if (targs & CS)
	{
		fprintf (
			f,
			"#if !defined(CSQC) && !defined(SSQC) && !defined(MENU)\n"
			"#define CSQC\n"
			"#ifndef CSQC_SIMPLE\n" // quakespasm's csqc implementation is simplified, and can do huds+menus, but that's about it.
			"#define CSQC_SIMPLE\n"
			"#endif\n"
			"#endif\n");
	}
	else if (targs & SS)
	{
		fprintf (
			f, "#if !defined(CSQC) && !defined(SSQC) && !defined(MENU)\n"
			   "#define SSQC\n"
			   "#endif\n");
	}
	else if (targs & MN)
	{
		fprintf (
			f, "#if !defined(CSQC) && !defined(SSQC) && !defined(MENU)\n"
			   "#define MENU\n"
			   "#endif\n");
	}
	fprintf (
		f, "#ifndef QSSDEP\n"
		   "#define QSSDEP(reason) __deprecated(reason)\n"
		   "#endif\n");

	fprintf (f, "\n\n//List of advertised extensions\n");
	for (i = 0; i < countof (qcextensions); i++)
		fprintf (f, "//%s\n", qcextensions[i].name);

	fprintf (f, "\n\n//Explicitly flag this stuff as probably-not-referenced, meaning fteqcc will shut up about it and silently strip what it can.\n");
	fprintf (f, "#pragma noref 1\n");

	if (targs & (SS | CS)) // qss uses the same system defs for both ssqc and csqc, as a simplification.
	{					   // I really hope that fteqcc's unused variable logic is up to scratch
		fprintf (f, "#if defined(CSQC_SIMPLE) || defined(SSQC)\n");
		fprintf (f, "entity		self,other,world;\n");
		fprintf (f, "float		time,frametime,force_retouch;\n");
		fprintf (f, "string		mapname;\n");
		fprintf (
			f, "float		deathmatch,coop,teamplay,serverflags,total_secrets,total_monsters,found_secrets,killed_monsters,parm1, parm2, parm3, parm4, "
			   "parm5, parm6, parm7, parm8, parm9, parm10, parm11, parm12, parm13, parm14, parm15, parm16;\n");
		fprintf (f, "vector		v_forward, v_up, v_right;\n");
		fprintf (f, "float		trace_allsolid,trace_startsolid,trace_fraction;\n");
		fprintf (f, "vector		trace_endpos,trace_plane_normal;\n");
		fprintf (f, "float		trace_plane_dist;\n");
		fprintf (f, "entity		trace_ent;\n");
		fprintf (f, "float		trace_inopen,trace_inwater;\n");
		fprintf (f, "entity		msg_entity;\n");
		fprintf (
			f, "void() 		"
			   "main,StartFrame,PlayerPreThink,PlayerPostThink,ClientKill,ClientConnect,PutClientInServer,ClientDisconnect,SetNewParms,SetChangeParms;\n");
		fprintf (f, "void		end_sys_globals;\n\n");
		fprintf (f, ".float		modelindex;\n");
		fprintf (f, ".vector		absmin, absmax;\n");
		fprintf (f, ".float		ltime,movetype,solid;\n");
		fprintf (f, ".vector		origin,oldorigin,velocity,angles,avelocity,punchangle;\n");
		fprintf (f, ".string		classname,model;\n");
		fprintf (f, ".float		frame,skin,effects;\n");
		fprintf (f, ".vector		mins, maxs,size;\n");
		fprintf (f, ".void()		touch,use,think,blocked;\n");
		fprintf (f, ".float		nextthink;\n");
		fprintf (f, ".entity		groundentity;\n");
		fprintf (f, ".float		health,frags,weapon;\n");
		fprintf (f, ".string		weaponmodel;\n");
		fprintf (f, ".float		weaponframe,currentammo,ammo_shells,ammo_nails,ammo_rockets,ammo_cells,items,takedamage;\n");
		fprintf (f, ".entity		chain;\n");
		fprintf (f, ".float		deadflag;\n");
		fprintf (f, ".vector		view_ofs;\n");
		fprintf (f, ".float		button0,button1,button2,impulse,fixangle;\n");
		fprintf (f, ".vector		v_angle;\n");
		fprintf (f, ".float		idealpitch;\n");
		fprintf (f, ".string		netname;\n");
		fprintf (f, ".entity 	enemy;\n");
		fprintf (f, ".float		flags,colormap,team,max_health,teleport_time,armortype,armorvalue,waterlevel,watertype,ideal_yaw,yaw_speed;\n");
		fprintf (f, ".entity		aiment,goalentity;\n");
		fprintf (f, ".float		spawnflags;\n");
		fprintf (f, ".string		target,targetname;\n");
		fprintf (f, ".float		dmg_take,dmg_save;\n");
		fprintf (f, ".entity		dmg_inflictor,owner;\n");
		fprintf (f, ".vector		movedir;\n");
		fprintf (f, ".string		message;\n");
		fprintf (f, ".float		sounds;\n");
		fprintf (f, ".string		noise, noise1, noise2, noise3;\n");
		fprintf (f, "void		end_sys_fields;\n");
		fprintf (f, "#endif\n");
	}
	if (targs & MN)
	{
		fprintf (f, "#if defined(MENU)\n");
		fprintf (f, "entity		self;\n");
		fprintf (f, "void		end_sys_globals;\n\n");
		fprintf (f, "void		end_sys_fields;\n");
		fprintf (f, "#endif\n");
	}

	fprintf (f, "\n\n//Some custom types (that might be redefined as accessors by fteextensions.qc, although we don't define any methods here)\n");
	fprintf (f, "#ifdef _ACCESSORS\n");
	fprintf (f, "accessor strbuf:float;\n");
	fprintf (f, "accessor searchhandle:float;\n");
	fprintf (f, "accessor hashtable:float;\n");
	fprintf (f, "accessor infostring:string;\n");
	fprintf (f, "accessor filestream:float;\n");
	fprintf (f, "#else\n");
	fprintf (f, "#define strbuf float\n");
	fprintf (f, "#define searchhandle float\n");
	fprintf (f, "#define hashtable float\n");
	fprintf (f, "#define infostring string\n");
	fprintf (f, "#define filestream float\n");
	fprintf (f, "#endif\n");

	fprintf (f, "\n\n//Common entry points\n");
#define QCEXTFUNC(n, t) fprintf (f, t " " #n "\n");
	QCEXTFUNCS_COMMON
	if (targs & (SS | CS))
	{
		QCEXTFUNCS_GAME
	}

	if (targs & SS)
	{
		fprintf (f, "\n\n//Serverside entry points\n");
		QCEXTFUNCS_SV
	}
	if (targs & CS)
	{
		fprintf (f, "\n\n//CSQC entry points\n");
		QCEXTFUNCS_CS
	}
#undef QCEXTFUNC

#define QCEXTGLOBAL_INT(n)	  fprintf (f, "int " #n ";\n");
#define QCEXTGLOBAL_FLOAT(n)  fprintf (f, "float " #n ";\n");
#define QCEXTGLOBAL_VECTOR(n) fprintf (f, "vector " #n ";\n");
	QCEXTGLOBALS_COMMON
	if (targs & (CS | SS))
	{
		QCEXTGLOBALS_GAME
	}
#undef QCEXTGLOBAL_INT
#undef QCEXTGLOBAL_FLOAT
#undef QCEXTGLOBAL_VECTOR

	fprintf (f, "const float FALSE		= 0;\n");
	fprintf (f, "const float TRUE		= 1;\n");

	if (targs & (CS | SS))
	{
		fprintf (f, "const float STAT_HEALTH = 0;		/* Player's health. */\n");
		//		fprintf(f, "const float STAT_FRAGS = 1;			/* unused */\n");
		fprintf (
			f, "const float STAT_WEAPONMODELI = 2;	/* This is the modelindex of the current viewmodel (renamed from the original name 'STAT_WEAPON' due "
			   "to confusions). */\n");
		fprintf (f, "const float STAT_AMMO = 3;			/* player.currentammo */\n");
		fprintf (f, "const float STAT_ARMOR = 4;\n");
		fprintf (f, "const float STAT_WEAPONFRAME = 5;\n");
		fprintf (f, "const float STAT_SHELLS = 6;\n");
		fprintf (f, "const float STAT_NAILS = 7;\n");
		fprintf (f, "const float STAT_ROCKETS = 8;\n");
		fprintf (f, "const float STAT_CELLS = 9;\n");
		fprintf (f, "const float STAT_ACTIVEWEAPON = 10;	/* player.weapon */\n");
		fprintf (f, "const float STAT_TOTALSECRETS = 11;\n");
		fprintf (f, "const float STAT_TOTALMONSTERS = 12;\n");
		fprintf (f, "const float STAT_FOUNDSECRETS = 13;\n");
		fprintf (f, "const float STAT_KILLEDMONSTERS = 14;\n");
		fprintf (
			f, "const float STAT_ITEMS = 15;		/* self.items | (self.items2<<23). In order to decode this stat properly, you need to use "
			   "getstatbits(STAT_ITEMS,0,23) to read self.items, and getstatbits(STAT_ITEMS,23,11) to read self.items2 or getstatbits(STAT_ITEMS,28,4) to "
			   "read the visible part of serverflags, whichever is applicable. */\n");
		fprintf (f, "const float STAT_VIEWHEIGHT = 16;	/* player.view_ofs_z */\n");
		fprintf (f, "const float STAT_VIEW2 = 20;		/* This stat contains the number of the entity in the server's .view2 field. */\n");
		fprintf (f, "const float STAT_VIEWZOOM = 21;		/* Scales fov and sensitivity. Part of DP_VIEWZOOM. */\n");
		fprintf (f, "const float STAT_IDEALPITCH = 25;\n");
		fprintf (f, "const float STAT_PUNCHANGLE_X = 26;\n");
		fprintf (f, "const float STAT_PUNCHANGLE_Y = 27;\n");
		fprintf (f, "const float STAT_PUNCHANGLE_Z = 28;\n");

		fprintf (f, "const float SOLID_BBOX = %i;\n", SOLID_BBOX);
		fprintf (f, "const float SOLID_BSP = %i;\n", SOLID_BSP);
		fprintf (f, "const float SOLID_NOT = %i;\n", SOLID_NOT);
		fprintf (f, "const float SOLID_SLIDEBOX = %i;\n", SOLID_SLIDEBOX);
		fprintf (f, "const float SOLID_TRIGGER = %i;\n", SOLID_TRIGGER);

		fprintf (f, "const float MOVETYPE_NONE = %i;\n", MOVETYPE_NONE);
		fprintf (f, "const float MOVETYPE_WALK = %i;\n", MOVETYPE_WALK);
		fprintf (f, "const float MOVETYPE_STEP = %i;\n", MOVETYPE_STEP);
		fprintf (f, "const float MOVETYPE_FLY = %i;\n", MOVETYPE_FLY);
		fprintf (f, "const float MOVETYPE_TOSS = %i;\n", MOVETYPE_TOSS);
		fprintf (f, "const float MOVETYPE_PUSH = %i;\n", MOVETYPE_PUSH);
		fprintf (f, "const float MOVETYPE_NOCLIP = %i;\n", MOVETYPE_NOCLIP);
		fprintf (f, "const float MOVETYPE_FLYMISSILE = %i;\n", MOVETYPE_FLYMISSILE);
		fprintf (f, "const float MOVETYPE_BOUNCE = %i;\n", MOVETYPE_BOUNCE);

		fprintf (f, "const float CONTENT_EMPTY = %i;\n", CONTENTS_EMPTY);
		fprintf (f, "const float CONTENT_SOLID = %i;\n", CONTENTS_SOLID);
		fprintf (f, "const float CONTENT_WATER = %i;\n", CONTENTS_WATER);
		fprintf (f, "const float CONTENT_SLIME = %i;\n", CONTENTS_SLIME);
		fprintf (f, "const float CONTENT_LAVA = %i;\n", CONTENTS_LAVA);
		fprintf (f, "const float CONTENT_SKY = %i;\n", CONTENTS_SKY);

		fprintf (f, "__used var float physics_mode = 2;\n");

		fprintf (f, "const float TE_SPIKE = %i;\n", TE_SPIKE);
		fprintf (f, "const float TE_SUPERSPIKE = %i;\n", TE_SUPERSPIKE);
		fprintf (f, "const float TE_GUNSHOT = %i;\n", TE_GUNSHOT);
		fprintf (f, "const float TE_EXPLOSION = %i;\n", TE_EXPLOSION);
		fprintf (f, "const float TE_TAREXPLOSION = %i;\n", TE_TAREXPLOSION);
		fprintf (f, "const float TE_LIGHTNING1 = %i;\n", TE_LIGHTNING1);
		fprintf (f, "const float TE_LIGHTNING2 = %i;\n", TE_LIGHTNING2);
		fprintf (f, "const float TE_WIZSPIKE = %i;\n", TE_WIZSPIKE);
		fprintf (f, "const float TE_KNIGHTSPIKE = %i;\n", TE_KNIGHTSPIKE);
		fprintf (f, "const float TE_LIGHTNING3 = %i;\n", TE_LIGHTNING3);
		fprintf (f, "const float TE_LAVASPLASH = %i;\n", TE_LAVASPLASH);
		fprintf (f, "const float TE_TELEPORT = %i;\n", TE_TELEPORT);
		fprintf (f, "const float TE_EXPLOSION2 = %i;\n", TE_EXPLOSION2);
		fprintf (f, "const float TE_BEAM = %i;\n", TE_BEAM);

		fprintf (f, "const float MF_ROCKET			= %#x;\n", EF_ROCKET);
		fprintf (f, "const float MF_GRENADE			= %#x;\n", EF_GRENADE);
		fprintf (f, "const float MF_GIB				= %#x;\n", EF_GIB);
		fprintf (f, "const float MF_ROTATE			= %#x;\n", EF_ROTATE);
		fprintf (f, "const float MF_TRACER			= %#x;\n", EF_TRACER);
		fprintf (f, "const float MF_ZOMGIB			= %#x;\n", EF_ZOMGIB);
		fprintf (f, "const float MF_TRACER2			= %#x;\n", EF_TRACER2);
		fprintf (f, "const float MF_TRACER3			= %#x;\n", EF_TRACER3);

		fprintf (f, "const float EF_BRIGHTFIELD = %i;\n", EF_BRIGHTFIELD);
		fprintf (f, "const float EF_MUZZLEFLASH = %i;\n", EF_MUZZLEFLASH);
		fprintf (f, "const float EF_BRIGHTLIGHT = %i;\n", EF_BRIGHTLIGHT);
		fprintf (f, "const float EF_DIMLIGHT = %i;\n", EF_DIMLIGHT);

		fprintf (f, "const float FL_FLY = %i;\n", FL_FLY);
		fprintf (f, "const float FL_SWIM = %i;\n", FL_SWIM);
		fprintf (f, "const float FL_CLIENT = %i;\n", FL_CLIENT);
		fprintf (f, "const float FL_INWATER = %i;\n", FL_INWATER);
		fprintf (f, "const float FL_MONSTER = %i;\n", FL_MONSTER);
		fprintf (f, "const float FL_GODMODE = %i;\n", FL_GODMODE);
		fprintf (f, "const float FL_NOTARGET = %i;\n", FL_NOTARGET);
		fprintf (f, "const float FL_ITEM = %i;\n", FL_ITEM);
		fprintf (f, "const float FL_ONGROUND = %i;\n", FL_ONGROUND);
		fprintf (f, "const float FL_PARTIALGROUND = %i;\n", FL_PARTIALGROUND);
		fprintf (f, "const float FL_WATERJUMP = %i;\n", FL_WATERJUMP);
		fprintf (f, "const float FL_JUMPRELEASED = %i;\n", FL_JUMPRELEASED);

		fprintf (f, "const float ATTN_NONE = %i;\n", 0);
		fprintf (f, "const float ATTN_NORM = %i;\n", 1);
		fprintf (f, "const float ATTN_IDLE = %i;\n", 2);
		fprintf (f, "const float ATTN_STATIC = %i;\n", 3);

		fprintf (f, "const float CHAN_AUTO = %i;\n", 0);
		fprintf (f, "const float CHAN_WEAPON = %i;\n", 1);
		fprintf (f, "const float CHAN_VOICE = %i;\n", 2);
		fprintf (f, "const float CHAN_ITEM = %i;\n", 3);
		fprintf (f, "const float CHAN_BODY = %i;\n", 4);
	}

	fprintf (f, "const float STAT_USER = 32;			/* Custom user stats start here (lower values are reserved for engine use). */\n");
	// these can be used for both custom stats and for reflection
	fprintf (f, "const float EV_VOID = %i;\n", ev_void);
	fprintf (f, "const float EV_STRING = %i;\n", ev_string);
	fprintf (f, "const float EV_FLOAT = %i;\n", ev_float);
	fprintf (f, "const float EV_VECTOR = %i;\n", ev_vector);
	fprintf (f, "const float EV_ENTITY = %i;\n", ev_entity);
	fprintf (f, "const float EV_FIELD = %i;\n", ev_field);
	fprintf (f, "const float EV_FUNCTION = %i;\n", ev_function);
	fprintf (f, "const float EV_POINTER = %i;\n", ev_pointer);
	fprintf (f, "const float EV_INTEGER = %i;\n", ev_ext_integer);

#define QCEXTFIELD(n, t) fprintf (f, "%s %s;\n", t, #n);
	// extra fields
	fprintf (f, "\n\n//Supported Extension fields\n");
	QCEXTFIELDS_ALL
	if (targs & (SS | CS))
	{
		QCEXTFIELDS_GAME
	}
	if (targs & (SS))
	{
		QCEXTFIELDS_SS
	}
#undef QCEXTFIELD

	if (targs & SS)
	{
		fprintf (f, ".float style;\n");		// not used by the engine, but is used by tools etc.
		fprintf (f, ".float light_lev;\n"); // ditto.

		// extra constants
		fprintf (f, "\n\n//Supported Extension Constants\n");

		fprintf (f, "const float CLIENTTYPE_DISCONNECT	= " STRINGIFY (0) ";\n");
		fprintf (f, "const float CLIENTTYPE_REAL			= " STRINGIFY (1) ";\n");
		fprintf (f, "const float CLIENTTYPE_BOT			= " STRINGIFY (2) ";\n");
		fprintf (f, "const float CLIENTTYPE_NOTCLIENT	= " STRINGIFY (3) ";\n");

		fprintf (f, "const float DAMAGE_AIM = %i;\n", DAMAGE_AIM);
		fprintf (f, "const float DAMAGE_NO = %i;\n", DAMAGE_NO);
		fprintf (f, "const float DAMAGE_YES = %i;\n", DAMAGE_YES);

		fprintf (f, "const float MSG_BROADCAST = %i;\n", MSG_BROADCAST);
		fprintf (f, "const float MSG_ONE = %i;\n", MSG_ONE);
		fprintf (f, "const float MSG_ALL = %i;\n", MSG_ALL);
		fprintf (f, "const float MSG_INIT = %i;\n", MSG_INIT);
		fprintf (f, "const float MSG_MULTICAST = %i;\n", MSG_EXT_MULTICAST);
		fprintf (f, "const float MSG_ENTITY = %i;\n", MSG_EXT_ENTITY);

		fprintf (f, "const float MSG_MULTICAST	= %i;\n", 4);
		fprintf (f, "const float MULTICAST_ALL	= %i;\n", MULTICAST_ALL_U);
		fprintf (f, "const float MULTICAST_PVS	= %i;\n", MULTICAST_PVS_U);
		fprintf (f, "const float MULTICAST_ONE	= %i;\n", MULTICAST_ONE_U);
		fprintf (f, "const float MULTICAST_ALL_R	= %i;\n", MULTICAST_ALL_R);
		fprintf (f, "const float MULTICAST_PVS_R	= %i;\n", MULTICAST_PVS_R);
		fprintf (f, "const float MULTICAST_ONE_R	= %i;\n", MULTICAST_ONE_R);
		fprintf (f, "const float MULTICAST_INIT	= %i;\n", MULTICAST_INIT);
	}

	fprintf (f, "const float FILE_READ		= " STRINGIFY (0) ";\n");
	fprintf (f, "const float FILE_APPEND		= " STRINGIFY (1) ";\n");
	fprintf (f, "const float FILE_WRITE		= " STRINGIFY (2) ";\n");

	// this is annoying. builtins from pr_cmds.c are not known here.
	if (targs & (CS | SS))
	{
		const char *conflictprefix = ""; //(targs&CS)?"":"//";
		fprintf (f, "\n\n//Vanilla Builtin list (reduced, so as to avoid conflicts)\n");
		fprintf (f, "void(vector) makevectors = #1;\n");
		fprintf (f, "void(entity,vector) setorigin = #2;\n");
		fprintf (f, "void(entity,string) setmodel = #3;\n");
		fprintf (f, "void(entity,vector,vector) setsize = #4;\n");
		fprintf (f, "float() random = #7;\n");
		fprintf (
			f,
			"%svoid(entity e, float chan, string samp, float vol, float atten, optional float speedpct, optional float flags, optional float timeofs) "
			"sound = #8;\n",
			conflictprefix);
		fprintf (f, "vector(vector) normalize = #9;\n");
		fprintf (f, "void(string e) error = #10;\n");
		fprintf (f, "void(string n) objerror = #11;\n");
		fprintf (f, "float(vector) vlen = #12;\n");
		fprintf (f, "float(vector fwd) vectoyaw = #13;\n");
		fprintf (f, "entity() spawn = #14;\n");
		fprintf (f, "void(entity e) remove = #15;\n");
		fprintf (f, "void(vector v1, vector v2, float flags, entity ent) traceline = #16;\n");
		if (targs & SS)
			fprintf (f, "entity() checkclient = #17;\n");
		fprintf (f, "entity(entity start, .string fld, string match) find = #18;\n");
		fprintf (f, "string(string s) precache_sound = #19;\n");
		fprintf (f, "string(string s) precache_model = #20;\n");
		if (targs & SS)
			fprintf (f, "%svoid(entity client, string s) stuffcmd = #21;\n", conflictprefix);
		fprintf (f, "%sentity(vector org, float rad, optional .entity chainfield) findradius = #22;\n", conflictprefix);
		if (targs & SS)
		{
			fprintf (
				f,
				"%svoid(string s, optional string s2, optional string s3, optional string s4, optional string s5, optional string s6, optional string s7, "
				"optional string s8) bprint = #23;\n",
				conflictprefix);
			fprintf (
				f,
				"%svoid(entity pl, string s, optional string s2, optional string s3, optional string s4, optional string s5, optional string s6, optional "
				"string s7) sprint = #24;\n",
				conflictprefix);
		}
		fprintf (f, "void(string,...) dprint = #25;\n");
		fprintf (f, "string(float) ftos = #26;\n");
		fprintf (f, "string(vector) vtos = #27;\n");
		fprintf (f, "%sfloat(float yaw, float dist, optional float settraceglobals) walkmove = #32;\n", conflictprefix);
		fprintf (f, "float() droptofloor = #34;\n");
		fprintf (f, "%svoid(float lightstyle, string stylestring, optional vector rgb) lightstyle = #35;\n", conflictprefix);
		fprintf (f, "float(float n) rint = #36;\n");
		fprintf (f, "float(float n) floor = #37;\n");
		fprintf (f, "float(float n) ceil = #38;\n");
		fprintf (f, "float(entity e) checkbottom = #40;\n");
		fprintf (f, "float(vector point) pointcontents = #41;\n");
		fprintf (f, "float(float n) fabs = #43;\n");
		if (targs & SS)
			fprintf (f, "vector(entity e, float speed) aim = #44;\n");
		fprintf (f, "float(string) cvar = #45;\n");
		fprintf (f, "void(string,...) localcmd = #46;\n");
		fprintf (f, "entity(entity) nextent = #47;\n");
		fprintf (f, "void(vector o, vector d, float color, float count) particle = #48;\n");
		fprintf (f, "void() changeyaw = #49;\n");
		fprintf (f, "%svector(vector fwd, optional vector up) vectoangles = #51;\n", conflictprefix);
		if (targs & SS)
		{
			fprintf (f, "void(float to, float val) WriteByte = #52;\n");
			fprintf (f, "void(float to, float val) WriteChar = #53;\n");
			fprintf (f, "void(float to, float val) WriteShort = #54;\n");
			fprintf (f, "void(float to, float val) WriteLong = #55;\n");
			fprintf (f, "void(float to, float val) WriteCoord = #56;\n");
			fprintf (f, "void(float to, float val) WriteAngle = #57;\n");
			fprintf (f, "void(float to, string val) WriteString = #58;\n");
			fprintf (f, "void(float to, entity val) WriteEntity = #59;\n");
		}
		fprintf (f, "void(float step) movetogoal = #67;\n");
		fprintf (f, "string(string s) precache_file = #68;\n");
		fprintf (f, "void(entity e) makestatic = #69;\n");
		if (targs & SS)
			fprintf (f, "void(string mapname, optional string newmapstartspot) changelevel = #70;\n");
		fprintf (f, "void(string var, string val) cvar_set = #72;\n");
		if (targs & SS)
			fprintf (
				f, "void(entity ent, string text, optional string text2, optional string text3, optional string text4, optional string text5, optional "
				   "string text6, optional string text7) centerprint = #73;\n");
		fprintf (f, "void (vector pos, string samp, float vol, float atten) ambientsound = #74;\n");
		fprintf (f, "string(string str) precache_model2 = #75;\n");
		fprintf (f, "string(string str) precache_sound2 = #76;\n");
		fprintf (f, "string(string str) precache_file2 = #77;\n");
		if (targs & SS)
			fprintf (f, "void(entity player) setspawnparms = #78;\n");
	}
	if (targs & MN)
	{
		fprintf (f, "void(string e) error = #2;\n");
		fprintf (f, "void(string n) objerror = #3;\n");
		fprintf (f, "float(vector) vlen = #9;\n");
		fprintf (f, "float(vector fwd) vectoyaw = #10;\n");
		fprintf (f, "vector(vector fwd, optional vector up) vectoangles = #11;\n");
		fprintf (f, "float() random = #12;\n");
		fprintf (f, "void(string,...) localcmd = #13;\n");
		fprintf (f, "float(string) cvar = #14;\n");
		fprintf (f, "void(string var, string val) cvar_set = #15;\n");
		fprintf (f, "void(string,...) dprint = #16;\n");
		fprintf (f, "string(float) ftos = #17;\n");
		fprintf (f, "float(float n) fabs = #18;\n");
		fprintf (f, "string(vector) vtos = #19;\n");
		fprintf (f, "entity() spawn = #22;\n");
		fprintf (f, "void(entity e) remove = #23;\n");
		fprintf (f, "entity(entity start, .string fld, string match) find = #24;\n");
		fprintf (f, "string(string s) precache_file = #28;\n");
		fprintf (f, "string(string s) precache_sound = #29;\n");
		fprintf (f, "float(float n) rint = #34;\n");
		fprintf (f, "float(float n) floor = #35;\n");
		fprintf (f, "float(float n) ceil = #36;\n");
		fprintf (f, "entity(entity) nextent = #37;\n");
		fprintf (f, "float() clientstate = #62;\n");
	}

	for (j = 0; j < 2; j++)
	{
		if (j)
			fprintf (f, "\n\n//Builtin Stubs List (these are present for simpler compatibility, but not properly supported in QuakeSpasm at this time).\n/*\n");
		else
			fprintf (f, "\n\n//Builtin list\n");
		for (i = 0; i < sizeof (extensionbuiltins) / sizeof (extensionbuiltins[0]); i++)
		{
			if ((targs & CS) && extensionbuiltins[i].csqcfunc)
;
			else if ((targs & SS) && extensionbuiltins[i].ssqcfunc)
;
			else
continue;

			if (j != (extensionbuiltins[i].desc ? !strncmp (extensionbuiltins[i].desc, "stub.", 5) : 0))
continue;
			fprintf (f, "%s %s = #%i;", extensionbuiltins[i].typestr, extensionbuiltins[i].name, extensionbuiltins[i].documentednumber);
			if (extensionbuiltins[i].desc && !j)
			{
const char *line = extensionbuiltins[i].desc;
const char *term;
fprintf (f, " /*");
while (*line)
{
	fprintf (f, "\n\t\t");
	term = line;
	while (*term && *term != '\n')
		term++;
	fwrite (line, 1, term - line, f);
	if (*term == '\n')
	{
		term++;
	}
	line = term;
}
fprintf (f, " */\n\n");
			}
			else
fprintf (f, "\n");
		}
		if (j)
			fprintf (f, "*/\n");
	}

	fprintf (f, "\n\n//Reset this back to normal.\n");
	fprintf (f, "#pragma noref 0\n");
	fclose (f);
}
