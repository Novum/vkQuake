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
const vec4	XBR_DISTANCE_WEIGHTS = vec4 (0.2627, 0.6780, 0.0593, 1.0);

vec4 fetch_menu_texel (ivec2 p, ivec2 origin, ivec2 size, bool clamp_coordinates)
{
	if (clamp_coordinates)
		p = clamp (p, ivec2 (0), size - 1);
	return texelFetch (sampler2D (tex, tex_sampler), origin + p, 0);
}

// Transparent palette texels have irrelevant RGB values. Premultiplying for
// comparisons prevents those hidden values from creating false edges.
vec4 comparison_color (vec4 c)
{
	return vec4 (c.rgb * c.a, c.a);
}

float comparison_distance (vec4 a, vec4 b)
{
	return dot (abs (a - b), XBR_DISTANCE_WEIGHTS);
}

bool colors_differ (vec4 a, vec4 b)
{
	return any (notEqual (a, b));
}

vec4 xbr_level2 (ivec2 p, ivec2 subpixel_index, ivec2 origin, ivec2 size, bool clamp_coordinates)
{
	int	  diagonal = 1 - (subpixel_index.x ^ subpixel_index.y);
	ivec2 direction = 2 * subpixel_index - 1;
	ivec2 u = ivec2 (diagonal * direction.x, (1 - diagonal) * direction.y);
	ivec2 v = ivec2 (-u.y, u.x);

#define P(offset)  fetch_menu_texel (p + (offset), origin, size, clamp_coordinates)
#define CP(offset) comparison_color (P (offset))
	vec4 output_e = P (ivec2 (0));
	vec4 output_f = P (u);
	vec4 output_h = P (v);
	vec4 e = comparison_color (output_e);
	vec4 f = comparison_color (output_f);
	vec4 h = comparison_color (output_h);

	// A corner with either neighbor equal to E cannot blend. Rotating the
	// neighborhood lets the overwhelmingly common flat path use three fetches.
	if (!colors_differ (e, f) || !colors_differ (e, h))
		return output_e;

	vec4 b = CP (-v);
	vec4 c = CP (u - v);
	vec4 d = CP (-u);
	vec4 g = CP (v - u);
	vec4 i = CP (u + v);
	vec4 i4 = CP (2 * u + v);
	vec4 i5 = CP (u + 2 * v);
	vec4 h5 = CP (2 * v);
	vec4 f4 = CP (2 * u);

	float d_ec = comparison_distance (e, c);
	float d_eg = comparison_distance (e, g);
	float d_hd = comparison_distance (h, d);
	float d_hi5 = comparison_distance (h, i5);
	float d_fi4 = comparison_distance (f, i4);
	float d_fb = comparison_distance (f, b);
	float d_ei = comparison_distance (e, i);
	float d_hf = comparison_distance (h, f);
	float wd1 = d_ec + d_eg + comparison_distance (i, h5) + comparison_distance (i, f4) + 4.0 * d_hf;
	float wd2 = d_hd + d_hi5 + d_fi4 + d_fb + 4.0 * d_ei;
	if (wd1 >= wd2)
		return output_e;

	// CORNER_C from the reference xBR Level 2 shader.
	bool irlv1 = (d_fb > XBR_EQ_THRESHOLD && comparison_distance (f, c) > XBR_EQ_THRESHOLD) ||
				 (d_hd > XBR_EQ_THRESHOLD && comparison_distance (h, g) > XBR_EQ_THRESHOLD) ||
				 (d_ei <= XBR_EQ_THRESHOLD && ((comparison_distance (f, f4) > XBR_EQ_THRESHOLD && d_fi4 > XBR_EQ_THRESHOLD) ||
											   (comparison_distance (h, h5) > XBR_EQ_THRESHOLD && d_hi5 > XBR_EQ_THRESHOLD))) ||
				 d_eg <= XBR_EQ_THRESHOLD || d_ec <= XBR_EQ_THRESHOLD;
	if (!irlv1)
		return output_e;

	// The adjacent-corner suppression rules are only needed for surviving
	// primary edges. Their center-cross tests can reject either side before
	// fetching that side's outer three texels.
	bool next_edri = false;
	bool previous_edri = false;
	bool test_next = colors_differ (e, b);
	bool test_previous = colors_differ (e, d);
	if (test_next || test_previous)
	{
		vec4  a = CP (-u - v);
		float d_ea = comparison_distance (e, a);
		float d_bd = comparison_distance (b, d);
		if (test_next)
		{
			vec4  b1 = CP (-2 * v);
			vec4  c1 = CP (u - 2 * v);
			vec4  c4 = CP (2 * u - v);
			float next_wd1 = d_ea + d_ei + comparison_distance (c, f4) + comparison_distance (c, b1) + 4.0 * d_fb;
			float next_wd2 = d_hf + comparison_distance (f, c4) + comparison_distance (b, c1) + d_bd + 4.0 * d_ec;
			next_edri = next_wd1 <= next_wd2;
		}
		if (test_previous)
		{
			vec4  d0 = CP (-2 * u);
			vec4  g0 = CP (v - 2 * u);
			vec4  g5 = CP (2 * v - u);
			float previous_wd1 = d_ei + d_ea + comparison_distance (g, d0) + comparison_distance (g, h5) + 4.0 * d_hd;
			float previous_wd2 = d_bd + comparison_distance (d, g0) + comparison_distance (h, g5) + d_hf + 4.0 * d_eg;
			previous_edri = previous_wd1 <= previous_wd2;
		}
	}
#undef CP
#undef P
	if (next_edri && previous_edri)
		return output_e;

	float d_fg = comparison_distance (f, g);
	float d_hc = comparison_distance (h, c);
	bool  blend_left = !next_edri && d_ec <= XBR_EQ_THRESHOLD && d_eg > 0.0 && colors_differ (d, g) && XBR_LV2_COEFFICIENT * d_fg <= d_hc;
	bool  blend_up = !previous_edri && d_eg <= XBR_EQ_THRESHOLD && d_ec > 0.0 && colors_differ (b, c) && XBR_LV2_COEFFICIENT * d_hc <= d_fg;
	if (!blend_left && !blend_up)
		return output_e;

	// Preserve the original shader's tie-breaking in favor of f.
	return comparison_distance (e, f) <= comparison_distance (e, h) ? output_f : output_h;
}

void main ()
{
	ivec2 texture_size = textureSize (sampler2D (tex, tex_sampler), 0);
	ivec2 region_origin = ivec2 (round (in_texture_region.xy * vec2 (texture_size)));
	ivec2 region_size = max (ivec2 (round ((in_texture_region.zw - in_texture_region.xy) * vec2 (texture_size))), ivec2 (1));
	ivec2 output_size = region_size * 2;
	vec2  region_uv = (in_texcoord.xy - in_texture_region.xy) * in_texcoord.zw;
	ivec2 output_texel = clamp (ivec2 (region_uv * vec2 (output_size)), ivec2 (0), output_size - 1);
	ivec2 source_texel = output_texel >> 1;
	ivec2 subpixel_index = output_texel & 1;

	vec4 filtered_color;
	if (any (lessThan (source_texel, ivec2 (2))) || any (greaterThanEqual (source_texel, region_size - 2)))
		filtered_color = xbr_level2 (source_texel, subpixel_index, region_origin, region_size, true);
	else
		filtered_color = xbr_level2 (source_texel, subpixel_index, region_origin, region_size, false);
	out_frag_color = in_color * filtered_color;
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
