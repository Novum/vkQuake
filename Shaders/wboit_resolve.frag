#version 460
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable
#extension GL_GOOGLE_include_directive : enable

layout (input_attachment_index = 0, set = 0, binding = 0) uniform subpassInput accum_input;
layout (input_attachment_index = 1, set = 0, binding = 1) uniform subpassInput reveal_input;

layout (location = 0) out vec4 out_frag_color;

#include "oit_resolve_common.inc"

void main ()
{
	vec4  accumulation = subpassLoad (accum_input);
	float revealage = subpassLoad (reveal_input).r;
	out_frag_color = WBOIT_ResolveColor (accumulation, revealage);
}
