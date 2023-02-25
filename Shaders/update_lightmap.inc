#define MAX_LIGHTSYTLES 64
#define MAX_DLIGHTS		64
#define MAXLIGHTMAPS	4

#include "globals.inc"

layout (push_constant) uniform PushConsts
{
	uint num_dlights;
	uint lightmap_width;
	uint x_workgroup_offset;
	uint y_workgroup_offset;
	bool dlights_only;
	uint num_cached_dlights;
}
push_constants;

struct bounds_t
{
	float mins_x;
	float mins_y;
	float mins_z;
	float maxs_x;
	float maxs_y;
	float maxs_z;
};

struct light_t
{
	vec3  origin;
	float radius;
	vec3  color;
	float minlight;
};

const uint	LIGHT_MASK_SIZE = (MAX_DLIGHTS + 31) / 32;
shared uint light_mask[LIGHT_MASK_SIZE];
shared uint was_lit;

layout (set = 0, binding = 0, rgba8) uniform writeonly image2D output_image;
layout (set = 0, binding = 1) uniform utexture2D surface_indices;
layout (set = 0, binding = 2) uniform texture2D lightstyles_textures[MAXLIGHTMAPS * 3 / 4];
layout (std430, set = 0, binding = 3) restrict readonly buffer surfaces_buffer
{
	surface_t surfaces[];
};
layout (std430, set = 0, binding = 4) restrict readonly buffer workgroup_bounds_buffer
{
	bounds_t workgroup_bounds[];
};
layout (std140, set = 0, binding = 5) uniform lightstyle_scales_buffer
{
	vec4 lightstyle_scales[MAX_LIGHTSYTLES / 4];
};
layout (std140, set = 0, binding = 6) uniform lights_buffer
{
	light_t lights[MAX_DLIGHTS];
	light_t cached_lights[MAX_DLIGHTS];
};
layout (std430, set = 0, binding = 7) readonly restrict buffer world_verts_buffer
{
	world_vert_t world_verts[];
};
#ifdef RAY_QUERIES
layout (set = 0, binding = 8) uniform accelerationStructureEXT tlas;
#endif

layout (constant_id = 0) const bool scaled_lm = false;

bool TestSphereAABB (light_t light, vec3 mins, vec3 maxs)
{
	float sq_dist = 0.0f;
	for (int i = 0; i < 3; ++i)
	{
		// For each axis count any excess distance outside box extents
		float v = light.origin[i];
		if (v < mins[i])
			sq_dist += (mins[i] - v) * (mins[i] - v);
		if (v > maxs[i])
			sq_dist += (v - maxs[i]) * (v - maxs[i]);
	}
	return sq_dist <= light.radius * light.radius;
}

#ifdef RAY_QUERIES
bool IsOccluded (vec3 origin, vec3 direction, float dist)
{
	rayQueryEXT ray_query;
	rayQueryInitializeEXT (
		ray_query, tlas, gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsOpaqueEXT | gl_RayFlagsSkipClosestHitShaderEXT, 0xFF, origin, 0.01f, direction, dist);
	rayQueryProceedEXT (ray_query);
	return rayQueryGetIntersectionTypeEXT (ray_query, true) == gl_RayQueryCommittedIntersectionTriangleEXT;
}
#endif

vec3 CalculateWorldPos (const surface_t surf, const float s, const float t, inout vec3 tangent, inout vec3 bitangent)
{
	const vec3 normal = vec3 (surf.normal_x, surf.normal_y, surf.normal_z);
	const vec3 pos_edge1 = (abs (normal.x) > abs (normal.z)) ? vec3 (-normal.y, normal.x, 0.0f) : vec3 (0.0f, -normal.z, normal.y);
	const vec3 pos_edge2 = cross (normal, pos_edge1);

	const vec2 st_edge1 = vec2 (dot (pos_edge1, surf.vecs[0].xyz), dot (pos_edge1, surf.vecs[1].xyz));
	const vec2 st_edge2 = vec2 (dot (pos_edge2, surf.vecs[0].xyz), dot (pos_edge2, surf.vecs[1].xyz));

	const float scale = 16.0f / ((st_edge1.x * st_edge2.y) - (st_edge2.x * st_edge1.y));
	tangent = scale * vec3 (
						  (st_edge2.y * pos_edge1.x) + (-st_edge1.y * pos_edge2.x), (st_edge2.y * pos_edge1.y) + (-st_edge1.y * pos_edge2.y),
						  (st_edge2.y * pos_edge1.z) + (-st_edge1.y * pos_edge2.z));
	bitangent = scale * vec3 (
							(-st_edge2.x * pos_edge1.x) + (st_edge1.x * pos_edge2.x), (-st_edge2.x * pos_edge1.y) + (st_edge1.x * pos_edge2.y),
							(-st_edge2.x * pos_edge1.z) + (st_edge1.x * pos_edge2.z));

	const world_vert_t vert = world_verts[surf.vbo_offset];
	const vec3		   vert1_pos = vec3 (vert.x, vert.y, vert.z);
	const float		   vert1_offset_s = -(dot (vert1_pos, surf.vecs[0].xyz) + surf.vecs[0].w) / 16.0f;
	const float		   vert1_offset_t = -(dot (vert1_pos, surf.vecs[1].xyz) + surf.vecs[1].w) / 16.0f;

	return vert1_pos + ((s + vert1_offset_s) * tangent) + ((t + vert1_offset_t) * bitangent);
}

bool LightIntersects (const light_t light, const uint workgroup_index)
{
	if (light.radius == 0.0f)
		return false;
	const bounds_t bounds = workgroup_bounds[workgroup_index];
	const vec3	   mins = vec3 (bounds.mins_x, bounds.mins_y, bounds.mins_z);
	const vec3	   maxs = vec3 (bounds.maxs_x, bounds.maxs_y, bounds.maxs_z);
	return TestSphereAABB (light, mins, maxs);
}

layout (local_size_x = 8, local_size_y = 8) in;
void main ()
{
	const uint workgroup_size = gl_WorkGroupSize.x * gl_WorkGroupSize.y;
	for (uint mask_index = gl_LocalInvocationIndex; mask_index < LIGHT_MASK_SIZE; mask_index += workgroup_size)
		light_mask[mask_index] = 0;

	if (gl_LocalInvocationIndex == (LIGHT_MASK_SIZE < workgroup_size ? LIGHT_MASK_SIZE : workgroup_size - 1))
		was_lit = 0;

	barrier ();

	const uint workgroup_index =
		gl_WorkGroupID.x + push_constants.x_workgroup_offset + ((push_constants.lightmap_width / 8) * (gl_WorkGroupID.y + push_constants.y_workgroup_offset));
	const uint num_cull_iterations = (MAX_DLIGHTS + workgroup_size - 1) / workgroup_size;
	for (uint iter = 0; iter < num_cull_iterations; ++iter)
	{
		const uint light_index = (iter * workgroup_size) + gl_LocalInvocationIndex;
		if (light_index < push_constants.num_dlights)
		{
			const light_t light = lights[light_index];
			if (LightIntersects (light, workgroup_index))
				atomicOr (light_mask[light_index / 32], 1u << (light_index % 32));
		}
	}

	barrier ();

	bool dlights = false;
	for (uint light_mask_index = 0; light_mask_index < LIGHT_MASK_SIZE; ++light_mask_index)
		if (light_mask[light_mask_index] != 0)
			dlights = true;

	if (push_constants.dlights_only && !dlights)
	{
		for (uint iter = 0; iter < num_cull_iterations; ++iter)
		{
			const uint light_index = (iter * workgroup_size) + gl_LocalInvocationIndex;
			if (light_index < push_constants.num_cached_dlights)
			{
				const light_t light = cached_lights[light_index];
				if (LightIntersects (light, workgroup_index))
					atomicOr (was_lit, 1);
			}
		}
		barrier ();
		if (was_lit == 0)
			return;
	}

	const ivec2 coords =
		ivec2 (gl_GlobalInvocationID.x + push_constants.x_workgroup_offset * 8, gl_GlobalInvocationID.y + push_constants.y_workgroup_offset * 8);
	const uint surface_index = texelFetch (surface_indices, coords, 0).x;

	// Early out if no surface for lightmap texel
	if (surface_index == 0xFFFFFFFF)
		return;

	const surface_t surf = surfaces[surface_index & 0x7FFFFFFF];
	const uint		packed_lighstyles = surf.packed_lightstyles;

	const uint lightstyles[4] = {
		(packed_lighstyles >> 0) & 0xFF, (packed_lighstyles >> 8) & 0xFF, (packed_lighstyles >> 16) & 0xFF, (packed_lighstyles >> 24) & 0xFF};

	vec3 light_accumulated = vec3 (0.0f, 0.0f, 0.0f);
	vec3 fourth_lightmap;
	for (int i = 0; i < MAXLIGHTMAPS; ++i)
	{
		const uint style = lightstyles[i];
		if (style == 255)
			break;
		const float scale = lightstyle_scales[style / 4][style % 4];
		if (i < 3)
		{
			const vec4 sample_tex = texelFetch (lightstyles_textures[i], coords, 0);
			light_accumulated += scale * sample_tex.xyz;
			fourth_lightmap[i] = sample_tex.w;
		}
		else
			light_accumulated += scale * fourth_lightmap;
	}

	if (dlights && surface_index < 0x80000000)
	{
		const float s = float (coords.x - (surf.packed_light_st & 0xFFFF)) - 0.5f;
		const float t = float (coords.y - (surf.packed_light_st >> 16)) - 0.5f;
		vec3		tangent = vec3 (0.0f);
		vec3		bitangent = vec3 (0.0f);
		const vec3	world_pos = CalculateWorldPos (surf, s, t, tangent, bitangent);

		for (uint light_mask_index = 0; light_mask_index < LIGHT_MASK_SIZE; ++light_mask_index)
		{
			uint mask = light_mask[light_mask_index];
			while (mask != 0)
			{
				const uint light_offset = findLSB (mask);
				mask &= ~(1u << light_offset);
				const uint	  light_index = (light_mask_index * 32) + light_offset;
				const light_t light = lights[light_index];
				const float	  dist = length (world_pos - light.origin);
				if (dist > (light.radius - light.minlight))
					continue;

				float occlusion = 1.0f;
#ifdef RAY_QUERIES
				// TODO: Quality settings
				const int NUM_SAMPLES = 2;
				for (int y = 0; y < NUM_SAMPLES; ++y)
				{
					for (int x = 0; x < NUM_SAMPLES; ++x)
					{
						const float offset_s = (x * (1.0f / NUM_SAMPLES)) + (0.5f / NUM_SAMPLES);
						const float offset_t = (y * (1.0f / NUM_SAMPLES)) + (0.5f / NUM_SAMPLES);
						const vec3	pos = world_pos + (offset_s * tangent) + (offset_t * bitangent);
						const vec3	light_vec = pos - light.origin;
						const float light_dist = length (light_vec);
						if (IsOccluded (light.origin, normalize (light_vec), light_dist - 1e-2f))
							occlusion -= 1.0f / float (NUM_SAMPLES * NUM_SAMPLES);
					}
				}
#endif

				const float brightness = (light.radius - dist) / 256.0f;
				light_accumulated.xyz += brightness * max (occlusion, 0.0f) * light.color;
			}
		}
	}

	if (scaled_lm)
		light_accumulated *= 0.25f;
	imageStore (output_image, coords, vec4 (light_accumulated, 0.0f));
}
