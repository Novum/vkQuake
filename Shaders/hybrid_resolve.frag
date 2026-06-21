#version 460
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (input_attachment_index = 0, set = 0, binding = 0) uniform subpassInput accum_input;
layout (input_attachment_index = 1, set = 0, binding = 1) uniform subpassInput reveal_input;
layout (set = 0, binding = 2) uniform sampler2D peel0_tex;
layout (set = 0, binding = 3) uniform sampler2D peel1_tex;
layout (set = 0, binding = 4) uniform sampler2D peel2_tex;
layout (set = 0, binding = 5) uniform sampler2D peel3_tex;

layout (location = 0) out vec4 out_frag_color;

void main ()
{
	vec4  accumulation = subpassLoad (accum_input);
	float revealage = subpassLoad (reveal_input).r;
	ivec2 coord = ivec2 (gl_FragCoord.xy);
	vec4  peel0 = texelFetch (peel0_tex, coord, 0);
	vec4  peel1 = texelFetch (peel1_tex, coord, 0);
	vec4  peel2 = texelFetch (peel2_tex, coord, 0);
	vec4  peel3 = texelFetch (peel3_tex, coord, 0);

	if (isinf (max (max (abs (accumulation.r), abs (accumulation.g)), abs (accumulation.b))))
		accumulation.rgb = vec3 (accumulation.a);

	vec3 wboit_color = accumulation.rgb / max (accumulation.a, 1e-5f);
	float wboit_alpha = 1.0f - revealage;

	// Composite peeled layers back-to-front: peel3 → peel2 → peel1 → peel0
	// WBOIT fills in wherever no peeled layer has content.
	vec3  color = wboit_color;
	float alpha = wboit_alpha;

	if (peel3.a > 1e-6f)
	{
		float omt = 1.0f - peel3.a;
		float new_alpha = alpha * omt + peel3.a;
		color = (peel3.rgb * peel3.a + color * alpha * omt) / max (new_alpha, 1e-5f);
		alpha = new_alpha;
	}

	if (peel2.a > 1e-6f)
	{
		float omt = 1.0f - peel2.a;
		float new_alpha = alpha * omt + peel2.a;
		color = (peel2.rgb * peel2.a + color * alpha * omt) / max (new_alpha, 1e-5f);
		alpha = new_alpha;
	}

	if (peel1.a > 1e-6f)
	{
		float omt = 1.0f - peel1.a;
		float new_alpha = alpha * omt + peel1.a;
		color = (peel1.rgb * peel1.a + color * alpha * omt) / max (new_alpha, 1e-5f);
		alpha = new_alpha;
	}

	if (peel0.a > 1e-6f)
	{
		float omt = 1.0f - peel0.a;
		float new_alpha = alpha * omt + peel0.a;
		color = (peel0.rgb * peel0.a + color * alpha * omt) / max (new_alpha, 1e-5f);
		alpha = new_alpha;
	}

	out_frag_color = vec4 (clamp (color, 0.0f, 1.0f), alpha);
}
