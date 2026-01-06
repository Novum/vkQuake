/*
 * snd_sdl.c - SDL audio driver for Hexen II: Hammer of Thyrion (uHexen2)
 * based on implementations found in the quakeforge and ioquake3 projects.
 *
 * Copyright (C) 1999-2005 Id Software, Inc.
 * Copyright (C) 2005-2012 O.Sezer <sezero@users.sourceforge.net>
 * Copyright (C) 2010-2014 QuakeSpasm developers
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "quakedef.h"

static int				 buffersize;
static SDL_AudioDeviceID audio_device = 0;
static SDL_AudioStream	*audio_stream = NULL;

static void SDLCALL paint_audio (void *userdata, SDL_AudioStream *stream, int additional_amount, int total_amount)
{
	int pos, len1, len2, tobufend;

	if (!shm || additional_amount <= 0)
		return;

	pos = (shm->samplepos * (shm->samplebits / 8));
	if (pos >= buffersize)
		shm->samplepos = pos = 0;

	tobufend = buffersize - pos;
	len1 = additional_amount;
	len2 = 0;

	if (len1 > tobufend)
	{
		len1 = tobufend;
		len2 = additional_amount - len1;
	}

	SDL_PutAudioStreamData (stream, shm->buffer + pos, len1);

	if (len2 > 0)
	{
		SDL_PutAudioStreamData (stream, shm->buffer, len2);
		shm->samplepos = (len2 / (shm->samplebits / 8));
	}
	else
	{
		shm->samplepos += (len1 / (shm->samplebits / 8));
	}

	if (shm->samplepos >= shm->samples)
		shm->samplepos = 0;
}

qboolean SNDDMA_Init (dma_t *dma)
{
	SDL_AudioSpec spec;
	char		  drivername[128];

	if (!SDL_InitSubSystem (SDL_INIT_AUDIO))
	{
		Con_Printf ("Couldn't init SDL audio: %s\n", SDL_GetError ());
		return false;
	}

	/* Set up the desired format */
	spec.freq = snd_mixspeed.value;
	spec.format = (loadas8bit.value) ? SDL_AUDIO_U8 : SDL_AUDIO_S16;
	spec.channels = 2;

	/* Open the audio device with callback */
	audio_stream = SDL_OpenAudioDeviceStream (SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, paint_audio, NULL);
	if (!audio_stream)
	{
		Con_Printf ("Couldn't open SDL audio: %s\n", SDL_GetError ());
		SDL_QuitSubSystem (SDL_INIT_AUDIO);
		return false;
	}

	audio_device = SDL_GetAudioStreamDevice (audio_stream);

	memset ((void *)dma, 0, sizeof (dma_t));
	shm = dma;

	/* Fill the audio DMA information block */
	shm->samplebits = SDL_AUDIO_BITSIZE (spec.format);
	shm->signed8 = (spec.format == SDL_AUDIO_S8);
	shm->speed = spec.freq;
	shm->channels = spec.channels;

	/* Calculate buffer size - aim for ~100ms of audio */
	int num_samples = (spec.channels * spec.freq) / 10;
	num_samples = Q_nextPow2 (num_samples);

	shm->samples = num_samples;
	shm->samplepos = 0;
	shm->submission_chunk = 1;

	Con_Printf ("SDL audio spec  : %d Hz, %d channels\n", spec.freq, spec.channels);
	{
		const char *driver = SDL_GetCurrentAudioDriver ();
		q_snprintf (drivername, sizeof (drivername), "%s", driver != NULL ? driver : "(UNKNOWN)");
	}
	buffersize = shm->samples * (shm->samplebits / 8);
	Con_Printf ("SDL audio driver: %s, %d bytes buffer\n", drivername, buffersize);

	shm->buffer = (unsigned char *)Mem_Alloc (buffersize);
	if (!shm->buffer)
	{
		SDL_DestroyAudioStream (audio_stream);
		audio_stream = NULL;
		audio_device = 0;
		SDL_QuitSubSystem (SDL_INIT_AUDIO);
		shm = NULL;
		Con_Printf ("Failed allocating memory for SDL audio\n");
		return false;
	}

	SDL_ResumeAudioDevice (audio_device);

	Con_Printf ("SDL audio initialized: samples=%d, samplebits=%d, channels=%d\n", shm->samples, shm->samplebits, shm->channels);

	return true;
}

int SNDDMA_GetDMAPos (void)
{
	return shm->samplepos;
}

void SNDDMA_Shutdown (void)
{
	if (shm)
	{
		Con_Printf ("Shutting down SDL sound\n");
		SDL_DestroyAudioStream (audio_stream);
		audio_stream = NULL;
		audio_device = 0;
		SDL_QuitSubSystem (SDL_INIT_AUDIO);
		if (shm->buffer)
			Mem_Free (shm->buffer);
		shm->buffer = NULL;
		shm = NULL;
	}
}

void SNDDMA_LockBuffer (void)
{
	if (audio_stream)
		SDL_LockAudioStream (audio_stream);
}

void SNDDMA_Submit (void)
{
	/* In callback model, unlock is all we need to do */
	if (audio_stream)
		SDL_UnlockAudioStream (audio_stream);
}

void SNDDMA_BlockSound (void)
{
	if (audio_device)
		SDL_PauseAudioDevice (audio_device);
}

void SNDDMA_UnblockSound (void)
{
	if (audio_device)
		SDL_ResumeAudioDevice (audio_device);
}
