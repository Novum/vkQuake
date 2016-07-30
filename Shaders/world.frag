#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout(set = 0, binding = 0) uniform sampler2D diffuse_tex;
layout(set = 1, binding = 0) uniform sampler2D lightmap_tex;
layout(set = 2, binding = 0) uniform sampler2D fullbright_tex;

layout (location = 0) in vec4 in_texcoords;

layout (location = 0) out vec4 out_frag_color;

layout (constant_id = 0) const bool use_fullbright = false;
layout (constant_id = 1) const bool use_alpha_test = false;

void main() 
{
	vec4 diffuse = texture(diffuse_tex, in_texcoords.xy);
	vec4 light = texture(lightmap_tex, in_texcoords.zw) * 2.0f;
	out_frag_color = diffuse * light;

	if (use_fullbright)
	{
		vec4 fullbright = texture(fullbright_tex, in_texcoords.xy);
		out_frag_color += fullbright;
	}

	if (use_alpha_test && out_frag_color.a < 0.666f)
		discard;
}
