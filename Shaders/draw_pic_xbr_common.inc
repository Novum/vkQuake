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
	mat4 mvp;
	vec3 fog_color;
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
const vec3 LUMA = vec3 (0.2627, 0.6780, 0.0593);

const vec4 Ao = vec4 (1.0, -1.0, -1.0, 1.0);
const vec4 Bo = vec4 (1.0, 1.0, -1.0, -1.0);
const vec4 Co = vec4 (1.5, 0.5, -0.5, 0.5);
const vec4 Ax = vec4 (1.0, -1.0, -1.0, 1.0);
const vec4 Bx = vec4 (0.5, 2.0, -0.5, -2.0);
const vec4 Cx = vec4 (1.0, 1.0, -0.5, 0.0);
const vec4 Ay = vec4 (1.0, -1.0, -1.0, 1.0);
const vec4 By = vec4 (2.0, 0.5, -2.0, -0.5);
const vec4 Cy = vec4 (2.0, 0.0, -1.0, 0.5);
const vec4 Ci = vec4 (0.25);

vec4 fetch_menu_texel (ivec2 p, ivec2 origin, ivec2 size)
{
	return texelFetch (sampler2D (tex, tex_sampler),
		origin + clamp (p, ivec2 (0), size - 1), 0);
}

// Transparent palette texels have irrelevant RGB values. Premultiplying for
// comparisons prevents those hidden values from creating false edges.
vec4 comparison_color (vec4 c)
{
	return vec4 (c.rgb * c.a, c.a);
}

float color_distance (vec4 a, vec4 b)
{
	a = comparison_color (a);
	b = comparison_color (b);
	return dot (abs (a.rgb - b.rgb), LUMA) + abs (a.a - b.a);
}

vec4 distance4 (mat4 a, mat4 b)
{
	return vec4 (color_distance (a[0], b[0]), color_distance (a[1], b[1]),
		color_distance (a[2], b[2]), color_distance (a[3], b[3]));
}

vec4 equal4 (mat4 a, mat4 b)
{
	return step (distance4 (a, b), vec4 (XBR_EQ_THRESHOLD));
}

vec4 not_equal4 (mat4 a, mat4 b)
{
	return vec4 (1.0) - equal4 (a, b);
}

float different (vec4 a, vec4 b)
{
	return color_distance (a, b) > 0.0 ? 1.0 : 0.0;
}

vec4 different4 (mat4 a, mat4 b)
{
	return vec4 (different (a[0], b[0]), different (a[1], b[1]),
		different (a[2], b[2]), different (a[3], b[3]));
}

vec4 weighted_distance (mat4 a, mat4 b, mat4 c, mat4 d,
	mat4 e, mat4 f, mat4 g, mat4 h)
{
	return distance4 (a, b) + distance4 (a, c) + distance4 (d, e) +
		distance4 (d, f) + 4.0 * distance4 (g, h);
}

vec4 less_than (vec4 a, vec4 b) { return vec4 (lessThan (a, b)); }
vec4 less_equal (vec4 a, vec4 b) { return step (a, b); }
vec4 logical_not (vec4 a) { return vec4 (1.0) - a; }

vec4 xbr_level2 (ivec2 p, vec2 fp, ivec2 origin, ivec2 size)
{
#define P(x, y) fetch_menu_texel (p + ivec2 (x, y), origin, size)
	vec4 A1 = P (-1, -2), B1 = P (0, -2), C1 = P (1, -2);
	vec4 A  = P (-1, -1), B  = P (0, -1), C  = P (1, -1);
	vec4 D  = P (-1,  0), E  = P (0,  0), F  = P (1,  0);
	vec4 G  = P (-1,  1), H  = P (0,  1), I  = P (1,  1);
	vec4 G5 = P (-1,  2), H5 = P (0,  2), I5 = P (1,  2);
	vec4 A0 = P (-2, -1), D0 = P (-2, 0), G0 = P (-2, 1);
	vec4 C4 = P ( 2, -1), F4 = P ( 2, 0), I4 = P ( 2, 1);
#undef P

	// Each column evaluates the same rule rotated around E.
	mat4 b  = mat4 (B, D, H, F);
	mat4 c  = mat4 (C, A, G, I);
	mat4 d  = mat4 (D, H, F, B);
	mat4 e  = mat4 (E, E, E, E);
	mat4 f  = mat4 (F, B, D, H);
	mat4 g  = mat4 (G, I, C, A);
	mat4 h  = mat4 (H, F, B, D);
	mat4 i  = mat4 (I, C, A, G);
	mat4 i4 = mat4 (I4, C1, A0, G5);
	mat4 i5 = mat4 (I5, C4, A1, G0);
	mat4 h5 = mat4 (H5, F4, B1, D0);
	mat4 f4 = mat4 (F4, B1, D0, H5);

	vec4 fx   = Ao * fp.y + Bo * fp.x;
	vec4 fx_l = Ax * fp.y + Bx * fp.x;
	vec4 fx_u = Ay * fp.y + By * fp.x;

	vec4 irlv0 = different4 (e, f) * different4 (e, h);
	// CORNER_C from the reference xBR Level 2 shader.
	vec4 irlv1 = clamp (irlv0 * (not_equal4 (f, b) * not_equal4 (f, c) +
		not_equal4 (h, d) * not_equal4 (h, g) + equal4 (e, i) *
		(not_equal4 (f, f4) * not_equal4 (f, i4) +
		 not_equal4 (h, h5) * not_equal4 (h, i5)) +
		equal4 (e, g) + equal4 (e, c)), 0.0, 1.0);
	vec4 irlv2l = different4 (e, g) * different4 (d, g);
	vec4 irlv2u = different4 (e, c) * different4 (b, c);

	// No screen-space AA: fp is one of the four fixed virtual 2x centers.
	vec4 fx45i = less_than (Co + Ci, fx);
	vec4 fx45  = less_than (Co, fx);
	vec4 fx30  = less_than (Cx, fx_l);
	vec4 fx60  = less_than (Cy, fx_u);

	vec4 wd1 = weighted_distance (e, c, g, i, h5, f4, h, f);
	vec4 wd2 = weighted_distance (h, d, i5, f, i4, b, e, i);
	vec4 d_fg = distance4 (f, g);
	vec4 d_hc = distance4 (h, c);
	vec4 edri = less_equal (wd1, wd2) * irlv0;
	vec4 edr = less_than (wd1, wd2) * irlv1 *
		logical_not (edri.yzwx * edri.wxyz);
	vec4 edr_l = less_equal (XBR_LV2_COEFFICIENT * d_fg, d_hc) * irlv2l * edr *
		logical_not (edri.yzwx) * equal4 (e, c);
	vec4 edr_u = less_equal (XBR_LV2_COEFFICIENT * d_hc, d_fg) * irlv2u * edr *
		logical_not (edri.wxyz) * equal4 (e, g);

	fx45i *= edri;
	fx45 *= edr;
	fx30 *= edr_l;
	fx60 *= edr_u;
	vec4 px = less_equal (distance4 (e, f), distance4 (e, h));
	vec4 blend = max (max (fx30, fx60), max (fx45, fx45i));

	vec4 r1 = mix (E, mix (H, F, px.x), blend.x);
	vec4 r2 = mix (E, mix (B, D, px.z), blend.z);
	vec4 ra = mix (r1, r2, step (color_distance (E, r1), color_distance (E, r2)));
	r1 = mix (E, mix (F, B, px.y), blend.y);
	r2 = mix (E, mix (D, H, px.w), blend.w);
	vec4 rb = mix (r1, r2, step (color_distance (E, r1), color_distance (E, r2)));
	return mix (ra, rb, step (color_distance (E, ra), color_distance (E, rb)));
}

void main ()
{
	ivec2 texture_size = textureSize (sampler2D (tex, tex_sampler), 0);
	ivec2 region_origin = ivec2 (round (in_texture_region.xy * vec2 (texture_size)));
	ivec2 region_size = max (ivec2 (round ((in_texture_region.zw -
		in_texture_region.xy) * vec2 (texture_size))), ivec2 (1));
	ivec2 output_size = region_size * 2;
	vec2 region_uv = (in_texcoord.xy - in_texture_region.xy) /
		(in_texture_region.zw - in_texture_region.xy);
	ivec2 output_texel = clamp (ivec2 (floor (region_uv * vec2 (output_size))),
		ivec2 (0), output_size - 1);
	ivec2 source_texel = output_texel / 2;
	vec2 subpixel = (vec2 (output_texel - source_texel * 2) + 0.5) * 0.5;

	out_frag_color = in_color * xbr_level2 (source_texel, subpixel, region_origin, region_size);
#ifdef ALPHA_TEST
	if (out_frag_color.a < 0.666f)
		discard;
#endif
	float fog = exp (-push_constants.fog_density * push_constants.fog_density *
		in_fog_frag_coord * in_fog_frag_coord);
	out_frag_color.rgb = mix (push_constants.fog_color, out_frag_color.rgb,
		clamp (fog, 0.0, 1.0));
}
