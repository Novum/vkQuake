#version 460
#extension GL_GOOGLE_include_directive : enable
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (push_constant) uniform PushConsts
{
	mat4 mvp;
#ifdef RGSS
	layout (offset = 112) vec2 rgss_scale;
#endif
}
push_constants;

#include "rgss.inc"

layout (location = 0) in vec3 in_position;

out gl_PerVertex
{
	vec4 gl_Position;
};

void main ()
{
	gl_Position = RGSS_POSITION (push_constants.mvp * vec4 (in_position, 1.0f));
}
