/*
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

#ifndef __SHADERS_H
#define __SHADERS_H

extern unsigned char basic_vert_spv[];
extern int basic_vert_spv_size;
extern unsigned char basic_frag_spv[];
extern int basic_frag_spv_size;
extern unsigned char basic_alphatest_frag_spv[];
extern int basic_alphatest_frag_spv_size;
extern unsigned char basic_char_frag_spv[];
extern int basic_char_frag_spv_size;
extern unsigned char basic_notex_frag_spv[];
extern int basic_notex_frag_spv_size;
extern unsigned char world_vert_spv[];
extern int world_vert_spv_size;
extern unsigned char world_frag_spv[];
extern int world_frag_spv_size;
extern unsigned char alias_vert_spv[];
extern int alias_vert_spv_size;
extern unsigned char alias_frag_spv[];
extern int alias_frag_spv_size;
extern unsigned char sky_layer_vert_spv[];
extern int sky_layer_vert_spv_size;
extern unsigned char sky_layer_frag_spv[];
extern int sky_layer_frag_spv_size;
extern unsigned char postprocess_vert_spv[];
extern int postprocess_vert_spv_size;
extern unsigned char postprocess_frag_spv[];
extern int postprocess_frag_spv_size;

#endif
