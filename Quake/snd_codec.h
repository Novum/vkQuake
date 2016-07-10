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

#ifndef _SND_CODEC_H_
#define _SND_CODEC_H_

typedef struct snd_info_s
{
	int rate;
	int bits, width;
	int channels;
	int samples;
	int blocksize;
	int size;
	int dataofs;
} snd_info_t;

typedef enum {
	STREAM_NONE = -1,
	STREAM_INIT,
	STREAM_PAUSE,
	STREAM_PLAY
} stream_status_t;

typedef struct snd_codec_s snd_codec_t;

typedef struct snd_stream_s
{
	fshandle_t fh;
	qboolean pak;
	char name[MAX_QPATH];	/* name of the source file */
	snd_info_t info;
	stream_status_t status;
	snd_codec_t *codec;	/* codec handling this stream */
	void *priv;		/* data private to the codec. */
} snd_stream_t;


void S_CodecInit (void);
void S_CodecShutdown (void);

/* Callers of the following S_CodecOpenStream* functions
 * are reponsible for attaching any path to the filename */

snd_stream_t *S_CodecOpenStreamType (const char *filename, unsigned int type);
	/* Decides according to the required type. */

snd_stream_t *S_CodecOpenStreamAny (const char *filename);
	/* Decides according to file extension. if the
	 * name has no extension, try all available. */

snd_stream_t *S_CodecOpenStreamExt (const char *filename);
	/* Decides according to file extension. the name
	 * MUST have an extension. */

void S_CodecCloseStream (snd_stream_t *stream);
int S_CodecReadStream (snd_stream_t *stream, int bytes, void *buffer);
int S_CodecRewindStream (snd_stream_t *stream);

snd_stream_t *S_CodecUtilOpen(const char *filename, snd_codec_t *codec);
void S_CodecUtilClose(snd_stream_t **stream);


#define CODECTYPE_NONE		0
#define CODECTYPE_MID		(1U << 0)
#define CODECTYPE_MOD		(1U << 1)
#define CODECTYPE_FLAC		(1U << 2)
#define CODECTYPE_WAV		(1U << 3)
#define CODECTYPE_MP3		(1U << 4)
#define CODECTYPE_VORBIS	(1U << 5)
#define CODECTYPE_OPUS		(1U << 6)
#define CODECTYPE_UMX		(1U << 7)

#define CODECTYPE_WAVE		CODECTYPE_WAV
#define CODECTYPE_MIDI		CODECTYPE_MID

int S_CodecIsAvailable (unsigned int type);
	/* return 1 if available, 0 if codec failed init
	 * or -1 if no such codec is present. */

#endif	/* _SND_CODEC_H_ */

