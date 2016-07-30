#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable
#extension GL_GOOGLE_include_directive : enable

layout(set = 0, binding = 0) uniform sampler diffuse_sampler;
layout(set = 1, binding = 0) uniform texture2D diffuse_tex;
layout(set = 2, binding = 0) uniform texture2D fullbright_tex;

layout (set = 3, binding = 0) uniform UBO
{
	mat4 model_matrix;
	vec3 shade_vector;
	float blend_factor;
	vec3 light_color;
	bool use_fullbright;
} ubo;

layout (location = 0) in vec2 in_texcoord;
layout (location = 1) in vec4 in_color;
layout (location = 2) in float in_fog_frag_coord;

layout (location = 0) out vec4 out_frag_color;

void main()
{
	vec4 result = texture(sampler2D(diffuse_tex, diffuse_sampler), in_texcoord.xy);
	result *= in_color;

	if (ubo.use_fullbright)
		result += texture(sampler2D(fullbright_tex, diffuse_sampler), in_texcoord.xy);

	result.a = in_color.a;
	out_frag_color = result;
}
