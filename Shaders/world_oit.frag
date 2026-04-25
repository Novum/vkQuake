#version 460
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable
#extension GL_GOOGLE_include_directive : enable

// keep in sync with glquake.h
#define LMBLOCK_WIDTH  1024
#define LMBLOCK_HEIGHT 1024

layout (push_constant) uniform PushConsts
{
	mat4  mvp;
	vec3  fog_color;
	float fog_density;
	float alpha;
}
push_constants;

layout (set = 0, binding = 0) uniform sampler2D diffuse_tex;
layout (set = 1, binding = 0) uniform sampler2D lightmap_tex;
layout (set = 2, binding = 0) uniform sampler2D fullbright_tex;

layout (location = 0) in vec4 in_texcoords;
layout (location = 1) in float in_fog_frag_coord;

layout (location = 0) out vec4 out_oit_accum;
layout (location = 1) out float out_oit_reveal;

layout (constant_id = 0) const bool use_fullbright = false;
layout (constant_id = 1) const bool use_alpha_test = false;
layout (constant_id = 2) const bool use_alpha_blend = false;
layout (constant_id = 3) const bool quantize_lm = false;
layout (constant_id = 4) const bool scaled_lm = false;

#include "world_common.inc"
#include "wboit.inc"

void main ()
{
	WriteOITTransparency (WorldFragmentColor (), out_oit_accum, out_oit_reveal);
}
