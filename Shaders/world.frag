#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout(set = 0, binding = 0) uniform sampler s;
layout(set = 1, binding = 0) uniform texture2D diffuse;
layout(set = 2, binding = 0) uniform texture2D lightmap;

layout (location = 0) in vec4 in_texcoords;

layout (location = 0) out vec4 out_frag_color;

void main() 
{
	out_frag_color = texture(sampler2D(diffuse, s), in_texcoords.xy);
}
