layout (location = 0) out vec4 out_oit_accum;
layout (location = 1) out float out_oit_reveal;

void WBOITWrite (vec4 frag_color)
{
	frag_color = clamp (frag_color, 0.0f, 1.0f);
	float z = 1.0f / gl_FragCoord.w;
	float weight = clamp (frag_color.a * frag_color.a * 0.03f / (1e-5f + pow (z / 1e7f, 1.0f)), 1e-2f, 3e3f);
	out_oit_accum = vec4 (frag_color.rgb * frag_color.a * weight, frag_color.a * weight);
	out_oit_reveal = frag_color.a;
}
