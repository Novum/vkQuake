/*
Copyright (C) 1996-2001 Id Software, Inc.
Copyright (C) 2002-2009 John Fitzgibbons and others

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

#ifndef _PALETTE_H
#define _PALETTE_H

#define NUM_PALETTE_OCTREE_NODES  184
#define NUM_PALETTE_OCTREE_COLORS 5844

typedef struct palette_octree_node_s
{
	uint32_t child_offsets[8];
} palette_octree_node_t;

COMPILE_TIME_ASSERT ("palette_octree_node_t", sizeof (palette_octree_node_t) == 32);

extern palette_octree_node_t palette_octree_nodes[NUM_PALETTE_OCTREE_NODES];
extern uint32_t				 palette_octree_colors[NUM_PALETTE_OCTREE_COLORS];

#ifdef _DEBUG
void CreatePaletteOctree_f (void);
#endif

#endif /* _PALETTE_H */
