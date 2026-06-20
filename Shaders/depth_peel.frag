#version 460
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable
#extension GL_GOOGLE_include_directive : enable

layout (push_constant) uniform PushConsts
{
	mat4  mvp;
	vec3  fog_color;
	float fog_density;
	float _pad_alpha;
	float farclip;
}
push_constants;

layout (set = 0, binding = 0) uniform sampler2D tex;
layout (set = 1, binding = 0) uniform sampler2D prev_depth_tex;

layout (constant_id = 0) const int PEEL_LAYER = 0;

layout (location = 0) in vec4 in_texcoord;
layout (location = 1) in vec4 in_color;
layout (location = 2) in float in_fog_frag_coord;

layout (location = 0) out vec4 out_frag_color;

#include "basic_common.inc"

void main ()
{
	if (PEEL_LAYER > 0)
	{
		ivec2 coord = ivec2 (gl_FragCoord.xy);
		float prev_depth = texelFetch (prev_depth_tex, coord, 0).r;
		if (gl_FragCoord.z > prev_depth)
			discard;
	}

	out_frag_color = BasicFragmentColor ();
}
