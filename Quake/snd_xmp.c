/* tracker music (module file) decoding support using libxmp >= v4.2.0
 * https://sourceforge.net/projects/xmp/
 * https://github.com/cmatsuoka/libxmp.git
 *
 * Copyright (C) 2016 O.Sezer <sezero@users.sourceforge.net>
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

#if defined(USE_CODEC_XMP)
#include "snd_codec.h"
#include "snd_codeci.h"
#include "snd_xmp.h"
#if defined(_WIN32) && defined(XMP_NO_DLL)
#define BUILDING_STATIC
#endif
#include <xmp.h>
#if ((XMP_VERCODE+0) < 0x040200)
#error libxmp version 4.2 or newer is required
#endif

static int S_XMP_StartPlay (snd_stream_t *stream)
{
	int fmt = 0;

	if (stream->info.channels == 1)
		fmt |= XMP_FORMAT_MONO;
	if (stream->info.width == 1)
		fmt |= XMP_FORMAT_8BIT|XMP_FORMAT_UNSIGNED;

	return xmp_start_player((xmp_context)stream->priv, stream->info.rate, fmt);
}

static qboolean S_XMP_CodecInitialize (void)
{
	return true;
}

static void S_XMP_CodecShutdown (void)
{
}

static qboolean S_XMP_CodecOpenStream (snd_stream_t *stream)
{
/* need to load the whole file into memory and pass it to libxmp
 * using xmp_load_module_from_memory() which requires libxmp >= 4.2.
 * libxmp-4.0/4.1 only have xmp_load_module() which accepts a file
 * name which isn't good with files in containers like paks, etc. */
	xmp_context c;
	byte *moddata;
	long len;
	int mark;

	c = xmp_create_context();
	if (c == NULL)
		return false;

	len = FS_filelength (&stream->fh);
	mark = Hunk_LowMark();
	moddata = (byte *) Hunk_Alloc(len);
	FS_fread(moddata, 1, len, &stream->fh);
	if (xmp_load_module_from_memory(c, moddata, len) != 0)
	{
		Con_DPrintf("Could not load module %s\n", stream->name);
		goto err1;
	}

	Hunk_FreeToLowMark(mark); /* free original file data */
	stream->priv = c;
	if (shm->speed > XMP_MAX_SRATE)
		stream->info.rate = 44100;
	else if (shm->speed < XMP_MIN_SRATE)
		stream->info.rate = 11025;
	else	stream->info.rate = shm->speed;
	stream->info.bits = shm->samplebits;
	stream->info.width = stream->info.bits / 8;
	stream->info.channels = shm->channels;

	if (S_XMP_StartPlay(stream) != 0)
		goto err2;
	/* percentual left/right channel separation, default is 70. */
	if (stream->info.channels == 2)
		if (xmp_set_player(c, XMP_PLAYER_MIX, 100) != 0)
			goto err3;
	/* interpolation type, default is XMP_INTERP_LINEAR */
	if (xmp_set_player(c, XMP_PLAYER_INTERP, XMP_INTERP_SPLINE) != 0)
		goto err3;

	return true;

err3:	xmp_end_player(c);
err2:	xmp_release_module(c);
err1:	xmp_free_context(c);
	return false;
}

static int S_XMP_CodecReadStream (snd_stream_t *stream, int bytes, void *buffer)
{
	int r;
	/* xmp_play_buffer() requires libxmp >= 4.1.  it will write
	 * native-endian pcm data to the buffer.  if the data write
	 * is partial, the rest of the buffer will be zero-filled.
	 * the last param is the number that the current sequence of
	 * the song will be looped at max. */
	r = xmp_play_buffer((xmp_context)stream->priv, buffer, bytes, 1);
	if (r == 0) {
		return bytes;
	}
	if (r == -XMP_END) {
		Con_DPrintf("XMP EOF\n");
		return 0;
	}
	return -1;
}

static void S_XMP_CodecCloseStream (snd_stream_t *stream)
{
	xmp_context c = (xmp_context)stream->priv;
	xmp_end_player(c);
	xmp_release_module(c);
	xmp_free_context(c);
	S_CodecUtilClose(&stream);
}

static int S_XMP_CodecRewindStream (snd_stream_t *stream)
{
	int ret;

	ret = S_XMP_StartPlay(stream);
	if (ret < 0) return ret;

	/*ret = xmp_set_position((xmp_context)stream->priv, 0);*/
	ret = xmp_seek_time((xmp_context)stream->priv, 0);
	if (ret < 0) return ret;

	return 0;
}

snd_codec_t xmp_codec =
{
	CODECTYPE_MOD,
	true,	/* always available. */
	"s3m",
	S_XMP_CodecInitialize,
	S_XMP_CodecShutdown,
	S_XMP_CodecOpenStream,
	S_XMP_CodecReadStream,
	S_XMP_CodecRewindStream,
	S_XMP_CodecCloseStream,
	NULL
};

#endif	/* USE_CODEC_XMP */
