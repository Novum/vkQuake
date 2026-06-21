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

float OIT_Weight (float alpha, float z, vec2 position_screen)
{
	float d = clamp (z / push_constants.farclip, 0.0f, 1.0f);

	// Term 1: depth-based weight (closer fragments get higher influence)
	float w_depth = alpha * d * d;
	// Term 2: discontinuity weight (handles semi-transparent layers at depth jumps)
	// Adaptive M: closer fragments get higher discontinuity weight to better
	// resolve overlapping transparent layers near the camera
	float M = clamp (10.0f / max (d, 0.01f), 1.0f, 100.0f);
	float w_disc = alpha * (1.0f - alpha) * (1.0f - alpha) * M * M;

	// Term 3: per-pixel edge weight using screen-space derivatives of depth
	// Large derivatives indicate silhouette boundaries where fragments from different
	// surfaces meet — boosting weight here prevents background bleeding through edges
	float grad = length (vec2 (dFdx (gl_FragCoord.z), dFdy (gl_FragCoord.z)));
	float edge_scale = clamp (grad * z * 100.0f, 0.0f, 1.0f);
	float w_edge = alpha * d * d * (1.0f + edge_scale * 4.0f);

	// Term 4: thickness weight from screen-space depth gradients
	// Thick transparent volumes (smoke, fog) should dominate thin layers (glass edges)
	float thickness = abs (dFdx (z)) + abs (dFdy (z));
	float w_thick = alpha * clamp (thickness * 0.1f, 0.0f, 1.0f) * d;

	float weight = max (max (max (w_depth, w_disc), w_edge), w_thick);

	weight *= 1.0f + Dither4x4 (position_screen) * 0.1f;

	weight = clamp (weight, 1e-2f, 1e2f);

	return weight;
}

float OIT_WeightParticle (float alpha, float z, vec2 position_screen)
{
	float d = clamp (z / push_constants.farclip, 0.0f, 1.0f);
	float weight = alpha * d * d;

	weight *= 1.0f + Dither4x4 (position_screen) * 0.1f;
	weight = clamp (weight, 1e-2f, 1e2f);

	return weight;
}

void WriteOITTransparency (vec4 frag_color, out vec4 oit_accum, out float oit_reveal)
{
	frag_color = clamp (frag_color, 0.0f, 1.0f);
	float z = 1.0f / gl_FragCoord.w;

	float weight = OIT_Weight (frag_color.a, z, gl_FragCoord.xy);

	oit_accum = vec4 (frag_color.rgb * frag_color.a * weight, frag_color.a * weight);
	oit_reveal = frag_color.a;
}

void WriteOITTransparencyParticle (vec4 frag_color, out vec4 oit_accum, out float oit_reveal)
{
	frag_color = clamp (frag_color, 0.0f, 1.0f);
	float z = 1.0f / gl_FragCoord.w;

	float weight = OIT_WeightParticle (frag_color.a, z, gl_FragCoord.xy);

	oit_accum = vec4 (frag_color.rgb * frag_color.a * weight, frag_color.a * weight);
	oit_reveal = frag_color.a;
}

void WriteMBOTTransparency (vec4 frag_color, out vec4 oit_accum, out vec2 oit_moments)
{
	frag_color = clamp (frag_color, 0.0f, 1.0f);
	float z = 1.0f / gl_FragCoord.w;

	float weight = OIT_Weight (frag_color.a, z, gl_FragCoord.xy);

	oit_accum = vec4 (frag_color.rgb * frag_color.a * weight, frag_color.a * weight);
	oit_moments = vec2 (z, z * z) * frag_color.a * weight;
}

void WriteMBOTTransparencyParticle (vec4 frag_color, out vec4 oit_accum, out vec2 oit_moments)
{
	frag_color = clamp (frag_color, 0.0f, 1.0f);
	float z = 1.0f / gl_FragCoord.w;

	float weight = OIT_WeightParticle (frag_color.a, z, gl_FragCoord.xy);

	oit_accum = vec4 (frag_color.rgb * frag_color.a * weight, frag_color.a * weight);
	oit_moments = vec2 (z, z * z) * frag_color.a * weight;
}
