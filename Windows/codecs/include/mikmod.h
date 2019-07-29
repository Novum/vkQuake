/*  MikMod sound library
    (c) 1998-2014 Miodrag Vallat and others - see the AUTHORS file
    for complete list.

    This library is free software; you can redistribute it and/or modify
    it under the terms of the GNU Library General Public License as
    published by the Free Software Foundation; either version 2 of
    the License, or (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Library General Public License for more details.

    You should have received a copy of the GNU Library General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
    02111-1307, USA.
*/

/*==============================================================================

  MikMod sound library include file

  ==============================================================================*/

#ifndef _MIKMOD_H_
#define _MIKMOD_H_

#include <stdio.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ========== Compiler magic for shared libraries
 *
 * ========== NOTE TO WINDOWS DEVELOPERS:
 * If you are compiling for Windows and will link to the static library
 * (libmikmod.a with MinGW, or mikmod_static.lib with MSVC or LCC, etc),
 * you must define MIKMOD_STATIC in your project.  Otherwise, dllimport
 * will be assumed.
 */
#if defined(_WIN32) || defined(__CYGWIN__)
# if defined(MIKMOD_BUILD) && defined(DLL_EXPORT)       /* building libmikmod as a dll for windows */
#   define MIKMODAPI __declspec(dllexport)
# elif defined(MIKMOD_BUILD) || defined(MIKMOD_STATIC)  /* building or using static libmikmod for windows */
#   define MIKMODAPI
# else
#   define MIKMODAPI __declspec(dllimport)                      /* using libmikmod dll for windows */
# endif
#elif defined(__OS2__) && defined(__WATCOMC__)
# if defined(MIKMOD_BUILD) && defined(__SW_BD)          /* building libmikmod as a dll for os/2 */
#   define MIKMODAPI __declspec(dllexport)
# else
#   define MIKMODAPI                                    /* using dll or static libmikmod for os/2 */
# endif
/* SYM_VISIBILITY should be defined if both the compiler
 * and the target support the visibility attributes. the
 * configury does that automatically. for the standalone
 * makefiles, etc, the developer should add the required
 * flags, i.e.:  -DSYM_VISIBILITY -fvisibility=hidden  */
#elif defined(MIKMOD_BUILD) && defined(SYM_VISIBILITY)
#   define MIKMODAPI __attribute__((visibility("default")))
#else
#   define MIKMODAPI
#endif

/*
 *  ========== Library version
 */

#define LIBMIKMOD_VERSION_MAJOR 3L
#define LIBMIKMOD_VERSION_MINOR 3L
#define LIBMIKMOD_REVISION     11L

#define LIBMIKMOD_VERSION \
    ((LIBMIKMOD_VERSION_MAJOR<<16)| \
     (LIBMIKMOD_VERSION_MINOR<< 8)| \
     (LIBMIKMOD_REVISION))

MIKMODAPI extern long MikMod_GetVersion(void);

/*
 *  ========== Dependency platform headers
 */

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <io.h>
#include <mmsystem.h>
#define _MIKMOD_WIN32
#endif

#if defined(__DJGPP__) || defined(MSDOS) || defined(__MSDOS__) || defined(__DOS__)
#define _MIKMOD_DOS
#endif

#if defined(__OS2__) || defined(__EMX__)
#define INCL_DOSSEMAPHORES
#include <os2.h>
#include <io.h>
#define _MIKMOD_OS2
#endif

#if defined(__MORPHOS__) || defined(__AROS__) || defined(_AMIGA) || defined(__AMIGA__) || defined(__amigaos__) || defined(AMIGAOS)
#include <exec/types.h>
#define _MIKMOD_AMIGA
#endif

/*
 *  ========== Platform independent-type definitions
 * (pain when it comes to cross-platform maintenance..)
 */

#if !(defined(_MIKMOD_OS2) || defined(_MIKMOD_WIN32))
typedef char               CHAR;
#endif

/* BOOL:  0=false, <>0 true -- 16 bits on Amiga, int-wide on others. */
#if !(defined(_MIKMOD_OS2) || defined(_MIKMOD_WIN32) || defined(_MIKMOD_AMIGA))
typedef int                BOOL;
#endif

/* 1 byte, signed and unsigned: */
typedef signed char        SBYTE;
#ifndef _MIKMOD_AMIGA
typedef unsigned char      UBYTE;
#endif

/* 2 bytes, signed and unsigned: */
#if !(defined __LCC__ && defined _WIN32)
typedef signed short int   SWORD;
#endif
#if !((defined __LCC__ && defined _WIN32) || defined(_MIKMOD_AMIGA))
typedef unsigned short int UWORD;
#endif

/* 4 bytes, signed and unsigned: */
#if defined(_LP64) || defined(__LP64__) || defined(__arch64__) || defined(__alpha) || defined(__x86_64) || defined(__powerpc64__)
        /* 64 bit architectures: */
typedef signed int         SLONG;
#if !(defined(_WIN32) || defined(_MIKMOD_AMIGA))
typedef unsigned int       ULONG;
#endif

#else  /* 32 bit architectures: */
typedef signed long int    SLONG;
#if !(defined(_MIKMOD_OS2) || defined(_MIKMOD_WIN32) || defined(_MIKMOD_AMIGA))
typedef unsigned long int  ULONG;
#endif
#endif

/* make sure types are of correct sizes: */
typedef int __mikmod_typetest [
   (
        (sizeof(SBYTE)==1) && (sizeof(UBYTE)==1)
     && (sizeof(SWORD)==2) && (sizeof(UWORD)==2)
     && (sizeof(SLONG)==4) && (sizeof(ULONG)==4)
#ifndef _MIKMOD_AMIGA
     && (sizeof(BOOL) == sizeof(int))
#endif
     && (sizeof(CHAR) == sizeof(char))
   ) * 2 - 1 ];

/*
 *  ========== Error codes
 */

enum {
    MMERR_OPENING_FILE = 1,
    MMERR_OUT_OF_MEMORY,
    MMERR_DYNAMIC_LINKING,

    MMERR_SAMPLE_TOO_BIG,
    MMERR_OUT_OF_HANDLES,
    MMERR_UNKNOWN_WAVE_TYPE,

    MMERR_LOADING_PATTERN,
    MMERR_LOADING_TRACK,
    MMERR_LOADING_HEADER,
    MMERR_LOADING_SAMPLEINFO,
    MMERR_NOT_A_MODULE,
    MMERR_NOT_A_STREAM,
    MMERR_MED_SYNTHSAMPLES,
    MMERR_ITPACK_INVALID_DATA,

    MMERR_DETECTING_DEVICE,
    MMERR_INVALID_DEVICE,
    MMERR_INITIALIZING_MIXER,
    MMERR_OPENING_AUDIO,
    MMERR_8BIT_ONLY,
    MMERR_16BIT_ONLY,
    MMERR_STEREO_ONLY,
    MMERR_ULAW,
    MMERR_NON_BLOCK,

    MMERR_AF_AUDIO_PORT,

    MMERR_AIX_CONFIG_INIT,
    MMERR_AIX_CONFIG_CONTROL,
    MMERR_AIX_CONFIG_START,

    MMERR_GUS_SETTINGS,
    MMERR_GUS_RESET,
    MMERR_GUS_TIMER,

    MMERR_HP_SETSAMPLESIZE,
    MMERR_HP_SETSPEED,
    MMERR_HP_CHANNELS,
    MMERR_HP_AUDIO_OUTPUT,
    MMERR_HP_AUDIO_DESC,
    MMERR_HP_BUFFERSIZE,

    MMERR_OSS_SETFRAGMENT,
    MMERR_OSS_SETSAMPLESIZE,
    MMERR_OSS_SETSTEREO,
    MMERR_OSS_SETSPEED,

    MMERR_SGI_SPEED,
    MMERR_SGI_16BIT,
    MMERR_SGI_8BIT,
    MMERR_SGI_STEREO,
    MMERR_SGI_MONO,

    MMERR_SUN_INIT,

    MMERR_OS2_MIXSETUP,
    MMERR_OS2_SEMAPHORE,
    MMERR_OS2_TIMER,
    MMERR_OS2_THREAD,

    MMERR_DS_PRIORITY,
    MMERR_DS_BUFFER,
    MMERR_DS_FORMAT,
    MMERR_DS_NOTIFY,
    MMERR_DS_EVENT,
    MMERR_DS_THREAD,
    MMERR_DS_UPDATE,

    MMERR_WINMM_HANDLE,
    MMERR_WINMM_ALLOCATED,
    MMERR_WINMM_DEVICEID,
    MMERR_WINMM_FORMAT,
    MMERR_WINMM_UNKNOWN,

    MMERR_MAC_SPEED,
    MMERR_MAC_START,

    MMERR_OSX_UNKNOWN_DEVICE,
    MMERR_OSX_BAD_PROPERTY,
    MMERR_OSX_UNSUPPORTED_FORMAT,
    MMERR_OSX_SET_STEREO,
    MMERR_OSX_BUFFER_ALLOC,
    MMERR_OSX_ADD_IO_PROC,
    MMERR_OSX_DEVICE_START,
    MMERR_OSX_PTHREAD,

    MMERR_DOSWSS_STARTDMA,
    MMERR_DOSSB_STARTDMA,

    MMERR_NO_FLOAT32,/* should actually be after MMERR_ULAW or something */

    MMERR_OPENAL_CREATECTX,
    MMERR_OPENAL_CTXCURRENT,
    MMERR_OPENAL_GENBUFFERS,
    MMERR_OPENAL_GENSOURCES,
    MMERR_OPENAL_SOURCE,
    MMERR_OPENAL_QUEUEBUFFERS,
    MMERR_OPENAL_UNQUEUEBUFFERS,
    MMERR_OPENAL_BUFFERDATA,
    MMERR_OPENAL_GETSOURCE,
    MMERR_OPENAL_SOURCEPLAY,
    MMERR_OPENAL_SOURCESTOP,

    MMERR_ALSA_NOCONFIG,
    MMERR_ALSA_SETPARAMS,
    MMERR_ALSA_SETFORMAT,
    MMERR_ALSA_SETRATE,
    MMERR_ALSA_SETCHANNELS,
    MMERR_ALSA_BUFFERSIZE,
    MMERR_ALSA_PCM_START,
    MMERR_ALSA_PCM_WRITE,
    MMERR_ALSA_PCM_RECOVER,

    MMERR_SNDIO_SETPARAMS,
    MMERR_SNDIO_BADPARAMS,

    MMERR_MAX
};

/*
 *  ========== Error handling
 */

typedef void (MikMod_handler)(void);
typedef MikMod_handler *MikMod_handler_t;

MIKMODAPI extern int  MikMod_errno;
MIKMODAPI extern BOOL MikMod_critical;
MIKMODAPI extern const char *MikMod_strerror(int);

MIKMODAPI extern MikMod_handler_t MikMod_RegisterErrorHandler(MikMod_handler_t);

/*
 *  ========== Library initialization and core functions
 */

struct MDRIVER;

MIKMODAPI extern void   MikMod_RegisterAllDrivers(void);

MIKMODAPI extern CHAR*  MikMod_InfoDriver(void);
MIKMODAPI extern void   MikMod_RegisterDriver(struct MDRIVER*);
MIKMODAPI extern int    MikMod_DriverFromAlias(const CHAR*);
MIKMODAPI extern struct MDRIVER *MikMod_DriverByOrdinal(int);

MIKMODAPI extern int    MikMod_Init(const CHAR*);
MIKMODAPI extern void   MikMod_Exit(void);
MIKMODAPI extern int    MikMod_Reset(const CHAR*);
MIKMODAPI extern int    MikMod_SetNumVoices(int,int);
MIKMODAPI extern BOOL   MikMod_Active(void);
MIKMODAPI extern int    MikMod_EnableOutput(void);
MIKMODAPI extern void   MikMod_DisableOutput(void);
MIKMODAPI extern void   MikMod_Update(void);

MIKMODAPI extern BOOL   MikMod_InitThreads(void);
MIKMODAPI extern void   MikMod_Lock(void);
MIKMODAPI extern void   MikMod_Unlock(void);

MIKMODAPI extern void*  MikMod_malloc(size_t);
MIKMODAPI extern void*  MikMod_calloc(size_t,size_t);
MIKMODAPI extern void*  MikMod_realloc(void*,size_t);
MIKMODAPI extern CHAR*  MikMod_strdup(const CHAR*);
MIKMODAPI extern void   MikMod_free(void*);  /* frees if ptr != NULL */

/*
 *  ========== Reader, Writer
 */

typedef struct MREADER {
    int  (*Seek)(struct MREADER*,long,int);
    long (*Tell)(struct MREADER*);
    BOOL (*Read)(struct MREADER*,void*,size_t);
    int  (*Get)(struct MREADER*);
    BOOL (*Eof)(struct MREADER*);
    long iobase;
    long prev_iobase;
} MREADER;

typedef struct MWRITER {
    int  (*Seek)(struct MWRITER*, long, int);
    long (*Tell)(struct MWRITER*);
    BOOL (*Write)(struct MWRITER*, const void*, size_t);
    int  (*Put)(struct MWRITER*, int);
} MWRITER;

/*
 *  ========== Samples
 */

/* Sample playback should not be interrupted */
#define SFX_CRITICAL 1

/* Sample format [loading and in-memory] flags: */
#define SF_16BITS       0x0001
#define SF_STEREO       0x0002
#define SF_SIGNED       0x0004
#define SF_BIG_ENDIAN   0x0008
#define SF_DELTA        0x0010
#define SF_ITPACKED     0x0020

#define SF_FORMATMASK   0x003F

/* General Playback flags */

#define SF_LOOP         0x0100
#define SF_BIDI         0x0200
#define SF_REVERSE      0x0400
#define SF_SUSTAIN      0x0800

#define SF_PLAYBACKMASK 0x0C00

/* Module-only Playback Flags */

#define SF_OWNPAN       0x1000
#define SF_UST_LOOP     0x2000

#define SF_EXTRAPLAYBACKMASK    0x3000

/* Panning constants */
#define PAN_LEFT        0
#define PAN_HALFLEFT    64
#define PAN_CENTER      128
#define PAN_HALFRIGHT   192
#define PAN_RIGHT       255
#define PAN_SURROUND    512 /* panning value for Dolby Surround */

typedef struct SAMPLE {
    SWORD  panning;     /* panning (0-255 or PAN_SURROUND) */
    ULONG  speed;       /* Base playing speed/frequency of note */
    UBYTE  volume;      /* volume 0-64 */
    UWORD  inflags;     /* sample format on disk */
    UWORD  flags;       /* sample format in memory */
    ULONG  length;      /* length of sample (in samples!) */
    ULONG  loopstart;   /* repeat position (relative to start, in samples) */
    ULONG  loopend;     /* repeat end */
    ULONG  susbegin;    /* sustain loop begin (in samples) \  Not Supported */
    ULONG  susend;      /* sustain loop end                /      Yet! */

    /* Variables used by the module player only! (ignored for sound effects) */
    UBYTE  globvol;     /* global volume */
    UBYTE  vibflags;    /* autovibrato flag stuffs */
    UBYTE  vibtype;     /* Vibratos moved from INSTRUMENT to SAMPLE */
    UBYTE  vibsweep;
    UBYTE  vibdepth;
    UBYTE  vibrate;
    CHAR*  samplename;  /* name of the sample */

    /* Values used internally only */
    UWORD  avibpos;     /* autovibrato pos [player use] */
    UBYTE  divfactor;   /* for sample scaling, maintains proper period slides */
    ULONG  seekpos;     /* seek position in file */
    SWORD  handle;      /* sample handle used by individual drivers */
    void (*onfree)(void *ctx); /* called from Sample_Free if not NULL */
    void *ctx;          /* context passed to previous function*/
} SAMPLE;

/* Sample functions */

MIKMODAPI extern SAMPLE *Sample_LoadRaw(const CHAR *,ULONG rate, ULONG channel, ULONG flags);
MIKMODAPI extern SAMPLE *Sample_LoadRawFP(FILE *fp,ULONG rate,ULONG channel, ULONG flags);
MIKMODAPI extern SAMPLE *Sample_LoadRawMem(const char *buf, int len, ULONG rate, ULONG channel, ULONG flags);
MIKMODAPI extern SAMPLE *Sample_LoadRawGeneric(MREADER*reader,ULONG rate, ULONG channel, ULONG flags);

MIKMODAPI extern SAMPLE *Sample_Load(const CHAR*);
MIKMODAPI extern SAMPLE *Sample_LoadFP(FILE*);
MIKMODAPI extern SAMPLE *Sample_LoadMem(const char *buf, int len);
MIKMODAPI extern SAMPLE *Sample_LoadGeneric(MREADER*);
MIKMODAPI extern void   Sample_Free(SAMPLE*);
MIKMODAPI extern SBYTE  Sample_Play(SAMPLE*,ULONG,UBYTE);

MIKMODAPI extern void   Voice_SetVolume(SBYTE,UWORD);
MIKMODAPI extern UWORD  Voice_GetVolume(SBYTE);
MIKMODAPI extern void   Voice_SetFrequency(SBYTE,ULONG);
MIKMODAPI extern ULONG  Voice_GetFrequency(SBYTE);
MIKMODAPI extern void   Voice_SetPanning(SBYTE,ULONG);
MIKMODAPI extern ULONG  Voice_GetPanning(SBYTE);
MIKMODAPI extern void   Voice_Play(SBYTE,SAMPLE*,ULONG);
MIKMODAPI extern void   Voice_Stop(SBYTE);
MIKMODAPI extern BOOL   Voice_Stopped(SBYTE);
MIKMODAPI extern SLONG  Voice_GetPosition(SBYTE);
MIKMODAPI extern ULONG  Voice_RealVolume(SBYTE);

/*
 *  ========== Internal module representation (UniMod)
 */

/*
    Instrument definition - for information only, the only field which may be
    of use in user programs is the name field
*/

/* Instrument note count */
#define INSTNOTES 120

/* Envelope point */
typedef struct ENVPT {
    SWORD pos;
    SWORD val;
} ENVPT;

/* Envelope point count */
#define ENVPOINTS 32

/* Instrument structure */
typedef struct INSTRUMENT {
    CHAR* insname;

    UBYTE flags;
    UWORD samplenumber[INSTNOTES];
    UBYTE samplenote[INSTNOTES];

    UBYTE nnatype;
    UBYTE dca;              /* duplicate check action */
    UBYTE dct;              /* duplicate check type */
    UBYTE globvol;
    UWORD volfade;
    SWORD panning;          /* instrument-based panning var */

    UBYTE pitpansep;        /* pitch pan separation (0 to 255) */
    UBYTE pitpancenter;     /* pitch pan center (0 to 119) */
    UBYTE rvolvar;          /* random volume varations (0 - 100%) */
    UBYTE rpanvar;          /* random panning varations (0 - 100%) */

    /* volume envelope */
    UBYTE volflg;           /* bit 0: on 1: sustain 2: loop */
    UBYTE volpts;
    UBYTE volsusbeg;
    UBYTE volsusend;
    UBYTE volbeg;
    UBYTE volend;
    ENVPT volenv[ENVPOINTS];
    /* panning envelope */
    UBYTE panflg;           /* bit 0: on 1: sustain 2: loop */
    UBYTE panpts;
    UBYTE pansusbeg;
    UBYTE pansusend;
    UBYTE panbeg;
    UBYTE panend;
    ENVPT panenv[ENVPOINTS];
    /* pitch envelope */
    UBYTE pitflg;           /* bit 0: on 1: sustain 2: loop */
    UBYTE pitpts;
    UBYTE pitsusbeg;
    UBYTE pitsusend;
    UBYTE pitbeg;
    UBYTE pitend;
    ENVPT pitenv[ENVPOINTS];
} INSTRUMENT;

struct MP_CONTROL;
struct MP_VOICE;

/*
    Module definition
*/

/* maximum master channels supported */
#define UF_MAXCHAN      64

/* Module flags */
#define UF_XMPERIODS    0x0001 /* XM periods / finetuning */
#define UF_LINEAR       0x0002 /* LINEAR periods (UF_XMPERIODS must be set) */
#define UF_INST         0x0004 /* Instruments are used */
#define UF_NNA          0x0008 /* IT: NNA used, set numvoices rather
                                  than numchn */
#define UF_S3MSLIDES    0x0010 /* uses old S3M volume slides */
#define UF_BGSLIDES     0x0020 /* continue volume slides in the background */
#define UF_HIGHBPM      0x0040 /* MED: can use >255 bpm */
#define UF_NOWRAP       0x0080 /* XM-type (i.e. illogical) pattern break
                                  semantics */
#define UF_ARPMEM       0x0100 /* IT: need arpeggio memory */
#define UF_FT2QUIRKS    0x0200 /* emulate some FT2 replay quirks */
#define UF_PANNING      0x0400 /* module uses panning effects or have
                                  non-tracker default initial panning */

typedef struct MODULE {
 /* general module information */
    CHAR*       songname;    /* name of the song */
    CHAR*       modtype;     /* string type of module loaded */
    CHAR*       comment;     /* module comments */

    UWORD       flags;       /* See module flags above */
    UBYTE       numchn;      /* number of module channels */
    UBYTE       numvoices;   /* max # voices used for full NNA playback */
    UWORD       numpos;      /* number of positions in this song */
    UWORD       numpat;      /* number of patterns in this song */
    UWORD       numins;      /* number of instruments */
    UWORD       numsmp;      /* number of samples */

    struct INSTRUMENT* instruments; /* all instruments */
    struct SAMPLE*     samples;     /* all samples */

    UBYTE       realchn;     /* real number of channels used */
    UBYTE       totalchn;    /* total number of channels used (incl NNAs) */

 /* playback settings */
    UWORD       reppos;      /* restart position */
    UBYTE       initspeed;   /* initial song speed */
    UWORD       inittempo;   /* initial song tempo */
    UBYTE       initvolume;  /* initial global volume (0 - 128) */
    UWORD       panning[UF_MAXCHAN]; /* panning positions */
    UBYTE       chanvol[UF_MAXCHAN]; /* channel positions */
    UWORD       bpm;         /* current beats-per-minute speed */
    UWORD       sngspd;      /* current song speed */
    SWORD       volume;      /* song volume (0-128) (or user volume) */

    BOOL        extspd;      /* extended speed flag (default enabled) */
    BOOL        panflag;     /* panning flag (default enabled) */
    BOOL        wrap;        /* wrap module ? (default disabled) */
    BOOL        loop;        /* allow module to loop ? (default enabled) */
    BOOL        fadeout;     /* volume fade out during last pattern */

    UWORD       patpos;      /* current row number */
    SWORD       sngpos;      /* current song position */
    ULONG       sngtime;     /* current song time in 2^-10 seconds */

    SWORD       relspd;      /* relative speed factor */

 /* internal module representation */
    UWORD       numtrk;      /* number of tracks */
    UBYTE**     tracks;      /* array of numtrk pointers to tracks */
    UWORD*      patterns;    /* array of Patterns */
    UWORD*      pattrows;    /* array of number of rows for each pattern */
    UWORD*      positions;   /* all positions */

    BOOL        forbid;      /* if true, no player update! */
    UWORD       numrow;      /* number of rows on current pattern */
    UWORD       vbtick;      /* tick counter (counts from 0 to sngspd) */
    UWORD       sngremainder;/* used for song time computation */

    struct MP_CONTROL* control; /* Effects Channel info (size pf->numchn) */
    struct MP_VOICE*   voice;   /* Audio Voice information (size md_numchn) */

    UBYTE       globalslide; /* global volume slide rate */
    UBYTE       pat_repcrazy;/* module has just looped to position -1 */
    UWORD       patbrk;      /* position where to start a new pattern */
    UBYTE       patdly;      /* patterndelay counter (command memory) */
    UBYTE       patdly2;     /* patterndelay counter (real one) */
    SWORD       posjmp;      /* flag to indicate a jump is needed... */
    UWORD       bpmlimit;    /* threshold to detect bpm or speed values */
} MODULE;


/* This structure is used to query current playing voices status */
typedef struct VOICEINFO {
    INSTRUMENT* i;            /* Current channel instrument */
    SAMPLE*     s;            /* Current channel sample */
    SWORD       panning;      /* panning position */
    SBYTE       volume;       /* channel's "global" volume (0..64) */
    UWORD       period;       /* period to play the sample at */
    UBYTE       kick;         /* if true = sample has been restarted */
} VOICEINFO;

/*
 *  ========== Module loaders
 */

struct MLOADER;

MIKMODAPI extern CHAR*   MikMod_InfoLoader(void);
MIKMODAPI extern void    MikMod_RegisterAllLoaders(void);
MIKMODAPI extern void    MikMod_RegisterLoader(struct MLOADER*);

MIKMODAPI extern struct MLOADER load_669; /* 669 and Extended-669 (by Tran/Renaissance) */
MIKMODAPI extern struct MLOADER load_amf; /* DMP Advanced Module Format (by Otto Chrons) */
MIKMODAPI extern struct MLOADER load_asy; /* ASYLUM Music Format 1.0 */
MIKMODAPI extern struct MLOADER load_dsm; /* DSIK internal module format */
MIKMODAPI extern struct MLOADER load_far; /* Farandole Composer (by Daniel Potter) */
MIKMODAPI extern struct MLOADER load_gdm; /* General DigiMusic (by Edward Schlunder) */
MIKMODAPI extern struct MLOADER load_gt2; /* Graoumf tracker */
MIKMODAPI extern struct MLOADER load_it;  /* Impulse Tracker (by Jeffrey Lim) */
MIKMODAPI extern struct MLOADER load_imf; /* Imago Orpheus (by Lutz Roeder) */
MIKMODAPI extern struct MLOADER load_med; /* Amiga MED modules (by Teijo Kinnunen) */
MIKMODAPI extern struct MLOADER load_m15; /* Soundtracker 15-instrument */
MIKMODAPI extern struct MLOADER load_mod; /* Standard 31-instrument Module loader */
MIKMODAPI extern struct MLOADER load_mtm; /* Multi-Tracker Module (by Renaissance) */
MIKMODAPI extern struct MLOADER load_okt; /* Amiga Oktalyzer */
MIKMODAPI extern struct MLOADER load_stm; /* ScreamTracker 2 (by Future Crew) */
MIKMODAPI extern struct MLOADER load_stx; /* STMIK 0.2 (by Future Crew) */
MIKMODAPI extern struct MLOADER load_s3m; /* ScreamTracker 3 (by Future Crew) */
MIKMODAPI extern struct MLOADER load_ult; /* UltraTracker (by MAS) */
MIKMODAPI extern struct MLOADER load_umx; /* Unreal UMX container of Epic Games */
MIKMODAPI extern struct MLOADER load_uni; /* MikMod and APlayer internal module format */
MIKMODAPI extern struct MLOADER load_xm;  /* FastTracker 2 (by Triton) */

/*
 *  ========== Module player
 */

MIKMODAPI extern MODULE* Player_Load(const CHAR*,int,BOOL);
MIKMODAPI extern MODULE* Player_LoadFP(FILE*,int,BOOL);
MIKMODAPI extern MODULE* Player_LoadMem(const char *buffer,int len,int maxchan,BOOL curious);
MIKMODAPI extern MODULE* Player_LoadGeneric(MREADER*,int,BOOL);
MIKMODAPI extern CHAR*   Player_LoadTitle(const CHAR*);
MIKMODAPI extern CHAR*   Player_LoadTitleFP(FILE*);
MIKMODAPI extern CHAR*   Player_LoadTitleMem(const char *buffer,int len);
MIKMODAPI extern CHAR*   Player_LoadTitleGeneric(MREADER*);

MIKMODAPI extern void    Player_Free(MODULE*);
MIKMODAPI extern void    Player_Start(MODULE*);
MIKMODAPI extern BOOL    Player_Active(void);
MIKMODAPI extern void    Player_Stop(void);
MIKMODAPI extern void    Player_TogglePause(void);
MIKMODAPI extern BOOL    Player_Paused(void);
MIKMODAPI extern void    Player_NextPosition(void);
MIKMODAPI extern void    Player_PrevPosition(void);
MIKMODAPI extern void    Player_SetPosition(UWORD);
MIKMODAPI extern BOOL    Player_Muted(UBYTE);
MIKMODAPI extern void    Player_SetVolume(SWORD);
MIKMODAPI extern MODULE* Player_GetModule(void);
MIKMODAPI extern void    Player_SetSpeed(UWORD);
MIKMODAPI extern void    Player_SetTempo(UWORD);
MIKMODAPI extern void    Player_Unmute(SLONG,...);
MIKMODAPI extern void    Player_Mute(SLONG,...);
MIKMODAPI extern void    Player_ToggleMute(SLONG,...);
MIKMODAPI extern int     Player_GetChannelVoice(UBYTE);
MIKMODAPI extern UWORD   Player_GetChannelPeriod(UBYTE);
MIKMODAPI extern int     Player_QueryVoices(UWORD numvoices, VOICEINFO *vinfo);
MIKMODAPI extern int     Player_GetRow(void);
MIKMODAPI extern int     Player_GetOrder(void);

typedef void (*MikMod_player_t)(void);
typedef void (*MikMod_callback_t)(unsigned char *data, size_t len);

MIKMODAPI extern MikMod_player_t MikMod_RegisterPlayer(MikMod_player_t);

#define MUTE_EXCLUSIVE  32000
#define MUTE_INCLUSIVE  32001

/*
 *  ========== Drivers
 */

enum {
    MD_MUSIC = 0,
    MD_SNDFX
};

enum {
    MD_HARDWARE = 0,
    MD_SOFTWARE
};

/* Mixing flags */

/* These ones take effect only after MikMod_Init or MikMod_Reset */
#define DMODE_16BITS     0x0001 /* enable 16 bit output */
#define DMODE_STEREO     0x0002 /* enable stereo output */
#define DMODE_SOFT_SNDFX 0x0004 /* Process sound effects via software mixer */
#define DMODE_SOFT_MUSIC 0x0008 /* Process music via software mixer */
#define DMODE_HQMIXER    0x0010 /* Use high-quality (slower) software mixer */
#define DMODE_FLOAT      0x0020 /* enable float output */
/* These take effect immediately. */
#define DMODE_SURROUND   0x0100 /* enable surround sound */
#define DMODE_INTERP     0x0200 /* enable interpolation */
#define DMODE_REVERSE    0x0400 /* reverse stereo */
#define DMODE_SIMDMIXER  0x0800 /* enable SIMD mixing */
#define DMODE_NOISEREDUCTION 0x1000 /* Low pass filtering */


struct SAMPLOAD;

typedef struct MDRIVER {
    struct MDRIVER* next;
    const CHAR* Name;
    const CHAR* Version;

    UBYTE       HardVoiceLimit; /* Limit of hardware mixer voices */
    UBYTE       SoftVoiceLimit; /* Limit of software mixer voices */

    const CHAR* Alias;
    const CHAR* CmdLineHelp;

    void        (*CommandLine)      (const CHAR*);
    BOOL        (*IsPresent)        (void);
    SWORD       (*SampleLoad)       (struct SAMPLOAD*,int);
    void        (*SampleUnload)     (SWORD);
    ULONG       (*FreeSampleSpace)  (int);
    ULONG       (*RealSampleLength) (int,struct SAMPLE*);
    int         (*Init)             (void);
    void        (*Exit)             (void);
    int         (*Reset)            (void);
    int         (*SetNumVoices)     (void);
    int         (*PlayStart)        (void);
    void        (*PlayStop)         (void);
    void        (*Update)           (void);
    void        (*Pause)            (void);
    void        (*VoiceSetVolume)   (UBYTE,UWORD);
    UWORD       (*VoiceGetVolume)   (UBYTE);
    void        (*VoiceSetFrequency)(UBYTE,ULONG);
    ULONG       (*VoiceGetFrequency)(UBYTE);
    void        (*VoiceSetPanning)  (UBYTE,ULONG);
    ULONG       (*VoiceGetPanning)  (UBYTE);
    void        (*VoicePlay)        (UBYTE,SWORD,ULONG,ULONG,ULONG,ULONG,UWORD);
    void        (*VoiceStop)        (UBYTE);
    BOOL        (*VoiceStopped)     (UBYTE);
    SLONG       (*VoiceGetPosition) (UBYTE);
    ULONG       (*VoiceRealVolume)  (UBYTE);
} MDRIVER;

/* These variables can be changed at ANY time and results will be immediate */
MIKMODAPI extern UBYTE md_volume;      /* global sound volume (0-128) */
MIKMODAPI extern UBYTE md_musicvolume; /* volume of song */
MIKMODAPI extern UBYTE md_sndfxvolume; /* volume of sound effects */
MIKMODAPI extern UBYTE md_reverb;      /* 0 = none;  15 = chaos */
MIKMODAPI extern UBYTE md_pansep;      /* 0 = mono;  128 == 100% (full left/right) */

/* The variables below can be changed at any time, but changes will not be
   implemented until MikMod_Reset is called. A call to MikMod_Reset may result
   in a skip or pop in audio (depending on the soundcard driver and the settings
   changed). */
MIKMODAPI extern UWORD md_device;      /* device */
MIKMODAPI extern UWORD md_mixfreq;     /* mixing frequency */
MIKMODAPI extern UWORD md_mode;        /* mode. See DMODE_? flags above */

/* The following variable should not be changed! */
MIKMODAPI extern MDRIVER* md_driver;   /* Current driver in use. */

/* Known drivers list */

MIKMODAPI extern struct MDRIVER drv_nos;    /* no sound */
MIKMODAPI extern struct MDRIVER drv_pipe;   /* piped output */
MIKMODAPI extern struct MDRIVER drv_raw;    /* raw file disk writer [music.raw] */
MIKMODAPI extern struct MDRIVER drv_stdout; /* output to stdout */
MIKMODAPI extern struct MDRIVER drv_wav;    /* RIFF WAVE file disk writer [music.wav] */
MIKMODAPI extern struct MDRIVER drv_aiff;   /* AIFF file disk writer [music.aiff] */

MIKMODAPI extern struct MDRIVER drv_ultra;  /* Linux Ultrasound driver */
MIKMODAPI extern struct MDRIVER drv_sam9407;/* Linux sam9407 driver */

MIKMODAPI extern struct MDRIVER drv_AF;     /* Dec Alpha AudioFile */
MIKMODAPI extern struct MDRIVER drv_ahi;    /* Amiga AHI */
MIKMODAPI extern struct MDRIVER drv_aix;    /* AIX audio device */
MIKMODAPI extern struct MDRIVER drv_alsa;   /* Advanced Linux Sound Architecture (ALSA) */
MIKMODAPI extern struct MDRIVER drv_esd;    /* Enlightened sound daemon (EsounD) */
MIKMODAPI extern struct MDRIVER drv_pulseaudio; /* PulseAudio  */
MIKMODAPI extern struct MDRIVER drv_hp;     /* HP-UX audio device */
MIKMODAPI extern struct MDRIVER drv_nas;    /* Network Audio System (NAS) */
MIKMODAPI extern struct MDRIVER drv_oss;    /* OpenSound System (Linux,FreeBSD...) */
MIKMODAPI extern struct MDRIVER drv_openal; /* OpenAL driver */
MIKMODAPI extern struct MDRIVER drv_sdl;    /* SDL audio driver */
MIKMODAPI extern struct MDRIVER drv_sgi;    /* SGI audio library */
MIKMODAPI extern struct MDRIVER drv_sndio;  /* OpenBSD sndio */
MIKMODAPI extern struct MDRIVER drv_sun;    /* Sun/NetBSD/OpenBSD audio device */

MIKMODAPI extern struct MDRIVER drv_dart;   /* OS/2 Direct Audio RealTime */
MIKMODAPI extern struct MDRIVER drv_os2;    /* OS/2 MMPM/2 */

MIKMODAPI extern struct MDRIVER drv_ds;     /* Win32 DirectSound driver */
MIKMODAPI extern struct MDRIVER drv_xaudio2;/* Win32 XAudio2 driver */
MIKMODAPI extern struct MDRIVER drv_win;    /* Win32 multimedia API driver */

MIKMODAPI extern struct MDRIVER drv_mac;    /* Macintosh Sound Manager driver */
MIKMODAPI extern struct MDRIVER drv_osx;    /* MacOS X CoreAudio Driver */

MIKMODAPI extern struct MDRIVER drv_dc;     /* Dreamcast driver */
MIKMODAPI extern struct MDRIVER drv_gp32;   /* GP32 Sound driver */
MIKMODAPI extern struct MDRIVER drv_psp;    /* PlayStation Portable driver */

MIKMODAPI extern struct MDRIVER drv_wss;    /* DOS WSS driver */
MIKMODAPI extern struct MDRIVER drv_sb;     /* DOS S/B driver */

MIKMODAPI extern struct MDRIVER drv_osles;  /* OpenSL ES driver for android */

/*========== Virtual channel mixer interface (for user-supplied drivers only) */

MIKMODAPI extern int   VC_Init(void);
MIKMODAPI extern void  VC_Exit(void);
MIKMODAPI extern void  VC_SetCallback(MikMod_callback_t callback);
MIKMODAPI extern int   VC_SetNumVoices(void);
MIKMODAPI extern ULONG VC_SampleSpace(int);
MIKMODAPI extern ULONG VC_SampleLength(int,SAMPLE*);

MIKMODAPI extern int   VC_PlayStart(void);
MIKMODAPI extern void  VC_PlayStop(void);

MIKMODAPI extern SWORD VC_SampleLoad(struct SAMPLOAD*,int);
MIKMODAPI extern void  VC_SampleUnload(SWORD);

MIKMODAPI extern ULONG VC_WriteBytes(SBYTE*,ULONG);
MIKMODAPI extern ULONG VC_SilenceBytes(SBYTE*,ULONG);

MIKMODAPI extern void  VC_VoiceSetVolume(UBYTE,UWORD);
MIKMODAPI extern UWORD VC_VoiceGetVolume(UBYTE);
MIKMODAPI extern void  VC_VoiceSetFrequency(UBYTE,ULONG);
MIKMODAPI extern ULONG VC_VoiceGetFrequency(UBYTE);
MIKMODAPI extern void  VC_VoiceSetPanning(UBYTE,ULONG);
MIKMODAPI extern ULONG VC_VoiceGetPanning(UBYTE);
MIKMODAPI extern void  VC_VoicePlay(UBYTE,SWORD,ULONG,ULONG,ULONG,ULONG,UWORD);

MIKMODAPI extern void  VC_VoiceStop(UBYTE);
MIKMODAPI extern BOOL  VC_VoiceStopped(UBYTE);
MIKMODAPI extern SLONG VC_VoiceGetPosition(UBYTE);
MIKMODAPI extern ULONG VC_VoiceRealVolume(UBYTE);

#ifdef __cplusplus
}
#endif

#endif

/* ex:set ts=4: */
