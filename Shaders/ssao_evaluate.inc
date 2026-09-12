// MIT License
//
// Copyright (c) 2016-2021 Intel Corporation
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

layout (local_size_x = 8, local_size_y = 8) in;
layout (set = 0, binding = 0) uniform sampler2D world_depth;
layout (set = 1, binding = 0) uniform usampler2D hilbert_lut;
layout (r8, set = 2, binding = 0) uniform writeonly image2D out_working_ao_term;
layout (r8, set = 3, binding = 0) uniform writeonly image2D out_working_edges;
#include "gtao.inc"
#include "thread_group_tiling.inc"

// Precision boundaries follow XeGTAO_MainPass and its helpers (see gtao.inc).

vec3 compute_viewspace_position (vec2 screen_pos, float viewspace_depth)
{
	vec3 ret;
	ret.xy = (pc.viewport.zw * screen_pos.xy + pc.projection.xy) * viewspace_depth;
	ret.z = viewspace_depth;
	return ret;
}

half4 calculate_edges (half center_z, half left_z, half right_z, half top_z, half bottom_z)
{
	half4 edges_lrtb = half4 (left_z, right_z, top_z, bottom_z) - center_z;
	half  slope_lr = (edges_lrtb.y - edges_lrtb.x) * half (0.5);
	half  slope_tb = (edges_lrtb.w - edges_lrtb.z) * half (0.5);
	half4 edges_lrtb_slope_adjusted = edges_lrtb + half4 (slope_lr, -slope_lr, slope_tb, -slope_tb);
	edges_lrtb = min (abs (edges_lrtb), abs (edges_lrtb_slope_adjusted));
	return clamp (half (1.25) - edges_lrtb / (center_z * half (0.011)), half (0.0), half (1.0));
}

half pack_edges (half4 edges_lrtb)
{
	edges_lrtb = round (clamp (edges_lrtb, half (0.0), half (1.0)) * half (2.9));
	return dot (edges_lrtb, half4 (64.0 / 255.0, 16.0 / 255.0, 4.0 / 255.0, 1.0 / 255.0));
}

vec3 calculate_normal (vec4 edges_lrtb, vec3 pix_center_pos, vec3 pix_l_pos, vec3 pix_r_pos, vec3 pix_t_pos, vec3 pix_b_pos)
{
	vec4 accepted_normals =
		clamp (vec4 (edges_lrtb.x * edges_lrtb.z, edges_lrtb.z * edges_lrtb.y, edges_lrtb.y * edges_lrtb.w, edges_lrtb.w * edges_lrtb.x) + 0.01, 0.0, 1.0);
	pix_l_pos = normalize (pix_l_pos - pix_center_pos);
	pix_r_pos = normalize (pix_r_pos - pix_center_pos);
	pix_t_pos = normalize (pix_t_pos - pix_center_pos);
	pix_b_pos = normalize (pix_b_pos - pix_center_pos);
	vec3 pixel_normal = accepted_normals.x * cross (pix_l_pos, pix_t_pos) + accepted_normals.y * cross (pix_t_pos, pix_r_pos) +
						accepted_normals.z * cross (pix_r_pos, pix_b_pos) + accepted_normals.w * cross (pix_b_pos, pix_l_pos);
	pixel_normal = normalize (pixel_normal);
	return pixel_normal;
}

half fast_sqrt (float x)
{
	return half (intBitsToFloat (0x1fbd1df5 + (floatBitsToInt (x) >> 1)));
}

half fast_acos (half in_x)
{
	half pi = half (3.141593);
	half half_pi = half (1.570796);
	half x = abs (in_x);
	half res = half (-0.156583) * x + half_pi;
	res *= fast_sqrt (half (1.0) - x);
	return (in_x >= half (0.0)) ? res : pi - res;
}

void main ()
{
	uvec2		   pixel = tiled_global_invocation_id ();
	gtao_constants c = get_gtao_constants ();
	if (any (greaterThanEqual (pixel, uvec2 (c.viewport_size))))
		return;
	if (texelFetch (world_depth, ivec2 (pixel), 0).x >= 65504.0)
	{
		imageStore (out_working_ao_term, ivec2 (pixel), vec4 (2.0 / 3.0));
		imageStore (out_working_edges, ivec2 (pixel), vec4 (0));
		return;
	}
	uint  index = texelFetch (hilbert_lut, ivec2 (pixel & 63u), 0).r;
	half2 noise = half2 (fract (0.5 + float (index) * vec2 (0.75487766624669276, 0.56984029099805326)));
	// Low: 1x2x2 = 4 samples; medium: 2x2x2 = 8; high: 3x3x2 = 18.
	half  slice_count = half (pc.settings.w);
	half  steps_per_slice = slice_count < half (3.0) ? half (2.0) : half (3.0);

	vec2 normalized_screen_pos = (pixel + vec2 (0.5)) * c.viewport_pixel_size;

	half4 values_ul = half4 (textureGather (world_depth, pixel * c.viewport_pixel_size, 0));
	half4 values_br = half4 (textureGatherOffset (world_depth, pixel * c.viewport_pixel_size, ivec2 (1, 1), 0));

	half viewspace_z = values_ul.y;

	half pix_lz = values_ul.x;
	half pix_tz = values_ul.z;
	half pix_rz = values_br.z;
	half pix_bz = values_br.x;

	half4 edges_lrtb = calculate_edges (viewspace_z, pix_lz, pix_rz, pix_tz, pix_bz);
	imageStore (out_working_edges, ivec2 (pixel), vec4 (pack_edges (edges_lrtb)));

	vec3  center = compute_viewspace_position (normalized_screen_pos, viewspace_z);
	vec3  left = compute_viewspace_position (normalized_screen_pos + vec2 (-1, 0) * c.viewport_pixel_size, pix_lz);
	vec3  right = compute_viewspace_position (normalized_screen_pos + vec2 (1, 0) * c.viewport_pixel_size, pix_rz);
	vec3  top = compute_viewspace_position (normalized_screen_pos + vec2 (0, -1) * c.viewport_pixel_size, pix_tz);
	vec3  bottom = compute_viewspace_position (normalized_screen_pos + vec2 (0, 1) * c.viewport_pixel_size, pix_bz);
	half3 viewspace_normal = half3 (calculate_normal (vec4 (edges_lrtb), center, left, right, top, bottom));

	// XeGTAO bias for FP16 depth storage.
	viewspace_z *= half (0.99920);

	vec3  pix_center_pos = compute_viewspace_position (normalized_screen_pos, viewspace_z);
	half3 view_vec = half3 (normalize (-pix_center_pos));

	half effect_radius = half (c.effect_radius) * half (1.457);
	half sample_distribution_power = half (2.0);
	half falloff_range = half (0.615) * effect_radius;

	half falloff_from = effect_radius * (half (1.0) - half (c.effect_falloff_range));

	half falloff_mul = half (-1.0) / falloff_range;
	half falloff_add = falloff_from / falloff_range + half (1.0);

	half world_visibility = half (0.0);
	half scene_visibility = half (0.0);

	half noise_slice = noise.x;
	half noise_sample = noise.y;

	half pixel_too_close_threshold = half (1.3);

	vec2 pixel_dir_rb_viewspace_size_at_center_z = vec2 (viewspace_z) * c.ndc_to_view_mul_x_pixel_size;

	half screenspace_radius = effect_radius / half (pixel_dir_rb_viewspace_size_at_center_z.x);

	half small_radius_fade = clamp ((half (10.0) - screenspace_radius) / half (100.0), half (0.0), half (1.0)) * half (0.5);
	world_visibility += small_radius_fade;
	scene_visibility += small_radius_fade;

	half min_s = pixel_too_close_threshold / screenspace_radius;

	for (half slice = half (0.0); slice < slice_count; slice++)
	{
		half slice_k = (slice + noise_slice) / slice_count;

		half  phi = slice_k * half (3.1415926535897932384626433832795);
		half  cos_phi = cos (phi);
		half  sin_phi = sin (phi);
		half2 omega = half2 (cos_phi, -sin_phi);

		omega *= screenspace_radius;

		half3 direction_vec = half3 (cos_phi, sin_phi, half (0.0));

		half3 ortho_direction_vec = direction_vec - (dot (direction_vec, view_vec) * view_vec);

		half3 axis_vec = normalize (cross (ortho_direction_vec, view_vec));

		half3 projected_normal_vec = viewspace_normal - axis_vec * dot (viewspace_normal, axis_vec);

		half sign_norm = sign (dot (ortho_direction_vec, projected_normal_vec));

		half projected_normal_vec_length = length (projected_normal_vec);
		half cos_norm = clamp (dot (projected_normal_vec, view_vec) / projected_normal_vec_length, half (0.0), half (1.0));

		half n = sign_norm * fast_acos (cos_norm);

		half low_horizon_cos0 = cos (n + half (1.5707963267948966192313216916398));
		half low_horizon_cos1 = cos (n - half (1.5707963267948966192313216916398));

		half world_horizon_cos0 = low_horizon_cos0;
		half world_horizon_cos1 = low_horizon_cos1;
		half scene_horizon_cos0 = low_horizon_cos0;
		half scene_horizon_cos1 = low_horizon_cos1;

		for (half step = half (0.0); step < steps_per_slice; step++)
		{
			half step_base_noise = (slice + step * steps_per_slice) * half (0.6180339887498948482);
			half step_noise = fract (noise_sample + step_base_noise);

			half s = (step + step_noise) / (steps_per_slice);

			s = pow (s, sample_distribution_power);

			s += min_s;

			half2 sample_offset = s * omega;

			half sample_offset_length = length (sample_offset);

			half mip_level = half (clamp (float (log2 (sample_offset_length)) - c.depth_mip_sampling_offset, 0.0, 5.0));

			sample_offset = round (sample_offset) * half2 (c.viewport_pixel_size);

			vec2 sample_screen_pos0 = normalized_screen_pos + vec2 (sample_offset);
			vec2 sample_depth0 = vec2 (half2 (textureLod (world_depth, sample_screen_pos0, float (mip_level)).xy));
			vec3 world_sample_pos0 = compute_viewspace_position (sample_screen_pos0, sample_depth0.x);

			vec2 sample_screen_pos1 = normalized_screen_pos - vec2 (sample_offset);
			vec2 sample_depth1 = vec2 (half2 (textureLod (world_depth, sample_screen_pos1, float (mip_level)).xy));
			vec3 world_sample_pos1 = compute_viewspace_position (sample_screen_pos1, sample_depth1.x);

			vec3 world_sample_delta0 = world_sample_pos0 - pix_center_pos;
			vec3 world_sample_delta1 = world_sample_pos1 - pix_center_pos;
			half world_sample_dist0 = half (length (world_sample_delta0));
			half world_sample_dist1 = half (length (world_sample_delta1));

			half3 world_sample_horizon_vec0 = half3 (world_sample_delta0 / float (world_sample_dist0));
			half3 world_sample_horizon_vec1 = half3 (world_sample_delta1 / float (world_sample_dist1));
			half  world_weight0 = clamp (world_sample_dist0 * falloff_mul + falloff_add, half (0.0), half (1.0));
			half  world_weight1 = clamp (world_sample_dist1 * falloff_mul + falloff_add, half (0.0), half (1.0));
			bool  in_bounds0 = all (greaterThanEqual (sample_screen_pos0, vec2 (0))) && all (lessThanEqual (sample_screen_pos0, vec2 (1)));
			bool  in_bounds1 = all (greaterThanEqual (sample_screen_pos1, vec2 (0))) && all (lessThanEqual (sample_screen_pos1, vec2 (1)));
			world_weight0 *= sample_depth0.x < 65504.0 && in_bounds0 ? half (1.0) : half (0.0);
			world_weight1 *= sample_depth1.x < 65504.0 && in_bounds1 ? half (1.0) : half (0.0);

			half world_shc0 = dot (world_sample_horizon_vec0, view_vec);
			half world_shc1 = dot (world_sample_horizon_vec1, view_vec);

			world_shc0 = mix (low_horizon_cos0, world_shc0, world_weight0);
			world_shc1 = mix (low_horizon_cos1, world_shc1, world_weight1);

			// Equal depths have identical sample contributions, even if earlier samples made the horizons differ.
			half scene_shc0 = world_shc0;
			half scene_shc1 = world_shc1;
			if (sample_depth0.x != sample_depth0.y)
			{
				vec3  scene_sample_pos0 = compute_viewspace_position (sample_screen_pos0, sample_depth0.y);
				vec3  scene_sample_delta0 = scene_sample_pos0 - pix_center_pos;
				half  scene_sample_dist0 = half (length (scene_sample_delta0));
				half3 scene_sample_horizon_vec0 = half3 (scene_sample_delta0 / float (scene_sample_dist0));
				half  scene_weight0 = clamp (scene_sample_dist0 * falloff_mul + falloff_add, half (0.0), half (1.0));
				scene_weight0 *= sample_depth0.y < 65504.0 && in_bounds0 ? half (1.0) : half (0.0);
				scene_shc0 = dot (scene_sample_horizon_vec0, view_vec);
				scene_shc0 = mix (low_horizon_cos0, scene_shc0, scene_weight0);
			}
			if (sample_depth1.x != sample_depth1.y)
			{
				vec3  scene_sample_pos1 = compute_viewspace_position (sample_screen_pos1, sample_depth1.y);
				vec3  scene_sample_delta1 = scene_sample_pos1 - pix_center_pos;
				half  scene_sample_dist1 = half (length (scene_sample_delta1));
				half3 scene_sample_horizon_vec1 = half3 (scene_sample_delta1 / float (scene_sample_dist1));
				half  scene_weight1 = clamp (scene_sample_dist1 * falloff_mul + falloff_add, half (0.0), half (1.0));
				scene_weight1 *= sample_depth1.y < 65504.0 && in_bounds1 ? half (1.0) : half (0.0);
				scene_shc1 = dot (scene_sample_horizon_vec1, view_vec);
				scene_shc1 = mix (low_horizon_cos1, scene_shc1, scene_weight1);
			}

			world_horizon_cos0 = max (world_horizon_cos0, world_shc0);
			world_horizon_cos1 = max (world_horizon_cos1, world_shc1);
			scene_horizon_cos0 = max (scene_horizon_cos0, scene_shc0);
			scene_horizon_cos1 = max (scene_horizon_cos1, scene_shc1);
		}

		projected_normal_vec_length = mix (projected_normal_vec_length, half (1.0), half (0.05));

		half world_h0 = -fast_acos (world_horizon_cos1);
		half world_h1 = fast_acos (world_horizon_cos0);
		half scene_h0 = -fast_acos (scene_horizon_cos1);
		half scene_h1 = fast_acos (scene_horizon_cos0);

		half world_iarc0 = (cos_norm + half (2.0) * world_h0 * sin (n) - cos (half (2.0) * world_h0 - n)) / half (4.0);
		half world_iarc1 = (cos_norm + half (2.0) * world_h1 * sin (n) - cos (half (2.0) * world_h1 - n)) / half (4.0);
		half scene_iarc0 = (cos_norm + half (2.0) * scene_h0 * sin (n) - cos (half (2.0) * scene_h0 - n)) / half (4.0);
		half scene_iarc1 = (cos_norm + half (2.0) * scene_h1 * sin (n) - cos (half (2.0) * scene_h1 - n)) / half (4.0);
		half world_local_visibility = projected_normal_vec_length * (world_iarc0 + world_iarc1);
		world_visibility += world_local_visibility;
		half scene_local_visibility = projected_normal_vec_length * (scene_iarc0 + scene_iarc1);
		scene_visibility += scene_local_visibility;
	}
	// Divide out world-only occlusion so only the entities' additional occlusion remains.
	// The world/scene ratio is vkQuake-specific; retain its FP32 division before XeGTAO output processing.
	half visibility = half (clamp (float (scene_visibility) / max (float (world_visibility), 1e-5), 0.0, 1.0));
	visibility = pow (visibility, half (c.final_value_power));
	visibility = max (half (0.03), visibility);

	visibility = clamp (visibility / half (1.5), half (0.0), half (1.0));
	imageStore (out_working_ao_term, ivec2 (pixel), vec4 (visibility));
}
