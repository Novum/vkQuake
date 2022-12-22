#version 460
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

// keep in sync with glquake.h
#define LMBLOCK_WIDTH  1024
#define LMBLOCK_HEIGHT 1024

layout (push_constant) uniform PushConsts
{
	mat4  mvp;
	vec3  fog_color;
	float fog_density;
	float alpha;
}
push_constants;

layout (set = 0, binding = 0) uniform sampler2D diffuse_tex;
layout (set = 1, binding = 0) uniform sampler2D lightmap_tex;
layout (set = 2, binding = 0) uniform sampler2D fullbright_tex;

layout (location = 0) in vec4 in_texcoords;
layout (location = 1) in float in_fog_frag_coord;

layout (location = 0) out vec4 out_frag_color;

layout (constant_id = 0) const bool use_fullbright = false;
layout (constant_id = 1) const bool use_alpha_test = false;
layout (constant_id = 2) const bool use_alpha_blend = false;
layout (constant_id = 3) const bool quantize_lm = false;
layout (constant_id = 4) const bool scaled_lm = false;

void main ()
{
	vec4 diffuse = texture (diffuse_tex, in_texcoords.xy);
	if (use_alpha_test && diffuse.a < 0.666f)
		discard;

	float lm_multiplier = scaled_lm ? 8.0f : 2.0f;
	vec3  light;

	if (quantize_lm)
	{
		ivec2 lm_size = ivec2 (LMBLOCK_WIDTH, LMBLOCK_HEIGHT);
		vec2  uv_exp = (floor ((lm_size * 16) * in_texcoords.zw) + 0.5) / (lm_size * 16);
		light = texture (lightmap_tex, uv_exp).rgb * lm_multiplier;
	}
	else
		light = texture (lightmap_tex, in_texcoords.zw).rgb * lm_multiplier;

	out_frag_color.rgb = diffuse.rgb * light.rgb;

	if (use_fullbright)
	{
		vec3 fullbright = texture (fullbright_tex, in_texcoords.xy).rgb;
		out_frag_color.rgb += fullbright;
	}

	float fog = exp (-push_constants.fog_density * push_constants.fog_density * in_fog_frag_coord * in_fog_frag_coord);
	fog = clamp (fog, 0.0, 1.0);
	out_frag_color.rgb = mix (push_constants.fog_color, out_frag_color.rgb, fog);

	if (use_alpha_blend)
		out_frag_color.a = push_constants.alpha;
}
