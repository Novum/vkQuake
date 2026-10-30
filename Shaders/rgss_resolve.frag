#version 460

layout (set = 0, binding = 0) uniform sampler2DArray frames;
layout (push_constant) uniform PushConsts
{
	float gamma;
	float contrast;
	uint  samples;
}
push_constants;
layout (location = 0) out vec4 out_frag_color;

void main ()
{
	vec3 color = vec3 (0.0);
	for (uint sample_index = 0; sample_index < push_constants.samples; ++sample_index)
		color += texelFetch (frames, ivec3 (ivec2 (gl_FragCoord.xy), int (sample_index)), 0).rgb;
	color /= float (push_constants.samples);
	// As with a display gamma ramp, gamma is applied after combining the images.
	out_frag_color = vec4 (pow (color * push_constants.contrast, vec3 (push_constants.gamma)), 1.0);
}
