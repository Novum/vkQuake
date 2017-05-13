#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable
#extension GL_GOOGLE_include_directive : enable

layout(push_constant) uniform PushConsts {
	mat4 view_projection_matrix;
	vec3 fog_color;
	float fog_density;
} push_constants;

layout (set = 2, binding = 0) uniform UBO
{
	mat4 model_matrix;
	vec3 shade_vector;
	float blend_factor;
	vec3 light_color;
	bool use_fullbright;
	float entalpha;
} ubo;

layout (location = 0) in vec2 in_texcoord;
layout (location = 1) in vec4 in_pose1_position;
layout (location = 2) in vec3 in_pose1_normal;
layout (location = 3) in vec4 in_pose2_position;
layout (location = 4) in vec3 in_pose2_normal;

layout (location = 0) out vec2 out_texcoord;
layout (location = 1) out vec4 out_color;
layout (location = 2) out float out_fog_frag_coord;

out gl_PerVertex {
	vec4 gl_Position;
};

float r_avertexnormal_dot(vec3 vertexnormal) // from MH 
{
	float dot = dot(vertexnormal, ubo.shade_vector);
	// wtf - this reproduces anorm_dots within as reasonable a degree of tolerance as the >= 0 case
	if (dot < 0.0)
		return 1.0 + dot * (13.0 / 44.0);
	else
		return 1.0 + dot;
}

void main()
{
	out_texcoord = in_texcoord;

	vec4 lerped_position = mix(vec4(in_pose1_position.xyz, 1.0f), vec4(in_pose2_position.xyz, 1.0f), ubo.blend_factor);
	vec4 model_space_position = ubo.model_matrix * lerped_position;
	gl_Position = push_constants.view_projection_matrix * model_space_position;

	float dot1 = r_avertexnormal_dot(in_pose1_normal);
	float dot2 = r_avertexnormal_dot(in_pose2_normal);
	out_color = vec4(ubo.light_color * mix(dot1, dot2, ubo.blend_factor), 1.0);

	out_fog_frag_coord = gl_Position.w;
}