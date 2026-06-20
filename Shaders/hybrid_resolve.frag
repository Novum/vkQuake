#version 460
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (input_attachment_index = 0, set = 0, binding = 0) uniform subpassInput accum_input;
layout (input_attachment_index = 1, set = 0, binding = 1) uniform subpassInput reveal_input;
layout (set = 0, binding = 2) uniform sampler2D peel0_tex;
layout (set = 0, binding = 3) uniform sampler2D peel1_tex;

layout (location = 0) out vec4 out_frag_color;

void main ()
{
	vec4  accumulation = subpassLoad (accum_input);
	float revealage = subpassLoad (reveal_input).r;
	ivec2 coord = ivec2 (gl_FragCoord.xy);
	vec4  peel0 = texelFetch (peel0_tex, coord, 0);
	vec4  peel1 = texelFetch (peel1_tex, coord, 0);

	if (isinf (max (max (abs (accumulation.r), abs (accumulation.g)), abs (accumulation.b))))
		accumulation.rgb = vec3 (accumulation.a);

	vec3 wboit_color = accumulation.rgb / max (accumulation.a, 1e-5f);
	float wboit_alpha = 1.0f - revealage;

	vec3  color = wboit_color;
	float alpha = wboit_alpha;

	float omt1 = 1.0f - peel1.a;
	float new_alpha = alpha * omt1 + peel1.a;
	vec3  new_color = (peel1.rgb * peel1.a + color * alpha * omt1) / max (new_alpha, 1e-5f);
	alpha = new_alpha;
	color = new_color;

	float omt0 = 1.0f - peel0.a;
	new_alpha = alpha * omt0 + peel0.a;
	new_color = (peel0.rgb * peel0.a + color * alpha * omt0) / max (new_alpha, 1e-5f);
	alpha = new_alpha;
	color = new_color;

	out_frag_color = vec4 (clamp (color, 0.0f, 1.0f), alpha);
}
