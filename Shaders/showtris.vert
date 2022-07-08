#version 460
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (push_constant) uniform PushConsts
{
	mat4 mvp;
}
push_constants;

layout (location = 0) in vec3 in_position;

out gl_PerVertex
{
	vec4 gl_Position;
};

void main ()
{
	gl_Position = push_constants.mvp * vec4 (in_position, 1.0f);
}
