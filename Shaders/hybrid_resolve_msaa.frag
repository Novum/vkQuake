#version 460
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (input_attachment_index = 0, set = 0, binding = 0) uniform subpassInputMS accum_input;
layout (input_attachment_index = 1, set = 0, binding = 1) uniform subpassInputMS reveal_input;
layout (set = 0, binding = 2) uniform sampler2D peel0_tex;
layout (set = 0, binding = 3) uniform sampler2D peel1_tex;

layout (location = 0) out vec4 out_frag_color;

void main ()
{
	vec4  accumulation = subpassLoad (accum_input, gl_SampleID);
	float revealage = subpassLoad (reveal_input, gl_SampleID).r;
	ivec2 coord = ivec2 (gl_FragCoord.xy);
	vec4  peel0 = texelFetch (peel0_tex, coord, 0);
	vec4  peel1 = texelFetch (peel1_tex, coord, 0);

	if (isinf (max (max (abs (accumulation.r), abs (accumulation.g)), abs (accumulation.b))))
		accumulation.rgb = vec3 (accumulation.a);

	vec3 wboit_color = accumulation.rgb / max (accumulation.a, 1e-5f);
	float wboit_alpha = 1.0f - revealage;

	vec3  color;
	float alpha;

	if (peel0.a > 1e-6f)
	{
		color = peel0.rgb;
		alpha = peel0.a;
	}
	else if (peel1.a > 1e-6f)
	{
		float omt1 = 1.0f - peel1.a;
		alpha = peel1.a + wboit_alpha * omt1;
		color = (peel1.rgb * peel1.a + wboit_color * wboit_alpha * omt1) / max (alpha, 1e-5f);
	}
	else
	{
		color = wboit_color;
		alpha = wboit_alpha;
	}

	out_frag_color = vec4 (clamp (color, 0.0f, 1.0f), alpha);
}
