float Dither4x4 (vec2 position_screen)
{
	const int bayer[16] = int[16] (
		 0,  8,  2, 10,
		12,  4, 14,  6,
		 3, 11,  1,  9,
		15,  7, 13,  5
	);
	ivec2 p = ivec2 (position_screen) % 4;
	return float (bayer[p.x + p.y * 4]) / 16.0f - 0.5f;
}

void WriteMBOTTransparency (vec4 frag_color, out vec4 oit_accum, out vec2 oit_moments)
{
	frag_color = clamp (frag_color, 0.0f, 1.0f);
	float z = 1.0f / gl_FragCoord.w;
	float d = clamp (z / push_constants.farclip, 0.0f, 1.0f);
	float alpha = frag_color.a;

	// MBOT weight: improved edge/discontinuity handling over WBOIT
	float w_depth = alpha * d * d;
	float M = clamp (10.0f / max (d, 0.01f), 1.0f, 100.0f);
	float w_disc = alpha * (1.0f - alpha) * (1.0f - alpha) * M * M;
	float grad = length (vec2 (dFdx (gl_FragCoord.z), dFdy (gl_FragCoord.z)));
	float edge_scale = clamp (grad * z * 100.0f, 0.0f, 1.0f);
	float w_edge = alpha * d * d * (1.0f + edge_scale * 4.0f);

	float weight = max (max (w_depth, w_disc), w_edge);

	weight *= 1.0f + Dither4x4 (gl_FragCoord.xy) * 0.1f;

	weight = clamp (weight, 1e-2f, 1e2f);

	oit_accum = vec4 (frag_color.rgb * alpha * weight, alpha * weight);
	oit_moments = vec2 (z, z * z) * alpha * weight;
}
