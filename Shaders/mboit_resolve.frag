#version 460
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

#if MSAA
#define MBOIT_SUBPASS_INPUT subpassInputMS
#define MBOIT_LOAD(x)		subpassLoad (x, gl_SampleID)
#else
#define MBOIT_SUBPASS_INPUT subpassInput
#define MBOIT_LOAD(x)		subpassLoad (x)
#endif

layout (input_attachment_index = 0, set = 0, binding = 0) uniform MBOIT_SUBPASS_INPUT mboit_b0_input;
layout (input_attachment_index = 1, set = 0, binding = 2) uniform MBOIT_SUBPASS_INPUT mboit_color_input;

layout (location = 0) out vec4 out_frag_color;

void main ()
{
	float b0 = MBOIT_LOAD (mboit_b0_input).r;
	vec4 color = MBOIT_LOAD (mboit_color_input);
	float alpha = 1.0f - exp (-max (b0, 0.0f));
	// normalizing by the accumulated alpha-weighted transmittance instead of the
	// total opacity compensates for errors in the reconstructed per-fragment transmittance
	vec3 average_color = color.rgb / max (color.a, 1e-5f);
	out_frag_color = vec4 (clamp (average_color, 0.0f, 1.0f), clamp (alpha, 0.0f, 1.0f));
}
