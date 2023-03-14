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
// sbar.c -- status bar code

#include "quakedef.h"

#define STAT_MINUS 10 // num frame for '-' stats digit

qpic_t *sb_nums[2][11];
qpic_t *sb_colon, *sb_slash;
qpic_t *sb_ibar;
qpic_t *sb_sbar;
qpic_t *sb_scorebar;

qpic_t *sb_weapons[7][8]; // 0 is active, 1 is owned, 2-5 are flashes
qpic_t *sb_ammo[4];
qpic_t *sb_sigil[4];
qpic_t *sb_armor[3];
qpic_t *sb_items[32];

qpic_t *sb_faces[7][2]; // 0 is gibbed, 1 is dead, 2-6 are alive
						// 0 is static, 1 is temporary animation
qpic_t *sb_face_invis;
qpic_t *sb_face_quad;
qpic_t *sb_face_invuln;
qpic_t *sb_face_invis_invuln;

qboolean sb_showscores;

int sb_lines; // scan lines to draw

qpic_t *rsb_invbar[2];
qpic_t *rsb_weapons[5];
qpic_t *rsb_items[2];
qpic_t *rsb_ammo[3];
qpic_t *rsb_teambord; // PGM 01/19/97 - team color border

// MED 01/04/97 added two more weapons + 3 alternates for grenade launcher
qpic_t *hsb_weapons[7][5]; // 0 is active, 1 is owned, 2-5 are flashes
// MED 01/04/97 added array to simplify weapon parsing
int		hipweapons[4] = {HIT_LASER_CANNON_BIT, HIT_MJOLNIR_BIT, 4, HIT_PROXIMITY_GUN_BIT};
// MED 01/04/97 added hipnotic items array
qpic_t *hsb_items[2];

// spike -- fix -game hipnotic by autodetecting hud types. the fte protocols will deal with the networking issue, other than demos, anyway
static int hudtype;
#define hipnotic (hudtype == 1)
#define rogue	 (hudtype == 2)

extern cvar_t scr_style;

void Sbar_MiniDeathmatchOverlay (cb_context_t *cbx);
void Sbar_DeathmatchOverlay (cb_context_t *cbx);
void M_DrawPic (cb_context_t *cbx, int x, int y, qpic_t *pic);

qboolean Sbar_CSQCCommand (void)
{
	qboolean ret = false;
	if ((scr_style.value < 1.0f) && cl.qcvm.extfuncs.CSQC_ConsoleCommand)
	{
		PR_SwitchQCVM (&cl.qcvm);
		G_INT (OFS_PARM0) = PR_MakeTempString (Cmd_Argv (0));
		PR_ExecuteProgram (cl.qcvm.extfuncs.CSQC_ConsoleCommand);
		ret = G_FLOAT (OFS_RETURN);
		PR_SwitchQCVM (NULL);
	}
	return ret;
}

/*
===============
Sbar_ShowScores

Tab key down
===============
*/
void Sbar_ShowScores (void)
{
	Sbar_CSQCCommand ();
	if (sb_showscores)
		return;
	sb_showscores = true;
}

/*
===============
Sbar_DontShowScores

Tab key up
===============
*/
void Sbar_DontShowScores (void)
{
	Sbar_CSQCCommand ();
	sb_showscores = false;
}

qpic_t *Sbar_CheckPicFromWad (const char *name)
{
	extern qpic_t *pic_nul;
	qpic_t		  *r;
	lumpinfo_t	  *info;
	if (!hudtype)
		return pic_nul; // one already failed, don't waste cpu
	if (!W_GetLumpName (name, &info))
		r = pic_nul;
	else
		r = Draw_PicFromWad (name);
	if (r == pic_nul)
		hudtype = 0;
	return r;
}

/*
===============
Sbar_LoadPics -- johnfitz -- load all the sbar pics
===============
*/
void Sbar_LoadPics (void)
{
	int i;

	for (i = 0; i < 10; i++)
	{
		sb_nums[0][i] = Draw_PicFromWad (va ("num_%i", i));
		sb_nums[1][i] = Draw_PicFromWad (va ("anum_%i", i));
	}

	sb_nums[0][10] = Draw_PicFromWad ("num_minus");
	sb_nums[1][10] = Draw_PicFromWad ("anum_minus");

	sb_colon = Draw_PicFromWad ("num_colon");
	sb_slash = Draw_PicFromWad ("num_slash");

	sb_weapons[0][0] = Draw_PicFromWad ("inv_shotgun");
	sb_weapons[0][1] = Draw_PicFromWad ("inv_sshotgun");
	sb_weapons[0][2] = Draw_PicFromWad ("inv_nailgun");
	sb_weapons[0][3] = Draw_PicFromWad ("inv_snailgun");
	sb_weapons[0][4] = Draw_PicFromWad ("inv_rlaunch");
	sb_weapons[0][5] = Draw_PicFromWad ("inv_srlaunch");
	sb_weapons[0][6] = Draw_PicFromWad ("inv_lightng");

	sb_weapons[1][0] = Draw_PicFromWad ("inv2_shotgun");
	sb_weapons[1][1] = Draw_PicFromWad ("inv2_sshotgun");
	sb_weapons[1][2] = Draw_PicFromWad ("inv2_nailgun");
	sb_weapons[1][3] = Draw_PicFromWad ("inv2_snailgun");
	sb_weapons[1][4] = Draw_PicFromWad ("inv2_rlaunch");
	sb_weapons[1][5] = Draw_PicFromWad ("inv2_srlaunch");
	sb_weapons[1][6] = Draw_PicFromWad ("inv2_lightng");

	for (i = 0; i < 5; i++)
	{
		sb_weapons[2 + i][0] = Draw_PicFromWad (va ("inva%i_shotgun", i + 1));
		sb_weapons[2 + i][1] = Draw_PicFromWad (va ("inva%i_sshotgun", i + 1));
		sb_weapons[2 + i][2] = Draw_PicFromWad (va ("inva%i_nailgun", i + 1));
		sb_weapons[2 + i][3] = Draw_PicFromWad (va ("inva%i_snailgun", i + 1));
		sb_weapons[2 + i][4] = Draw_PicFromWad (va ("inva%i_rlaunch", i + 1));
		sb_weapons[2 + i][5] = Draw_PicFromWad (va ("inva%i_srlaunch", i + 1));
		sb_weapons[2 + i][6] = Draw_PicFromWad (va ("inva%i_lightng", i + 1));
	}

	sb_ammo[0] = Draw_PicFromWad ("sb_shells");
	sb_ammo[1] = Draw_PicFromWad ("sb_nails");
	sb_ammo[2] = Draw_PicFromWad ("sb_rocket");
	sb_ammo[3] = Draw_PicFromWad ("sb_cells");

	sb_armor[0] = Draw_PicFromWad ("sb_armor1");
	sb_armor[1] = Draw_PicFromWad ("sb_armor2");
	sb_armor[2] = Draw_PicFromWad ("sb_armor3");

	sb_items[0] = Draw_PicFromWad ("sb_key1");
	sb_items[1] = Draw_PicFromWad ("sb_key2");
	sb_items[2] = Draw_PicFromWad ("sb_invis");
	sb_items[3] = Draw_PicFromWad ("sb_invuln");
	sb_items[4] = Draw_PicFromWad ("sb_suit");
	sb_items[5] = Draw_PicFromWad ("sb_quad");

	sb_sigil[0] = Draw_PicFromWad ("sb_sigil1");
	sb_sigil[1] = Draw_PicFromWad ("sb_sigil2");
	sb_sigil[2] = Draw_PicFromWad ("sb_sigil3");
	sb_sigil[3] = Draw_PicFromWad ("sb_sigil4");

	sb_faces[4][0] = Draw_PicFromWad ("face1");
	sb_faces[4][1] = Draw_PicFromWad ("face_p1");
	sb_faces[3][0] = Draw_PicFromWad ("face2");
	sb_faces[3][1] = Draw_PicFromWad ("face_p2");
	sb_faces[2][0] = Draw_PicFromWad ("face3");
	sb_faces[2][1] = Draw_PicFromWad ("face_p3");
	sb_faces[1][0] = Draw_PicFromWad ("face4");
	sb_faces[1][1] = Draw_PicFromWad ("face_p4");
	sb_faces[0][0] = Draw_PicFromWad ("face5");
	sb_faces[0][1] = Draw_PicFromWad ("face_p5");

	sb_face_invis = Draw_PicFromWad ("face_invis");
	sb_face_invuln = Draw_PicFromWad ("face_invul2");
	sb_face_invis_invuln = Draw_PicFromWad ("face_inv2");
	sb_face_quad = Draw_PicFromWad ("face_quad");

	sb_sbar = Draw_PicFromWad ("sbar");
	sb_ibar = Draw_PicFromWad ("ibar");
	sb_scorebar = Draw_PicFromWad ("scorebar");

	hudtype = 0;

	// MED 01/04/97 added new hipnotic weapons
	if (!hudtype)
	{
		hudtype = 1;
		hsb_weapons[0][0] = Sbar_CheckPicFromWad ("inv_laser");
		hsb_weapons[0][1] = Sbar_CheckPicFromWad ("inv_mjolnir");
		hsb_weapons[0][2] = Sbar_CheckPicFromWad ("inv_gren_prox");
		hsb_weapons[0][3] = Sbar_CheckPicFromWad ("inv_prox_gren");
		hsb_weapons[0][4] = Sbar_CheckPicFromWad ("inv_prox");

		hsb_weapons[1][0] = Sbar_CheckPicFromWad ("inv2_laser");
		hsb_weapons[1][1] = Sbar_CheckPicFromWad ("inv2_mjolnir");
		hsb_weapons[1][2] = Sbar_CheckPicFromWad ("inv2_gren_prox");
		hsb_weapons[1][3] = Sbar_CheckPicFromWad ("inv2_prox_gren");
		hsb_weapons[1][4] = Sbar_CheckPicFromWad ("inv2_prox");

		for (i = 0; i < 5; i++)
		{
			hsb_weapons[2 + i][0] = Sbar_CheckPicFromWad (va ("inva%i_laser", i + 1));
			hsb_weapons[2 + i][1] = Sbar_CheckPicFromWad (va ("inva%i_mjolnir", i + 1));
			hsb_weapons[2 + i][2] = Sbar_CheckPicFromWad (va ("inva%i_gren_prox", i + 1));
			hsb_weapons[2 + i][3] = Sbar_CheckPicFromWad (va ("inva%i_prox_gren", i + 1));
			hsb_weapons[2 + i][4] = Sbar_CheckPicFromWad (va ("inva%i_prox", i + 1));
		}

		hsb_items[0] = Sbar_CheckPicFromWad ("sb_wsuit");
		hsb_items[1] = Sbar_CheckPicFromWad ("sb_eshld");
	}

	if (!hudtype)
	{
		hudtype = 2;
		rsb_invbar[0] = Sbar_CheckPicFromWad ("r_invbar1");
		rsb_invbar[1] = Sbar_CheckPicFromWad ("r_invbar2");

		rsb_weapons[0] = Sbar_CheckPicFromWad ("r_lava");
		rsb_weapons[1] = Sbar_CheckPicFromWad ("r_superlava");
		rsb_weapons[2] = Sbar_CheckPicFromWad ("r_gren");
		rsb_weapons[3] = Sbar_CheckPicFromWad ("r_multirock");
		rsb_weapons[4] = Sbar_CheckPicFromWad ("r_plasma");

		rsb_items[0] = Sbar_CheckPicFromWad ("r_shield1");
		rsb_items[1] = Sbar_CheckPicFromWad ("r_agrav1");

		// PGM 01/19/97 - team color border
		rsb_teambord = Sbar_CheckPicFromWad ("r_teambord");
		// PGM 01/19/97 - team color border

		rsb_ammo[0] = Sbar_CheckPicFromWad ("r_ammolava");
		rsb_ammo[1] = Sbar_CheckPicFromWad ("r_ammomulti");
		rsb_ammo[2] = Sbar_CheckPicFromWad ("r_ammoplasma");
	}
}

/*
===============
Sbar_Init -- johnfitz -- rewritten
===============
*/
void Sbar_Init (void)
{
	Cmd_AddCommand ("+showscores", Sbar_ShowScores);
	Cmd_AddCommand ("-showscores", Sbar_DontShowScores);

	Sbar_LoadPics ();
}

//=============================================================================

// drawing routines are relative to the status bar location

/*
=============
Sbar_DrawPic -- johnfitz -- rewritten now that GL_SetCanvas is doing the work
=============
*/
void Sbar_DrawPic (cb_context_t *cbx, int x, int y, qpic_t *pic)
{
	Draw_Pic (cbx, x, y + 24, pic, 1.0f, false);
}

/*
=============
Sbar_DrawPicAlpha -- johnfitz
=============
*/
void Sbar_DrawPicAlpha (cb_context_t *cbx, int x, int y, qpic_t *pic, float alpha)
{
	Draw_Pic (cbx, x, y + 24, pic, alpha, true);
}

/*
================
Sbar_DrawCharacter -- johnfitz -- rewritten now that GL_SetCanvas is doing the work
================
*/
void Sbar_DrawCharacter (cb_context_t *cbx, int x, int y, int num)
{
	Draw_Character (cbx, x, y + 24, num);
}

/*
================
Sbar_DrawString -- johnfitz -- rewritten now that GL_SetCanvas is doing the work
================
*/
void Sbar_DrawString (cb_context_t *cbx, int x, int y, const char *str)
{
	Draw_String (cbx, x, y + 24, str);
}

/*
===============
Sbar_DrawScrollString -- johnfitz
===============
*/
void Sbar_DrawScrollString (cb_context_t *cbx, int x, int y, int width, const char *str)
{
	int len, ofs;
	len = strlen (str) * 8 + 40;
	ofs = ((int)(realtime * 30)) % len;
	Sbar_DrawString (cbx, x - ofs, y, str);
	Sbar_DrawCharacter (cbx, x - ofs + len - 32, y, '/');
	Sbar_DrawCharacter (cbx, x - ofs + len - 24, y, '/');
	Sbar_DrawCharacter (cbx, x - ofs + len - 16, y, '/');
	Sbar_DrawString (cbx, x - ofs + len, y, str);
}

/*
=============
Sbar_itoa
=============
*/
int Sbar_itoa (int num, char *buf)
{
	char *str;
	int	  pow10;
	int	  dig;

	str = buf;

	if (num < 0)
	{
		*str++ = '-';
		num = -num;
	}

	for (pow10 = 10; num >= pow10; pow10 *= 10)
		;

	do
	{
		pow10 /= 10;
		dig = num / pow10;
		*str++ = '0' + dig;
		num -= dig * pow10;
	} while (pow10 != 1);

	*str = 0;

	return str - buf;
}

/*
=============
Sbar_DrawNum
=============
*/
void Sbar_DrawNum (cb_context_t *cbx, int x, int y, int num, int digits, int color)
{
	char  str[12];
	char *ptr;
	int	  l, frame;

	num = q_min (999, num); // johnfitz -- cap high values rather than truncating number

	l = Sbar_itoa (num, str);
	ptr = str;
	if (l > digits)
		ptr += (l - digits);
	if (l < digits)
		x += (digits - l) * 24;

	while (*ptr)
	{
		if (*ptr == '-')
			frame = STAT_MINUS;
		else
			frame = *ptr - '0';

		Sbar_DrawPic (cbx, x, y, sb_nums[color][frame]); // johnfitz -- DrawTransPic is obsolete
		x += 24;
		ptr++;
	}
}

//=============================================================================

int fragsort[MAX_SCOREBOARD];

char scoreboardtext[MAX_SCOREBOARD][20];
int	 scoreboardtop[MAX_SCOREBOARD];
int	 scoreboardbottom[MAX_SCOREBOARD];
int	 scoreboardcount[MAX_SCOREBOARD];
int	 scoreboardlines;

/*
===============
Sbar_SortFrags
===============
*/
void Sbar_SortFrags (void)
{
	int i, j, k;

	// sort by frags
	scoreboardlines = 0;
	for (i = 0; i < cl.maxclients; i++)
	{
		if (cl.scores[i].name[0])
		{
			fragsort[scoreboardlines] = i;
			scoreboardlines++;
		}
	}

	for (i = 0; i < scoreboardlines; i++)
	{
		for (j = 0; j < scoreboardlines - 1 - i; j++)
		{
			if (cl.scores[fragsort[j]].frags < cl.scores[fragsort[j + 1]].frags)
			{
				k = fragsort[j];
				fragsort[j] = fragsort[j + 1];
				fragsort[j + 1] = k;
			}
		}
	}
}

int Sbar_ColorForMap (int m)
{
	return m < 128 ? m + 8 : m + 8;
}

/*
===============
Sbar_UpdateScoreboard
===============
*/
void Sbar_UpdateScoreboard (void)
{
	int			  i, k;
	int			  top, bottom;
	scoreboard_t *s;

	Sbar_SortFrags ();

	// draw the text
	memset (scoreboardtext, 0, sizeof (scoreboardtext));

	for (i = 0; i < scoreboardlines; i++)
	{
		k = fragsort[i];
		s = &cl.scores[k];
		q_snprintf (&scoreboardtext[i][1], sizeof (scoreboardtext[i]) - 1, "%3i %s", s->frags, s->name);

		top = s->colors & 0xf0;
		bottom = (s->colors & 15) << 4;
		scoreboardtop[i] = Sbar_ColorForMap (top);
		scoreboardbottom[i] = Sbar_ColorForMap (bottom);
	}
}

/*
===============
Sbar_SoloScoreboard -- johnfitz -- new layout
===============
*/
void Sbar_SoloScoreboard (cb_context_t *cbx)
{
	char str[256];
	int	 minutes, seconds, tens, units;
	int	 left, right, len;

	q_snprintf (str, sizeof (str), "Kills: %i/%i", cl.stats[STAT_MONSTERS], cl.stats[STAT_TOTALMONSTERS]);
	left = 8 + strlen (str) * 8;
	Sbar_DrawString (cbx, 8, 12, str);

	q_snprintf (str, sizeof (str), "Secrets: %i/%i", cl.stats[STAT_SECRETS], cl.stats[STAT_TOTALSECRETS]);
	right = 312 - strlen (str) * 8;
	Sbar_DrawString (cbx, right, 12, str);

	if (!fitzmode)
	{ /* QuakeSpasm customization: */
		q_snprintf (str, sizeof (str), "skill %i", (int)(skill.value + 0.5));
		Sbar_DrawString (cbx, (left + right) / 2 - strlen (str) * 4, 12, str);

		q_snprintf (str, sizeof (str), "%s (%s)", cl.levelname, cl.mapname);
		len = strlen (str);
		if (len > 40)
			Sbar_DrawScrollString (cbx, 0, 4, 320, str);
		else
			Sbar_DrawString (cbx, 160 - len * 4, 4, str);
		return;
	}
	minutes = cl.time / 60;
	seconds = cl.time - 60 * minutes;
	tens = seconds / 10;
	units = seconds - 10 * tens;
	q_snprintf (str, sizeof (str), "%i:%i%i", minutes, tens, units);
	Sbar_DrawString (cbx, 160 - strlen (str) * 4, 12, str);

	len = strlen (cl.levelname);
	if (len > 40)
		Sbar_DrawScrollString (cbx, 0, 4, 320, cl.levelname);
	else
		Sbar_DrawString (cbx, 160 - len * 4, 4, cl.levelname);
}

/*
===============
Sbar_DrawScoreboard
===============
*/
void Sbar_DrawScoreboard (cb_context_t *cbx)
{
	Sbar_SoloScoreboard (cbx);
	if (cl.gametype == GAME_DEATHMATCH)
		Sbar_DeathmatchOverlay (cbx);
}

//=============================================================================

/*
===============
Sbar_DrawInventory
===============
*/
void Sbar_DrawInventory (cb_context_t *cbx)
{
	int	  i, val;
	char  num[6];
	float time;
	int	  flashon;

	if (rogue)
	{
		if (cl.stats[STAT_ACTIVEWEAPON] >= RIT_LAVA_NAILGUN)
			Sbar_DrawPicAlpha (cbx, 0, -24, rsb_invbar[0], scr_sbaralpha.value); // johnfitz -- scr_sbaralpha
		else
			Sbar_DrawPicAlpha (cbx, 0, -24, rsb_invbar[1], scr_sbaralpha.value); // johnfitz -- scr_sbaralpha
	}
	else
	{
		Sbar_DrawPicAlpha (cbx, 0, -24, sb_ibar, scr_sbaralpha.value); // johnfitz -- scr_sbaralpha
	}

	// weapons
	for (i = 0; i < 7; i++)
	{
		if (cl.items & (IT_SHOTGUN << i))
		{
			time = cl.item_gettime[i];
			if (time > (float)cl.time)
				time = cl.item_gettime[i] = cl.time - 2;
			flashon = (int)((cl.time - time) * 10);
			if (flashon >= 10)
			{
				if (cl.stats[STAT_ACTIVEWEAPON] == (IT_SHOTGUN << i))
					flashon = 1;
				else
					flashon = 0;
			}
			else
				flashon = (flashon % 5) + 2;

			Sbar_DrawPic (cbx, i * 24, -16, sb_weapons[flashon][i]);
		}
	}

	// MED 01/04/97
	// hipnotic weapons
	if (hipnotic)
	{
		int grenadeflashing = 0;
		for (i = 0; i < 4; i++)
		{
			if (cl.items & (1 << hipweapons[i]))
			{
				time = cl.item_gettime[hipweapons[i]];
				if (time > (float)cl.time)
					time = cl.item_gettime[hipweapons[i]] = cl.time - 2;
				flashon = (int)((cl.time - time) * 10);
				if (flashon >= 10)
				{
					if (cl.stats[STAT_ACTIVEWEAPON] == (1 << hipweapons[i]))
						flashon = 1;
					else
						flashon = 0;
				}
				else
					flashon = (flashon % 5) + 2;

				// check grenade launcher
				if (i == 2)
				{
					if (cl.items & HIT_PROXIMITY_GUN)
					{
						if (flashon)
						{
							grenadeflashing = 1;
							Sbar_DrawPic (cbx, 96, -16, hsb_weapons[flashon][2]);
						}
					}
				}
				else if (i == 3)
				{
					if (cl.items & (IT_SHOTGUN << 4))
					{
						if (flashon && !grenadeflashing)
						{
							Sbar_DrawPic (cbx, 96, -16, hsb_weapons[flashon][3]);
						}
						else if (!grenadeflashing)
						{
							Sbar_DrawPic (cbx, 96, -16, hsb_weapons[0][3]);
						}
					}
					else
						Sbar_DrawPic (cbx, 96, -16, hsb_weapons[flashon][4]);
				}
				else
					Sbar_DrawPic (cbx, 176 + (i * 24), -16, hsb_weapons[flashon][i]);
			}
		}
	}

	if (rogue)
	{
		// check for powered up weapon.
		if (cl.stats[STAT_ACTIVEWEAPON] >= RIT_LAVA_NAILGUN)
		{
			for (i = 0; i < 5; i++)
			{
				if (cl.stats[STAT_ACTIVEWEAPON] == (RIT_LAVA_NAILGUN << i))
				{
					Sbar_DrawPic (cbx, (i + 2) * 24, -16, rsb_weapons[i]);
				}
			}
		}
	}

	// ammo counts
	for (i = 0; i < 4; i++)
	{
		val = cl.stats[STAT_SHELLS + i];
		val = (val < 0) ? 0 : q_min (999, val); // johnfitz -- cap displayed value to 999
		q_snprintf (num, sizeof (num), "%3i", val);
		if (num[0] != ' ')
			Sbar_DrawCharacter (cbx, (6 * i + 1) * 8 + 2, -24, 18 + num[0] - '0');
		if (num[1] != ' ')
			Sbar_DrawCharacter (cbx, (6 * i + 2) * 8 + 2, -24, 18 + num[1] - '0');
		if (num[2] != ' ')
			Sbar_DrawCharacter (cbx, (6 * i + 3) * 8 + 2, -24, 18 + num[2] - '0');
	}

	flashon = 0;
	// items
	for (i = 0; i < 6; i++)
	{
		if (cl.items & (1 << (17 + i)))
		{
			time = cl.item_gettime[17 + i];
			if (!time || time <= cl.time - 2 || !flashon)
			{
				// MED 01/04/97 changed keys
				if (!hipnotic || (i > 1))
					Sbar_DrawPic (cbx, 192 + i * 16, -16, sb_items[i]);
			}
		}
	}
	// MED 01/04/97 added hipnotic items
	//  hipnotic items
	if (hipnotic)
	{
		for (i = 0; i < 2; i++)
		{
			if (cl.items & (1 << (24 + i)))
			{
				time = cl.item_gettime[24 + i];
				if (!time || time <= cl.time - 2 || !flashon)
					Sbar_DrawPic (cbx, 288 + i * 16, -16, hsb_items[i]);
			}
		}
	}

	if (rogue)
	{
		// new rogue items
		for (i = 0; i < 2; i++)
		{
			if (cl.items & (1 << (29 + i)))
			{
				time = cl.item_gettime[29 + i];
				if (!time || time <= cl.time - 2 || !flashon)
					Sbar_DrawPic (cbx, 288 + i * 16, -16, rsb_items[i]);
			}
		}
	}
	else
	{
		// sigils
		for (i = 0; i < 4; i++)
		{
			if (cl.items & (1u << (28 + i)))
			{
				time = cl.item_gettime[28 + i];
				if (!time || time <= cl.time - 2 || !flashon)
					Sbar_DrawPic (cbx, 320 - 32 + i * 8, -16, sb_sigil[i]);
			}
		}
	}
}

//=============================================================================

/*
===============
Sbar_DrawFrags -- johnfitz -- heavy revision
===============
*/
void Sbar_DrawFrags (cb_context_t *cbx)
{
	int			  numscores, i, x, color;
	char		  num[12];
	scoreboard_t *s;

	Sbar_SortFrags ();

	// draw the text
	numscores = q_min (scoreboardlines, 4);

	for (i = 0, x = 184; i < numscores; i++, x += 32)
	{
		s = &cl.scores[fragsort[i]];
		if (!s->name[0])
			continue;

		// top color
		color = s->colors & 0xf0;
		color = Sbar_ColorForMap (color);
		Draw_Fill (cbx, x + 10, 1, 28, 4, color, 1);

		// bottom color
		color = (s->colors & 15) << 4;
		color = Sbar_ColorForMap (color);
		Draw_Fill (cbx, x + 10, 5, 28, 3, color, 1);

		// number
		q_snprintf (num, sizeof (num), "%3i", s->frags);
		Sbar_DrawCharacter (cbx, x + 12, -24, num[0]);
		Sbar_DrawCharacter (cbx, x + 20, -24, num[1]);
		Sbar_DrawCharacter (cbx, x + 28, -24, num[2]);

		// brackets
		if (fragsort[i] == cl.viewentity - 1)
		{
			Sbar_DrawCharacter (cbx, x + 6, -24, 16);
			Sbar_DrawCharacter (cbx, x + 32, -24, 17);
		}
	}
}

//=============================================================================

/*
===============
Sbar_DrawFace
===============
*/
void Sbar_DrawFace (cb_context_t *cbx, int x, int y, qboolean classic_style)
{
	int f, anim;

	// PGM 01/19/97 - team color drawing
	// PGM 03/02/97 - fixed so color swatch only appears in CTF modes
	if (classic_style && rogue && (cl.maxclients != 1) && (teamplay.value > 3) && (teamplay.value < 7))
	{
		int			  top, bottom;
		int			  xofs;
		char		  num[12];
		scoreboard_t *s;

		s = &cl.scores[cl.viewentity - 1];
		// draw background
		top = s->colors & 0xf0;
		bottom = (s->colors & 15) << 4;
		top = Sbar_ColorForMap (top);
		bottom = Sbar_ColorForMap (bottom);

		if (cl.gametype == GAME_DEATHMATCH)
			xofs = 113;
		else
			xofs = ((vid.width - 320) >> 1) + 113;

		Sbar_DrawPic (cbx, 112, 0, rsb_teambord);
		Draw_Fill (cbx, xofs, 24 + 3, 22, 9, top, 1);
		Draw_Fill (cbx, xofs, 24 + 12, 22, 9, bottom, 1);

		// draw number
		f = s->frags;
		q_snprintf (num, sizeof (num), "%3i", f);

		if (top == 8)
		{
			if (num[0] != ' ')
				Sbar_DrawCharacter (cbx, 113, 3, 18 + num[0] - '0');
			if (num[1] != ' ')
				Sbar_DrawCharacter (cbx, 120, 3, 18 + num[1] - '0');
			if (num[2] != ' ')
				Sbar_DrawCharacter (cbx, 127, 3, 18 + num[2] - '0');
		}
		else
		{
			Sbar_DrawCharacter (cbx, 113, 3, num[0]);
			Sbar_DrawCharacter (cbx, 120, 3, num[1]);
			Sbar_DrawCharacter (cbx, 127, 3, num[2]);
		}

		return;
	}
	// PGM 01/19/97 - team color drawing

	if ((cl.items & (IT_INVISIBILITY | IT_INVULNERABILITY)) == (IT_INVISIBILITY | IT_INVULNERABILITY))
	{
		Sbar_DrawPic (cbx, x, y, sb_face_invis_invuln);
		return;
	}
	if (cl.items & IT_QUAD)
	{
		Sbar_DrawPic (cbx, x, y, sb_face_quad);
		return;
	}
	if (cl.items & IT_INVISIBILITY)
	{
		Sbar_DrawPic (cbx, x, y, sb_face_invis);
		return;
	}
	if (cl.items & IT_INVULNERABILITY)
	{
		Sbar_DrawPic (cbx, x, y, sb_face_invuln);
		return;
	}

	if (cl.stats[STAT_HEALTH] >= 100)
		f = 4;
	else
		f = cl.stats[STAT_HEALTH] / 20;
	if (f < 0) // in case we ever decide to draw when health <= 0
		f = 0;

	if (cl.time <= cl.faceanimtime)
		anim = 1;
	else
		anim = 0;
	Sbar_DrawPic (cbx, x, y, sb_faces[f][anim]);
}

/*
===============
Sbar_DrawCSCQ
===============
*/
static void Sbar_DrawCSCQ (cb_context_t *cbx)
{
	qboolean deathmatchoverlay = false;
	float	 s = CLAMP (1.0, scr_sbarscale.value, (float)glwidth / 320.0);
	int		 items = cl.stats[STAT_ITEMS];
	if (cl.time < cl.oldtime)
		cl.stats[STAT_ITEMS] = 0;
	GL_SetCanvas (cbx, CANVAS_CSQC); // johnfitz
	PR_SwitchQCVM (&cl.qcvm);
	pr_global_struct->frametime = host_frametime;
	if (qcvm->extglobals.cltime)
		*qcvm->extglobals.cltime = realtime;
	if (qcvm->extglobals.clframetime)
		*qcvm->extglobals.clframetime = host_frametime;
	if (qcvm->extglobals.intermission)
		*qcvm->extglobals.intermission = cl.intermission;
	if (qcvm->extglobals.player_localentnum)
		*qcvm->extglobals.player_localentnum = cl.viewentity;
	pr_global_struct->time = cl.time;
	Sbar_SortFrags ();
	G_VECTORSET (OFS_PARM0, vid.width / s, vid.height / s, 0);
	G_FLOAT (OFS_PARM1) = sb_showscores;
	PR_ExecuteProgram (cl.qcvm.extfuncs.CSQC_DrawHud);
	if (cl.qcvm.extfuncs.CSQC_DrawScores)
	{
		G_VECTORSET (OFS_PARM0, vid.width / s, vid.height / s, 0);
		G_FLOAT (OFS_PARM1) = sb_showscores;
		if (key_dest != key_menu)
			PR_ExecuteProgram (cl.qcvm.extfuncs.CSQC_DrawScores);
	}
	else
		deathmatchoverlay = (sb_showscores || cl.stats[STAT_HEALTH] <= 0);
	PR_SwitchQCVM (NULL);
	cl.stats[STAT_ITEMS] = items;

	if (deathmatchoverlay && cl.gametype == GAME_DEATHMATCH)
	{
		GL_SetCanvas (cbx, CANVAS_SBAR);
		Sbar_DeathmatchOverlay (cbx);
	}
}

/*
===============
Sbar_DrawClassic
===============
*/
static void Sbar_DrawClassic (cb_context_t *cbx)
{
	GL_SetCanvas (cbx, CANVAS_SBAR);

	if (scr_viewsize.value < 110) // johnfitz -- check viewsize instead of sb_lines
	{
		Sbar_DrawInventory (cbx);
		if (cl.maxclients != 1)
			Sbar_DrawFrags (cbx);
	}

	if (sb_showscores || cl.stats[STAT_HEALTH] <= 0)
	{
		Sbar_DrawPicAlpha (cbx, 0, 0, sb_scorebar, scr_sbaralpha.value); // johnfitz -- scr_sbaralpha
		Sbar_DrawScoreboard (cbx);
	}
	else if (scr_viewsize.value < 120) // johnfitz -- check viewsize instead of sb_lines
	{
		Sbar_DrawPicAlpha (cbx, 0, 0, sb_sbar, scr_sbaralpha.value); // johnfitz -- scr_sbaralpha

		// keys (hipnotic only)
		// MED 01/04/97 moved keys here so they would not be overwritten
		if (hipnotic)
		{
			if (cl.items & IT_KEY1)
				Sbar_DrawPic (cbx, 209, 3, sb_items[0]);
			if (cl.items & IT_KEY2)
				Sbar_DrawPic (cbx, 209, 12, sb_items[1]);
		}
		// armor
		if (cl.items & IT_INVULNERABILITY)
		{
			Sbar_DrawNum (cbx, 24, 0, 666, 3, 1);
			Sbar_DrawPic (cbx, 0, 0, draw_disc);
		}
		else
		{
			if (rogue)
			{
				Sbar_DrawNum (cbx, 24, 0, cl.stats[STAT_ARMOR], 3, cl.stats[STAT_ARMOR] <= 25);
				if (cl.items & RIT_ARMOR3)
					Sbar_DrawPic (cbx, 0, 0, sb_armor[2]);
				else if (cl.items & RIT_ARMOR2)
					Sbar_DrawPic (cbx, 0, 0, sb_armor[1]);
				else if (cl.items & RIT_ARMOR1)
					Sbar_DrawPic (cbx, 0, 0, sb_armor[0]);
			}
			else
			{
				Sbar_DrawNum (cbx, 24, 0, cl.stats[STAT_ARMOR], 3, cl.stats[STAT_ARMOR] <= 25);
				if (cl.items & IT_ARMOR3)
					Sbar_DrawPic (cbx, 0, 0, sb_armor[2]);
				else if (cl.items & IT_ARMOR2)
					Sbar_DrawPic (cbx, 0, 0, sb_armor[1]);
				else if (cl.items & IT_ARMOR1)
					Sbar_DrawPic (cbx, 0, 0, sb_armor[0]);
			}
		}

		// face
		Sbar_DrawFace (cbx, 112, 0, true);

		// health
		Sbar_DrawNum (cbx, 136, 0, cl.stats[STAT_HEALTH], 3, cl.stats[STAT_HEALTH] <= 25);

		// ammo icon
		if (rogue)
		{
			if (cl.items & RIT_SHELLS)
				Sbar_DrawPic (cbx, 224, 0, sb_ammo[0]);
			else if (cl.items & RIT_NAILS)
				Sbar_DrawPic (cbx, 224, 0, sb_ammo[1]);
			else if (cl.items & RIT_ROCKETS)
				Sbar_DrawPic (cbx, 224, 0, sb_ammo[2]);
			else if (cl.items & RIT_CELLS)
				Sbar_DrawPic (cbx, 224, 0, sb_ammo[3]);
			else if (cl.items & RIT_LAVA_NAILS)
				Sbar_DrawPic (cbx, 224, 0, rsb_ammo[0]);
			else if (cl.items & RIT_PLASMA_AMMO)
				Sbar_DrawPic (cbx, 224, 0, rsb_ammo[1]);
			else if (cl.items & RIT_MULTI_ROCKETS)
				Sbar_DrawPic (cbx, 224, 0, rsb_ammo[2]);
		}
		else
		{
			if (cl.items & IT_SHELLS)
				Sbar_DrawPic (cbx, 224, 0, sb_ammo[0]);
			else if (cl.items & IT_NAILS)
				Sbar_DrawPic (cbx, 224, 0, sb_ammo[1]);
			else if (cl.items & IT_ROCKETS)
				Sbar_DrawPic (cbx, 224, 0, sb_ammo[2]);
			else if (cl.items & IT_CELLS)
				Sbar_DrawPic (cbx, 224, 0, sb_ammo[3]);
		}

		Sbar_DrawNum (cbx, 248, 0, cl.stats[STAT_AMMO], 3, cl.stats[STAT_AMMO] <= 10);
	}

	// johnfitz -- removed the vid.width > 320 check here
	if (cl.gametype == GAME_DEATHMATCH)
		Sbar_MiniDeathmatchOverlay (cbx);
}

/*
===============
Sbar_DrawModern
===============
*/
static void Sbar_DrawModern (cb_context_t *cbx)
{
	if (scr_viewsize.value >= 120.0f)
		return;

	GL_SetCanvas (cbx, CANVAS_BOTTOMLEFT);
	Sbar_DrawFace (cbx, 20, 135, false);
	Sbar_DrawNum (cbx, 45, 135, cl.stats[STAT_HEALTH], 3, cl.stats[STAT_HEALTH] <= 25);

	if (scr_viewsize.value < 110.0f)
	{
		// armor
		const int ARMOR_NUM_X = 45;
		const int ARMOR_NUM_Y = 110;
		const int ARMOR_ICON_X = 20;
		const int ARMOR_ICON_Y = 110;
		if (cl.items & IT_INVULNERABILITY)
		{
			Sbar_DrawNum (cbx, ARMOR_NUM_X, ARMOR_NUM_Y, 666, 3, 1);
			Sbar_DrawPic (cbx, ARMOR_ICON_X, ARMOR_ICON_Y, draw_disc);
		}
		else if (cl.stats[STAT_ARMOR] > 0)
		{
			if (rogue)
			{
				Sbar_DrawNum (cbx, ARMOR_NUM_X, ARMOR_NUM_Y, cl.stats[STAT_ARMOR], 3, cl.stats[STAT_ARMOR] <= 25);
				if (cl.items & RIT_ARMOR3)
					Sbar_DrawPic (cbx, ARMOR_ICON_X, ARMOR_ICON_Y, sb_armor[2]);
				else if (cl.items & RIT_ARMOR2)
					Sbar_DrawPic (cbx, ARMOR_ICON_X, ARMOR_ICON_Y, sb_armor[1]);
				else if (cl.items & RIT_ARMOR1)
					Sbar_DrawPic (cbx, ARMOR_ICON_X, ARMOR_ICON_Y, sb_armor[0]);
			}
			else
			{
				Sbar_DrawNum (cbx, ARMOR_NUM_X, ARMOR_NUM_Y, cl.stats[STAT_ARMOR], 3, cl.stats[STAT_ARMOR] <= 25);
				if (cl.items & IT_ARMOR3)
					Sbar_DrawPic (cbx, ARMOR_ICON_X, ARMOR_ICON_Y, sb_armor[2]);
				else if (cl.items & IT_ARMOR2)
					Sbar_DrawPic (cbx, ARMOR_ICON_X, ARMOR_ICON_Y, sb_armor[1]);
				else if (cl.items & IT_ARMOR1)
					Sbar_DrawPic (cbx, ARMOR_ICON_X, ARMOR_ICON_Y, sb_armor[0]);
			}
		}
	}

	{
		GL_SetCanvas (cbx, CANVAS_BOTTOMRIGHT);
		Sbar_DrawNum (cbx, 195, 135, cl.stats[STAT_AMMO], 3, cl.stats[STAT_AMMO] <= 10);

		// ammo icon
		const int AMMO_ICON_X = 280;
		const int AMMO_ICON_Y = 135;
		if (rogue)
		{
			if (cl.items & RIT_SHELLS)
				Sbar_DrawPic (cbx, AMMO_ICON_X, AMMO_ICON_Y, sb_ammo[0]);
			else if (cl.items & RIT_NAILS)
				Sbar_DrawPic (cbx, AMMO_ICON_X, AMMO_ICON_Y, sb_ammo[1]);
			else if (cl.items & RIT_ROCKETS)
				Sbar_DrawPic (cbx, AMMO_ICON_X, AMMO_ICON_Y, sb_ammo[2]);
			else if (cl.items & RIT_CELLS)
				Sbar_DrawPic (cbx, AMMO_ICON_X, AMMO_ICON_Y, sb_ammo[3]);
			else if (cl.items & RIT_LAVA_NAILS)
				Sbar_DrawPic (cbx, AMMO_ICON_X, AMMO_ICON_Y, rsb_ammo[0]);
			else if (cl.items & RIT_PLASMA_AMMO)
				Sbar_DrawPic (cbx, AMMO_ICON_X, AMMO_ICON_Y, rsb_ammo[1]);
			else if (cl.items & RIT_MULTI_ROCKETS)
				Sbar_DrawPic (cbx, AMMO_ICON_X, AMMO_ICON_Y, rsb_ammo[2]);
		}
		else
		{
			if (cl.items & IT_SHELLS)
				Sbar_DrawPic (cbx, AMMO_ICON_X, AMMO_ICON_Y, sb_ammo[0]);
			else if (cl.items & IT_NAILS)
				Sbar_DrawPic (cbx, AMMO_ICON_X, AMMO_ICON_Y, sb_ammo[1]);
			else if (cl.items & IT_ROCKETS)
				Sbar_DrawPic (cbx, AMMO_ICON_X, AMMO_ICON_Y, sb_ammo[2]);
			else if (cl.items & IT_CELLS)
				Sbar_DrawPic (cbx, AMMO_ICON_X, AMMO_ICON_Y, sb_ammo[3]);
		}
	}

	if (scr_viewsize.value < 110.0f)
	{
		const int KEY_ICON_X = 284;
		const int KEY_ICON_Y = 115;
		// keys
		for (int i = 0; i < 2; i++)
		{
			if (cl.items & (1 << (17 + i)) && (!hipnotic || (i > 1)))
				Sbar_DrawPic (cbx, KEY_ICON_X, KEY_ICON_Y - (i * 16), sb_items[i]);
		}
	}

	GL_SetCanvas (cbx, CANVAS_SBAR);
	if (sb_showscores || cl.stats[STAT_HEALTH] <= 0)
	{
		Sbar_DrawPicAlpha (cbx, 0, 0, sb_scorebar, scr_sbaralpha.value); // johnfitz -- scr_sbaralpha
		Sbar_DrawScoreboard (cbx);
	}
}

/*
===============
Sbar_Draw
===============
*/
void Sbar_Draw (cb_context_t *cbx)
{
	float w; // johnfitz

	if (scr_con_current == vid.height)
		return; // console is full screen

	if ((scr_style.value < 1.0f) && cl.qcvm.extfuncs.CSQC_DrawHud && !qcvm)
	{
		Sbar_DrawCSCQ (cbx);
		return;
	}

	if (cl.intermission)
		return; // johnfitz -- never draw sbar during intermission

	GL_SetCanvas (cbx, CANVAS_DEFAULT); // johnfitz

	// johnfitz -- don't waste fillrate by clearing the area behind the sbar
	w = CLAMP (320.0f, scr_sbarscale.value * 320.0f, (float)glwidth);
	if (sb_lines && glwidth > w)
	{
		if (scr_sbaralpha.value < 1)
			Draw_TileClear (cbx, 0, glheight - sb_lines, glwidth, sb_lines);
		if (cl.gametype == GAME_DEATHMATCH)
			Draw_TileClear (cbx, w, glheight - sb_lines, glwidth - w, sb_lines);
		else
		{
			Draw_TileClear (cbx, 0, glheight - sb_lines, (glwidth - w) / 2.0f, sb_lines);
			Draw_TileClear (cbx, (glwidth - w) / 2.0f + w, glheight - sb_lines, (glwidth - w) / 2.0f, sb_lines);
		}
	}
	// johnfitz

	if (scr_style.value < 2.0f)
		Sbar_DrawClassic (cbx);
	else
		Sbar_DrawModern (cbx);
}

//=============================================================================

/*
==================
Sbar_IntermissionNumber

==================
*/
void Sbar_IntermissionNumber (cb_context_t *cbx, int x, int y, int num, int digits, int color)
{
	char  str[12];
	char *ptr;
	int	  l, frame;

	l = Sbar_itoa (num, str);
	ptr = str;
	if (l > digits)
		ptr += (l - digits);
	if (l < digits)
		x += (digits - l) * 24;

	while (*ptr)
	{
		if (*ptr == '-')
			frame = STAT_MINUS;
		else
			frame = *ptr - '0';

		Draw_Pic (cbx, x, y, sb_nums[color][frame], 1.0f, false); // johnfitz -- stretched menus
		x += 24;
		ptr++;
	}
}

/*
==================
Sbar_IntermissionPicForChar
==================
*/
qpic_t *Sbar_IntermissionPicForChar (char c, int color)
{
	if ((unsigned)(c - '0') < 10)
		return sb_nums[color][c - '0'];
	if (c == '/')
		return sb_slash;
	if (c == ':')
		return sb_colon;
	if (c == '-')
		return sb_nums[color][STAT_MINUS];
	return NULL;
}

/*
==================
Sbar_IntermissionTextWidth
==================
*/
int Sbar_IntermissionTextWidth (const char *str, int color)
{
	int len = 0;
	while (*str)
	{
		qpic_t *pic = Sbar_IntermissionPicForChar (*str++, color);
		len += pic ? pic->width : 24;
	}
	return len;
}

/*
==================
Sbar_IntermissionText
==================
*/
void Sbar_IntermissionText (cb_context_t *cbx, int x, int y, const char *str, int color)
{
	while (*str)
	{
		qpic_t *pic = Sbar_IntermissionPicForChar (*str++, color);
		if (!pic)
			continue;
		Draw_Pic (cbx, x, y, pic, 1.0f, false);
		x += pic ? pic->width : 24;
	}
}

/*
==================
Sbar_DeathmatchOverlay
==================
*/
void Sbar_DeathmatchOverlay (cb_context_t *cbx)
{
	qpic_t		 *pic;
	int			  i, k, l;
	int			  top, bottom;
	int			  x, y, f;
	char		  num[12];
	scoreboard_t *s;

	GL_SetCanvas (cbx, CANVAS_MENU); // johnfitz

	pic = Draw_CachePic ("gfx/ranking.lmp");
	M_DrawPic (cbx, (320 - pic->width) / 2, 8, pic);

	// scores
	Sbar_SortFrags ();

	// draw the text
	l = scoreboardlines;

	x = 80; // johnfitz -- simplified becuase some positioning is handled elsewhere
	y = 40;
	for (i = 0; i < l; i++)
	{
		k = fragsort[i];
		s = &cl.scores[k];
		if (!s->name[0])
			continue;

		// draw background
		top = s->colors & 0xf0;
		bottom = (s->colors & 15) << 4;
		top = Sbar_ColorForMap (top);
		bottom = Sbar_ColorForMap (bottom);

		Draw_Fill (cbx, x, y, 40, 4, top, 1);		 // johnfitz -- stretched overlays
		Draw_Fill (cbx, x, y + 4, 40, 4, bottom, 1); // johnfitz -- stretched overlays

		// draw number
		f = s->frags;
		q_snprintf (num, sizeof (num), "%3i", f);

		Draw_Character (cbx, x + 8, y, num[0]);	 // johnfitz -- stretched overlays
		Draw_Character (cbx, x + 16, y, num[1]); // johnfitz -- stretched overlays
		Draw_Character (cbx, x + 24, y, num[2]); // johnfitz -- stretched overlays

		if (k == cl.viewentity - 1)
			Draw_Character (cbx, x - 8, y, 12); // johnfitz -- stretched overlays

#if 0
{
	int				total;
	int				n, minutes, tens, units;

	// draw time
		total = cl.completed_time - s->entertime;
		minutes = (int)total/60;
		n = total - minutes*60;
		tens = n/10;
		units = n%10;

		q_snprintf (num, sizeof(num), "%3i:%i%i", minutes, tens, units);

		M_Print ( x+48 , y, num); //johnfitz -- was Draw_String, changed for stretched overlays
}
#endif

		// draw name
		M_Print (cbx, x + 64, y, s->name); // johnfitz -- was Draw_String, changed for stretched overlays

		y += 10;
	}

	GL_SetCanvas (cbx, CANVAS_SBAR); // johnfitz
}

/*
==================
Sbar_MiniDeathmatchOverlay
==================
*/
void Sbar_MiniDeathmatchOverlay (cb_context_t *cbx)
{
	int			  i, k, top, bottom, x, y, f, numlines;
	char		  num[12];
	float		  scale; // johnfitz
	scoreboard_t *s;

	scale = CLAMP (1.0, scr_sbarscale.value, (float)glwidth / 320.0); // johnfitz

	// MAX_SCOREBOARDNAME = 32, so total width for this overlay plus sbar is 632, but we can cut off some i guess
	if (glwidth / scale < 512 || scr_viewsize.value >= 120) // johnfitz -- test should consider scr_sbarscale
		return;

	// scores
	Sbar_SortFrags ();

	// draw the text
	numlines = (scr_viewsize.value >= 110) ? 3 : 6; // johnfitz

	// find us
	for (i = 0; i < scoreboardlines; i++)
		if (fragsort[i] == cl.viewentity - 1)
			break;
	if (i == scoreboardlines) // we're not there
		i = 0;
	else // figure out start
		i = i - numlines / 2;
	if (i > scoreboardlines - numlines)
		i = scoreboardlines - numlines;
	if (i < 0)
		i = 0;

	x = 324;
	y = (scr_viewsize.value >= 110) ? 24 : 0;			// johnfitz -- start at the right place
	for (; i < scoreboardlines && y <= 48; i++, y += 8) // johnfitz -- change y init, test, inc
	{
		k = fragsort[i];
		s = &cl.scores[k];
		if (!s->name[0])
			continue;

		// colors
		top = s->colors & 0xf0;
		bottom = (s->colors & 15) << 4;
		top = Sbar_ColorForMap (top);
		bottom = Sbar_ColorForMap (bottom);

		Draw_Fill (cbx, x, y + 1, 40, 4, top, 1);
		Draw_Fill (cbx, x, y + 5, 40, 3, bottom, 1);

		// number
		f = s->frags;
		q_snprintf (num, sizeof (num), "%3i", f);
		Draw_Character (cbx, x + 8, y, num[0]);
		Draw_Character (cbx, x + 16, y, num[1]);
		Draw_Character (cbx, x + 24, y, num[2]);

		// brackets
		if (k == cl.viewentity - 1)
		{
			Draw_Character (cbx, x, y, 16);
			Draw_Character (cbx, x + 32, y, 17);
		}

		// name
		Draw_String (cbx, x + 48, y, s->name);
	}
}

/*
==================
Sbar_IntermissionOverlay
==================
*/
void Sbar_IntermissionOverlay (cb_context_t *cbx)
{
	qpic_t *pic;
	char	time[32];
	char	secrets[32];
	char	monsters[32];
	int		ltime, lsecrets, lmonsters;
	int		total;

	if ((scr_style.value < 1.0f) && cl.qcvm.extfuncs.CSQC_DrawScores && !qcvm)
	{
		float s = CLAMP (1.0, scr_sbarscale.value, (float)glwidth / 320.0);
		GL_SetCanvas (cbx, CANVAS_CSQC);
		PR_SwitchQCVM (&cl.qcvm);
		if (qcvm->extglobals.cltime)
			*qcvm->extglobals.cltime = realtime;
		if (qcvm->extglobals.clframetime)
			*qcvm->extglobals.clframetime = host_frametime;
		if (qcvm->extglobals.player_localentnum)
			*qcvm->extglobals.player_localentnum = cl.viewentity;
		if (qcvm->extglobals.intermission)
			*qcvm->extglobals.intermission = cl.intermission;
		if (qcvm->extglobals.intermission_time)
			*qcvm->extglobals.intermission_time = cl.completed_time;
		pr_global_struct->time = cl.time;
		pr_global_struct->frametime = host_frametime;
		Sbar_SortFrags ();
		G_VECTORSET (OFS_PARM0, vid.width / s, vid.height / s, 0);
		G_FLOAT (OFS_PARM1) = sb_showscores;
		PR_ExecuteProgram (cl.qcvm.extfuncs.CSQC_DrawScores);
		PR_SwitchQCVM (NULL);
		return;
	}

	if (cl.gametype == GAME_DEATHMATCH)
	{
		Sbar_DeathmatchOverlay (cbx);
		return;
	}

	GL_SetCanvas (cbx, CANVAS_MENU); // johnfitz

	q_snprintf (time, sizeof (time), "%d:%02d", cl.completed_time / 60, cl.completed_time % 60);
	q_snprintf (secrets, sizeof (secrets), "%d/%2d", cl.stats[STAT_SECRETS], cl.stats[STAT_TOTALSECRETS]);
	q_snprintf (monsters, sizeof (monsters), "%d/%2d", cl.stats[STAT_MONSTERS], cl.stats[STAT_TOTALMONSTERS]);

	ltime = Sbar_IntermissionTextWidth (time, 0);
	lsecrets = Sbar_IntermissionTextWidth (secrets, 0);
	lmonsters = Sbar_IntermissionTextWidth (monsters, 0);

	total = q_max (ltime, lsecrets);
	total = q_max (lmonsters, total);

	pic = Draw_CachePic ("gfx/inter.lmp");
	total += pic->width + 24;
	total = q_min (320, total);
	Draw_Pic (cbx, 160 - total / 2, 56, pic, 1.0f, false);

	pic = Draw_CachePic ("gfx/complete.lmp");
	Draw_Pic (cbx, 160 - pic->width / 2, 24, pic, 1.0f, false);

	Sbar_IntermissionText (cbx, 160 + total / 2 - ltime, 64, time, 0);
	Sbar_IntermissionText (cbx, 160 + total / 2 - lsecrets, 104, secrets, 0);
	Sbar_IntermissionText (cbx, 160 + total / 2 - lmonsters, 144, monsters, 0);
}

/*
==================
Sbar_FinaleOverlay
==================
*/
void Sbar_FinaleOverlay (cb_context_t *cbx)
{
	qpic_t *pic;

	GL_SetCanvas (cbx, CANVAS_MENU); // johnfitz

	pic = Draw_CachePic ("gfx/finale.lmp");
	Draw_Pic (cbx, (320 - pic->width) / 2, 16, pic, 1.0f, false); // johnfitz -- stretched menus
}
