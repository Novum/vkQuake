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

layout (location = 0) in vec3 in_position;
layout (location = 1) in vec2 in_texcoord;
layout (location = 2) in vec4 in_color;
layout (location = 3) in vec4 in_texture_region;

layout (location = 0) out vec4 out_texcoord;
layout (location = 1) out vec4 out_color;
layout (location = 2) out float out_fog_frag_coord;
layout (location = 3) flat out vec4 out_texture_region;

out gl_PerVertex
{
	vec4 gl_Position;
};

void main ()
{
	gl_Position = push_constants.mvp * vec4 (in_position, 1.0);
	out_texcoord = vec4 (in_texcoord, 1.0 / (in_texture_region.zw - in_texture_region.xy));
	out_color = in_color;
	out_fog_frag_coord = gl_Position.w;
	out_texture_region = in_texture_region;
}
