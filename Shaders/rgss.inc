// Updated between complete scene renders, with a transfer -> vertex barrier.
// Geometry and UI command buffers can therefore be replayed without advancing
// animation, particles, or any other simulation state.
#ifdef RGSS
layout (set = 5, binding = 0) uniform RGSSJitter
{
	vec2 offset;
}
rgss;
#define RGSS_POSITION(position) ((position) + vec4 (rgss.offset * push_constants.rgss_scale * (position).w, 0.0, 0.0))
#else
#define RGSS_POSITION(position) (position)
#endif
