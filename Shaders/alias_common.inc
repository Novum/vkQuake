#ifndef ALIAS_ALPHA_TEST
#define ALIAS_ALPHA_TEST 0
#endif

vec4 AliasFragmentColor ()
{
	vec4 result = texture (diffuse_tex, in_texcoord.xy);

#if ALIAS_ALPHA_TEST
	if (result.a < 0.666f)
		discard;
#endif

	float original_diffuse_tex_a = result.a;
	result.a = 1.0;

	result *= in_color * 2.0f;

	if ((ubo.flags & 0x1) != 0)
		result += texture (fullbright_tex, in_texcoord.xy);

	result.a = original_diffuse_tex_a * ubo.entalpha;

	float fog = exp (-push_constants.fog_density * push_constants.fog_density * in_fog_frag_coord * in_fog_frag_coord);
	fog = clamp (fog, 0.0, 1.0);
	result.rgb = mix (push_constants.fog_color, result.rgb, fog);

	return result;
}
