#version 460
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable
#extension GL_GOOGLE_include_directive : enable

layout (input_attachment_index = 0, set = 0, binding = 0) uniform subpassInputMS accum_input;
layout (input_attachment_index = 1, set = 0, binding = 1) uniform subpassInputMS reveal_input;
layout (set = 0, binding = 2) uniform sampler2D peel0_tex;
layout (set = 0, binding = 3) uniform sampler2D peel1_tex;
layout (set = 0, binding = 4) uniform sampler2D peel2_tex;
layout (set = 0, binding = 5) uniform sampler2D peel3_tex;

layout (location = 0) out vec4 out_frag_color;

#include "oit_resolve_common.inc"

void main ()
{
	vec4  accumulation = subpassLoad (accum_input, gl_SampleID);
	float revealage = subpassLoad (reveal_input, gl_SampleID).r;
	ivec2 coord = ivec2 (gl_FragCoord.xy);
	vec4  peel0 = texelFetch (peel0_tex, coord, 0);
	vec4  peel1 = texelFetch (peel1_tex, coord, 0);
	vec4  peel2 = texelFetch (peel2_tex, coord, 0);
	vec4  peel3 = texelFetch (peel3_tex, coord, 0);
	out_frag_color = Hybrid_ResolveColor (accumulation, revealage, peel0, peel1, peel2, peel3);
}
