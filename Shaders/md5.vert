#version 460
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable
#extension GL_GOOGLE_include_directive : enable

layout (push_constant) uniform PushConsts
{
	mat4  view_projection_matrix;
	vec3  fog_color;
	float fog_density;
}
push_constants;

layout (set = 2, binding = 0) uniform UBO
{
	mat4  model_matrix;
	vec3  shade_vector;
	float blend_factor;
	vec3  light_color;
	float entalpha;
	uint  flags;
	uint  joints_offset0;
	uint  joints_offset1;
}
ubo;

layout (std430, set = 3, binding = 0) restrict readonly buffer joints_buffer
{
	float joint_mats[];
};

mat4x3 MD5_LoadJointMatrix (uint joint_offset)
{
	uint base = joint_offset * 12;
	return mat4x3 (
		joint_mats[base + 0], joint_mats[base + 4], joint_mats[base + 8],
		joint_mats[base + 1], joint_mats[base + 5], joint_mats[base + 9],
		joint_mats[base + 2], joint_mats[base + 6], joint_mats[base + 10],
		joint_mats[base + 3], joint_mats[base + 7], joint_mats[base + 11]);
}

#include "skinning.inc"

layout (location = 0) in vec3 in_position;
layout (location = 1) in vec3 in_normal;
layout (location = 2) in vec2 in_texcoord;
layout (location = 3) in vec4 in_joint_weights0;
#if defined(EIGHT_WEIGHT_SKINNING)
layout (location = 4) in vec4 in_joint_weights1;
layout (location = 5) in uvec4 in_joint_indices0;
layout (location = 6) in uvec4 in_joint_indices1;
layout (location = 7) in vec4 in_joint_position_x0;
layout (location = 8) in vec4 in_joint_position_x1;
layout (location = 9) in vec4 in_joint_position_y0;
layout (location = 10) in vec4 in_joint_position_y1;
layout (location = 11) in vec4 in_joint_position_z0;
layout (location = 12) in vec4 in_joint_position_z1;
#else
layout (location = 4) in uvec4 in_joint_indices0;
layout (location = 5) in vec4 in_joint_position_x0;
layout (location = 6) in vec4 in_joint_position_y0;
layout (location = 7) in vec4 in_joint_position_z0;
#endif

layout (location = 0) out vec2 out_texcoord;
layout (location = 1) out vec4 out_color;
layout (location = 2) out float out_fog_frag_coord;

out gl_PerVertex
{
	vec4 gl_Position;
};

float r_avertexnormal_dot (vec3 vertexnormal) // from MH
{
	const float dot = dot (vertexnormal, ubo.shade_vector);
	// wtf - this reproduces anorm_dots within as reasonable a degree of tolerance as the >= 0 case
	if (dot < 0.0)
		return 1.0 + dot * (13.0 / 44.0);
	else
		return 1.0 + dot;
}

void main ()
{
	out_texcoord = in_texcoord;

	vec3 skinned_positions[2] = {vec3 (0.0f), vec3 (0.0f)};
	vec3 skinned_normals[2] = {vec3 (0.0f), vec3 (0.0f)};
	uint joint_offsets[2] = {ubo.joints_offset0, ubo.joints_offset1};
#if defined(EIGHT_WEIGHT_SKINNING)
	vec4 joint_weights[MD5_INFLUENCE_GROUP_COUNT] = {in_joint_weights0, in_joint_weights1};
	uvec4 joint_indices[MD5_INFLUENCE_GROUP_COUNT] = {in_joint_indices0, in_joint_indices1};
	vec4 joint_position_x[MD5_INFLUENCE_GROUP_COUNT] = {in_joint_position_x0, in_joint_position_x1};
	vec4 joint_position_y[MD5_INFLUENCE_GROUP_COUNT] = {in_joint_position_y0, in_joint_position_y1};
	vec4 joint_position_z[MD5_INFLUENCE_GROUP_COUNT] = {in_joint_position_z0, in_joint_position_z1};
#else
	vec4 joint_weights[MD5_INFLUENCE_GROUP_COUNT] = {in_joint_weights0};
	uvec4 joint_indices[MD5_INFLUENCE_GROUP_COUNT] = {in_joint_indices0};
	vec4 joint_position_x[MD5_INFLUENCE_GROUP_COUNT] = {in_joint_position_x0};
	vec4 joint_position_y[MD5_INFLUENCE_GROUP_COUNT] = {in_joint_position_y0};
	vec4 joint_position_z[MD5_INFLUENCE_GROUP_COUNT] = {in_joint_position_z0};
#endif
	MD5_NormalizeJointWeights (joint_weights);
	vec4 joint_positions[MD5_INFLUENCE_COUNT];
	MD5_LoadJointPositions (joint_position_x, joint_position_y, joint_position_z, joint_weights, joint_positions);
	MD5_SkinFrames (
		joint_offsets, joint_indices, joint_weights, joint_positions, in_normal, (ubo.flags & 0x2) == 0, skinned_positions, skinned_normals);

	const vec4 lerped_position = vec4 (mix (skinned_positions[0], skinned_positions[1], ubo.blend_factor), 1.0f);
	const vec4 model_space_position = ubo.model_matrix * lerped_position;
	gl_Position = push_constants.view_projection_matrix * model_space_position;

	if ((ubo.flags & 0x2) == 0)
	{
		const vec3	lerped_normal = mix (skinned_normals[0], skinned_normals[1], ubo.blend_factor);
		const float dot = r_avertexnormal_dot (normalize (lerped_normal));
		out_color = vec4 (ubo.light_color * dot, 1.0);
	}
	else
		out_color = vec4 (ubo.light_color, 1.0f);

	out_fog_frag_coord = gl_Position.w;
}
