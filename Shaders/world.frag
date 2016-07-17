#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout(set = 0, binding = 0) uniform sampler diffuse_sampler;
layout(set = 0, binding = 1) uniform sampler lightmap_sampler;
layout(set = 1, binding = 0) uniform texture2D diffuse_tex;
layout(set = 2, binding = 0) uniform texture2D lightmap_tex;

layout (location = 0) in vec4 in_texcoords;

layout (location = 0) out vec4 out_frag_color;

void main() 
{
	vec4 diffuse = texture(sampler2D(diffuse_tex, diffuse_sampler), in_texcoords.xy);
	vec4 light = texture(sampler2D(lightmap_tex, lightmap_sampler), in_texcoords.zw) * 2.0f;
	out_frag_color = diffuse * light;
}
