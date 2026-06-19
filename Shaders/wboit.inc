void WriteOITTransparency (vec4 frag_color, out vec4 oit_accum, out float oit_reveal)
{
	frag_color = clamp (frag_color, 0.0f, 1.0f);
	float z = 1.0f / gl_FragCoord.w;
	float d = clamp (z / push_constants.farclip, 0.0f, 1.0f);
	float alpha = frag_color.a;

	// Term 1: depth-based weight (closer fragments get higher influence)
	float w_depth = alpha * d * d;
	// Term 2: discontinuity weight (handles semi-transparent layers at depth jumps)
	float M = 10.0f;
	float w_disc = alpha * (1.0f - alpha) * (1.0f - alpha) * M * M;

	float weight = clamp (max (w_depth, w_disc), 1e-2f, 3e3f);

	oit_accum = vec4 (frag_color.rgb * alpha * weight, alpha * weight);
	oit_reveal = frag_color.a;
}
