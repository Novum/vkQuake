/*
 * MP3 decoding support using libmpg123, loosely based on an SDL_mixer
 * See: http://bubu.lv/changeset/4/public/libs/SDL/generated/SDL_mixer
 *
 * Copyright (C) 2011-2012 O.Sezer <sezero@users.sourceforge.net>
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

#if defined(USE_CODEC_MP3)
#include "snd_codec.h"
#include "snd_codeci.h"
#include "snd_mp3.h"
#include <errno.h>

#define MPG123_DEF_SSIZE_T  /* we do define ssize_t in our stdinc.h */
#include <mpg123.h>

#if !defined(MPG123_API_VERSION) || (MPG123_API_VERSION < 24)
#error minimum required libmpg123 version is 1.12.0 (api version 24)
#endif	/* MPG123_API_VERSION */

/* Private data */
typedef struct _mp3_priv_t
{
	int handle_newed, handle_opened;
	mpg123_handle* handle;
} mp3_priv_t;

/* CALLBACK FUNCTIONS: */
/* CAREFUL: libmpg123 expects POSIX read() and lseek() behavior,
 * however our FS_fread() and FS_fseek() return fread() and fseek()
 * compatible values.  */

static ssize_t mp3_read (void *f, void *buf, size_t size)
{
	ssize_t ret = (ssize_t) FS_fread(buf, 1, size, (fshandle_t *)f);
	if (ret == 0 && errno != 0)
		ret = -1;
	return ret;
}

static off_t mp3_seek (void *f, off_t offset, int whence)
{
	if (f == NULL) return (-1);
	if (FS_fseek((fshandle_t *)f, (long) offset, whence) < 0)
		return (off_t)-1;
	return (off_t) FS_ftell((fshandle_t *)f);
}

static qboolean S_MP3_CodecInitialize (void)
{
	if (!mp3_codec.initialized)
	{
		if (mpg123_init() != MPG123_OK)
		{
			Con_Printf ("Could not initialize mpg123\n");
			return false;
		}
		mp3_codec.initialized = true;
		return true;
	}

	return true;
}

static void S_MP3_CodecShutdown (void)
{
	if (mp3_codec.initialized)
	{
		mp3_codec.initialized = false;
		mpg123_exit();
	}
}

static qboolean S_MP3_CodecOpenStream (snd_stream_t *stream)
{
	long rate = 0;
	int encoding = 0, channels = 0;
	mp3_priv_t *priv = NULL;

	stream->priv = Z_Malloc(sizeof(mp3_priv_t));
	priv = (mp3_priv_t *) stream->priv;
	priv->handle = mpg123_new(NULL, NULL);
	if (priv->handle == NULL)
	{
		Con_Printf("Unable to allocate mpg123 handle\n");
		goto _fail;
	}
	priv->handle_newed = 1;

	if (mpg123_replace_reader_handle(priv->handle, mp3_read, mp3_seek, NULL) != MPG123_OK ||
	    mpg123_open_handle(priv->handle, &stream->fh) != MPG123_OK)
	{
		Con_Printf("Unable to open mpg123 handle\n");
		goto _fail;
	}
	priv->handle_opened = 1;

	if (mpg123_getformat(priv->handle, &rate, &channels, &encoding) != MPG123_OK)
	{
		Con_Printf("Unable to retrieve mpg123 format for %s\n", stream->name);
		goto _fail;
	}

	switch (channels)
	{
	case MPG123_MONO:
		stream->info.channels = 1;
		break;
	case MPG123_STEREO:
		stream->info.channels = 2;
		break;
	default:
		Con_Printf("Unsupported number of channels %d in %s\n", channels, stream->name);
		goto _fail;
	}

	stream->info.rate = rate;

	switch (encoding)
	{
	case MPG123_ENC_UNSIGNED_8:
		stream->info.bits = 8;
		stream->info.width = 1;
		break;
	case MPG123_ENC_SIGNED_8:
	/* unsupported: force mpg123 to convert */
		stream->info.bits = 8;
		stream->info.width = 1;
		encoding = MPG123_ENC_UNSIGNED_8;
		break;
	case MPG123_ENC_SIGNED_16:
		stream->info.bits = 16;
		stream->info.width = 2;
		break;
	case MPG123_ENC_UNSIGNED_16:
	default:
	/* unsupported: force mpg123 to convert */
		stream->info.bits = 16;
		stream->info.width = 2;
		encoding = MPG123_ENC_SIGNED_16;
		break;
	}
	if (mpg123_format_support(priv->handle, rate, encoding) == 0)
	{
		Con_Printf("Unsupported format for %s\n", stream->name);
		goto _fail;
	}
	mpg123_format_none(priv->handle);
	mpg123_format(priv->handle, rate, channels, encoding);

	return true;
_fail:
	if (priv)
	{
		if (priv->handle_opened)
			mpg123_close(priv->handle);
		if (priv->handle_newed)
			mpg123_delete(priv->handle);
		Z_Free(stream->priv);
	}
	return false;
}

static int S_MP3_CodecReadStream (snd_stream_t *stream, int bytes, void *buffer)
{
	mp3_priv_t *priv = (mp3_priv_t *) stream->priv;
	size_t bytes_read = 0;
	int res = mpg123_read (priv->handle, (unsigned char *)buffer, (size_t)bytes, &bytes_read);
	switch (res)
	{
	case MPG123_DONE:
		Con_DPrintf("mp3 EOF\n");
	case MPG123_OK:
		return (int)bytes_read;
	}
	return -1; /* error */
}

static void S_MP3_CodecCloseStream (snd_stream_t *stream)
{
	mp3_priv_t *priv = (mp3_priv_t *) stream->priv;
	mpg123_close(priv->handle);
	mpg123_delete(priv->handle);
	Z_Free(stream->priv);
	S_CodecUtilClose(&stream);
}

static int S_MP3_CodecRewindStream (snd_stream_t *stream)
{
	mp3_priv_t *priv = (mp3_priv_t *) stream->priv;
	off_t res = mpg123_seek(priv->handle, 0, SEEK_SET);
	if (res >= 0) return (0);
	return res;
}

snd_codec_t mp3_codec =
{
	CODECTYPE_MP3,
	false,
	"mp3",
	S_MP3_CodecInitialize,
	S_MP3_CodecShutdown,
	S_MP3_CodecOpenStream,
	S_MP3_CodecReadStream,
	S_MP3_CodecRewindStream,
	S_MP3_CodecCloseStream,
	NULL
};

#endif	/* USE_CODEC_MP3 */

