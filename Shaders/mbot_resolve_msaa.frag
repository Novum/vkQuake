#version 460
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (input_attachment_index = 0, set = 0, binding = 0) uniform subpassInputMS accum_input;
layout (input_attachment_index = 1, set = 0, binding = 1) uniform subpassInputMS moments_input;

layout (location = 0) out vec4 out_frag_color;

void main ()
{
	vec4  accumulation = subpassLoad (accum_input, gl_SampleID);

	if (accumulation.a < 1e-6f)
	{
		out_frag_color = vec4 (0.0f, 0.0f, 0.0f, 0.0f);
		return;
	}

	vec4  moments = subpassLoad (moments_input, gl_SampleID);

	if (isinf (max (max (abs (accumulation.r), abs (accumulation.g)), abs (accumulation.b))))
		accumulation.rgb = vec3 (accumulation.a);

	float m1 = moments.r;
	float m2 = moments.g;

	float mean_z = m1 / accumulation.a;
	float variance = m2 / accumulation.a - mean_z * mean_z;
	float s2 = max (variance, 1e-10f);

	float a = s2 / (s2 + mean_z * mean_z + 1e-10f);
	float w0 = a * a;
	float w1 = (1.0f - a);
	float transmittance = w0 * exp (-2.0f * m1) + w1 * exp (-m1 * mean_z / s2);
	transmittance = clamp (transmittance, 0.0f, 1.0f);

	vec3 average_color = accumulation.rgb / accumulation.a;

	out_frag_color = vec4 (clamp (average_color, 0.0f, 1.0f), 1.0f - transmittance);
}
