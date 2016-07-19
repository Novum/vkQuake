#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout(push_constant) uniform PushConsts {
	mat4 mvp;
} push_constants;

layout (set = 0, binding = 0) uniform UBO
{
	mat4 model_view;
	vec3 shade_vector;
	float blend_factor;
	vec4 light_color;
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

	vec4 lerped_vert = mix(in_pose1_position, in_pose2_position, ubo.blend_factor);
	gl_Position = push_constants.mvp * lerped_vert;

	float dot1 = r_avertexnormal_dot(in_pose1_normal);
	float dot2 = r_avertexnormal_dot(in_pose2_normal);
	out_color = ubo.light_color * vec4(vec3(mix(dot1, dot2, ubo.blend_factor)), 1.0);

	// fog
	vec3 ecPosition = vec3(ubo.model_view * lerped_vert);
	out_fog_frag_coord = abs(ecPosition.z);
}