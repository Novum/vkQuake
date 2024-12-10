/*
Copyright (C) 1996-2001 Id Software, Inc.
Copyright (C) 2002-2009 John Fitzgibbons and others
Copyright (C) 2007-2008 Kristian Duske
Copyright (C) 2010-2021 QuakeSpasm developers
Copyright (C) 2016-2021 vkQuake developers

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
#ifndef QUAKEDEFS_H
#define QUAKEDEFS_H

#include "quakever.h"

// quakedef.h -- primary header for client

#define QUAKE_GAME // as opposed to utilities

#define CONFIG_NAME "vkQuake.cfg"

// define	PARANOID			// speed sapping error checking

#define GAMENAME "id1" // directory to look in by default

#define PSET_SCRIPT			   // enable the scriptable particle system (poorly ported from FTE)
#define PSET_SCRIPT_EFFECTINFO // scripted particle system can load dp's effects

#define LERP_BANDAID // HACK: send think interval over FTE protocol (loopback only, no demos)

#define BSP29_VALVE // enable Half-Life map support

#include "q_stdinc.h"

// !!! if this is changed, it must be changed in d_ifacea.h too !!!
#define CACHE_SIZE 32 // used to align key data structures

#define Q_UNUSED(x) (x = x) // for pesky compiler / lint warnings

#define MAX_NUM_ARGVS 50

// up / down
#define PITCH 0

// left / right
#define YAW 1

// fall over
#define ROLL 2

#define MAX_QPATH 64 // max length of a quake game pathname

#define ON_EPSILON 0.1 // point on plane side epsilon

#define DIST_EPSILON (0.03125) // 1/32 epsilon to keep floating point happy (moved from world.c)

#define MAX_PHYSICS_FREQ (71.9990) // Physics beyond 72Hz is broken, use a slightly lower value to overcome some bugs

#define MAX_MSGLEN	 64000 // max length of a reliable message //ericw -- was 32000
#define MAX_DATAGRAM 64000 // max length of unreliable message //johnfitz -- was 1024

#define DATAGRAM_MTU 1400 // johnfitz -- actual limit for unreliable messages to nonlocal clients

//
// per-level limits
//
#define MIN_EDICTS 256	 // johnfitz -- lowest allowed value for max_edicts cvar
#define MAX_EDICTS 32000 // johnfitz -- highest allowed value for max_edicts cvar

// never re-use free edicts younger than MIN_EDICT_AGE_FOR_REUSE (age = qcvm->time - e->freetime)
#define MIN_EDICT_AGE_FOR_REUSE 2.0

// Special case at loading savegame : it generates a ton of trensient allocs by spawn,
// so use freetime to detect this situation and allow immediate reuse, NOT using MIN_EDICT_AGE_FOR_REUSE
// criteria in this case.
#define MAX_EDICT_FREETIME_ALWAYS_REUSE 2.0

// ents past 8192 can't play sounds in the standard protocol
#define MAX_LIGHTSTYLES	  64
#define MAX_MODELS		  4096 // johnfitz -- was 256
#define MAX_SOUNDS		  2048 // johnfitz -- was 256
#define MAX_PARTICLETYPES 2048

#define SAVEGAME_COMMENT_LENGTH 39

#define MAX_STYLESTRING 64

//
// stats are integers communicated to the client by the server
//
// clang-format off
typedef enum
{
	MAX_CL_BASE_STATS	= 32,
	MAX_CL_STATS		= 256,

	STAT_HEALTH			= 0,
	STAT_FRAGS			= 1,
	STAT_WEAPON			= 2,
	STAT_AMMO			= 3,
	STAT_ARMOR			= 4,
	STAT_WEAPONFRAME	= 5,
	STAT_SHELLS			= 6,
	STAT_NAILS			= 7,
	STAT_ROCKETS		= 8,
	STAT_CELLS			= 9,
	STAT_ACTIVEWEAPON	= 10,
	STAT_NONCLIENT		= 11,	// first stat not included in svc_clientdata
	STAT_TOTALSECRETS	= 11,
	STAT_TOTALMONSTERS	= 12,
	STAT_SECRETS		= 13,	// bumped on client side by svc_foundsecret
	STAT_MONSTERS		= 14,	// bumped by svc_killedmonster
	STAT_ITEMS			= 15,	//replaces clc_clientdata info
	STAT_VIEWHEIGHT		= 16, // replaces clc_clientdata info
	STAT_VIEWZOOM		= 21, // DP
	STAT_IDEALPITCH		= 25, // nq-emu
	STAT_PUNCHANGLE_X	= 26, // nq-emu
	STAT_PUNCHANGLE_Y	= 27, // nq-emu
	STAT_PUNCHANGLE_Z	= 28, // nq-emu
} stat_t;

// stock defines
//
typedef enum
{
	IT_SHOTGUN			= 1,
	IT_SUPER_SHOTGUN	= 2,
	IT_NAILGUN			= 4,
	IT_SUPER_NAILGUN	= 8,
	IT_GRENADE_LAUNCHER	= 16,
	IT_ROCKET_LAUNCHER	= 32,
	IT_LIGHTNING		= 64,
	IT_SUPER_LIGHTNING	= 128,
	IT_SHELLS			= 256,
	IT_NAILS			= 512,
	IT_ROCKETS			= 1024,
	IT_CELLS			= 2048,
	IT_AXE				= 4096,
	IT_ARMOR1			= 8192,
	IT_ARMOR2			= 16384,
	IT_ARMOR3			= 32768,
	IT_SUPERHEALTH		= 65536,
	IT_KEY1				= 131072,
	IT_KEY2				= 262144,
	IT_INVISIBILITY		= 524288,
	IT_INVULNERABILITY	= 1048576,
	IT_SUIT				= 2097152,
	IT_QUAD				= 4194304,
	IT_SIGIL1			= (1<<28),
	IT_SIGIL2			= (1<<29),
	IT_SIGIL3			= (1<<30),
	IT_SIGIL4			= (1<<31),
} items_t;

//===========================================
// rogue changed and added defines

typedef enum
{
	RIT_SHELLS				= 128,
	RIT_NAILS				= 256,
	RIT_ROCKETS				= 512,
	RIT_CELLS				= 1024,
	RIT_AXE					= 2048,
	RIT_LAVA_NAILGUN		= 4096,
	RIT_LAVA_SUPER_NAILGUN	= 8192,
	RIT_MULTI_GRENADE		= 16384,
	RIT_MULTI_ROCKET		= 32768,
	RIT_PLASMA_GUN			= 65536,
	RIT_ARMOR1				= 8388608,
	RIT_ARMOR2				= 16777216,
	RIT_ARMOR3				= 33554432,
	RIT_LAVA_NAILS			= 67108864,
	RIT_PLASMA_AMMO			= 134217728,
	RIT_MULTI_ROCKETS		= 268435456,
	RIT_SHIELD				= 536870912,
	RIT_ANTIGRAV			= 1073741824,
	RIT_SUPERHEALTH			= 2147483648,
} rogueitems_t;

// MED 01/04/97 added hipnotic defines
//===========================================
// hipnotic added defines
typedef enum
{
	HIT_PROXIMITY_GUN_BIT	= 16,
	HIT_MJOLNIR_BIT			= 7,
	HIT_LASER_CANNON_BIT	= 23,
	HIT_PROXIMITY_GUN		= (1<<HIT_PROXIMITY_GUN_BIT),
	HIT_MJOLNIR				= (1<<HIT_MJOLNIR_BIT),
	HIT_LASER_CANNON		= (1<<HIT_LASER_CANNON_BIT),
	HIT_WETSUIT				= (1<<(23+2)),
	HIT_EMPATHY_SHIELDS		= (1<<(23+3)),
} hipnoticitems_t;

// clang-format on
//===========================================

#define MAX_SCOREBOARD	   16
#define MAX_SCOREBOARDNAME 32

#define SOUND_CHANNELS 8

#define WARPIMAGESIZE 512
#define WARPIMAGEMIPS 5

#define DOUBLE_BUFFERED 2

typedef struct
{
	const char *basedir;
	const char *userdir; // user's directory on UNIX platforms.
						 // if user directories are enabled, basedir
						 // and userdir will point to different
						 // memory locations, otherwise to the same.
	int			argc;
	char	  **argv;
	int			errstate;
} quakeparms_t;

// johnfitz -- stuff for 2d drawing control
typedef enum
{
	CANVAS_NONE,
	CANVAS_DEFAULT,
	CANVAS_CONSOLE,
	CANVAS_MENU,
	CANVAS_SBAR,
	CANVAS_WARPIMAGE,
	CANVAS_CROSSHAIR,
	CANVAS_BOTTOMLEFT,
	CANVAS_BOTTOMRIGHT,
	CANVAS_TOPRIGHT,
	CANVAS_CSQC,
	CANVAS_INVALID = -1
} canvastype;

#ifdef _MSC_VER
static inline int FindFirstBitNonZero (const uint32_t mask)
{
	unsigned long result;
	_BitScanForward (&result, mask);
	return result;
}
static inline int FindFirstBitNonZero64 (const uint64_t mask)
{
#if !defined(_M_IX86)
	unsigned long result;
	_BitScanForward64 (&result, mask);
	return result;
#else
	unsigned long result;
	if ((mask & 0xFFFFFFFF) != 0)
	{
		_BitScanForward (&result, (uint32_t)mask);
		return result;
	}
	else
	{
		_BitScanForward (&result, (uint32_t)(mask >> 32));
		return result + 32;
	}
#endif
}
static inline int FindLastBitNonZero (const uint32_t mask)
{
	unsigned long result;
	_BitScanReverse (&result, mask);
	return result;
}
static inline int FindLastBitNonZero64 (const uint64_t mask)
{
#if !defined(_M_IX86)
	unsigned long result;
	_BitScanReverse64 (&result, mask);
	return result;
#else
	unsigned long result;
	if ((mask >> 32) != 0)
	{
		_BitScanReverse (&result, (uint32_t)(mask >> 32));
		return result + 32;
	}
	else
	{
		_BitScanReverse (&result, (uint32_t)mask);
		return result;
	}
#endif
}
#define THREAD_LOCAL  __declspec (thread)
#define FORCE_INLINE  __forceinline
#define UNREACHABLE() __assume (false)
#else
static inline int FindFirstBitNonZero (const uint32_t mask)
{
	return __builtin_ctz (mask);
}
static inline int FindFirstBitNonZero64 (const uint64_t mask)
{
	return __builtin_ctzll (mask);
}
static inline int FindLastBitNonZero (const uint32_t mask)
{
	return 31 ^ __builtin_clz (mask);
}
static inline int FindLastBitNonZero64 (const uint64_t mask)
{
	return 63 ^ __builtin_clzll (mask);
}
#define THREAD_LOCAL  _Thread_local
#define FORCE_INLINE  __attribute__ ((always_inline)) inline
#define UNREACHABLE() __builtin_unreachable ()
#endif

#include "sys.h"
#include "common.h"
#include "mem.h"
#include "bspfile.h"
#include "mathlib.h"
#include "cvar.h"

#include "protocol.h"
#include "net.h"

#include "cmd.h"
#include "crc.h"

#include "progs.h"
#include "server.h"

#include "platform.h"

#include <vulkan/vulkan_core.h>
#if VK_HEADER_VERSION < 162
#error Vulkan SDK too old
#endif

#include "console.h"
#include "wad.h"
#include "vid.h"
#include "screen.h"
#include "draw.h"
#include "render.h"
#include "view.h"
#include "sbar.h"
#include "q_sound.h"
#include "client.h"

#include "gl_model.h"
#include "world.h"

#include "image.h"	   //johnfitz
#include "gl_texmgr.h" //johnfitz
#include "input.h"
#include "keys.h"
#include "menu.h"
#include "cdaudio.h"
#include "glquake.h"
#include "../Shaders/shaders.h"

#include "tasks.h"
#include "atomics.h"
#include "hash_map.h"

//=============================================================================

// the host system specifies the base of the directory tree, the
// command line parms passed to the program, and the amount of memory
// available for the program to use

extern qboolean noclip_anglehack;

//
// host
//
extern quakeparms_t *host_parms;

extern cvar_t sys_ticrate;
extern cvar_t sys_nostdout;
extern cvar_t developer;
extern cvar_t max_edicts; // johnfitz

extern qboolean host_initialized; // true if into command execution
extern double	host_frametime;
extern byte	   *host_colormap;
extern int		host_framecount; // incremented every frame, never reset
extern double	realtime;		 // not bounded in any way, changed at
								 // start of every frame, never reset

typedef struct filelist_item_s
{
	char					name[32];
	struct filelist_item_s *next;
} filelist_item_t;

extern filelist_item_t *modlist;
extern filelist_item_t *extralevels;
extern filelist_item_t *demolist;
extern filelist_item_t *savelist;

void			   Host_ClearMemory (void);
void			   Host_ServerFrame (void);
void			   Host_InitCommands (void);
void			   Host_Init (void);
void			   Host_Shutdown (void);
void			   Host_Callback_Notify (cvar_t *var); /* callback function for CVAR_NOTIFY */
FUNC_NORETURN void Host_Error (const char *error, ...) FUNC_PRINTF (1, 2);
FUNC_NORETURN void Host_EndGame (const char *message, ...) FUNC_PRINTF (1, 2);
void			   Host_Frame (double time);
void			   Host_Quit_f (void);
void			   Host_ClientCommands (const char *fmt, ...) FUNC_PRINTF (1, 2);
void			   Host_ShutdownServer (qboolean crash);
void			   Host_WriteConfiguration (void);
void			   Host_Resetdemos (void);

void ExtraMaps_Init (void);
void Modlist_Init (void);
void DemoList_Init (void);
void SaveList_Init (void);

void ExtraMaps_NewGame (void);
void DemoList_Rebuild (void);
void SaveList_Rebuild (void);

extern int current_skill; // skill level for currently loaded level (in case
						  //  the user changes the cvar while the level is
						  //  running, this reflects the level actually in use)

extern qboolean isDedicated;

extern int minimum_memory;

extern atomic_uint32_t num_vulkan_tex_allocations;
extern atomic_uint32_t num_vulkan_bmodel_allocations;
extern atomic_uint32_t num_vulkan_mesh_allocations;
extern atomic_uint32_t num_vulkan_misc_allocations;
extern atomic_uint32_t num_vulkan_dynbuf_allocations;
extern atomic_uint32_t num_vulkan_combined_image_samplers;
extern atomic_uint32_t num_vulkan_ubos_dynamic;
extern atomic_uint32_t num_vulkan_input_attachments;
extern atomic_uint32_t num_vulkan_storage_images;

extern qboolean multiuser;

#endif /* QUAKEDEFS_H */
