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
layout (set = 0, binding = 0) uniform sampler2D source;
layout (set = 1, binding = 0) uniform sampler2D edges;
layout (r8, set = 2, binding = 0) uniform writeonly image2D output_texture;
#include "gtao.inc"

// Precision boundaries follow XeGTAO_Denoise and its helpers (see gtao.inc).

half4 unpack_edges (half _packed_val)
{
	uint  packed_val = uint ((_packed_val * half (255.5)));
	half4 edges_lrtb;
	edges_lrtb.x = half ((packed_val >> 6) & 0x03) / half (3.0);
	edges_lrtb.y = half ((packed_val >> 4) & 0x03) / half (3.0);
	edges_lrtb.z = half ((packed_val >> 2) & 0x03) / half (3.0);
	edges_lrtb.w = half ((packed_val >> 0) & 0x03) / half (3.0);

	return clamp (edges_lrtb, half (0.0), half (1.0));
}

void add_sample (half ssao_value, half edge_value, inout half sum, inout half sum_weight)
{
	sum += edge_value * ssao_value;
	sum_weight += edge_value;
}

void main ()
{
	uvec2		   pixel = gl_GlobalInvocationID.xy * uvec2 (2, 1);
	gtao_constants c = get_gtao_constants ();
	if (any (greaterThanEqual (pixel, uvec2 (c.viewport_size))))
		return;
	bool final_apply = pc.settings.z != 0;

	half blur_amount = final_apply ? half (c.denoise_blur_beta) : (half (c.denoise_blur_beta) / half (5.0));
	half diag_weight = half (0.85 * 0.5);

	vec2  gather_center = vec2 (pixel.x, pixel.y) * c.viewport_pixel_size;
	half4 edges_q0 = half4 (textureGatherOffset (edges, gather_center, ivec2 (0, 0), 0));
	half4 edges_q1 = half4 (textureGatherOffset (edges, gather_center, ivec2 (2, 0), 0));
	half4 edges_q2 = half4 (textureGatherOffset (edges, gather_center, ivec2 (1, 2), 0));

	half4 vis_q0 = half4 (textureGatherOffset (source, gather_center, ivec2 (0, 0), 0));
	half4 vis_q1 = half4 (textureGatherOffset (source, gather_center, ivec2 (2, 0), 0));
	half4 vis_q2 = half4 (textureGatherOffset (source, gather_center, ivec2 (0, 2), 0));
	half4 vis_q3 = half4 (textureGatherOffset (source, gather_center, ivec2 (2, 2), 0));

	for (int side = 0; side < 2; side++)
	{
		ivec2 pix_coord = ivec2 (pixel.x + side, pixel.y);

		half4 edges_l_lrtb = unpack_edges ((side == 0) ? (edges_q0.x) : (edges_q0.y));
		half4 edges_t_lrtb = unpack_edges ((side == 0) ? (edges_q0.z) : (edges_q1.w));
		half4 edges_r_lrtb = unpack_edges ((side == 0) ? (edges_q1.x) : (edges_q1.y));
		half4 edges_b_lrtb = unpack_edges ((side == 0) ? (edges_q2.w) : (edges_q2.z));

		half4 edges_c_lrtb = unpack_edges ((side == 0) ? (edges_q0.y) : (edges_q1.x));

		edges_c_lrtb *= half4 (edges_l_lrtb.y, edges_r_lrtb.x, edges_t_lrtb.w, edges_b_lrtb.z);

		half leak_threshold = half (2.5);
		half leak_strength = half (0.5);
		half edginess =
			(clamp (half (4.0) - leak_threshold - dot (edges_c_lrtb, half4 (1.0)), half (0.0), half (1.0)) / (half (4.0) - leak_threshold)) * leak_strength;
		edges_c_lrtb = clamp (edges_c_lrtb + edginess, half (0.0), half (1.0));

		half weight_tl = diag_weight * (edges_c_lrtb.x * edges_l_lrtb.z + edges_c_lrtb.z * edges_t_lrtb.x);
		half weight_tr = diag_weight * (edges_c_lrtb.z * edges_t_lrtb.y + edges_c_lrtb.y * edges_r_lrtb.z);
		half weight_bl = diag_weight * (edges_c_lrtb.w * edges_b_lrtb.x + edges_c_lrtb.x * edges_l_lrtb.w);
		half weight_br = diag_weight * (edges_c_lrtb.y * edges_r_lrtb.w + edges_c_lrtb.w * edges_b_lrtb.y);

		half ssao_value = (side == 0) ? (vis_q0[1]) : (vis_q1[0]);
		half ssao_value_l = (side == 0) ? (vis_q0[0]) : (vis_q0[1]);
		half ssao_value_t = (side == 0) ? (vis_q0[2]) : (vis_q1[3]);
		half ssao_value_r = (side == 0) ? (vis_q1[0]) : (vis_q1[1]);
		half ssao_value_b = (side == 0) ? (vis_q2[2]) : (vis_q3[3]);
		half ssao_value_tl = (side == 0) ? (vis_q0[3]) : (vis_q0[2]);
		half ssao_value_br = (side == 0) ? (vis_q3[3]) : (vis_q3[2]);
		half ssao_value_tr = (side == 0) ? (vis_q1[3]) : (vis_q1[2]);
		half ssao_value_bl = (side == 0) ? (vis_q2[3]) : (vis_q2[2]);

		half sum_weight = blur_amount;
		half sum = ssao_value * sum_weight;

		add_sample (ssao_value_l, edges_c_lrtb.x, sum, sum_weight);
		add_sample (ssao_value_r, edges_c_lrtb.y, sum, sum_weight);
		add_sample (ssao_value_t, edges_c_lrtb.z, sum, sum_weight);
		add_sample (ssao_value_b, edges_c_lrtb.w, sum, sum_weight);

		add_sample (ssao_value_tl, weight_tl, sum, sum_weight);
		add_sample (ssao_value_tr, weight_tr, sum, sum_weight);
		add_sample (ssao_value_bl, weight_bl, sum, sum_weight);
		add_sample (ssao_value_br, weight_br, sum, sum_weight);

		half visibility = sum / sum_weight;
		visibility *= final_apply ? half (1.5) : half (1.0);
		imageStore (output_texture, pix_coord, vec4 (visibility));
	}
}
