#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout(push_constant) uniform PushConsts {
	layout(offset = 64) vec3 fog_color;
	layout(offset = 76) float fog_density;
	layout(offset = 80) bool use_fullbright;
} push_constants;

layout(set = 0, binding = 1) uniform sampler diffuse_sampler;
layout(set = 1, binding = 0) uniform texture2D diffuse_tex;
layout(set = 2, binding = 0) uniform texture2D fullbright_tex;

layout (location = 0) in vec2 in_texcoord;
layout (location = 1) in vec4 in_color;
layout (location = 2) in float in_fog_frag_coord;

layout (location = 0) out vec4 out_frag_color;

void main()
{
	vec4 result = texture(sampler2D(diffuse_tex, diffuse_sampler), in_texcoord.xy);
	result *= in_color;

	if (push_constants.use_fullbright)
		result += texture(sampler2D(fullbright_tex, diffuse_sampler), in_texcoord.xy);

	result = clamp(result, 0.0, 1.0);

	// apply GL_EXP2 fog (from the orange book)
	float fog = exp(-push_constants.fog_density * push_constants.fog_density * in_fog_frag_coord * in_fog_frag_coord);
	fog = clamp(fog, 0.0, 1.0);
	result = mix(vec4(push_constants.fog_color, 1.0f), result, fog);

	result.a = in_color.a;
	out_frag_color = result;
}
