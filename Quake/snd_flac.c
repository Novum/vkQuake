/*
 * fLaC streaming music support, loosely based QuakeForge implementation
 * with modifications.  requires libFLAC >= 1.0.4 at compile and runtime.
 *
 * Copyright (C) 2005 Bill Currie <bill@taniwha.org>
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
 *
 */

#include "quakedef.h"

#if defined(USE_CODEC_FLAC)
#include "snd_codec.h"
#include "snd_codeci.h"
#include "snd_flac.h"

#undef LEGACY_FLAC
#include <FLAC/stream_decoder.h>
/* FLAC 1.1.3 has FLAC_API_VERSION_CURRENT == 8 */
#if !defined(FLAC_API_VERSION_CURRENT) || ((FLAC_API_VERSION_CURRENT+0) < 8)
#define LEGACY_FLAC
#include <FLAC/seekable_stream_decoder.h>
#endif

#ifdef LEGACY_FLAC
#define FLAC__StreamDecoder					FLAC__SeekableStreamDecoder
#define FLAC__StreamDecoderReadStatus				FLAC__SeekableStreamDecoderReadStatus
#define FLAC__StreamDecoderSeekStatus				FLAC__SeekableStreamDecoderSeekStatus
#define FLAC__StreamDecoderTellStatus				FLAC__SeekableStreamDecoderTellStatus
#define FLAC__StreamDecoderLengthStatus				FLAC__SeekableStreamDecoderLengthStatus

#define FLAC__stream_decoder_new				FLAC__seekable_stream_decoder_new
#define FLAC__stream_decoder_finish				FLAC__seekable_stream_decoder_finish
#define FLAC__stream_decoder_delete				FLAC__seekable_stream_decoder_delete
#define FLAC__stream_decoder_process_single			FLAC__seekable_stream_decoder_process_single
#define FLAC__stream_decoder_seek_absolute			FLAC__seekable_stream_decoder_seek_absolute
#define FLAC__stream_decoder_process_until_end_of_metadata	FLAC__seekable_stream_decoder_process_until_end_of_metadata
#define FLAC__stream_decoder_get_state				FLAC__seekable_stream_decoder_get_state

#define FLAC__STREAM_DECODER_INIT_STATUS_OK			FLAC__SEEKABLE_STREAM_DECODER_OK
#define FLAC__STREAM_DECODER_READ_STATUS_CONTINUE		FLAC__SEEKABLE_STREAM_DECODER_READ_STATUS_OK
#define FLAC__STREAM_DECODER_READ_STATUS_ABORT			FLAC__SEEKABLE_STREAM_DECODER_READ_STATUS_ERROR
#define FLAC__STREAM_DECODER_READ_STATUS_END_OF_STREAM		FLAC__SEEKABLE_STREAM_DECODER_READ_STATUS_OK	/* !!! */
#define FLAC__STREAM_DECODER_WRITE_STATUS_ABORT			FLAC__STREAM_DECODER_WRITE_STATUS_ABORT
#define FLAC__STREAM_DECODER_SEEK_STATUS_OK			FLAC__SEEKABLE_STREAM_DECODER_SEEK_STATUS_OK
#define FLAC__STREAM_DECODER_SEEK_STATUS_ERROR			FLAC__SEEKABLE_STREAM_DECODER_SEEK_STATUS_ERROR
#define FLAC__STREAM_DECODER_TELL_STATUS_OK			FLAC__SEEKABLE_STREAM_DECODER_TELL_STATUS_OK
#define FLAC__STREAM_DECODER_TELL_STATUS_ERROR			FLAC__SEEKABLE_STREAM_DECODER_TELL_STATUS_ERROR
#define FLAC__STREAM_DECODER_LENGTH_STATUS_OK			FLAC__SEEKABLE_STREAM_DECODER_LENGTH_STATUS_OK
typedef unsigned FLAC_SIZE_T;
#else
typedef size_t   FLAC_SIZE_T;
#endif

typedef struct {
	FLAC__StreamDecoder *decoder;
	fshandle_t *file;
	snd_info_t *info;
	byte *buffer;
	int size, pos, error;
} flacfile_t;

/* CALLBACK FUNCTIONS: */
static void
flac_error_func (const FLAC__StreamDecoder *decoder,
		 FLAC__StreamDecoderErrorStatus status, void *client_data)
{
	flacfile_t *ff = (flacfile_t *) client_data;
	ff->error = -1;
	Con_Printf ("FLAC: decoder error %i\n", status);
}

static FLAC__StreamDecoderReadStatus
flac_read_func (const FLAC__StreamDecoder *decoder, FLAC__byte buffer[],
		FLAC_SIZE_T *bytes, void *client_data)
{
	flacfile_t *ff = (flacfile_t *) client_data;
	if (*bytes > 0)
	{
		*bytes = FS_fread(buffer, 1, *bytes, ff->file);
		if (FS_ferror(ff->file))
			return FLAC__STREAM_DECODER_READ_STATUS_ABORT;
		if (*bytes == 0)
			return FLAC__STREAM_DECODER_READ_STATUS_END_OF_STREAM;
		return FLAC__STREAM_DECODER_READ_STATUS_CONTINUE;
	}
	return FLAC__STREAM_DECODER_READ_STATUS_ABORT;
}

static FLAC__StreamDecoderSeekStatus
flac_seek_func (const FLAC__StreamDecoder *decoder,
		FLAC__uint64 absolute_byte_offset, void *client_data)
{
	flacfile_t *ff = (flacfile_t *) client_data;
	if (FS_fseek(ff->file, (long)absolute_byte_offset, SEEK_SET) < 0)
		return FLAC__STREAM_DECODER_SEEK_STATUS_ERROR;
	return FLAC__STREAM_DECODER_SEEK_STATUS_OK;
}

static FLAC__StreamDecoderTellStatus
flac_tell_func (const FLAC__StreamDecoder *decoder,
		FLAC__uint64 *absolute_byte_offset, void *client_data)
{
	flacfile_t *ff = (flacfile_t *) client_data;
	long pos = FS_ftell (ff->file);
	if (pos < 0) return FLAC__STREAM_DECODER_TELL_STATUS_ERROR;
	*absolute_byte_offset = (FLAC__uint64) pos;
	return FLAC__STREAM_DECODER_TELL_STATUS_OK;
}

static FLAC__StreamDecoderLengthStatus
flac_length_func (const FLAC__StreamDecoder *decoder,
		  FLAC__uint64 *stream_length, void *client_data)
{
	flacfile_t *ff = (flacfile_t *) client_data;
	*stream_length = (FLAC__uint64) FS_filelength (ff->file);
	return FLAC__STREAM_DECODER_LENGTH_STATUS_OK;
}

static FLAC__bool
flac_eof_func (const FLAC__StreamDecoder *decoder, void *client_data)
{
	flacfile_t *ff = (flacfile_t *) client_data;
	if (FS_feof (ff->file)) return true;
	return false;
}

static FLAC__StreamDecoderWriteStatus
flac_write_func (const FLAC__StreamDecoder *decoder,
		 const FLAC__Frame *frame, const FLAC__int32 * const buffer[],
		 void *client_data)
{
	flacfile_t *ff = (flacfile_t *) client_data;

	if (!ff->buffer) {
		ff->buffer = (byte *) malloc (ff->info->blocksize * ff->info->channels * ff->info->width);
		if (!ff->buffer) {
			ff->error = -1; /* needn't set this here, but... */
			Con_Printf("Insufficient memory for fLaC audio\n");
			return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
		}
	}

	if (ff->info->channels == 1)
	{
		unsigned i;
		const FLAC__int32 *in = buffer[0];

		if (ff->info->bits == 8)
		{
			byte  *out = ff->buffer;
			for (i = 0; i < frame->header.blocksize; i++)
				*out++ = *in++ + 128;
		}
		else
		{
			short *out = (short *) ff->buffer;
			for (i = 0; i < frame->header.blocksize; i++)
				*out++ = *in++;
		}
	}
	else
	{
		unsigned i;
		const FLAC__int32 *li = buffer[0];
		const FLAC__int32 *ri = buffer[1];

		if (ff->info->bits == 8)
		{
			char  *lo = (char *) ff->buffer + 0;
			char  *ro = (char *) ff->buffer + 1;
			for (i = 0; i < frame->header.blocksize; i++, lo++, ro++)
			{
				*lo++ = *li++ + 128;
				*ro++ = *ri++ + 128;
			}
		}
		else
		{
			short *lo = (short *) ff->buffer + 0;
			short *ro = (short *) ff->buffer + 1;
			for (i = 0; i < frame->header.blocksize; i++, lo++, ro++)
			{
				*lo++ = *li++;
				*ro++ = *ri++;
			}
		}
	}

	ff->size = frame->header.blocksize * ff->info->width * ff->info->channels;
	ff->pos = 0;
	return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
}

static void
flac_meta_func (const FLAC__StreamDecoder *decoder,
			    const FLAC__StreamMetadata *metadata, void *client_data)
{
	flacfile_t *ff = (flacfile_t *) client_data;
	if (metadata->type == FLAC__METADATA_TYPE_STREAMINFO)
	{
		ff->info->rate = metadata->data.stream_info.sample_rate;
		ff->info->bits = metadata->data.stream_info.bits_per_sample;
		ff->info->width = ff->info->bits / 8;
		ff->info->channels = metadata->data.stream_info.channels;
		ff->info->blocksize = metadata->data.stream_info.max_blocksize;
		ff->info->dataofs = 0;	/* got the STREAMINFO metadata */
	}
}


static qboolean S_FLAC_CodecInitialize (void)
{
	return true;
}

static void S_FLAC_CodecShutdown (void)
{
}

static qboolean S_FLAC_CodecOpenStream (snd_stream_t *stream)
{
	flacfile_t *ff;
	int rc;

	ff = (flacfile_t *) Z_Malloc(sizeof(flacfile_t));

	ff->decoder = FLAC__stream_decoder_new ();
	if (ff->decoder == NULL)
	{
		Con_Printf("Unable to create fLaC decoder\n");
		goto _fail;
	}

	stream->priv = ff;
	ff->info = & stream->info;
	ff->file = & stream->fh;
	ff->info->dataofs = -1; /* check for STREAMINFO metadata existence */

#ifdef LEGACY_FLAC
	FLAC__seekable_stream_decoder_set_error_callback (ff->decoder, flac_error_func);
	FLAC__seekable_stream_decoder_set_read_callback (ff->decoder, flac_read_func);
	FLAC__seekable_stream_decoder_set_seek_callback (ff->decoder, flac_seek_func);
	FLAC__seekable_stream_decoder_set_tell_callback (ff->decoder, flac_tell_func);
	FLAC__seekable_stream_decoder_set_length_callback (ff->decoder, flac_length_func);
	FLAC__seekable_stream_decoder_set_eof_callback (ff->decoder, flac_eof_func);
	FLAC__seekable_stream_decoder_set_write_callback (ff->decoder, flac_write_func);
	FLAC__seekable_stream_decoder_set_metadata_callback (ff->decoder, flac_meta_func);
	FLAC__seekable_stream_decoder_set_client_data (ff->decoder, ff);
	rc = FLAC__seekable_stream_decoder_init (ff->decoder);
#else
	rc = FLAC__stream_decoder_init_stream(ff->decoder,
								flac_read_func,
								flac_seek_func,
								flac_tell_func,
								flac_length_func,
								flac_eof_func,
								flac_write_func,
								flac_meta_func,
								flac_error_func,
							ff);
#endif
	if (rc != FLAC__STREAM_DECODER_INIT_STATUS_OK) /* unlikely */
	{
		Con_Printf ("FLAC: decoder init error %i\n", rc);
		goto _fail;
	}

	rc = FLAC__stream_decoder_process_until_end_of_metadata (ff->decoder);
	if (rc == false || ff->error)
	{
		rc = FLAC__stream_decoder_get_state(ff->decoder);
		Con_Printf("%s not a valid flac file? (decoder state %i)\n",
						stream->name, rc);
		goto _fail;
	}

	if (ff->info->dataofs < 0)
	{
		Con_Printf("%s has no STREAMINFO\n", stream->name);
		goto _fail;
	}
	if (ff->info->bits != 8 && ff->info->bits != 16)
	{
		Con_Printf("%s is not 8 or 16 bit\n", stream->name);
		goto _fail;
	}
	if (ff->info->channels != 1 && ff->info->channels != 2)
	{
		Con_Printf("Unsupported number of channels %d in %s\n",
					ff->info->channels, stream->name);
		goto _fail;
	}

	return true;

_fail:
	if (ff->decoder)
	{
		FLAC__stream_decoder_finish (ff->decoder);
		FLAC__stream_decoder_delete (ff->decoder);
	}
	Z_Free(ff);
	return false;
}

static int S_FLAC_CodecReadStream (snd_stream_t *stream, int len, void *buffer)
{
	flacfile_t *ff = (flacfile_t *) stream->priv;
	byte *buf = (byte *) buffer;
	int count = 0;

	while (len) {
		int res = 0;
		if (ff->size == ff->pos)
			FLAC__stream_decoder_process_single (ff->decoder);
		if (ff->error) return -1;
		res = ff->size - ff->pos;
		if (res > len)
			res = len;
		if (res > 0) {
			memcpy (buf, ff->buffer + ff->pos, res);
			count += res;
			len -= res;
			buf += res;
			ff->pos += res;
		} else if (res < 0) { /* error */
			return -1;
		} else {
			Con_DPrintf ("FLAC: EOF\n");
			break;
		}
	}
	return count;
}

static void S_FLAC_CodecCloseStream (snd_stream_t *stream)
{
	flacfile_t *ff = (flacfile_t *) stream->priv;

	FLAC__stream_decoder_finish (ff->decoder);
	FLAC__stream_decoder_delete (ff->decoder);

	if (ff->buffer)
			free(ff->buffer);
	Z_Free(ff);

	S_CodecUtilClose(&stream);
}

static int S_FLAC_CodecRewindStream (snd_stream_t *stream)
{
	flacfile_t *ff = (flacfile_t *) stream->priv;

	ff->pos = ff->size = 0;
	if (FLAC__stream_decoder_seek_absolute(ff->decoder, 0)) return 0;
	return -1;
}

snd_codec_t flac_codec =
{
	CODECTYPE_FLAC,
	true,	/* always available. */
	"flac",
	S_FLAC_CodecInitialize,
	S_FLAC_CodecShutdown,
	S_FLAC_CodecOpenStream,
	S_FLAC_CodecReadStream,
	S_FLAC_CodecRewindStream,
	S_FLAC_CodecCloseStream,
	NULL
};

#endif	/* USE_CODEC_FLAC */

