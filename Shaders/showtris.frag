#version 460
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (location = 0) out vec4 out_frag_color;

void main ()
{
	out_frag_color = vec4 (1.0f, 1.0f, 1.0f, 0.0f);
}
