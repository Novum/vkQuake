void WriteOITTransparency (vec4 frag_color, out vec4 oit_accum, out float oit_reveal)
{
	frag_color = clamp (frag_color, 0.0f, 1.0f);
	float z = 1.0f / gl_FragCoord.w;
	float weight = clamp (frag_color.a * frag_color.a * 0.03f / (1e-5f + pow (z / 1e7f, 1.0f)), 1e-2f, 3e3f);
	oit_accum = vec4 (frag_color.rgb * frag_color.a * weight, frag_color.a * weight);
	oit_reveal = frag_color.a;
}
