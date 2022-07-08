#version 460
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (push_constant) uniform PushConsts
{
	mat4  mvp;
	vec3  fog_color;
	float fog_density;
}
push_constants;

layout (set = 0, binding = 0) uniform sampler2D tex;

layout (location = 0) in vec4 in_texcoord;
layout (location = 1) in vec4 in_color;
layout (location = 2) in float in_fog_frag_coord;

layout (location = 0) out vec4 out_frag_color;

void main ()
{
	out_frag_color = in_color * texture (tex, in_texcoord.xy);
	if (push_constants.fog_density > 0.0f)
		out_frag_color.rgb = (out_frag_color.rgb * (1.0f - push_constants.fog_density)) + (push_constants.fog_color * push_constants.fog_density);
}
