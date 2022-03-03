#version 460
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout(push_constant) uniform PushConsts {
	mat4 mvp;
	vec3 fog_color;
	float fog_density;
} push_constants;

layout(set = 0, binding = 0) uniform sampler2D solid_tex;
layout(set = 1, binding = 0) uniform sampler2D alpha_tex;

layout (location = 0) in vec4 in_texcoord1;
layout (location = 1) in vec4 in_texcoord2;
layout (location = 2) in vec4 in_color;

layout (location = 0) out vec4 out_frag_color;

void main() 
{
	vec4 solid_layer = texture(solid_tex, in_texcoord1.xy);
	vec4 alpha_layer = texture(alpha_tex, in_texcoord2.xy);

	out_frag_color = vec4((solid_layer.rgb * (1.0f - alpha_layer.a) + alpha_layer.rgb * alpha_layer.a), in_color.a);

	if (push_constants.fog_density > 0.0f)
		out_frag_color.rgb = (out_frag_color.rgb * (1.0f - push_constants.fog_density)) + (push_constants.fog_color * push_constants.fog_density);
}
