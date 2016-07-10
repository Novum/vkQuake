/*
 * Ogg/Opus streaming music support, loosely based on several open source
 * Quake engine based projects with many modifications.
 *
 * Copyright (C) 2012-2013 O.Sezer <sezero@users.sourceforge.net>
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
 *
 */

#include "quakedef.h"

#if defined(USE_CODEC_OPUS)
#include "snd_codec.h"
#include "snd_codeci.h"
#include "snd_opus.h"

#include <errno.h>
#include <opusfile.h>


/* CALLBACK FUNCTIONS: */

static int opc_fclose (void *f)
{
	return 0;		/* we fclose() elsewhere. */
}

static int opc_fread (void *f, unsigned char *buf, int size)
{
	int ret;

	if (size < 0)
	{
		errno = EINVAL;
		return -1;
	}

	ret = (int) FS_fread(buf, 1, (size_t)size, (fshandle_t *)f);
	if (ret == 0 && errno != 0)
		ret = -1;
	return ret;
}

static int opc_fseek (void *f, opus_int64 off, int whence)
{
	if (f == NULL) return (-1);
	return FS_fseek((fshandle_t *)f, (long) off, whence);
}

static opus_int64 opc_ftell (void *f)
{
	return (opus_int64) FS_ftell((fshandle_t *)f);
}

static const OpusFileCallbacks opc_qfs =
{
	(int (*)(void *, unsigned char *, int)) opc_fread,
	(int (*)(void *, opus_int64, int))	opc_fseek,
	(opus_int64 (*)(void *))		opc_ftell,
	(int (*)(void *))			opc_fclose
};

static qboolean S_OPUS_CodecInitialize (void)
{
	return true;
}

static void S_OPUS_CodecShutdown (void)
{
}

static qboolean S_OPUS_CodecOpenStream (snd_stream_t *stream)
{
	OggOpusFile *opFile;
	const OpusHead *op_info;
	long numstreams;
	int res;

	opFile = op_open_callbacks(&stream->fh, &opc_qfs, NULL, 0, &res);
	if (!opFile)
	{
		Con_Printf("%s is not a valid Opus file (error %i).\n",
				stream->name, res);
		goto _fail;
	}

	stream->priv = opFile;

	if (!op_seekable(opFile))
	{
		Con_Printf("Opus stream %s not seekable.\n", stream->name);
		goto _fail;
	}

	op_info = op_head(opFile, -1);
	if (!op_info)
	{
		Con_Printf("Unable to get stream information for %s.\n", stream->name);
		goto _fail;
	}

	/* FIXME: handle section changes */
	numstreams = op_info->stream_count;
	if (numstreams != 1)
	{
		Con_Printf("More than one (%ld) stream in %s\n",
					(long)op_info->stream_count, stream->name);
		goto _fail;
	}

	if (op_info->channel_count != 1 && op_info->channel_count != 2)
	{
		Con_Printf("Unsupported number of channels %d in %s\n",
					op_info->channel_count, stream->name);
		goto _fail;
	}

	/* All Opus audio is coded at 48 kHz, and should also be decoded
	 * at 48 kHz for playback: info->input_sample_rate only tells us
	 * the sampling rate of the original input before opus encoding.
	 * S_RawSamples() shall already downsample this, as necessary.  */
	stream->info.rate = 48000;
	stream->info.channels = op_info->channel_count;
	/* op_read() yields 16-bit output using native endian ordering: */
	stream->info.bits = 16;
	stream->info.width = 2;

	return true;
_fail:
	if (opFile)
		op_free(opFile);
	return false;
}

static int S_OPUS_CodecReadStream (snd_stream_t *stream, int bytes, void *buffer)
{
	int	section;	/* FIXME: handle section changes */
	int	cnt, res, rem;
	opus_int16 *	ptr;

	rem = bytes / stream->info.width;
	if (rem / stream->info.channels <= 0)
		return 0;

	cnt = 0;
	ptr = (opus_int16 *) buffer;
	while (1)
	{
	/* op_read() yields 16-bit output using native endian ordering. returns
	 * the number of samples read per channel on success, or a negative value
	 * on failure. */
		res = op_read((OggOpusFile *)stream->priv, ptr, rem, &section);
		if (res <= 0)
			break;
		cnt += res;
		res *= stream->info.channels;
		rem -= res;
		if (rem <= 0)
			break;
		ptr += res;
	}

	if (res < 0)
		return res;

	cnt *= (stream->info.channels * stream->info.width);
	return cnt;
}

static void S_OPUS_CodecCloseStream (snd_stream_t *stream)
{
	op_free((OggOpusFile *)stream->priv);
	S_CodecUtilClose(&stream);
}

static int S_OPUS_CodecRewindStream (snd_stream_t *stream)
{
	return op_pcm_seek ((OggOpusFile *)stream->priv, 0);
}

snd_codec_t opus_codec =
{
	CODECTYPE_OPUS,
	true,	/* always available. */
	"opus",
	S_OPUS_CodecInitialize,
	S_OPUS_CodecShutdown,
	S_OPUS_CodecOpenStream,
	S_OPUS_CodecReadStream,
	S_OPUS_CodecRewindStream,
	S_OPUS_CodecCloseStream,
	NULL
};

#endif	/* USE_CODEC_OPUS */

