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

#ifndef _QUAKE_MENU_H
#define _QUAKE_MENU_H

enum m_state_e
{
	m_none,
	m_main,
	m_singleplayer,
	m_load,
	m_save,
	m_multiplayer,
	m_setup,
	m_net,
	m_options,
	m_game,
	m_sound,
	m_video,
	m_graphics,
	m_keys,
	m_help,
	m_quit,
	m_lanconfig,
	m_mpgameoptions,
	m_search,
	m_slist,
	m_mods,
	m_maps,
	m_skill,
};

extern enum m_state_e m_state;
extern enum m_state_e m_return_state;

extern qboolean m_entersound;

extern qboolean m_is_quitting;

//
// menus
//
void	 M_Init (void);
void	 M_NewGame (void);
void	 M_Keydown (int key);
void	 M_Charinput (int key);
qboolean M_TextEntry (void);
qboolean M_WaitingForKeyBinding (void);
void	 M_ToggleMenu_f (void);
float	 M_GetScale ();
void	 M_UpdateMouse ();
void	 M_MenuChanged ();

void M_Menu_Main_f (void);
void M_Menu_Options_f (void);
void M_Menu_Quit_f (void);

void M_Print (cb_context_t *cbx, int cx, int cy, const char *str);

void M_Draw (cb_context_t *cbx);

void M_DrawPic (cb_context_t *cbx, int x, int y, qpic_t *pic);
void M_DrawTransPic (cb_context_t *cbx, int x, int y, qpic_t *pic);

void M_Mouse_UpdateCursor (int *cursor, int left, int right, int top, int item_height, int index);

void	 M_Menu_Video_f (void);
void	 M_Video_Draw (cb_context_t *cbx);
void	 M_Video_Key (int key);
qboolean M_HandleScrollBarKeys (const int key, int *cursor, int *first_drawn, const int num_total, const int max_on_screen);

#define MENU_TOP		 40
#define MENU_CURSOR_X	 60
#define MENU_LABEL_X	 70
#define MENU_VALUE_X	 (27 * CHARACTER_SIZE)
#define MENU_SLIDER_X	 (MENU_VALUE_X + 6)
#define MENU_SCROLLBAR_X (46 * CHARACTER_SIZE) // make some room for slider labels
#define MAX_MENU_LINES	 14

// Max FPS menu entry is a slider [10 .. 1000 OR no limit] by steps of 2 fps so we can set 72 fps
#define MIN_FPS_MENU_VALUE	MIN_HOST_MAXFPS
#define FPS_MENU_VALUE_STEP 2.0f
#define MAX_FPS_MENU_VALUE	MAX_HOST_MAXFPS

typedef struct crosshair_s
{
	const char *name;
	char		crosshair_char;
	float		viewport_x_offset;
	float		viewport_y_offset;
	int			menu_x_offset;
	int			menu_y_offset;
	const char *pic_path; // NULL for the legacy character crosshairs
} crosshair_t;

crosshair_t M_GetCrosshairDef (float crosshair_def_value);
const char *M_GetCrosshairColorName (float crosshair_color_value);
void		M_GetCrosshairColor (float crosshair_color_value, float *rgb);
void		M_DrawCrosshair (cb_context_t *cbx, float x, float y, float size);

#endif /* _QUAKE_MENU_H */
