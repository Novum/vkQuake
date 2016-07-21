#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout(set = 0, binding = 0) uniform sampler diffuse_sampler;
layout(set = 1, binding = 0) uniform texture2D solid_tex;
layout(set = 2, binding = 0) uniform texture2D alpha_tex;

layout (location = 0) in vec4 in_texcoord1;
layout (location = 1) in vec4 in_texcoord2;
layout (location = 2) in vec4 in_color;

layout (location = 0) out vec4 out_frag_color;

void main() 
{
	vec4 solid_layer = texture(sampler2D(solid_tex, diffuse_sampler), in_texcoord1.xy);
	vec4 alpha_layer = texture(sampler2D(alpha_tex, diffuse_sampler), in_texcoord2.xy);

	out_frag_color = vec4((solid_layer.rgb * (1.0f - alpha_layer.a) + alpha_layer.rgb * alpha_layer.a), in_color.a);
}
