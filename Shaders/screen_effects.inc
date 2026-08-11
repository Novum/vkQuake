#extension GL_EXT_control_flow_attributes : enable

#define NUM_PALETTE_OCTREE_NODES 184

struct OctreeNode
{
	uvec4 children[2];
};

layout (set = 0, binding = 0) uniform sampler2D input_tex;
layout (set = 0, binding = 1) uniform sampler2D blue_noise_tex;
layout (set = 0, binding = 5) uniform sampler2D gtao_tex;
layout (set = 0, binding = 6) uniform sampler2D gtao_view_depth_tex;
layout (set = 0, binding = 7) uniform sampler2D gtao_denoise_tex;
layout (set = 0, binding = 3) uniform samplerBuffer palette_colors;
layout (set = 0, binding = 4) uniform PaletteOctree
{
	OctreeNode nodes[NUM_PALETTE_OCTREE_NODES];
}
palette_octree;

layout (push_constant) uniform PushConsts
{
	uvec2 clamp_size;
	vec2  screen_size_rcp;
	float aspect_ratio;
	float time;
	uint  flags;
	float poly_blend_r;
	float poly_blend_g;
	float poly_blend_b;
	float poly_blend_a;
	float gtao_strength;
	uint  gtao_debug_mode;
	uint  gtao_denoise;
	float gtao_multibounce;
	uint  gtao_halfres;
}
push_constants;

#if defined(USE_SUBGROUP_OPS)
uint Compact1By1 (uint x)
{
	x &= 0x55555555;
	if (gl_SubgroupSize > 4)
		x = (x ^ (x >> 1)) & 0x33333333;
	if (gl_SubgroupSize > 16)
		x = (x ^ (x >> 2)) & 0x0f0f0f0f;
	return x;
}

uint DecodeMorton2X (uint code)
{
	return Compact1By1 (code >> 0);
}

uint DecodeMorton2Y (uint code)
{
	return Compact1By1 (code >> 1);
}
#endif

float blue_noise (ivec2 u)
{
	return texelFetch (blue_noise_tex, ivec2 (uint (u.x) % 64, uint (u.y) % 64), 0).r;
}

vec4 gtao_fetch (ivec2 pixel)
{
	return (push_constants.gtao_denoise & 1u) != 0u ? texelFetch (gtao_denoise_tex, pixel, 0) : texelFetch (gtao_tex, pixel, 0);
}

float gtao_depth_similarity (float center_depth, float sample_depth)
{
	if (center_depth <= 0.0f || sample_depth <= 0.0f)
		return center_depth == sample_depth ? 1.0f : 0.0f;
	return clamp (1.25f - abs (sample_depth - center_depth) / max (center_depth * 0.011f, 0.00001f), 0.0f, 1.0f);
}

float gtao_relative_depth_difference (float center_depth, float sample_depth)
{
	if (center_depth <= 0.0f || sample_depth <= 0.0f)
		return center_depth == sample_depth ? 0.0f : 1e30f;
	return abs (sample_depth - center_depth) / max (center_depth, 0.00001f);
}

vec4 gtao_resolve (ivec2 target_pixel, vec2 target_uv, bool warped)
{
	if (warped)
		target_pixel = clamp (ivec2 (target_uv / push_constants.screen_size_rcp), ivec2 (0), ivec2 (push_constants.clamp_size));
	if (push_constants.gtao_halfres == 0u)
		return gtao_fetch (target_pixel);

	// Half-resolution AO is stored contiguously in the upper-left working
	// region. AO texel q represents source pixel 2*q.
	ivec2 source_max = ivec2 (push_constants.clamp_size);
	ivec2 working_max = source_max / 2;
	ivec2 base = clamp (target_pixel / 2, ivec2 (0), working_max);
	if (all (equal (target_pixel, base * 2)))
		return gtao_fetch (base);
	ivec2 candidates[4] = ivec2[4] (
		base, min (base + ivec2 (1, 0), working_max),
		min (base + ivec2 (0, 1), working_max), min (base + ivec2 (1, 1), working_max));
	float target_depth = texelFetch (gtao_view_depth_tex, target_pixel, 0).r;
	vec2 fraction = clamp (vec2 (target_pixel - base * 2) * 0.5f, 0.0f, 1.0f);
	vec4 spatial_weights = vec4 ((1.0f - fraction.x) * (1.0f - fraction.y), fraction.x * (1.0f - fraction.y),
		(1.0f - fraction.x) * fraction.y, fraction.x * fraction.y);
	vec4 closest_result = gtao_fetch (base);
	vec4 best_result = closest_result;
	float visibility_sum = 0.0f;
	float weight_sum = 0.0f;
	float best_weight = 0.0f;
	float closest_depth_difference = 1e30f;
	for (int i = 0; i < 4; ++i)
	{
		if (spatial_weights[i] <= 0.0f)
			continue;
		ivec2 source_pixel = min (candidates[i] * 2, source_max);
		float sample_depth = texelFetch (gtao_view_depth_tex, source_pixel, 0).r;
		float depth_difference = gtao_relative_depth_difference (target_depth, sample_depth);
		vec4 candidate = gtao_fetch (candidates[i]);
		if (depth_difference < closest_depth_difference)
		{
			closest_depth_difference = depth_difference;
			closest_result = candidate;
		}
		float weight = spatial_weights[i] * gtao_depth_similarity (target_depth, sample_depth);
		visibility_sum += candidate.r * weight;
		weight_sum += weight;
		if (weight > best_weight)
		{
			best_weight = weight;
			best_result = candidate;
		}
	}
	vec4 result = weight_sum > 0.0001f ? best_result : closest_result;
	if (weight_sum > 0.0001f)
		result.r = visibility_sum / weight_sum;
	return result;
}

vec3 gtao_multibounce_visibility (float visibility, vec3 albedo)
{
	vec3 a = 2.0404f * albedo - 0.3324f;
	vec3 b = -4.7951f * albedo + 0.6417f;
	vec3 c = 2.7552f * albedo + 0.6903f;
	return max (vec3 (visibility), ((visibility * a + b) * visibility + c) * visibility);
}

#define SCREEN_EFFECT_FLAG_SCALE_MASK 0x3
#define SCREEN_EFFECT_FLAG_SCALE_2X	  0x1
#define SCREEN_EFFECT_FLAG_SCALE_4X	  0x2
#define SCREEN_EFFECT_FLAG_SCALE_8X	  0x3
#define SCREEN_EFFECT_FLAG_WATER_WARP 0x4
#define SCREEN_EFFECT_FLAG_PALETTIZE  0x8
#define SCREEN_EFFECT_FLAG_MENU		  0x10
#define SCREEN_EFFECT_FLAG_GTAO		  0x20

#if defined(SCALING)
// Vulkan guarantees 16384 bytes of shared memory, so host doesn't need to check
shared uint group_red[16];
shared uint group_green[16];
shared uint group_blue[16];
#endif

#if defined(SCALING) && defined(USE_SUBGROUP_OPS)
// Vulkan spec states that workgroup size in x dimension must be a multiple of the
// subgroup size for VK_PIPELINE_SHADER_STAGE_CREATE_REQUIRE_FULL_SUBGROUPS_BIT_EXT
layout (local_size_x = 64, local_size_y = 1) in;
#else
layout (local_size_x = 8, local_size_y = 8) in;
#endif
void main ()
{
	const uint tile_size_x = 8;
	const uint tile_size_y = 8;

#if defined(SCALING) && defined(USE_SUBGROUP_OPS)
	// gl_SubgroupSize >= 4 && gl_SubgroupSize <= 64 otherwise the host code chooses the shared mem only shader
	// Vulkan guarantees subgroup size must be power of two and between 1 and 128
	uint subgroup_width = 0;
	uint subgroup_height = 0;
	switch (gl_SubgroupSize)
	{
	case 4:
		subgroup_width = 2;
		subgroup_height = 2;
		break;
	case 8:
		subgroup_width = 4;
		subgroup_height = 2;
		break;
	case 16:
		subgroup_width = 4;
		subgroup_height = 4;
		break;
	case 32:
		subgroup_width = 8;
		subgroup_height = 4;
		break;
	case 64:
		subgroup_width = 8;
		subgroup_height = 8;
		break;
	}
	// Morton order for subgroupShuffleXor
	const uint subgroup_x = DecodeMorton2X (gl_SubgroupInvocationID);
	const uint subgroup_y = DecodeMorton2Y (gl_SubgroupInvocationID);
	const uint num_subgroups_x = (tile_size_x + subgroup_width - 1) / subgroup_width;
	const uint local_x = ((gl_SubgroupID % num_subgroups_x) * subgroup_width) + subgroup_x;
	const uint local_y = ((gl_SubgroupID / num_subgroups_x) * subgroup_height) + subgroup_y;
	const uint pos_x = (gl_WorkGroupID.x * tile_size_x) + local_x;
	const uint pos_y = (gl_WorkGroupID.y * tile_size_y) + local_y;
#else
	const uint local_x = gl_LocalInvocationID.x;
	const uint local_y = gl_LocalInvocationID.y;
	const uint pos_x = gl_GlobalInvocationID.x;
	const uint pos_y = gl_GlobalInvocationID.y;
#endif

	vec4 color = vec4 (0.0f, 0.0f, 0.0f, 0.0f);
	vec2 effect_texcoord = (vec2 (pos_x, pos_y) + 0.5f) * push_constants.screen_size_rcp;

	[[branch]] if ((push_constants.flags & SCREEN_EFFECT_FLAG_WATER_WARP) != 0)
	{
		const float cycle_x = 3.14159f * 5.0f;
		const float cycle_y = cycle_x * push_constants.aspect_ratio;
		const float amp_x = 1.0f / 300.0f;
		const float amp_y = amp_x * push_constants.aspect_ratio;

		const float pos_x_norm = float (pos_x) * push_constants.screen_size_rcp.x;
		const float pos_y_norm = float (pos_y) * push_constants.screen_size_rcp.y;

		const float tex_x = (pos_x_norm + (sin (pos_y_norm * cycle_x + push_constants.time) * amp_x)) * (1.0f - amp_x * 2.0f) + amp_x;
		const float tex_y = (pos_y_norm + (sin (pos_x_norm * cycle_y + push_constants.time) * amp_y)) * (1.0f - amp_y * 2.0f) + amp_y;

		effect_texcoord = vec2 (tex_x, tex_y);
		color = texture (input_tex, effect_texcoord);
	}
	else
		color = texelFetch (input_tex, ivec2 (min (push_constants.clamp_size.x, pos_x), min (push_constants.clamp_size.y, pos_y)), 0);

	[[branch]] if ((push_constants.flags & SCREEN_EFFECT_FLAG_GTAO) != 0)
	{
		const ivec2 ao_pixel = ivec2 (min (push_constants.clamp_size.x, pos_x), min (push_constants.clamp_size.y, pos_y));
		const bool gtao_warped = (push_constants.flags & SCREEN_EFFECT_FLAG_WATER_WARP) != 0;
		const vec4 ao_sample = gtao_resolve (ao_pixel, effect_texcoord, gtao_warped);
		if (push_constants.gtao_debug_mode == 1u || push_constants.gtao_debug_mode == 7u)
			color.rgb = vec3 (clamp (ao_sample.r, 0.0f, 1.0f));
		else if (push_constants.gtao_debug_mode > 1u)
			color.rgb = ao_sample.rgb;
		else
		{
			float ao_visibility = clamp (ao_sample.r, 0.0f, 1.0f);
			float visibility = clamp (1.0f - ((1.0f - ao_visibility) * push_constants.gtao_strength), 0.0f, 1.0f);
			vec3 single_bounce = color.rgb * visibility;
			vec3 multi_bounce = color.rgb * gtao_multibounce_visibility (visibility, color.rgb);
			color.rgb = mix (single_bounce, multi_bounce, clamp (push_constants.gtao_multibounce, 0.0f, 1.0f));
		}
	}

	[[branch]] if ((push_constants.flags & SCREEN_EFFECT_FLAG_PALETTIZE) != 0)
	{
		uvec3 search_color = uvec3 (color.rgb * 255.0f);
		uint  current_node_offset = 0;
		uint  current_shift = 7;
		uvec3 node_coords = uvec3 (0u);
		[[loop]] while (true)
		{
			const uvec3 node_offsets = (search_color - node_coords) >> current_shift;
			const uint	child_index = node_offsets.r + node_offsets.g * 2 + node_offsets.b * 4;
			const uint	offset = palette_octree.nodes[current_node_offset].children[child_index / 4][child_index % 4];
			[[branch]] if ((offset & (1u << 31)) == 0)
			{
				node_coords = node_coords + (node_offsets << current_shift);
				current_shift -= 1;
				current_node_offset = offset;
			}
			else
			{
				const uint num_colors = offset & 0xF;
				const uint colors_offset = (offset >> 4) & 0xFFFF;
				vec3	   best_color = texelFetch (palette_colors, int (colors_offset)).rgb;
				vec3	   second_best_color = best_color;
				float	   best_dist_sq = dot (color.rgb - best_color, color.rgb - best_color);
				float	   second_best_dist_sq = 1e38f;
				[[loop]] for (int i = 1; i < num_colors; ++i)
				{
					vec3  palette_color = texelFetch (palette_colors, int (colors_offset + i)).rgb;
					float dist_sq = dot (color.rgb - palette_color, color.rgb - palette_color);
					[[flatten]] if (dist_sq < best_dist_sq)
					{
						second_best_dist_sq = best_dist_sq;
						second_best_color = best_color;
						best_color = palette_color;
						best_dist_sq = dist_sq;
					}
					else if ((dist_sq > best_dist_sq) && (dist_sq < second_best_dist_sq))
					{
						second_best_color = palette_color;
						second_best_dist_sq = dist_sq;
					}
				}

				float luma[3][3];
				[[unroll]] for (int y = -1; y <= 1; ++y)
				{
					[[unroll]] for (int x = -1; x <= 1; ++x)
					{
						uint sample_x = min (push_constants.clamp_size.x, int (pos_x) + x);
						uint sample_y = min (push_constants.clamp_size.y, int (pos_y) + y);
						vec3 rgb = texelFetch (input_tex, ivec2 (sample_x, sample_y), 0).rgb;
						luma[x + 1][y + 1] = (rgb.r + rgb.g + rgb.b) / 3.0f;
					}
				}

				// Run a sobel filter because we don't want to apply too much dithering to very smooth screen areas.
				float s[2] = {1.0f, 2.0f};
				vec2  sobel_xy = vec2 (
					 (s[0] * luma[0][0]) - (s[0] * luma[2][0]) + (s[1] * luma[0][1]) - (s[1] * luma[2][1]) + (s[0] * luma[0][2]) - (s[0] * luma[2][2]),
					 (s[0] * luma[0][0]) - (s[0] * luma[0][2]) + (s[1] * luma[1][0]) - (s[1] * luma[1][2]) + (s[0] * luma[2][0]) - (s[0] * luma[2][2]));
				float sobel = dot (sobel_xy, sobel_xy);

				float p = clamp ((5.0f - (sobel * 1e4f)), 1.0f, 5.0f);
				float a = pow (best_dist_sq, p);
				float b = pow (second_best_dist_sq, p);
				float ratio = a / (a + b);
#if defined(SCALING)
				uint noise_shift = push_constants.flags & SCREEN_EFFECT_FLAG_SCALE_MASK;
#else
				uint noise_shift = 0;
#endif
				const float noise = blue_noise (ivec2 (pos_x >> noise_shift, pos_y >> noise_shift));
				color.rgb = (ratio < noise) ? best_color : second_best_color;
				break;
			}
		}
	}

#if defined(SCALING)
	[[branch]] if ((push_constants.flags & SCREEN_EFFECT_FLAG_SCALE_MASK) == SCREEN_EFFECT_FLAG_SCALE_2X)
	{
#if defined(USE_SUBGROUP_OPS)
		if (gl_SubgroupSize >= 4)
		{
			color.r = uintBitsToFloat (subgroupShuffle (floatBitsToUint (color.r), gl_SubgroupInvocationID & ~0x3));
			color.g = uintBitsToFloat (subgroupShuffle (floatBitsToUint (color.g), gl_SubgroupInvocationID & ~0x3));
			color.b = uintBitsToFloat (subgroupShuffle (floatBitsToUint (color.b), gl_SubgroupInvocationID & ~0x3));
		}
		else
		{
#endif
			const uint local_idx = local_x + (local_y * tile_size_x);
			const uint shared_mem_idx = (local_x / 2) + ((local_y / 2) * (tile_size_x / 2));
			if (((local_x % 2) == 0) && ((local_y % 2) == 0))
			{
				group_red[shared_mem_idx] = floatBitsToUint (color.r);
				group_green[shared_mem_idx] = floatBitsToUint (color.g);
				group_blue[shared_mem_idx] = floatBitsToUint (color.b);
			}
			barrier ();
			color.r = uintBitsToFloat (group_red[shared_mem_idx]);
			color.g = uintBitsToFloat (group_green[shared_mem_idx]);
			color.b = uintBitsToFloat (group_blue[shared_mem_idx]);
#if defined(USE_SUBGROUP_OPS)
		}
#endif
	}
	else if ((push_constants.flags & SCREEN_EFFECT_FLAG_SCALE_MASK) == SCREEN_EFFECT_FLAG_SCALE_4X)
	{
#if defined(USE_SUBGROUP_OPS)
		if (gl_SubgroupSize >= 16)
		{
			color.r = uintBitsToFloat (subgroupShuffle (floatBitsToUint (color.r), gl_SubgroupInvocationID & ~0xF));
			color.g = uintBitsToFloat (subgroupShuffle (floatBitsToUint (color.g), gl_SubgroupInvocationID & ~0xF));
			color.b = uintBitsToFloat (subgroupShuffle (floatBitsToUint (color.b), gl_SubgroupInvocationID & ~0xF));
		}
		else
		{
#endif
			const uint local_idx = local_x + (local_y * tile_size_x);
			const uint shared_mem_idx = (local_x / 4) + ((local_y / 4) * (tile_size_x / 4));
			if (((local_x % 4) == 0) && ((local_y % 4) == 0))
			{
				group_red[shared_mem_idx] = floatBitsToUint (color.r);
				group_green[shared_mem_idx] = floatBitsToUint (color.g);
				group_blue[shared_mem_idx] = floatBitsToUint (color.b);
			}
			barrier ();
			color.r = uintBitsToFloat (group_red[shared_mem_idx]);
			color.g = uintBitsToFloat (group_green[shared_mem_idx]);
			color.b = uintBitsToFloat (group_blue[shared_mem_idx]);
#if defined(USE_SUBGROUP_OPS)
		}
#endif
	}
	else if ((push_constants.flags & SCREEN_EFFECT_FLAG_SCALE_MASK) == SCREEN_EFFECT_FLAG_SCALE_8X)
	{
#if defined(USE_SUBGROUP_OPS)
		if (gl_SubgroupSize >= 64)
		{
			color.r = uintBitsToFloat (subgroupShuffle (floatBitsToUint (color.r), gl_SubgroupInvocationID & ~0x3F));
			color.g = uintBitsToFloat (subgroupShuffle (floatBitsToUint (color.g), gl_SubgroupInvocationID & ~0x3F));
			color.b = uintBitsToFloat (subgroupShuffle (floatBitsToUint (color.b), gl_SubgroupInvocationID & ~0x3F));
		}
		else
		{
#endif
			const uint local_idx = local_x + (local_y * tile_size_x);
			const uint shared_mem_idx = (local_x / 8) + ((local_y / 8) * (tile_size_x / 8));
			if (((local_x % 8) == 0) && ((local_y % 8) == 0))
			{
				group_red[shared_mem_idx] = floatBitsToUint (color.r);
				group_green[shared_mem_idx] = floatBitsToUint (color.g);
				group_blue[shared_mem_idx] = floatBitsToUint (color.b);
			}
			barrier ();
			color.r = uintBitsToFloat (group_red[shared_mem_idx]);
			color.g = uintBitsToFloat (group_green[shared_mem_idx]);
			color.b = uintBitsToFloat (group_blue[shared_mem_idx]);
#if defined(USE_SUBGROUP_OPS)
		}
#endif
	}
#endif

	color.rgb = mix (color.rgb, vec3 (push_constants.poly_blend_r, push_constants.poly_blend_g, push_constants.poly_blend_b), push_constants.poly_blend_a);
	[[branch]] if ((push_constants.flags & SCREEN_EFFECT_FLAG_MENU) != 0)
		color.rgb = mix (color.rgb, vec3 (color.r * 0.3f + color.g * 0.59f + color.b * 0.11f), 0.5f) * 0.6f;

	imageStore (output_image, ivec2 (pos_x, pos_y), color);
}
