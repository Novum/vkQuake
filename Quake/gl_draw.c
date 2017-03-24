/*
Copyright (C) 1996-2001 Id Software, Inc.
Copyright (C) 2002-2009 John Fitzgibbons and others
Copyright (C) 2007-2008 Kristian Duske
Copyright (C) 2010-2014 QuakeSpasm developers
Copyright (C) 2016 Axel Gneiting

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

// draw.c -- 2d drawing

#include "quakedef.h"

cvar_t		scr_conalpha = {"scr_conalpha", "0.5", CVAR_ARCHIVE}; //johnfitz

qpic_t		*draw_disc;
qpic_t		*draw_backtile;

gltexture_t *char_texture; //johnfitz
qpic_t		*pic_ovr, *pic_ins; //johnfitz -- new cursor handling
qpic_t		*pic_nul; //johnfitz -- for missing gfx, don't crash

//johnfitz -- new pics
byte pic_ovr_data[8][8] =
{
	{255,255,255,255,255,255,255,255},
	{255, 15, 15, 15, 15, 15, 15,255},
	{255, 15, 15, 15, 15, 15, 15,  2},
	{255, 15, 15, 15, 15, 15, 15,  2},
	{255, 15, 15, 15, 15, 15, 15,  2},
	{255, 15, 15, 15, 15, 15, 15,  2},
	{255, 15, 15, 15, 15, 15, 15,  2},
	{255,255,  2,  2,  2,  2,  2,  2},
};

byte pic_ins_data[9][8] =
{
	{ 15, 15,255,255,255,255,255,255},
	{ 15, 15,  2,255,255,255,255,255},
	{ 15, 15,  2,255,255,255,255,255},
	{ 15, 15,  2,255,255,255,255,255},
	{ 15, 15,  2,255,255,255,255,255},
	{ 15, 15,  2,255,255,255,255,255},
	{ 15, 15,  2,255,255,255,255,255},
	{ 15, 15,  2,255,255,255,255,255},
	{255,  2,  2,255,255,255,255,255},
};

byte pic_nul_data[8][8] =
{
	{252,252,252,252,  0,  0,  0,  0},
	{252,252,252,252,  0,  0,  0,  0},
	{252,252,252,252,  0,  0,  0,  0},
	{252,252,252,252,  0,  0,  0,  0},
	{  0,  0,  0,  0,252,252,252,252},
	{  0,  0,  0,  0,252,252,252,252},
	{  0,  0,  0,  0,252,252,252,252},
	{  0,  0,  0,  0,252,252,252,252},
};

byte pic_stipple_data[8][8] =
{
	{255,  0,  0,  0,255,  0,  0,  0},
	{  0,  0,255,  0,  0,  0,255,  0},
	{255,  0,  0,  0,255,  0,  0,  0},
	{  0,  0,255,  0,  0,  0,255,  0},
	{255,  0,  0,  0,255,  0,  0,  0},
	{  0,  0,255,  0,  0,  0,255,  0},
	{255,  0,  0,  0,255,  0,  0,  0},
	{  0,  0,255,  0,  0,  0,255,  0},
};

byte pic_crosshair_data[8][8] =
{
	{255,255,255,255,255,255,255,255},
	{255,255,255,  8,  9,255,255,255},
	{255,255,255,  6,  8,  2,255,255},
	{255,  6,  8,  8,  6,  8,  8,255},
	{255,255,  2,  8,  8,  2,  2,  2},
	{255,255,255,  7,  8,  2,255,255},
	{255,255,255,255,  2,  2,255,255},
	{255,255,255,255,255,255,255,255},
};
//johnfitz

typedef struct
{
	gltexture_t *gltexture;
	float		sl, tl, sh, th;
} glpic_t;

canvastype currentcanvas = CANVAS_NONE; //johnfitz -- for GL_SetCanvas

//==============================================================================
//
//  PIC CACHING
//
//==============================================================================

typedef struct cachepic_s
{
	char		name[MAX_QPATH];
	qpic_t		pic;
	byte		padding[32];	// for appended glpic
} cachepic_t;

#define	MAX_CACHED_PICS		128
cachepic_t	menu_cachepics[MAX_CACHED_PICS];
int			menu_numcachepics;

byte		menuplyr_pixels[4096];

//  scrap allocation
//  Allocate all the little status bar obejcts into a single texture
//  to crutch up stupid hardware / drivers

#define	MAX_SCRAPS		2
#define	BLOCK_WIDTH		256
#define	BLOCK_HEIGHT	256

int			scrap_allocated[MAX_SCRAPS][BLOCK_WIDTH];
byte		scrap_texels[MAX_SCRAPS][BLOCK_WIDTH*BLOCK_HEIGHT]; //johnfitz -- removed *4 after BLOCK_HEIGHT
qboolean	scrap_dirty;
gltexture_t	*scrap_textures[MAX_SCRAPS]; //johnfitz


/*
================
Scrap_AllocBlock

returns an index into scrap_texnums[] and the position inside it
================
*/
int Scrap_AllocBlock (int w, int h, int *x, int *y)
{
	int		i, j;
	int		best, best2;
	int		texnum;

	for (texnum=0 ; texnum<MAX_SCRAPS ; texnum++)
	{
		best = BLOCK_HEIGHT;

		for (i=0 ; i<BLOCK_WIDTH-w ; i++)
		{
			best2 = 0;

			for (j=0 ; j<w ; j++)
			{
				if (scrap_allocated[texnum][i+j] >= best)
					break;
				if (scrap_allocated[texnum][i+j] > best2)
					best2 = scrap_allocated[texnum][i+j];
			}
			if (j == w)
			{	// this is a valid spot
				*x = i;
				*y = best = best2;
			}
		}

		if (best + h > BLOCK_HEIGHT)
			continue;

		for (i=0 ; i<w ; i++)
			scrap_allocated[texnum][*x + i] = best + h;

		return texnum;
	}

	Sys_Error ("Scrap_AllocBlock: full");
	return 0;
}

/*
================
Scrap_Upload -- johnfitz -- now uses TexMgr
================
*/
void Scrap_Upload (void)
{
	char name[8];
	int	i;

	for (i=0; i<MAX_SCRAPS; i++)
	{
		sprintf (name, "scrap%i", i);
		scrap_textures[i] = TexMgr_LoadImage (NULL, name, BLOCK_WIDTH, BLOCK_HEIGHT, SRC_INDEXED, scrap_texels[i],
			"", (src_offset_t)scrap_texels[i], TEXPREF_ALPHA | TEXPREF_OVERWRITE | TEXPREF_NOPICMIP);
	}

	scrap_dirty = false;
}

/*
================
Draw_PicFromWad
================
*/
qpic_t *Draw_PicFromWad (const char *name)
{
	qpic_t	*p;
	glpic_t	gl;
	src_offset_t offset; //johnfitz

	p = (qpic_t *) W_GetLumpName (name);
	if (!p) return pic_nul; //johnfitz

	// load little ones into the scrap
	if (p->width < 64 && p->height < 64)
	{
		int		x, y;
		int		i, j, k;
		int		texnum;

		texnum = Scrap_AllocBlock (p->width, p->height, &x, &y);
		scrap_dirty = true;
		k = 0;
		for (i=0 ; i<p->height ; i++)
		{
			for (j=0 ; j<p->width ; j++, k++)
				scrap_texels[texnum][(y+i)*BLOCK_WIDTH + x + j] = p->data[k];
		}
		gl.gltexture = scrap_textures[texnum]; //johnfitz -- changed to an array
		//johnfitz -- no longer go from 0.01 to 0.99
		gl.sl = x/(float)BLOCK_WIDTH;
		gl.sh = (x+p->width)/(float)BLOCK_WIDTH;
		gl.tl = y/(float)BLOCK_WIDTH;
		gl.th = (y+p->height)/(float)BLOCK_WIDTH;
	}
	else
	{
		char texturename[64]; //johnfitz
		q_snprintf (texturename, sizeof(texturename), "%s:%s", WADFILENAME, name); //johnfitz

		offset = (src_offset_t)p - (src_offset_t)wad_base + sizeof(int)*2; //johnfitz

		gl.gltexture = TexMgr_LoadImage (NULL, texturename, p->width, p->height, SRC_INDEXED, p->data, WADFILENAME,
										  offset, TEXPREF_ALPHA | TEXPREF_PAD | TEXPREF_NOPICMIP); //johnfitz -- TexMgr
		gl.sl = 0;
		gl.sh = (float)p->width/(float)TexMgr_PadConditional(p->width); //johnfitz
		gl.tl = 0;
		gl.th = (float)p->height/(float)TexMgr_PadConditional(p->height); //johnfitz
	}

	memcpy (p->data, &gl, sizeof(glpic_t));

	return p;
}

/*
================
Draw_CachePic
================
*/
qpic_t	*Draw_CachePic (const char *path)
{
	cachepic_t	*pic;
	int			i;
	qpic_t		*dat;
	glpic_t		gl;

	for (pic=menu_cachepics, i=0 ; i<menu_numcachepics ; pic++, i++)
	{
		if (!strcmp (path, pic->name))
			return &pic->pic;
	}
	if (menu_numcachepics == MAX_CACHED_PICS)
		Sys_Error ("menu_numcachepics == MAX_CACHED_PICS");
	menu_numcachepics++;
	strcpy (pic->name, path);

//
// load the pic from disk
//
	dat = (qpic_t *)COM_LoadTempFile (path, NULL);
	if (!dat)
		Sys_Error ("Draw_CachePic: failed to load %s", path);
	SwapPic (dat);

	// HACK HACK HACK --- we need to keep the bytes for
	// the translatable player picture just for the menu
	// configuration dialog
	if (!strcmp (path, "gfx/menuplyr.lmp"))
		memcpy (menuplyr_pixels, dat->data, dat->width*dat->height);

	pic->pic.width = dat->width;
	pic->pic.height = dat->height;

	gl.gltexture = TexMgr_LoadImage (NULL, path, dat->width, dat->height, SRC_INDEXED, dat->data, path,
									  sizeof(int)*2, TEXPREF_ALPHA | TEXPREF_PAD | TEXPREF_NOPICMIP); //johnfitz -- TexMgr
	gl.sl = 0;
	gl.sh = (float)dat->width/(float)TexMgr_PadConditional(dat->width); //johnfitz
	gl.tl = 0;
	gl.th = (float)dat->height/(float)TexMgr_PadConditional(dat->height); //johnfitz
	memcpy (pic->pic.data, &gl, sizeof(glpic_t));

	return &pic->pic;
}

/*
================
Draw_MakePic -- johnfitz -- generate pics from internal data
================
*/
qpic_t *Draw_MakePic (const char *name, int width, int height, byte *data)
{
	int flags = TEXPREF_NEAREST | TEXPREF_ALPHA | TEXPREF_PERSIST | TEXPREF_NOPICMIP | TEXPREF_PAD;
	qpic_t		*pic;
	glpic_t		gl;

	pic = (qpic_t *) Hunk_Alloc (sizeof(qpic_t) - 4 + sizeof (glpic_t));
	pic->width = width;
	pic->height = height;

	gl.gltexture = TexMgr_LoadImage (NULL, name, width, height, SRC_INDEXED, data, "", (src_offset_t)data, flags);
	gl.sl = 0;
	gl.sh = (float)width/(float)TexMgr_PadConditional(width);
	gl.tl = 0;
	gl.th = (float)height/(float)TexMgr_PadConditional(height);
	memcpy (pic->data, &gl, sizeof(glpic_t));

	return pic;
}

//==============================================================================
//
//  INIT
//
//==============================================================================

/*
===============
Draw_LoadPics -- johnfitz
===============
*/
void Draw_LoadPics (void)
{
	byte		*data;
	src_offset_t	offset;

	data = (byte *) W_GetLumpName ("conchars");
	if (!data) Sys_Error ("Draw_LoadPics: couldn't load conchars");
	offset = (src_offset_t)data - (src_offset_t)wad_base;
	char_texture = TexMgr_LoadImage (NULL, WADFILENAME":conchars", 128, 128, SRC_INDEXED, data,
		WADFILENAME, offset, TEXPREF_ALPHA | TEXPREF_NEAREST | TEXPREF_NOPICMIP | TEXPREF_CONCHARS);

	draw_disc = Draw_PicFromWad ("disc");
	draw_backtile = Draw_PicFromWad ("backtile");
}

/*
===============
Draw_NewGame -- johnfitz
===============
*/
void Draw_NewGame (void)
{
	cachepic_t	*pic;
	int			i;

	// empty scrap and reallocate gltextures
	memset(&scrap_allocated, 0, sizeof(scrap_allocated));
	memset(&scrap_texels, 255, sizeof(scrap_texels));
	Scrap_Upload (); //creates 2 empty gltextures

	// reload wad pics
	W_LoadWadFile (); //johnfitz -- filename is now hard-coded for honesty
	Draw_LoadPics ();
	SCR_LoadPics ();
	Sbar_LoadPics ();

	// empty lmp cache
	for (pic = menu_cachepics, i = 0; i < menu_numcachepics; pic++, i++)
		pic->name[0] = 0;
	menu_numcachepics = 0;
}

/*
===============
Draw_Init -- johnfitz -- rewritten
===============
*/
void Draw_Init (void)
{
	Cvar_RegisterVariable (&scr_conalpha);

	// clear scrap and allocate gltextures
	memset(&scrap_allocated, 0, sizeof(scrap_allocated));
	memset(&scrap_texels, 255, sizeof(scrap_texels));
	Scrap_Upload (); //creates 2 empty textures

	// create internal pics
	pic_ins = Draw_MakePic ("ins", 8, 9, &pic_ins_data[0][0]);
	pic_ovr = Draw_MakePic ("ovr", 8, 8, &pic_ovr_data[0][0]);
	pic_nul = Draw_MakePic ("nul", 8, 8, &pic_nul_data[0][0]);

	// load game pics
	Draw_LoadPics ();
}

//==============================================================================
//
//  2D DRAWING
//
//==============================================================================

/*
================
Draw_FillCharacterQuad
================
*/
void Draw_FillCharacterQuad (int x, int y, char num, basicvertex_t * output)
{
	int				row, col;
	float			frow, fcol, size;

	row = num>>4;
	col = num&15;

	frow = row*0.0625;
	fcol = col*0.0625;
	size = 0.0625;

	basicvertex_t corner_verts[4];
	memset(&corner_verts, 255, sizeof(corner_verts));

	corner_verts[0].position[0] = x;
	corner_verts[0].position[1] = y;
	corner_verts[0].position[2] = 0.0f;
	corner_verts[0].texcoord[0] = fcol;
	corner_verts[0].texcoord[1] = frow;

	corner_verts[1].position[0] = x+8;
	corner_verts[1].position[1] = y;
	corner_verts[1].position[2] = 0.0f;
	corner_verts[1].texcoord[0] = fcol + size;
	corner_verts[1].texcoord[1] = frow;

	corner_verts[2].position[0] = x+8;
	corner_verts[2].position[1] = y+8;
	corner_verts[2].position[2] = 0.0f;
	corner_verts[2].texcoord[0] = fcol + size;
	corner_verts[2].texcoord[1] = frow + size;

	corner_verts[3].position[0] = x;
	corner_verts[3].position[1] = y+8;
	corner_verts[3].position[2] = 0.0f;
	corner_verts[3].texcoord[0] = fcol;
	corner_verts[3].texcoord[1] = frow + size;

	output[0] = corner_verts[0];
	output[1] = corner_verts[1];
	output[2] = corner_verts[2];
	output[3] = corner_verts[2];
	output[4] = corner_verts[3];
	output[5] = corner_verts[0];
}

/*
================
Draw_Character
================
*/
void Draw_Character (int x, int y, int num)
{
	if (y <= -8)
		return;			// totally off screen

	num &= 255;

	if (num == 32)
		return; //don't waste verts on spaces

	VkBuffer buffer;
	VkDeviceSize buffer_offset;
	basicvertex_t * vertices = (basicvertex_t*)R_VertexAllocate(6 * sizeof(basicvertex_t), &buffer, &buffer_offset);

	Draw_FillCharacterQuad(x, y, (char)num, vertices);

	vkCmdBindVertexBuffers(vulkan_globals.command_buffer, 0, 1, &buffer, &buffer_offset);
	vkCmdBindPipeline(vulkan_globals.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_globals.basic_alphatest_pipeline[render_pass_index]);
	vkCmdBindDescriptorSets(vulkan_globals.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_globals.basic_pipeline_layout, 0, 1, &char_texture->descriptor_set, 0, NULL);
	vkCmdDraw(vulkan_globals.command_buffer, 6, 1, 0, 0);
}

/*
================
Draw_String
================
*/
void Draw_String (int x, int y, const char *str)
{
	int num_verts = 0;
	int i;
	const char *tmp;

	if (y <= -8)
		return;			// totally off screen

	for (tmp = str; *tmp != 0; ++tmp)
		if (*tmp != 32)
			num_verts += 6;

	VkBuffer buffer;
	VkDeviceSize buffer_offset;
	basicvertex_t * vertices = (basicvertex_t*)R_VertexAllocate(num_verts * sizeof(basicvertex_t), &buffer, &buffer_offset);

	for (i = 0; *str != 0; ++str)
	{
		if (*str != 32)
		{
			Draw_FillCharacterQuad(x, y, *str, vertices + i * 6);
			i++;
		}
		x += 8;
	}

	vkCmdBindVertexBuffers(vulkan_globals.command_buffer, 0, 1, &buffer, &buffer_offset);
	vkCmdBindPipeline(vulkan_globals.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_globals.basic_alphatest_pipeline[render_pass_index]);
	vkCmdBindDescriptorSets(vulkan_globals.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_globals.basic_pipeline_layout, 0, 1, &char_texture->descriptor_set, 0, NULL);
	vkCmdDraw(vulkan_globals.command_buffer, num_verts, 1, 0, 0);
}

/*
=============
Draw_Pic -- johnfitz -- modified
=============
*/
void Draw_Pic (int x, int y, qpic_t *pic, float alpha)
{
	glpic_t			*gl;
	int	i;

	if (scrap_dirty)
		Scrap_Upload ();
	gl = (glpic_t *)pic->data;

	VkBuffer buffer;
	VkDeviceSize buffer_offset;
	basicvertex_t * vertices = (basicvertex_t*)R_VertexAllocate(6 * sizeof(basicvertex_t), &buffer, &buffer_offset);

	basicvertex_t corner_verts[4];
	memset(&corner_verts, 255, sizeof(corner_verts));

	corner_verts[0].position[0] = x;
	corner_verts[0].position[1] = y;
	corner_verts[0].position[2] = 0.0f;
	corner_verts[0].texcoord[0] = gl->sl;
	corner_verts[0].texcoord[1] = gl->tl;

	corner_verts[1].position[0] = x+pic->width;
	corner_verts[1].position[1] = y;
	corner_verts[1].position[2] = 0.0f;
	corner_verts[1].texcoord[0] = gl->sh;
	corner_verts[1].texcoord[1] = gl->tl;

	corner_verts[2].position[0] = x+pic->width;
	corner_verts[2].position[1] = y+pic->height;
	corner_verts[2].position[2] = 0.0f;
	corner_verts[2].texcoord[0] = gl->sh;
	corner_verts[2].texcoord[1] = gl->th;

	corner_verts[3].position[0] = x;
	corner_verts[3].position[1] = y+pic->height;
	corner_verts[3].position[2] = 0.0f;
	corner_verts[3].texcoord[0] = gl->sl;
	corner_verts[3].texcoord[1] = gl->th;

	for (i = 0; i<4; ++i)
		corner_verts[i].color[3] = alpha * 255.0f;

	vertices[0] = corner_verts[0];
	vertices[1] = corner_verts[1];
	vertices[2] = corner_verts[2];
	vertices[3] = corner_verts[2];
	vertices[4] = corner_verts[3];
	vertices[5] = corner_verts[0];

	vkCmdBindVertexBuffers(vulkan_globals.command_buffer, 0, 1, &buffer, &buffer_offset);
	vkCmdBindPipeline(vulkan_globals.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_globals.basic_blend_pipeline[render_pass_index]);
	vkCmdBindDescriptorSets(vulkan_globals.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_globals.basic_pipeline_layout, 0, 1, &gl->gltexture->descriptor_set, 0, NULL);
	vkCmdDraw(vulkan_globals.command_buffer, 6, 1, 0, 0);
}

/*
=============
Draw_TransPicTranslate -- johnfitz -- rewritten to use texmgr to do translation

Only used for the player color selection menu
=============
*/
void Draw_TransPicTranslate (int x, int y, qpic_t *pic, int top, int bottom)
{
	static int oldtop = -2;
	static int oldbottom = -2;

	if (top != oldtop || bottom != oldbottom)
	{
		glpic_t *p = (glpic_t *)pic->data;
		gltexture_t *glt = p->gltexture;
		oldtop = top;
		oldbottom = bottom;
		TexMgr_ReloadImage (glt, top, bottom);
	}
	Draw_Pic (x, y, pic, 1.0f);
}

/*
================
Draw_ConsoleBackground -- johnfitz -- rewritten
================
*/
void Draw_ConsoleBackground (void)
{
	qpic_t *pic;
	float alpha;

	pic = Draw_CachePic ("gfx/conback.lmp");
	pic->width = vid.conwidth;
	pic->height = vid.conheight;

	alpha = (con_forcedup) ? 1.0 : scr_conalpha.value;

	GL_SetCanvas (CANVAS_CONSOLE); //in case this is called from weird places

	if (alpha > 0.0)
	{
		Draw_Pic (0, 0, pic, alpha);
	}
}


/*
=============
Draw_TileClear

This repeats a 64*64 tile graphic to fill the screen around a sized down
refresh window.
=============
*/
void Draw_TileClear (int x, int y, int w, int h)
{
	glpic_t	*gl;

	gl = (glpic_t *)draw_backtile->data;
	
	VkBuffer buffer;
	VkDeviceSize buffer_offset;
	basicvertex_t * vertices = (basicvertex_t*)R_VertexAllocate(6 * sizeof(basicvertex_t), &buffer, &buffer_offset);

	basicvertex_t corner_verts[4];
	memset(&corner_verts, 255, sizeof(corner_verts));

	corner_verts[0].position[0] = x;
	corner_verts[0].position[1] = y;
	corner_verts[0].position[2] = 0.0f;
	corner_verts[0].texcoord[0] = x/64.0;
	corner_verts[0].texcoord[1] = y/64.0;

	corner_verts[1].position[0] = x+w;
	corner_verts[1].position[1] = y;
	corner_verts[1].position[2] = 0.0f;
	corner_verts[1].texcoord[0] = (x+w)/64.0;
	corner_verts[1].texcoord[1] = y/64.0;

	corner_verts[2].position[0] = x+w;
	corner_verts[2].position[1] = y+h;
	corner_verts[2].position[2] = 0.0f;
	corner_verts[2].texcoord[0] = (x+w)/64.0;
	corner_verts[2].texcoord[1] = (y+h)/64.0;

	corner_verts[3].position[0] = x;
	corner_verts[3].position[1] = y+h;
	corner_verts[3].position[2] = 0.0f;
	corner_verts[3].texcoord[0] = x/64.0;
	corner_verts[3].texcoord[1] = (y+h)/64.0;

	vertices[0] = corner_verts[0];
	vertices[1] = corner_verts[1];
	vertices[2] = corner_verts[2];
	vertices[3] = corner_verts[2];
	vertices[4] = corner_verts[3];
	vertices[5] = corner_verts[0];

	vkCmdBindDescriptorSets(vulkan_globals.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_globals.basic_pipeline_layout, 0, 1, &gl->gltexture->descriptor_set, 0, NULL);
	vkCmdBindVertexBuffers(vulkan_globals.command_buffer, 0, 1, &buffer, &buffer_offset);
	vkCmdBindPipeline(vulkan_globals.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_globals.basic_blend_pipeline[render_pass_index]);
	vkCmdDraw(vulkan_globals.command_buffer, 6, 1, 0, 0);
}

/*
=============
Draw_Fill

Fills a box of pixels with a single color
=============
*/
void Draw_Fill (int x, int y, int w, int h, int c, float alpha) //johnfitz -- added alpha
{
	int i;
	byte *pal = (byte *)d_8to24table; //johnfitz -- use d_8to24table instead of host_basepal

	VkBuffer buffer;
	VkDeviceSize buffer_offset;
	basicvertex_t * vertices = (basicvertex_t*)R_VertexAllocate(6 * sizeof(basicvertex_t), &buffer, &buffer_offset);

	basicvertex_t corner_verts[4];
	memset(&corner_verts, 0, sizeof(corner_verts));

	corner_verts[0].position[0] = 0.0f;
	corner_verts[0].position[1] = 0.0f;

	corner_verts[1].position[0] = glwidth;
	corner_verts[1].position[1] = 0.0f;

	corner_verts[2].position[0] = glwidth;
	corner_verts[2].position[1] = glheight;

	corner_verts[3].position[0] = 0.0f;
	corner_verts[3].position[1] = glheight;


	for (i = 0; i < 4; ++i)
	{
		corner_verts[i].color[0] = pal[c*4]/255.0;
		corner_verts[i].color[1] = pal[c*4+1]/255.0;
		corner_verts[i].color[2] = pal[c*4+2]/255.0;
		corner_verts[i].color[3] = alpha;
	}

	vertices[0] = corner_verts[0];
	vertices[1] = corner_verts[1];
	vertices[2] = corner_verts[2];
	vertices[3] = corner_verts[2];
	vertices[4] = corner_verts[3];
	vertices[5] = corner_verts[0];

	vkCmdBindVertexBuffers(vulkan_globals.command_buffer, 0, 1, &buffer, &buffer_offset);
	vkCmdBindPipeline(vulkan_globals.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_globals.basic_notex_blend_pipeline[render_pass_index]);
	vkCmdDraw(vulkan_globals.command_buffer, 6, 1, 0, 0);
}

/*
================
Draw_FadeScreen
================
*/
void Draw_FadeScreen (void)
{
	int i;

	GL_SetCanvas (CANVAS_DEFAULT);

	VkBuffer buffer;
	VkDeviceSize buffer_offset;
	basicvertex_t * vertices = (basicvertex_t*)R_VertexAllocate(6 * sizeof(basicvertex_t), &buffer, &buffer_offset);

	basicvertex_t corner_verts[4];
	memset(&corner_verts, 0, sizeof(corner_verts));

	corner_verts[0].position[0] = 0.0f;
	corner_verts[0].position[1] = 0.0f;

	corner_verts[1].position[0] = glwidth;
	corner_verts[1].position[1] = 0.0f;

	corner_verts[2].position[0] = glwidth;
	corner_verts[2].position[1] = glheight;

	corner_verts[3].position[0] = 0.0f;
	corner_verts[3].position[1] = glheight;

	for (i = 0; i < 4; ++i)
		corner_verts[i].color[3] = 128;

	vertices[0] = corner_verts[0];
	vertices[1] = corner_verts[1];
	vertices[2] = corner_verts[2];
	vertices[3] = corner_verts[2];
	vertices[4] = corner_verts[3];
	vertices[5] = corner_verts[0];

	vkCmdBindVertexBuffers(vulkan_globals.command_buffer, 0, 1, &buffer, &buffer_offset);
	vkCmdBindPipeline(vulkan_globals.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_globals.basic_notex_blend_pipeline[render_pass_index]);
	vkCmdDraw(vulkan_globals.command_buffer, 6, 1, 0, 0);

	Sbar_Changed();
}

/*
================
GL_OrthoMatrix
================
*/
static void GL_OrthoMatrix(float left, float right, float bottom, float top, float n, float f)
{
	float tx = -(right + left) / (right - left);
	float ty = (top + bottom) / (top - bottom);
	float tz = -(f + n) / (f - n);

	float matrix[16];
	memset(&matrix, 0, sizeof(matrix));

	// First column
	matrix[0*4 + 0] = 2.0f / (right-left);

	// Second column
	matrix[1*4 + 1] = -2.0f / (top-bottom);
	
	// Third column
	matrix[2*4 + 2] = -2.0f / (f-n);

	// Fourth column
	matrix[3*4 + 0] = tx;
	matrix[3*4 + 1] = ty;
	matrix[3*4 + 2] = tz;
	matrix[3*4 + 3] = 1.0f;

	vkCmdPushConstants(vulkan_globals.command_buffer, vulkan_globals.basic_pipeline_layout, VK_SHADER_STAGE_ALL_GRAPHICS, 0, 16 * sizeof(float), matrix);
}

/*
================
GL_Viewport
================
*/
void GL_Viewport(float x, float y, float width, float height)
{
	VkViewport viewport;
	viewport.x = x;
	viewport.y = vid.height - (y + height);
	viewport.width = width;
	viewport.height = height;
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;

	vkCmdSetViewport(vulkan_globals.command_buffer, 0, 1, &viewport);
}

/*
================
GL_SetCanvas -- johnfitz -- support various canvas types
================
*/
void GL_SetCanvas (canvastype newcanvas)
{
	if (newcanvas == currentcanvas)
		return;

	extern vrect_t scr_vrect;
	float s;
	int lines;

	currentcanvas = newcanvas;

	switch(newcanvas)
	{
	case CANVAS_DEFAULT:
		GL_OrthoMatrix (0, glwidth, glheight, 0, -99999, 99999);
		GL_Viewport (glx, gly, glwidth, glheight);
		break;
	case CANVAS_CONSOLE:
		lines = vid.conheight - (scr_con_current * vid.conheight / glheight);
		GL_OrthoMatrix (0, vid.conwidth, vid.conheight + lines, lines, -99999, 99999);
		GL_Viewport (glx, gly, glwidth, glheight);
		break;
	case CANVAS_MENU:
		s = q_min((float)glwidth / 320.0, (float)glheight / 200.0);
		s = CLAMP (1.0, scr_menuscale.value, s);
		GL_OrthoMatrix (0, 640, 200, 0, -99999, 99999);
		GL_Viewport (glx + (glwidth - 320*s) / 2, gly + (glheight - 200*s) / 2, 640*s, 200*s);
		break;
	case CANVAS_SBAR:
		s = CLAMP (1.0, scr_sbarscale.value, (float)glwidth / 320.0);
		if (cl.gametype == GAME_DEATHMATCH)
		{
			GL_OrthoMatrix (0, glwidth / s, 48, 0, -99999, 99999);
			GL_Viewport (glx, gly, glwidth, 48*s);
		}
		else
		{
			GL_OrthoMatrix (0, 320, 48, 0, -99999, 99999);
			GL_Viewport (glx + (glwidth - 320*s) / 2, gly, 320*s, 48*s);
		}
		break;
	case CANVAS_WARPIMAGE:
		GL_OrthoMatrix (0, 128, 0, 128, -99999, 99999);
		GL_Viewport (glx, gly+glheight-WARPIMAGESIZE, WARPIMAGESIZE, WARPIMAGESIZE);
		break;
	case CANVAS_CROSSHAIR: //0,0 is center of viewport
		s = CLAMP (1.0, scr_crosshairscale.value, 10.0);
		GL_OrthoMatrix (scr_vrect.width/-2/s, scr_vrect.width/2/s, scr_vrect.height/2/s, scr_vrect.height/-2/s, -99999, 99999);
		GL_Viewport (scr_vrect.x, glheight - scr_vrect.y - scr_vrect.height, scr_vrect.width & ~1, scr_vrect.height & ~1);
		break;
	case CANVAS_BOTTOMLEFT: //used by devstats
		s = (float)glwidth/vid.conwidth; //use console scale
		GL_OrthoMatrix (0, 320, 200, 0, -99999, 99999);
		GL_Viewport (glx, gly, 320*s, 200*s);
		break;
	case CANVAS_BOTTOMRIGHT: //used by fps/clock
		s = (float)glwidth/vid.conwidth; //use console scale
		GL_OrthoMatrix (0, 320, 200, 0, -99999, 99999);
		GL_Viewport (glx+glwidth-320*s, gly, 320*s, 200*s);
		break;
	case CANVAS_TOPRIGHT: //used by disc
		s = 1;
		GL_OrthoMatrix (0, 320, 200, 0, -99999, 99999);
		GL_Viewport (glx+glwidth-320*s, gly+glheight-200*s, 320*s, 200*s);
		break;
	default:
		Sys_Error ("GL_SetCanvas: bad canvas type");
	}
}

/*
================
GL_Set2D
================
*/
void GL_Set2D (void)
{
	currentcanvas = CANVAS_INVALID;
	GL_SetCanvas (CANVAS_DEFAULT);

	vkCmdEndRenderPass(vulkan_globals.command_buffer);

	if (render_warp)
	{
		VkImageMemoryBarrier image_barriers[2];
		image_barriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		image_barriers[0].pNext = NULL;
		image_barriers[0].srcAccessMask = 0;
		image_barriers[0].dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		image_barriers[0].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		image_barriers[0].newLayout = VK_IMAGE_LAYOUT_GENERAL;
		image_barriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		image_barriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		image_barriers[0].image = vulkan_globals.color_buffers[0];
		image_barriers[0].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		image_barriers[0].subresourceRange.baseMipLevel = 0;
		image_barriers[0].subresourceRange.levelCount = 1;
		image_barriers[0].subresourceRange.baseArrayLayer = 0;
		image_barriers[0].subresourceRange.layerCount = 1;

		image_barriers[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		image_barriers[1].pNext = NULL;
		image_barriers[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		image_barriers[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		image_barriers[1].oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		image_barriers[1].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		image_barriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		image_barriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		image_barriers[1].image = vulkan_globals.color_buffers[1];
		image_barriers[1].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		image_barriers[1].subresourceRange.baseMipLevel = 0;
		image_barriers[1].subresourceRange.levelCount = 1;
		image_barriers[1].subresourceRange.baseArrayLayer = 0;
		image_barriers[1].subresourceRange.layerCount = 1;

		vkCmdPipelineBarrier(vulkan_globals.command_buffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, NULL, 0, NULL, 2, image_barriers);
		
		const uint32_t screen_size[2] = { vid.width, vid.height };
		const float aspect_ratio_time[2] = { (float)vid.width / (float)vid.height, cl.time };
		vkCmdPushConstants(vulkan_globals.command_buffer, vulkan_globals.screen_warp_pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, 2 * sizeof(uint32_t), screen_size);
		vkCmdPushConstants(vulkan_globals.command_buffer, vulkan_globals.screen_warp_pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 2 * sizeof(uint32_t), 2 * sizeof(float), aspect_ratio_time);

		vkCmdBindDescriptorSets(vulkan_globals.command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, vulkan_globals.screen_warp_pipeline_layout, 0, 1, &vulkan_globals.screen_warp_desc_set, 0, NULL);
		vkCmdBindPipeline(vulkan_globals.command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, vulkan_globals.screen_warp_pipeline);
		vkCmdDispatch(vulkan_globals.command_buffer, (vid.width + 7) / 8, (vid.height + 7) / 8, 1);

		image_barriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		image_barriers[0].pNext = NULL;
		image_barriers[0].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		image_barriers[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		image_barriers[0].oldLayout = VK_IMAGE_LAYOUT_GENERAL;
		image_barriers[0].newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		image_barriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		image_barriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		image_barriers[0].image = vulkan_globals.color_buffers[0];
		image_barriers[0].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		image_barriers[0].subresourceRange.baseMipLevel = 0;
		image_barriers[0].subresourceRange.levelCount = 1;
		image_barriers[0].subresourceRange.baseArrayLayer = 0;
		image_barriers[0].subresourceRange.layerCount = 1;

		vkCmdPipelineBarrier(vulkan_globals.command_buffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, NULL, 0, NULL, 1, image_barriers);
	}
	else
	{
		VkMemoryBarrier memory_barrier;
		memory_barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
		memory_barrier.pNext = NULL;
		memory_barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		memory_barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

		vkCmdPipelineBarrier(vulkan_globals.command_buffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 1, &memory_barrier, 0, NULL, 0, NULL);
	}

	GL_AcquireNextSwapChainImage();

	vkCmdBeginRenderPass(vulkan_globals.command_buffer, &vulkan_globals.ui_render_pass_begin_info, VK_SUBPASS_CONTENTS_INLINE);
	render_pass_index = 1;
}
