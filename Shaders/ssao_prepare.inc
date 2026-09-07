layout (push_constant) uniform constants
{
	vec4 viewport, projection, settings;
}
pc;

bool in_view (ivec2 p)
{
	return all (greaterThanEqual (vec2 (p), pc.viewport.xy)) && all (lessThan (vec2 (p), pc.viewport.xy + round (1.0 / pc.viewport.zw)));
}
layout (local_size_x = 8, local_size_y = 8) in;
#ifdef MULTISAMPLED
layout (set = 1, binding = 0) uniform sampler2DMS scene_depth;
#define DEPTH(image, p, s) texelFetch (image, p, s).r
#else
layout (set = 1, binding = 0) uniform sampler2D scene_depth;
#define DEPTH(image, p, s) texelFetch (image, p, 0).r
#endif
layout (rg16f, set = 2, binding = 0) uniform writeonly image2D prepared;
void main ()
{
	ivec2 output_pixel = ivec2 (gl_GlobalInvocationID.xy);
	if (any (greaterThanEqual (output_pixel, imageSize (prepared))))
		return;
	float depth = 0.0;
	if (in_view (output_pixel))
	{
#ifdef MULTISAMPLED
		for (int s = 0; s < int (pc.projection.w); ++s)
#else
		const int s = 0;
#endif
			depth = max (depth, DEPTH (scene_depth, output_pixel, s));
	}
	// Reserve the largest finite half value for missing depth.
	float view_depth = depth > 0.0 ? min (pc.projection.z / depth, 65472.0) : 65504.0;
	imageStore (prepared, output_pixel, vec4 (view_depth, 65504.0, 0.0, 0.0));
}
