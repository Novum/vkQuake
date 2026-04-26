#version 460
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable
#extension GL_GOOGLE_include_directive : enable

#define WAVELET_SET 1
#include "wavelet.inc"

layout (location = 0) out vec4 out_frag_color;

void main ()
{
	out_frag_color = vec4 (WaveletTotalTransmittance (), 1.0f);
}
