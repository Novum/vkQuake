layout(push_constant) uniform PushConsts {
	vec2	screen_size_rcp;
	float	aspect_ratio;
	float	time;
	uint	flags;
} push_constants;

#if defined(USE_SUBGROUP_OPS)
uint Compact1By1(uint x)
{
	x &= 0x55555555;
	if (gl_SubgroupSize > 4)
		x = (x ^ (x >>  1)) & 0x33333333;
	if (gl_SubgroupSize > 16)
		x = (x ^ (x >>  2)) & 0x0f0f0f0f;
	return x;
}

uint DecodeMorton2X(uint code)
{
	return Compact1By1(code >> 0);
}

uint DecodeMorton2Y(uint code)
{
	return Compact1By1(code >> 1);
}
#endif

#if defined(SCALING)
// Vulkan guarantees 16384 bytes of shared memory, so host doesn't need to check
shared uint group_red[16];
shared uint group_green[16];
shared uint group_blue[16];
#endif

#define groupMemoryBarrierSync() groupMemoryBarrier(); barrier();

layout (local_size_x = 8, local_size_y = 8) in;
void main() 
{
#if defined(SCALING) && defined(USE_SUBGROUP_OPS)
	// gl_SubgroupSize >= 4 otherwise the host code chooses the shared mem only shader
	// Vulkan guarantees subgroup size must be power of two and between 1 and 128
	uint subgroup_width = 0;
	uint subgroup_height = 0;
	switch(gl_SubgroupSize)
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
		case 128:
			subgroup_width = 16;
			subgroup_height = 8;
			break;
	}
	// Morton order for subgroupShuffleXor
	const uint subgroup_x = DecodeMorton2X(gl_SubgroupInvocationID);
	const uint subgroup_y = DecodeMorton2Y(gl_SubgroupInvocationID);
	const uint num_subgroups_x = (gl_WorkGroupSize.x + subgroup_width - 1) / subgroup_width;
	const uint local_x = ((gl_SubgroupID % num_subgroups_x) * subgroup_width) + subgroup_x;
	const uint local_y = ((gl_SubgroupID / num_subgroups_x) * subgroup_height) + subgroup_y;
	const uint pos_x = (gl_WorkGroupID.x * gl_WorkGroupSize.x) + local_x;
	const uint pos_y = (gl_WorkGroupID.y * gl_WorkGroupSize.y) + local_y;
#else
	const uint local_x = gl_LocalInvocationID.x;
	const uint local_y = gl_LocalInvocationID.y;
	const uint pos_x = gl_GlobalInvocationID.x;
	const uint pos_y = gl_GlobalInvocationID.y;
#endif

	vec4 color = vec4(0.0f, 0.0f, 0.0f, 0.0f);
	
	if ((push_constants.flags & 0x1) != 0)
	{
		const float cycle_x = 3.14159f * 5.0f;
		const float cycle_y = cycle_x * push_constants.aspect_ratio;
		const float amp_x = 1.0f / 300.0f;
		const float amp_y = amp_x * push_constants.aspect_ratio;
	
		const float pos_x_norm = float(pos_x) * push_constants.screen_size_rcp.x;
		const float pos_y_norm = float(pos_y) * push_constants.screen_size_rcp.y;
	
		const float tex_x = (pos_x_norm + (sin(pos_y_norm * cycle_x + push_constants.time) * amp_x)) * (1.0f - amp_x * 2.0f) + amp_x;
		const float tex_y = (pos_y_norm + (sin(pos_x_norm * cycle_y + push_constants.time) * amp_y)) * (1.0f - amp_y * 2.0f) + amp_y;
		
		color = texture(input_tex, vec2(tex_x, tex_y));
	}
	else
		color = texelFetch(input_tex, ivec2(pos_x, pos_y), 0);

#if defined(SCALING)
	// Only integer atomics are supported, so scale up to have enough precision
	uvec3 color_int = uvec3(color.rgb * 16777216.0f);
	if ((push_constants.flags & 0x2) != 0)
	{
#if defined(USE_SUBGROUP_OPS)
		if(gl_SubgroupSize >= 4)
		{
			uint red = color_int.r + subgroupShuffleXor(color_int.r, 0x01);
			red += subgroupShuffleXor(red, 0x02);
			uint green = color_int.g + subgroupShuffleXor(color_int.g, 0x01);
			green += subgroupShuffleXor(green, 0x02);
			uint blue = color_int.b + subgroupShuffleXor(color_int.b, 0x01);
			blue += subgroupShuffleXor(blue, 0x02);
			color_int.r = red / 4;
			color_int.g = green / 4;
			color_int.b = blue / 4;
		} else
		{
#endif
			const uint local_idx = local_x + (local_y * gl_WorkGroupSize.x);
			if (local_idx < 16)
			{
				group_red[local_idx] = 0;
				group_green[local_idx] = 0;
				group_blue[local_idx] = 0;
			}
			groupMemoryBarrierSync();
			const uint shared_mem_idx = (local_x / 2) + ((local_y / 2) * (gl_WorkGroupSize.x / 2));
			atomicAdd(group_red[shared_mem_idx], color_int.r);
			atomicAdd(group_green[shared_mem_idx], color_int.g);
			atomicAdd(group_blue[shared_mem_idx], color_int.b);
			groupMemoryBarrierSync();
			color_int.r = group_red[shared_mem_idx] / 4;
			color_int.g = group_green[shared_mem_idx] / 4;
			color_int.b = group_blue[shared_mem_idx] / 4;
#if defined(USE_SUBGROUP_OPS)
		}
#endif
	}

	if ((push_constants.flags & 0x4) != 0)
	{
#if defined(USE_SUBGROUP_OPS)
		if(gl_SubgroupSize >= 16)
		{
			uint red = color_int.r + subgroupShuffleXor(color_int.r, 0x04);
			red += subgroupShuffleXor(red, 0x08);
			uint green = color_int.g + subgroupShuffleXor(color_int.g, 0x04);
			green += subgroupShuffleXor(green, 0x08);
			uint blue = color_int.b + subgroupShuffleXor(color_int.b, 0x01);
			blue += subgroupShuffleXor(blue, 0x08);
			color_int.r = red / 4;
			color_int.g = green / 4;
			color_int.b = blue / 4;
		} else
		{
			if(gl_SubgroupSize < 4)
#endif
				groupMemoryBarrierSync();
			if ((local_x < 4) && (local_y == 0))
			{
				group_red[local_x] = 0;
				group_green[local_x] = 0;
				group_blue[local_x] = 0;
			}
			groupMemoryBarrierSync();
			const uint shared_mem_idx = (local_x / 4) + ((local_y / 4) * (gl_WorkGroupSize.x / 4));
			if (((local_x & 0x1)==0) && ((local_y & 0x1)==0))
			{
				atomicAdd(group_red[shared_mem_idx], color_int.r);
				atomicAdd(group_green[shared_mem_idx], color_int.g);
				atomicAdd(group_blue[shared_mem_idx], color_int.b);
			}
			groupMemoryBarrierSync();
			color_int.r = group_red[shared_mem_idx] / 4;
			color_int.g = group_green[shared_mem_idx] / 4;
			color_int.b = group_blue[shared_mem_idx] / 4;
#if defined(USE_SUBGROUP_OPS)
		}
#endif
	}

	if ((push_constants.flags & 0x8) != 0)
	{
#if defined(USE_SUBGROUP_OPS)
		if(gl_SubgroupSize >= 64)
		{
			uint red = color_int.r + subgroupShuffleXor(color_int.r, 0x10);
			red += subgroupShuffleXor(red, 0x20);
			uint green = color_int.g + subgroupShuffleXor(color_int.g, 0x10);
			green += subgroupShuffleXor(green, 0x20);
			uint blue = color_int.b + subgroupShuffleXor(color_int.b, 0x10);
			blue += subgroupShuffleXor(blue, 0x20);
			color_int.r = red / 4;
			color_int.g = green / 4;
			color_int.b = blue / 4;
		} else
		{
			if(gl_SubgroupSize < 16)
#endif
				groupMemoryBarrierSync();
			if ((local_x == 0) && (local_y == 0))
			{
				group_red[0] = 0;
				group_green[0] = 0;
				group_blue[0] = 0;
			}
			groupMemoryBarrierSync();
			if (((local_x & 0x3)==0) && ((local_y & 0x3)==0))
			{
				atomicAdd(group_red[0], color_int.r);
				atomicAdd(group_green[0], color_int.g);
				atomicAdd(group_blue[0], color_int.b);
			}
			groupMemoryBarrierSync();
			color_int.r = group_red[0] / 4;
			color_int.g = group_green[0] / 4;
			color_int.b = group_blue[0] / 4;
#if defined(USE_SUBGROUP_OPS)
		}
#endif
	}

	// Finally convert back to float [0, 1]
	color.rgb = vec3(color_int) * (1.0f / 16777216.0f);
#endif
	
	imageStore(output_image, ivec2(pos_x, pos_y), color);
}
