#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout(set = 0, binding = 0) uniform sampler2D diffuse_tex;
layout(set = 1, binding = 0) uniform sampler2D lightmap_tex;

layout (location = 0) in vec4 in_texcoords;

layout (location = 0) out vec4 out_frag_color;

void main() 
{
	vec4 diffuse = texture(diffuse_tex, in_texcoords.xy);
	vec4 light = texture(lightmap_tex, in_texcoords.zw) * 2.0f;
	out_frag_color = diffuse * light;
}
