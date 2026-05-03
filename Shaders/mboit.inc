// Compiled in three variants per shader family:
// (default)                     moment pass, writes b0 and the power moments
// MBOIT_COMPOSITE               composite pass, reconstructs transmittance from the moments
// MBOIT_COMPOSITE + MSAA  composite pass reading multisampled moment buffers
#if MBOIT_COMPOSITE
#if MSAA
// sample 0 approximation: moments only differ between samples at the edges of
// transparent geometry, and the resolve pass still composites per sample
#define MBOIT_SUBPASS_INPUT subpassInputMS
#define MBOIT_LOAD(x)		subpassLoad (x, 0)
#else
#define MBOIT_SUBPASS_INPUT subpassInput
#define MBOIT_LOAD(x)		subpassLoad (x)
#endif
layout (input_attachment_index = 0, set = MBOIT_INPUT_SET, binding = 0) uniform MBOIT_SUBPASS_INPUT mboit_b0_input;
layout (input_attachment_index = 1, set = MBOIT_INPUT_SET, binding = 1) uniform MBOIT_SUBPASS_INPUT mboit_moments0_input;
layout (location = 0) out vec4 out_color;
#else
layout (location = 0) out float out_b0;
layout (location = 1) out vec4 out_moments0;
#endif

// weight of the fragment's own absorbance at its depth; also makes explicit
// self-removal unnecessary. 0.25 is the value recommended by the paper.
#define MBOIT_OVERESTIMATION 0.25f

// log-warp view-space depth to [-1, 1]. with reversed-Z gl_FragCoord.z == znear / viewz,
// so -log2 (gl_FragCoord.z) == log2 (viewz / znear); 14 octaves cover viewz up to 16384 * znear
float MBOITDepth ()
{
	return clamp (-log2 (gl_FragCoord.z) * (1.0f / 14.0f), 0.0f, 1.0f) * 2.0f - 1.0f;
}

float MBOITAbsorbance (float alpha)
{
	return -log (max (1.0f - clamp (alpha, 0.0f, 0.999f), 1e-4f));
}

void WriteMBOITMoments (vec4 frag_color, out float out_b0, out vec4 out_moments0)
{
	frag_color = clamp (frag_color, 0.0f, 1.0f);
	float absorbance = MBOITAbsorbance (frag_color.a);
	float z = MBOITDepth ();
	float z2 = z * z;

	out_b0 = absorbance;
	out_moments0 = vec4 (z, z2, z2 * z, z2 * z2) * absorbance;
}

// Reconstructs the transmittance at the given depth from four normalized power moments
// and the given zeroth moment. Ported from the supplementary code of "Moment-Based
// Order-Independent Transparency" (Muenstermann, Krumpen, Klein, Peters, I3D 2018)
// https://momentsingraphics.de/I3D2018.html
float MBOITTransmittance4 (float b_0, vec2 b_even, vec2 b_odd, float depth, float bias, vec4 bias_vector)
{
	vec4 b = vec4 (b_odd.x, b_even.x, b_odd.y, b_even.y);
	// Bias input data to avoid artifacts
	b = mix (b, bias_vector, bias);
	vec3 z;
	z[0] = depth;

	// Compute a Cholesky factorization of the Hankel matrix B storing only non-
	// trivial entries or related products
	float L21D11 = fma (-b[0], b[1], b[2]);
	float D11 = fma (-b[0], b[0], b[1]);
	float InvD11 = 1.0f / D11;
	float L21 = L21D11 * InvD11;
	float SquaredDepthVariance = fma (-b[1], b[1], b[3]);
	float D22 = fma (-L21D11, L21, SquaredDepthVariance);

	// Obtain a scaled inverse image of bz = (1, z[0], z[0]*z[0])^T
	vec3 c = vec3 (1.0f, z[0], z[0] * z[0]);
	// Forward substitution to solve L*c1 = bz
	c[1] -= b.x;
	c[2] -= b.y + L21 * c[1];
	// Scaling to solve D*c2 = c1
	c[1] *= InvD11;
	c[2] /= D22;
	// Backward substitution to solve L^T*c3 = c2
	c[1] -= L21 * c[2];
	c[0] -= dot (c.yz, b.xy);
	// Solve the quadratic equation c[0] + c[1]*z + c[2]*z^2 to obtain solutions z[1] and z[2]
	float InvC2 = 1.0f / c[2];
	float p = c[1] * InvC2;
	float q = c[0] * InvC2;
	float D = (p * p * 0.25f) - q;
	float r = sqrt (D);
	z[1] = -p * 0.5f - r;
	z[2] = -p * 0.5f + r;
	// Compute the absorbance by summing the appropriate weights
	vec3  polynomial;
	vec3  weight_factor = vec3 (MBOIT_OVERESTIMATION, (z[1] < z[0]) ? 1.0f : 0.0f, (z[2] < z[0]) ? 1.0f : 0.0f);
	float f0 = weight_factor[0];
	float f1 = weight_factor[1];
	float f2 = weight_factor[2];
	float f01 = (f1 - f0) / (z[1] - z[0]);
	float f12 = (f2 - f1) / (z[2] - z[1]);
	float f012 = (f12 - f01) / (z[2] - z[0]);
	polynomial[0] = f012;
	polynomial[1] = polynomial[0];
	polynomial[0] = f01 - polynomial[0] * z[1];
	polynomial[2] = polynomial[1];
	polynomial[1] = polynomial[0] - polynomial[1] * z[0];
	polynomial[0] = f0 - polynomial[0] * z[0];
	float absorbance = polynomial[0] + dot (b.xy, polynomial.yz);
	// the reconstruction can degenerate when all absorbance sits at a single depth;
	// treating the whole distribution as being at the fragment's depth is exact in that case
	if (isnan (absorbance))
		absorbance = MBOIT_OVERESTIMATION;
	// Turn the normalized absorbance into transmittance
	return clamp (exp (-b_0 * absorbance), 0.0f, 1.0f);
}

float MBOITTransmittanceAtDepth (float z, float b0, vec4 moments0)
{
	if (b0 <= 1e-3f)
		return 1.0f;

	moments0 /= b0;

	// the bias strength is well above the paper's 32 bit recommendation: a single fragment
	// produces a rank one Hankel matrix and the pivots of its Cholesky factorization drown
	// in fp32 cancellation noise with weaker biasing, turning isolated surfaces into NaNs
	return MBOITTransmittance4 (b0, moments0.yw, moments0.xz, z, 6e-5f, vec4 (0.0f, 0.375f, 0.0f, 0.375f));
}

void WriteMBOITComposite (vec4 frag_color, float b0, vec4 moments0, out vec4 out_color)
{
	frag_color = clamp (frag_color, 0.0f, 1.0f);
	float transmittance = MBOITTransmittanceAtDepth (MBOITDepth (), b0, moments0);
	out_color = vec4 (frag_color.rgb * frag_color.a * transmittance, frag_color.a * transmittance);
}

void MBOITWrite (vec4 frag_color)
{
#if MBOIT_COMPOSITE
	WriteMBOITComposite (frag_color, MBOIT_LOAD (mboit_b0_input).r, MBOIT_LOAD (mboit_moments0_input), out_color);
#else
	WriteMBOITMoments (frag_color, out_b0, out_moments0);
#endif
}
