#version 460
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (set = 1, binding = 2) uniform sampler2D wavelet_lighting;

layout (location = 0) out vec4 out_frag_color;

void main ()
{
	out_frag_color = texelFetch (wavelet_lighting, ivec2 (gl_FragCoord.xy), 0);
}
