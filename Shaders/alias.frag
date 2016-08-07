#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable
#extension GL_GOOGLE_include_directive : enable

layout(push_constant) uniform PushConsts {
	mat4 mvp;
	vec3 fog_color;
	float fog_density;
} push_constants;

layout(set = 0, binding = 0) uniform sampler2D diffuse_tex;
layout(set = 1, binding = 0) uniform sampler2D fullbright_tex;

layout (set = 2, binding = 0) uniform UBO
{
	mat4 model_matrix;
	vec3 shade_vector;
	float blend_factor;
	vec3 light_color;
	bool use_fullbright;
	float entalpha;
} ubo;

layout (location = 0) in vec2 in_texcoord;
layout (location = 1) in vec4 in_color;
layout (location = 2) in float in_fog_frag_coord;

layout (location = 0) out vec4 out_frag_color;

void main()
{
	vec4 result = texture(diffuse_tex, in_texcoord.xy);
	result *= in_color;

	if (ubo.use_fullbright)
		result += texture(fullbright_tex, in_texcoord.xy);

	result.a = ubo.entalpha;

	float fog = exp(-push_constants.fog_density * push_constants.fog_density * in_fog_frag_coord * in_fog_frag_coord);
	fog = clamp(fog, 0.0, 1.0);
	result.rgb = mix(push_constants.fog_color, result.rgb, fog);

	out_frag_color = result;
}
