/*
Copyright (C) 1996-2001 Id Software, Inc.
Copyright (C) 2002-2009 John Fitzgibbons and others
Copyright (C) 2007-2008 Kristian Duske
Copyright (C) 2010-2014 QuakeSpasm developers

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

#ifndef __QUAKEDEFS_H
#define __QUAKEDEFS_H

// quakedef.h -- primary header for client

#define	QUAKE_GAME		// as opposed to utilities

#define	VERSION			1.09
#define	GLQUAKE_VERSION		1.00
#define	D3DQUAKE_VERSION	0.01
#define	WINQUAKE_VERSION	0.996
#define	LINUX_VERSION		1.30
#define	X11_VERSION		1.10

#define	FITZQUAKE_VERSION	0.85	//johnfitz
#define	QUAKESPASM_VERSION	0.91
#define	QUAKESPASM_VER_PATCH	0	// helper to print a string like 0.91.0

//define	PARANOID			// speed sapping error checking

#define	GAMENAME	"id1"		// directory to look in by default

#include "q_stdinc.h"

// !!! if this is changed, it must be changed in d_ifacea.h too !!!
#define CACHE_SIZE	32	// used to align key data structures

#define Q_UNUSED(x)	(x = x)	// for pesky compiler / lint warnings

#define	MINIMUM_MEMORY	0x550000
#define	MINIMUM_MEMORY_LEVELPAK	(MINIMUM_MEMORY + 0x100000)

#define MAX_NUM_ARGVS	50

// up / down
#define	PITCH		0

// left / right
#define	YAW		1

// fall over
#define	ROLL		2


#define	MAX_QPATH	64		// max length of a quake game pathname

#define	ON_EPSILON	0.1		// point on plane side epsilon

#define	DIST_EPSILON	(0.03125)	// 1/32 epsilon to keep floating point happy (moved from world.c)

#define	MAX_MSGLEN	64000		// max length of a reliable message //ericw -- was 32000
#define	MAX_DATAGRAM	32000		// max length of unreliable message //johnfitz -- was 1024

#define	DATAGRAM_MTU	1400		// johnfitz -- actual limit for unreliable messages to nonlocal clients

//
// per-level limits
//
#define	MIN_EDICTS	256		// johnfitz -- lowest allowed value for max_edicts cvar
#define	MAX_EDICTS	32000		// johnfitz -- highest allowed value for max_edicts cvar
						// ents past 8192 can't play sounds in the standard protocol
#define	MAX_LIGHTSTYLES	64
#define	MAX_MODELS	2048		// johnfitz -- was 256
#define	MAX_SOUNDS	2048		// johnfitz -- was 256

#define	SAVEGAME_COMMENT_LENGTH	39

#define	MAX_STYLESTRING		64

//
// stats are integers communicated to the client by the server
//
#define	MAX_CL_STATS		32
#define	STAT_HEALTH		0
#define	STAT_FRAGS		1
#define	STAT_WEAPON		2
#define	STAT_AMMO		3
#define	STAT_ARMOR		4
#define	STAT_WEAPONFRAME	5
#define	STAT_SHELLS		6
#define	STAT_NAILS		7
#define	STAT_ROCKETS		8
#define	STAT_CELLS		9
#define	STAT_ACTIVEWEAPON	10
#define	STAT_TOTALSECRETS	11
#define	STAT_TOTALMONSTERS	12
#define	STAT_SECRETS		13	// bumped on client side by svc_foundsecret
#define	STAT_MONSTERS		14	// bumped by svc_killedmonster

// stock defines
//
#define	IT_SHOTGUN		1
#define	IT_SUPER_SHOTGUN	2
#define	IT_NAILGUN		4
#define	IT_SUPER_NAILGUN	8
#define	IT_GRENADE_LAUNCHER	16
#define	IT_ROCKET_LAUNCHER	32
#define	IT_LIGHTNING		64
#define	IT_SUPER_LIGHTNING	128
#define	IT_SHELLS		256
#define	IT_NAILS		512
#define	IT_ROCKETS		1024
#define	IT_CELLS		2048
#define	IT_AXE			4096
#define	IT_ARMOR1		8192
#define	IT_ARMOR2		16384
#define	IT_ARMOR3		32768
#define	IT_SUPERHEALTH		65536
#define	IT_KEY1			131072
#define	IT_KEY2			262144
#define	IT_INVISIBILITY		524288
#define	IT_INVULNERABILITY	1048576
#define	IT_SUIT			2097152
#define	IT_QUAD			4194304
#define	IT_SIGIL1		(1<<28)
#define	IT_SIGIL2		(1<<29)
#define	IT_SIGIL3		(1<<30)
#define	IT_SIGIL4		(1<<31)

//===========================================
//rogue changed and added defines

#define	RIT_SHELLS		128
#define	RIT_NAILS		256
#define	RIT_ROCKETS		512
#define	RIT_CELLS		1024
#define	RIT_AXE			2048
#define	RIT_LAVA_NAILGUN	4096
#define	RIT_LAVA_SUPER_NAILGUN	8192
#define	RIT_MULTI_GRENADE	16384
#define	RIT_MULTI_ROCKET	32768
#define	RIT_PLASMA_GUN		65536
#define	RIT_ARMOR1		8388608
#define	RIT_ARMOR2		16777216
#define	RIT_ARMOR3		33554432
#define	RIT_LAVA_NAILS		67108864
#define	RIT_PLASMA_AMMO		134217728
#define	RIT_MULTI_ROCKETS	268435456
#define	RIT_SHIELD		536870912
#define	RIT_ANTIGRAV		1073741824
#define	RIT_SUPERHEALTH		2147483648

//MED 01/04/97 added hipnotic defines
//===========================================
//hipnotic added defines
#define	HIT_PROXIMITY_GUN_BIT	16
#define	HIT_MJOLNIR_BIT		7
#define	HIT_LASER_CANNON_BIT	23
#define	HIT_PROXIMITY_GUN	(1<<HIT_PROXIMITY_GUN_BIT)
#define	HIT_MJOLNIR		(1<<HIT_MJOLNIR_BIT)
#define	HIT_LASER_CANNON	(1<<HIT_LASER_CANNON_BIT)
#define	HIT_WETSUIT		(1<<(23+2))
#define	HIT_EMPATHY_SHIELDS	(1<<(23+3))

//===========================================

#define	MAX_SCOREBOARD		16
#define	MAX_SCOREBOARDNAME	32

#define	SOUND_CHANNELS		8

typedef struct
{
	const char *basedir;
	const char *userdir;	// user's directory on UNIX platforms.
				// if user directories are enabled, basedir
				// and userdir will point to different
				// memory locations, otherwise to the same.
	int	argc;
	char	**argv;
	void	*membase;
	int	memsize;
	int	numcpus;
} quakeparms_t;

#include "common.h"
#include "bspfile.h"
#include "sys.h"
#include "zone.h"
#include "mathlib.h"
#include "cvar.h"

#include "protocol.h"
#include "net.h"

#include "cmd.h"
#include "crc.h"

#include "progs.h"
#include "server.h"

#include "platform.h"
#if defined(SDL_FRAMEWORK) || defined(NO_SDL_CONFIG)
#if defined(USE_SDL2)
#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>
#else
#include <SDL/SDL.h>
#include <SDL/SDL_opengl.h>
#endif
#else
#include "SDL.h"
#include "SDL_opengl.h"
#endif
#ifndef APIENTRY
#define	APIENTRY
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

#include "image.h"	//johnfitz
#include "gl_texmgr.h"	//johnfitz
#include "input.h"
#include "keys.h"
#include "menu.h"
#include "cdaudio.h"
#include "glquake.h"


//=============================================================================

// the host system specifies the base of the directory tree, the
// command line parms passed to the program, and the amount of memory
// available for the program to use

extern qboolean noclip_anglehack;

//
// host
//
extern	quakeparms_t *host_parms;

extern	cvar_t		sys_ticrate;
extern	cvar_t		sys_throttle;
extern	cvar_t		sys_nostdout;
extern	cvar_t		developer;
extern	cvar_t		max_edicts; //johnfitz

extern	qboolean	host_initialized;	// true if into command execution
extern	double		host_frametime;
extern	byte		*host_colormap;
extern	int		host_framecount;	// incremented every frame, never reset
extern	double		realtime;		// not bounded in any way, changed at
							// start of every frame, never reset

void Host_ClearMemory (void);
void Host_ServerFrame (void);
void Host_InitCommands (void);
void Host_Init (void);
void Host_Shutdown(void);
void Host_Callback_Notify (cvar_t *var);	/* callback function for CVAR_NOTIFY */
void Host_Error (const char *error, ...) __attribute__((__format__(__printf__,1,2), __noreturn__));
void Host_EndGame (const char *message, ...) __attribute__((__format__(__printf__,1,2), __noreturn__));
void Host_Frame (float time);
void Host_Quit_f (void);
void Host_ClientCommands (const char *fmt, ...) __attribute__((__format__(__printf__,1,2)));
void Host_ShutdownServer (qboolean crash);
void Host_WriteConfiguration (void);

void ExtraMaps_Init (void);
void Modlist_Init (void);
void DemoList_Init (void);

void DemoList_Rebuild (void);

extern int		current_skill;	// skill level for currently loaded level (in case
					//  the user changes the cvar while the level is
					//  running, this reflects the level actually in use)

extern qboolean		isDedicated;

extern int		minimum_memory;

#endif	/* __QUAKEDEFS_H */

