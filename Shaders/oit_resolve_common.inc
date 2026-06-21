vec4 WBOIT_ResolveColor (vec4 accumulation, float revealage)
{
	if (isinf (max (max (abs (accumulation.r), abs (accumulation.g)), abs (accumulation.b))))
		accumulation.rgb = vec3 (accumulation.a);
	vec3 average_color = accumulation.rgb / max (accumulation.a, 1e-5f);
	return vec4 (clamp (average_color, 0.0f, 1.0f), 1.0f - revealage);
}

vec4 MBOT_ResolveColor (vec4 accumulation, vec2 moments)
{
	if (accumulation.a < 1e-6f)
		return vec4 (0.0f, 0.0f, 0.0f, 0.0f);

	if (isinf (max (max (abs (accumulation.r), abs (accumulation.g)), abs (accumulation.b))))
		accumulation.rgb = vec3 (accumulation.a);

	float m1 = moments.r;
	float m2 = moments.g;

	float mean_z = m1 / accumulation.a;
	float variance = m2 / accumulation.a - mean_z * mean_z;
	float s2 = max (variance, 1e-10f);

	float a = s2 / (s2 + mean_z * mean_z + 1e-10f);
	float w0 = a * a;
	float w1 = (1.0f - a);
	float moment_transmittance = w0 * exp (-2.0f * m1) + w1 * exp (-m1 * mean_z / s2);

	// WBOIT-style revealage as baseline
	float wboit_revealage = exp (-accumulation.a * 10.0f);

	// Blend: variance-aware when moments disagree (depth discontinuities),
	// baseline otherwise — preserves murky water appearance
	float transmittance = mix (wboit_revealage, moment_transmittance, clamp (variance * 1000.0f, 0.0f, 1.0f));
	transmittance = clamp (transmittance, 0.0f, 1.0f);

	vec3 average_color = accumulation.rgb / accumulation.a;

	return vec4 (clamp (average_color, 0.0f, 1.0f), 1.0f - transmittance);
}

vec4 Hybrid_ResolveColor (vec4 accumulation, float revealage, vec4 peel0, vec4 peel1, vec4 peel2, vec4 peel3)
{
	if (isinf (max (max (abs (accumulation.r), abs (accumulation.g)), abs (accumulation.b))))
		accumulation.rgb = vec3 (accumulation.a);

	vec3 wboit_color = accumulation.rgb / max (accumulation.a, 1e-5f);
	float wboit_alpha = 1.0f - revealage;

	// Composite peeled layers back-to-front: peel3 → peel2 → peel1 → peel0
	// WBOIT fills in wherever no peeled layer has content.
	vec3  color = wboit_color;
	float alpha = wboit_alpha;

	if (peel3.a > 1e-6f)
	{
		float omt = 1.0f - peel3.a;
		float new_alpha = alpha * omt + peel3.a;
		color = (peel3.rgb * peel3.a + color * alpha * omt) / max (new_alpha, 1e-5f);
		alpha = new_alpha;
	}

	if (peel2.a > 1e-6f)
	{
		float omt = 1.0f - peel2.a;
		float new_alpha = alpha * omt + peel2.a;
		color = (peel2.rgb * peel2.a + color * alpha * omt) / max (new_alpha, 1e-5f);
		alpha = new_alpha;
	}

	if (peel1.a > 1e-6f)
	{
		float omt = 1.0f - peel1.a;
		float new_alpha = alpha * omt + peel1.a;
		color = (peel1.rgb * peel1.a + color * alpha * omt) / max (new_alpha, 1e-5f);
		alpha = new_alpha;
	}

	if (peel0.a > 1e-6f)
	{
		float omt = 1.0f - peel0.a;
		float new_alpha = alpha * omt + peel0.a;
		color = (peel0.rgb * peel0.a + color * alpha * omt) / max (new_alpha, 1e-5f);
		alpha = new_alpha;
	}

	return vec4 (clamp (color, 0.0f, 1.0f), alpha);
}
