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
	m_video,
	m_keys,
	m_help,
	m_quit,
	m_lanconfig,
	m_gameoptions,
	m_search,
	m_slist,
	m_mods,
};

extern enum m_state_e m_state;
extern enum m_state_e m_return_state;

extern qboolean m_entersound;

//
// menus
//
void	 M_Init (void);
void	 M_NewGame (void);
void	 M_Keydown (int key);
void	 M_Charinput (int key);
qboolean M_TextEntry (void);
void	 M_ToggleMenu_f (void);

void M_Menu_Main_f (void);
void M_Menu_Options_f (void);
void M_Menu_Quit_f (void);

void M_Print (cb_context_t *cbx, int cx, int cy, const char *str);
void M_PrintWhite (cb_context_t *cbx, int cx, int cy, const char *str);
void M_DrawSlider (cb_context_t *cbx, int x, int y, float range);

void M_Draw (cb_context_t *cbx);
void M_DrawCharacter (cb_context_t *cbx, int cx, int line, int num);

void M_DrawPic (cb_context_t *cbx, int x, int y, qpic_t *pic);
void M_DrawTransPic (cb_context_t *cbx, int x, int y, qpic_t *pic);
void M_DrawCheckbox (cb_context_t *cbx, int x, int y, int on);

void M_Mouse_UpdateListCursor (int *cursor, int left, int right, int top, int item_height, int num_items, int scroll_offset);
void M_Mouse_UpdateCursor (int *cursor, int left, int right, int top, int item_height, int index);

#endif /* _QUAKE_MENU_H */
