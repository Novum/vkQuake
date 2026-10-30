/* Voodoo5 spatial supersampling. GPL-2.0-or-later. */
#ifndef RGSS_H
#define RGSS_H

enum
{
	AA_MULTISAMPLE,
	AA_SUPERSAMPLE,
	AA_3DFX_2X,
	AA_3DFX_4X,
	AA_3DFX_8X,
	AA_MODE_COUNT
};

#define RGSS_JITTER_SET		 5
#define RGSS_VIEWPORT_OFFSET 112
#define RGSS_CONSTANT_SIZE	 120

static inline int RGSS_SampleCount (int mode)
{
	return mode == AA_3DFX_2X ? 2 : mode == AA_3DFX_4X ? 4 : mode == AA_3DFX_8X ? 8 : 0;
}

static inline void RGSS_Jitter (int samples, int sample, float *x, float *y)
{
	/* Pixel-center-relative VSA-100 offsets, h3defs.h *_CORRECT_DEF:
	 * https://sources.debian.org/src/glide/2002.04.10ds1-11/glide3x/h5/incsrc/h3defs.h
	 * The old -0.5 pixel convention is not part of Vulkan's pixel-center convention.
	 */
	static const float offsets2[2][2] = {{-0.25f, -0.25f}, {0.25f, 0.25f}};
	static const float offsets4[4][2] = {{-0.125f, -0.375f}, {0.375f, -0.125f}, {-0.375f, 0.125f}, {0.125f, 0.375f}};
	/* Glide's four-chip 8x table, aaXOffset[9] / aaYOffset[9] in gpci.c:
	 * https://sources.debian.org/src/glide/2002.04.10ds1-16/glide3x/h5/glide3/src/gpci.c
	 * GLIDE_34GETENV encodes offsets as offset * 16 + 8; decode to pixel-center-relative values.
	 */
	static const float offsets8[8][2] = {{-0.125f, -0.375f}, {-0.25f, -0.125f}, {0.375f, -0.125f}, {0.125f, -0.25f},
										 {-0.375f, 0.125f},	 {0.25f, 0.125f},	{0.125f, 0.375f},  {-0.125f, 0.25f}};
	const float (*offsets)[2] = samples == 2 ? offsets2 : samples == 4 ? offsets4 : offsets8;
	*x = offsets[sample][0];
	*y = offsets[sample][1];
}

#endif
