#define MAX_LIGHTSYTLES 64
#define MAX_DLIGHTS		64
#define MAXLIGHTMAPS	4

#include "globals.inc"

layout (push_constant) uniform PushConsts
{
	uint  num_dlights;
	uint  lightmap_width;
	uint  x_workgroup_offset;
	uint  y_workgroup_offset;
	bool  dlights_only;
	uint  num_cached_dlights;
	uint  submodel_transforms_offset;
	uint  prev_submodel_transforms_offset;
	float view_pos_x;
	float view_pos_y;
	float view_pos_z;
#ifdef RAY_QUERIES
	uint shadow_samples;
#endif
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
	uint  submodel;
};

struct light_t
{
	vec3  origin;
	float radius;
	vec3  color;
	float minlight; // < 0: rerelease dynamiclight with intensity -minlight, using the KEX falloff
	vec3  cone_dir;
	float cone_cos; // cos of the spotlight apex angle, <= -1: not a spotlight
};

const uint	LIGHT_MASK_SIZE = (MAX_DLIGHTS + 31) / 32;
shared uint light_mask[LIGHT_MASK_SIZE];
shared uint was_lit;

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
layout (std430, set = 0, binding = 8) readonly restrict buffer surface_submodels_buffer
{
	uint surface_submodels[];
};
layout (std430, set = 0, binding = 9) readonly restrict buffer submodel_transforms_buffer
{
	vec4 submodel_transforms[]; // three model to world matrix rows per submodel
};

#ifdef RAY_QUERIES
layout (set = 1, binding = 0) uniform accelerationStructureEXT tlas;
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
		ray_query, tlas, gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsOpaqueEXT | gl_RayFlagsSkipClosestHitShaderEXT, 0xFF, origin, 0.0f, direction, dist);
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

bool LightIntersects (const light_t light, const uint workgroup_index, const uint transforms_offset)
{
	if (light.radius == 0.0f)
		return false;
	const bounds_t bounds = workgroup_bounds[workgroup_index];
	// workgroups with surfaces from multiple coordinate spaces can't be culled with a single AABB
	if (bounds.submodel == 0xFFFFFFFF)
		return true;
	vec3 mins = vec3 (bounds.mins_x, bounds.mins_y, bounds.mins_z);
	vec3 maxs = vec3 (bounds.maxs_x, bounds.maxs_y, bounds.maxs_z);
	if (bounds.submodel != 0)
	{
		// model space bounds of a movable submodel, transform to world space with the entity transform
		const uint base = (transforms_offset + bounds.submodel) * 3;
		const vec4 row0 = submodel_transforms[base + 0];
		const vec4 row1 = submodel_transforms[base + 1];
		const vec4 row2 = submodel_transforms[base + 2];
		const vec4 center = vec4 ((mins + maxs) * 0.5f, 1.0f);
		const vec3 extents = (maxs - mins) * 0.5f;
		const vec3 world_center = vec3 (dot (row0, center), dot (row1, center), dot (row2, center));
		const vec3 world_extents = vec3 (dot (abs (row0.xyz), extents), dot (abs (row1.xyz), extents), dot (abs (row2.xyz), extents));
		mins = world_center - world_extents;
		maxs = world_center + world_extents;
	}
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
			if (LightIntersects (light, workgroup_index, push_constants.submodel_transforms_offset))
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
				// cached lights are tested against the previous frame transform so texels of a
				// submodel that moved away from a light still get one clearing update
				const light_t light = cached_lights[light_index];
				if (LightIntersects (light, workgroup_index, push_constants.prev_submodel_transforms_offset))
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
		vec3		world_pos = CalculateWorldPos (surf, s, t, tangent, bitangent);
		vec3		normal = vec3 (surf.normal_x, surf.normal_y, surf.normal_z);
		if (((surf.packed_tex_edgecount >> 15) & 1u) != 0u) // SURF_PLANEBACK
			normal = -normal;

		// submodel vertices are in model space, transform to world space with the current entity transform
		const uint submodel = surface_submodels[surface_index & 0x7FFFFFFF];
		if (submodel != 0)
		{
			const uint base = (push_constants.submodel_transforms_offset + submodel) * 3;
			const vec4 row0 = submodel_transforms[base + 0];
			const vec4 row1 = submodel_transforms[base + 1];
			const vec4 row2 = submodel_transforms[base + 2];
			const vec4 model_pos = vec4 (world_pos, 1.0f);
			world_pos = vec3 (dot (row0, model_pos), dot (row1, model_pos), dot (row2, model_pos));
			tangent = vec3 (dot (row0.xyz, tangent), dot (row1.xyz, tangent), dot (row2.xyz, tangent));
			bitangent = vec3 (dot (row0.xyz, bitangent), dot (row1.xyz, bitangent), dot (row2.xyz, bitangent));
			normal = normalize (vec3 (dot (row0.xyz, normal), dot (row1.xyz, normal), dot (row2.xyz, normal)));
		}

		// texels on surfaces facing away from the camera can't be rasterized this frame, write base
		// light only; the update runs before rendering, so they are re-lit with dlights on the same
		// frame the camera turns back to them
		const vec3 view_pos = vec3 (push_constants.view_pos_x, push_constants.view_pos_y, push_constants.view_pos_z);
		if (dot (view_pos - world_pos, normal) > 0.0f)
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
				if (dist > (light.radius - max (light.minlight, 0.0f)))
					continue;

				const bool kex_light = light.minlight < 0.0f;

				float cone_scale = 1.0f;
				if ((light.cone_cos > -1.0f) && (dist > 0.0f))
				{
					const float cone_dot = dot ((world_pos - light.origin) / dist, light.cone_dir);
					if (kex_light)
					{
						// linear falloff from cone axis to edge, from the KEX shader
						// (ComputeSpotLight in QuakeClusteredShading.inc, QuakeEX.kpf)
						if (cone_dot < light.cone_cos)
							continue;
						cone_scale = 1.0f - (1.0f - cone_dot) / (1.0f - light.cone_cos);
					}
					else
					{
						// soft edged spotlight falloff
						cone_scale = smoothstep (light.cone_cos, mix (light.cone_cos, 1.0f, 0.25f), cone_dot);
						if (cone_scale <= 0.0f)
							continue;
					}
				}

				float brightness;
				if (kex_light)
				{
					// KEX clustered shading (ComputeLightShading in QuakeClusteredShading.inc):
					// range-normalized linear falloff with a Lambert term. KEX adds this on
					// top of lightmap * 2 (ComputeShadedColor in QuakeUtils.inc) while our
					// lightmap gets the same 2x overbright after this shader, so divide by 2
					const float len = (light.radius - dist) / light.radius;
					const vec3	dir = (light.origin - world_pos) / max (dist, 0.001f);
					const float lambert = max (dot (normal, dir), 0.0f);
					brightness = -light.minlight * 0.5f * len * lambert;
				}
				else
					brightness = (light.radius - dist) / 256.0f;

				// below half a lightmap quantization step the contribution is invisible,
				// skip the shadow rays (in particular for texels facing away from the light)
				if ((brightness * cone_scale) < (1.0f / 512.0f))
					continue;

				float occlusion = 1.0f;
#ifdef RAY_QUERIES
				// R2 sequence constants (based on plastic constant φ₂)
				const float R2_ALPHA1 = 0.7548776662466927;
				const float R2_ALPHA2 = 0.5698402909980532;

				// Per-texel offset breaks coherence between adjacent texels
				const vec2 texel_offset = fract (vec2 (coords) * vec2 (R2_ALPHA1, R2_ALPHA2));

				for (uint i = 0; i < push_constants.shadow_samples; ++i)
				{
					const vec2	sample_offset = fract (texel_offset + float (i) * vec2 (R2_ALPHA1, R2_ALPHA2));
					const vec3	pos = world_pos + (sample_offset.x * tangent) + (sample_offset.y * bitangent);
					const vec3	light_vec = pos - light.origin;
					const float light_dist = length (light_vec);
					if (IsOccluded (light.origin, normalize (light_vec), light_dist - 1e-2f))
						occlusion -= 1.0f / float (push_constants.shadow_samples);
				}
#endif
				light_accumulated.xyz += brightness * cone_scale * max (occlusion, 0.0f) * light.color;
			}
		}
	}

	if (scaled_lm)
		light_accumulated *= 0.25f;
	imageStore (output_image, coords, vec4 (light_accumulated, 0.0f));
}
