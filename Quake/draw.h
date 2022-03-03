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

#ifndef _QUAKE_DRAW_H
#define _QUAKE_DRAW_H

// draw.h -- these are the only functions outside the refresh allowed
// to touch the vid buffer

extern qpic_t *draw_disc; // also used on sbar

void    Draw_Init (void);
void    Draw_Character (int x, int y, int num);
void    Draw_Pic (int x, int y, qpic_t *pic, float alpha, qboolean alpha_blend);
void    Draw_SubPic (float x, float y, float w, float h, qpic_t *pic, float s1, float t1, float s2, float t2, float *rgb, float alpha);
void    Draw_TransPicTranslate (int x, int y, qpic_t *pic, int top, int bottom); // johnfitz -- more parameters
void    Draw_ConsoleBackground (void);                                           // johnfitz -- removed parameter int lines
void    Draw_TileClear (int x, int y, int w, int h);
void    Draw_Fill (int x, int y, int w, int h, int c, float alpha); // johnfitz -- added alpha
void    Draw_FadeScreen (void);
void    Draw_String (int x, int y, const char *str);
qpic_t *Draw_PicFromWad2 (const char *name, unsigned int texflags);
qpic_t *Draw_PicFromWad (const char *name);
qpic_t *Draw_CachePic (const char *path);
qpic_t *Draw_TryCachePic (const char *path, unsigned int texflags);
void    Draw_NewGame (void);

void GL_Viewport (float x, float y, float width, float height, float min_depth, float max_depth);
void GL_SetCanvas (canvastype newcanvas); // johnfitz

#endif /* _QUAKE_DRAW_H */
