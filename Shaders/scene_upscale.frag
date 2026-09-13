#version 460

layout (set = 0, binding = 0) uniform sampler2D scene_color;
layout (push_constant) uniform PushConstants
{
	vec2 output_size_rcp;
};
layout (location = 0) out vec4 out_color;

void main ()
{
	const vec2 source_size = vec2 (textureSize (scene_color, 0));
	const vec2 footprint = source_size * output_size_rcp;
	const vec2 source_min = (gl_FragCoord.xy - 0.5) * footprint;
	const vec2 first_pixel = floor (source_min);

	// Upscaling makes the output footprint at most one source pixel wide per axis.
	// Only the part crossing the next source-pixel boundary contributes its color.
	const vec2 next_weight = clamp ((source_min - first_pixel + footprint - 1.0) / footprint, 0.0, 1.0);

	// Bilinear filtering at these adjusted coordinates applies the four rectangle
	// overlap weights in one sample. Integer scales reproduce solid source pixels.
	const vec2 uv = (first_pixel + 0.5 + next_weight) / source_size;
	out_color = vec4 (textureLod (scene_color, uv, 0.0).rgb, 1.0);
}
