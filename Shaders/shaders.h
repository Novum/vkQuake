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

#define DECLARE_SHADER_SPV(name)             \
	extern const unsigned char name##_spv[]; \
	extern const int		   name##_spv_size;

DECLARE_SHADER_SPV (basic_vert);
DECLARE_SHADER_SPV (basic_frag);
DECLARE_SHADER_SPV (basic_alphatest_frag);
DECLARE_SHADER_SPV (basic_notex_frag);
DECLARE_SHADER_SPV (world_vert);
DECLARE_SHADER_SPV (world_frag);
DECLARE_SHADER_SPV (alias_vert);
DECLARE_SHADER_SPV (alias_frag);
DECLARE_SHADER_SPV (alias_alphatest_frag);
DECLARE_SHADER_SPV (md5_vert);
DECLARE_SHADER_SPV (sky_layer_vert);
DECLARE_SHADER_SPV (sky_layer_frag);
DECLARE_SHADER_SPV (sky_box_frag);
DECLARE_SHADER_SPV (sky_cube_vert);
DECLARE_SHADER_SPV (sky_cube_frag);
DECLARE_SHADER_SPV (postprocess_vert);
DECLARE_SHADER_SPV (postprocess_frag);
DECLARE_SHADER_SPV (screen_effects_8bit_comp);
DECLARE_SHADER_SPV (screen_effects_8bit_scale_comp);
DECLARE_SHADER_SPV (screen_effects_8bit_scale_sops_comp);
DECLARE_SHADER_SPV (screen_effects_10bit_comp);
DECLARE_SHADER_SPV (screen_effects_10bit_scale_comp);
DECLARE_SHADER_SPV (screen_effects_10bit_scale_sops_comp);
DECLARE_SHADER_SPV (cs_tex_warp_comp);
DECLARE_SHADER_SPV (indirect_comp);
DECLARE_SHADER_SPV (indirect_clear_comp);
DECLARE_SHADER_SPV (showtris_vert);
DECLARE_SHADER_SPV (showtris_frag);
DECLARE_SHADER_SPV (update_lightmap_comp);
DECLARE_SHADER_SPV (update_lightmap_rt_comp);
DECLARE_SHADER_SPV (ray_debug_comp);

#undef DECLARE_SHADER_SPV

#endif
