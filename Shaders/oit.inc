void WriteOITTransparency (inout vec4 frag_color, out vec4 oit_accum, out float oit_reveal)
{
	float z = 1.0f / max (gl_FragCoord.w, 1e-5f);
	float weight = clamp (frag_color.a * frag_color.a * 0.03f / (1e-5f + pow (z / 2e5f, 2.0f)), 1e-2f, 3e3f);
	oit_accum = vec4 (frag_color.rgb * frag_color.a * weight, frag_color.a * weight);
	oit_reveal = frag_color.a;
	frag_color = vec4 (0.0f);
}
