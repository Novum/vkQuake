layout (constant_id = 0) const bool fade_rgb = false;
layout (constant_id = 1) const bool fade_alpha = true;
layout (constant_id = 2) const uint sample_count = 1;

layout (push_constant) uniform PushConsts
{
	mat4  mvp;
	vec3  fog_color;
	float fog_density;
	float depth_scale;
	float inverse_fade_distance;
}
push_constants;

layout (set = 0, binding = 0) uniform sampler2D tex;
#ifdef MSAA
layout (input_attachment_index = 0, set = 1, binding = 0) uniform subpassInputMS scene_depth;
#else
layout (input_attachment_index = 0, set = 1, binding = 0) uniform subpassInput scene_depth;
#endif
layout (location = 0) in vec4 in_texcoord;
layout (location = 1) in vec4 in_color;
layout (location = 2) in float in_fog_frag_coord;
layout (location = 0) out vec4 out_frag_color;

void main ()
{
	vec4 color = in_color * texture (tex, in_texcoord.xy);
	// Reversed Z: use the nearest covered scene sample without a depth resolve.
#ifdef MSAA
	float depth = 0.0;
	for (int sample_index = 0; sample_index < int (sample_count); ++sample_index)
		depth = max (depth, subpassLoad (scene_depth, sample_index).r);
#else
	float depth = subpassLoad (scene_depth).r;
#endif
	// Use unbiased particle view depth; the raster depth bias is for decals.
	float fade = depth > 0.0 ? clamp ((push_constants.depth_scale / depth - in_fog_frag_coord) * push_constants.inverse_fade_distance, 0.0, 1.0) : 1.0;
	// Color-based and premultiplied blends need their RGB contribution faded too.
	if (fade_rgb)
		color.rgb *= fade;
	if (fade_alpha)
		color.a *= fade;
	out_frag_color = color;
}
