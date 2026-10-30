#version 460
#extension GL_GOOGLE_include_directive : enable
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (push_constant) uniform PushConsts
{
	mat4  mvp;
	vec3  fog_color;
	float fog_density;
#ifdef RGSS
	layout (offset = 112) vec2 rgss_scale;
#endif
}
push_constants;

#include "rgss.inc"

layout (location = 0) in vec3 in_position;
layout (location = 1) in vec2 in_texcoord;
layout (location = 2) in vec4 in_color;

layout (location = 0) out vec4 out_texcoord;
layout (location = 1) out vec4 out_color;
layout (location = 2) out float out_fog_frag_coord;

out gl_PerVertex
{
	vec4 gl_Position;
};

void main ()
{
	gl_Position = RGSS_POSITION (push_constants.mvp * vec4 (in_position, 1.0f));
	out_texcoord = vec4 (in_texcoord.xy, 0.0f, 0.0f);
	out_color = in_color;
	out_fog_frag_coord = gl_Position.w;
}
