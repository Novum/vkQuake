#ifndef WAVELET_SET
#define WAVELET_SET 1
#endif

#define WAVELET_RANK 2
#define WAVELET_COEFFICIENT_COUNT 8
#define WAVELET_COEFFICIENT_MAX 16.0f

layout (set = WAVELET_SET, binding = 0, r32ui) uniform coherent uimage2DArray wavelet_coefficients;

uint WaveletPackCoefficient (vec3 value)
{
	uvec3 quantized = uvec3 (round (clamp (value / WAVELET_COEFFICIENT_MAX, vec3 (0.0f), vec3 (1.0f)) * 1023.0f));
	return quantized.x | (quantized.y << 10) | (quantized.z << 20);
}

vec3 WaveletUnpackCoefficient (uint value)
{
	uvec3 quantized = uvec3 (value & 1023u, (value >> 10) & 1023u, (value >> 20) & 1023u);
	return vec3 (quantized) * (WAVELET_COEFFICIENT_MAX / 1023.0f);
}

vec3 WaveletTransmittance (vec4 frag_color)
{
	return clamp (vec3 (1.0f) - frag_color.a * (vec3 (1.0f) - frag_color.rgb), vec3 (1.0f / 512.0f), vec3 (1.0f));
}

vec3 WaveletSignal (vec4 frag_color)
{
	return -log (WaveletTransmittance (frag_color));
}

float WaveletDepth ()
{
	return clamp (1.0f - gl_FragCoord.z, 0.0f, 1.0f);
}

void WaveletAddCoefficient (int coefficient_index, vec3 addend)
{
	ivec3 coord = ivec3 (ivec2 (gl_FragCoord.xy), coefficient_index);
	uint  old_value = imageLoad (wavelet_coefficients, coord).r;

	for (int attempt = 0; attempt < 1024; ++attempt)
	{
		vec3 current = WaveletUnpackCoefficient (old_value);
		uint new_value = WaveletPackCoefficient (max (current + addend, vec3 (0.0f)));
		if (new_value == old_value)
			break;

		uint read_value = imageAtomicCompSwap (wavelet_coefficients, coord, old_value, new_value);
		if (read_value == old_value)
			break;
		old_value = read_value;
	}
}

void WaveletAddEvent (vec3 signal, float depth)
{
	depth *= float (WAVELET_COEFFICIENT_COUNT - 1) / float (WAVELET_COEFFICIENT_COUNT);

	int index = clamp (int (floor (depth * float (WAVELET_COEFFICIENT_COUNT))), 0, WAVELET_COEFFICIENT_COUNT - 1);
	index += WAVELET_COEFFICIENT_COUNT - 1;

	for (int i = 0; i < WAVELET_RANK + 1; ++i)
	{
		int	  power = WAVELET_RANK - i;
		int	  new_index = (index - 1) >> 1;
		float k = float ((new_index + 1) & ((1 << power) - 1));

		int	  wavelet_sign = ((index & 1) << 1) - 1;
		float wavelet_phase = float ((index + 1) & 1) * exp2 (-float (power));
		vec3  addend = ((-exp2 (-float (power)) * k + depth) * float (wavelet_sign) + wavelet_phase) * exp2 (float (power) * 0.5f) * signal;
		WaveletAddCoefficient (new_index, addend);

		index = new_index;
	}

	WaveletAddCoefficient (WAVELET_COEFFICIENT_COUNT - 1, signal * (1.0f - depth));
}

vec3 WaveletSampleCoefficient (int coefficient_index)
{
	ivec3 coord = ivec3 (ivec2 (gl_FragCoord.xy), coefficient_index);
	return WaveletUnpackCoefficient (imageLoad (wavelet_coefficients, coord).r);
}

vec3 WaveletEvaluateIndex (int index)
{
	vec3 result = vec3 (0.0f);
	index += WAVELET_COEFFICIENT_COUNT - 1;

	for (int i = 0; i < WAVELET_RANK + 1; ++i)
	{
		int	 power = WAVELET_RANK - i;
		int	 new_index = (index - 1) >> 1;
		vec3 coeff = WaveletSampleCoefficient (new_index);
		int	 wavelet_sign = ((index & 1) << 1) - 1;
		result -= exp2 (float (power) * 0.5f) * coeff * float (wavelet_sign);
		index = new_index;
	}
	return result;
}

vec3 WaveletEvaluate (float depth, vec3 self_signal)
{
	vec3 scale_coefficient = WaveletSampleCoefficient (WAVELET_COEFFICIENT_COUNT - 1);
	scale_coefficient -= self_signal * (1.0f - depth);

	depth *= float (WAVELET_COEFFICIENT_COUNT - 1) / float (WAVELET_COEFFICIENT_COUNT);

	float coefficient_depth = depth * float (WAVELET_COEFFICIENT_COUNT);
	int	  index = clamp (int (floor (coefficient_depth)), 0, WAVELET_COEFFICIENT_COUNT - 1);

	vec3 a = vec3 (0.0f);
	vec3 b = scale_coefficient + WaveletEvaluateIndex (index);
	if (index > 0)
		a = scale_coefficient + WaveletEvaluateIndex (index - 1);

	float t = coefficient_depth >= float (WAVELET_COEFFICIENT_COUNT) ? 1.0f : fract (coefficient_depth);
	return max (mix (a, b, t), vec3 (0.0f));
}

vec3 WaveletTotalTransmittance ()
{
	return exp (-WaveletEvaluate (0.0f, vec3 (0.0f)));
}

void WriteWaveletCoefficients (vec4 frag_color)
{
	WaveletAddEvent (WaveletSignal (frag_color), WaveletDepth ());
}

void WriteWaveletShaded (inout vec4 frag_color)
{
	vec3 signal = WaveletSignal (frag_color);
	vec3 transmittance = exp (-WaveletEvaluate (WaveletDepth (), signal));
	frag_color.rgb *= frag_color.a * transmittance;
	frag_color.a = 0.0f;
}
