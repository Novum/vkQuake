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

#include "quakedef.h"
#include "bgmusic.h"

void (*vid_menucmdfn) (void); // johnfitz
void (*vid_menukeyfn) (int key);

enum m_state_e m_state;

static void M_Menu_SinglePlayer_f (void);
static void M_Menu_Load_f (void);
static void M_Menu_Save_f (void);
static void M_Menu_MultiPlayer_f (void);
static void M_Menu_Setup_f (void);
static void M_Menu_Net_f (void);
static void M_Menu_LanConfig_f (void);
static void M_Menu_MPGameOptions_f (void);
static void M_Menu_Search_f (enum slistScope_e scope);
static void M_Menu_ServerList_f (void);
static void M_Menu_Keys_f (void);
static void M_Menu_Help_f (void);
static void M_Menu_Mods_f (void);

static void M_Main_Draw (cb_context_t *cbx);
static void M_SinglePlayer_Draw (cb_context_t *cbx);
static void M_Load_Draw (cb_context_t *cbx);
static void M_Save_Draw (cb_context_t *cbx);
static void M_MultiPlayer_Draw (cb_context_t *cbx);
static void M_Setup_Draw (cb_context_t *cbx);
static void M_Net_Draw (cb_context_t *cbx);
static void M_LanConfig_Draw (cb_context_t *cbx);
static void M_MPGameOptions_Draw (cb_context_t *cbx);
static void M_Search_Draw (cb_context_t *cbx);
static void M_ServerList_Draw (cb_context_t *cbx);
static void M_Options_Draw (cb_context_t *cbx);
static void M_Mods_Draw (cb_context_t *cbx);
static void M_Keys_Draw (cb_context_t *cbx);
static void M_Help_Draw (cb_context_t *cbx);
static void M_Quit_Draw (cb_context_t *cbx);

static void M_Main_Key (int key);
static void M_SinglePlayer_Key (int key);
static void M_Load_Key (int key);
static void M_Save_Key (int key);
static void M_MultiPlayer_Key (int key);
static void M_Setup_Key (int key);
static void M_Net_Key (int key);
static void M_LanConfig_Key (int key);
static void M_MPGameOptions_Key (int key);
static void M_Search_Key (int key);
static void M_ServerList_Key (int key);
static void M_Options_Key (int key);
static void M_Keys_Key (int key);
static void M_Help_Key (int key);
static void M_Mods_Key (int key);
static void M_Quit_Key (int key);

qboolean m_entersound; // play after drawing a frame, so caching
					   // won't disrupt the sound
qboolean m_recursiveDraw;

qboolean m_is_quitting = false; // prevents SDL_StartTextInput during quit

enum m_state_e m_return_state;
qboolean	   m_return_onerror;
char		   m_return_reason[32];

#define StartingGame (m_multiplayer_cursor == 1)
#define JoiningGame	 (m_multiplayer_cursor == 0)
#define IPXConfig	 (m_net_cursor == 0)
#define TCPIPConfig	 (m_net_cursor == 1)

static int		m_main_cursor;
static qboolean m_mouse_moved;
static qboolean menu_changed;
static int		m_mouse_x = -1;
static int		m_mouse_y = -1;
static int		m_mouse_x_pixels = -1;
static int		m_mouse_y_pixels = -1;

static int scrollbar_x;
static int scrollbar_y;
static int scrollbar_size;

void M_ConfigureNetSubsystem (void);

extern qboolean keydown[256];

extern cvar_t scr_fov;
extern cvar_t scr_showfps;
extern cvar_t scr_style;
extern cvar_t autoload;
extern cvar_t r_rtshadows;
extern cvar_t r_particles;
extern cvar_t r_md5models;
extern cvar_t r_lerpmodels;
extern cvar_t r_lerpmove;
extern cvar_t r_lerpturn;
extern cvar_t vid_filter;
extern cvar_t vid_palettize;
extern cvar_t vid_anisotropic;
extern cvar_t vid_fsaa;
extern cvar_t vid_fsaamode;
extern cvar_t host_maxfps;
extern cvar_t snd_waterfx;
extern cvar_t cl_bob;
extern cvar_t cl_rollangle;
extern cvar_t v_gunkick;

static qboolean slider_grab;
static qboolean scrollbar_grab;

/*
================
M_GetScale
================
*/
float M_GetScale ()
{
	static float latched_menuscale;
	if (!slider_grab)
		latched_menuscale = scr_menuscale.value;
	return latched_menuscale;
}

/*
================
M_PixelToMenuCanvasCoord
================
*/
static void M_PixelToMenuCanvasCoord (int *x, int *y)
{
	float s = q_min ((float)glwidth / 320.0, (float)glheight / 200.0);
	s = CLAMP (1.0, M_GetScale (), s);
	*x = (*x - (glwidth - 320 * s) / 2) / s;
	*y = (*y - (glheight - 200 * s) / 2) / s;
}

#define MENU_OPTION_STRING (str)

/*
================
M_PrintHighlighted
================
*/
void M_PrintHighlighted (cb_context_t *cbx, int cx, int cy, const char *str)
{
	while (*str)
	{
		Draw_Character (cbx, cx, cy, (*str));
		str++;
		cx += CHARACTER_SIZE;
	}
}

/*
================
M_Print
================
*/
void M_Print (cb_context_t *cbx, int cx, int cy, const char *str)
{
	while (*str)
	{
		Draw_Character (cbx, cx, cy, (*str) + 128);
		str++;
		cx += CHARACTER_SIZE;
	}
}

/*
================
M_PrintElided
================
*/
void M_PrintElided (cb_context_t *cbx, int cx, int cy, const char *str, const int max_length)
{
	int i = 0;
	while (str[i] && i < max_length)
	{
		Draw_Character (cbx, cx, cy, str[i] + 128);
		++i;
		cx += CHARACTER_SIZE;
	}
	if (str[i] != 0)
	{
		for (int j = 0; j < 3; ++j)
		{
			Draw_Character (cbx, cx, cy, '.' + 128);
			cx += CHARACTER_SIZE / 2;
		}
	}
}

/*
================
M_PrintWhite
================
*/
void M_PrintWhite (cb_context_t *cbx, int cx, int cy, const char *str)
{
	while (*str)
	{
		Draw_Character (cbx, cx, cy, *str);
		str++;
		cx += CHARACTER_SIZE;
	}
}

/*
================
M_DrawTransPic
================
*/
void M_DrawTransPic (cb_context_t *cbx, int x, int y, qpic_t *pic)
{
	Draw_Pic (cbx, x, y, pic, 1.0f, false); // johnfitz -- simplified becuase centering is handled elsewhere
}

/*
================
M_DrawPic
================
*/
void M_DrawPic (cb_context_t *cbx, int x, int y, qpic_t *pic)
{
	Draw_Pic (cbx, x, y, pic, 1.0f, false); // johnfitz -- simplified becuase centering is handled elsewhere
}

/*
================
M_DrawTransPicTranslate
================
*/
static void M_DrawTransPicTranslate (cb_context_t *cbx, int x, int y, qpic_t *pic, int top, int bottom) // johnfitz -- more parameters
{
	Draw_TransPicTranslate (cbx, x, y, pic, top, bottom); // johnfitz -- simplified becuase centering is handled elsewhere
}

/*
================
M_DrawTextBox
================
*/
static void M_DrawTextBox (cb_context_t *cbx, int x, int y, int width, int lines)
{
	qpic_t *p;
	int		cx, cy;
	int		n;

	// draw left side
	cx = x;
	cy = y;
	p = Draw_CachePic ("gfx/box_tl.lmp");
	M_DrawTransPic (cbx, cx, cy, p);
	p = Draw_CachePic ("gfx/box_ml.lmp");
	for (n = 0; n < lines; n++)
	{
		cy += 8;
		M_DrawTransPic (cbx, cx, cy, p);
	}
	p = Draw_CachePic ("gfx/box_bl.lmp");
	M_DrawTransPic (cbx, cx, cy + 8, p);

	// draw middle
	cx += 8;
	while (width > 0)
	{
		cy = y;
		p = Draw_CachePic ("gfx/box_tm.lmp");
		M_DrawTransPic (cbx, cx, cy, p);
		p = Draw_CachePic ("gfx/box_mm.lmp");
		for (n = 0; n < lines; n++)
		{
			cy += 8;
			if (n == 1)
				p = Draw_CachePic ("gfx/box_mm2.lmp");
			M_DrawTransPic (cbx, cx, cy, p);
		}
		p = Draw_CachePic ("gfx/box_bm.lmp");
		M_DrawTransPic (cbx, cx, cy + 8, p);
		width -= 2;
		cx += 16;
	}

	// draw right side
	cy = y;
	p = Draw_CachePic ("gfx/box_tr.lmp");
	M_DrawTransPic (cbx, cx, cy, p);
	p = Draw_CachePic ("gfx/box_mr.lmp");
	for (n = 0; n < lines; n++)
	{
		cy += 8;
		M_DrawTransPic (cbx, cx, cy, p);
	}
	p = Draw_CachePic ("gfx/box_br.lmp");
	M_DrawTransPic (cbx, cx, cy + 8, p);
}

/*
================
M_MenuChanged
================
*/
void M_MenuChanged ()
{
	m_entersound = true;
	menu_changed = true;
}

#define SLIDER_SIZE	  10
#define SLIDER_EXTENT ((SLIDER_SIZE - 1) * 8)
#define SLIDER_START  (MENU_SLIDER_X + 4)
#define SLIDER_END	  (SLIDER_START + SLIDER_EXTENT)

/*
================
M_DrawSlider
================
*/
void M_DrawSlider (cb_context_t *cbx, int x, int y, float value)
{
	value = CLAMP (0.0f, value, 1.0f);
	Draw_Character (cbx, x - 8, y, 128);
	for (int i = 0; i < SLIDER_SIZE; i++)
		Draw_Character (cbx, x + i * 8, y, 129);
	Draw_Character (cbx, x + SLIDER_SIZE * 8, y, 130);
	Draw_Character (cbx, x + (SLIDER_SIZE - 1) * 8 * value, y, 131);
}

/*
================
M_GetSliderPos
================
*/
float M_GetSliderPos (float low, float high, float current, qboolean backward, qboolean mouse, float clamped_mouse, int dir, float step, float snap_start)
{
	float f;

	if (mouse)
	{
		if (backward)
			f = high + (low - high) * (clamped_mouse - SLIDER_START) / SLIDER_EXTENT;
		else
			f = low + (high - low) * (clamped_mouse - SLIDER_START) / SLIDER_EXTENT;
	}
	else
	{
		if (backward)
			f = current - dir * step;
		else
			f = current + dir * step;
	}
	if (!mouse || f > snap_start)
		f = (int)(f / step + 0.5f) * step;
	if (f < low)
		f = low;
	else if (f > high)
		f = high;

	return f;
}

/*
================
M_DrawScrollbar
================
*/
void M_DrawScrollbar (cb_context_t *cbx, int x, int y, float value, float size)
{
	scrollbar_x = x;
	scrollbar_y = y - 8;
	scrollbar_size = (size + 2) * CHARACTER_SIZE;
	value = CLAMP (0.0f, value, 1.0f);
	Draw_Character (cbx, x, y - CHARACTER_SIZE, 128 + 256);
	for (int i = 0; i < size; i++)
		Draw_Character (cbx, x, y + i * CHARACTER_SIZE, 129 + 256);
	Draw_Character (cbx, x, y + size * CHARACTER_SIZE, 130 + 256);
	Draw_Character (cbx, x, y + (size - 1) * CHARACTER_SIZE * value, 131 + 256);
}

/*
================
M_DrawCheckbox
================
*/
void M_DrawCheckbox (cb_context_t *cbx, int x, int y, int on)
{
	if (on)
		M_Print (cbx, x, y, "on");
	else
		M_Print (cbx, x, y, "off");
}

//=============================================================================

int m_save_demonum;

/*
================
M_ToggleMenu_f
================
*/
void M_ToggleMenu_f (void)
{
	M_MenuChanged ();

	if (key_dest == key_menu)
	{
		if (m_state != m_main)
		{
			M_Menu_Main_f ();
			return;
		}

		IN_Activate ();
		key_dest = key_game;
		m_state = m_none;
		return;
	}
	if (key_dest == key_console)
	{
		Con_ToggleConsole_f ();
	}
	else
	{
		M_Menu_Main_f ();
	}
}

/*
================
M_InScrollbar
================
*/
static qboolean M_InScrollbar ()
{
	return scrollbar_grab || (scrollbar_size && (m_mouse_x >= scrollbar_x) && (m_mouse_x <= (scrollbar_x + 8)) && (m_mouse_y >= scrollbar_y) &&
							  (m_mouse_y <= (scrollbar_y + scrollbar_size)) && (m_mouse_y <= (scrollbar_y + scrollbar_size)));
}

/*
================
M_HandleScrollBarKeys
================
*/
qboolean M_HandleScrollBarKeys (const int key, int *cursor, int *first_drawn, const int num_total, const int max_on_screen)
{
	const int prev_cursor = *cursor;
	qboolean  handled_mouse = false;

	if (num_total == 0)
	{
		*cursor = 0;
		*first_drawn = 0;
		return false;
	}

	switch (key)
	{
	case K_MOUSE1:
		if (M_InScrollbar () && ((num_total - max_on_screen) > 0) && !slider_grab)
		{
			handled_mouse = true;
			scrollbar_grab = true;
			int clamped_mouse = CLAMP (scrollbar_y + 8, m_mouse_y, scrollbar_y + scrollbar_size - 8);
			*first_drawn = ((float)clamped_mouse - scrollbar_y - 8) / (scrollbar_size - 16) * (num_total - max_on_screen) + 0.5f;
			if (*cursor < *first_drawn)
				*cursor = *first_drawn;
			else if (*cursor >= *first_drawn + max_on_screen)
				*cursor = *first_drawn + max_on_screen - 1;
		}
		break;

	case K_HOME:
		*cursor = 0;
		*first_drawn = 0;
		break;

	case K_END:
		*cursor = num_total - 1;
		*first_drawn = num_total - max_on_screen;
		break;

	case K_PGUP:
		*cursor = q_max (0, *cursor - max_on_screen);
		*first_drawn = q_max (0, *first_drawn - max_on_screen);
		break;

	case K_PGDN:
		*cursor = q_min (num_total - 1, *cursor + max_on_screen);
		*first_drawn = q_min (*first_drawn + max_on_screen, num_total - max_on_screen);
		break;

	case K_UPARROW:
		if (*cursor == 0)
			*cursor = num_total - 1;
		else
			--*cursor;
		break;

	case K_DOWNARROW:
		if (*cursor == num_total - 1)
			*cursor = 0;
		else
			++*cursor;
		break;

	case K_MWHEELUP:
		*first_drawn = q_max (0, *first_drawn - 1);
		*cursor = q_min (*cursor, *first_drawn + max_on_screen - 1);
		break;

	case K_MWHEELDOWN:
		*first_drawn = q_min (*first_drawn + 1, num_total - max_on_screen);
		*cursor = q_max (*cursor, *first_drawn);
		break;
	}

	if (*cursor != prev_cursor)
		S_LocalSound ("misc/menu1.wav");

	if (num_total <= max_on_screen)
		*first_drawn = 0;
	else
		*first_drawn = CLAMP (*cursor - max_on_screen + 1, *first_drawn, *cursor);

	return handled_mouse;
}

/*
================
M_UpdateCursorForList
================
*/
void M_Mouse_UpdateListCursor (int *cursor, int left, int right, int top, int item_height, int num_items, int scroll_offset)
{
	if (!scrollbar_grab && !slider_grab && m_mouse_moved && (num_items > 0) && (m_mouse_x >= left) && (m_mouse_x <= right) && (m_mouse_y >= top) &&
		(m_mouse_y <= (top + item_height * num_items)))
		*cursor = scroll_offset + CLAMP (0, (m_mouse_y - top) / item_height, num_items - 1);
}

/*
================
M_Mouse_UpdateCursor
================
*/
void M_Mouse_UpdateCursor (int *cursor, int left, int right, int top, int item_height, int index)
{
	if (m_mouse_moved && (m_mouse_x >= left) && (m_mouse_x <= right) && (m_mouse_y >= top) && (m_mouse_y <= (top + item_height)))
		*cursor = index;
}

//=============================================================================
/* MAIN MENU */

#define MAIN_ITEMS 5

void M_Menu_Main_f (void)
{
	M_MenuChanged ();
	if (key_dest != key_menu)
	{
		m_save_demonum = cls.demonum;
		cls.demonum = -1;
	}
	IN_Deactivate (true);
	key_dest = key_menu;
	m_state = m_main;
}

static qpic_t *Get_Menu2 ()
{
	qboolean base_game = COM_GetGameNames (false)[0] == 0;
	// Check if user has actually installed vkquake.pak, otherwise fall back to old menu
	return (base_game && registered.value) ? Draw_TryCachePic ("gfx/mainmenu2.lmp", TEXPREF_ALPHA | TEXPREF_PAD | TEXPREF_NOPICMIP) : NULL;
}

void M_Main_Draw (cb_context_t *cbx)
{
	int		f;
	qpic_t *p;
	qpic_t *menu2 = Get_Menu2 ();
	int		main_items = MAIN_ITEMS + (menu2 ? 1 : 0);

	M_DrawTransPic (cbx, 16, 4, Draw_CachePic ("gfx/qplaque.lmp"));
	p = Draw_CachePic ("gfx/ttl_main.lmp");
	M_DrawPic (cbx, (320 - p->width) / 2, 4, p);

	M_DrawTransPic (cbx, 72, 32, menu2 ? menu2 : Draw_CachePic ("gfx/mainmenu.lmp"));

	f = (int)(realtime * 10) % 6;

	M_Mouse_UpdateListCursor (&m_main_cursor, 70, 320, 32, 20, main_items, 0);
	M_DrawTransPic (cbx, 54, 32 + m_main_cursor * 20, Draw_CachePic (va ("gfx/menudot%i.lmp", f + 1)));
}

void M_Main_Key (int key)
{
	qpic_t *menu2 = Get_Menu2 ();

	switch (key)
	{
	case K_MOUSE2:
	case K_ESCAPE:
	case K_BBUTTON:
		IN_Activate ();
		key_dest = key_game;
		m_state = m_none;
		cls.demonum = m_save_demonum;
		if (!fitzmode && !cl_startdemos.value) /* QuakeSpasm customization: */
			break;
		if (cls.demonum != -1 && !cls.demoplayback && cls.state != ca_connected)
			CL_NextDemo ();
		break;

	case K_DOWNARROW:
		S_LocalSound ("misc/menu1.wav");
		if (++m_main_cursor >= (MAIN_ITEMS + (menu2 ? 1 : 0)))
			m_main_cursor = 0;
		break;

	case K_UPARROW:
		S_LocalSound ("misc/menu1.wav");
		if (--m_main_cursor < 0)
			m_main_cursor = (MAIN_ITEMS + (menu2 ? 1 : 0)) - 1;
		break;

	case K_ENTER:
	case K_KP_ENTER:
	case K_ABUTTON:
	case K_MOUSE1:
		switch (m_main_cursor)
		{
		case 0:
			M_Menu_SinglePlayer_f ();
			break;

		case 1:
			M_Menu_MultiPlayer_f ();
			break;

		case 2:
			M_Menu_Options_f ();
			break;

		case 3:
			M_Menu_Help_f ();
			break;

		case 4:
			if (menu2)
				M_Menu_Mods_f ();
			else
				M_Menu_Quit_f ();
			break;
		case 5:
			M_Menu_Quit_f ();
			break;
		}
	}
}

//=============================================================================
/* SINGLE PLAYER MENU */

int m_singleplayer_cursor;
#define SINGLEPLAYER_ITEMS 3

static void M_Menu_SinglePlayer_f (void)
{
	M_MenuChanged ();
	IN_Deactivate (true);
	key_dest = key_menu;
	m_state = m_singleplayer;
}

static void M_SinglePlayer_Draw (cb_context_t *cbx)
{
	int		f;
	qpic_t *p;

	M_DrawTransPic (cbx, 16, 4, Draw_CachePic ("gfx/qplaque.lmp"));
	p = Draw_CachePic ("gfx/ttl_sgl.lmp");
	M_DrawPic (cbx, (320 - p->width) / 2, 4, p);
	M_DrawTransPic (cbx, 72, 32, Draw_CachePic ("gfx/sp_menu.lmp"));

	f = (int)(realtime * 10) % 6;

	M_Mouse_UpdateListCursor (&m_singleplayer_cursor, 70, 320, 32, 20, SINGLEPLAYER_ITEMS, 0);
	M_DrawTransPic (cbx, 54, 32 + m_singleplayer_cursor * 20, Draw_CachePic (va ("gfx/menudot%i.lmp", f + 1)));
}

static void M_SinglePlayer_Key (int key)
{
	switch (key)
	{
	case K_MOUSE2:
	case K_ESCAPE:
	case K_BBUTTON:
		M_Menu_Main_f ();
		break;

	case K_DOWNARROW:
		S_LocalSound ("misc/menu1.wav");
		if (++m_singleplayer_cursor >= SINGLEPLAYER_ITEMS)
			m_singleplayer_cursor = 0;
		break;

	case K_UPARROW:
		S_LocalSound ("misc/menu1.wav");
		if (--m_singleplayer_cursor < 0)
			m_singleplayer_cursor = SINGLEPLAYER_ITEMS - 1;
		break;

	case K_MOUSE1:
	case K_ENTER:
	case K_KP_ENTER:
	case K_ABUTTON:
		m_entersound = true;

		switch (m_singleplayer_cursor)
		{
		case 0:
			if (sv.active)
				if (!SCR_ModalMessage ("Are you sure you want to\nstart a new game? (y/n)\n", 0.0f))
					break;
			IN_Activate ();
			key_dest = key_game;
			if (sv.active)
				Cbuf_AddText ("disconnect\n");
			Cbuf_AddText ("maxplayers 1\n");
			Cbuf_AddText ("deathmatch 0\n"); // johnfitz
			Cbuf_AddText ("coop 0\n");		 // johnfitz
			Cbuf_AddText ("map start\n");
			break;

		case 1:
			M_Menu_Load_f ();
			break;

		case 2:
			M_Menu_Save_f ();
			break;
		}
	}
}

//=============================================================================
/* LOAD/SAVE MENU */

int load_cursor; // 0 < load_cursor < MAX_SAVEGAMES

#define MAX_SAVEGAMES 20 /* johnfitz -- increased from 12 */
char m_filenames[MAX_SAVEGAMES][SAVEGAME_COMMENT_LENGTH + 1];
int	 loadable[MAX_SAVEGAMES];

static void M_ScanSaves (void)
{
	int	  i, j, k;
	char  name[MAX_OSPATH];
	FILE *f;
	int	  version;
	char *save_path = multiuser ? SDL_GetPrefPath ("vkQuake", COM_GetGameNames (true)) : NULL;

	for (i = 0; i < MAX_SAVEGAMES; i++)
	{
		strcpy (m_filenames[i], "--- UNUSED SLOT ---");
		loadable[i] = false;
		for (j = (multiuser ? 0 : 1); j < 2; ++j)
		{
			if (j == 0)
				q_snprintf (name, sizeof (name), "%ss%i.sav", save_path, i);
			else
				q_snprintf (name, sizeof (name), "%s/s%i.sav", com_gamedir, i);
			f = fopen (name, "r");
			if (!f)
				continue;
			if (fscanf (f, "%i\n", &version) != 1)
				continue;
			if (fscanf (f, "%79s\n", name) != 1)
				continue;
			q_strlcpy (m_filenames[i], name, SAVEGAME_COMMENT_LENGTH + 1);

			// change _ back to space
			for (k = 0; k < SAVEGAME_COMMENT_LENGTH; k++)
			{
				if (m_filenames[i][k] == '_')
					m_filenames[i][k] = ' ';
			}
			loadable[i] = true;
			fclose (f);
			break;
		}
	}

	SDL_free (save_path);
}

static void M_Menu_Load_f (void)
{
	M_MenuChanged ();
	m_state = m_load;

	IN_Deactivate (true);
	key_dest = key_menu;
	M_ScanSaves ();
}

static void M_Menu_Save_f (void)
{
	if (!sv.active)
		return;
	if (cl.intermission)
		return;
	if (svs.maxclients != 1)
		return;
	M_MenuChanged ();
	m_state = m_save;

	IN_Deactivate (true);
	key_dest = key_menu;
	M_ScanSaves ();
}

static void M_Load_Draw (cb_context_t *cbx)
{
	int		i;
	qpic_t *p;

	p = Draw_CachePic ("gfx/p_load.lmp");
	M_DrawPic (cbx, (320 - p->width) / 2, 4, p);

	for (i = 0; i < MAX_SAVEGAMES; i++)
		M_Print (cbx, 16, 32 + 8 * i, m_filenames[i]);

	// line cursor
	M_Mouse_UpdateListCursor (&load_cursor, 16, 320, 32, 8, MAX_SAVEGAMES, 0);
	Draw_Character (cbx, 8, 32 + load_cursor * 8, 12 + ((int)(realtime * 4) & 1));
}

static void M_Save_Draw (cb_context_t *cbx)
{
	int		i;
	qpic_t *p;

	p = Draw_CachePic ("gfx/p_save.lmp");
	M_DrawPic (cbx, (320 - p->width) / 2, 4, p);

	for (i = 0; i < MAX_SAVEGAMES; i++)
		M_Print (cbx, 16, 32 + 8 * i, m_filenames[i]);

	// line cursor
	M_Mouse_UpdateListCursor (&load_cursor, 16, 320, 32, 8, MAX_SAVEGAMES, 0);
	Draw_Character (cbx, 8, 32 + load_cursor * 8, 12 + ((int)(realtime * 4) & 1));
}

static void M_Load_Key (int k)
{
	switch (k)
	{
	case K_MOUSE2:
	case K_ESCAPE:
	case K_BBUTTON:
		M_Menu_SinglePlayer_f ();
		break;

	case K_MOUSE1:
	case K_ENTER:
	case K_KP_ENTER:
	case K_ABUTTON:
		S_LocalSound ("misc/menu2.wav");
		if (!loadable[load_cursor])
			return;
		m_state = m_none;
		IN_Activate ();
		key_dest = key_game;

		// Host_Loadgame_f can't bring up the loading plaque because too much
		// stack space has been used, so do it now
		SCR_BeginLoadingPlaque ();

		// issue the load command
		Cbuf_AddText (va ("load s%i\n", load_cursor));
		return;

	case K_UPARROW:
	case K_LEFTARROW:
		S_LocalSound ("misc/menu1.wav");
		load_cursor--;
		if (load_cursor < 0)
			load_cursor = MAX_SAVEGAMES - 1;
		break;

	case K_DOWNARROW:
	case K_RIGHTARROW:
		S_LocalSound ("misc/menu1.wav");
		load_cursor++;
		if (load_cursor >= MAX_SAVEGAMES)
			load_cursor = 0;
		break;
	}
}

static void M_Save_Key (int k)
{
	switch (k)
	{
	case K_MOUSE2:
	case K_ESCAPE:
	case K_BBUTTON:
		M_Menu_SinglePlayer_f ();
		break;

	case K_MOUSE1:
	case K_ENTER:
	case K_KP_ENTER:
	case K_ABUTTON:
		m_state = m_none;
		IN_Activate ();
		key_dest = key_game;
		Cbuf_AddText (va ("save s%i\n", load_cursor));
		return;

	case K_UPARROW:
	case K_LEFTARROW:
		S_LocalSound ("misc/menu1.wav");
		load_cursor--;
		if (load_cursor < 0)
			load_cursor = MAX_SAVEGAMES - 1;
		break;

	case K_DOWNARROW:
	case K_RIGHTARROW:
		S_LocalSound ("misc/menu1.wav");
		load_cursor++;
		if (load_cursor >= MAX_SAVEGAMES)
			load_cursor = 0;
		break;
	}
}

//=============================================================================
/* MULTIPLAYER MENU */

int m_multiplayer_cursor;
#define MULTIPLAYER_ITEMS 3

static void M_Menu_MultiPlayer_f (void)
{
	IN_Deactivate (true);
	key_dest = key_menu;
	m_state = m_multiplayer;
	m_entersound = true;
}

static void M_MultiPlayer_Draw (cb_context_t *cbx)
{
	int		f;
	qpic_t *p;

	M_DrawTransPic (cbx, 16, 4, Draw_CachePic ("gfx/qplaque.lmp"));
	p = Draw_CachePic ("gfx/p_multi.lmp");
	M_DrawPic (cbx, (320 - p->width) / 2, 4, p);
	M_DrawTransPic (cbx, 72, 32, Draw_CachePic ("gfx/mp_menu.lmp"));

	f = (int)(realtime * 10) % 6;

	M_Mouse_UpdateListCursor (&m_multiplayer_cursor, 70, 320, 32, 20, MULTIPLAYER_ITEMS, 0);
	M_DrawTransPic (cbx, 54, 32 + m_multiplayer_cursor * 20, Draw_CachePic (va ("gfx/menudot%i.lmp", f + 1)));

	if (ipxAvailable || ipv4Available || ipv6Available)
		return;
	M_PrintWhite (cbx, (320 / 2) - ((27 * 8) / 2), 148, "No Communications Available");
}

static void M_MultiPlayer_Key (int key)
{
	switch (key)
	{
	case K_MOUSE2:
	case K_ESCAPE:
	case K_BBUTTON:
		M_Menu_Main_f ();
		break;

	case K_DOWNARROW:
		S_LocalSound ("misc/menu1.wav");
		if (++m_multiplayer_cursor >= MULTIPLAYER_ITEMS)
			m_multiplayer_cursor = 0;
		break;

	case K_UPARROW:
		S_LocalSound ("misc/menu1.wav");
		if (--m_multiplayer_cursor < 0)
			m_multiplayer_cursor = MULTIPLAYER_ITEMS - 1;
		break;

	case K_MOUSE1:
	case K_ENTER:
	case K_KP_ENTER:
	case K_ABUTTON:
		m_entersound = true;
		switch (m_multiplayer_cursor)
		{
		case 0:
			if (ipxAvailable || ipv4Available || ipv6Available)
				M_Menu_Net_f ();
			break;

		case 1:
			if (ipxAvailable || ipv4Available || ipv6Available)
				M_Menu_Net_f ();
			break;

		case 2:
			M_Menu_Setup_f ();
			break;
		}
	}
}

//=============================================================================
/* SETUP MENU */

int setup_cursor = 4;
int setup_cursor_table[] = {40, 56, 80, 104, 140};

char setup_hostname[16];
char setup_myname[16];
int	 setup_oldtop;
int	 setup_oldbottom;
int	 setup_top;
int	 setup_bottom;

#define NUM_SETUP_CMDS 5

static void M_Menu_Setup_f (void)
{
	IN_Deactivate (true);
	key_dest = key_menu;
	m_state = m_setup;
	m_entersound = true;
	strcpy (setup_myname, cl_name.string);
	strcpy (setup_hostname, hostname.string);
	setup_top = setup_oldtop = ((int)cl_color.value) >> 4;
	setup_bottom = setup_oldbottom = ((int)cl_color.value) & 15;
}

static void M_Setup_Draw (cb_context_t *cbx)
{
	qpic_t *p;

	M_DrawTransPic (cbx, 16, 4, Draw_CachePic ("gfx/qplaque.lmp"));
	p = Draw_CachePic ("gfx/p_multi.lmp");
	M_DrawPic (cbx, (320 - p->width) / 2, 4, p);

	M_Print (cbx, 64, 40, "Hostname");
	M_DrawTextBox (cbx, 160, 32, 16, 1);
	M_Print (cbx, 168, 40, setup_hostname);

	M_Print (cbx, 64, 56, "Your name");
	M_DrawTextBox (cbx, 160, 48, 16, 1);
	M_Print (cbx, 168, 56, setup_myname);

	M_Print (cbx, 64, 80, "Shirt color");
	M_Print (cbx, 64, 104, "Pants color");

	M_DrawTextBox (cbx, 64, 140 - 8, 14, 1);
	M_Print (cbx, 72, 140, "Accept Changes");

	p = Draw_CachePic ("gfx/bigbox.lmp");
	M_DrawTransPic (cbx, 160, 64, p);
	p = Draw_CachePic ("gfx/menuplyr.lmp");
	M_DrawTransPicTranslate (cbx, 172, 72, p, setup_top, setup_bottom);

	for (int i = 0; i < 5; ++i)
		M_Mouse_UpdateCursor (&setup_cursor, 0, 400, setup_cursor_table[i], 8, i);
	Draw_Character (cbx, 56, setup_cursor_table[setup_cursor], 12 + ((int)(realtime * 4) & 1));

	if (setup_cursor == 0)
		Draw_Character (cbx, 168 + 8 * strlen (setup_hostname), setup_cursor_table[setup_cursor], 10 + ((int)(realtime * 4) & 1));

	if (setup_cursor == 1)
		Draw_Character (cbx, 168 + 8 * strlen (setup_myname), setup_cursor_table[setup_cursor], 10 + ((int)(realtime * 4) & 1));
}

static void M_Setup_Key (int k)
{
	switch (k)
	{
	case K_MOUSE2:
	case K_ESCAPE:
	case K_BBUTTON:
		M_Menu_MultiPlayer_f ();
		break;

	case K_UPARROW:
		S_LocalSound ("misc/menu1.wav");
		setup_cursor--;
		if (setup_cursor < 0)
			setup_cursor = NUM_SETUP_CMDS - 1;
		break;

	case K_DOWNARROW:
		S_LocalSound ("misc/menu1.wav");
		setup_cursor++;
		if (setup_cursor >= NUM_SETUP_CMDS)
			setup_cursor = 0;
		break;

	case K_LEFTARROW:
		if (setup_cursor < 2)
			return;
		S_LocalSound ("misc/menu3.wav");
		if (setup_cursor == 2)
			setup_top = setup_top - 1;
		if (setup_cursor == 3)
			setup_bottom = setup_bottom - 1;
		break;
	case K_RIGHTARROW:
		if (setup_cursor < 2)
			return;
	forward:
		S_LocalSound ("misc/menu3.wav");
		if (setup_cursor == 2)
			setup_top = setup_top + 1;
		if (setup_cursor == 3)
			setup_bottom = setup_bottom + 1;
		break;

	case K_MOUSE1:
	case K_ENTER:
	case K_KP_ENTER:
	case K_ABUTTON:
		if (setup_cursor == 0 || setup_cursor == 1)
			return;

		if (setup_cursor == 2 || setup_cursor == 3)
			goto forward;

		// setup_cursor == 4 (OK)
		if (strcmp (cl_name.string, setup_myname) != 0)
			Cbuf_AddText (va ("name \"%s\"\n", setup_myname));
		if (strcmp (hostname.string, setup_hostname) != 0)
			Cvar_Set ("hostname", setup_hostname);
		if (setup_top != setup_oldtop || setup_bottom != setup_oldbottom)
			Cbuf_AddText (va ("color %i %i\n", setup_top, setup_bottom));
		m_entersound = true;
		M_Menu_MultiPlayer_f ();
		break;

	case K_BACKSPACE:
		if (setup_cursor == 0)
		{
			if (strlen (setup_hostname))
				setup_hostname[strlen (setup_hostname) - 1] = 0;
		}

		if (setup_cursor == 1)
		{
			if (strlen (setup_myname))
				setup_myname[strlen (setup_myname) - 1] = 0;
		}
		break;
	}

	if (setup_top > 13)
		setup_top = 0;
	if (setup_top < 0)
		setup_top = 13;
	if (setup_bottom > 13)
		setup_bottom = 0;
	if (setup_bottom < 0)
		setup_bottom = 13;
}

static void M_Setup_Char (int k)
{
	int l;

	switch (setup_cursor)
	{
	case 0:
		l = strlen (setup_hostname);
		if (l < 15)
		{
			setup_hostname[l + 1] = 0;
			setup_hostname[l] = k;
		}
		break;
	case 1:
		l = strlen (setup_myname);
		if (l < 15)
		{
			setup_myname[l + 1] = 0;
			setup_myname[l] = k;
		}
		break;
	}
}

qboolean M_Setup_TextEntry (void)
{
	return (setup_cursor == 0 || setup_cursor == 1);
}

//=============================================================================
/* NET MENU */

int m_net_cursor;
int m_first_net_item;
int m_net_items;

static const char *net_helpMessage[] = {
	/* .........1.........2.... */
	" Novell network LANs    ", " or Windows 95 DOS-box. ", "                        ", "(LAN=Local Area Network)",

	" Commonly used to play  ", " over the Internet, but ", " also used on a Local   ", " Area Network.          "};

static void M_Menu_Net_f (void)
{
	M_MenuChanged ();
	IN_Deactivate (true);
	key_dest = key_menu;
	m_state = m_net;

	m_net_items = 2;
	m_first_net_item = 0;
	if (!ipxAvailable)
	{
		m_net_items -= 1;
		m_first_net_item += 1;
	}
	if (!ipv4Available && !ipv6Available)
		m_net_items -= 1;

	m_net_cursor = CLAMP (m_first_net_item, m_net_cursor, m_first_net_item + m_net_items);
}

static void M_Net_Draw (cb_context_t *cbx)
{
	int		f;
	qpic_t *p;

	M_DrawTransPic (cbx, 16, 4, Draw_CachePic ("gfx/qplaque.lmp"));
	p = Draw_CachePic ("gfx/p_multi.lmp");
	M_DrawPic (cbx, (320 - p->width) / 2, 4, p);

	f = 32;

	if (ipxAvailable)
		p = Draw_CachePic ("gfx/netmen3.lmp");
	else
		p = Draw_CachePic ("gfx/dim_ipx.lmp");
	M_DrawTransPic (cbx, 72, f, p);

	f += 19;
	if (ipv4Available || ipv6Available)
		p = Draw_CachePic ("gfx/netmen4.lmp");
	else
		p = Draw_CachePic ("gfx/dim_tcp.lmp");
	M_DrawTransPic (cbx, 72, f, p);

	f = (320 - 26 * 8) / 2;
	M_DrawTextBox (cbx, f, 96, 24, 4);
	f += 8;
	M_Print (cbx, f, 104, net_helpMessage[m_net_cursor * 4 + 0]);
	M_Print (cbx, f, 112, net_helpMessage[m_net_cursor * 4 + 1]);
	M_Print (cbx, f, 120, net_helpMessage[m_net_cursor * 4 + 2]);
	M_Print (cbx, f, 128, net_helpMessage[m_net_cursor * 4 + 3]);

	f = (int)(realtime * 10) % 6;
	M_Mouse_UpdateListCursor (&m_net_cursor, 70, 320, 32, 20, m_net_items, m_first_net_item);
	M_DrawTransPic (cbx, 54, 32 + m_net_cursor * 20, Draw_CachePic (va ("gfx/menudot%i.lmp", f + 1)));
}

static void M_Net_Key (int k)
{
	switch (k)
	{
	case K_MOUSE2:
	case K_ESCAPE:
	case K_BBUTTON:
		M_Menu_MultiPlayer_f ();
		break;

	case K_DOWNARROW:
		S_LocalSound ("misc/menu1.wav");
		if (++m_net_cursor >= m_net_items)
			m_net_cursor = 0;
		break;

	case K_UPARROW:
		S_LocalSound ("misc/menu1.wav");
		if (--m_net_cursor < m_first_net_item)
			m_net_cursor = m_net_items - 1;
		break;

	case K_MOUSE1:
	case K_ENTER:
	case K_KP_ENTER:
	case K_ABUTTON:
		m_entersound = true;
		M_Menu_LanConfig_f ();
		break;
	}
}

//=============================================================================
/* GAME OPTIONS MENU */

enum
{
	GAME_OPT_SCALE,
	GAME_OPT_SBALPHA,
	GAME_OPT_MOUSESPEED,
	GAME_OPT_VIEWBOB,
	GAME_OPT_VIEWROLL,
	GAME_OPT_GUNKICK,
	GAME_OPT_SHOWGUN,
	GAME_OPT_ALWAYRUN,
	GAME_OPT_INVMOUSE,
	GAME_OPT_HUD_DETAIL,
	GAME_OPT_HUD_STYLE,
	GAME_OPT_CROSSHAIR,
	GAME_OPT_AUTOLOAD,
	GAME_OPT_STARTUP_DEMOS,
	GAME_OPT_SHOWFPS,
	GAME_OPTIONS_ITEMS
};

#define GAME_OPTIONS_PER_PAGE MAX_MENU_LINES
static int game_options_cursor = 0;
static int first_game_option = 0;

static void M_Menu_GameOptions_f (void)
{
	IN_Deactivate (true);
	key_dest = key_menu;
	m_state = m_game;
	m_entersound = true;
}

static void M_GameOptions_AdjustSliders (int dir, qboolean mouse)
{
	if (scrollbar_grab)
		return;

	float f, clamped_mouse = CLAMP (SLIDER_START, (float)m_mouse_x, SLIDER_END);

	if (fabsf (clamped_mouse - (float)m_mouse_x) > 12.0f)
		mouse = false;

	if (dir)
		S_LocalSound ("misc/menu3.wav");

	if (mouse)
		slider_grab = true;

	switch (game_options_cursor)
	{
	case GAME_OPT_SCALE: // console and menu scale
		if (scr_relativescale.value)
		{
			f = M_GetSliderPos (1, 3.0f, scr_relativescale.value, false, mouse, clamped_mouse, dir, 0.1, 999);
			Cvar_SetValue ("scr_relativescale", f);
		}
		else
		{
			f = M_GetSliderPos (1, ((vid.width + 31) / 32) / 10.0, scr_conscale.value, false, mouse, clamped_mouse, dir, 0.1, 999);
			Cvar_SetValue ("scr_conscale", f);
			Cvar_SetValue ("scr_menuscale", f);
			Cvar_SetValue ("scr_sbarscale", f);
		}
		break;
	case GAME_OPT_MOUSESPEED: // mouse speed
		f = M_GetSliderPos (1, 11, sensitivity.value, false, mouse, clamped_mouse, dir, 0.5, 999);
		Cvar_SetValue ("sensitivity", f);
		break;
	case GAME_OPT_SBALPHA: // statusbar alpha
		f = M_GetSliderPos (0, 1, 1.0f - scr_sbaralpha.value, true, mouse, clamped_mouse, dir, 0.05, 999);
		Cvar_SetValue ("scr_sbaralpha", 1.0f - f);
		break;
	case GAME_OPT_VIEWBOB: // statusbar alpha
		f = (1.0f - M_GetSliderPos (0, 1, 1.0f - (cl_bob.value * 20.0f), true, mouse, clamped_mouse, dir, 0.05, 999)) / 20.0f;
		Cvar_SetValue ("cl_bob", f);
		break;
	case GAME_OPT_VIEWROLL: // statusbar alpha
		f = (1.0f - M_GetSliderPos (0, 1, 1.0f - (cl_rollangle.value * 0.2f), true, mouse, clamped_mouse, dir, 0.05, 999)) / 0.2f;
		Cvar_SetValue ("cl_rollangle", f);
		break;
	case GAME_OPT_GUNKICK: // gun kick
		Cvar_SetValue ("v_gunkick", ((int)v_gunkick.value + 3 + dir) % 3);
		break;
	case GAME_OPT_SHOWGUN: // gun kick
		Cvar_SetValue ("r_drawviewmodel", ((int)r_drawviewmodel.value + 2 + dir) % 2);
		break;
	case GAME_OPT_ALWAYRUN: // always run
		Cvar_SetValue ("cl_alwaysrun", !(cl_alwaysrun.value || cl_forwardspeed.value > 200));

		// The past vanilla "always run" option set these two CVARs, so reset them too when
		// changing in case the user previously used that option.
		Cvar_SetValue ("cl_forwardspeed", 200);
		Cvar_SetValue ("cl_backspeed", 200);
		break;
	case GAME_OPT_INVMOUSE:
		Cvar_SetValue ("m_pitch", -m_pitch.value);
		break;
	case GAME_OPT_CROSSHAIR:
		Cvar_SetValue ("crosshair", ((int)crosshair.value + 3 + dir) % 3);
		break;
	case GAME_OPT_HUD_DETAIL: // interface detail
		// cycles through 120 (none), 110 (standard), 100 (full)
		if (scr_viewsize.value <= 100.0f)
			Cvar_SetValue ("viewsize", dir < 0 ? 110.0f : 120.0f);
		else if (scr_viewsize.value <= 110.0f)
			Cvar_SetValue ("viewsize", dir < 0 ? 120.0f : 100.0f);
		else
			Cvar_SetValue ("viewsize", dir < 0 ? 100.0f : 110.0f);
		break;
	case GAME_OPT_HUD_STYLE:
		Cvar_SetValue ("scr_style", ((int)scr_style.value + 3 + dir) % 3);
		break;
	case GAME_OPT_AUTOLOAD: // load last save on death
		Cvar_SetValue ("autoload", ((int)autoload.value + 3 + dir) % 3);
		break;
	case GAME_OPT_STARTUP_DEMOS:
		Cvar_SetValue ("cl_startdemos", ((int)cl_startdemos.value + 2 + dir) % 2);
		break;
	case GAME_OPT_SHOWFPS:
		Cvar_SetValue ("scr_showfps", ((int)scr_showfps.value + 2 + dir) % 2);
		break;
	}
}

static void M_GameOptions_Key (int k)
{
	if (M_HandleScrollBarKeys (k, &game_options_cursor, &first_game_option, GAME_OPTIONS_ITEMS, GAME_OPTIONS_PER_PAGE))
		return;

	switch (k)
	{
	case K_MOUSE2:
	case K_ESCAPE:
	case K_BBUTTON:
		M_Menu_Options_f ();
		break;

	case K_MOUSE1:
	case K_ENTER:
	case K_KP_ENTER:
	case K_ABUTTON:
		m_entersound = true;
		M_GameOptions_AdjustSliders (1, k == K_MOUSE1);
		return;

	case K_LEFTARROW:
		M_GameOptions_AdjustSliders (-1, false);
		break;

	case K_RIGHTARROW:
		M_GameOptions_AdjustSliders (1, false);
		break;
	}
}
static void M_GameOptions_Draw (cb_context_t *cbx)
{
	float	  l, r;
	qpic_t	 *p;
	const int top = MENU_TOP;

	M_DrawTransPic (cbx, 16, 4, Draw_CachePic ("gfx/qplaque.lmp"));
	p = Draw_CachePic ("gfx/p_option.lmp");
	M_DrawPic (cbx, (320 - p->width) / 2, 4, p);

	// Draw the items in the order of the enum defined above:

	for (int i = 0; i < GAME_OPTIONS_PER_PAGE && i < (int)GAME_OPTIONS_ITEMS; i++)
	{
		const int y = top + i * CHARACTER_SIZE;
		switch (i + first_game_option)
		{
		case GAME_OPT_SCALE:
			M_Print (cbx, MENU_LABEL_X, y, "Interface Scale");
			l = scr_relativescale.value ? 2.0f : ((vid.width / 320.0) - 1);
			r = l > 0 ? ((scr_relativescale.value ? scr_relativescale.value : scr_conscale.value) - 1) / l : 0;
			M_DrawSlider (cbx, MENU_SLIDER_X, y, r);
			break;

		case GAME_OPT_SBALPHA:
			M_Print (cbx, MENU_LABEL_X, y, "HUD Opacity");
			r = scr_sbaralpha.value; // scr_sbaralpha range is 1.0 to 0.0
			M_DrawSlider (cbx, MENU_SLIDER_X, y, r);
			break;

		case GAME_OPT_MOUSESPEED:
			M_Print (cbx, MENU_LABEL_X, y, "Mouse Speed");
			r = (sensitivity.value - 1) / 10;
			M_DrawSlider (cbx, MENU_SLIDER_X, y, r);
			break;

		case GAME_OPT_VIEWBOB:
			M_Print (cbx, MENU_LABEL_X, y, "View Bob");
			r = cl_bob.value * 20.0f;
			M_DrawSlider (cbx, MENU_SLIDER_X, y, r);
			break;

		case GAME_OPT_VIEWROLL:
			M_Print (cbx, MENU_LABEL_X, y, "View Roll");
			r = cl_rollangle.value * 0.2f;
			M_DrawSlider (cbx, MENU_SLIDER_X, y, r);
			break;

		case GAME_OPT_GUNKICK:
			M_Print (cbx, MENU_LABEL_X, y, "Gun Kick");
			M_Print (cbx, MENU_VALUE_X, y, (v_gunkick.value == 2) ? "smooth" : (v_gunkick.value == 1) ? "classic" : "off");
			break;

		case GAME_OPT_SHOWGUN:
			M_Print (cbx, MENU_LABEL_X, y, "Show Gun");
			M_DrawCheckbox (cbx, MENU_VALUE_X, y, r_drawviewmodel.value);
			break;

		case GAME_OPT_ALWAYRUN:
			M_Print (cbx, MENU_LABEL_X, y, "Always Run");
			M_DrawCheckbox (cbx, MENU_VALUE_X, y, cl_alwaysrun.value || cl_forwardspeed.value > 200.0);
			break;

		case GAME_OPT_INVMOUSE:
			M_Print (cbx, MENU_LABEL_X, y, "Invert Mouse");
			M_DrawCheckbox (cbx, MENU_VALUE_X, y, m_pitch.value < 0);
			break;

		case GAME_OPT_HUD_DETAIL:
			M_Print (cbx, MENU_LABEL_X, y, "HUD Detail");
			if (scr_viewsize.value >= 120.0f)
				M_Print (cbx, MENU_VALUE_X, y, "None");
			else if (scr_viewsize.value >= 110.0f)
				M_Print (cbx, MENU_VALUE_X, y, "Minimal");
			else
				M_Print (cbx, MENU_VALUE_X, y, "Full");
			break;

		case GAME_OPT_HUD_STYLE:
			M_Print (cbx, MENU_LABEL_X, y, "HUD Style");
			if (scr_style.value < 1.0f)
				M_Print (cbx, MENU_VALUE_X, y, "Mod");
			else if (scr_style.value < 2.0f)
				M_Print (cbx, MENU_VALUE_X, y, "Classic");
			else
				M_Print (cbx, MENU_VALUE_X, y, "Modern");
			break;

		case GAME_OPT_CROSSHAIR:
			M_Print (cbx, MENU_LABEL_X, y, "Crosshair");
			if (crosshair.value == 0.0f)
				M_Print (cbx, MENU_VALUE_X, y, "off");
			else if (crosshair.value < 2.0f)
				M_PrintHighlighted (cbx, MENU_VALUE_X, y, "+");
			else
				M_PrintHighlighted (cbx, MENU_VALUE_X + 2, y - 1, ".");
			break;

		case GAME_OPT_AUTOLOAD:
			M_Print (cbx, MENU_LABEL_X, y, "Load last save");
			M_Print (cbx, MENU_VALUE_X, y, (autoload.value >= 2) ? "fast" : (autoload.value ? "on" : "off"));
			break;

		case GAME_OPT_STARTUP_DEMOS:
			M_Print (cbx, MENU_LABEL_X, y, "Startup Demos");
			M_DrawCheckbox (cbx, MENU_VALUE_X, y, cl_startdemos.value);
			break;

		case GAME_OPT_SHOWFPS:
			M_Print (cbx, MENU_LABEL_X, y, "Show FPS");
			M_DrawCheckbox (cbx, MENU_VALUE_X, y, scr_showfps.value);
			break;
		}
	}

	if (GAME_OPTIONS_ITEMS > GAME_OPTIONS_PER_PAGE)
		M_DrawScrollbar (
			cbx, MENU_SCROLLBAR_X, MENU_TOP + CHARACTER_SIZE, (float)(first_game_option) / (GAME_OPTIONS_ITEMS - GAME_OPTIONS_PER_PAGE),
			GAME_OPTIONS_PER_PAGE - 2);

	// cursor
	M_Mouse_UpdateListCursor (&game_options_cursor, MENU_CURSOR_X, 320, top, CHARACTER_SIZE, GAME_OPTIONS_PER_PAGE, first_game_option);
	Draw_Character (cbx, MENU_CURSOR_X, top + (game_options_cursor - first_game_option) * CHARACTER_SIZE, 12 + ((int)(realtime * 4) & 1));
}

//=============================================================================
/* GRAPHICS OPTIONS MENU */

enum
{
	GRAPHICS_OPT_GAMMA,
	GRAPHICS_OPT_CONTRAST,
	GRAPHICS_OPT_FOV,
	GRAPHICS_OPT_8BIT_COLOR,
	GRAPHICS_OPT_FILTER,
	GRAPHICS_OPT_MAX_FPS,
	GRAPHICS_OPT_ANTIALIASING_SAMPLES,
	GRAPHICS_OPT_ANTIALIASING_MODE,
	GRAPHICS_OPT_RENDER_SCALE,
	GRAPHICS_OPT_ANISOTROPY,
	GRAPHICS_OPT_UNDERWATER,
	GRAPHICS_OPT_MODELS,
	GRAPHICS_OPT_MODEL_INTERPOLATION,
	GRAPHICS_OPT_PARTICLES,
	GRAPHICS_OPT_SHADOWS,
	GRAPHICS_OPTIONS_ITEMS,
};

static int M_GraphicsOptions_NumItems ()
{
	return GRAPHICS_OPTIONS_ITEMS - (vulkan_globals.ray_query ? 0 : 1);
}

static int graphics_options_cursor = 0;

static void M_Menu_GraphicsOptions_f (void)
{
	IN_Deactivate (true);
	key_dest = key_menu;
	m_state = m_graphics;
	m_entersound = true;
}

static void M_GraphicsOptions_ChooseNextAASamples (int dir)
{
	int value = vid_fsaa.value;

	if (dir > 0)
	{
		if (value >= 16)
			value = 0;
		else if (value >= 8)
			value = 16;
		else if (value >= 4)
			value = 8;
		else if (value >= 2)
			value = 4;
		else
			value = 2;
	}
	else
	{
		if (value <= 0)
			value = 16;
		else if (value <= 2)
			value = 0;
		else if (value <= 4)
			value = 2;
		else if (value <= 8)
			value = 4;
		else if (value <= 16)
			value = 8;
		else
			value = 16;
	}

	Cvar_SetValueQuick (&vid_fsaa, (float)value);
}

static void M_GraphicsOptions_ChooseNextRenderScale (int dir)
{
	int value = r_scale.value;

	if (dir > 0)
	{
		if (value >= 8)
			value = 0;
		else if (value >= 4)
			value = 8;
		else if (value >= 2)
			value = 4;
		else
			value = 2;
	}
	else
	{
		if (value <= 0)
			value = 8;
		else if (value <= 2)
			value = 0;
		else if (value <= 4)
			value = 2;
		else if (value <= 8)
			value = 4;
		else
			value = 8;
	}

	Cvar_SetValueQuick (&r_scale, (float)value);
}

static void M_GraphicsOptions_ChooseNextParticles (int dir)
{
	int value = r_particles.value;

	if (dir > 0)
	{
		if (value == 0)
			value = 2;
		else if (value == 2)
			value = 1;
		else
			value = 0;
	}
	else
	{
		if (value == 0)
			value = 1;
		else if (value == 2)
			value = 0;
		else
			value = 2;
	}

	Cvar_SetValueQuick (&r_particles, (float)value);
}

static void M_GraphicsOptions_AdjustSliders (int dir, qboolean mouse)
{
	float f, clamped_mouse = CLAMP (SLIDER_START, (float)m_mouse_x, SLIDER_END);

	if (fabsf (clamped_mouse - (float)m_mouse_x) > 12.0f)
		mouse = false;

	if (dir)
		S_LocalSound ("misc/menu3.wav");

	if (mouse)
		slider_grab = true;

	switch (graphics_options_cursor)
	{
	case GRAPHICS_OPT_GAMMA:
		f = M_GetSliderPos (0.5, 1, vid_gamma.value, true, mouse, clamped_mouse, dir, 0.05, 999);
		Cvar_SetValue ("gamma", f);
		break;
	case GRAPHICS_OPT_CONTRAST:
		f = M_GetSliderPos (1, 2, vid_contrast.value, false, mouse, clamped_mouse, dir, 0.1, 999);
		Cvar_SetValue ("contrast", f);
		break;
	case GRAPHICS_OPT_FOV:
		f = M_GetSliderPos (80, 130, scr_fov.value, false, mouse, clamped_mouse, dir, 5, 999);
		Cvar_SetValue ("fov", f);
		break;
	case GRAPHICS_OPT_8BIT_COLOR:
		Cvar_SetValueQuick (&vid_palettize, (float)(((int)vid_palettize.value + 2 + dir) % 2));
		break;
	case GRAPHICS_OPT_FILTER:
		Cvar_SetValueQuick (&vid_filter, (float)(((int)vid_filter.value + 2 + dir) % 2));
		break;
	case GRAPHICS_OPT_MAX_FPS:
		Cvar_SetValueQuick (&host_maxfps, (float)CLAMP (0, (((int)host_maxfps.value + (dir * 10)) / 10) * 10, 1000));
		break;
	case GRAPHICS_OPT_ANTIALIASING_SAMPLES:
		M_GraphicsOptions_ChooseNextAASamples (dir);
		Cbuf_AddText ("vid_restart\n");
		break;
	case GRAPHICS_OPT_ANTIALIASING_MODE:
		if (vulkan_globals.device_features.sampleRateShading)
			Cvar_SetValueQuick (&vid_fsaamode, (float)(((int)vid_fsaamode.value + 2 + dir) % 2));
		break;
	case GRAPHICS_OPT_RENDER_SCALE:
		M_GraphicsOptions_ChooseNextRenderScale (dir);
		break;
	case GRAPHICS_OPT_ANISOTROPY:
		Cvar_SetValueQuick (&vid_anisotropic, (float)(((int)vid_anisotropic.value + 2 + dir) % 2));
		break;
	case GRAPHICS_OPT_UNDERWATER:
		Cvar_SetValueQuick (&r_waterwarp, (float)(((int)r_waterwarp.value + 3 + dir) % 3));
		break;
	case GRAPHICS_OPT_MODELS:
		Cvar_SetValueQuick (&r_md5models, (float)(((int)r_md5models.value + 2 + dir) % 2));
		break;
	case GRAPHICS_OPT_MODEL_INTERPOLATION:
		Cvar_SetValueQuick (&r_lerpmodels, (float)(((int)r_lerpmodels.value + 2 + dir) % 2));
		Cvar_SetValueQuick (&r_lerpmove, r_lerpmodels.value);
		Cvar_SetValueQuick (&r_lerpturn, r_lerpmodels.value);
		break;
	case GRAPHICS_OPT_PARTICLES:
		M_GraphicsOptions_ChooseNextParticles (dir);
		break;
	case GRAPHICS_OPT_SHADOWS:
		if (vulkan_globals.ray_query)
			Cvar_SetValueQuick (&r_rtshadows, (float)(((int)r_rtshadows.value + 2 + dir) % 2));
		break;
	}
}

static void M_GraphicsOptions_Key (int k)
{
	switch (k)
	{
	case K_MOUSE2:
	case K_ESCAPE:
	case K_BBUTTON:
		M_Menu_Options_f ();
		break;

	case K_MOUSE1:
	case K_ENTER:
	case K_KP_ENTER:
	case K_ABUTTON:
		m_entersound = true;
		M_GraphicsOptions_AdjustSliders (1, k == K_MOUSE1);
		return;

	case K_UPARROW:
		S_LocalSound ("misc/menu1.wav");
		graphics_options_cursor--;
		if (graphics_options_cursor < 0)
			graphics_options_cursor = M_GraphicsOptions_NumItems () - 1;
		break;

	case K_DOWNARROW:
		S_LocalSound ("misc/menu1.wav");
		graphics_options_cursor++;
		if (graphics_options_cursor >= M_GraphicsOptions_NumItems ())
			graphics_options_cursor = 0;
		break;

	case K_LEFTARROW:
		M_GraphicsOptions_AdjustSliders (-1, false);
		break;

	case K_RIGHTARROW:
		M_GraphicsOptions_AdjustSliders (1, false);
		break;
	}
}
static void M_GraphicsOptions_Draw (cb_context_t *cbx)
{
	float	  r = 0.0f;
	qpic_t	 *p;
	const int top = MENU_TOP;

	M_DrawTransPic (cbx, 16, 4, Draw_CachePic ("gfx/qplaque.lmp"));
	p = Draw_CachePic ("gfx/p_option.lmp");
	M_DrawPic (cbx, (320 - p->width) / 2, 4, p);

	// Draw the items in the order of the enum defined above:
	M_Print (cbx, MENU_LABEL_X, top + CHARACTER_SIZE * GRAPHICS_OPT_GAMMA, "Gamma");
	r = (1.0 - vid_gamma.value) / 0.5;
	M_DrawSlider (cbx, MENU_SLIDER_X, top + CHARACTER_SIZE * GRAPHICS_OPT_GAMMA, r);

	M_Print (cbx, MENU_LABEL_X, top + CHARACTER_SIZE * GRAPHICS_OPT_CONTRAST, "Contrast");
	r = vid_contrast.value - 1.0;
	M_DrawSlider (cbx, MENU_SLIDER_X, top + CHARACTER_SIZE * GRAPHICS_OPT_CONTRAST, r);

	M_Print (cbx, MENU_LABEL_X, top + CHARACTER_SIZE * GRAPHICS_OPT_FOV, "Field of View");
	r = (scr_fov.value - 80) / (130 - 80);
	M_DrawSlider (cbx, MENU_SLIDER_X, top + CHARACTER_SIZE * GRAPHICS_OPT_FOV, r);

	M_Print (cbx, MENU_LABEL_X, top + CHARACTER_SIZE * GRAPHICS_OPT_8BIT_COLOR, "8-bit Color");
	M_DrawCheckbox (cbx, MENU_VALUE_X, top + CHARACTER_SIZE * GRAPHICS_OPT_8BIT_COLOR, vid_palettize.value);

	M_Print (cbx, MENU_LABEL_X, top + CHARACTER_SIZE * GRAPHICS_OPT_FILTER, "Textures");
	M_Print (cbx, MENU_VALUE_X, top + CHARACTER_SIZE * GRAPHICS_OPT_FILTER, (vid_filter.value == 0) ? "smooth" : "classic");

	M_Print (cbx, MENU_LABEL_X, top + CHARACTER_SIZE * GRAPHICS_OPT_MAX_FPS, "Max FPS");
	if (host_maxfps.value <= 0)
		M_Print (cbx, MENU_VALUE_X, top + CHARACTER_SIZE * GRAPHICS_OPT_MAX_FPS, "no limit");
	else
		M_Print (cbx, MENU_VALUE_X, top + CHARACTER_SIZE * GRAPHICS_OPT_MAX_FPS, va ("%" SDL_PRIu32, q_min ((uint32_t)host_maxfps.value, (uint32_t)1000)));

	M_Print (cbx, MENU_LABEL_X, top + CHARACTER_SIZE * GRAPHICS_OPT_ANTIALIASING_SAMPLES, "Antialiasing");
	M_Print (
		cbx, MENU_VALUE_X, top + CHARACTER_SIZE * GRAPHICS_OPT_ANTIALIASING_SAMPLES,
		((int)vid_fsaa.value >= 2) ? va ("%ix", CLAMP (2, (int)vid_fsaa.value, 16)) : "off");

	M_Print (cbx, MENU_LABEL_X, top + CHARACTER_SIZE * GRAPHICS_OPT_ANTIALIASING_MODE, "AA mode");
	M_Print (
		cbx, MENU_VALUE_X, top + CHARACTER_SIZE * GRAPHICS_OPT_ANTIALIASING_MODE,
		(((int)vid_fsaamode.value == 0) || !vulkan_globals.device_features.sampleRateShading) ? "Multisample" : "Supersample");

	M_Print (cbx, MENU_LABEL_X, top + CHARACTER_SIZE * GRAPHICS_OPT_RENDER_SCALE, "Render Scale");
	M_Print (cbx, MENU_VALUE_X, top + CHARACTER_SIZE * GRAPHICS_OPT_RENDER_SCALE, (r_scale.value >= 2) ? va ("1/%i", (int)r_scale.value) : "off");

	M_Print (cbx, MENU_LABEL_X, top + CHARACTER_SIZE * GRAPHICS_OPT_ANISOTROPY, "Anisotropic");
	M_Print (
		cbx, MENU_VALUE_X, top + CHARACTER_SIZE * GRAPHICS_OPT_ANISOTROPY,
		(vid_anisotropic.value == 0) ? "off" : va ("on (%gx)", vulkan_globals.device_properties.limits.maxSamplerAnisotropy));

	M_Print (cbx, MENU_LABEL_X, top + CHARACTER_SIZE * GRAPHICS_OPT_UNDERWATER, "Underwater FX");
	M_Print (
		cbx, MENU_VALUE_X, top + CHARACTER_SIZE * GRAPHICS_OPT_UNDERWATER,
		(r_waterwarp.value == 0) ? "off" : ((r_waterwarp.value == 1) ? "Classic" : "glQuake"));

	M_Print (cbx, MENU_LABEL_X, top + CHARACTER_SIZE * GRAPHICS_OPT_MODELS, "Models");
	M_Print (cbx, MENU_VALUE_X, top + CHARACTER_SIZE * GRAPHICS_OPT_MODELS, (r_md5models.value == 0) ? "classic" : "remastered");

	M_Print (cbx, MENU_LABEL_X, top + CHARACTER_SIZE * GRAPHICS_OPT_MODEL_INTERPOLATION, "Animations");
	M_Print (cbx, MENU_VALUE_X, top + CHARACTER_SIZE * GRAPHICS_OPT_MODEL_INTERPOLATION, (r_lerpmodels.value == 0) ? "classic" : "smooth");

	M_Print (cbx, MENU_LABEL_X, top + CHARACTER_SIZE * GRAPHICS_OPT_PARTICLES, "Particles");
	M_Print (
		cbx, MENU_VALUE_X, top + CHARACTER_SIZE * GRAPHICS_OPT_PARTICLES,
		((int)r_particles.value == 0) ? "off" : (((int)r_particles.value == 2) ? "Classic" : "glQuake"));

	if (vulkan_globals.ray_query)
	{
		M_Print (cbx, MENU_LABEL_X, top + CHARACTER_SIZE * GRAPHICS_OPT_SHADOWS, "Dynamic Shadows");
		M_DrawCheckbox (cbx, MENU_VALUE_X, top + CHARACTER_SIZE * GRAPHICS_OPT_SHADOWS, r_rtshadows.value);
	}

	// cursor
	M_Mouse_UpdateListCursor (&graphics_options_cursor, MENU_CURSOR_X, 320, top, CHARACTER_SIZE, M_GraphicsOptions_NumItems (), 0);
	Draw_Character (cbx, MENU_CURSOR_X, top + graphics_options_cursor * CHARACTER_SIZE, 12 + ((int)(realtime * 4) & 1));
}

//=============================================================================
/* SOUND OPTIONS MENU */

enum
{
	SOUND_OPT_SNDVOL,
	SOUND_OPT_MUSICVOL,
	SOUND_OPT_MUSICEXT,
	SOUND_OPT_WATERFX,
	SOUND_OPTIONS_ITEMS
};

static int sound_options_cursor = 0;

static void M_Menu_SoundOptions_f (void)
{
	IN_Deactivate (true);
	key_dest = key_menu;
	m_state = m_sound;
	m_entersound = true;
}

static void M_SoundOptions_AdjustSliders (int dir, qboolean mouse)
{
	float f, clamped_mouse = CLAMP (SLIDER_START, (float)m_mouse_x, SLIDER_END);

	if (fabsf (clamped_mouse - (float)m_mouse_x) > 12.0f)
		mouse = false;

	if (dir)
		S_LocalSound ("misc/menu3.wav");

	if (mouse)
		slider_grab = true;

	switch (sound_options_cursor)
	{
	case SOUND_OPT_SNDVOL:
		f = M_GetSliderPos (0, 1, sfxvolume.value, false, mouse, clamped_mouse, dir, 0.1, 999);
		Cvar_SetValue ("volume", f);
		break;
	case SOUND_OPT_MUSICVOL:
		f = M_GetSliderPos (0, 1, bgmvolume.value, false, mouse, clamped_mouse, dir, 0.1, 999);
		Cvar_SetValue ("bgmvolume", f);
		break;
	case SOUND_OPT_MUSICEXT:
		Cvar_SetValueQuick (&bgm_extmusic, (float)(((int)bgm_extmusic.value + 2 + dir) % 2));
		break;
	case SOUND_OPT_WATERFX:
		Cvar_SetValueQuick (&snd_waterfx, (float)(((int)snd_waterfx.value + 2 + dir) % 2));
		break;
	}
}

static void M_SoundOptions_Key (int k)
{
	switch (k)
	{
	case K_MOUSE2:
	case K_ESCAPE:
	case K_BBUTTON:
		M_Menu_Options_f ();
		break;

	case K_MOUSE1:
	case K_ENTER:
	case K_KP_ENTER:
	case K_ABUTTON:
		m_entersound = true;
		M_SoundOptions_AdjustSliders (1, k == K_MOUSE1);
		return;

	case K_UPARROW:
		S_LocalSound ("misc/menu1.wav");
		sound_options_cursor--;
		if (sound_options_cursor < 0)
			sound_options_cursor = SOUND_OPTIONS_ITEMS - 1;
		break;

	case K_DOWNARROW:
		S_LocalSound ("misc/menu1.wav");
		sound_options_cursor++;
		if (sound_options_cursor >= SOUND_OPTIONS_ITEMS)
			sound_options_cursor = 0;
		break;

	case K_LEFTARROW:
		M_SoundOptions_AdjustSliders (-1, false);
		break;

	case K_RIGHTARROW:
		M_SoundOptions_AdjustSliders (1, false);
		break;
	}
}
static void M_SoundOptions_Draw (cb_context_t *cbx)
{
	qpic_t	 *p;
	const int top = MENU_TOP;

	M_DrawTransPic (cbx, 16, 4, Draw_CachePic ("gfx/qplaque.lmp"));
	p = Draw_CachePic ("gfx/p_option.lmp");
	M_DrawPic (cbx, (320 - p->width) / 2, 4, p);

	// Draw the items in the order of the enum defined above:

	M_Print (cbx, MENU_LABEL_X, top + CHARACTER_SIZE * SOUND_OPT_SNDVOL, "Sound Volume");
	M_DrawSlider (cbx, MENU_SLIDER_X, top + CHARACTER_SIZE * SOUND_OPT_SNDVOL, sfxvolume.value);

	M_Print (cbx, MENU_LABEL_X, top + CHARACTER_SIZE * SOUND_OPT_MUSICVOL, "Music Volume");
	M_DrawSlider (cbx, MENU_SLIDER_X, top + CHARACTER_SIZE * SOUND_OPT_MUSICVOL, bgmvolume.value);

	M_Print (cbx, MENU_LABEL_X, top + CHARACTER_SIZE * SOUND_OPT_MUSICEXT, "External Music");
	M_DrawCheckbox (cbx, MENU_VALUE_X, top + CHARACTER_SIZE * SOUND_OPT_MUSICEXT, bgm_extmusic.value);

	M_Print (cbx, MENU_LABEL_X, top + CHARACTER_SIZE * SOUND_OPT_WATERFX, "Underwater FX");
	M_DrawCheckbox (cbx, MENU_VALUE_X, top + CHARACTER_SIZE * SOUND_OPT_WATERFX, snd_waterfx.value);

	// cursor
	M_Mouse_UpdateListCursor (&sound_options_cursor, MENU_CURSOR_X, 320, top, CHARACTER_SIZE, SOUND_OPTIONS_ITEMS, 0);
	Draw_Character (cbx, MENU_CURSOR_X, top + sound_options_cursor * CHARACTER_SIZE, 12 + ((int)(realtime * 4) & 1));
}

//=============================================================================
/* OPTIONS MENU */

enum
{
	OPT_GAME = 0,
	OPT_CONTROLS,
	OPT_VIDEO,
	OPT_GRAPHICS,
	OPT_SOUND,
	OPT_PADDING,
	OPT_DEFAULTS,
	OPTIONS_ITEMS
};

static int options_cursor;

void M_Menu_Options_f (void)
{
	M_MenuChanged ();
	IN_Deactivate (true);
	key_dest = key_menu;
	m_state = m_options;
}

static void M_Options_Draw (cb_context_t *cbx)
{
	qpic_t	 *p;
	const int top = 40;

	M_DrawTransPic (cbx, 16, 4, Draw_CachePic ("gfx/qplaque.lmp"));
	p = Draw_CachePic ("gfx/p_option.lmp");
	M_DrawPic (cbx, (320 - p->width) / 2, 4, p);

	// Draw the items in the order of the enum defined above:
	M_Print (cbx, MENU_LABEL_X, top + CHARACTER_SIZE * OPT_GAME, "Game");
	M_Print (cbx, MENU_LABEL_X, top + CHARACTER_SIZE * OPT_CONTROLS, "Key Bindings");
	M_Print (cbx, MENU_LABEL_X, top + CHARACTER_SIZE * OPT_VIDEO, "Video");
	M_Print (cbx, MENU_LABEL_X, top + CHARACTER_SIZE * OPT_GRAPHICS, "Graphics");
	M_Print (cbx, MENU_LABEL_X, top + CHARACTER_SIZE * OPT_SOUND, "Sound");
	M_Print (cbx, MENU_LABEL_X, top + CHARACTER_SIZE * OPT_DEFAULTS, "Reset config");

	// cursor
	M_Mouse_UpdateListCursor (&options_cursor, MENU_LABEL_X, 320, top, CHARACTER_SIZE, OPTIONS_ITEMS, 0);
	if (options_cursor == OPT_PADDING)
		options_cursor = OPT_SOUND;
	Draw_Character (cbx, MENU_CURSOR_X, top + options_cursor * CHARACTER_SIZE, 12 + ((int)(realtime * 4) & 1));
}

void M_Options_Key (int k)
{
	switch (k)
	{
	case K_MOUSE2:
	case K_ESCAPE:
	case K_BBUTTON:
		M_Menu_Main_f ();
		break;

	case K_MOUSE1:
	case K_ENTER:
	case K_KP_ENTER:
	case K_ABUTTON:
		m_entersound = true;
		m_mouse_x_pixels = -1;
		switch (options_cursor)
		{
		case OPT_GAME:
			M_Menu_GameOptions_f ();
			break;
		case OPT_CONTROLS:
			M_Menu_Keys_f ();
			break;
		case OPT_DEFAULTS:
			if (SCR_ModalMessage (
					"This will reset all controls\n"
					"and stored cvars. Continue? (y/n)\n",
					15.0f))
			{
				Cbuf_AddText ("resetcfg\n");
				Cbuf_AddText ("exec default.cfg\n");
			}
			break;
		case OPT_VIDEO:
			M_Menu_Video_f ();
			break;
		case OPT_GRAPHICS:
			M_Menu_GraphicsOptions_f ();
			break;
		case OPT_SOUND:
			M_Menu_SoundOptions_f ();
			break;
		}
		return;

	case K_UPARROW:
		S_LocalSound ("misc/menu1.wav");
		--options_cursor;
		if (options_cursor == OPT_PADDING)
			--options_cursor;
		if (options_cursor < 0)
			options_cursor = OPTIONS_ITEMS - 1;
		break;

	case K_DOWNARROW:
		S_LocalSound ("misc/menu1.wav");
		++options_cursor;
		if (options_cursor == OPT_PADDING)
			++options_cursor;
		if (options_cursor >= OPTIONS_ITEMS)
			options_cursor = 0;
		break;
	}
}

//=============================================================================
/* KEYS MENU */

const char *bindnames[][2] = {
	{"+forward", "Move Forward"},
	{"+back", "Move Backward"},
	{"+moveleft", "Strafe Left"},
	{"+moveright", "Strafe Right"},
	{"+jump", "Jump / Swim up"},
	{"+attack", "Attack"},
	{"+speed", "Run"},
	{"+zoom", "Quick zoom"},
	{"+moveup", "Swim up"},
	{"+movedown", "Swim down"},
	{"+showscores", "Show Scores"},
	{"impulse 10", "Next weapon"},
	{"impulse 12", "Prev weapon"},
	{"impulse 1", "Axe"},
	{"impulse 2", "Shotgun"},
	{"impulse 3", "Super Shotgun"},
	{"impulse 4", "Nailgun"},
	{"impulse 5", "Super Nailgun"},
	{"impulse 6", "Grenade Lnchr."},
	{"impulse 7", "Rocket Lnchr."},
	{"impulse 8", "Thunderbolt"},
	{"toggleconsole", "Toggle console"},
};

#define NUMCOMMANDS (sizeof (bindnames) / sizeof (bindnames[0]))

static int		keys_cursor;
static qboolean bind_grab;

void M_Menu_Keys_f (void)
{
	M_MenuChanged ();
	IN_Deactivate (true);
	key_dest = key_menu;
	m_state = m_keys;
}

void M_FindKeysForCommand (const char *command, int *twokeys)
{
	int	  count;
	int	  j;
	char *b;

	twokeys[0] = twokeys[1] = -1;
	count = 0;

	for (j = 0; j < MAX_KEYS; j++)
	{
		b = keybindings[j];
		if (!b)
			continue;
		if (!strcmp (b, command))
		{
			twokeys[count] = j;
			count++;
			if (count == 2)
				break;
		}
	}
}

void M_UnbindCommand (const char *command)
{
	int	  j;
	char *b;

	for (j = 0; j < MAX_KEYS; j++)
	{
		b = keybindings[j];
		if (!b)
			continue;
		if (!strcmp (b, command))
			Key_SetBinding (j, NULL);
	}
}

extern qpic_t *pic_up, *pic_down;

#define BINDS_PER_PAGE 19

static int first_key;

static void M_Keys_Draw (cb_context_t *cbx)
{
	int			i, x, y;
	int			keys[2];
	const char *name;
	qpic_t	   *p;
	int			keys_height = q_min (BINDS_PER_PAGE, NUMCOMMANDS - first_key);

	p = Draw_CachePic ("gfx/ttl_cstm.lmp");
	M_DrawPic (cbx, (320 - p->width) / 2, 4, p);

	if (bind_grab)
		M_Print (cbx, 12, 32, "Press a key or button for this action");
	else
		M_Print (cbx, 18, 32, "Enter to change, backspace to clear");

	// search for known bindings
	for (i = 0; i < BINDS_PER_PAGE && i < (int)NUMCOMMANDS; i++)
	{
		y = 48 + 8 * i;

		M_Print (cbx, 10, y, bindnames[i + first_key][1]);

		M_FindKeysForCommand (bindnames[i + first_key][0], keys);

		if (keys[0] == -1)
		{
			M_Print (cbx, 140, y, "???");
		}
		else
		{
			name = Key_KeynumToString (keys[0]);
			M_Print (cbx, 140, y, name);
			x = strlen (name) * 8;
			if (keys[1] != -1)
			{
				name = Key_KeynumToString (keys[1]);
				M_PrintHighlighted (cbx, 138 + x, y, ",");
				M_Print (cbx, 138 + x + 12, y, name);
				x = x + 12 + strlen (name) * 8;
			}
		}
	}

	if (NUMCOMMANDS > BINDS_PER_PAGE)
		M_DrawScrollbar (cbx, MENU_SCROLLBAR_X, 56, (float)(first_key) / (NUMCOMMANDS - BINDS_PER_PAGE), BINDS_PER_PAGE - 2);

	if (bind_grab)
		Draw_Character (cbx, 130, 48 + (keys_cursor - first_key) * 8, '=');
	else
	{
		M_Mouse_UpdateListCursor (&keys_cursor, 12, 400, 48, 8, keys_height, first_key);
		Draw_Character (cbx, 0, 48 + (keys_cursor - first_key) * 8, 12 + ((int)(realtime * 4) & 1));
	}
}

void M_Keys_Key (int k)
{
	char cmd[80];
	int	 keys[2];

	if (bind_grab)
	{ // defining a key
		S_LocalSound ("misc/menu1.wav");
		if ((k != K_ESCAPE) && (k != '`'))
		{
			q_snprintf (cmd, sizeof (cmd), "bind \"%s\" \"%s\"\n", Key_KeynumToString (k), bindnames[keys_cursor][0]);
			Cbuf_InsertText (cmd);
		}

		bind_grab = false;
		IN_Deactivate (true); // deactivate because we're returning to the menu
		return;
	}

	if (M_HandleScrollBarKeys (k, &keys_cursor, &first_key, (int)NUMCOMMANDS, BINDS_PER_PAGE))
		return;

	switch (k)
	{
	case K_MOUSE2:
	case K_ESCAPE:
	case K_BBUTTON:
		M_Menu_Options_f ();
		break;

	case K_MOUSE1:
	case K_ENTER: // go into bind mode
	case K_KP_ENTER:
	case K_ABUTTON:
		M_FindKeysForCommand (bindnames[keys_cursor][0], keys);
		S_LocalSound ("misc/menu2.wav");
		if (keys[1] != -1)
			M_UnbindCommand (bindnames[keys_cursor][0]);
		bind_grab = true;
		IN_Activate (); // activate to allow mouse key binding
		break;

	case K_BACKSPACE: // delete bindings
	case K_DEL:
		S_LocalSound ("misc/menu2.wav");
		M_UnbindCommand (bindnames[keys_cursor][0]);
		break;
	}
}

//=============================================================================
/* HELP MENU */

int help_page;
#define NUM_HELP_PAGES 6

static void M_Menu_Help_f (void)
{
	M_MenuChanged ();
	IN_Deactivate (true);
	key_dest = key_menu;
	m_state = m_help;
	m_entersound = true;
	help_page = 0;
}

static void M_Help_Draw (cb_context_t *cbx)
{
	M_DrawPic (cbx, 0, 0, Draw_CachePic (va ("gfx/help%i.lmp", help_page)));
}

static void M_Help_Key (int key)
{
	switch (key)
	{
	case K_MOUSE2:
	case K_ESCAPE:
	case K_BBUTTON:
		M_Menu_Main_f ();
		break;

	case K_MOUSE1:
	case K_MWHEELDOWN:
	case K_UPARROW:
	case K_RIGHTARROW:
		m_entersound = true;
		if (++help_page >= NUM_HELP_PAGES)
			help_page = 0;
		break;

	case K_MWHEELUP:
	case K_DOWNARROW:
	case K_LEFTARROW:
		m_entersound = true;
		if (--help_page < 0)
			help_page = NUM_HELP_PAGES - 1;
		break;
	}
}

//=============================================================================
/* MODS MENU */

#define MAX_MODS_ON_SCREEN MAX_MENU_LINES

static int num_mods = 0;
static int first_mod = 0;
static int mods_cursor = 0;
static int mod_loaded_from_menu = 0;

static void M_Menu_Mods_f (void)
{
	M_MenuChanged ();
	IN_Deactivate (true);
	key_dest = key_menu;
	m_state = m_mods;
	m_entersound = true;
	num_mods = 0;
	for (filelist_item_t *item = modlist; item; item = item->next)
		++num_mods;
	first_mod = 0;
	mods_cursor = 0;
}

static void M_Mods_Draw (cb_context_t *cbx)
{
	M_DrawTransPic (cbx, 16, 4, Draw_CachePic ("gfx/qplaque.lmp"));
	qpic_t *p = Draw_CachePic ("gfx/p_mods.lmp");
	M_DrawPic (cbx, (320 - p->width) / 2, 4, p);
	int mod_index = -first_mod;
	int mods_height = q_min (MAX_MODS_ON_SCREEN, num_mods - first_mod);

	for (filelist_item_t *item = modlist; item; item = item->next)
	{
		if (mod_index >= MAX_MODS_ON_SCREEN)
			break;
		if (mod_index >= 0)
			M_PrintElided (cbx, MENU_LABEL_X, 32 + mod_index * CHARACTER_SIZE, item->name, 20);
		++mod_index;
	}

	M_Mouse_UpdateListCursor (&mods_cursor, 12, 400, 32, CHARACTER_SIZE, mods_height, first_mod);
	Draw_Character (cbx, MENU_CURSOR_X, 32 + (mods_cursor - first_mod) * CHARACTER_SIZE, 12 + ((int)(realtime * 4) & 1));
	if (num_mods > MAX_MODS_ON_SCREEN)
		M_DrawScrollbar (cbx, 260, 32 + 8, (float)(first_mod) / (float)(num_mods - MAX_MODS_ON_SCREEN), MAX_MODS_ON_SCREEN - 2);
}

static void M_Mods_Key (int key)
{
	int mod_index = 0;

	if (M_HandleScrollBarKeys (key, &mods_cursor, &first_mod, num_mods, MAX_MODS_ON_SCREEN))
		return;

	switch (key)
	{
	case K_MOUSE2:
	case K_ESCAPE:
	case K_BBUTTON:
		M_Menu_Main_f ();
		break;

	case K_MOUSE1:
	case K_ENTER:
	case K_KP_ENTER:
	case K_ABUTTON:
		for (filelist_item_t *item = modlist; item; item = item->next)
			if (mod_index++ == mods_cursor)
			{
				Cbuf_AddText ("game \"");
				Cbuf_AddText (item->name);
				Cbuf_AddText ("\"\n");
				mod_loaded_from_menu = 1;
				m_state = m_main;
			}
		break;
	}
}

//=============================================================================
/* QUIT MENU */

static int			  msg_number;
static enum m_state_e m_quit_prevstate;
static qboolean		  was_in_menus;

void M_Menu_Quit_f (void)
{
	if (m_state == m_quit)
		return;
	if (!mod_loaded_from_menu)
	{
		was_in_menus = (key_dest == key_menu);
		IN_Deactivate (true);
		key_dest = key_menu;
		m_quit_prevstate = m_state;
		m_state = m_quit;
		m_entersound = true;
		msg_number = rand () & 7;
	}
	else
	{
		mod_loaded_from_menu = 0;
		Cbuf_AddText ("game " GAMENAME "\n");
	}
}

static void M_Quit_Key (int key)
{
	if (key == K_ESCAPE)
	{
		if (was_in_menus)
		{
			m_state = m_quit_prevstate;
			m_entersound = true;
		}
		else
		{
			IN_Activate ();
			key_dest = key_game;
			m_state = m_none;
		}
	}
}

static void M_Quit_Char (int key)
{
	switch (key)
	{
	case 'n':
	case 'N':
		if (was_in_menus)
		{
			m_state = m_quit_prevstate;
			m_entersound = true;
		}
		else
		{
			IN_Activate ();
			key_dest = key_game;
			m_state = m_none;
		}
		break;

	case 'y':
	case 'Y':
		m_is_quitting = true;
		IN_Deactivate (true);
		key_dest = key_console;
		Cbuf_InsertText ("quit");
		break;

	default:
		break;
	}
}

static qboolean M_Quit_TextEntry (void)
{
	return true;
}

static void M_Quit_Draw (cb_context_t *cbx) // johnfitz -- modified for new quit message
{
	char msg1[40];
	char msg2[] = "by Axel Gneiting"; /* msg2/msg3 are mostly [40] */
	char msg3[] = "Press y to quit";
	int	 boxlen;

	if (was_in_menus)
	{
		m_state = m_quit_prevstate;
		m_recursiveDraw = true;
		M_Draw (cbx);
		m_state = m_quit;
	}

	q_snprintf (msg1, sizeof (msg1), "vkQuake " VKQUAKE_VER_STRING);

	// okay, this is kind of fucked up.  M_DrawTextBox will always act as if
	// width is even. Also, the width and lines values are for the interior of the box,
	// but the x and y values include the border.
	boxlen = q_max (strlen (msg1), q_max ((sizeof (msg2) - 1), (sizeof (msg3) - 1))) + 1;
	if (boxlen & 1)
		boxlen++;
	M_DrawTextBox (cbx, 160 - 4 * (boxlen + 2), 76, boxlen, 4);

	// now do the text
	M_Print (cbx, 160 - 4 * strlen (msg1), 88, msg1);
	M_Print (cbx, 160 - 4 * (sizeof (msg2) - 1), 96, msg2);
	M_PrintWhite (cbx, 160 - 4 * (sizeof (msg3) - 1), 104, msg3);
}

//=============================================================================
/* LAN CONFIG MENU */

static int lan_config_cursor = -1;
#define NUM_LANCONFIG_CMDS 4

static int	lan_config_port;
static char lan_config_portname[6];
static char lan_config_joinname[22];

static void M_Menu_LanConfig_f (void)
{
	M_MenuChanged ();
	IN_Deactivate (true);
	key_dest = key_menu;
	m_state = m_lanconfig;
	if (lan_config_cursor == -1)
	{
		if (JoiningGame && TCPIPConfig)
			lan_config_cursor = 2;
		else
			lan_config_cursor = 1;
	}
	if (StartingGame && lan_config_cursor >= 2)
		lan_config_cursor = 1;
	lan_config_port = DEFAULTnet_hostport;
	q_snprintf (lan_config_portname, sizeof (lan_config_portname), "%u", lan_config_port);

	m_return_onerror = false;
	m_return_reason[0] = 0;
}

static void M_LanConfig_Draw (cb_context_t *cbx)
{
	qpic_t	   *p;
	int			basex;
	int			y;
	int			numaddresses, i;
	qhostaddr_t addresses[16];
	const char *startJoin;
	const char *protocol;

	M_DrawTransPic (cbx, 16, 4, Draw_CachePic ("gfx/qplaque.lmp"));
	p = Draw_CachePic ("gfx/p_multi.lmp");
	basex = (320 - p->width) / 2;
	M_DrawPic (cbx, basex, 4, p);

	basex = 72;

	if (StartingGame)
		startJoin = "New Game";
	else
		startJoin = "Join Game";
	if (IPXConfig)
		protocol = "IPX";
	else
		protocol = "TCP/IP";
	M_Print (cbx, basex, 32, va ("%s - %s", startJoin, protocol));
	basex += 8;

	y = 52;
	M_Print (cbx, basex, y, "Address:");
	numaddresses = NET_ListAddresses (addresses, sizeof (addresses) / sizeof (addresses[0]));
	if (!numaddresses)
	{
		M_Print (cbx, basex + 9 * 8, y, "NONE KNOWN");
		y += 8;
	}
	else
		for (i = 0; i < numaddresses; i++)
		{
			M_Print (cbx, basex + 9 * 8, y, addresses[i]);
			y += 8;
		}

	y += 8; // for the port's box
	M_Print (cbx, basex, y, "Port");
	M_DrawTextBox (cbx, basex + 8 * 8, y - 8, 6, 1);
	M_Print (cbx, basex + 9 * 8, y, lan_config_portname);
	M_Mouse_UpdateCursor (&lan_config_cursor, basex, 320, y, 8, 0);
	if (lan_config_cursor == 0)
	{
		Draw_Character (cbx, basex + 9 * 8 + 8 * strlen (lan_config_portname), y, 10 + ((int)(realtime * 4) & 1));
		Draw_Character (cbx, basex - 8, y, 12 + ((int)(realtime * 4) & 1));
	}
	y += 20;

	if (JoiningGame)
	{
		M_Print (cbx, basex, y, "Search for local games...");
		M_Mouse_UpdateCursor (&lan_config_cursor, basex, 320, y, 8, 1);
		if (lan_config_cursor == 1)
			Draw_Character (cbx, basex - 8, y, 12 + ((int)(realtime * 4) & 1));
		y += 8;

		M_Print (cbx, basex, y, "Search for public games...");
		M_Mouse_UpdateCursor (&lan_config_cursor, basex, 320, y, 8, 2);
		if (lan_config_cursor == 2)
			Draw_Character (cbx, basex - 8, y, 12 + ((int)(realtime * 4) & 1));
		y += 8;

		M_Print (cbx, basex, y, "Join game at:");
		y += 24;
		M_DrawTextBox (cbx, basex + 8, y - 8, 22, 1);
		M_Print (cbx, basex + 16, y, lan_config_joinname);
		M_Mouse_UpdateCursor (&lan_config_cursor, basex, 320, y, 8, 3);
		if (lan_config_cursor == 3)
		{
			Draw_Character (cbx, basex + 16 + 8 * strlen (lan_config_joinname), y, 10 + ((int)(realtime * 4) & 1));
			Draw_Character (cbx, basex - 8, y, 12 + ((int)(realtime * 4) & 1));
		}
		y += 16;
	}
	else
	{
		M_DrawTextBox (cbx, basex, y - 8, 2, 1);
		M_Print (cbx, basex + 8, y, "OK");
		M_Mouse_UpdateCursor (&lan_config_cursor, basex, 320, y, 8, 1);
		if (lan_config_cursor == 1)
			Draw_Character (cbx, basex - 8, y, 12 + ((int)(realtime * 4) & 1));
		y += 16;
	}

	if (*m_return_reason)
		M_PrintWhite (cbx, basex, 148, m_return_reason);
}

static void M_LanConfig_Key (int key)
{
	int l;

	switch (key)
	{
	case K_MOUSE2:
	case K_ESCAPE:
	case K_BBUTTON:
		M_Menu_Net_f ();
		break;

	case K_UPARROW:
		S_LocalSound ("misc/menu1.wav");
		lan_config_cursor--;
		if (lan_config_cursor < 0)
			lan_config_cursor = NUM_LANCONFIG_CMDS - 1;
		break;

	case K_DOWNARROW:
		S_LocalSound ("misc/menu1.wav");
		lan_config_cursor++;
		if (lan_config_cursor >= NUM_LANCONFIG_CMDS)
			lan_config_cursor = 0;
		break;

	case K_MOUSE1:
	case K_ENTER:
	case K_KP_ENTER:
	case K_ABUTTON:
		if (lan_config_cursor == 0)
			break;

		m_entersound = true;

		M_ConfigureNetSubsystem ();

		if (StartingGame)
		{
			if (lan_config_cursor == 1)
				M_Menu_MPGameOptions_f ();
		}
		else
		{
			if (lan_config_cursor == 1)
				M_Menu_Search_f (SLIST_LAN);
			else if (lan_config_cursor == 2)
				M_Menu_Search_f (SLIST_INTERNET);
			else if (lan_config_cursor == 3)
			{
				m_return_state = m_state;
				m_return_onerror = true;
				IN_Activate ();
				key_dest = key_game;
				m_state = m_none;
				Cbuf_AddText (va ("connect \"%s\"\n", lan_config_joinname));
			}
		}

		break;

	case K_BACKSPACE:
		if (lan_config_cursor == 0)
		{
			if (strlen (lan_config_portname))
				lan_config_portname[strlen (lan_config_portname) - 1] = 0;
		}

		if (lan_config_cursor == 3)
		{
			if (strlen (lan_config_joinname))
				lan_config_joinname[strlen (lan_config_joinname) - 1] = 0;
		}
		break;
	}

	if (StartingGame && lan_config_cursor >= 2)
	{
		if (key == K_UPARROW)
			lan_config_cursor = 1;
		else
			lan_config_cursor = 0;
	}

	l = atoi (lan_config_portname);
	if (l > 65535)
		l = lan_config_port;
	else
		lan_config_port = l;
	q_snprintf (lan_config_portname, sizeof (lan_config_portname), "%u", lan_config_port);
}

static void M_LanConfig_Char (int key)
{
	int l;

	switch (lan_config_cursor)
	{
	case 0:
		if (key < '0' || key > '9')
			return;
		l = strlen (lan_config_portname);
		if (l < 5)
		{
			lan_config_portname[l + 1] = 0;
			lan_config_portname[l] = key;
		}
		break;
	case 3:
		l = strlen (lan_config_joinname);
		if (l < 21)
		{
			lan_config_joinname[l + 1] = 0;
			lan_config_joinname[l] = key;
		}
		break;
	}
}

static qboolean M_LanConfig_TextEntry (void)
{
	return (lan_config_cursor == 0 || lan_config_cursor == 3);
}

//=============================================================================
/* GAME OPTIONS MENU */

typedef struct
{
	const char *name;
	const char *description;
} level_t;

static level_t levels[] = {
	{"start", "Entrance"}, // 0

	{"e1m1", "Slipgate Complex"}, // 1
	{"e1m2", "Castle of the Damned"},
	{"e1m3", "The Necropolis"},
	{"e1m4", "The Grisly Grotto"},
	{"e1m5", "Gloom Keep"},
	{"e1m6", "The Door To Chthon"},
	{"e1m7", "The House of Chthon"},
	{"e1m8", "Ziggurat Vertigo"},

	{"e2m1", "The Installation"}, // 9
	{"e2m2", "Ogre Citadel"},
	{"e2m3", "Crypt of Decay"},
	{"e2m4", "The Ebon Fortress"},
	{"e2m5", "The Wizard's Manse"},
	{"e2m6", "The Dismal Oubliette"},
	{"e2m7", "Underearth"},

	{"e3m1", "Termination Central"}, // 16
	{"e3m2", "The Vaults of Zin"},
	{"e3m3", "The Tomb of Terror"},
	{"e3m4", "Satan's Dark Delight"},
	{"e3m5", "Wind Tunnels"},
	{"e3m6", "Chambers of Torment"},
	{"e3m7", "The Haunted Halls"},

	{"e4m1", "The Sewage System"}, // 23
	{"e4m2", "The Tower of Despair"},
	{"e4m3", "The Elder God Shrine"},
	{"e4m4", "The Palace of Hate"},
	{"e4m5", "Hell's Atrium"},
	{"e4m6", "The Pain Maze"},
	{"e4m7", "Azure Agony"},
	{"e4m8", "The Nameless City"},

	{"end", "Shub-Niggurath's Pit"}, // 31

	{"dm1", "Place of Two Deaths"}, // 32
	{"dm2", "Claustrophobopolis"},
	{"dm3", "The Abandoned Base"},
	{"dm4", "The Bad Place"},
	{"dm5", "The Cistern"},
	{"dm6", "The Dark Zone"}};

// MED 01/06/97 added hipnotic levels
static level_t hipnoticlevels[] = {
	{"start", "Command HQ"}, // 0

	{"hip1m1", "The Pumping Station"}, // 1
	{"hip1m2", "Storage Facility"},
	{"hip1m3", "The Lost Mine"},
	{"hip1m4", "Research Facility"},
	{"hip1m5", "Military Complex"},

	{"hip2m1", "Ancient Realms"}, // 6
	{"hip2m2", "The Black Cathedral"},
	{"hip2m3", "The Catacombs"},
	{"hip2m4", "The Crypt"},
	{"hip2m5", "Mortum's Keep"},
	{"hip2m6", "The Gremlin's Domain"},

	{"hip3m1", "Tur Torment"}, // 12
	{"hip3m2", "Pandemonium"},
	{"hip3m3", "Limbo"},
	{"hip3m4", "The Gauntlet"},

	{"hipend", "Armagon's Lair"}, // 16

	{"hipdm1", "The Edge of Oblivion"} // 17
};

// PGM 01/07/97 added rogue levels
// PGM 03/02/97 added dmatch level
static level_t roguelevels[] = {{"start", "Split Decision"},   {"r1m1", "Deviant's Domain"}, {"r1m2", "Dread Portal"},		{"r1m3", "Judgement Call"},
								{"r1m4", "Cave of Death"},	   {"r1m5", "Towers of Wrath"},	 {"r1m6", "Temple of Pain"},	{"r1m7", "Tomb of the Overlord"},
								{"r2m1", "Tempus Fugit"},	   {"r2m2", "Elemental Fury I"}, {"r2m3", "Elemental Fury II"}, {"r2m4", "Curse of Osiris"},
								{"r2m5", "Wizard's Keep"},	   {"r2m6", "Blood Sacrifice"},	 {"r2m7", "Last Bastion"},		{"r2m8", "Source of Evil"},
								{"ctf1", "Division of Change"}};

typedef struct
{
	const char *description;
	int			firstLevel;
	int			levels;
} episode_t;

static episode_t episodes[] = {{"Welcome to Quake", 0, 1}, {"Doomed Dimension", 1, 8}, {"Realm of Black Magic", 9, 7}, {"Netherworld", 16, 7},
							   {"The Elder World", 23, 8}, {"Final Level", 31, 1},	   {"Deathmatch Arena", 32, 6}};

// MED 01/06/97  added hipnotic episodes
static episode_t hipnoticepisodes[] = {{"Scourge of Armagon", 0, 1}, {"Fortress of the Dead", 1, 5}, {"Dominion of Darkness", 6, 6},
									   {"The Rift", 12, 4},			 {"Final Level", 16, 1},		 {"Deathmatch Arena", 17, 1}};

// PGM 01/07/97 added rogue episodes
// PGM 03/02/97 added dmatch episode
static episode_t rogueepisodes[] = {{"Introduction", 0, 1}, {"Hell's Fortress", 1, 7}, {"Corridors of Time", 8, 8}, {"Deathmatch Arena", 16, 1}};

static int startepisode;
static int startlevel;
static int maxplayers;

static int mpgameoptions_cursor_table[] = {40, 56, 64, 72, 80, 88, 96, 112, 120};
#define NUM_MPGAMEOPTIONS 9
static int mpgameoptions_cursor;

static void M_Menu_MPGameOptions_f (void)
{
	M_MenuChanged ();
	IN_Deactivate (true);
	key_dest = key_menu;
	m_state = m_mpgameoptions;
	if (maxplayers == 0)
		maxplayers = svs.maxclients;
	if (maxplayers < 2)
		maxplayers = 4;
}

static void M_MPGameOptions_Draw (cb_context_t *cbx)
{
	qpic_t *p;

	M_DrawTransPic (cbx, 16, 4, Draw_CachePic ("gfx/qplaque.lmp"));
	p = Draw_CachePic ("gfx/p_multi.lmp");
	M_DrawPic (cbx, (320 - p->width) / 2, 4, p);

	M_DrawTextBox (cbx, 152, 32, 10, 1);
	M_Print (cbx, 160, 40, "begin game");

	M_Print (cbx, 0, 56, "      Max players");
	M_Print (cbx, 160, 56, va ("%i", maxplayers));

	M_Print (cbx, 0, 64, "        Game Type");
	if (coop.value)
		M_Print (cbx, 160, 64, "Cooperative");
	else
		M_Print (cbx, 160, 64, "Deathmatch");

	M_Print (cbx, 0, 72, "        Teamplay");
	if (rogue)
	{
		const char *msg;

		switch ((int)teamplay.value)
		{
		case 1:
			msg = "No Friendly Fire";
			break;
		case 2:
			msg = "Friendly Fire";
			break;
		case 3:
			msg = "Tag";
			break;
		case 4:
			msg = "Capture the Flag";
			break;
		case 5:
			msg = "One Flag CTF";
			break;
		case 6:
			msg = "Three Team CTF";
			break;
		default:
			msg = "Off";
			break;
		}
		M_Print (cbx, 160, 72, msg);
	}
	else
	{
		const char *msg;

		switch ((int)teamplay.value)
		{
		case 1:
			msg = "No Friendly Fire";
			break;
		case 2:
			msg = "Friendly Fire";
			break;
		default:
			msg = "Off";
			break;
		}
		M_Print (cbx, 160, 72, msg);
	}

	M_Print (cbx, 0, 80, "            Skill");
	if (skill.value == 0)
		M_Print (cbx, 160, 80, "Easy difficulty");
	else if (skill.value == 1)
		M_Print (cbx, 160, 80, "Normal difficulty");
	else if (skill.value == 2)
		M_Print (cbx, 160, 80, "Hard difficulty");
	else
		M_Print (cbx, 160, 80, "Nightmare difficulty");

	M_Print (cbx, 0, 88, "     Frag Limit");
	if (fraglimit.value == 0)
		M_Print (cbx, 160, 88, "none");
	else
		M_Print (cbx, 160, 88, va ("%i frags", (int)fraglimit.value));

	M_Print (cbx, 0, 96, "     Time Limit");
	if (timelimit.value == 0)
		M_Print (cbx, 160, 96, "none");
	else
		M_Print (cbx, 160, 96, va ("%i minutes", (int)timelimit.value));

	M_Print (cbx, 0, 112, "         Episode");
	// MED 01/06/97 added hipnotic episodes
	if (hipnotic)
		M_Print (cbx, 160, 112, hipnoticepisodes[startepisode].description);
	// PGM 01/07/97 added rogue episodes
	else if (rogue)
		M_Print (cbx, 160, 112, rogueepisodes[startepisode].description);
	else
		M_Print (cbx, 160, 112, episodes[startepisode].description);

	M_Print (cbx, 0, 120, "           Level");
	// MED 01/06/97 added hipnotic episodes
	if (hipnotic)
	{
		M_Print (cbx, 160, 120, hipnoticlevels[hipnoticepisodes[startepisode].firstLevel + startlevel].description);
		M_Print (cbx, 160, 128, hipnoticlevels[hipnoticepisodes[startepisode].firstLevel + startlevel].name);
	}
	// PGM 01/07/97 added rogue episodes
	else if (rogue)
	{
		M_Print (cbx, 160, 120, roguelevels[rogueepisodes[startepisode].firstLevel + startlevel].description);
		M_Print (cbx, 160, 128, roguelevels[rogueepisodes[startepisode].firstLevel + startlevel].name);
	}
	else
	{
		M_Print (cbx, 160, 120, levels[episodes[startepisode].firstLevel + startlevel].description);
		M_Print (cbx, 160, 128, levels[episodes[startepisode].firstLevel + startlevel].name);
	}

	// line cursor
	for (int i = 0; i < NUM_MPGAMEOPTIONS; ++i)
		M_Mouse_UpdateCursor (&mpgameoptions_cursor, 0, 400, mpgameoptions_cursor_table[i], 8, i);
	Draw_Character (cbx, 144, mpgameoptions_cursor_table[mpgameoptions_cursor], 12 + ((int)(realtime * 4) & 1));
}

static void M_NetStart_Change (int dir)
{
	int	  count;
	float f;

	switch (mpgameoptions_cursor)
	{
	case 1:
		maxplayers += dir;
		if (maxplayers > svs.maxclientslimit)
			maxplayers = svs.maxclientslimit;
		if (maxplayers < 2)
			maxplayers = 2;
		break;

	case 2:
		Cvar_Set ("coop", coop.value ? "0" : "1");
		break;

	case 3:
		count = (rogue) ? 6 : 2;
		f = teamplay.value + dir;
		if (f > count)
			f = 0;
		else if (f < 0)
			f = count;
		Cvar_SetValue ("teamplay", f);
		break;

	case 4:
		f = skill.value + dir;
		if (f > 3)
			f = 0;
		else if (f < 0)
			f = 3;
		Cvar_SetValue ("skill", f);
		break;

	case 5:
		f = fraglimit.value + dir * 10;
		if (f > 100)
			f = 0;
		else if (f < 0)
			f = 100;
		Cvar_SetValue ("fraglimit", f);
		break;

	case 6:
		f = timelimit.value + dir * 5;
		if (f > 60)
			f = 0;
		else if (f < 0)
			f = 60;
		Cvar_SetValue ("timelimit", f);
		break;

	case 7:
		startepisode += dir;
		// MED 01/06/97 added hipnotic count
		if (hipnotic)
			count = 6;
		// PGM 01/07/97 added rogue count
		// PGM 03/02/97 added 1 for dmatch episode
		else if (rogue)
			count = 4;
		else if (registered.value)
			count = 7;
		else
			count = 2;

		if (startepisode < 0)
			startepisode = count - 1;

		if (startepisode >= count)
			startepisode = 0;

		startlevel = 0;
		break;

	case 8:
		startlevel += dir;
		// MED 01/06/97 added hipnotic episodes
		if (hipnotic)
			count = hipnoticepisodes[startepisode].levels;
		// PGM 01/06/97 added hipnotic episodes
		else if (rogue)
			count = rogueepisodes[startepisode].levels;
		else
			count = episodes[startepisode].levels;

		if (startlevel < 0)
			startlevel = count - 1;

		if (startlevel >= count)
			startlevel = 0;
		break;
	}
}

static void M_MPGameOptions_Key (int key)
{
	switch (key)
	{
	case K_MOUSE2:
	case K_ESCAPE:
	case K_BBUTTON:
		M_Menu_Net_f ();
		break;

	case K_UPARROW:
		S_LocalSound ("misc/menu1.wav");
		mpgameoptions_cursor--;
		if (mpgameoptions_cursor < 0)
			mpgameoptions_cursor = NUM_MPGAMEOPTIONS - 1;
		break;

	case K_DOWNARROW:
		S_LocalSound ("misc/menu1.wav");
		mpgameoptions_cursor++;
		if (mpgameoptions_cursor >= NUM_MPGAMEOPTIONS)
			mpgameoptions_cursor = 0;
		break;

	case K_LEFTARROW:
		if (mpgameoptions_cursor == 0)
			break;
		S_LocalSound ("misc/menu3.wav");
		M_NetStart_Change (-1);
		break;

	case K_RIGHTARROW:
		if (mpgameoptions_cursor == 0)
			break;
		S_LocalSound ("misc/menu3.wav");
		M_NetStart_Change (1);
		break;

	case K_MOUSE1:
	case K_ENTER:
	case K_KP_ENTER:
	case K_ABUTTON:
		S_LocalSound ("misc/menu2.wav");
		if (mpgameoptions_cursor == 0)
		{
			if (sv.active)
				Cbuf_AddText ("disconnect\n");
			Cbuf_AddText ("listen 0\n"); // so host_netport will be re-examined
			Cbuf_AddText (va ("maxplayers %u\n", maxplayers));
			SCR_BeginLoadingPlaque ();

			if (hipnotic)
				Cbuf_AddText (va ("map %s\n", hipnoticlevels[hipnoticepisodes[startepisode].firstLevel + startlevel].name));
			else if (rogue)
				Cbuf_AddText (va ("map %s\n", roguelevels[rogueepisodes[startepisode].firstLevel + startlevel].name));
			else
				Cbuf_AddText (va ("map %s\n", levels[episodes[startepisode].firstLevel + startlevel].name));

			return;
		}

		M_NetStart_Change (1);
		break;
	}
}

//=============================================================================
/* SEARCH MENU */

static qboolean			 search_complete = false;
static double			 search_complete_time;
static enum slistScope_e search_last_scope = SLIST_LAN;

static void M_Menu_Search_f (enum slistScope_e scope)
{
	M_MenuChanged ();
	IN_Deactivate (true);
	key_dest = key_menu;
	m_state = m_search;
	slist_silent = true;
	slist_scope = search_last_scope = scope;
	search_complete = false;
	NET_Slist_f ();
}

static void M_Search_Draw (cb_context_t *cbx)
{
	qpic_t *p;
	int		x;

	p = Draw_CachePic ("gfx/p_multi.lmp");
	M_DrawPic (cbx, (320 - p->width) / 2, 4, p);
	x = (320 / 2) - ((12 * 8) / 2) + 4;
	M_DrawTextBox (cbx, x - 8, 32, 12, 1);
	M_Print (cbx, x, 40, "Searching...");

	if (slistInProgress)
	{
		NET_Poll ();
		return;
	}

	if (!search_complete)
	{
		search_complete = true;
		search_complete_time = realtime;
	}

	if (hostCacheCount)
	{
		M_Menu_ServerList_f ();
		return;
	}

	M_PrintWhite (cbx, (320 / 2) - ((22 * 8) / 2), 64, "No Quake servers found");
	if ((realtime - search_complete_time) < 3.0)
		return;

	M_Menu_LanConfig_f ();
}

static void M_Search_Key (int key) {}

//=============================================================================
/* SLIST MENU */

static int		slist_cursor;
static int		slist_first;
static qboolean slist_sorted;
#define SERVER_LIST_MAX_ON_SCREEN 21

static void M_Menu_ServerList_f (void)
{
	M_MenuChanged ();
	IN_Deactivate (true);
	key_dest = key_menu;
	m_state = m_slist;
	slist_cursor = 0;
	slist_first = 0;
	m_return_onerror = false;
	m_return_reason[0] = 0;
	slist_sorted = false;
}

static void M_ServerList_Draw (cb_context_t *cbx)
{
	size_t	n;
	qpic_t *p;

	if (!slist_sorted)
	{
		slist_sorted = true;
		NET_SlistSort ();
	}

	if (hostCacheCount > SERVER_LIST_MAX_ON_SCREEN)
		M_DrawScrollbar (cbx, 0, 40, (float)(slist_first) / (hostCacheCount - SERVER_LIST_MAX_ON_SCREEN), SERVER_LIST_MAX_ON_SCREEN - 2);
	M_Mouse_UpdateListCursor (&slist_cursor, 12, 400, 32, 8, SERVER_LIST_MAX_ON_SCREEN, slist_first);

	p = Draw_CachePic ("gfx/p_multi.lmp");
	M_DrawPic (cbx, (320 - p->width) / 2, 4, p);
	for (n = 0; n < SERVER_LIST_MAX_ON_SCREEN && n < hostCacheCount; n++)
		M_Print (cbx, 28, 32 + 8 * n, NET_SlistPrintServer (slist_first + n));
	Draw_Character (cbx, 16, 32 + (slist_cursor - slist_first) * 8, 12 + ((int)(realtime * 4) & 1));

	if (*m_return_reason)
		M_PrintWhite (cbx, 16, 148, m_return_reason);
}

static void M_ServerList_Key (int k)
{
	if (M_HandleScrollBarKeys (k, &slist_cursor, &slist_first, hostCacheCount, SERVER_LIST_MAX_ON_SCREEN))
		return;

	switch (k)
	{
	case K_MOUSE2:
	case K_ESCAPE:
	case K_BBUTTON:
		M_Menu_LanConfig_f ();
		break;

	case K_SPACE:
		M_Menu_Search_f (search_last_scope);
		break;

	case K_MOUSE1:
	case K_ENTER:
	case K_KP_ENTER:
	case K_ABUTTON:
		S_LocalSound ("misc/menu2.wav");
		m_return_state = m_state;
		m_return_onerror = true;
		slist_sorted = false;
		IN_Activate ();
		key_dest = key_game;
		m_state = m_none;
		Cbuf_AddText (va ("connect \"%s\"\n", NET_SlistPrintServerName (slist_cursor)));
		break;

	default:
		break;
	}
}

//=============================================================================
/* Credits menu -- used by the 2021 re-release */

static void M_Menu_Credits_f (void) {}

//=============================================================================
/* Menu Subsystem */

void M_Init (void)
{
	Cmd_AddCommand ("togglemenu", M_ToggleMenu_f);

	Cmd_AddCommand ("menu_main", M_Menu_Main_f);
	Cmd_AddCommand ("menu_singleplayer", M_Menu_SinglePlayer_f);
	Cmd_AddCommand ("menu_load", M_Menu_Load_f);
	Cmd_AddCommand ("menu_save", M_Menu_Save_f);
	Cmd_AddCommand ("menu_multiplayer", M_Menu_MultiPlayer_f);
	Cmd_AddCommand ("menu_setup", M_Menu_Setup_f);
	Cmd_AddCommand ("menu_options", M_Menu_Options_f);
	Cmd_AddCommand ("menu_keys", M_Menu_Keys_f);
	Cmd_AddCommand ("menu_video", M_Menu_Video_f);
	Cmd_AddCommand ("help", M_Menu_Help_f);
	Cmd_AddCommand ("menu_quit", M_Menu_Quit_f);
	Cmd_AddCommand ("menu_credits", M_Menu_Credits_f); // needed by the 2021 re-release
}

void M_NewGame (void)
{
	m_main_cursor = 0;
}

void M_UpdateMouse (void)
{
	int new_mouse_x;
	int new_mouse_y;
	SDL_GetMouseState (&new_mouse_x, &new_mouse_y);
	m_mouse_moved = !menu_changed && ((m_mouse_x_pixels != new_mouse_x) || (m_mouse_y_pixels != new_mouse_y));
	m_mouse_x_pixels = new_mouse_x;
	m_mouse_y_pixels = new_mouse_y;
	menu_changed = false;

	m_mouse_x = new_mouse_x;
	m_mouse_y = new_mouse_y;
	M_PixelToMenuCanvasCoord (&m_mouse_x, &m_mouse_y);

	if (scrollbar_grab)
	{
		if (keydown[K_MOUSE1] && M_InScrollbar ())
			M_Keydown (K_MOUSE1);
		else
			scrollbar_grab = false;
	}
	else if (slider_grab)
	{
		if (keydown[K_MOUSE1] && (m_state == m_game) && (game_options_cursor >= GAME_OPT_SCALE) && (game_options_cursor <= GAME_OPT_VIEWROLL))
			M_GameOptions_AdjustSliders (0, true);
		else if (
			keydown[K_MOUSE1] && (m_state == m_graphics) && (graphics_options_cursor >= GRAPHICS_OPT_GAMMA) && (graphics_options_cursor <= GRAPHICS_OPT_FOV))
			M_GraphicsOptions_AdjustSliders (0, true);
		else if (keydown[K_MOUSE1] && (m_state == m_sound) && (graphics_options_cursor >= SOUND_OPT_SNDVOL) && (graphics_options_cursor <= SOUND_OPT_MUSICVOL))
			M_SoundOptions_AdjustSliders (0, true);
		else
			slider_grab = false;
	}

	scrollbar_size = 0;
}

void M_Draw (cb_context_t *cbx)
{
	if (m_state == m_none || key_dest != key_menu)
		return;

	if (!m_recursiveDraw)
	{
		if (scr_con_current)
		{
			Draw_ConsoleBackground (cbx);
			S_ExtraUpdate ();
		}

		Draw_FadeScreen (cbx); // johnfitz -- fade even if console fills screen
	}
	else
	{
		m_recursiveDraw = false;
	}

	GL_SetCanvas (cbx, CANVAS_MENU); // johnfitz

	switch (m_state)
	{
	case m_none:
		break;

	case m_main:
		M_Main_Draw (cbx);
		break;

	case m_singleplayer:
		M_SinglePlayer_Draw (cbx);
		break;

	case m_load:
		M_Load_Draw (cbx);
		break;

	case m_save:
		M_Save_Draw (cbx);
		break;

	case m_multiplayer:
		M_MultiPlayer_Draw (cbx);
		break;

	case m_setup:
		M_Setup_Draw (cbx);
		break;

	case m_net:
		M_Net_Draw (cbx);
		break;

	case m_options:
		M_Options_Draw (cbx);
		break;

	case m_game:
		M_GameOptions_Draw (cbx);
		break;

	case m_keys:
		M_Keys_Draw (cbx);
		break;

	case m_video:
		M_Video_Draw (cbx);
		break;

	case m_graphics:
		M_GraphicsOptions_Draw (cbx);
		break;

	case m_sound:
		M_SoundOptions_Draw (cbx);
		break;

	case m_help:
		M_Help_Draw (cbx);
		break;

	case m_mods:
		M_Mods_Draw (cbx);
		break;

	case m_quit:
		if (!fitzmode)
		{ /* QuakeSpasm customization: */
			/* Quit now! S.A. */
			m_is_quitting = true;
			key_dest = key_console;
			Cbuf_InsertText ("quit");
		}
		else
			M_Quit_Draw (cbx);
		break;

	case m_lanconfig:
		M_LanConfig_Draw (cbx);
		break;

	case m_mpgameoptions:
		M_MPGameOptions_Draw (cbx);
		break;

	case m_search:
		M_Search_Draw (cbx);
		break;

	case m_slist:
		M_ServerList_Draw (cbx);
		break;
	}

	if (m_entersound)
	{
		S_LocalSound ("misc/menu2.wav");
		m_entersound = false;
	}

	S_ExtraUpdate ();
}

void M_Keydown (int key)
{
	switch (m_state)
	{
	case m_none:
		return;

	case m_main:
		M_Main_Key (key);
		return;

	case m_singleplayer:
		M_SinglePlayer_Key (key);
		return;

	case m_load:
		M_Load_Key (key);
		return;

	case m_save:
		M_Save_Key (key);
		return;

	case m_multiplayer:
		M_MultiPlayer_Key (key);
		return;

	case m_setup:
		M_Setup_Key (key);
		return;

	case m_net:
		M_Net_Key (key);
		return;

	case m_options:
		M_Options_Key (key);
		return;

	case m_game:
		M_GameOptions_Key (key);
		return;

	case m_graphics:
		M_GraphicsOptions_Key (key);
		return;

	case m_mods:
		M_Mods_Key (key);
		break;

	case m_keys:
		M_Keys_Key (key);
		return;

	case m_video:
		M_Video_Key (key);
		return;

	case m_sound:
		M_SoundOptions_Key (key);
		return;

	case m_help:
		M_Help_Key (key);
		return;

	case m_quit:
		M_Quit_Key (key);
		return;

	case m_lanconfig:
		M_LanConfig_Key (key);
		return;

	case m_mpgameoptions:
		M_MPGameOptions_Key (key);
		return;

	case m_search:
		M_Search_Key (key);
		break;

	case m_slist:
		M_ServerList_Key (key);
		return;
	}
}

void M_Charinput (int key)
{
	switch (m_state)
	{
	case m_setup:
		M_Setup_Char (key);
		return;
	case m_quit:
		M_Quit_Char (key);
		return;
	case m_lanconfig:
		M_LanConfig_Char (key);
		return;
	default:
		return;
	}
}

qboolean M_TextEntry (void)
{
	switch (m_state)
	{
	case m_setup:
		return M_Setup_TextEntry ();
	case m_quit:
		return M_Quit_TextEntry ();
	case m_lanconfig:
		return M_LanConfig_TextEntry ();
	default:
		return false;
	}
}

void M_ConfigureNetSubsystem (void)
{
	// enable/disable net systems to match desired config
	Cbuf_AddText ("stopdemo\n");

	if (IPXConfig || TCPIPConfig)
		net_hostport = lan_config_port;
}
