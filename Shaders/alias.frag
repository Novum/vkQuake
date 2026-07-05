#version 460
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable
#extension GL_GOOGLE_include_directive : enable

// Compiled in multiple variants: WBOIT writes weighted blended OIT accumulation,
// MBOIT writes power moments, MBOIT + MBOIT_COMPOSITE reconstructs transmittance
// from them and MSAA reads multisampled moment buffers

layout (push_constant) uniform PushConsts
{
	mat4  mvp;
	vec3  fog_color;
	float fog_density;
}
push_constants;

layout (set = 0, binding = 0) uniform sampler2D diffuse_tex;
layout (set = 1, binding = 0) uniform sampler2D fullbright_tex;

layout (set = 2, binding = 0) uniform UBO
{
	mat4  model_matrix;
	vec3  shade_vector;
	float blend_factor;
	vec3  light_color;
	float entalpha;
	uint  flags;
}
ubo;

layout (location = 0) in vec2 in_texcoord;
layout (location = 1) in vec4 in_color;
layout (location = 2) in float in_fog_frag_coord;

#ifndef ALIAS_ALPHA_TEST
#define ALIAS_ALPHA_TEST 0
#endif
#include "alias_common.inc"

#if MBOIT
#ifndef MBOIT_INPUT_SET
#define MBOIT_INPUT_SET 3
#endif
#include "mboit.inc"
#elif WBOIT
#include "wboit.inc"
#else
layout (location = 0) out vec4 out_frag_color;
#endif

void main ()
{
#if MBOIT
	MBOITWrite (AliasFragmentColor ());
#elif WBOIT
	WBOITWrite (AliasFragmentColor ());
#else
	out_frag_color = AliasFragmentColor ();
#endif
}
