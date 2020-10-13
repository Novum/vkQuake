/*
Copyright (C) 1996-1997 Id Software, Inc.

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

#include "quakedef.h"

/*
authorship and stuff:
Audio resampling functions:   Originally from DarkPlaces, presumably LordHavoc is the original author.
Raw Audio Capture (DSound):   Spike.
Networking:                   Spike.
Raw Audio Playback/Streaming: qqshka: This code is taken straight from ezQuake.
Codec:                        The Speex team.
Codec:                        Opus peeps...

Compatibility:
This code requires the engine to know if the peer also supports the extension.
Servers send a 'cmd pext\n' stufftext. The client intercepts this and replaces it with a 'cmd pext KEY VALUE ...\n' command.
When the server receives the extended pext command, it reads the key+value pairs, and tells the client the extensions that be used for the connection.
No voice commands nor packets will be sent which the other is unable to understand. The exception is in the form of demos. Such demos will require the replaying client to understand the data.


*/

/*****************************************************************************************************************************/
/*System componant (should be inside sys_win.c, only win32 supported)*/
/*sidenote: should probably just statically link*/

#ifdef USE_CODEC_OPUS
#ifndef _WIN32	//for some reason, the win32 libs lack opus_encode* symbols. the dlls contain them though. I've no idea what's going on there.
#define OPUS_STATIC
#endif
#endif

typedef struct {
	void **funcptr;
	char *name;
} dllfunction_t;
typedef void *dllhandle_t;
dllhandle_t *Sys_LoadLibrary(const char *name, dllfunction_t *funcs);
void Sys_CloseLibrary(dllhandle_t *lib);

#include "arch_def.h"

#ifdef _WIN32
#include <windows.h>
void Sys_CloseLibrary(dllhandle_t *lib)
{
	FreeLibrary((HMODULE)lib);
}
dllhandle_t *Sys_LoadLibrary(const char *name, dllfunction_t *funcs)
{
	int i;
	HMODULE lib;

	lib = LoadLibrary(name);
	if (!lib)
	{
#ifdef _WIN64
		lib = LoadLibrary(va("%s_64", name));
#elif defined(_WIN32)
		lib = LoadLibrary(va("%s_32", name));
#endif
		if (!lib)
			return NULL;
	}

	if (funcs)
	{
		for (i = 0; funcs[i].name; i++)
		{
			*funcs[i].funcptr = GetProcAddress(lib, funcs[i].name);
			if (!*funcs[i].funcptr)
				break;
		}
		if (funcs[i].name)
		{
			Con_SafePrintf("Symbol %s missing in module %s\n", funcs[i].name, name);
			Sys_CloseLibrary((dllhandle_t*)lib);
			lib = NULL;
		}
	}

	return (dllhandle_t*)lib;
}
#elif defined(PLATFORM_UNIX)
//unixes should have a dlopen (this test includes osx)
#include <dlfcn.h>
void Sys_CloseLibrary(dllhandle_t *lib)
{
	dlclose((void*)lib);
}
dllhandle_t *Sys_LoadLibrary(const char *name, dllfunction_t *funcs)
{
	int i;
	dllhandle_t *lib;

	lib = dlopen (name, RTLD_LAZY|RTLD_LOCAL);
	if (!lib)
		return NULL;

	if (funcs)
	{
		for (i = 0; funcs[i].name; i++)
		{
			*funcs[i].funcptr = dlsym(lib, funcs[i].name);
			if (!*funcs[i].funcptr)
				break;
		}
		if (funcs[i].name)
		{
			Con_SafePrintf("Symbol %s missing in module %s\n", funcs[i].name, name);
			Sys_CloseLibrary((dllhandle_t*)lib);
			lib = NULL;
		}
	}

	return (dllhandle_t*)lib;
}
#else
void Sys_CloseLibrary(dllhandle_t *lib)
{
}
dllhandle_t *Sys_LoadLibrary(const char *name, dllfunction_t *funcs)
{
	Con_SafePrintf("Sys_LoadLibrary(%s) is not implemented on this platform\n", name);
	return NULL;
}
#endif


/*****************************************************************************************************************************/
/*audio capture componant. only dsound supported in this implementation.*/

/*sound capture driver stuff*/
typedef struct
{
	void *(*Init) (int samplerate);			/*create a new context*/
	void (*Start) (void *ctx);		/*begin grabbing new data, old data is potentially flushed*/
	unsigned int (*Update) (void *ctx, unsigned char *buffer, unsigned int minbytes, unsigned int maxbytes);	/*grab the data into a different buffer*/
	void (*Stop) (void *ctx);		/*stop grabbing new data, old data may remain*/
	void (*Shutdown) (void *ctx);	/*destroy everything*/
} snd_capture_driver_t;

#if defined(USE_SDL2) && SDL_VERSION_ATLEAST(2,0,5)
	#define USE_SDL_CAPTURE
#elif defined(_WIN32)
	#define USE_DSOUND_CAPTURE
#else
	//user is screwed. linux has too many competing apis that probably won't work.
	#pragma message("No VOIP audio capture supported")
#endif

#ifdef USE_DSOUND_CAPTURE
#include <mmsystem.h>
#include <dsound.h>
static HINSTANCE hInstDSC;
static HRESULT (WINAPI *pDirectSoundCaptureCreate)(GUID FAR *lpGUID, LPDIRECTSOUNDCAPTURE FAR *lplpDS, IUnknown FAR *pUnkOuter);

typedef struct
{
	LPDIRECTSOUNDCAPTURE DSCapture;
	LPDIRECTSOUNDCAPTUREBUFFER DSCaptureBuffer;
	long lastreadpos;
} dsndcapture_t;
static const long bufferbytes = 1024*1024;

static const long inputwidth = 2;

static void *DSOUND_Capture_Init (int rate)
{
	dsndcapture_t *result;
	DSCBUFFERDESC bufdesc;

	WAVEFORMATEX  wfxFormat;

	wfxFormat.wFormatTag = WAVE_FORMAT_PCM;
	wfxFormat.nChannels = 1;
	wfxFormat.nSamplesPerSec = rate;
	wfxFormat.wBitsPerSample = 8*inputwidth;
	wfxFormat.nBlockAlign = wfxFormat.nChannels * (wfxFormat.wBitsPerSample / 8);
	wfxFormat.nAvgBytesPerSec = wfxFormat.nSamplesPerSec * wfxFormat.nBlockAlign;
	wfxFormat.cbSize = 0;

	bufdesc.dwSize = sizeof(bufdesc);
	bufdesc.dwBufferBytes = bufferbytes;
	bufdesc.dwFlags = 0;
	bufdesc.dwReserved = 0;
	bufdesc.lpwfxFormat = &wfxFormat;

	/*probably already inited*/
	if (!hInstDSC)
	{
		hInstDSC = LoadLibrary("dsound.dll");

		if (hInstDSC == NULL)
		{
			Con_SafePrintf ("Couldn't load dsound.dll\n");
			return NULL;
		}
	}
	/*global pointer, used only in this function*/
	if (!pDirectSoundCaptureCreate)
	{
		pDirectSoundCaptureCreate = (void *)GetProcAddress(hInstDSC, "DirectSoundCaptureCreate");

		if (!pDirectSoundCaptureCreate)
		{
			Con_SafePrintf ("Couldn't get DS proc addr\n");
			return NULL;
		}

//		pDirectSoundCaptureEnumerate = (void *)GetProcAddress(hInstDS,"DirectSoundCaptureEnumerateA");
	}

	result = Z_Malloc(sizeof(*result));
	if (!FAILED(pDirectSoundCaptureCreate(NULL, &result->DSCapture, NULL)))
	{
		if (!FAILED(IDirectSoundCapture_CreateCaptureBuffer(result->DSCapture, &bufdesc, &result->DSCaptureBuffer, NULL)))
		{
			return result;
		}
		IDirectSoundCapture_Release(result->DSCapture);
		Con_SafePrintf ("Couldn't create a capture buffer\n");
	}
	Z_Free(result);
	return NULL;
}

static void DSOUND_Capture_Start(void *ctx)
{
	DWORD capturePos;
	dsndcapture_t *c = ctx;
	IDirectSoundCaptureBuffer_Start(c->DSCaptureBuffer, DSBPLAY_LOOPING);

	c->lastreadpos = 0;
	IDirectSoundCaptureBuffer_GetCurrentPosition(c->DSCaptureBuffer, &capturePos, &c->lastreadpos);
}

static void DSOUND_Capture_Stop(void *ctx)
{
	dsndcapture_t *c = ctx;
	IDirectSoundCaptureBuffer_Stop(c->DSCaptureBuffer);
}

static void DSOUND_Capture_Shutdown(void *ctx)
{
	dsndcapture_t *c = ctx;
	if (c->DSCaptureBuffer)
	{
		IDirectSoundCaptureBuffer_Stop(c->DSCaptureBuffer);
		IDirectSoundCaptureBuffer_Release(c->DSCaptureBuffer);
	}
	if (c->DSCapture)
	{
		IDirectSoundCapture_Release(c->DSCapture);
	}
	Z_Free(ctx);
}

/*minsamples is a hint*/
static unsigned int DSOUND_Capture_Update(void *ctx, unsigned char *buffer, unsigned int minbytes, unsigned int maxbytes)
{
	dsndcapture_t *c = ctx;
	HRESULT hr;
	LPBYTE lpbuf1 = NULL;
	LPBYTE lpbuf2 = NULL;
	DWORD dwsize1 = 0;
	DWORD dwsize2 = 0;

	DWORD capturePos;
	DWORD readPos;
	long  filled;

// Query to see how much data is in buffer.
	hr = IDirectSoundCaptureBuffer_GetCurrentPosition(c->DSCaptureBuffer, &capturePos, &readPos);
	if (hr != DS_OK)
	{
		return 0;
	}
	filled = readPos - c->lastreadpos;
	if (filled < 0)
		filled += bufferbytes; // unwrap offset

	if (filled > (long)maxbytes)	//figure out how much we need to empty it by, and if that's enough to be worthwhile.
		filled = maxbytes;
	else if (filled < (long)minbytes)
		return 0;

//	filled /= inputwidth;
//	filled *= inputwidth;

	// Lock free space in the DS
	hr = IDirectSoundCaptureBuffer_Lock(c->DSCaptureBuffer, c->lastreadpos, filled, (void **) &lpbuf1, &dwsize1, (void **) &lpbuf2, &dwsize2, 0);
	if (hr == DS_OK)
	{
		// Copy from DS to the buffer
		memcpy(buffer, lpbuf1, dwsize1);
		if(lpbuf2 != NULL)
		{
			memcpy(buffer+dwsize1, lpbuf2, dwsize2);
		}
		// Update our buffer offset and unlock sound buffer
 		c->lastreadpos = (c->lastreadpos + dwsize1 + dwsize2) % bufferbytes;
		IDirectSoundCaptureBuffer_Unlock(c->DSCaptureBuffer, lpbuf1, dwsize1, lpbuf2, dwsize2);
	}
	else
	{
		return 0;
	}
	return filled;
}
static snd_capture_driver_t DSOUND_Capture =
{
	DSOUND_Capture_Init,
	DSOUND_Capture_Start,
	DSOUND_Capture_Update,
	DSOUND_Capture_Stop,
	DSOUND_Capture_Shutdown
};
#endif
#ifdef USE_SDL_CAPTURE
//Requires SDL 2.0.5+ supposedly.
//Bugging out for me on windows, with really low audio levels. looks like there's been some float->int conversion without a multiplier. asking for float audio gives stupidly low values too.
typedef struct
{
	SDL_AudioDeviceID dev;
} sdlcapture_t;

static void SDL_Capture_Start(void *ctx)
{
	sdlcapture_t *d = ctx;
	SDL_PauseAudioDevice(d->dev, SDL_FALSE);
}

static void SDL_Capture_Stop(void *ctx)
{
	sdlcapture_t *d = ctx;
	SDL_PauseAudioDevice(d->dev, SDL_TRUE);
}

static void SDL_Capture_Shutdown(void *ctx)
{
	sdlcapture_t *d = ctx;
	SDL_CloseAudioDevice(d->dev);
	Z_Free(d);
}

static void *SDL_Capture_Init (int rate)
{
	SDL_AudioSpec want, have;
	sdlcapture_t c, *r;

	SDL_memset(&want, 0, sizeof(want)); /* or SDL_zero(want) */
	want.freq = rate;
	want.format = AUDIO_S16;
	want.channels = 1;
	want.samples = 256;	//this seems to be chunk sizes rather than total buffer size, so lets keep it reasonably small for lower latencies
	want.callback = NULL;

	c.dev = SDL_OpenAudioDevice(NULL, true, &want, &have, 0);
	if (!c.dev)	//failed?
		return NULL;

	r = Z_Malloc(sizeof(*r));
	*r = c;
	return r;
}

/*minbytes is a hint to not bother wasting time*/
static unsigned int SDL_Capture_Update(void *ctx, unsigned char *buffer, unsigned int minbytes, unsigned int maxbytes)
{
	sdlcapture_t *c = ctx;
	unsigned int queuedsize = SDL_GetQueuedAudioSize(c->dev);
	if (queuedsize < minbytes)
		return 0;
	if (queuedsize > maxbytes)
		queuedsize = maxbytes;

	queuedsize = SDL_DequeueAudio(c->dev, buffer, queuedsize);
	return queuedsize;
}
static snd_capture_driver_t SDL_Capture =
{
	SDL_Capture_Init,
	SDL_Capture_Start,
	SDL_Capture_Update,
	SDL_Capture_Stop,
	SDL_Capture_Shutdown
};
#endif

/*****************************************************************************************************************************/

/*Mixer stuff*/

#define LINEARUPSCALE(in, inrate, insamps, out, outrate, outlshift, outrshift) \
		{ \
			scale = inrate / (double)outrate; \
			infrac = floor(scale * 65536); \
			outsamps = insamps / scale; \
			inaccum = 0; \
			outnlsamps = floor(1.0 / scale); \
			outsamps -= outnlsamps; \
			\
			while (outsamps) \
			{ \
				*out = ((0xFFFF - inaccum)*in[0] + inaccum*in[1]) >> (16 - outlshift + outrshift); \
				inaccum += infrac; \
				in += (inaccum >> 16); \
				inaccum &= 0xFFFF; \
				out++; \
				outsamps--; \
			} \
			while (outnlsamps) \
			{ \
				*out = (*in >> outrshift) << outlshift; \
				out++; \
				outnlsamps--; \
			} \
		}

#define LINEARUPSCALESTEREO(in, inrate, insamps, out, outrate, outlshift, outrshift) \
		{ \
			scale = inrate / (double)outrate; \
			infrac = floor(scale * 65536); \
			outsamps = insamps / scale; \
			inaccum = 0; \
			outnlsamps = floor(1.0 / scale); \
			outsamps -= outnlsamps; \
			\
			while (outsamps) \
			{ \
				out[0] = ((0xFFFF - inaccum)*in[0] + inaccum*in[2]) >> (16 - outlshift + outrshift); \
				out[1] = ((0xFFFF - inaccum)*in[1] + inaccum*in[3]) >> (16 - outlshift + outrshift); \
				inaccum += infrac; \
				in += (inaccum >> 16) * 2; \
				inaccum &= 0xFFFF; \
				out += 2; \
				outsamps--; \
			} \
			while (outnlsamps) \
			{ \
				out[0] = (in[0] >> outrshift) << outlshift; \
				out[1] = (in[1] >> outrshift) << outlshift; \
				out += 2; \
				outnlsamps--; \
			} \
		}

#define LINEARUPSCALESTEREOTOMONO(in, inrate, insamps, out, outrate, outlshift, outrshift) \
		{ \
			scale = inrate / (double)outrate; \
			infrac = floor(scale * 65536); \
			outsamps = insamps / scale; \
			inaccum = 0; \
			outnlsamps = floor(1.0 / scale); \
			outsamps -= outnlsamps; \
			\
			while (outsamps) \
			{ \
				*out = ((((0xFFFF - inaccum)*in[0] + inaccum*in[2]) >> (16 - outlshift + outrshift)) + \
				(((0xFFFF - inaccum)*in[1] + inaccum*in[3]) >> (16 - outlshift + outrshift))) >> 1; \
				inaccum += infrac; \
				in += (inaccum >> 16) * 2; \
				inaccum &= 0xFFFF; \
				out++; \
				outsamps--; \
			} \
			while (outnlsamps) \
			{ \
				out[0] = (((in[0] >> outrshift) << outlshift) + ((in[1] >> outrshift) << outlshift)) >> 1; \
				out++; \
				outnlsamps--; \
			} \
		}

#define LINEARDOWNSCALE(in, inrate, insamps, out, outrate, outlshift, outrshift) \
		{ \
			scale = outrate / (double)inrate; \
			infrac = floor(scale * 65536); \
			inaccum = 0; \
			insamps--; \
			outsampleft = 0; \
			\
			while (insamps) \
			{ \
				inaccum += infrac; \
				if (inaccum >> 16) \
				{ \
					inaccum &= 0xFFFF; \
					outsampleft += (infrac - inaccum) * (*in); \
					*out = outsampleft >> (16 - outlshift + outrshift); \
					out++; \
					outsampleft = inaccum * (*in); \
				} \
				else \
					outsampleft += infrac * (*in); \
				in++; \
				insamps--; \
			} \
			outsampleft += (0xFFFF - inaccum) * (*in);\
			*out = outsampleft >> (16 - outlshift + outrshift); \
		}

#define LINEARDOWNSCALESTEREO(in, inrate, insamps, out, outrate, outlshift, outrshift) \
		{ \
			scale = outrate / (double)inrate; \
			infrac = floor(scale * 65536); \
			inaccum = 0; \
			insamps--; \
			outsampleft = 0; \
			outsampright = 0; \
			\
			while (insamps) \
			{ \
				inaccum += infrac; \
				if (inaccum >> 16) \
				{ \
					inaccum &= 0xFFFF; \
					outsampleft += (infrac - inaccum) * in[0]; \
					outsampright += (infrac - inaccum) * in[1]; \
					out[0] = outsampleft >> (16 - outlshift + outrshift); \
					out[1] = outsampright >> (16 - outlshift + outrshift); \
					out += 2; \
					outsampleft = inaccum * in[0]; \
					outsampright = inaccum * in[1]; \
				} \
				else \
				{ \
					outsampleft += infrac * in[0]; \
					outsampright += infrac * in[1]; \
				} \
				in += 2; \
				insamps--; \
			} \
			outsampleft += (0xFFFF - inaccum) * in[0];\
			outsampright += (0xFFFF - inaccum) * in[1];\
			out[0] = outsampleft >> (16 - outlshift + outrshift); \
			out[1] = outsampright >> (16 - outlshift + outrshift); \
		}

#define LINEARDOWNSCALESTEREOTOMONO(in, inrate, insamps, out, outrate, outlshift, outrshift) \
		{ \
			scale = outrate / (double)inrate; \
			infrac = floor(scale * 65536); \
			inaccum = 0; \
			insamps--; \
			outsampleft = 0; \
			\
			while (insamps) \
			{ \
				inaccum += infrac; \
				if (inaccum >> 16) \
				{ \
					inaccum &= 0xFFFF; \
					outsampleft += (infrac - inaccum) * ((in[0] + in[1]) >> 1); \
					*out = outsampleft >> (16 - outlshift + outrshift); \
					out++; \
					outsampleft = inaccum * ((in[0] + in[1]) >> 1); \
				} \
				else \
					outsampleft += infrac * ((in[0] + in[1]) >> 1); \
				in += 2; \
				insamps--; \
			} \
			outsampleft += (0xFFFF - inaccum) * ((in[0] + in[1]) >> 1);\
			*out = outsampleft >> (16 - outlshift + outrshift); \
		}

#define STANDARDRESCALE(in, inrate, insamps, out, outrate, outlshift, outrshift) \
		{ \
			scale = inrate / (double)outrate; \
			infrac = floor(scale * 65536); \
			outsamps = insamps / scale; \
			inaccum = 0; \
			\
			while (outsamps) \
			{ \
				*out = (*in >> outrshift) << outlshift; \
				inaccum += infrac; \
				in += (inaccum >> 16); \
				inaccum &= 0xFFFF; \
				out++; \
				outsamps--; \
			} \
		}

#define STANDARDRESCALESTEREO(in, inrate, insamps, out, outrate, outlshift, outrshift) \
		{ \
			scale = inrate / (double)outrate; \
			infrac = floor(scale * 65536); \
			outsamps = insamps / scale; \
			inaccum = 0; \
			\
			while (outsamps) \
			{ \
				out[0] = (in[0] >> outrshift) << outlshift; \
				out[1] = (in[1] >> outrshift) << outlshift; \
				inaccum += infrac; \
				in += (inaccum >> 16) * 2; \
				inaccum &= 0xFFFF; \
				out += 2; \
				outsamps--; \
			} \
		}

#define STANDARDRESCALESTEREOTOMONO(in, inrate, insamps, out, outrate, outlshift, outrshift) \
		{ \
			scale = inrate / (double)outrate; \
			infrac = floor(scale * 65536); \
			outsamps = insamps / scale; \
			inaccum = 0; \
			\
			while (outsamps) \
			{ \
				out[0] = (((in[0] >> outrshift) << outlshift) + ((in[1] >> outrshift) << outlshift)) >> 1; \
				inaccum += infrac; \
				in += (inaccum >> 16) * 2; \
				inaccum &= 0xFFFF; \
				out++; \
				outsamps--; \
			} \
		}

#define QUICKCONVERT(in, insamps, out, outlshift, outrshift) \
		{ \
			while (insamps) \
			{ \
				*out = (*in >> outrshift) << outlshift; \
				out++; \
				in++; \
				insamps--; \
			} \
		}

#define QUICKCONVERTSTEREOTOMONO(in, insamps, out, outlshift, outrshift) \
		{ \
			while (insamps) \
			{ \
				*out = (((in[0] >> outrshift) << outlshift) + ((in[1] >> outrshift) << outlshift)) >> 1; \
				out++; \
				in += 2; \
				insamps--; \
			} \
		}

// SND_ResampleStream: takes a sound stream and converts with given parameters. Limited to
// 8-16-bit signed conversions and mono-to-mono/stereo-to-stereo conversions.
// Not an in-place algorithm.
void SND_ResampleStream (void *in, int inrate, int inwidth, int inchannels, int insamps, void *out, int outrate, int outwidth, int outchannels, int resampstyle)
{
	double scale;
	signed char *in8 = (signed char *)in;
	short *in16 = (short *)in;
	signed char *out8 = (signed char *)out;
	short *out16 = (short *)out;
	int outsamps, outnlsamps, outsampleft, outsampright;
	int infrac, inaccum;

	if (insamps <= 0)
		return;

	if (inchannels == outchannels && inwidth == outwidth && inrate == outrate)
	{
		memcpy(out, in, inwidth*insamps*inchannels);
		return;
	}

	if (inchannels == 1 && outchannels == 1)
	{
		if (inwidth == 1)
		{
			if (outwidth == 1)
			{
				if (inrate < outrate) // upsample
				{
					if (resampstyle)
						LINEARUPSCALE(in8, inrate, insamps, out8, outrate, 0, 0)
					else
						STANDARDRESCALE(in8, inrate, insamps, out8, outrate, 0, 0)
				}
				else // downsample
				{
				if (resampstyle > 1)
					LINEARDOWNSCALE(in8, inrate, insamps, out8, outrate, 0, 0)
				else
					STANDARDRESCALE(in8, inrate, insamps, out8, outrate, 0, 0)
				}
				return;
			}
			else
			{
				if (inrate == outrate) // quick convert
					QUICKCONVERT(in8, insamps, out16, 8, 0)
				else if (inrate < outrate) // upsample
				{
					if (resampstyle)
						LINEARUPSCALE(in8, inrate, insamps, out16, outrate, 8, 0)
					else
						STANDARDRESCALE(in8, inrate, insamps, out16, outrate, 8, 0)
				}
				else // downsample
				{
					if (resampstyle > 1)
						LINEARDOWNSCALE(in8, inrate, insamps, out16, outrate, 8, 0)
					else
						STANDARDRESCALE(in8, inrate, insamps, out16, outrate, 8, 0)
				}
				return;
			}
		}
		else // 16-bit
		{
			if (outwidth == 2)
			{
				if (inrate < outrate) // upsample
				{
					if (resampstyle)
						LINEARUPSCALE(in16, inrate, insamps, out16, outrate, 0, 0)
					else
						STANDARDRESCALE(in16, inrate, insamps, out16, outrate, 0, 0)
				}
				else // downsample
				{
					if (resampstyle > 1)
						LINEARDOWNSCALE(in16, inrate, insamps, out16, outrate, 0, 0)
					else
						STANDARDRESCALE(in16, inrate, insamps, out16, outrate, 0, 0)
				}
				return;
			}
			else
			{
				if (inrate == outrate) // quick convert
					QUICKCONVERT(in16, insamps, out8, 0, 8)
				else if (inrate < outrate) // upsample
				{
					if (resampstyle)
						LINEARUPSCALE(in16, inrate, insamps, out8, outrate, 0, 8)
					else
						STANDARDRESCALE(in16, inrate, insamps, out8, outrate, 0, 8)
				}
				else // downsample
				{
					if (resampstyle > 1)
						LINEARDOWNSCALE(in16, inrate, insamps, out8, outrate, 0, 8)
					else
						STANDARDRESCALE(in16, inrate, insamps, out8, outrate, 0, 8)
				}
			return;
			}
		}
	}
	else if (outchannels == 2 && inchannels == 2)
	{
		if (inwidth == 1)
		{
			if (outwidth == 1)
			{
				if (inrate < outrate) // upsample
				{
					if (resampstyle)
						LINEARUPSCALESTEREO(in8, inrate, insamps, out8, outrate, 0, 0)
					else
						STANDARDRESCALESTEREO(in8, inrate, insamps, out8, outrate, 0, 0)
				}
				else // downsample
				{
					if (resampstyle > 1)
						LINEARDOWNSCALESTEREO(in8, inrate, insamps, out8, outrate, 0, 0)
					else
						STANDARDRESCALESTEREO(in8, inrate, insamps, out8, outrate, 0, 0)
				}
			}
			else
			{
				if (inrate == outrate) // quick convert
				{
					insamps *= 2;
					QUICKCONVERT(in8, insamps, out16, 8, 0)
				}
				else if (inrate < outrate) // upsample
				{
					if (resampstyle)
						LINEARUPSCALESTEREO(in8, inrate, insamps, out16, outrate, 8, 0)
					else
						STANDARDRESCALESTEREO(in8, inrate, insamps, out16, outrate, 8, 0)
				}
				else // downsample
				{
					if (resampstyle > 1)
						LINEARDOWNSCALESTEREO(in8, inrate, insamps, out16, outrate, 8, 0)
					else
						STANDARDRESCALESTEREO(in8, inrate, insamps, out16, outrate, 8, 0)
				}
			}
		}
		else // 16-bit
		{
			if (outwidth == 2)
			{
				if (inrate < outrate) // upsample
				{
					if (resampstyle)
						LINEARUPSCALESTEREO(in16, inrate, insamps, out16, outrate, 0, 0)
					else
						STANDARDRESCALESTEREO(in16, inrate, insamps, out16, outrate, 0, 0)
				}
				else // downsample
				{
					if (resampstyle > 1)
						LINEARDOWNSCALESTEREO(in16, inrate, insamps, out16, outrate, 0, 0)
					else
						STANDARDRESCALESTEREO(in16, inrate, insamps, out16, outrate, 0, 0)
				}
			}
			else
			{
				if (inrate == outrate) // quick convert
				{
					insamps *= 2;
					QUICKCONVERT(in16, insamps, out8, 0, 8)
				}
				else if (inrate < outrate) // upsample
				{
					if (resampstyle)
						LINEARUPSCALESTEREO(in16, inrate, insamps, out8, outrate, 0, 8)
					else
						STANDARDRESCALESTEREO(in16, inrate, insamps, out8, outrate, 0, 8)
				}
				else // downsample
				{
					if (resampstyle > 1)
						LINEARDOWNSCALESTEREO(in16, inrate, insamps, out8, outrate, 0, 8)
					else
						STANDARDRESCALESTEREO(in16, inrate, insamps, out8, outrate, 0, 8)
				}
			}
		}
	}
#if 0
	else if (outchannels == 1 && inchannels == 2)
	{
		if (inwidth == 1)
		{
			if (outwidth == 1)
			{
				if (inrate < outrate) // upsample
				{
					if (resampstyle)
						LINEARUPSCALESTEREOTOMONO(in8, inrate, insamps, out8, outrate, 0, 0)
					else
						STANDARDRESCALESTEREOTOMONO(in8, inrate, insamps, out8, outrate, 0, 0)
				}
				else // downsample
					STANDARDRESCALESTEREOTOMONO(in8, inrate, insamps, out8, outrate, 0, 0)
			}
			else
			{
				if (inrate == outrate) // quick convert
					QUICKCONVERTSTEREOTOMONO(in8, insamps, out16, 8, 0)
				else if (inrate < outrate) // upsample
				{
					if (resampstyle)
						LINEARUPSCALESTEREOTOMONO(in8, inrate, insamps, out16, outrate, 8, 0)
					else
						STANDARDRESCALESTEREOTOMONO(in8, inrate, insamps, out16, outrate, 8, 0)
				}
				else // downsample
					STANDARDRESCALESTEREOTOMONO(in8, inrate, insamps, out16, outrate, 8, 0)
			}
		}
		else // 16-bit
		{
			if (outwidth == 2)
			{
				if (inrate < outrate) // upsample
				{
					if (resampstyle)
						LINEARUPSCALESTEREOTOMONO(in16, inrate, insamps, out16, outrate, 0, 0)
					else
						STANDARDRESCALESTEREOTOMONO(in16, inrate, insamps, out16, outrate, 0, 0)
				}
				else // downsample
					STANDARDRESCALESTEREOTOMONO(in16, inrate, insamps, out16, outrate, 0, 0)
			}
			else
			{
				if (inrate == outrate) // quick convert
					QUICKCONVERTSTEREOTOMONO(in16, insamps, out8, 0, 8)
				else if (inrate < outrate) // upsample
				{
					if (resampstyle)
						LINEARUPSCALESTEREOTOMONO(in16, inrate, insamps, out8, outrate, 0, 8)
					else
						STANDARDRESCALESTEREOTOMONO(in16, inrate, insamps, out8, outrate, 0, 8)
				}
				else // downsample
					STANDARDRESCALESTEREOTOMONO(in16, inrate, insamps, out8, outrate, 0, 8)
			}
		}
	}
#endif
}






#define MAX_RAW_CACHE (1024 * 32) // have no idea which size it actually should be. this specifies the maximum buffer size. if it gets full, the entire thing is dumped in order to try to reduce latency (we have no time drifting or anything)

typedef struct
{
	qboolean inuse;
	int id;
	sfx_t sfx;
} streaming_t;

static void S_RawClearStream(streaming_t *s);

#define MAX_RAW_SOURCES (MAX_SCOREBOARD+1)

static streaming_t s_streamers[MAX_RAW_SOURCES] = {{0}};

/*static void S_RawClear(void)
{
	int i;
	streaming_t *s;

	for (s = s_streamers, i = 0; i < MAX_RAW_SOURCES; i++, s++)
	{
		S_RawClearStream(s);
	}

	memset(s_streamers, 0, sizeof(s_streamers));
}*/

// Stop playing particular stream and make it free.
static void S_RawClearStream(streaming_t *s)
{
	int i;
	sfxcache_t * currentcache;

	if (!s)
		return;

	// get current cache if any.
	currentcache = (sfxcache_t *)Cache_Check(&s->sfx.cache);
	if (currentcache)
	{
		currentcache->loopstart = -1; //stop mixing it
	}

	// remove link on sfx from the channels array.
	for (i = 0; i < total_channels; i++)
	{
		if (snd_channels[i].sfx == &s->sfx)
		{
			snd_channels[i].sfx = NULL;
			break;
		}
	}

	// free cache.
	if (s->sfx.cache.data)
		Cache_Free(&s->sfx.cache, false);

	// clear whole struct.
	memset(s, 0, sizeof(*s));
}

// Searching for free slot or re-use previous one with the same sourceid.
static streaming_t * S_RawGetFreeStream(int sourceid)
{
	int i;
	streaming_t *s, *free = NULL;

	for (s = s_streamers, i = 0; i < MAX_RAW_SOURCES; i++, s++)
	{
		if (!s->inuse)
		{
			if (!free)
			{
				free = s; // found free stream slot.
			}

			continue;
		}

		if (s->id == sourceid)
		{
			return s; // re-using slot.
		}
	}

	return free;
}

// Streaming audio.
// This is useful when there is one source, and the sound is to be played with no attenuation.
void S_RawAudio(int sourceid, byte *data, unsigned int speed, unsigned int samples, unsigned int channelsnum, unsigned int width, float volume)
{
	int i;
	int newsize;
	int prepadl;
	int spare;
	int outsamples;
	double speedfactor;
	sfxcache_t * currentcache;
	streaming_t * s;

	// search for free slot or re-use previous one with the same sourceid.
	s = S_RawGetFreeStream(sourceid);

	if (!s)
	{
		Con_DPrintf("No free audio streams or stream not found\n");
		return;
	}

	// empty data mean someone tell us to shut up particular slot.
	if (!data)
	{
		S_RawClearStream(s);
		return;
	}

	// attempting to add new stream
	if (!s->inuse)
	{
		sfxcache_t * newcache;

		// clear whole struct.
		memset(s, 0, sizeof(*s));
		// allocate cache.
		newsize = MAX_RAW_CACHE+sizeof(sfxcache_t);

		newcache = Cache_Alloc(&s->sfx.cache, newsize, "rawaudio");
		if (!newcache)
		{
			Con_DPrintf("Cache_Alloc failed\n");
			return;
		}

		s->inuse = true;
		s->id = sourceid;
		// strcpy(s->sfx.name, ""); // FIXME: probably we should put some specific tag name here?
		s->sfx.cache.data = newcache;
		newcache->speed = shm->speed;
		newcache->stereo = channelsnum-1;
		newcache->width = width;
		newcache->loopstart = -1;
		newcache->length = 0;
		newcache = NULL;

		// Con_Printf("Added new raw stream\n");
	}

	// get current cache if any.
	currentcache = (sfxcache_t *)Cache_Check(&s->sfx.cache);
	if (!currentcache)
	{
		Con_DPrintf("Cache_Check failed\n");
		S_RawClearStream(s);
		return;
	}

	if ( currentcache->speed != shm->speed
	|| currentcache->stereo != (int)channelsnum-1
	|| currentcache->width != (int)width)
	{
		currentcache->speed = shm->speed;
		currentcache->stereo = channelsnum-1;
		currentcache->width = width;
		// newcache->loopstart = -1;
		currentcache->length = 0;
		// Con_Printf("Restarting raw stream\n");
	}

	speedfactor = (double)speed/shm->speed;
	outsamples = samples/speedfactor;

	//prepadl is the length of data at the start of the sample which appears to be unused. its the amount of data that we can discard.
	prepadl = 0x7fffffff;
	for (i = 0; i < total_channels; i++)
	{
		if (snd_channels[i].sfx == &s->sfx)
		{
			if (prepadl > (snd_channels[i].pos/*>>PITCHSHIFT*/))
				prepadl = (snd_channels[i].pos/*>>PITCHSHIFT*/);
			break;
		}
	}

	if (prepadl == 0x7fffffff)
	{
		prepadl = 0;
		spare = 0;
		if (spare > shm->speed)
		{
			Con_DPrintf("Sacrificed raw sound stream\n");
			spare = 0; //too far out. sacrifice it all
		}
	}
	else
	{
		if (prepadl < 0)
			prepadl = 0;
		spare = currentcache->length - prepadl;
		if (spare < 0) //remaining samples since last time
			spare = 0;

		if (spare > shm->speed * 2) // more than 2 seconds of sound
		{
			Con_DPrintf("Sacrificed raw sound stream\n");
			spare = 0; //too far out. sacrifice it all
		}
	}

	newsize = (spare + outsamples) * (currentcache->stereo+1) * currentcache->width;
	if (newsize > MAX_RAW_CACHE)
	{	//this can happen quite often when our playback driver isn't playing sound due to the window not having focus.
		Con_DPrintf("VOIP stream overflowed\n");
		S_RawClearStream(s);
		return;
	}

	// move along spare/remaning samples in the begging of the buffer.
	memmove(currentcache->data,
	currentcache->data + prepadl * (currentcache->stereo+1) * currentcache->width,
	spare * (currentcache->stereo+1) * currentcache->width);

	currentcache->length = spare + outsamples;

	// resample.
	{
		short *outpos = (short *)(currentcache->data + spare * (currentcache->stereo+1) * currentcache->width);
		SND_ResampleStream(data,
				speed,
				width,
				channelsnum,
				samples,
				outpos,
				shm->speed,
				currentcache->width,
				currentcache->stereo+1,
				true
				);
	}

	currentcache->loopstart = -1;//currentcache->total_length;

	for (i = 0; i < total_channels; i++)
	{
		if (snd_channels[i].sfx == &s->sfx)
		{
#if 0
			// FIXME: qqshka: hrm, should it be just like this all the time??? I think it should.
			snd_channels[i].pos = 0;
			snd_channels[i].end = paintedtime + currentcache->total_length;
#else
			snd_channels[i].pos -= prepadl; // * channels[i].rate;
			snd_channels[i].end += outsamples;
			snd_channels[i].master_vol = (int) (volume * 255); // this should changed volume on alredy playing sound.

			if (snd_channels[i].end < paintedtime)
			{
				snd_channels[i].pos = 0;
				snd_channels[i].end = paintedtime + currentcache->length;
			}
#endif
			break;
		}
	}

	//this one wasn't playing, lets start it then.
	if (i == total_channels)
	{
		// Con_DPrintf("start sound\n");
		S_StartSound(sourceid+1, -2, &s->sfx, r_origin, 1, 0);
	}
}

/*****************************************************************************************************************************/
/*client/coding/decoding componant*/

//#define USE_SPEEX_CODEC
//#define USE_SPEEX_DSP

/*the client cvars*/
cvar_t cl_voip_send = {"cl_voip_send", "0"};	//0=off,1=voice activation,2=continuous
cvar_t cl_voip_test = {"cl_voip_test", "0"};
cvar_t cl_voip_vad_threshhold = {"cl_voip_vad_threshhold", "15"};
cvar_t cl_voip_vad_delay = {"cl_voip_vad_delay", "0.3"};
cvar_t cl_voip_capturingvol = {"cl_voip_capturingvol", "0.5", true};
cvar_t cl_voip_showmeter = {"cl_voip_showmeter", "1", true};

cvar_t cl_voip_play = {"cl_voip_play", "1", true};
cvar_t cl_voip_micamp = {"cl_voip_micamp", "2", true};
cvar_t cl_voip_ducking = {"cl_voip_ducking", "0.5", true};
cvar_t cl_voip_codec = {"cl_voip_codec", "", true};	//opus by default (quakespasm actually comes with a dll for that one)
cvar_t cl_voip_noisefilter = {"cl_voip_noisefilter", "1", true};
cvar_t cl_voip_autogain = {"cl_voip_autogain", "0", true};
cvar_t cl_voip_opus_bitrate = {"cl_voip_opus_bitrate", "3000", true};

#ifdef USE_SPEEX_CODEC
#include <speex/speex.h>
#endif
#ifdef USE_SPEEX_DSP
#include <speex/speex_preprocess.h>
#endif

#ifndef QDECL
#define QDECL	//__cdecl
#endif

enum
{
	VOIP_SPEEX_OLD	= 0,	//original supported codec (with needless padding [because I didn't understand speex's format] narrowband speex encoded+played at the wrong rate [because it was easier at the time])
	VOIP_RAW16		= 1,	//support is not recommended, but useful for testing. 16bit, 11khz, mono
	VOIP_OPUS		= 2,	//supposed to be better than speex.
	VOIP_SPEEX_NARROW = 3,	//narrowband speex. packed data.
	VOIP_SPEEX_WIDE = 4,	//wideband speex. packed data.
	VOIP_SPEEX_ULTRAWIDE = 5,//for wasteful people
	VOIP_PCMA		= 6,	//G711 is kinda shit, encoding audio at 8khz with funny truncation for 13bit to 8bit, but its quite simple, and smaller than raw pcm, as well as a well-known voip standard
	VOIP_PCMU		= 7,	//ulaw version of g711 (instead of alaw)


	VOIP_INVALID = 16	//not currently generating audio.
};
static struct
{
#ifdef USE_SPEEX_CODEC
	struct
	{
		qboolean inited;
		qboolean loaded;
		dllhandle_t *speexlib;

		SpeexBits encbits;
		SpeexBits decbits[MAX_SCOREBOARD];

		const SpeexMode *modenb;
		const SpeexMode *modewb;
	} speex;
#endif
#ifdef USE_SPEEX_DSP
	struct
	{
		qboolean inited;
		qboolean loaded;
		dllhandle_t *speexdsplib;

		SpeexPreprocessState *preproc;	//filter out noise
		int curframesize;
		int cursamplerate;
	} speexdsp;
#endif

	struct
	{
		qboolean inited[2];
		qboolean loaded[2];
		dllhandle_t *opuslib[2];
	} opus;

	unsigned char enccodec;
	void *encoder;
	unsigned int encframesize;
	unsigned int encsamplerate;
	int curbitrate;

	void *decoder[MAX_SCOREBOARD];
	unsigned char deccodec[MAX_SCOREBOARD];
	unsigned char decseq[MAX_SCOREBOARD];	/*sender's sequence, to detect+cover minor packetloss*/
	unsigned char decgen[MAX_SCOREBOARD];	/*last generation. if it changes, we flush speex to reset packet loss*/
	unsigned int decsamplerate[MAX_SCOREBOARD];
	unsigned int decframesize[MAX_SCOREBOARD];
	float lastspoke[MAX_SCOREBOARD];	/*time when they're no longer considered talking. if future, they're talking*/
	float lastspoke_any;

	unsigned char capturebuf[32768]; /*pending data*/
	unsigned int capturepos;/*amount of pending data*/
	unsigned int encsequence;/*the outgoing sequence count*/
	unsigned int enctimestamp;/*for rtp streaming*/
	unsigned int generation;/*incremented whenever capture is restarted*/
	qboolean wantsend;	/*set if we're capturing data to send*/
	float voiplevel;	/*your own voice level*/
	unsigned int dumps;	/*trigger a new generation thing after a bit*/
	unsigned int keeps;	/*for vad_delay*/

	snd_capture_driver_t *cdriver;/*capture driver's functions*/
	void *cdriverctx;	/*capture driver context*/

	qboolean voipsendbutton;
} s_voip;

//snd_capture_driver_t DSOUND_Capture;
//snd_capture_driver_t OSS_Capture;

#define OPUS_APPLICATION_VOIP				2048
#define OPUS_SET_BITRATE_REQUEST			4002
#define OPUS_RESET_STATE					4028
#ifdef OPUS_STATIC
#include <opus.h>
#define qopus_encoder_create	opus_encoder_create
#define qopus_encoder_destroy	opus_encoder_destroy
#define qopus_encoder_ctl		opus_encoder_ctl
#define qopus_encode			opus_encode
#define qopus_decoder_create	opus_decoder_create
#define qopus_decoder_destroy	opus_decoder_destroy
#define qopus_decoder_ctl		opus_decoder_ctl
#define qopus_decode			opus_decode
#else
#define opus_int32 int
#define opus_int16 short
#define OpusEncoder void
#define OpusDecoder void
static OpusEncoder *(QDECL *qopus_encoder_create)(opus_int32 Fs, int channels, int application, int *error);
static void (QDECL *qopus_encoder_destroy)(OpusEncoder *st);
static int (QDECL *qopus_encoder_ctl)(OpusEncoder *st, int request, ...);
static opus_int32 (QDECL *qopus_encode)(OpusEncoder *st, const opus_int16 *pcm, int frame_size, unsigned char *data, opus_int32 max_data_bytes);
static OpusDecoder *(QDECL *qopus_decoder_create)(opus_int32 Fs, int channels, int *error);
static void (QDECL *qopus_decoder_destroy)(OpusDecoder *st);
static int (QDECL *qopus_decoder_ctl)(OpusDecoder *st, int request, ...);
static int (QDECL *qopus_decode)(OpusDecoder *st, const unsigned char *data, opus_int32 len, opus_int16 *pcm, int frame_size, int decode_fec);
static dllfunction_t qopusencodefuncs[] =
{
	{(void*)&qopus_encoder_create,	"opus_encoder_create"},
	{(void*)&qopus_encoder_destroy,	"opus_encoder_destroy"},
	{(void*)&qopus_encoder_ctl,		"opus_encoder_ctl"},
	{(void*)&qopus_encode,			"opus_encode"},

	{NULL}
};
static dllfunction_t qopusdecodefuncs[] =
{
	{(void*)&qopus_decoder_create,	"opus_decoder_create"},
	{(void*)&qopus_decoder_destroy,	"opus_decoder_destroy"},
	{(void*)&qopus_decoder_ctl,		"opus_decoder_ctl"},
	{(void*)&qopus_decode,			"opus_decode"},

	{NULL}
};
#endif

#ifdef SPEEX_STATIC
#define qspeex_lib_get_mode speex_lib_get_mode
#define qspeex_bits_init speex_bits_init
#define qspeex_bits_reset speex_bits_reset
#define qspeex_bits_write speex_bits_write

#define qspeex_preprocess_state_init speex_preprocess_state_init
#define qspeex_preprocess_state_destroy speex_preprocess_state_destroy
#define qspeex_preprocess_ctl speex_preprocess_ctl
#define qspeex_preprocess_run speex_preprocess_run

#define qspeex_encoder_init speex_encoder_init
#define qspeex_encoder_destroy speex_encoder_destroy
#define qspeex_encoder_ctl speex_encoder_ctl
#define qspeex_encode_int speex_encode_int

#define qspeex_decoder_init speex_decoder_init
#define qspeex_decoder_destroy speex_decoder_destroy
#define qspeex_decode_int speex_decode_int
#define qspeex_bits_read_from speex_bits_read_from
#else
#ifdef USE_SPEEX_CODEC
static const SpeexMode *(QDECL *qspeex_lib_get_mode)(int mode);
static void (QDECL *qspeex_bits_init)(SpeexBits *bits);
static void (QDECL *qspeex_bits_reset)(SpeexBits *bits);
static int (QDECL *qspeex_bits_write)(SpeexBits *bits, char *bytes, int max_len);

static void * (QDECL *qspeex_encoder_init)(const SpeexMode *mode);
static int (QDECL *qspeex_encoder_ctl)(void *state, int request, void *ptr);
static int (QDECL *qspeex_encode_int)(void *state, spx_int16_t *in, SpeexBits *bits);

static void *(QDECL *qspeex_decoder_init)(const SpeexMode *mode);
static void (QDECL *qspeex_decoder_destroy)(void *state);
static int (QDECL *qspeex_decode_int)(void *state, SpeexBits *bits, spx_int16_t *out);
static void (QDECL *qspeex_bits_read_from)(SpeexBits *bits, char *bytes, int len);

static dllfunction_t qspeexfuncs[] =
{
	{(void*)&qspeex_lib_get_mode, "speex_lib_get_mode"},
	{(void*)&qspeex_bits_init, "speex_bits_init"},
	{(void*)&qspeex_bits_reset, "speex_bits_reset"},
	{(void*)&qspeex_bits_write, "speex_bits_write"},

	{(void*)&qspeex_encoder_init, "speex_encoder_init"},
	{(void*)&qspeex_encoder_ctl, "speex_encoder_ctl"},
	{(void*)&qspeex_encode_int, "speex_encode_int"},

	{(void*)&qspeex_decoder_init, "speex_decoder_init"},
	{(void*)&qspeex_decoder_destroy, "speex_decoder_destroy"},
	{(void*)&qspeex_decode_int, "speex_decode_int"},
	{(void*)&qspeex_bits_read_from, "speex_bits_read_from"},

	{NULL}
};
#endif

#ifdef USE_SPEEX_DSP
static SpeexPreprocessState *(QDECL *qspeex_preprocess_state_init)(int frame_size, int sampling_rate);
static void (QDECL *qspeex_preprocess_state_destroy)(SpeexPreprocessState *st);
static int (QDECL *qspeex_preprocess_ctl)(SpeexPreprocessState *st, int request, void *ptr);
static int (QDECL *qspeex_preprocess_run)(SpeexPreprocessState *st, spx_int16_t *x);

static dllfunction_t qspeexdspfuncs[] =
{
	{(void*)&qspeex_preprocess_state_init, "speex_preprocess_state_init"},
	{(void*)&qspeex_preprocess_state_destroy, "speex_preprocess_state_destroy"},
	{(void*)&qspeex_preprocess_ctl, "speex_preprocess_ctl"},
	{(void*)&qspeex_preprocess_run, "speex_preprocess_run"},

	{NULL}
};
#endif
#endif

#ifdef USE_SPEEX_DSP
static qboolean S_SpeexDSP_Init(void)
{
#ifndef SPEEX_STATIC
	if (s_voip.speexdsp.inited)
		return s_voip.speexdsp.loaded;
	s_voip.speexdsp.inited = true;

	
	s_voip.speexdsp.speexdsplib = Sys_LoadLibrary("libspeexdsp", qspeexdspfuncs);
	if (!s_voip.speexdsp.speexdsplib)
	{
		Con_Printf("libspeexdsp not found. Your mic may be noisy.\n");
		return false;
	}
#endif

	s_voip.speexdsp.loaded = true;
	return s_voip.speexdsp.loaded;
}
#endif

#ifdef USE_SPEEX_CODEC
static qboolean S_Speex_Init(void)
{
#ifndef SPEEX_STATIC
	if (s_voip.speex.inited)
		return s_voip.speex.loaded;
	s_voip.speex.inited = true;

	s_voip.speex.speexlib = Sys_LoadLibrary("libspeex", qspeexfuncs);
	if (!s_voip.speex.speexlib)
	{
		Con_Printf("libspeex not found. Voice chat is not available.\n");
		return false;
	}
#endif

	s_voip.speex.modenb = qspeex_lib_get_mode(SPEEX_MODEID_NB);
	s_voip.speex.modewb = qspeex_lib_get_mode(SPEEX_MODEID_WB);

	s_voip.speex.loaded = true;
	return s_voip.speex.loaded;
}
#endif

static qboolean S_Opus_Init(int encdec)
{
#ifndef OPUS_STATIC
#ifdef _WIN32
	char *modulename = "libopus-0.dll";
//	char *altmodulename = "libopus.dll";
#else
	char *modulename = "libopus.so.0";
//	char *altmodulename = "libopus.so";
#endif

	if (s_voip.opus.inited[encdec])
		return s_voip.opus.loaded[encdec];
	s_voip.opus.inited[encdec] = true;

	s_voip.opus.opuslib[encdec] = Sys_LoadLibrary(modulename, encdec?qopusencodefuncs:qopusdecodefuncs);
//	if (!s_voip.opus.opuslib[encdec] && encdec)
//		s_voip.opus.opuslib[encdec] = Sys_LoadLibrary(altmodulename, encdec?qopusencodefuncs:qopusdecodefuncs);
	if (!s_voip.opus.opuslib[encdec])
	{
		Con_Printf("%s or its exports not found. Opus voip %s is not available.\n", modulename, encdec?"transmission":"reception");
		return false;
	}
#endif

	s_voip.opus.loaded[encdec] = true;
	return s_voip.opus.loaded[encdec];
}

//g711 used to be patented, but those have since expired.
//there's two forms, a-law is generally considered better quality, u-law is a little simpler.
static size_t PCMA_Decode(short *out, unsigned char *in, size_t samples)
{
	size_t i = 0;
	for (i = 0; i < samples; i++)
	{
		unsigned char inv = in[i]^0x55;	//g711 alaw inverts every other bit
		int m = inv&0xf;
		int e = (inv&0x70)>>4;
		if (e)
			m = (((m)<<1)|0x21) << (e-1);
		else
			m = (((m)<<1)|1);
		if (inv & 0x80)
			out[i] = -m;
		else
			out[i] = m;
	}
	return i;
}
static size_t PCMA_Encode(unsigned char *out, size_t outsize, short *in, size_t samples)
{
	size_t i = 0;
	for (i = 0; i < samples; i++)
	{
		int o = in[i];
		unsigned char b;
		if (o < 0)
		{
			o = -o;
			b = 0x80;
		}
		else
			b = 0;

		if (o >= 0x0800)
			b |= ((o>>7)&0xf) | 0x70;
		else if (o >= 0x0400)
			b |= ((o>>6)&0xf) | 0x60;
		else if (o >= 0x0200)
			b |= ((o>>5)&0xf) | 0x50;
		else if (o >= 0x0100)
			b |= ((o>>4)&0xf) | 0x40;
		else if (o >= 0x0080)
			b |= ((o>>3)&0xf) | 0x30;
		else if (o >= 0x0040)
			b |= ((o>>2)&0xf) | 0x20;
		else if (o >= 0x0020)
			b |= ((o>>1)&0xf) | 0x10;
		else
			b |= ((o>>1)&0xf) | 0x00;
		out[i] = b^0x55;	//invert every-other bit.
	}

	return samples;
}
static size_t PCMU_Decode(short *out, unsigned char *in, size_t samples)
{
	size_t i = 0;
	for (i = 0; i < samples; i++)
	{
		unsigned char inv = in[i]^0xff;
		int m = (((inv&0xf)<<1)|0x21) << ((inv&0x70)>>4);
		m -= 33;
		if (inv & 0x80)
			out[i] = -m;
		else
			out[i] = m;
	}
	return i;
}
static size_t PCMU_Encode(unsigned char *out, size_t outsize, short *in, size_t samples)
{
	size_t i = 0;
	for (i = 0; i < samples; i++)
	{
		int o = in[i];
		unsigned char b;
		if (o < 0)
		{
			o = ~o;
			b = 0x80;
		}
		else
			b = 0;
		o+=33;

		if (o >= 0x1000)
			b |= ((o>>8)&0xf) | 0x70;
		else if (o >= 0x0800)
			b |= ((o>>7)&0xf) | 0x60;
		else if (o >= 0x0400)
			b |= ((o>>6)&0xf) | 0x50;
		else if (o >= 0x0200)
			b |= ((o>>5)&0xf) | 0x40;
		else if (o >= 0x0100)
			b |= ((o>>4)&0xf) | 0x30;
		else if (o >= 0x0080)
			b |= ((o>>3)&0xf) | 0x20;
		else if (o >= 0x0040)
			b |= ((o>>2)&0xf) | 0x10;
		else
			b |= ((o>>1)&0xf) | 0x00;
		out[i] = b^0xff;
	}

	return samples;
}

void S_Voip_Decode(unsigned int sender, unsigned int codec, unsigned int gen, unsigned char seq, unsigned int bytes, unsigned char *data)
{
	unsigned char *start;
	short decodebuf[8192];
	unsigned int decodesamps, len, drops;
	int r;

	if (sender >= MAX_SCOREBOARD)
		return;

	decodesamps = 0;
	drops = 0;
	start = data;

	s_voip.lastspoke[sender] = realtime + 0.5;
	if (s_voip.lastspoke[sender] > s_voip.lastspoke_any)
		s_voip.lastspoke_any = s_voip.lastspoke[sender];

	//if they re-started speaking, flush any old state to avoid things getting weirdly delayed and reset the codec properly.
	if (s_voip.decgen[sender] != gen || s_voip.deccodec[sender] != codec)
	{
		S_RawAudio(sender, NULL, s_voip.decsamplerate[sender], 0, 1, 2, 0);

		if (s_voip.deccodec[sender] != codec)
		{
			//make sure old state is closed properly.
			switch(s_voip.deccodec[sender])
			{
#ifdef USE_SPEEX_CODEC
			case VOIP_SPEEX_OLD:
			case VOIP_SPEEX_NARROW:
			case VOIP_SPEEX_WIDE:
			case VOIP_SPEEX_ULTRAWIDE:
				qspeex_decoder_destroy(s_voip.decoder[sender]);
				break;
#endif
			case VOIP_RAW16:
			case VOIP_PCMA:
			case VOIP_PCMU:
				break;
			case VOIP_OPUS:
				qopus_decoder_destroy(s_voip.decoder[sender]);
				break;
			}
			s_voip.decoder[sender] = NULL;
			s_voip.deccodec[sender] = VOIP_INVALID;
		}

		switch(codec)
		{
		default:	//codec not supported.
			return;
		case VOIP_RAW16:
			s_voip.decsamplerate[sender] = 11025;
			break;
		case VOIP_PCMA:
		case VOIP_PCMU:
			s_voip.decsamplerate[sender] = 8000;
			s_voip.decframesize[sender] = 8000/20;
			break;
#ifdef USE_SPEEX_CODEC
		case VOIP_SPEEX_OLD:
		case VOIP_SPEEX_NARROW:
		case VOIP_SPEEX_WIDE:
		case VOIP_SPEEX_ULTRAWIDE:
			{
				const SpeexMode *smode;
				if (!S_Speex_Init())
					return;	//speex not usable.
				if (codec == VOIP_SPEEX_NARROW)
				{
					s_voip.decsamplerate[sender] = 8000;
					s_voip.decframesize[sender] = 160;
					smode = s_voip.speex.modenb;
				}
				else if (codec == VOIP_SPEEX_WIDE)
				{
					s_voip.decsamplerate[sender] = 16000;
					s_voip.decframesize[sender] = 320;
					smode = s_voip.speex.modewb;
				}
				else if (codec == VOIP_SPEEX_ULTRAWIDE)
				{
					s_voip.decsamplerate[sender] = 32000;
					s_voip.decframesize[sender] = 640;
					smode = s_voip.speex.modeuwb;
				}
				else
				{
					s_voip.decsamplerate[sender] = 11025;
					s_voip.decframesize[sender] = 160;
					smode = s_voip.speex.modenb;
				}
				if (!s_voip.decoder[sender])
				{
					qspeex_bits_init(&s_voip.speex.decbits[sender]);
					qspeex_bits_reset(&s_voip.speex.decbits[sender]);
					s_voip.decoder[sender] = qspeex_decoder_init(smode);
					if (!s_voip.decoder[sender])
						return;
				}
				else
					qspeex_bits_reset(&s_voip.speex.decbits[sender]);
			}
			break;
#endif
		case VOIP_OPUS:
			if (!S_Opus_Init(false))
				return;

			//the lazy way to reset the codec!
			if (!s_voip.decoder[sender])
			{
				//opus outputs to 8, 12, 16, 24, or 48khz. pick whichever has least excess samples and resample to fit it.
				if (shm->speed <= 8000)
					s_voip.decsamplerate[sender] = 8000;
				else if (shm->speed <= 12000)
					s_voip.decsamplerate[sender] = 12000;
				else if (shm->speed <= 16000)
					s_voip.decsamplerate[sender] = 16000;
				else if (shm->speed <= 24000)
					s_voip.decsamplerate[sender] = 24000;
				else
					s_voip.decsamplerate[sender] = 48000;
				s_voip.decoder[sender] = qopus_decoder_create(s_voip.decsamplerate[sender], 1/*FIXME: support stereo where possible*/, NULL);
				if (!s_voip.decoder[sender])
					return;

				s_voip.decframesize[sender] = s_voip.decsamplerate[sender]/400;	//this is the maximum size in a single frame.
			}
			else
				qopus_decoder_ctl(s_voip.decoder[sender], OPUS_RESET_STATE);
			break;
		}
		s_voip.deccodec[sender] = codec;
		s_voip.decgen[sender] = gen;
		s_voip.decseq[sender] = seq;
	}


	//if there's packetloss, tell the decoder about the missing parts.
	//no infinite loops please.
	if ((unsigned)(seq - s_voip.decseq[sender]) > 10)
		s_voip.decseq[sender] = seq - 10;
	while(s_voip.decseq[sender] != seq)
	{
		if (decodesamps + s_voip.decframesize[sender] > sizeof(decodebuf)/sizeof(decodebuf[0]))
		{
			S_RawAudio(sender, (byte*)decodebuf, s_voip.decsamplerate[sender], decodesamps, 1, 2, cl_voip_play.value);
			decodesamps = 0;
		}
		switch(codec)
		{
		case VOIP_RAW16:
			break;	//just skip it.
#ifdef USE_SPEEX_CODEC
		case VOIP_SPEEX_OLD:
		case VOIP_SPEEX_NARROW:
		case VOIP_SPEEX_WIDE:
		case VOIP_SPEEX_ULTRAWIDE:
			qspeex_decode_int(s_voip.decoder[sender], NULL, decodebuf + decodesamps);
			decodesamps += s_voip.decframesize[sender];
			break;
#endif
		case VOIP_OPUS:
			r = qopus_decode(s_voip.decoder[sender], NULL, 0, decodebuf + decodesamps, q_min(s_voip.decframesize[sender], sizeof(decodebuf)/sizeof(decodebuf[0]) - decodesamps), false);
			if (r > 0)
				decodesamps += r;
			break;
		}
		s_voip.decseq[sender]++;
	}

	while (bytes > 0)
	{
		if (decodesamps + s_voip.decframesize[sender] >= sizeof(decodebuf)/sizeof(decodebuf[0]))
		{
			S_RawAudio(sender, (byte*)decodebuf, s_voip.decsamplerate[sender], decodesamps, 1, 2, cl_voip_play.value);
			decodesamps = 0;
		}
		switch(codec)
		{
		default:
			bytes = 0;
			break;
#ifdef USE_SPEEX_CODEC
		case VOIP_SPEEX_OLD:
		case VOIP_SPEEX_NARROW:
		case VOIP_SPEEX_WIDE:
		case VOIP_SPEEX_ULTRAWIDE:
			if (codec == VOIP_SPEEX_OLD)
			{	//older versions support only this, and require this extra bit.
				bytes--;
				len = *start++;
				if (bytes < len)
					break;
			}
			else
				len = bytes;
			qspeex_bits_read_from(&s_voip.speex.decbits[sender], start, len);
			bytes -= len;
			start += len;
			while (qspeex_decode_int(s_voip.decoder[sender], &s_voip.speex.decbits[sender], decodebuf + decodesamps) == 0)
			{
				decodesamps += s_voip.decframesize[sender];
				s_voip.decseq[sender]++;
				seq++;
				if (decodesamps + s_voip.decframesize[sender] >= sizeof(decodebuf)/sizeof(decodebuf[0]))
				{
					S_RawAudio(sender, (byte*)decodebuf, s_voip.decsamplerate[sender], decodesamps, 1, 2, cl_voip_play.value);
					decodesamps = 0;
				}
			}
			break;
#endif
		case VOIP_RAW16:
			len = q_min(bytes, sizeof(decodebuf)-(sizeof(decodebuf[0])*decodesamps));
			memcpy(decodebuf+decodesamps, start, len);
			decodesamps += len / sizeof(decodebuf[0]);
			s_voip.decseq[sender]++;
			bytes -= len;
			start += len;
			break;
		case VOIP_PCMA:
		case VOIP_PCMU:
			len = q_min(bytes, sizeof(decodebuf)-(sizeof(decodebuf[0])*decodesamps));
			if (len > s_voip.decframesize[sender]*2)
				len = s_voip.decframesize[sender]*2;
			if (codec == VOIP_PCMA)
				decodesamps += PCMA_Decode(decodebuf+decodesamps, start, len);
			else
				decodesamps += PCMU_Decode(decodebuf+decodesamps, start, len);
			s_voip.decseq[sender]++;
			bytes -= len;
			start += len;
			break;
		case VOIP_OPUS:
			len = bytes;
			if (decodesamps > 0)
			{
				S_RawAudio(sender, (byte*)decodebuf, s_voip.decsamplerate[sender], decodesamps, 1, 2, cl_voip_play.value);
				decodesamps = 0;
			}
			r = qopus_decode(s_voip.decoder[sender], start, len, decodebuf + decodesamps, sizeof(decodebuf)/sizeof(decodebuf[0]) - decodesamps, false);
			if (r > 0)
			{
				int frames = r / s_voip.decframesize[sender];
				decodesamps += r;
				s_voip.decseq[sender] = (s_voip.decseq[sender] + frames) & 0xff;
				seq = (seq+frames)&0xff;
			}
			else if (r < 0)
				Con_Printf("Opus decoding error %i\n", r);

			bytes -= len;
			start += len;
			break;
		}
	}

	if (drops)
		Con_DPrintf("%i dropped audio frames\n", drops);

	if (decodesamps > 0)
		S_RawAudio(sender, (byte*)decodebuf, s_voip.decsamplerate[sender], decodesamps, 1, 2, cl_voip_play.value);
}

void S_Voip_Parse(void)
{
	unsigned int sender;
	unsigned int bytes;
	unsigned char data[1024];
	unsigned char seq, gen;
	unsigned char codec;
	unsigned int i;

	sender = MSG_ReadByte();
	gen = MSG_ReadByte();
	codec = gen>>4;
	gen &= 0x0f;
	seq = MSG_ReadByte();
	bytes = MSG_ReadShort();
	if (bytes > sizeof(data) || cl_voip_play.value <= 0)
	{
		while(bytes-- > 0)
			MSG_ReadByte();
		return;
	}
	for(i = 0; i < bytes; i++)
		data[i] = MSG_ReadByte();

	sender %= MAX_SCOREBOARD;

	//if testing, don't get confused if the server is echoing voice too!
	if (cl_voip_test.value)
		if ((int)sender == cl.viewentity-1)	//FIXME: this isn't exactly reliable
			return;

	S_Voip_Decode(sender, codec, gen, seq, bytes, data);
}
static float S_Voip_Preprocess(short *start, unsigned int samples, float micamp)
{
	unsigned int i;
	float level = 0, f;
	unsigned int framesize = s_voip.encframesize;
	while(samples >= framesize)
	{
#ifdef USE_SPEEX_DSP
		if (s_voip.speexdsp.preproc)
			qspeex_preprocess_run(s_voip.speexdsp.preproc, start);
#endif
		for (i = 0; i < framesize; i++)
		{
			f = start[i] * micamp;
			start[i] = f;
			f = fabs(start[i]);
			level += f*f;
		}

		start += framesize;
		samples -= framesize;
	}
	return level;
}
static void S_Voip_Codecs_f(void)
{
	const char *codecname;
	unsigned int codecsupported;
	unsigned int i;
	for (i = 0; i < 16; i++)
	{
		switch(i)
		{
#ifdef USE_SPEEX_CODEC
		case VOIP_SPEEX_OLD:
			codecname = "Speex (Legacy)";
			codecsupported = S_Speex_Init()?3:0;
			break;
		case VOIP_SPEEX_NARROW:
			codecname = "Speex NarrowBand";
			codecsupported = S_Speex_Init()?3:0;
			break;
		case VOIP_SPEEX_WIDE:
			codecname = "Speex WideBand";
			codecsupported = S_Speex_Init()?7:0;
			break;
		case VOIP_SPEEX_ULTRAWIDE:
			codecname = "Speex UltraWideBand";
			codecsupported = S_Speex_Init()?7:0;
			break;
#endif
		case VOIP_RAW16:
			codecname = "Raw PCM";
			codecsupported = 7;
			break;
		case VOIP_PCMA:
			codecname = "G711 A-Law";
			codecsupported = 3;
			break;
		case VOIP_PCMU:
			codecname = "G711 U-Law";
			codecsupported = 3;
			break;
		case VOIP_OPUS:
			codecname = "Opus";
			codecsupported = (S_Opus_Init(false)?1:0)|(S_Opus_Init(true)?2:0);
			break;
		default:
			continue;
//			codecname = "Unknown Codec";
//			codecsupported = -1;
//			break;
		}
		Con_Printf("%i: %s", i, codecname);
		if (codecsupported == 7)
			Con_Printf(" (Not recommended)\n");	//both supported
		else if ((codecsupported & 3) == 3)
			Con_Printf(" (Available)\n");	//both supported
		else if (codecsupported & 1)
			Con_Printf(" (Decode Only)\n");
		else if (codecsupported & 2)
			Con_Printf(" (Encode Only)\n");
		else
			Con_Printf(" (Unavailable)\n");
	}
}
void S_Voip_EncodeStop(void)
{
	if (s_voip.cdriver)
	{
		if (s_voip.cdriverctx)
		{
			if (s_voip.wantsend)
			{
				s_voip.cdriver->Stop(s_voip.cdriverctx);
				s_voip.wantsend = false;
			}
			s_voip.cdriver->Shutdown(s_voip.cdriverctx);
			s_voip.cdriverctx = NULL;
		}
		s_voip.cdriver = NULL;
	}
	switch(s_voip.enccodec)
	{
	case VOIP_SPEEX_OLD:
	case VOIP_SPEEX_NARROW:
	case VOIP_SPEEX_WIDE:
	case VOIP_SPEEX_ULTRAWIDE:
		break;
	case VOIP_OPUS:
		qopus_encoder_destroy(s_voip.encoder);
		break;
	}
	s_voip.encoder = NULL;
	s_voip.enccodec = VOIP_INVALID;
	s_voip.voiplevel = -1;
}
void S_Voip_Transmit(unsigned char clc, sizebuf_t *buf)
{
	unsigned char outbuf[8192];
	unsigned int outpos;//in bytes
	unsigned int encpos;//in bytes
	short *start;
	unsigned int initseq;//in frames
	unsigned int samps;
	float level;
	size_t len;
	float micamp = cl_voip_micamp.value;
	int voipsendenable = true;
#ifdef USE_SPEEX_DSP
	static qboolean cl_voip_noisefilter_old = 0;
	static qboolean cl_voip_autogain_old = 0;
#endif

	int voipcodec;

	if (buf)
	{
		/*if you're sending sound, you should be prepared to accept others yelling at you to shut up*/
		if (cl_voip_play.value <= 0)
			voipsendenable = false;
		else if (!(cl.protocol_pext2 & PEXT2_VOICECHAT))
		{
			buf = NULL;
			voipsendenable = cl_voip_test.value;
		}
	}
	else
		voipsendenable = cl_voip_test.value;

	if (!voipsendenable)
	{	//if we're not allowing any voice, stop and give up now.
		S_Voip_EncodeStop();
		return;
	}

	voipsendenable = cl_voip_send.value>0 || s_voip.voipsendbutton;

	voicevolumescale = s_voip.lastspoke_any > realtime?cl_voip_ducking.value:1;

	voipcodec = cl_voip_codec.value;
	if ((voipsendenable || s_voip.cdriver) && !*cl_voip_codec.string)
	{	//empty cvar = pick a default that should work...
		if (S_Opus_Init(true))
			voipcodec = VOIP_OPUS;
#ifdef USE_SPEEX_CODEC
		else if (S_Speex_Init())
			voipcodec = VOIP_SPEEX_NARROW;
#endif
		else
			voipcodec = VOIP_PCMA;
	}

	//if the codec has changed, shut down the old encoder context
	if (voipcodec != s_voip.enccodec && s_voip.cdriver)
		S_Voip_EncodeStop();

	//create a new encoder context if we're not got one.
	if (!s_voip.cdriver && voipsendenable)
	{
		s_voip.voiplevel = -1;

		/*Add new drivers in order of priority*/
#ifdef USE_DSOUND_CAPTURE
		if (!s_voip.cdriver || !s_voip.cdriver->Init)
			s_voip.cdriver = &DSOUND_Capture;
#endif
#ifdef USE_SDL_CAPTURE
		if (!s_voip.cdriver || !s_voip.cdriver->Init)
			s_voip.cdriver = &SDL_Capture;
#endif

		/*no way to capture audio, give up*/
		if (!s_voip.cdriver || !s_voip.cdriver->Init)
			return;

		/*see if we can init our encoding codec...*/
		switch(voipcodec)
		{
#ifdef USE_SPEEX_CODEC
		case VOIP_SPEEX_OLD:
		case VOIP_SPEEX_NARROW:
		case VOIP_SPEEX_WIDE:
		case VOIP_SPEEX_ULTRAWIDE:
			{
				const SpeexMode *smode;
				if (!S_Speex_Init())
				{
					Con_Printf("Unable to use speex codec - not installed\n");
					s_voip.cdriver = NULL;
					return;
				}

				if (voipcodec == VOIP_SPEEX_ULTRAWIDE)
					smode = s_voip.speex.modeuwb;
				else if (voipcodec == VOIP_SPEEX_WIDE)
					smode = s_voip.speex.modewb;
				else
					smode = s_voip.speex.modenb;
				qspeex_bits_init(&s_voip.speex.encbits);
				qspeex_bits_reset(&s_voip.speex.encbits);
				s_voip.encoder = qspeex_encoder_init(smode);
				if (!s_voip.encoder)
					return;
				qspeex_encoder_ctl(s_voip.encoder, SPEEX_GET_FRAME_SIZE, &s_voip.encframesize);
				qspeex_encoder_ctl(s_voip.encoder, SPEEX_GET_SAMPLING_RATE, &s_voip.encsamplerate);
				if (voipcodec == VOIP_SPEEX_NARROW)
					s_voip.encsamplerate = 8000;
				else if (voipcodec == VOIP_SPEEX_WIDE)
					s_voip.encsamplerate = 16000;
				else if (voipcodec == VOIP_SPEEX_ULTRAWIDE)
					s_voip.encsamplerate = 32000;
				else
					s_voip.encsamplerate = 11025;
				qspeex_encoder_ctl(s_voip.encoder, SPEEX_SET_SAMPLING_RATE, &s_voip.encsamplerate);
			}
			break;
#endif
		case VOIP_RAW16:
			s_voip.encsamplerate = 11025;
			s_voip.encframesize = 256;
			break;
		case VOIP_PCMA:
		case VOIP_PCMU:
			s_voip.encsamplerate = 8000;
			s_voip.encframesize = 8000/20;
			break;
		case VOIP_OPUS:
			if (!S_Opus_Init(true))
			{
				Con_Printf("Unable to use opus codec - not installed\n");
				s_voip.cdriver = NULL;
				return;
			}

			//use whatever is convienient.
			s_voip.encsamplerate = 48000;
			s_voip.encframesize = s_voip.encsamplerate / 400;	//2.5ms frames, at a minimum.
			s_voip.encoder = qopus_encoder_create(s_voip.encsamplerate, 1, OPUS_APPLICATION_VOIP, NULL);
			if (!s_voip.encoder)
				return;

			s_voip.curbitrate = 0;

//			opus_encoder_ctl(enc, OPUS_SET_BITRATE(bitrate_bps));
//			opus_encoder_ctl(enc, OPUS_SET_BANDWIDTH(OPUS_BANDWIDTH_NARROWBAND));
//			opus_encoder_ctl(enc, OPUS_SET_VBR(use_vbr));
//			opus_encoder_ctl(enc, OPUS_SET_VBR_CONSTRAINT(cvbr));
//			opus_encoder_ctl(enc, OPUS_SET_COMPLEXITY(complexity));
//			opus_encoder_ctl(enc, OPUS_SET_INBAND_FEC(use_inbandfec));
//			opus_encoder_ctl(enc, OPUS_SET_FORCE_CHANNELS(forcechannels));
//			opus_encoder_ctl(enc, OPUS_SET_DTX(use_dtx));	//FIXME: we should probably use this one, as we're sending network packets regardless.
//			opus_encoder_ctl(enc, OPUS_SET_PACKET_LOSS_PERC(packet_loss_perc));

//			opus_encoder_ctl(enc, OPUS_GET_LOOKAHEAD(&skip));
//			opus_encoder_ctl(enc, OPUS_SET_LSB_DEPTH(16));


			break;
		default:
			Con_Printf("Unable to use that codec - not implemented\n");
			//can't start up other coedcs, cos we're too lame.
			return;
		}
		s_voip.enccodec = voipcodec;

		s_voip.cdriverctx = s_voip.cdriver->Init(s_voip.encsamplerate);

		if (!s_voip.cdriverctx)
			Con_Printf("No microphone detected\n");
	}

	/*couldn't init a driver?*/
	if (!s_voip.cdriverctx)
	{
		s_voip.voiplevel = -1;
		return;
	}

	if (!voipsendenable && s_voip.wantsend)
	{
		s_voip.wantsend = false;
		s_voip.capturepos += s_voip.cdriver->Update(s_voip.cdriverctx, (unsigned char*)s_voip.capturebuf + s_voip.capturepos, 1, sizeof(s_voip.capturebuf) - s_voip.capturepos);
		s_voip.cdriver->Stop(s_voip.cdriverctx);
		/*note: we still grab audio to flush everything that was captured while it was active*/
	}
	else if (voipsendenable && !s_voip.wantsend)
	{
		s_voip.wantsend = true;
		if (!s_voip.capturepos)
		{	/*if we were actually still sending, it was probably only off for a single frame, in which case don't reset it*/
			s_voip.dumps = 0;
			s_voip.generation++;
			s_voip.encsequence = 0;

			//reset codecs so they start with a clean slate when new audio blocks are generated.
			switch(s_voip.enccodec)
			{
#ifdef USE_SPEEX_CODEC
			case VOIP_SPEEX_OLD:
			case VOIP_SPEEX_NARROW:
			case VOIP_SPEEX_WIDE:
				qspeex_bits_reset(&s_voip.speex.encbits);
				break;
#endif
			case VOIP_RAW16:
				break;
			case VOIP_PCMA:
			case VOIP_PCMU:
				break;
			case VOIP_OPUS:
				qopus_encoder_ctl(s_voip.encoder, OPUS_RESET_STATE);
				break;
			}
		}
		else
		{
			s_voip.capturepos += s_voip.cdriver->Update(s_voip.cdriverctx, (unsigned char*)s_voip.capturebuf + s_voip.capturepos, 1, sizeof(s_voip.capturebuf) - s_voip.capturepos);
		}
		s_voip.cdriver->Start(s_voip.cdriverctx);
	}

	if (s_voip.wantsend)
		voicevolumescale = q_min(voicevolumescale, cl_voip_capturingvol.value);

	s_voip.capturepos += s_voip.cdriver->Update(s_voip.cdriverctx, (unsigned char*)s_voip.capturebuf + s_voip.capturepos, s_voip.encframesize*2, sizeof(s_voip.capturebuf) - s_voip.capturepos);

	if (!s_voip.wantsend && s_voip.capturepos < s_voip.encframesize*2)
	{
		s_voip.voiplevel = -1;
		s_voip.capturepos = 0;
		return;
	}

	initseq = s_voip.encsequence;
	level = 0;
	samps=0;
	//*2 for 16bit audio input.
	for (encpos = 0, outpos = 0; s_voip.capturepos-encpos >= s_voip.encframesize*2 && sizeof(outbuf)-outpos > 64; )
	{
		start = (short*)(s_voip.capturebuf + encpos);

#ifdef USE_SPEEX_DSP
		if (cl_voip_noisefilter.value || cl_voip_autogain.value)
		{
			if (!s_voip.speexdsp.preproc || cl_voip_noisefilter.value != cl_voip_noisefilter_old || cl_voip_autogain.value != cl_voip_autogain_old || s_voip.speexdsp.curframesize != s_voip.encframesize || s_voip.speexdsp.cursamplerate != s_voip.encsamplerate)
			{
				cl_voip_noisefilter_old = cl_voip_noisefilter.value;
				cl_voip_autogain_old = cl_voip_autogain.value;
				if (s_voip.speexdsp.preproc)
					qspeex_preprocess_state_destroy(s_voip.speexdsp.preproc);
				s_voip.speexdsp.preproc = NULL;
				if (S_SpeexDSP_Init())
				{
					int i;
					s_voip.speexdsp.preproc = qspeex_preprocess_state_init(s_voip.encframesize, s_voip.encsamplerate);
					i = cl_voip_noisefilter.value;
					qspeex_preprocess_ctl(s_voip.speexdsp.preproc, SPEEX_PREPROCESS_SET_DENOISE, &i);
					i = cl_voip_autogain.value;
					qspeex_preprocess_ctl(s_voip.speexdsp.preproc, SPEEX_PREPROCESS_SET_AGC, &i);

					s_voip.speexdsp.curframesize = s_voip.encframesize;
					s_voip.speexdsp.cursamplerate = s_voip.encsamplerate;
				}
			}
		}
		else if (s_voip.speexdsp.preproc)
		{
			qspeex_preprocess_state_destroy(s_voip.speexdsp.preproc);
			s_voip.speexdsp.preproc = NULL;
		}
#endif

		switch(s_voip.enccodec)
		{
#ifdef USE_SPEEX_CODEC
		case VOIP_SPEEX_OLD:
			level += S_Voip_Preprocess(start, s_voip.encframesize, micamp);
			qspeex_bits_reset(&s_voip.speex.encbits);
			qspeex_encode_int(s_voip.encoder, start, &s_voip.speex.encbits);
			len = qspeex_bits_write(&s_voip.speex.encbits, outbuf+(outpos+1), sizeof(outbuf) - (outpos+1));
			if (len < 0 || len > 255)
				len = 0;
			outbuf[outpos] = len;
			outpos += 1+len;
			s_voip.encsequence++;
			samps+=s_voip.encframesize;
			encpos += s_voip.encframesize*2;
			break;
		case VOIP_SPEEX_NARROW:
		case VOIP_SPEEX_WIDE:
		case VOIP_SPEEX_ULTRAWIDE:
			qspeex_bits_reset(&s_voip.speex.encbits);
			for (; s_voip.capturepos-encpos >= s_voip.encframesize*2 && sizeof(outbuf)-outpos > 64; )
			{
				start = (short*)(s_voip.capturebuf + encpos);
				level += S_Voip_Preprocess(start, s_voip.encframesize, micamp);
				qspeex_encode_int(s_voip.encoder, start, &s_voip.speex.encbits);
				s_voip.encsequence++;
				samps+=s_voip.encframesize;
				encpos += s_voip.encframesize*2;
			}
			len = qspeex_bits_write(&s_voip.speex.encbits, outbuf+outpos, sizeof(outbuf) - outpos);
			outpos += len;
			break;
#endif
		case VOIP_RAW16:
			len = s_voip.capturepos-encpos;	//amount of data to be eaten in this frame
			len = q_min((unsigned int)len, sizeof(outbuf)-outpos);
			len &= ~((s_voip.encframesize*2)-1);
			level += S_Voip_Preprocess(start, len/2, micamp);
			memcpy(outbuf+outpos, start, len);	//'encode'
			outpos += len;			//bytes written to output
			encpos += len;			//number of bytes consumed

			s_voip.encsequence++;	//increment number of packets, for packetloss detection.
			samps+=len / 2;	//number of samplepairs eaten in this packet. for stats.
			break;
		case VOIP_PCMA:
		case VOIP_PCMU:
			len = s_voip.capturepos-encpos;	//amount of data to be eaten in this frame
			len = q_min(len, sizeof(outbuf)-outpos);
			len = q_min(len, s_voip.encframesize*2);
			level += S_Voip_Preprocess(start, len/2, micamp);
			if (s_voip.enccodec == VOIP_PCMA)
				outpos += PCMA_Encode(outbuf+outpos, sizeof(outbuf)-outpos, start, len/2);
			else
				outpos += PCMU_Encode(outbuf+outpos, sizeof(outbuf)-outpos, start, len/2);
			encpos += len;			//number of bytes consumed
			s_voip.encsequence++;	//increment number of packets, for packetloss detection.
			samps+=len / 2;	//number of samplepairs eaten in this packet. for stats.
			break;
		case VOIP_OPUS:
			{
				//opus rtp only supports/allows a single chunk in each packet.
				//luckily it allows power-of-two frames
				int frames;
				int nrate;
				//densely pack the frames.
				start = (short*)(s_voip.capturebuf + encpos);
				frames = (s_voip.capturepos-encpos)/2;
				if (frames >= 2880)
					frames = 2880;
				else if (frames >= 1920)
					frames = 1920;
				else if (frames >= 960)
					frames = 960;
				else if (cls.demorecording)
					break;	//don't generate small frames if we're recording a demo, due to overheads.
				else if (frames >= 480)
					frames = 480;
				else if (frames >= 240)
					frames = 240;
				else if (frames >= 120)
					frames = 120;
				else
				{
					Con_Printf("invalid Opus frame size\n");
					frames = 0;
				}

				nrate = cl_voip_opus_bitrate.value;
				if (nrate != s_voip.curbitrate)
				{
					s_voip.curbitrate = nrate;
					if (nrate == 0)
						nrate = -1000;
					else
					{	//"Rates from 500 to 512000 bits per second are meaningful"
						nrate = q_max(512, nrate);
						nrate = q_min(nrate, 512000);
					}
					qopus_encoder_ctl(s_voip.encoder, OPUS_SET_BITRATE_REQUEST, (int)nrate);
				}
				//fixme: might want to add an option for complexity too. maybe others.

				level += S_Voip_Preprocess(start, frames, micamp);
				len = qopus_encode(s_voip.encoder, start, frames, outbuf+outpos, sizeof(outbuf) - outpos);
				if (len >= 0)
				{	//FIXME: "If the return value is 2 bytes or less, then the packet does not need to be transmitted (DTX)."
					s_voip.encsequence += frames / s_voip.encframesize;
					outpos += len;
					samps+=frames;
					encpos += frames*2;
				}
				else
				{
					Con_Printf("Opus encoding error: %u\n", (unsigned int)len);
					encpos = s_voip.capturepos;
				}
			}
			break;
		default:
			outbuf[outpos] = 0;
			break;
		}

		if (s_voip.enccodec == VOIP_OPUS)
			break;	//one per frame
	}
	if (samps)
	{
		float nl;
		s_voip.enctimestamp += samps;
		nl = (3000*level) / (32767.0f*32767*samps);
		s_voip.voiplevel = (s_voip.voiplevel*7 + nl)/8;
		if (s_voip.voiplevel < cl_voip_vad_threshhold.value && !((int)cl_voip_send.value & 6) && !s_voip.voipsendbutton)
		{
			/*try and dump it, it was too quiet, and they're not pressing +voip*/
			if (s_voip.keeps > samps)
			{
				/*but not instantly*/
				s_voip.keeps -= samps;
			}
			else
			{
				outpos = 0;
				s_voip.dumps += samps;
				s_voip.keeps = 0;
			}
		}
		else
			s_voip.keeps = s_voip.encsamplerate * cl_voip_vad_delay.value;
		if (outpos)
		{
			if (s_voip.dumps > s_voip.encsamplerate/4)
				s_voip.generation++;
			s_voip.dumps = 0;
		}
	}

	if (outpos)
	{
		int localplayeridx = cl.viewentity-1;	//FIXME: this is not reliable
		if (cl_voip_send.value != 4)
		{
			extern cvar_t sv_voip_echo;
			if (buf && (unsigned int)(buf->maxsize - buf->cursize) >= outpos+5)
			{
				MSG_WriteByte(buf, clc);
				MSG_WriteByte(buf, (s_voip.enccodec<<4) | (s_voip.generation & 0x0f)); /*gonna leave that nibble clear here... in this version, the client will ignore packets with those bits set. can use them for codec or something*/
				MSG_WriteByte(buf, initseq);
				MSG_WriteShort(buf, outpos);
				SZ_Write(buf, outbuf, outpos);
			}
			if (cls.demorecording && (!sv.active || !sv_voip_echo.value))	//voice is not (normally) echoed by the server, which means that you won't hear yourself speak. which is unfortunate when it comes to demos.
			{	//the small size of various voip packets means that the 12 bytes of angles overhead is going to give a noticable size overhead...
				byte b;
				unsigned short s;
				int i = LittleLong (5+outpos);
				fwrite (&i, 4, 1, cls.demofile);
				for (i = 0; i < 3; i++)
				{
					float f = LittleFloat (cl.viewangles[i]);
					fwrite (&f, 4, 1, cls.demofile);
				}
				b = clc;
				fwrite (&b, 1, 1, cls.demofile);
				b = (s_voip.enccodec<<4) | (s_voip.generation & 0x0f);
				fwrite (&b, 1, 1, cls.demofile);
				b = initseq;
				fwrite (&b, 1, 1, cls.demofile);
				s = outpos;
				fwrite (&s, 2, 1, cls.demofile);
				fwrite (outbuf, outpos, 1, cls.demofile);
			}
		}

		if (localplayeridx < MAX_SCOREBOARD)
		{
			if (cl_voip_test.value)
				S_Voip_Decode(localplayeridx, s_voip.enccodec, s_voip.generation & 0x0f, initseq, outpos, outbuf);

			//update our own lastspoke, so queries shows that we're speaking when we're speaking in a generic way, even if we can't hear ourselves.
			//but don't update general lastspoke, so ducking applies only when others speak. use capturingvol for yourself. they're more explicit that way.
			s_voip.lastspoke[localplayeridx] = realtime + 0.5;
		}
	}

	/*remove sent data*/
	if (encpos)
	{
		memmove(s_voip.capturebuf, s_voip.capturebuf + encpos, s_voip.capturepos-encpos);
		s_voip.capturepos -= encpos;
	}
}
void S_Voip_Ignore(unsigned int slot, qboolean ignore)
{
	MSG_WriteByte (&cls.message, clc_stringcmd);
	MSG_WriteString (&cls.message, va("vignore %i %i", slot, ignore));
}
static void S_Voip_Enable_f(void)
{
	s_voip.voipsendbutton = true;
}
static void S_Voip_Disable_f(void)
{
	s_voip.voipsendbutton = false;
}
static void S_Voip_f(void)
{
	if (!strcmp(Cmd_Argv(1), "maxgain"))
	{
#ifdef USE_SPEEX_DSP
		int i = atoi(Cmd_Argv(2));
		if (s_voip.speexdsp.preproc)
			qspeex_preprocess_ctl(s_voip.speexdsp.preproc, SPEEX_PREPROCESS_SET_AGC_MAX_GAIN, &i);
#endif
	}
}
static void S_Voip_Play_Callback(cvar_t *var, char *oldval)
{
	if (cl.protocol_pext2 & PEXT2_VOICECHAT)
	{
		MSG_WriteByte (&cls.message, clc_stringcmd);
		if (var->value > 0)
			MSG_WriteString (&cls.message, va("unmuteall"));
		else
			MSG_WriteString (&cls.message, va("muteall"));
	}
}
void S_Voip_MapChange(void)
{
	s_voip.voipsendbutton = false;	//release +voip on each map change, because people that leave it enabled the whole time are annoying cunts (buttons get stuck sometimes).
	S_Voip_Play_Callback(&cl_voip_play, "");
}
int S_Voip_Loudness(qboolean ignorevad)
{
	if (s_voip.voiplevel > 100)
		return 100;
	if (!s_voip.cdriverctx || (!ignorevad && s_voip.dumps))
		return -1;
	return s_voip.voiplevel;
}
qboolean S_Voip_Speaking(unsigned int plno)
{
	if (plno >= MAX_SCOREBOARD)
		return false;
	return s_voip.lastspoke[plno] > realtime;
}

void S_Voip_Init(void)
{
	int i;
	for (i = 0; i < MAX_SCOREBOARD; i++)
		s_voip.deccodec[i] = VOIP_INVALID;
	s_voip.enccodec = VOIP_INVALID;

	Cvar_RegisterVariable(&cl_voip_send);
	Cvar_RegisterVariable(&cl_voip_vad_threshhold);
	Cvar_RegisterVariable(&cl_voip_vad_delay);
	Cvar_RegisterVariable(&cl_voip_capturingvol);
	Cvar_RegisterVariable(&cl_voip_showmeter);
	Cvar_RegisterVariable(&cl_voip_play);
	Cvar_RegisterVariable(&cl_voip_test);
	Cvar_RegisterVariable(&cl_voip_ducking);
	Cvar_RegisterVariable(&cl_voip_micamp);
#ifndef FORCE_ENCODER
	Cvar_RegisterVariable(&cl_voip_codec);
#endif
	Cvar_RegisterVariable(&cl_voip_opus_bitrate);
	Cvar_RegisterVariable(&cl_voip_noisefilter);
	Cvar_RegisterVariable(&cl_voip_autogain);
	Cmd_AddCommand("cl_voip_codecs", S_Voip_Codecs_f);
	Cmd_AddCommand("+voip", S_Voip_Enable_f);
	Cmd_AddCommand("-voip", S_Voip_Disable_f);
	Cmd_AddCommand("voip", S_Voip_f);
}


/*****************************************************************************************************************************/
/*server componant*/

cvar_t sv_voip = {"sv_voip", "1"};
cvar_t sv_voip_echo = {"sv_voip_echo", "0"};


/*
Pivicy issues:
By sending voice chat to a server, you are unsure who might be listening.
Server could be changed to record voice.
Voice will be saved in any demos made of the game.
You're never quite sure if anyone might join the server and your team before you finish saying a sentance.
You run the risk of sounds around you being recorded by quake, including but not limited to: TV channels, loved ones, phones, YouTube videos featuring certain moans.
Default on non-team games is to broadcast.
*/

#define VOICE_RING_SIZE 512 /*POT*/
struct
{
	struct voice_ring_s
	{
			unsigned int sender;
			unsigned char receiver[MAX_SCOREBOARD/8];
			unsigned char gen;
			unsigned char seq;
			unsigned int datalen;
			unsigned char data[1024];
	} ring[VOICE_RING_SIZE];
	unsigned int write;
} voice;
void SV_VoiceReadPacket(client_t *client)
{
	unsigned int vt = client->voip.target;
	unsigned int j;
	struct voice_ring_s *ring;
	unsigned short bytes;
	client_t *cl;
	unsigned char gen = MSG_ReadByte();
	unsigned char seq = MSG_ReadByte();
	/*read the data from the client*/
	bytes = MSG_ReadShort();
	ring = &voice.ring[voice.write & (VOICE_RING_SIZE-1)];
	if (bytes > sizeof(ring->data) || !sv_voip.value)
	{
		while (bytes-- > 0)
			MSG_ReadByte();
		return;
	}
	else
	{
		voice.write++;
		for (j = 0; j < bytes; j++)
			ring->data[j] = MSG_ReadByte();
	}

	ring->datalen = bytes;
	ring->sender = client - svs.clients;
	ring->gen = gen;
	ring->seq = seq;

	/*broadcast it its to their team, and its not teamplay*/
	if (vt == VT_TEAM && !teamplay.value)
		vt = VT_ALL;

	/*figure out which team members are meant to receive it*/
	for (j = 0; j < MAX_SCOREBOARD/8; j++)
		ring->receiver[j] = 0;
	for (j = 0, cl = svs.clients; j < (unsigned int)svs.maxclients; j++, cl++)
	{
		if (!cl->spawned || !cl->active)
			continue;

		if (vt == VT_TEAM)
		{
			if ((cl->colors & 0xf) == (client->colors & 0xf))
				continue;	// on different teams
		}
		else if (vt == VT_NONMUTED)
		{
			if (client->voip.mute[j>>3] & (1<<(j&3)))
				continue;
		}
		else if (vt >= VT_PLAYERSLOT0)
		{
			if (j != vt - VT_PLAYERSLOT0)
				continue;
		}

		ring->receiver[j>>3] |= 1<<(j&3);
	}
}
void SV_VoiceInitClient(client_t *client)
{
	client->voip.target = VT_TEAM;
	client->voip.active = false;
	client->voip.read = voice.write;
	memset(client->voip.mute, 0, sizeof(client->voip.mute));
}
void SV_VoiceSendPacket(client_t *client, sizebuf_t *buf)
{
	unsigned int clno;
	qboolean send;
	struct voice_ring_s *ring;

	clno = client - svs.clients;

	if (!(client->protocol_pext2 & PEXT2_VOICECHAT))
		return;
	if (!client->voip.active)
	{
		client->voip.read = voice.write;
		return;
	}

	while(client->voip.read < voice.write)
	{
		/*they might be too far behind*/
		if (client->voip.read+VOICE_RING_SIZE < voice.write)
			client->voip.read = voice.write - VOICE_RING_SIZE;

		ring = &voice.ring[(client->voip.read) & (VOICE_RING_SIZE-1)];

		/*figure out if it was for us*/
		send = false;
		if (ring->receiver[clno>>3] & (1<<(clno&3)))
			send = true;

		if (client->voip.mute[ring->sender>>3] & (1<<(ring->sender&3)))
			send = false;

		if (ring->sender == clno && !sv_voip_echo.value)
			send = false;

		if (send)
		{
			if (buf->maxsize - buf->cursize < (int)ring->datalen+5)
				break;
			MSG_WriteByte(buf, svcfte_voicechat);
			MSG_WriteByte(buf, ring->sender);
			MSG_WriteByte(buf, ring->gen);
			MSG_WriteByte(buf, ring->seq);
			MSG_WriteShort(buf, ring->datalen);
			SZ_Write(buf, ring->data, ring->datalen);
		}
		client->voip.read++;
	}
}

static void SV_Voice_Ignore_f(void)
{
	if (cmd_source == src_command)
	{
		if (cls.state == ca_connected)
			Cmd_ForwardToServer ();
	}
	else
	{
		unsigned int other;
		int type = 0;

		if (Cmd_Argc() < 2)
		{
			/*only a name = toggle*/
			type = 0;
		}
		else
		{
			/*mute if 1, unmute if 0*/
			if (atoi(Cmd_Argv(2)))
				type = 1;
			else
				type = -1;
		}
		other = atoi(Cmd_Argv(1));
		if (other >= MAX_SCOREBOARD)
			return;

		switch(type)
		{
		case -1:
			host_client->voip.mute[other>>3] &= ~(1<<(other&3));
			break;
		case 0:	
			host_client->voip.mute[other>>3] ^= (1<<(other&3));
			break;
		case 1:
			host_client->voip.mute[other>>3] |= (1<<(other&3));
		}
	}
}
static void SV_Voice_Target_f(void)
{
	if (cmd_source == src_command)
	{
		if (cls.state == ca_connected)
			Cmd_ForwardToServer ();
	}
	else
	{
		unsigned int other;
		const char *t = Cmd_Argv(1);
		if (!strcmp(t, "team"))
			host_client->voip.target = VT_TEAM;
		else if (!strcmp(t, "all"))
			host_client->voip.target = VT_ALL;
		else if (!strcmp(t, "nonmuted"))
			host_client->voip.target = VT_NONMUTED;
		else if (*t >= '0' && *t <= '9')
		{
			other = atoi(t);
			if (other >= MAX_SCOREBOARD)
				return;
			host_client->voip.target = VT_PLAYERSLOT0 + other;
		}
		else
		{
			/*don't know who you mean, futureproofing*/
			host_client->voip.target = VT_TEAM;
		}
	}
}
static void SV_Voice_MuteAll_f(void)
{
	if (cmd_source == src_command)
	{
		Cvar_Set("cl_voip_play", "0");
		if (cls.state == ca_connected)
			Cmd_ForwardToServer ();
	}
	else
	{
		host_client->voip.active = false;
	}
}
static void SV_Voice_UnmuteAll_f(void)
{
	if (cmd_source == src_command)
	{
		Cvar_Set("cl_voip_play", "1");
		if (cls.state == ca_connected)
			Cmd_ForwardToServer ();
	}
	else
	{
		host_client->voip.active = true;
	}
}

void SV_VoiceInit(void)
{
	Cvar_RegisterVariable(&sv_voip);
	Cvar_RegisterVariable(&sv_voip_echo);

	Cmd_AddCommand_ClientCommand("muteall", SV_Voice_MuteAll_f);
	Cmd_AddCommand_ClientCommand("unmuteall", SV_Voice_UnmuteAll_f);
	Cmd_AddCommand_ClientCommand("vignore", SV_Voice_Ignore_f);
	Cmd_AddCommand_ClientCommand("voicetarg", SV_Voice_Target_f);
}
