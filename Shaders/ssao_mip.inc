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

#include "gtao.inc"

layout (local_size_x = 64) in;
#ifdef MULTISAMPLED
layout (set = 0, binding = 0) uniform sampler2DMS scene_depth;
#else
layout (set = 0, binding = 0) uniform sampler2D scene_depth;
#endif
layout (rg16f, set = 0, binding = 1) uniform image2D mip0;
layout (rg16f, set = 0, binding = 2) uniform writeonly image2D mip1;
layout (rg16f, set = 0, binding = 3) uniform writeonly image2D mip2;
layout (rg16f, set = 0, binding = 4) uniform writeonly image2D mip3;
layout (rg16f, set = 0, binding = 5) uniform writeonly image2D mip4;
shared half2 depths1[64];
shared half2 depths2[16];
shared half2 depths3[4];

// XeGTAO_DepthMIPFilter: all depth-filter arithmetic uses lpfloat.
half reduce_depth (half a, half b, half c, half d)
{
	// vkQuake reserves 65504 for missing depth in either channel.
	half farthest = max (
		max (a < half (65504.0) ? a : half (0.0), b < half (65504.0) ? b : half (0.0)),
		max (c < half (65504.0) ? c : half (0.0), d < half (65504.0) ? d : half (0.0)));
	if (farthest == half (0.0))
		return half (65504.0);
	a = a < half (65504.0) ? a : farthest;
	b = b < half (65504.0) ? b : farthest;
	c = c < half (65504.0) ? c : farthest;
	d = d < half (65504.0) ? d : farthest;

	half depth_range_scale_factor = half (0.75);
	half effect_radius = depth_range_scale_factor * half (pc.settings.x) * half (1.457);
	half falloff_range = half (0.615) * effect_radius;
	half falloff_from = effect_radius * (half (1.0) - half (get_gtao_constants ().effect_falloff_range));
	half falloff_mul = half (-1.0) / falloff_range;
	half falloff_add = falloff_from / falloff_range + half (1.0);

	half weight0 = clamp ((farthest - a) * falloff_mul + falloff_add, half (0.0), half (1.0));
	half weight1 = clamp ((farthest - b) * falloff_mul + falloff_add, half (0.0), half (1.0));
	half weight2 = clamp ((farthest - c) * falloff_mul + falloff_add, half (0.0), half (1.0));
	half weight3 = clamp ((farthest - d) * falloff_mul + falloff_add, half (0.0), half (1.0));
	half weight_sum = weight0 + weight1 + weight2 + weight3;
	return (weight0 * a + weight1 * b + weight2 * c + weight3 * d) / weight_sum;
}

half2 reduce (half2 a, half2 b, half2 c, half2 d)
{
	return half2 (reduce_depth (a.x, b.x, c.x, d.x), reduce_depth (a.y, b.y, c.y, d.y));
}

half2 shuffle_depth (half2 depth, uint mask)
{
#ifdef USE_FP16
	// Transport the half pair through a 32-bit shuffle; shaderSubgroupExtendedTypes is not required.
	uint packed_depth = packHalf2x16 (vec2 (depth));
	return half2 (unpackHalf2x16 (subgroupShuffleXor (packed_depth, mask)));
#else
	return subgroupShuffleXor (depth, mask);
#endif
}

uint morton_x (uint i)
{
	return (i & 1u) | ((i >> 1u) & 2u) | ((i >> 2u) & 4u);
}

half2 read2 (ivec2 p)
{
	ivec2 last = max (ivec2 (0), min (ivec2 (3), imageSize (mip2) - ivec2 (gl_WorkGroupID.xy) * 4 - 1));
	p = clamp (p, ivec2 (0), last);
	return depths2[p.y * 4 + p.x];
}

half2 read3 (ivec2 p)
{
	ivec2 last = max (ivec2 (0), min (ivec2 (1), imageSize (mip3) - ivec2 (gl_WorkGroupID.xy) * 2 - 1));
	p = clamp (p, ivec2 (0), last);
	return depths3[p.y * 2 + p.x];
}

// Each pixel has one owner: preserve world depth and resolve scene depth in registers.
half2 prepare_depth (ivec2 p)
{
	if (any (greaterThanEqual (p, imageSize (mip0))))
		return half2 (65504.0);
	float depth = 0.0;
	if (all (greaterThanEqual (vec2 (p), pc.viewport.xy)) && all (lessThan (vec2 (p), pc.viewport.xy + round (1.0 / pc.viewport.zw))))
	{
#ifdef MULTISAMPLED
		for (int s = 0; s < int (pc.projection.w); ++s)
			depth = max (depth, texelFetch (scene_depth, p, s).r);
#else
		depth = texelFetch (scene_depth, p, 0).r;
#endif
	}
	half2 depths = half2 (imageLoad (mip0, p).x, depth > 0.0 ? min (pc.projection.z / depth, 65472.0) : 65504.0);
	// Match the already stored world depth before reducing the two channels together.
	depths = half2 (unpackHalf2x16 (packHalf2x16 (vec2 (depths))));
	imageStore (mip0, p, vec4 (depths, 0, 0));
	return depths;
}

void main ()
{
	// Full subgroups, as in screen_effects: each four adjacent lanes form a 2x2 quad.
	// No early return: even edge lanes must participate in shuffles and barriers.
	uint  index = gl_SubgroupID * gl_SubgroupSize + gl_SubgroupInvocationID;
	ivec2 local = ivec2 (morton_x (index), morton_x (index >> 1));
	ivec2 p = ivec2 (gl_WorkGroupID.xy) * 8 + local;
	ivec2 q = p * 2;
	half2 depth = reduce (prepare_depth (q), prepare_depth (q + ivec2 (1, 0)), prepare_depth (q + ivec2 (0, 1)), prepare_depth (q + ivec2 (1, 1)));
	// An odd trailing row/column still needs mip 0, even when it contributes to no higher mip.
	ivec2 origin = ivec2 (gl_WorkGroupID.xy) * 8;
	if (any (greaterThanEqual (origin, imageSize (mip1))))
		return;
	// Replicate the last valid mip-1 value for partial tiles, matching the existing reduction.
	// Full tiles stay entirely in registers and use the subgroup reduction below.
	if (any (greaterThan (origin + 8, imageSize (mip1))))
	{
		depths1[local.y * 8 + local.x] = depth;
		barrier ();
		ivec2 clamped = min (local, imageSize (mip1) - origin - 1);
		depth = depths1[clamped.y * 8 + clamped.x];
	}
	if (all (lessThan (p, imageSize (mip1))))
		imageStore (mip1, p, vec4 (depth, 0, 0));
	half2 a = depth;
	half2 b = shuffle_depth (depth, 1);
	half2 c = shuffle_depth (depth, 2);
	half2 d = shuffle_depth (depth, 3);
	depth = reduce (a, b, c, d);
	if ((index & 3u) == 0)
	{
		if (gl_SubgroupSize < 16)
			depths2[(local.y / 2) * 4 + local.x / 2] = depth;
		if (all (lessThan (p / 2, imageSize (mip2))))
			imageStore (mip2, p / 2, vec4 (depth, 0, 0));
	}
	if (gl_SubgroupSize >= 16)
	{
		a = depth;
		b = shuffle_depth (depth, 4);
		c = shuffle_depth (depth, 8);
		d = shuffle_depth (depth, 12);
		depth = reduce (a, b, c, d);
		if ((index & 15u) == 0)
		{
			if (gl_SubgroupSize < 64)
				depths3[(local.y / 4) * 2 + local.x / 4] = depth;
			if (all (lessThan (p / 4, imageSize (mip3))))
				imageStore (mip3, p / 4, vec4 (depth, 0, 0));
		}
		if (gl_SubgroupSize >= 64)
		{
			a = depth;
			b = shuffle_depth (depth, 16);
			c = shuffle_depth (depth, 32);
			d = shuffle_depth (depth, 48);
			if (index == 0 && all (lessThan (ivec2 (gl_WorkGroupID.xy), imageSize (mip4))))
				imageStore (mip4, ivec2 (gl_WorkGroupID.xy), vec4 (reduce (a, b, c, d), 0, 0));
			return;
		}
	}
	else
	{
		barrier ();
		if (gl_LocalInvocationIndex < 4)
		{
			ivec2 pos = ivec2 (gl_LocalInvocationIndex & 1u, gl_LocalInvocationIndex >> 1u);
			q = pos * 2;
			depth = reduce (read2 (q), read2 (q + ivec2 (1, 0)), read2 (q + ivec2 (0, 1)), read2 (q + ivec2 (1, 1)));
			depths3[gl_LocalInvocationIndex] = depth;
			p = ivec2 (gl_WorkGroupID.xy) * 2 + pos;
			if (all (lessThan (p, imageSize (mip3))))
				imageStore (mip3, p, vec4 (depth, 0, 0));
		}
	}
	barrier ();
	if (gl_LocalInvocationIndex == 0 && all (lessThan (ivec2 (gl_WorkGroupID.xy), imageSize (mip4))))
		imageStore (mip4, ivec2 (gl_WorkGroupID.xy), vec4 (reduce (read3 (ivec2 (0)), read3 (ivec2 (1, 0)), read3 (ivec2 (0, 1)), read3 (ivec2 (1))), 0, 0));
}
