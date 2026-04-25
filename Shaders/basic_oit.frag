#version 460
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable
#extension GL_GOOGLE_include_directive : enable

layout (push_constant) uniform PushConsts
{
	mat4  mvp;
	vec3  fog_color;
	float fog_density;
}
push_constants;

layout (set = 0, binding = 0) uniform sampler2D tex;

layout (location = 0) in vec4 in_texcoord;
layout (location = 1) in vec4 in_color;
layout (location = 2) in float in_fog_frag_coord;

layout (location = 0) out vec4 out_oit_accum;
layout (location = 1) out float out_oit_reveal;

#include "basic_common.inc"
#include "wboit.inc"

void main ()
{
	WriteOITTransparency (BasicFragmentColor (), out_oit_accum, out_oit_reveal);
}
