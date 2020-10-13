/*
Copyright (C) 1996-2001 Id Software, Inc.
Copyright (C) 2010-2011 O. Sezer <sezero@users.sourceforge.net>
Copyright (C) 2010-2014 QuakeSpasm developers

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
// snd_mix.c -- portable code to mix sounds for snd_dma.c

#include "quakedef.h"

#define	PAINTBUFFER_SIZE	2048
portable_samplepair_t paintbuffer[PAINTBUFFER_SIZE];
int		snd_scaletable[32][256];
int		*snd_p, snd_linear_count;
short		*snd_out;

static int	snd_vol;

static void Snd_WriteLinearBlastStereo16 (void)
{
	int		i;
	int		val;

	for (i = 0; i < snd_linear_count; i += 2)
	{
		val = snd_p[i] / 256;
		if (val > 0x7fff)
			snd_out[i] = 0x7fff;
		else if (val < (short)0x8000)
			snd_out[i] = (short)0x8000;
		else
			snd_out[i] = val;

		val = snd_p[i+1] / 256;
		if (val > 0x7fff)
			snd_out[i+1] = 0x7fff;
		else if (val < (short)0x8000)
			snd_out[i+1] = (short)0x8000;
		else
			snd_out[i+1] = val;
	}
}

static void S_TransferStereo16 (int endtime)
{
	int		lpos;
	int		lpaintedtime;

	snd_p = (int *) paintbuffer;
	lpaintedtime = paintedtime;

	while (lpaintedtime < endtime)
	{
	// handle recirculating buffer issues
		lpos = lpaintedtime & ((shm->samples >> 1) - 1);

		snd_out = (short *)shm->buffer + (lpos << 1);

		snd_linear_count = (shm->samples >> 1) - lpos;
		if (lpaintedtime + snd_linear_count > endtime)
			snd_linear_count = endtime - lpaintedtime;

		snd_linear_count <<= 1;

	// write a linear blast of samples
		Snd_WriteLinearBlastStereo16 ();

		snd_p += snd_linear_count;
		lpaintedtime += (snd_linear_count >> 1);
	}
}

static void S_TransferPaintBuffer (int endtime)
{
	int	out_idx, out_mask;
	int	count, step, val;
	int	*p;

	if (shm->samplebits == 16 && shm->channels == 2)
	{
		S_TransferStereo16 (endtime);
		return;
	}

	p = (int *) paintbuffer;
	count = (endtime - paintedtime) * shm->channels;
	out_mask = shm->samples - 1;
	out_idx = paintedtime * shm->channels & out_mask;
	step = 3 - shm->channels;

	if (shm->samplebits == 16)
	{
		short *out = (short *)shm->buffer;
		while (count--)
		{
			val = *p / 256;
			p+= step;
			if (val > 0x7fff)
				val = 0x7fff;
			else if (val < (short)0x8000)
				val = (short)0x8000;
			out[out_idx] = val;
			out_idx = (out_idx + 1) & out_mask;
		}
	}
	else if (shm->samplebits == 8 && !shm->signed8)
	{
		unsigned char *out = shm->buffer;
		while (count--)
		{
			val = *p / 256;
			p+= step;
			if (val > 0x7fff)
				val = 0x7fff;
			else if (val < (short)0x8000)
				val = (short)0x8000;
			out[out_idx] = (val / 256) + 128;
			out_idx = (out_idx + 1) & out_mask;
		}
	}
	else if (shm->samplebits == 8)	/* S8 format, e.g. with Amiga AHI */
	{
		signed char *out = (signed char *) shm->buffer;
		while (count--)
		{
			val = *p / 256;
			p+= step;
			if (val > 0x7fff)
				val = 0x7fff;
			else if (val < (short)0x8000)
				val = (short)0x8000;
			out[out_idx] = (val / 256);
			out_idx = (out_idx + 1) & out_mask;
		}
	}
}

/*
==============
S_MakeBlackmanWindowKernel

Makes a lowpass filter kernel, from equation 16-4 in
"The Scientist and Engineer's Guide to Digital Signal Processing"

M is the kernel size (not counting the center point), must be even
kernel has room for M+1 floats
f_c is the filter cutoff frequency, as a fraction of the samplerate
==============
*/
static void S_MakeBlackmanWindowKernel(float *kernel, int M, float f_c)
{
	int i;
	for (i = 0; i <= M; i++)
	{
		if (i == M/2)
		{
			kernel[i] = 2 * M_PI * f_c;
		}
		else
		{
			kernel[i] = ( sin(2 * M_PI * f_c * (i - M/2.0)) / (i - (M/2.0)) )
				* (0.42 - 0.5*cos(2 * M_PI * i / (double)M)
				   + 0.08*cos(4 * M_PI * i / (double)M) );
		}
	}

// normalize the kernel so all of the values sum to 1
	{
		float sum = 0;
		for (i = 0; i <= M; i++)
		{
			sum += kernel[i];
		}

		for (i = 0; i <= M; i++)
		{
			kernel[i] /= sum;
		}
	}
}

typedef struct {
	float *memory;  // kernelsize floats
	float *kernel;  // kernelsize floats
	int kernelsize; // M+1, rounded up to be a multiple of 16
	int M;			// M value used to make kernel, even
	int parity;		// 0-3
	float f_c;		// cutoff frequency, [0..1], fraction of sample rate
} filter_t;

static void S_UpdateFilter(filter_t *filter, int M, float f_c)
{
	if (filter->f_c != f_c || filter->M != M)
	{
		if (filter->memory != NULL) free(filter->memory);
		if (filter->kernel != NULL) free(filter->kernel);

		filter->M = M;
		filter->f_c = f_c;

		filter->parity = 0;
	// M + 1 rounded up to the next multiple of 16
		filter->kernelsize = (M + 1) + 16 - ((M + 1) % 16);
		filter->memory = (float *) calloc(filter->kernelsize, sizeof(float));
		filter->kernel = (float *) calloc(filter->kernelsize, sizeof(float));
		
		S_MakeBlackmanWindowKernel(filter->kernel, M, f_c);
	}
}

/*
==============
S_ApplyFilter

Lowpass-filter the given buffer containing 44100Hz audio.

As an optimization, it decimates the audio to 11025Hz (setting every sample
position that's not a multiple of 4 to 0), then convoluting with the filter
kernel is 4x faster, because we can skip 3/4 of the input samples that are
known to be 0 and skip 3/4 of the filter kernel.
==============
*/
static void S_ApplyFilter(filter_t *filter, int *data, int stride, int count)
{
	int i, j;
	float *input;
	const int kernelsize = filter->kernelsize;
	const float *kernel = filter->kernel;
	int parity;

	input = (float *) malloc(sizeof(float) * (filter->kernelsize + count));

// set up the input buffer
// memory holds the previous filter->kernelsize samples of input.
	memcpy(input, filter->memory, filter->kernelsize * sizeof(float));

	for (i=0; i<count; i++)
	{
		input[filter->kernelsize+i] = data[i * stride] / (32768.0 * 256.0);
	}

// copy out the last filter->kernelsize samples to 'memory' for next time
	memcpy(filter->memory, input + count, filter->kernelsize * sizeof(float));

// apply the filter
	parity = filter->parity;

	for (i=0; i<count; i++)
	{
		const float *input_plus_i = input + i;
		float val[4] = {0, 0, 0, 0};

		for (j = (4 - parity) % 4; j < kernelsize; j+=16)
		{
			val[0] += kernel[j] * input_plus_i[j];
			val[1] += kernel[j+4] * input_plus_i[j+4];
			val[2] += kernel[j+8] * input_plus_i[j+8];
			val[3] += kernel[j+12] * input_plus_i[j+12];
		}

	// 4.0 factor is to increase volume by 12 dB; this is to make up the
	// volume drop caused by the zero-filling this filter does.
		data[i * stride] = (val[0] + val[1] + val[2] + val[3])
			* (32768.0 * 256.0 * 4.0);

		parity = (parity + 1) % 4;
	}

	filter->parity = parity;

	free(input);
}

/*
==============
S_LowpassFilter

lowpass filters 24-bit integer samples in 'data' (stored in 32-bit ints).
assumes 44100Hz sample rate, and lowpasses at around 5kHz
memory should be a zero-filled filter_t struct
==============
*/
static void S_LowpassFilter(int *data, int stride, int count,
							filter_t *memory)
{
	int M;
	float bw, f_c;

	switch ((int)snd_filterquality.value)
	{
	case 1:
		M = 126; bw = 0.900; break;
	case 2:
		M = 150; bw = 0.915; break;
	case 3:
		M = 174; bw = 0.930; break;
	case 4:
		M = 198; bw = 0.945; break;
	case 5:
	default:
		M = 222; bw = 0.960; break;
	}

	f_c = (bw * 11025 / 2.0) / 44100.0;

	S_UpdateFilter(memory, M, f_c);
	S_ApplyFilter(memory, data, stride, count);
}

/*
===============================================================================

CHANNEL MIXING

===============================================================================
*/

static void SND_PaintChannelFrom8 (channel_t *ch, sfxcache_t *sc, int endtime, int paintbufferstart);
static void SND_PaintChannelFrom16 (channel_t *ch, sfxcache_t *sc, int endtime, int paintbufferstart);

void S_PaintChannels (int endtime)
{
	int		i;
	int		end, ltime, count;
	channel_t	*ch;
	sfxcache_t	*sc;

	snd_vol = sfxvolume.value * 256;

	while (paintedtime < endtime)
	{
	// if paintbuffer is smaller than DMA buffer
		end = endtime;
		if (endtime - paintedtime > PAINTBUFFER_SIZE)
			end = paintedtime + PAINTBUFFER_SIZE;

	// clear the paint buffer
		memset(paintbuffer, 0, (end - paintedtime) * sizeof(portable_samplepair_t));

	// paint in the channels.
		ch = snd_channels;
		for (i = 0; i < total_channels; i++, ch++)
		{
			if (!ch->sfx)
				continue;
			if (!ch->leftvol && !ch->rightvol)
				continue;
			sc = S_LoadSound (ch->sfx);
			if (!sc)
				continue;

			ltime = paintedtime;

			while (ltime < end)
			{	// paint up to end
				if (ch->end < end)
					count = ch->end - ltime;
				else
					count = end - ltime;

				if (count > 0)
				{
					// the last param to SND_PaintChannelFrom is the index
					// to start painting to in the paintbuffer, usually 0.
					if (sc->width == 1)
						SND_PaintChannelFrom8(ch, sc, count, ltime - paintedtime);
					else
						SND_PaintChannelFrom16(ch, sc, count, ltime - paintedtime);

					ltime += count;
				}

			// if at end of loop, restart
				if (ltime >= ch->end)
				{
					if (sc->loopstart >= 0)
					{
						ch->pos = sc->loopstart;
						ch->end = ltime + sc->length - ch->pos;
					}
					else
					{	// channel just stopped
						ch->sfx = NULL;
						break;
					}
				}
			}
		}

	// clip each sample to 0dB, then reduce by 6dB (to leave some headroom for
	// the lowpass filter and the music). the lowpass will smooth out the
	// clipping
		for (i=0; i<end-paintedtime; i++)
		{
			paintbuffer[i].left = CLAMP(-32768 * 256, paintbuffer[i].left, 32767 * 256) / 2;
			paintbuffer[i].right = CLAMP(-32768 * 256, paintbuffer[i].right, 32767 * 256) / 2;
		}

	// apply a lowpass filter
		if (sndspeed.value == 11025 && shm->speed == 44100)
		{
			static filter_t memory_l, memory_r;
			S_LowpassFilter((int *)paintbuffer,       2, end - paintedtime, &memory_l);
			S_LowpassFilter(((int *)paintbuffer) + 1, 2, end - paintedtime, &memory_r);
		}

	// paint in the music
		if (s_rawend >= paintedtime)
		{	// copy from the streaming sound source
			int		s;
			int		stop;

			stop = (end < s_rawend) ? end : s_rawend;

			for (i = paintedtime; i < stop; i++)
			{
				s = i & (MAX_RAW_SAMPLES - 1);
			// lower music by 6db to match sfx
				paintbuffer[i - paintedtime].left += s_rawsamples[s].left / 2;
				paintbuffer[i - paintedtime].right += s_rawsamples[s].right / 2;
			}
			//	if (i != end)
			//		Con_Printf ("partial stream\n");
			//	else
			//		Con_Printf ("full stream\n");
		}

	// transfer out according to DMA format
		S_TransferPaintBuffer(end);
		paintedtime = end;
	}
}

void SND_InitScaletable (void)
{
	int		i, j;
	int		scale;

	for (i = 0; i < 32; i++)
	{
		scale = i * 8 * 256 * sfxvolume.value;
		for (j = 0; j < 256; j++)
		{
		/* When compiling with gcc-4.1.0 at optimisations O1 and
		   higher, the tricky signed char type conversion is not
		   guaranteed. Therefore we explicity calculate the signed
		   value from the index as required. From Kevin Shanahan.
		   See: http://gcc.gnu.org/bugzilla/show_bug.cgi?id=26719
		*/
		//	snd_scaletable[i][j] = ((signed char)j) * scale;
			snd_scaletable[i][j] = ((j < 128) ?  j : j - 256) * scale;
		}
	}
}


static void SND_PaintChannelFrom8 (channel_t *ch, sfxcache_t *sc, int count, int paintbufferstart)
{
	int	data;
	int		*lscale, *rscale;
	unsigned char	*sfx;
	int		i;

	if (ch->leftvol > 255)
		ch->leftvol = 255;
	if (ch->rightvol > 255)
		ch->rightvol = 255;

	lscale = snd_scaletable[ch->leftvol >> 3];
	rscale = snd_scaletable[ch->rightvol >> 3];
	sfx = (unsigned char *)sc->data + ch->pos;

	for (i = 0; i < count; i++)
	{
		data = sfx[i];
		paintbuffer[paintbufferstart + i].left += lscale[data];
		paintbuffer[paintbufferstart + i].right += rscale[data];
	}

	ch->pos += count;
}

static void SND_PaintChannelFrom16 (channel_t *ch, sfxcache_t *sc, int count, int paintbufferstart)
{
	int	data;
	int	left, right;
	int	leftvol, rightvol;
	signed short	*sfx;
	int	i;

	leftvol = ch->leftvol * snd_vol;
	rightvol = ch->rightvol * snd_vol;
	leftvol /= 256;
	rightvol /= 256;
	sfx = (signed short *)sc->data + ch->pos;

	for (i = 0; i < count; i++)
	{
		data = sfx[i];
	// this was causing integer overflow as observed in quakespasm
	// with the warpspasm mod moved >>8 to left/right volume above.
	//	left = (data * leftvol) >> 8;
	//	right = (data * rightvol) >> 8;
		left = data * leftvol;
		right = data * rightvol;
		paintbuffer[paintbufferstart + i].left += left;
		paintbuffer[paintbufferstart + i].right += right;
	}

	ch->pos += count;
}

