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
layout (rg32f, set = 2, binding = 0) uniform image2D prepared;
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
	float view_depth = depth > 0.0 ? pc.projection.z / depth : 1e9;
	vec2  depths;
	if (pc.settings.z == 0)
		depths = vec2 (view_depth, 1e9);
	else
	{
		depths = imageLoad (prepared, output_pixel).xy;
		depths.y = view_depth;
	}
	imageStore (prepared, output_pixel, vec4 (depths, 0.0, 0.0));
}
