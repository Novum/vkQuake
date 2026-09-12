// MIT License
//
// Copyright (c) 2020-2024 NVIDIA CORPORATION
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

// Remap 2D workgroups into vertical bands for texture cache locality. Local
// invocation IDs are unchanged, including any subgroup or Morton-order mapping.
// https://github.com/LouisBavoil/ThreadGroupIDSwizzling/blob/master/ThreadGroupTilingX.hlsl
uvec2 tiled_work_group_id ()
{
	const uint tile_width = 32;
	uvec2 grid = gl_NumWorkGroups.xy;
	if (grid.x <= tile_width || grid.y == 1)
		return gl_WorkGroupID.xy;
	uint group_index = gl_WorkGroupID.y * grid.x + gl_WorkGroupID.x;
	uint tile_size = tile_width * grid.y;
	uint tile_index = group_index / tile_size;
	uint tile_offset = group_index - tile_index * tile_size;
	uint tile_start = tile_index * tile_width;
	uvec2 group = uvec2 (tile_start + tile_offset % tile_width, tile_offset / tile_width);
	if (tile_start + tile_width > grid.x)
	{
		// The last band may contain fewer than tile_width columns.
		uint tail_width = grid.x - tile_start;
		group = uvec2 (tile_start + tile_offset % tail_width, tile_offset / tail_width);
	}
	return group;
}

// For shaders whose local invocation grid directly maps to a 2D image tile.
uvec2 tiled_global_invocation_id ()
{
	return tiled_work_group_id () * gl_WorkGroupSize.xy + gl_LocalInvocationID.xy;
}
