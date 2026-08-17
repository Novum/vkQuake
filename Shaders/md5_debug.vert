#version 460
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

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

layout (location = 0) out vec4 out_texcoord;
layout (location = 1) out vec4 out_color;
layout (location = 2) out float out_fog_frag_coord;

out gl_PerVertex
{
	vec4 gl_Position;
};

mat4x3 MD5_LoadJointMatrix (uint joint_offset)
{
	uint base = joint_offset * 12;
	return mat4x3 (
		joint_mats[base + 0], joint_mats[base + 4], joint_mats[base + 8], joint_mats[base + 1], joint_mats[base + 5], joint_mats[base + 9],
		joint_mats[base + 2], joint_mats[base + 6], joint_mats[base + 10], joint_mats[base + 3], joint_mats[base + 7], joint_mats[base + 11]);
}

void main ()
{
	const uint joint_index = gl_VertexIndex;
	const vec4 joint_origin = vec4 (0.0, 0.0, 0.0, 1.0);
	const vec3 pos0 = (MD5_LoadJointMatrix (ubo.joints_offset0 + joint_index) * joint_origin).xyz;
	const vec3 pos1 = (MD5_LoadJointMatrix (ubo.joints_offset1 + joint_index) * joint_origin).xyz;
	const vec3 pos = mix (pos0, pos1, ubo.blend_factor);

	gl_Position = push_constants.view_projection_matrix * ubo.model_matrix * vec4 (pos, 1.0);
	out_texcoord = vec4 (0.0);
	out_color = vec4 (ubo.light_color, 1.0);
	out_fog_frag_coord = gl_Position.w;
}
