/*
   Hyllian's xBR Level 2 shader, adapted for vkQuake's menu pictures.

   Copyright (C) 2011-2022 Hyllian - sergiogdb@gmail.com

   Permission is hereby granted, free of charge, to any person obtaining a copy
   of this software and associated documentation files (the "Software"), to deal
   in the Software without restriction, including without limitation the rights
   to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
   copies of the Software, and to permit persons to whom the Software is
   furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
   OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
   SOFTWARE.

   The filter is evaluated at the centers of a virtual 2x image. This keeps
   its result independent of the framebuffer resolution and menu scale.
*/
layout (push_constant) uniform PushConsts
{
	mat4  mvp;
	vec3  fog_color;
	float fog_density;
}
push_constants;
layout (set = 0, binding = 0) uniform texture2D tex;
layout (set = 1, binding = 0) uniform sampler tex_sampler;
layout (location = 0) in vec4 in_texcoord;
layout (location = 1) in vec4 in_color;
layout (location = 2) in float in_fog_frag_coord;
layout (location = 3) flat in vec4 in_texture_region;
layout (location = 0) out vec4 out_frag_color;

const float XBR_EQ_THRESHOLD = 0.32;
const float XBR_LV2_COEFFICIENT = 2.30;
const vec3	LUMA = vec3 (0.2627, 0.6780, 0.0593);

vec4 fetch_menu_texel (ivec2 p, ivec2 origin, ivec2 size)
{
	return texelFetch (sampler2D (tex, tex_sampler), origin + clamp (p, ivec2 (0), size - 1), 0);
}

// Transparent palette texels have irrelevant RGB values. Premultiplying for
// comparisons prevents those hidden values from creating false edges.
vec4 comparison_color (vec4 c)
{
	return vec4 (c.rgb * c.a, c.a);
}

float comparison_distance (vec4 a, vec4 b)
{
	return dot (abs (a.rgb - b.rgb), LUMA) + abs (a.a - b.a);
}

bool colors_differ (vec4 a, vec4 b)
{
	return any (notEqual (a, b));
}

struct xbr_corner_t
{
	vec4 b, c, d, f, g, h, i;
	vec4 i4, i5, h5, f4;
};

float corner_weighted_distance_1 (vec4 center, xbr_corner_t corner)
{
	return comparison_distance (center, corner.c) + comparison_distance (center, corner.g) + comparison_distance (corner.i, corner.h5) +
		   comparison_distance (corner.i, corner.f4) + 4.0 * comparison_distance (corner.h, corner.f);
}

float corner_weighted_distance_2 (vec4 center, xbr_corner_t corner)
{
	return comparison_distance (corner.h, corner.d) + comparison_distance (corner.h, corner.i5) + comparison_distance (corner.f, corner.i4) +
		   comparison_distance (corner.f, corner.b) + 4.0 * comparison_distance (center, corner.i);
}

bool corner_edri (vec4 center, xbr_corner_t corner)
{
	return colors_differ (center, corner.f) && colors_differ (center, corner.h) &&
		   corner_weighted_distance_1 (center, corner) <= corner_weighted_distance_2 (center, corner);
}

vec4 evaluate_corner (
	vec4 output_center, vec4 output_f, vec4 output_h, vec4 center, xbr_corner_t corner, xbr_corner_t next_corner, xbr_corner_t previous_corner)
{
	float d_ec = comparison_distance (center, corner.c);
	float d_eg = comparison_distance (center, corner.g);
	float d_hd = comparison_distance (corner.h, corner.d);
	float d_hi5 = comparison_distance (corner.h, corner.i5);
	float d_fi4 = comparison_distance (corner.f, corner.i4);
	float d_fb = comparison_distance (corner.f, corner.b);
	float d_ei = comparison_distance (center, corner.i);
	float wd1 =
		d_ec + d_eg + comparison_distance (corner.i, corner.h5) + comparison_distance (corner.i, corner.f4) + 4.0 * comparison_distance (corner.h, corner.f);
	float wd2 = d_hd + d_hi5 + d_fi4 + d_fb + 4.0 * d_ei;
	if (wd1 >= wd2)
		return output_center;

	// CORNER_C from the reference xBR Level 2 shader.
	bool irlv1 = (d_fb > XBR_EQ_THRESHOLD && comparison_distance (corner.f, corner.c) > XBR_EQ_THRESHOLD) ||
				 (d_hd > XBR_EQ_THRESHOLD && comparison_distance (corner.h, corner.g) > XBR_EQ_THRESHOLD) ||
				 (d_ei <= XBR_EQ_THRESHOLD && ((comparison_distance (corner.f, corner.f4) > XBR_EQ_THRESHOLD && d_fi4 > XBR_EQ_THRESHOLD) ||
											   (comparison_distance (corner.h, corner.h5) > XBR_EQ_THRESHOLD && d_hi5 > XBR_EQ_THRESHOLD))) ||
				 d_eg <= XBR_EQ_THRESHOLD || d_ec <= XBR_EQ_THRESHOLD;
	if (!irlv1)
		return output_center;

	bool next_edri = corner_edri (center, next_corner);
	bool previous_edri = corner_edri (center, previous_corner);
	if (next_edri && previous_edri)
		return output_center;

	float d_fg = comparison_distance (corner.f, corner.g);
	float d_hc = comparison_distance (corner.h, corner.c);
	bool  blend_left = !next_edri && d_ec <= XBR_EQ_THRESHOLD && d_eg > 0.0 && colors_differ (corner.d, corner.g) && XBR_LV2_COEFFICIENT * d_fg <= d_hc;
	bool  blend_up = !previous_edri && d_eg <= XBR_EQ_THRESHOLD && d_ec > 0.0 && colors_differ (corner.b, corner.c) && XBR_LV2_COEFFICIENT * d_hc <= d_fg;
	if (!blend_left && !blend_up)
		return output_center;

	// Preserve the original shader's tie-breaking in favor of f.
	return comparison_distance (center, corner.f) <= comparison_distance (center, corner.h) ? output_f : output_h;
}

vec4 xbr_level2 (ivec2 p, vec2 fp, ivec2 origin, ivec2 size)
{
#define P(x, y) fetch_menu_texel (p + ivec2 (x, y), origin, size)
	vec4 output_b = P (0, -1), output_d = P (-1, 0), output_e = P (0, 0);
	vec4 output_f = P (1, 0), output_h = P (0, 1);
	vec4 b = comparison_color (output_b), d = comparison_color (output_d);
	vec4 e = comparison_color (output_e), f = comparison_color (output_f), h = comparison_color (output_h);

	// The fixed 2x centers map to lanes 2, 1, 3, and 0 of the original
	// four-corner vector shader.
	ivec2 subpixel_index = ivec2 (fp * 2.0);
	int	  corner_index = 2 - subpixel_index.x + subpixel_index.y - 2 * subpixel_index.x * subpixel_index.y;
	vec4  selected_comparison_f, selected_comparison_h;
	vec4  selected_output_f, selected_output_h;
	if (corner_index == 0)
	{
		selected_comparison_f = f;
		selected_comparison_h = h;
		selected_output_f = output_f;
		selected_output_h = output_h;
	}
	else if (corner_index == 1)
	{
		selected_comparison_f = b;
		selected_comparison_h = f;
		selected_output_f = output_b;
		selected_output_h = output_f;
	}
	else if (corner_index == 2)
	{
		selected_comparison_f = d;
		selected_comparison_h = b;
		selected_output_f = output_d;
		selected_output_h = output_b;
	}
	else
	{
		selected_comparison_f = h;
		selected_comparison_h = d;
		selected_output_f = output_h;
		selected_output_h = output_d;
	}

	// A corner with either neighbor equal to E cannot blend. Most fragments
	// return here after fetching only the center cross.
	if (!colors_differ (e, selected_comparison_f) || !colors_differ (e, selected_comparison_h))
		return output_e;

#define CP(x, y) comparison_color (P (x, y))
	vec4 a1 = CP (-1, -2), b1 = CP (0, -2), c1 = CP (1, -2);
	vec4 a = CP (-1, -1), c = CP (1, -1);
	vec4 g = CP (-1, 1), i = CP (1, 1);
	vec4 g5 = CP (-1, 2), h5 = CP (0, 2), i5 = CP (1, 2);
	vec4 a0 = CP (-2, -1), d0 = CP (-2, 0), g0 = CP (-2, 1);
	vec4 c4 = CP (2, -1), f4 = CP (2, 0), i4 = CP (2, 1);
#undef CP
#undef P

	// The same rule rotated around E for each corner.
	xbr_corner_t corner0 = xbr_corner_t (b, c, d, f, g, h, i, i4, i5, h5, f4);
	xbr_corner_t corner1 = xbr_corner_t (d, a, h, b, i, f, c, c1, c4, f4, b1);
	xbr_corner_t corner2 = xbr_corner_t (h, g, f, d, c, b, a, a0, a1, b1, d0);
	xbr_corner_t corner3 = xbr_corner_t (f, i, b, h, a, d, g, g5, g0, d0, h5);

	xbr_corner_t corner, next_corner, previous_corner;
	if (corner_index == 0)
	{
		corner = corner0;
		next_corner = corner1;
		previous_corner = corner3;
	}
	else if (corner_index == 1)
	{
		corner = corner1;
		next_corner = corner2;
		previous_corner = corner0;
	}
	else if (corner_index == 2)
	{
		corner = corner2;
		next_corner = corner3;
		previous_corner = corner1;
	}
	else
	{
		corner = corner3;
		next_corner = corner0;
		previous_corner = corner2;
	}

	return evaluate_corner (output_e, selected_output_f, selected_output_h, e, corner, next_corner, previous_corner);
}

void main ()
{
	ivec2 texture_size = textureSize (sampler2D (tex, tex_sampler), 0);
	ivec2 region_origin = ivec2 (round (in_texture_region.xy * vec2 (texture_size)));
	ivec2 region_size = max (ivec2 (round ((in_texture_region.zw - in_texture_region.xy) * vec2 (texture_size))), ivec2 (1));
	ivec2 output_size = region_size * 2;
	vec2  region_uv = (in_texcoord.xy - in_texture_region.xy) / (in_texture_region.zw - in_texture_region.xy);
	ivec2 output_texel = clamp (ivec2 (floor (region_uv * vec2 (output_size))), ivec2 (0), output_size - 1);
	ivec2 source_texel = output_texel / 2;
	vec2  subpixel = (vec2 (output_texel - source_texel * 2) + 0.5) * 0.5;

	out_frag_color = in_color * xbr_level2 (source_texel, subpixel, region_origin, region_size);
#ifdef ALPHA_TEST
	if (out_frag_color.a < 0.666f)
		discard;
#endif
	// GUI draws normally have no fog. Avoid the transcendental operation in
	// that overwhelmingly common case while preserving the existing behavior.
	if (push_constants.fog_density != 0.0)
	{
		float fog = exp (-push_constants.fog_density * push_constants.fog_density * in_fog_frag_coord * in_fog_frag_coord);
		out_frag_color.rgb = mix (push_constants.fog_color, out_frag_color.rgb, clamp (fog, 0.0, 1.0));
	}
}
