#version 460

layout (set = 0, binding = 0) uniform sampler2D scene_color;
layout (push_constant) uniform PushConstants
{
	vec2 output_size_rcp;
	uint upscale_filter;
};
layout (location = 0) out vec4 out_color;

vec3 SampleClassic (vec2 source_size)
{
	const vec2 footprint = source_size * output_size_rcp;
	const vec2 source_min = (gl_FragCoord.xy - 0.5) * footprint;
	const vec2 first_pixel = floor (source_min);

	// Upscaling makes the output footprint at most one source pixel wide per axis.
	// Only the part crossing the next source-pixel boundary contributes its color.
	const vec2 next_weight = clamp ((source_min - first_pixel + footprint - 1.0) / footprint, 0.0, 1.0);

	// Bilinear filtering at these adjusted coordinates applies the four rectangle
	// overlap weights in one sample. Integer scales reproduce solid source pixels.
	const vec2 uv = (first_pixel + 0.5 + next_weight) / source_size;
	return textureLod (scene_color, uv, 0.0).rgb;
}

// Catmull-Rom reconstruction. The two positive middle weights can share a
// bilinear lookup on each axis, reducing the 4x4 kernel to nine samples.
vec3 SampleBicubic (vec2 uv, vec2 source_size)
{
	const vec2 position = uv * source_size - 0.5;
	const vec2 base = floor (position);
	const vec2 f = position - base;
	const vec2 f2 = f * f;
	const vec2 f3 = f2 * f;
	const vec2 w0 = -0.5 * f + f2 - 0.5 * f3;
	const vec2 w1 = 1.0 - 2.5 * f2 + 1.5 * f3;
	const vec2 w2 = 0.5 * f + 2.0 * f2 - 1.5 * f3;
	const vec2 w3 = -0.5 * f2 + 0.5 * f3;
	const vec2 w12 = w1 + w2;
	const vec2 middle = base + 0.5 + w2 / w12;
	const vec3 x = vec3 (base.x - 0.5, middle.x, base.x + 2.5) / source_size.x;
	const vec3 y = vec3 (base.y - 0.5, middle.y, base.y + 2.5) / source_size.y;
	const vec3 wx = vec3 (w0.x, w12.x, w3.x);
	const vec3 wy = vec3 (w0.y, w12.y, w3.y);

	vec3 color = vec3 (0.0);
	for (int j = 0; j < 3; ++j)
		for (int i = 0; i < 3; ++i)
			color += textureLod (scene_color, vec2 (x[i], y[j]), 0.0).rgb * wx[i] * wy[j];
	return color;
}

void main ()
{
	const vec2 source_size = vec2 (textureSize (scene_color, 0));
	if (upscale_filter == 0)
		out_color = vec4 (SampleClassic (source_size), 1.0);
	else
		out_color = vec4 (SampleBicubic (gl_FragCoord.xy * output_size_rcp, source_size), 1.0);
}
