/*
 * tracker music (module file) decoding support using libmodplug
 *
 * Copyright (C) 2013 O.Sezer <sezero@users.sourceforge.net>
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

#if defined(USE_CODEC_MODPLUG)
#include "snd_codec.h"
#include "snd_codeci.h"
#include "snd_modplug.h"
#include <libmodplug/modplug.h>

static void S_MODPLUG_SetSettings (snd_stream_t *stream)
{
	ModPlug_Settings settings;

	ModPlug_GetSettings(&settings);
	settings.mFlags = MODPLUG_ENABLE_OVERSAMPLING;
	settings.mChannels = shm->channels;
	settings.mBits = shm->samplebits;
	settings.mFrequency = shm->speed;
	settings.mResamplingMode = MODPLUG_RESAMPLE_SPLINE;/*MODPLUG_RESAMPLE_FIR*/
	settings.mLoopCount = -1;		/* to enable module internal loops */
	ModPlug_SetSettings(&settings);

	if (stream) {
		stream->info.rate = shm->speed;
		stream->info.bits = shm->samplebits;
		stream->info.width = stream->info.bits / 8;
		stream->info.channels = shm->channels;
	}
}

static qboolean S_MODPLUG_CodecInitialize (void)
{
	return true;
}

static void S_MODPLUG_CodecShutdown (void)
{
}

static qboolean S_MODPLUG_CodecOpenStream (snd_stream_t *stream)
{
/* need to load the whole file into memory and pass it to libmodplug */
	byte *moddata;
	long len;
	int mark;

	len = FS_filelength (&stream->fh);
	mark = Hunk_LowMark();
	moddata = (byte *) Hunk_Alloc(len);
	FS_fread(moddata, 1, len, &stream->fh);

	S_MODPLUG_SetSettings(stream);
	stream->priv = ModPlug_Load(moddata, len);
	Hunk_FreeToLowMark(mark); /* free original file data */
	if (!stream->priv)
	{
		Con_DPrintf("Could not load module %s\n", stream->name);
		return false;
	}

	ModPlug_Seek((ModPlugFile*)stream->priv, 0);
	/* default volume (128) sounds rather low? */
	ModPlug_SetMasterVolume((ModPlugFile*)stream->priv, 384);	/* 0-512 */
	return true;
}

static int S_MODPLUG_CodecReadStream (snd_stream_t *stream, int bytes, void *buffer)
{
	return ModPlug_Read((ModPlugFile*)stream->priv, buffer, bytes);
}

static void S_MODPLUG_CodecCloseStream (snd_stream_t *stream)
{
	ModPlug_Unload((ModPlugFile*)stream->priv);
	S_CodecUtilClose(&stream);
}

static int S_MODPLUG_CodecJumpToOrder (snd_stream_t *stream, int to)
{
	ModPlug_SeekOrder((ModPlugFile*)stream->priv, to);
	return 0;
}

static int S_MODPLUG_CodecRewindStream (snd_stream_t *stream)
{
	ModPlug_Seek((ModPlugFile*)stream->priv, 0);
	return 0;
}

snd_codec_t modplug_codec =
{
	CODECTYPE_MOD,
	true,	/* always available. */
	"s3m",
	S_MODPLUG_CodecInitialize,
	S_MODPLUG_CodecShutdown,
	S_MODPLUG_CodecOpenStream,
	S_MODPLUG_CodecReadStream,
	S_MODPLUG_CodecRewindStream,
	S_MODPLUG_CodecJumpToOrder,
	S_MODPLUG_CodecCloseStream,
	NULL
};

#endif	/* USE_CODEC_MODPLUG */
