/*
 * tracker music (module file) decoding support using libmikmod
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

#if defined(USE_CODEC_MIKMOD)
#include "snd_codec.h"
#include "snd_codeci.h"
#include "snd_mikmod.h"
#include <mikmod.h>

#if ((LIBMIKMOD_VERSION+0) < 0x030105)
#error libmikmod version is way too old and unusable.
#endif
#if (LIBMIKMOD_VERSION < 0x030107) /* ancient libmikmod */
#define S_MIKMOD_initlib(c) MikMod_Init()
#else
#define S_MIKMOD_initlib(c) MikMod_Init(c)
#endif

#ifndef DMODE_NOISEREDUCTION
#define DMODE_NOISEREDUCTION 0x1000 /* Low pass filtering */
#endif
#ifndef DMODE_SIMDMIXER
#define DMODE_SIMDMIXER 0x0800 /* enable SIMD mixing */
#endif

typedef struct _mik_priv {
/* struct MREADER in libmikmod <= 3.2.0-beta2
 * doesn't have iobase members. adding them here
 * so that if we compile against 3.2.0-beta2, we
 * can still run OK against 3.2.0b3 and newer. */
	struct MREADER reader;
	long iobase, prev_iobase;
	fshandle_t *fh;
	MODULE *module;
} mik_priv_t;

static int MIK_Seek (MREADER *r, long ofs, int whence)
{
	return FS_fseek(((mik_priv_t *)r)->fh, ofs, whence);
}

static long MIK_Tell (MREADER *r)
{
	return FS_ftell(((mik_priv_t *)r)->fh);
}

static BOOL MIK_Read (MREADER *r, void *ptr, size_t siz)
{
	return !!FS_fread(ptr, siz, 1, ((mik_priv_t *)r)->fh);
}

static int MIK_Get (MREADER *r)
{
	return FS_fgetc(((mik_priv_t *)r)->fh);
}

static BOOL MIK_Eof (MREADER *r)
{
	return FS_feof(((mik_priv_t *)r)->fh);
}

static qboolean S_MIKMOD_CodecInitialize (void)
{
	if (mikmod_codec.initialized)
		return true;

	/* set mode flags to only we like: */
	md_mode = 0;
	if ((shm->samplebits / 8) == 2)
		md_mode |= DMODE_16BITS;
	if (shm->channels == 2)
		md_mode |= DMODE_STEREO;
	md_mode |= DMODE_SOFT_MUSIC;	/* this is a software-only mixer */

	/* md_mixfreq is UWORD, so something like 96000 isn't OK */
	md_mixfreq = (shm->speed < 65536)? shm->speed : 48000;

	/* keeping md_device as 0 which is default (auto-detect: we
	 * only register drv_nos, and it will be the only one found.)
	 * md_pansep (stereo channels separation) default 128 is OK.
	 * no reverbation (md_reverb 0 (up to 15)) is OK.
	 * md_musicvolume and md_sndfxvolume defaults are 128: OK. */
	/* just tone down overall volume md_volume from 128 to 96? */
	md_volume = 96;

	MikMod_RegisterDriver(&drv_nos);	/* only need the "nosound" driver, none else */
	MikMod_RegisterAllLoaders();
	if (S_MIKMOD_initlib(NULL))
	{
		Con_DPrintf("Could not initialize MikMod: %s\n", MikMod_strerror(MikMod_errno));
		return false;
	}

	/* this can't get set with drv_nos, but whatever, be safe: */
	md_mode &= ~DMODE_SIMDMIXER;	/* SIMD mixer is buggy when combined with HQMIXER */

	mikmod_codec.initialized = true;
	return true;
}

static void S_MIKMOD_CodecShutdown (void)
{
	if (mikmod_codec.initialized)
	{
		mikmod_codec.initialized = false;
		MikMod_Exit();
	}
}

static qboolean S_MIKMOD_CodecOpenStream (snd_stream_t *stream)
{
	mik_priv_t *priv;

	stream->priv = Z_Malloc(sizeof(mik_priv_t));
	priv = (mik_priv_t *) stream->priv;
	priv->reader.Seek = MIK_Seek;
	priv->reader.Tell = MIK_Tell;
	priv->reader.Read = MIK_Read;
	priv->reader.Get  = MIK_Get;
	priv->reader.Eof  = MIK_Eof;
	priv->fh = &stream->fh;

	priv->module = Player_LoadGeneric((MREADER *)stream->priv, 64, 0);
	if (!priv->module)
	{
		Con_DPrintf("Could not load module: %s\n", MikMod_strerror(MikMod_errno));
		Z_Free(stream->priv);
		return false;
	}

	/* keep default values of fadeout (0: don't fade out volume during when last
	 * position of the module is being played), extspd (1: do process Protracker
	 * extended speed effect), panflag (1: do process panning effects), wrap (0:
	 * don't wrap to restart position when module is finished) are OK with us as
	 * set internally by libmikmod::Player_Init(). */
	/* just change the loop setting to 0, i.e. don't process in-module loops: */
	priv->module->loop	= 0;
	Player_Start(priv->module);

	stream->info.rate	= md_mixfreq;
	stream->info.bits	= (md_mode & DMODE_16BITS)? 16: 8;
	stream->info.width	= stream->info.bits / 8;
	stream->info.channels	= (md_mode & DMODE_STEREO)? 2 : 1;
/*	Con_DPrintf("Playing %s (%d chn)\n", priv->module->songname, priv->module->numchn);*/

	return true;
}

static int S_MIKMOD_CodecReadStream (snd_stream_t *stream, int bytes, void *buffer)
{
	if (!Player_Active())
		return 0;
	return (int) VC_WriteBytes((SBYTE *)buffer, bytes);
}

static void S_MIKMOD_CodecCloseStream (snd_stream_t *stream)
{
	Player_Stop();
	Player_Free(((mik_priv_t *)stream->priv)->module);
	Z_Free(stream->priv);
	S_CodecUtilClose(&stream);
}

static int S_MIKMOD_CodecRewindStream (snd_stream_t *stream)
{
	Player_SetPosition (0);
	return 0;
}

snd_codec_t mikmod_codec =
{
	CODECTYPE_MOD,
	false,
	"s3m",
	S_MIKMOD_CodecInitialize,
	S_MIKMOD_CodecShutdown,
	S_MIKMOD_CodecOpenStream,
	S_MIKMOD_CodecReadStream,
	S_MIKMOD_CodecRewindStream,
	S_MIKMOD_CodecCloseStream,
	NULL
};

#endif	/* USE_CODEC_MIKMOD */

