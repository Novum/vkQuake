/*
Copyright (C) 1996-2001 Id Software, Inc.
Copyright (C) 2002-2009 John Fitzgibbons and others
Copyright (C) 2007-2008 Kristian Duske
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

// screen.c -- master for refresh, status bar, console, chat, notify, etc

#include "quakedef.h"

#include "cfgfile.h"

#include <setjmp.h>

/*

background clear
rendering
turtle/net/ram icons
sbar
centerprint / slow centerprint
notify lines
intermission / finale overlay
loading plaque
console
menu

required background clears
required update regions


syncronous draw mode or async
One off screen buffer, with updates either copied or xblited
Need to double buffer?


async draw will require the refresh area to be cleared, because it will be
xblited, but sync draw can just ignore it.

sync
draw

CenterPrint ()
SlowPrint ()
Screen_Update ();
Con_Printf ();

net
turn off messages option

the refresh is allways rendered, unless the console is full screen


console is:
	notify lines
	half
	full

*/

int glwidth, glheight;

float scr_con_current;
float scr_conlines; // lines of console to display

// johnfitz -- new cvars
cvar_t scr_menuscale = {"scr_menuscale", "1", CVAR_ARCHIVE};
cvar_t scr_sbarscale = {"scr_sbarscale", "1", CVAR_ARCHIVE};
cvar_t scr_sbaralpha = {"scr_sbaralpha", "0.75", CVAR_ARCHIVE};
cvar_t scr_conwidth = {"scr_conwidth", "0", CVAR_ARCHIVE};
cvar_t scr_conscale = {"scr_conscale", "1", CVAR_ARCHIVE};
cvar_t scr_crosshairscale = {"scr_crosshairscale", "1", CVAR_ARCHIVE};
cvar_t scr_showfps = {"scr_showfps", "0", CVAR_ARCHIVE};
cvar_t scr_clock = {"scr_clock", "0", CVAR_NONE};
cvar_t scr_autoclock = {"scr_autoclock", "1", CVAR_ARCHIVE};
cvar_t scr_usekfont = {"scr_usekfont", "0", CVAR_NONE}; // 2021 re-release
cvar_t scr_style = {"scr_style", "0", CVAR_ARCHIVE};

cvar_t scr_viewsize = {"viewsize", "100", CVAR_ARCHIVE};
cvar_t scr_viewsize_allow_shrinking = {"viewsize_allow_shrinking", "0", CVAR_ARCHIVE};
cvar_t scr_fov = {"fov", "90", CVAR_ARCHIVE}; // 10 - 170
cvar_t scr_fov_adapt = {"fov_adapt", "1", CVAR_ARCHIVE};
cvar_t scr_zoomfov = {"zoom_fov", "30", CVAR_ARCHIVE}; // 10 - 170
cvar_t scr_zoomspeed = {"zoom_speed", "8", CVAR_ARCHIVE};
cvar_t scr_conspeed = {"scr_conspeed", "500", CVAR_ARCHIVE};
cvar_t scr_conanim = {"scr_conanim", "0", CVAR_ARCHIVE};
cvar_t scr_centertime = {"scr_centertime", "2", CVAR_NONE};
cvar_t scr_showturtle = {"showturtle", "0", CVAR_NONE};
cvar_t scr_showpause = {"showpause", "1", CVAR_NONE};
cvar_t scr_printspeed = {"scr_printspeed", "8", CVAR_NONE};

cvar_t cl_gun_fovscale = {"cl_gun_fovscale", "1", CVAR_ARCHIVE}; // Qrack

// All scaling is done relative to resolution with scr_relativescale
cvar_t scr_relativescale = {"scr_relativescale", "2", CVAR_ARCHIVE};
cvar_t scr_relmenuscale = {"scr_relmenuscale", "1", CVAR_ARCHIVE};
cvar_t scr_relsbarscale = {"scr_relsbarscale", "1", CVAR_ARCHIVE};
cvar_t scr_relcrosshairscale = {"scr_relcrosshairscale", "1", CVAR_ARCHIVE};
cvar_t scr_relconscale = {"scr_relconscale", "1", CVAR_ARCHIVE};

extern cvar_t crosshair;
extern cvar_t r_tasks;
extern cvar_t r_gpulightmapupdate;
extern cvar_t r_showtris;
extern cvar_t r_showbboxes;

qboolean scr_initialized; // ready to draw

qpic_t *scr_net;
qpic_t *scr_turtle;

int clearconsole;

vrect_t scr_vrect;

qboolean scr_disabled_for_loading;
qboolean scr_drawloading;
float	 scr_disabled_time;

qboolean	   in_update_screen;
extern jmp_buf screen_error;
SDL_mutex	  *draw_qcvm_mutex;

void SCR_ScreenShot_f (void);

/*
===============================================================================

CENTER PRINTING

===============================================================================
*/

char  scr_centerstring[1024];
float scr_centertime_start; // for slow victory printing
float scr_centertime_off;
float scr_clock_off;
int	  scr_center_lines;
int	  scr_erase_lines;
int	  scr_erase_center;

void SCR_CenterPrintClear (void)
{
	scr_centertime_off = 0;
	scr_clock_off = 0;
}

/*
==============
SCR_CenterPrint

Called for important messages that should stay in the center of the screen
for a few moments
==============
*/
void SCR_CenterPrint (const char *str) // update centerprint data
{
	strncpy (scr_centerstring, str, sizeof (scr_centerstring) - 1);
	scr_centertime_off = cl.time + scr_centertime.value;
	scr_centertime_start = cl.time;

	// count the number of lines for centering
	scr_center_lines = 1;
	str = scr_centerstring;
	while (*str)
	{
		if (*str == '\n')
			scr_center_lines++;
		str++;
	}
}

void SCR_DrawCenterString (cb_context_t *cbx) // actually do the drawing
{
	char *start;
	int	  l;
	int	  j;
	int	  x, y;
	int	  remaining;

	GL_SetCanvas (cbx, CANVAS_MENU); // johnfitz

	// the finale prints the characters one at a time
	if (cl.intermission)
		remaining = scr_printspeed.value * (cl.time - scr_centertime_start);
	else
		remaining = 9999;

	scr_erase_center = 0;
	start = scr_centerstring;

	if (scr_center_lines <= 4)
		y = 200 * 0.35; // johnfitz -- 320x200 coordinate system
	else
		y = 48;
	if (crosshair.value)
		y -= 8;

	do
	{
		// scan the width of the line
		for (l = 0; l < 40; l++)
			if (start[l] == '\n' || !start[l])
				break;
		x = (320 - l * 8) / 2; // johnfitz -- 320x200 coordinate system
		for (j = 0; j < l; j++, x += CHARACTER_SIZE)
		{
			Draw_Character (cbx, x, y, start[j]); // johnfitz -- stretch overlays
			if (!remaining--)
				return;
		}

		y += 8;

		while (*start && *start != '\n')
			start++;

		if (!*start)
			break;
		start++; // skip the \n
	} while (1);
}

void SCR_CheckDrawCenterString (cb_context_t *cbx)
{
	if (scr_center_lines > scr_erase_lines)
		scr_erase_lines = scr_center_lines;

	if (scr_centertime_off <= cl.time && !cl.intermission)
		return;
	if (key_dest != key_game)
		return;
	if (cl.paused && (!cls.demoplayback || cls.demospeed > 0.f)) // johnfitz -- don't show centerprint during a pause
		return;

	SCR_DrawCenterString (cbx);
}

//=============================================================================

/*
====================
SCR_ToggleZoom_f
====================
*/
static void SCR_ToggleZoom_f (void)
{
	if (cl.zoomdir)
		cl.zoomdir = -cl.zoomdir;
	else
		cl.zoomdir = cl.zoom > 0.5f ? -1.f : 1.f;
}

/*
====================
SCR_ZoomDown_f
====================
*/
static void SCR_ZoomDown_f (void)
{
	cl.zoomdir = 1.f;
}

/*
====================
SCR_ZoomUp_f
====================
*/
static void SCR_ZoomUp_f (void)
{
	cl.zoomdir = -1.f;
}

/*
====================
SCR_UpdateZoom
====================
*/
void SCR_UpdateZoom (void)
{
	float delta = cl.zoomdir * scr_zoomspeed.value * (cl.time - cl.oldtime);
	if (!delta)
		return;
	cl.zoom += delta;
	if (cl.zoom >= 1.f)
	{
		cl.zoom = 1.f;
		cl.zoomdir = 0.f;
	}
	else if (cl.zoom <= 0.f)
	{
		cl.zoom = 0.f;
		cl.zoomdir = 0.f;
	}
	vid.recalc_refdef = 1;
}

/*
====================
AdaptFovx
Adapt a 4:3 horizontal FOV to the current screen size using the "Hor+" scaling:
2.0 * atan(width / height * 3.0 / 4.0 * tan(fov_x / 2.0))
====================
*/
float AdaptFovx (float fov_x, float width, float height)
{
	float a, x;

	if (fov_x < 1 || fov_x > 179)
		Sys_Error ("Bad fov: %f", fov_x);
	if (cl.statsf[STAT_VIEWZOOM])
	{
		fov_x *= cl.statsf[STAT_VIEWZOOM] / 255.0;
		fov_x = CLAMP (1, fov_x, 179);
	}

	if (!scr_fov_adapt.value)
		return fov_x;
	if ((x = height / width) == 0.75)
		return fov_x;
	a = atan (0.75 / x * tan (fov_x / 360 * M_PI));
	a = a * 360 / M_PI;
	return a;
}

/*
====================
CalcFovy
====================
*/
float CalcFovy (float fov_x, float width, float height)
{
	float a, x;

	if (fov_x < 1 || fov_x > 179)
		Sys_Error ("Bad fov: %f", fov_x);

	x = width / tan (fov_x / 360 * M_PI);
	a = atan (height / x);
	a = a * 360 / M_PI;
	return a;
}

/*
=================
SCR_CalcRefdef

Must be called whenever vid changes
Internal use only
=================
*/
static void SCR_CalcRefdef (void)
{
	float size, scale; // johnfitz -- scale
	float zoom;

	// bound viewsize
	if (scr_viewsize.value < 30)
		Cvar_SetQuick (&scr_viewsize, "30");
	if (scr_viewsize.value > 130)
		Cvar_SetQuick (&scr_viewsize, "130");

	// bound fov
	if (scr_fov.value < 10)
		Cvar_SetQuick (&scr_fov, "10");
	if (scr_fov.value > 170)
		Cvar_SetQuick (&scr_fov, "170");
	if (scr_zoomfov.value < 10)
		Cvar_SetQuick (&scr_zoomfov, "10");
	if (scr_zoomfov.value > 170)
		Cvar_SetQuick (&scr_zoomfov, "170");

	vid.recalc_refdef = 0;

	// johnfitz -- rewrote this section
	size = scr_viewsize.value;
	scale = CLAMP (1.0, scr_sbarscale.value, (float)glwidth / 320.0);

	if ((size >= 120) || cl.intermission || (scr_sbaralpha.value < 1) || ((scr_style.value < 1.0f) && cl.qcvm.extfuncs.CSQC_DrawHud) ||
		(scr_style.value >= 2.0f)) // johnfitz -- scr_sbaralpha.value. Spike -- simple csqc assumes fullscreen video the same way.
		sb_lines = 0;
	else if (size >= 110)
		sb_lines = 24 * scale;
	else
		sb_lines = 48 * scale;

	size = q_min (scr_viewsize.value, 100.f) / 100;
	// johnfitz

	// johnfitz -- rewrote this section
	r_refdef.vrect.width = q_max (glwidth * size, 96);					  // no smaller than 96, for icons
	r_refdef.vrect.height = q_min (glheight * size, glheight - sb_lines); // make room for sbar
	r_refdef.vrect.x = (glwidth - r_refdef.vrect.width) / 2;
	r_refdef.vrect.y = (glheight - sb_lines - r_refdef.vrect.height) / 2;
	// johnfitz

	zoom = cl.zoom;
	zoom *= zoom * (3.f - 2.f * zoom); // smoothstep
	r_refdef.basefov = scr_fov.value + (scr_zoomfov.value - scr_fov.value) * zoom;
	r_refdef.fov_x = AdaptFovx (r_refdef.basefov, vid.width, vid.height);
	r_refdef.fov_y = CalcFovy (r_refdef.fov_x, r_refdef.vrect.width, r_refdef.vrect.height);

	scr_vrect = r_refdef.vrect;
}

/*
=================
SCR_SizeUp_f

Keybinding command
=================
*/
void SCR_SizeUp_f (void)
{
	Cvar_SetValueQuick (&scr_viewsize, scr_viewsize.value + 10);
}

/*
=================
SCR_SizeDown_f

Keybinding command
=================
*/
void SCR_SizeDown_f (void)
{
	float new_value = scr_viewsize.value - 10;
	if (!scr_viewsize_allow_shrinking.value)
		new_value = q_max (new_value, 100);
	Cvar_SetValueQuick (&scr_viewsize, new_value);
}

static void SCR_Callback_refdef (cvar_t *var)
{
	vid.recalc_refdef = 1;
}

/*
==================
SCR_Conwidth_f -- johnfitz -- called when scr_conwidth or scr_conscale changes
==================
*/
static void SCR_Conwidth_f (cvar_t *var)
{
	vid.recalc_refdef = 1;
	vid.conwidth = (scr_conwidth.value > 0) ? (int)scr_conwidth.value : (scr_conscale.value > 0) ? (int)(vid.width / scr_conscale.value) : vid.width;
	vid.conwidth = CLAMP (320, vid.conwidth, vid.width);
	vid.conwidth &= 0xFFFFFFF8;
	vid.conheight = vid.conwidth * vid.height / vid.width;
}

/*
==================
SCR_UpdateRelativeScale
==================
*/
void SCR_UpdateRelativeScale ()
{
	if (scr_relativescale.value)
	{
		float normalization_scale = 0.0013f; // To make scr_relmenuscale etc. more user friendly
		float relative_scale = CLAMP (1.0f, scr_relativescale.value, 3.0f) * normalization_scale;

		scr_menuscale.flags &= ~(CVAR_ARCHIVE | CVAR_ROM);
		Cvar_SetValue ("scr_menuscale", (float)vid.height * scr_relmenuscale.value * relative_scale);
		scr_menuscale.flags |= CVAR_ROM;

		scr_sbarscale.flags &= ~(CVAR_ARCHIVE | CVAR_ROM);
		Cvar_SetValue ("scr_sbarscale", (float)vid.height * scr_relsbarscale.value * relative_scale);
		scr_sbarscale.flags |= CVAR_ROM;

		scr_crosshairscale.flags &= ~(CVAR_ARCHIVE | CVAR_ROM);
		Cvar_SetValue ("scr_crosshairscale", (float)vid.height * scr_relcrosshairscale.value * relative_scale);
		scr_crosshairscale.flags |= CVAR_ROM;

		scr_conscale.flags &= ~(CVAR_ARCHIVE | CVAR_ROM);
		Cvar_SetValue ("scr_conscale", (float)vid.height * scr_relconscale.value * relative_scale);
		scr_conscale.flags |= CVAR_ROM;
	}
	else
	{
		scr_menuscale.flags |= CVAR_ARCHIVE;
		scr_menuscale.flags &= ~CVAR_ROM;
		scr_sbarscale.flags |= CVAR_ARCHIVE;
		scr_sbarscale.flags &= ~CVAR_ROM;
		scr_crosshairscale.flags |= CVAR_ARCHIVE;
		scr_crosshairscale.flags &= ~CVAR_ROM;
		scr_conscale.flags |= CVAR_ARCHIVE;
		scr_conscale.flags &= ~CVAR_ROM;
	}
	SCR_Conwidth_f (NULL);
}

/*
==================
SCR_UpdateRelativeScale_f
==================
*/
static void SCR_UpdateRelativeScale_f (cvar_t *var)
{
	SCR_UpdateRelativeScale ();
}

//============================================================================

/*
==================
SCR_LoadPics -- johnfitz
==================
*/
void SCR_LoadPics (void)
{
	scr_net = Draw_PicFromWad ("net");
	scr_turtle = Draw_PicFromWad ("turtle");
}

/*
==================
SCR_Init
==================
*/
void SCR_Init (void)
{
	// johnfitz -- new cvars
	Cvar_RegisterVariable (&scr_menuscale);
	Cvar_RegisterVariable (&scr_sbarscale);
	Cvar_SetCallback (&scr_sbaralpha, SCR_Callback_refdef);
	Cvar_RegisterVariable (&scr_sbaralpha);
	Cvar_SetCallback (&scr_conwidth, &SCR_Conwidth_f);
	Cvar_SetCallback (&scr_conscale, &SCR_Conwidth_f);
	Cvar_RegisterVariable (&scr_conwidth);
	Cvar_RegisterVariable (&scr_conscale);
	Cvar_RegisterVariable (&scr_crosshairscale);
	Cvar_RegisterVariable (&scr_showfps);
	Cvar_RegisterVariable (&scr_clock);
	Cvar_RegisterVariable (&scr_autoclock);
	// johnfitz
	Cvar_RegisterVariable (&scr_usekfont); // 2021 re-release
	Cvar_SetCallback (&scr_fov, SCR_Callback_refdef);
	Cvar_SetCallback (&scr_fov_adapt, SCR_Callback_refdef);
	Cvar_SetCallback (&scr_zoomfov, SCR_Callback_refdef);
	Cvar_SetCallback (&scr_viewsize, SCR_Callback_refdef);
	Cvar_SetCallback (&scr_style, SCR_Callback_refdef);
	Cvar_RegisterVariable (&scr_fov);
	Cvar_RegisterVariable (&scr_fov_adapt);
	Cvar_RegisterVariable (&scr_zoomfov);
	Cvar_RegisterVariable (&scr_zoomspeed);
	Cvar_RegisterVariable (&scr_viewsize);
	Cvar_RegisterVariable (&scr_viewsize_allow_shrinking);
	Cvar_RegisterVariable (&scr_conspeed);
	Cvar_RegisterVariable (&scr_conanim);
	Cvar_RegisterVariable (&scr_showturtle);
	Cvar_RegisterVariable (&scr_showpause);
	Cvar_RegisterVariable (&scr_centertime);
	Cvar_RegisterVariable (&scr_printspeed);
	Cvar_RegisterVariable (&scr_style);
	Cvar_RegisterVariable (&cl_gun_fovscale);

	Cvar_RegisterVariable (&scr_relativescale);
	Cvar_RegisterVariable (&scr_relmenuscale);
	Cvar_RegisterVariable (&scr_relsbarscale);
	Cvar_RegisterVariable (&scr_relcrosshairscale);
	Cvar_RegisterVariable (&scr_relconscale);
	Cvar_SetCallback (&scr_relativescale, &SCR_UpdateRelativeScale_f);
	Cvar_SetCallback (&scr_relmenuscale, &SCR_UpdateRelativeScale_f);
	Cvar_SetCallback (&scr_relsbarscale, &SCR_UpdateRelativeScale_f);
	Cvar_SetCallback (&scr_relcrosshairscale, &SCR_UpdateRelativeScale_f);
	Cvar_SetCallback (&scr_relconscale, &SCR_UpdateRelativeScale_f);
	SCR_UpdateRelativeScale ();

	if (CFG_OpenConfig ("config.cfg") == 0)
	{
		const char *early_read[] = {"scr_relativescale"};
		CFG_ReadCvars (early_read, 1);
		CFG_CloseConfig ();
	}

	Cmd_AddCommand ("screenshot", SCR_ScreenShot_f);
	Cmd_AddCommand ("sizeup", SCR_SizeUp_f);
	Cmd_AddCommand ("sizedown", SCR_SizeDown_f);

	Cmd_AddCommand ("togglezoom", SCR_ToggleZoom_f);
	Cmd_AddCommand ("+zoom", SCR_ZoomDown_f);
	Cmd_AddCommand ("-zoom", SCR_ZoomUp_f);

	SCR_LoadPics (); // johnfitz

	draw_qcvm_mutex = SDL_CreateMutex ();

	scr_initialized = true;
}

//============================================================================

/*
==============
SCR_DrawFPS -- johnfitz
==============
*/
void SCR_DrawFPS (cb_context_t *cbx)
{
	static double oldtime = 0;
	static double lastfps = 0;
	static int	  oldframecount = 0;
	double		  elapsed_time;
	int			  frames;

	elapsed_time = realtime - oldtime;
	frames = r_framecount - oldframecount;

	if (elapsed_time < 0 || frames < 0)
	{
		oldtime = realtime;
		oldframecount = r_framecount;
		return;
	}
	// update value every 3/4 second
	if (elapsed_time > 0.75)
	{
		lastfps = frames / elapsed_time;
		oldtime = realtime;
		oldframecount = r_framecount;
	}

	if (scr_showfps.value && scr_viewsize.value < 130)
	{
		char st[16];
		int	 x, y;
		q_snprintf (st, sizeof (st), "%4.0f fps", lastfps);
		x = 320 - (strlen (st) << 3);
		y = 200 - 8;
		GL_SetCanvas (cbx, CANVAS_BOTTOMRIGHT);
		Draw_String (cbx, x, y, st);
	}
}

/*
==============
SCR_DrawClock -- johnfitz
==============
*/
void SCR_DrawClock (cb_context_t *cbx)
{
	char			str[32];
	int				y = 200 - 8;
	static qboolean shown_pause;
	extern qboolean sb_showscores;

	if (cls.demoplayback && cls.demospeed != 1 && cls.demospeed != 0) // always show if playback speed is modified
	{
		scr_clock_off = 2.0f;
		shown_pause = false;
	}

	if (cls.demoplayback && cls.demospeed == 0 && !shown_pause) // show for a bit if paused
	{
		scr_clock_off = 1.5f;
		shown_pause = true;
	}

	if ((scr_clock.value == 0 && scr_clock_off <= 0 && !(sb_showscores && !fitzmode && scr_autoclock.value)) || scr_viewsize.value >= 130)
		return;

	scr_clock_off -= host_frametime / (cls.demospeed ? cls.demospeed : 1.f);

	GL_SetCanvas (cbx, CANVAS_BOTTOMRIGHT);

	if (scr_showfps.value)
		y -= 8; // make room for fps counter

	if (scr_clock.value >= 2)
	{
		q_snprintf (str, sizeof (str), "%i/%i", cl.stats[STAT_MONSTERS], cl.stats[STAT_TOTALMONSTERS]);
		Draw_String (cbx, 320 - (strlen (str) << 3), y, str);
		y -= 8;
		q_snprintf (str, sizeof (str), "%i/%i", cl.stats[STAT_SECRETS], cl.stats[STAT_TOTALSECRETS]);
		Draw_String (cbx, 320 - (strlen (str) << 3), y, str);
		y -= 8;
	}

	q_snprintf (str, sizeof (str), "%i:%02i", (int)cl.time / 60, (int)cl.time % 60);
	Draw_String (cbx, 320 - (strlen (str) << 3), y, str);

	// show playback rate
	if (cls.demoplayback && cls.demospeed != 1)
	{
		y -= 8;
		q_snprintf (str, sizeof (str), "[%gx]", cls.demospeed);
		if (cls.demospeed == 0)
			q_snprintf (str, sizeof (str), "[paused]");
		Draw_String (cbx, 320 - (strlen (str) << 3), y, str);
	}
}

/*
==============
SCR_DrawDevStats
==============
*/
void SCR_DrawDevStats (cb_context_t *cbx)
{
	char str[40];
	int	 y = 25 - 9; // 9=number of lines to print
	int	 x = 0;		 // margin

	if (!devstats.value)
		return;

	GL_SetCanvas (cbx, CANVAS_BOTTOMLEFT);

	Draw_Fill (cbx, x, y * 8, 19 * 8, 9 * 8, 0, 0.5); // dark rectangle

	q_snprintf (str, sizeof (str), "devstats |Curr Peak");
	Draw_String (cbx, x, (y++) * 8 - x, str);

	q_snprintf (str, sizeof (str), "---------+---------");
	Draw_String (cbx, x, (y++) * 8 - x, str);

	q_snprintf (str, sizeof (str), "Edicts   |%4i %4i", dev_stats.edicts, dev_peakstats.edicts);
	Draw_String (cbx, x, (y++) * 8 - x, str);

	q_snprintf (str, sizeof (str), "Packet   |%4i %4i", dev_stats.packetsize, dev_peakstats.packetsize);
	Draw_String (cbx, x, (y++) * 8 - x, str);

	q_snprintf (str, sizeof (str), "Visedicts|%4i %4i", dev_stats.visedicts, dev_peakstats.visedicts);
	Draw_String (cbx, x, (y++) * 8 - x, str);

	q_snprintf (str, sizeof (str), "Efrags   |%4i %4i", dev_stats.efrags, dev_peakstats.efrags);
	Draw_String (cbx, x, (y++) * 8 - x, str);

	q_snprintf (str, sizeof (str), "Dlights  |%4i %4i", dev_stats.dlights, dev_peakstats.dlights);
	Draw_String (cbx, x, (y++) * 8 - x, str);

	q_snprintf (str, sizeof (str), "Beams    |%4i %4i", dev_stats.beams, dev_peakstats.beams);
	Draw_String (cbx, x, (y++) * 8 - x, str);

	q_snprintf (str, sizeof (str), "Tempents |%4i %4i", dev_stats.tempents, dev_peakstats.tempents);
	Draw_String (cbx, x, (y++) * 8 - x, str);
}

/*
==============
SCR_DrawTurtle
==============
*/
void SCR_DrawTurtle (cb_context_t *cbx)
{
	static int count;

	if (!scr_showturtle.value)
		return;

	if (host_frametime < 0.1)
	{
		count = 0;
		return;
	}

	count++;
	if (count < 3)
		return;

	GL_SetCanvas (cbx, CANVAS_DEFAULT); // johnfitz

	Draw_Pic (cbx, scr_vrect.x, scr_vrect.y, scr_turtle, 1.0f, false);
}

/*
==============
SCR_DrawNet
==============
*/
void SCR_DrawNet (cb_context_t *cbx)
{
	if (realtime - cl.last_received_message < 0.3)
		return;
	if (cls.demoplayback)
		return;

	GL_SetCanvas (cbx, CANVAS_DEFAULT); // johnfitz

	Draw_Pic (cbx, scr_vrect.x + 64, scr_vrect.y, scr_net, 1.0f, false);
}

/*
==============
DrawPause
==============
*/
void SCR_DrawPause (cb_context_t *cbx)
{
	qpic_t *pic;

	if (!cl.paused)
		return;

	if (!scr_showpause.value || scr_viewsize.value >= 130) // turn off for screenshots
		return;

	if (cls.demoplayback && cls.demospeed == 0.f)
		return;

	GL_SetCanvas (cbx, CANVAS_MENU); // johnfitz

	pic = Draw_CachePic ("gfx/pause.lmp");
	Draw_Pic (cbx, (320 - pic->width) / 2, (240 - 48 - pic->height) / 2, pic, 1.0f, false); // johnfitz -- stretched menus
}

/*
==============
SCR_DrawLoading
==============
*/
void SCR_DrawLoading (cb_context_t *cbx)
{
	qpic_t *pic;

	if (!scr_drawloading)
		return;

	GL_SetCanvas (cbx, CANVAS_MENU); // johnfitz

	pic = Draw_CachePic ("gfx/loading.lmp");
	Draw_Pic (cbx, (320 - pic->width) / 2, (240 - 48 - pic->height) / 2, pic, 1.0f, false); // johnfitz -- stretched menus
}

/*
==============
SCR_DrawCrosshair -- johnfitz
==============
*/
void SCR_DrawCrosshair (cb_context_t *cbx)
{
	if (!crosshair.value || scr_viewsize.value >= 130)
		return;

	GL_SetCanvas (cbx, CANVAS_CROSSHAIR);
	if (crosshair.value >= 2.0f)
		Draw_Character (cbx, -2, -5, '.'); // 0,0 is center of viewport
	else
		Draw_Character (cbx, -4, -4, '+'); // 0,0 is center of viewport
}

//=============================================================================

/*
==================
SCR_SetUpToDrawConsole
==================
*/
void SCR_SetUpToDrawConsole (void)
{
	// johnfitz -- let's hack away the problem of slow console when host_timescale is <0
	extern cvar_t host_timescale;
	float		  timescale;
	// johnfitz

	Con_CheckResize ();

	if (scr_drawloading)
		return; // never a console with loading plaque

	if (con_forcedup)
	{
		scr_conlines = glheight; // full screen //johnfitz -- glheight instead of vid.height
		scr_con_current = scr_conlines;
	}
	else if (key_dest == key_console)
		scr_conlines = glheight / 2; // half screen //johnfitz -- glheight instead of vid.height
	else
		scr_conlines = 0; // none visible

	timescale = (host_timescale.value > 0) ? host_timescale.value : 1; // johnfitz -- timescale

	if (scr_conanim.value)
	{
		if (scr_conlines < scr_con_current)
		{
			// ericw -- (glheight/600.0) factor makes conspeed resolution independent, using 800x600 as a baseline
			scr_con_current -= scr_conspeed.value * (glheight / 600.0) * host_frametime / timescale; // johnfitz -- timescale
			if (scr_conlines > scr_con_current)
				scr_con_current = scr_conlines;
		}
		else if (scr_conlines > scr_con_current)
		{
			// ericw -- (glheight/600.0)
			scr_con_current += scr_conspeed.value * (glheight / 600.0) * host_frametime / timescale; // johnfitz -- timescale
			if (scr_conlines < scr_con_current)
				scr_con_current = scr_conlines;
		}
	}
	else
	{
		if (scr_conlines < scr_con_current)
			scr_con_current = scr_conlines;
		else if (scr_conlines > scr_con_current)
			scr_con_current = scr_conlines;
	}
}

/*
==================
SCR_DrawConsole
==================
*/
void SCR_DrawConsole (cb_context_t *cbx)
{
	if (scr_con_current)
	{
		Con_DrawConsole (cbx, scr_con_current, true);
		clearconsole = 0;
	}
	else
	{
		if (key_dest == key_game || key_dest == key_message)
			Con_DrawNotify (cbx); // only draw notify in game
	}
}

//=============================================================================

/*
===============
SCR_BeginLoadingPlaque

================
*/
void SCR_BeginLoadingPlaque (void)
{
	S_StopAllSounds (true, false);

	if (cls.state != ca_connected)
		return;
	if (cls.signon != SIGNONS)
		return;

	// redraw with no console and the loading plaque
	Con_ClearNotify ();
	SCR_CenterPrintClear ();
	scr_con_current = 0;

	scr_drawloading = true;
	SCR_UpdateScreen (false);
	scr_drawloading = false;

	scr_disabled_for_loading = true;
	scr_disabled_time = realtime;
}

/*
===============
SCR_EndLoadingPlaque

================
*/
void SCR_EndLoadingPlaque (void)
{
	scr_disabled_for_loading = false;
	Con_ClearNotify ();
}

//=============================================================================

const char *scr_notifystring;
qboolean	scr_drawdialog;

void SCR_DrawNotifyString (cb_context_t *cbx)
{
	const char *start;
	int			l;
	int			j;
	int			x, y;

	GL_SetCanvas (cbx, CANVAS_MENU); // johnfitz

	start = scr_notifystring;

	y = 200 * 0.35; // johnfitz -- stretched overlays

	do
	{
		// scan the width of the line
		for (l = 0; l < 40; l++)
			if (start[l] == '\n' || !start[l])
				break;
		x = (320 - l * 8) / 2; // johnfitz -- stretched overlays
		for (j = 0; j < l; j++, x += CHARACTER_SIZE)
			Draw_Character (cbx, x, y, start[j]);

		y += 8;

		while (*start && *start != '\n')
			start++;

		if (!*start)
			break;
		start++; // skip the \n
	} while (1);
}

/*
==================
SCR_ModalMessage

Displays a text string in the center of the screen and waits for a Y or N
keypress.
==================
*/
int SCR_ModalMessage (const char *text, float timeout) // johnfitz -- timeout
{
	double time1, time2; // johnfitz -- timeout
	int	   lastkey, lastchar;

	if (cls.state == ca_dedicated)
		return true;

	scr_notifystring = text;

	// draw a fresh screen
	scr_drawdialog = true;
	SCR_UpdateScreen (false);
	scr_drawdialog = false;

	S_ClearBuffer (); // so dma doesn't loop current sound

	time1 = Sys_DoubleTime () + timeout; // johnfitz -- timeout
	time2 = 0.0f;						 // johnfitz -- timeout

	Key_BeginInputGrab ();
	do
	{
		Sys_SendKeyEvents ();
		Key_GetGrabbedInput (&lastkey, &lastchar);
		Sys_Sleep (16);
		if (timeout)
			time2 = Sys_DoubleTime (); // johnfitz -- zero timeout means wait forever.
	} while (lastchar != 'y' && lastchar != 'Y' && lastchar != 'n' && lastchar != 'N' && lastkey != K_ESCAPE && lastkey != K_ABUTTON && lastkey != K_BBUTTON &&
			 lastkey != K_MOUSE2 && time2 <= time1);
	Key_EndInputGrab ();

	//	SCR_UpdateScreen (); //johnfitz -- commented out

	// johnfitz -- timeout
	if (time2 > time1)
		return false;
	// johnfitz

	return (lastchar == 'y' || lastchar == 'Y' || lastkey == K_ABUTTON);
}

//=============================================================================

// johnfitz -- deleted SCR_BringDownConsole

/*
==================
SCR_TileClear
johnfitz -- modified to use glwidth/glheight instead of vid.width/vid.height
		also fixed the dimentions of right and top panels
==================
*/
void SCR_TileClear (cb_context_t *cbx)
{
	if (r_refdef.vrect.x > 0 || r_refdef.vrect.y > 0)
		GL_SetCanvas (cbx, CANVAS_DEFAULT);

	if (r_refdef.vrect.x > 0)
	{
		// left
		Draw_TileClear (cbx, 0, 0, r_refdef.vrect.x, glheight - sb_lines);
		// right
		Draw_TileClear (cbx, r_refdef.vrect.x + r_refdef.vrect.width, 0, glwidth - r_refdef.vrect.x - r_refdef.vrect.width, glheight - sb_lines);
	}

	if (r_refdef.vrect.y > 0)
	{
		// top
		Draw_TileClear (cbx, r_refdef.vrect.x, 0, r_refdef.vrect.width, r_refdef.vrect.y);
		// bottom
		Draw_TileClear (
			cbx, r_refdef.vrect.x, r_refdef.vrect.y + r_refdef.vrect.height, r_refdef.vrect.width,
			glheight - r_refdef.vrect.y - r_refdef.vrect.height - sb_lines);
	}
}

/*
==================
SCR_DrawGUI
==================
*/
static void SCR_DrawGUI (void *unused)
{
	cb_context_t *cbx = vulkan_globals.secondary_cb_contexts[SCBX_GUI];

	GL_SetCanvas (cbx, CANVAS_DEFAULT);
	R_BindPipeline (cbx, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_globals.basic_blend_pipeline[cbx->render_pass_index]);

	// FIXME: only call this when needed
	R_BeginDebugUtilsLabel (cbx, "2D");
	SCR_TileClear (cbx);

	const qboolean cscqhud = (scr_style.value < 1.0f) && cl.qcvm.extfuncs.CSQC_DrawHud;
	qboolean	   use_mutex = r_showbboxes.value && cscqhud;

	if (use_mutex)
		SDL_LockMutex (draw_qcvm_mutex);

	if (cscqhud && setjmp (screen_error))
		PR_ClearProgs (&cl.qcvm);

	if (scr_drawdialog) // new game confirm
	{
		if (con_forcedup)
			Draw_ConsoleBackground (cbx);
		else
			Sbar_Draw (cbx);
		Draw_FadeScreen (cbx);
		SCR_DrawNotifyString (cbx);
	}
	else if (scr_drawloading) // loading
	{
		SCR_DrawLoading (cbx);
		Sbar_Draw (cbx);
	}
	else if (cl.intermission == 1 && key_dest == key_game) // end of level
	{
		Sbar_IntermissionOverlay (cbx);
	}
	else if (cl.intermission == 2 && key_dest == key_game) // end of episode
	{
		Sbar_FinaleOverlay (cbx);
		SCR_CheckDrawCenterString (cbx);
	}
	else
	{
		SCR_DrawCrosshair (cbx); // johnfitz
		SCR_DrawNet (cbx);
		SCR_DrawTurtle (cbx);
		SCR_DrawPause (cbx);
		SCR_CheckDrawCenterString (cbx);
		Sbar_Draw (cbx);
		SCR_DrawDevStats (cbx); // johnfitz
		SCR_DrawFPS (cbx);		// johnfitz
		SCR_DrawClock (cbx);	// johnfitz
		SCR_DrawConsole (cbx);
		M_Draw (cbx);
	}

	if (use_mutex)
		SDL_UnlockMutex (draw_qcvm_mutex);

	R_EndDebugUtilsLabel (cbx);
}

/*
==================
SCR_SetupFrame
==================
*/
static void SCR_SetupFrame (void *unused)
{
	SCR_SetUpToDrawConsole ();
	V_SetupFrame ();
}

/*
==================
SCR_DrawDone
==================
*/
static void SCR_DrawDone (void *unused)
{
	r_framecount++;
}

/*
==================
SCR_UpdateScreen

This is called every frame, and can also be called explicitly to flush
text to the screen.

WARNING: be very careful calling this from elsewhere, because the refresh
needs almost the entire 256k of stack space!
==================
*/
void SCR_UpdateScreen (qboolean use_tasks)
{
	if (!scr_initialized || !con_initialized || in_update_screen)
		return; // not initialized yet

	if (Tasks_IsWorker ())
		return; // not safe

	in_update_screen = true;
	use_tasks = use_tasks && (Tasks_NumWorkers () > 1) && r_tasks.value && r_gpulightmapupdate.value;

	if (scr_disabled_for_loading)
	{
		if (realtime - scr_disabled_time > 60)
		{
			scr_disabled_for_loading = false;
			Con_Printf ("load failed.\n");
		}
		else
		{
			in_update_screen = false;
			return;
		}
	}

	if (vid.recalc_refdef)
		SCR_CalcRefdef ();

	// decide on the height of the console
	con_forcedup = !cl.worldmodel || cls.signon != SIGNONS;

	task_handle_t begin_rendering_task = INVALID_TASK_HANDLE;
	if (!GL_BeginRendering (use_tasks, &begin_rendering_task, &glwidth, &glheight))
	{
		in_update_screen = false;
		return;
	}

	if (use_tasks)
	{
		if (prev_end_rendering_task != INVALID_TASK_HANDLE)
		{
			Task_AddDependency (prev_end_rendering_task, begin_rendering_task);
			prev_end_rendering_task = INVALID_TASK_HANDLE;
		}

		task_handle_t draw_done_task = Task_AllocateAndAssignFunc (SCR_DrawDone, NULL, 0);
		task_handle_t setup_frame_task = Task_AllocateAndAssignFunc (SCR_SetupFrame, NULL, 0);
		V_RenderView (use_tasks, begin_rendering_task, setup_frame_task, draw_done_task);
		task_handle_t draw_gui_task = Task_AllocateAndAssignFunc (SCR_DrawGUI, NULL, 0);
		task_handle_t end_rendering_task = GL_EndRendering (use_tasks, true);

		Task_AddDependency (begin_rendering_task, draw_gui_task);
		Task_AddDependency (setup_frame_task, draw_gui_task);
		Task_AddDependency (draw_gui_task, draw_done_task);
		Task_AddDependency (draw_done_task, end_rendering_task);

		task_handle_t tasks[] = {begin_rendering_task, setup_frame_task, draw_done_task, draw_gui_task, end_rendering_task};
		Tasks_Submit (sizeof (tasks) / sizeof (task_handle_t), tasks);

		while (!Task_Join (draw_done_task, 10))
			S_ExtraUpdate ();
		prev_end_rendering_task = end_rendering_task;
	}
	else
	{
		GL_SynchronizeEndRenderingTask ();
		SCR_SetupFrame (NULL);
		V_RenderView (use_tasks, INVALID_TASK_HANDLE, INVALID_TASK_HANDLE, INVALID_TASK_HANDLE);
		S_ExtraUpdate ();
		SCR_DrawGUI (NULL);
		SCR_DrawDone (NULL);
		GL_EndRendering (false, true);
	}

	in_update_screen = false;
}
