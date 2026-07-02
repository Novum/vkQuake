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

layout (set = 0, binding = 0) uniform sampler2D tex;

layout (location = 0) in vec4 in_texcoord;
layout (location = 1) in vec4 in_color;
layout (location = 2) in float in_fog_frag_coord;

#include "basic_common.inc"

#if MBOIT
#define MBOIT_INPUT_SET 1
#include "mboit.inc"
#elif WBOIT
#include "wboit.inc"
#else
layout (location = 0) out vec4 out_frag_color;
#endif

void main ()
{
#if MBOIT
	MBOITWrite (BasicFragmentColor ());
#elif WBOIT
	WBOITWrite (BasicFragmentColor ());
#else
	out_frag_color = BasicFragmentColor ();
#endif
}
