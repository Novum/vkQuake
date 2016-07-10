/*
 * Audio Codecs: Adapted from ioquake3 with changes.
 * For now, only handles streaming music, not sound effects.
 *
 * Copyright (C) 1999-2005 Id Software, Inc.
 * Copyright (C) 2005 Stuart Dalton <badcdev@gmail.com>
 * Copyright (C) 2010-2012 O.Sezer <sezero@users.sourceforge.net>
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

#ifndef _SND_CODECI_H_
#define _SND_CODECI_H_

/* Codec internals */
typedef qboolean (*CODEC_INIT)(void);
typedef void (*CODEC_SHUTDOWN)(void);
typedef qboolean (*CODEC_OPEN)(snd_stream_t *stream);
typedef int (*CODEC_READ)(snd_stream_t *stream, int bytes, void *buffer);
typedef int (*CODEC_REWIND)(snd_stream_t *stream);
typedef void (*CODEC_CLOSE)(snd_stream_t *stream);

struct snd_codec_s
{
	unsigned int type;	/* handled data type. (1U << n) */
	qboolean initialized;	/* init succeedded */
	const char *ext;	/* expected extension */
	CODEC_INIT initialize;
	CODEC_SHUTDOWN shutdown;
	CODEC_OPEN codec_open;
	CODEC_READ codec_read;
	CODEC_REWIND codec_rewind;
	CODEC_CLOSE codec_close;
	snd_codec_t *next;
};

qboolean S_CodecForwardStream (snd_stream_t *stream, unsigned int type);
			/* Forward a stream to another codec of 'type' type. */

#endif	/* _SND_CODECI_H_ */

