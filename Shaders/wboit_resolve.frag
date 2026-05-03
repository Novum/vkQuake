#version 460
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

#if MSAA
#define OIT_SUBPASS_INPUT subpassInputMS
#define OIT_LOAD(x)		  subpassLoad (x, gl_SampleID)
#else
#define OIT_SUBPASS_INPUT subpassInput
#define OIT_LOAD(x)		  subpassLoad (x)
#endif

layout (input_attachment_index = 0, set = 0, binding = 0) uniform OIT_SUBPASS_INPUT accum_input;
layout (input_attachment_index = 1, set = 0, binding = 1) uniform OIT_SUBPASS_INPUT reveal_input;

layout (location = 0) out vec4 out_frag_color;

void main ()
{
	vec4  accumulation = OIT_LOAD (accum_input);
	float revealage = OIT_LOAD (reveal_input).r;
	if (isinf (max (max (abs (accumulation.r), abs (accumulation.g)), abs (accumulation.b))))
		accumulation.rgb = vec3 (accumulation.a);
	vec3 average_color = accumulation.rgb / max (accumulation.a, 1e-5f);
	out_frag_color = vec4 (clamp (average_color, 0.0f, 1.0f), 1.0f - revealage);
}
