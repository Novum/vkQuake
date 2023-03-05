/*
Copyright (C) 1996-2001 Id Software, Inc.
Copyright (C) 2002-2009 John Fitzgibbons and others
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

#ifndef _QUAKE_PROTOCOL_H
#define _QUAKE_PROTOCOL_H

// protocol.h -- communications protocols

#define PROTOCOL_NETQUAKE  15  // johnfitz -- standard quake protocol
#define PROTOCOL_FITZQUAKE 666 // johnfitz -- added new protocol for fitzquake 0.85
#define PROTOCOL_RMQ	   999
#define PROTOCOL_FTE_PEXT1 \
	(('F' << 0) + ('T' << 8) + ('E' << 16) + ('X' << 24)) // fte extensions, provides extensions to the underlying base protocol (like 666 or even 15).
#define PROTOCOL_FTE_PEXT2 \
	(('F' << 0) + ('T' << 8) + ('E' << 16) + ('2' << 24)) // fte extensions, provides extensions to the underlying base protocol (like 666 or even 15).

// PROTOCOL_RMQ protocol flags
#define PRFL_SHORTANGLE			(1 << 1)
#define PRFL_FLOATANGLE			(1 << 2)
#define PRFL_24BITCOORD			(1 << 3)
#define PRFL_FLOATCOORD			(1 << 4)
#define PRFL_EDICTSCALE			(1 << 5)
#define PRFL_ALPHASANITY		(1 << 6) // cleanup insanity with alpha
#define PRFL_INT32COORD			(1 << 7)
#define PRFL_MOREFLAGS			(1 << 31) // not supported
// PROTOCOL_FTE_PEXT1 flags
#define PEXT1_CSQC				0x40000000	 //(full)csqc additions, required for csqc ents+events.
#define PEXT1_SUPPORTED_CLIENT	(PEXT1_CSQC) // pext1 flags that we advertise to servers (aka: full support)
#define PEXT1_SUPPORTED_SERVER	(PEXT1_CSQC) // pext1 flags that we accept from clients.
#define PEXT1_ACCEPTED_CLIENT	(PEXT1_SUPPORTED_CLIENT)
// PROTOCOL_FTE_PEXT2 flags
#define PEXT2_PRYDONCURSOR		0x00000001								   // a mouse cursor exposed to ssqc
#define PEXT2_VOICECHAT			0x00000002								   //+voip or cl_voip_send 1; requires opus dll, and others to also have that same dll.
#define PEXT2_REPLACEMENTDELTAS 0x00000008								   // more compact entity deltas (can also be split across multiple packets)
#define PEXT2_PREDINFO			0x00000020								   // provides input acks and reworks stats such that clc_clientdata becomes redundant.
#define PEXT2_SUPPORTED_CLIENT	(PEXT2_REPLACEMENTDELTAS | PEXT2_PREDINFO) // pext2 flags that we understand+support
#define PEXT2_SUPPORTED_SERVER	(PEXT2_REPLACEMENTDELTAS | PEXT2_PREDINFO)
#define PEXT2_ACCEPTED_CLIENT	(PEXT2_SUPPORTED_CLIENT | PEXT2_PRYDONCURSOR | PEXT2_VOICECHAT) // pext2 flags that we can parse, but don't want to advertise

// if the high bit of the servercmd is set, the low bits are fast update flags:
#define U_MOREBITS (1 << 0)
#define U_ORIGIN1  (1 << 1)
#define U_ORIGIN2  (1 << 2)
#define U_ORIGIN3  (1 << 3)
#define U_ANGLE2   (1 << 4)
#define U_STEP	   (1 << 5) // johnfitz -- was U_NOLERP, renamed since it's only used for MOVETYPE_STEP
#define U_FRAME	   (1 << 6)
#define U_SIGNAL   (1 << 7) // just differentiates from other updates

// svc_update can pass all of the fast update bits, plus more
#define U_ANGLE1	 (1 << 8)
#define U_ANGLE3	 (1 << 9)
#define U_MODEL		 (1 << 10)
#define U_COLORMAP	 (1 << 11)
#define U_SKIN		 (1 << 12)
#define U_EFFECTS	 (1 << 13)
#define U_LONGENTITY (1 << 14)
// johnfitz -- PROTOCOL_FITZQUAKE -- new bits
#define U_EXTEND1	 (1 << 15)
#define U_ALPHA		 (1 << 16) // 1 byte, uses ENTALPHA_ENCODE, not sent if equal to baseline
#define U_FRAME2	 (1 << 17) // 1 byte, this is .frame & 0xFF00 (second byte)
#define U_MODEL2	 (1 << 18) // 1 byte, this is .modelindex & 0xFF00 (second byte)
#define U_LERPFINISH (1 << 19) // 1 byte, 0.0-1.0 maps to 0-255, not sent if exactly 0.1, this is ent->v.nextthink - sv.time, used for lerping
#define U_SCALE		 (1 << 20) // 1 byte, for PROTOCOL_RMQ PRFL_EDICTSCALE, currently read but ignored
#define U_UNUSED21	 (1 << 21)
#define U_UNUSED22	 (1 << 22)
#define U_EXTEND2	 (1 << 23) // another byte to follow, future expansion
// johnfitz

// johnfitz -- PROTOCOL_NEHAHRA transparency
#define U_TRANS (1 << 15)
// johnfitz

// spike -- FTE Replacement Deltas
// first byte contains the stuff that's most likely to change constantly
#define UF_FRAME	(1u << 0)
#define UF_ORIGINXY (1u << 1)
#define UF_ORIGINZ	(1u << 2)
#define UF_ANGLESXZ (1u << 3)
#define UF_ANGLESY	(1u << 4)
#define UF_EFFECTS	(1u << 5)
#define UF_PREDINFO (1u << 6) /*ent is predicted, probably a player*/
#define UF_EXTEND1	(1u << 7)

/*stuff which is common on ent spawning*/
#define UF_RESET	(1u << 8)
#define UF_16BIT	(1u << 9) /*within this update, frame/skin/model is 16bit, not part of the deltaing itself*/
#define UF_MODEL	(1u << 10)
#define UF_SKIN		(1u << 11)
#define UF_COLORMAP (1u << 12)
#define UF_SOLID	(1u << 13) /*encodes the size of the entity, so prediction can bump against it*/
#define UF_FLAGS	(1u << 14) /*some extra flags like viewmodelforclient*/
#define UF_EXTEND2	(1u << 15)

/*the rest is optional extensions*/
#define UF_ALPHA	   (1u << 16) /*transparency*/
#define UF_SCALE	   (1u << 17) /*rescaling stuff, 1/16th*/
#define UF_BONEDATA	   (1u << 18) /*for ssqc control over skeletal models*/
#define UF_DRAWFLAGS   (1u << 19) /*scale offsets and things*/
#define UF_TAGINFO	   (1u << 20) /*simple entity attachments, generally needs either md3s or skeletal models*/
#define UF_LIGHT	   (1u << 21) /*attaching rtlights to dynamic entities from ssqc*/
#define UF_TRAILEFFECT (1u << 22) /*attaches custom particle trails to entities, woo.*/
#define UF_EXTEND3	   (1u << 23)

#define UF_COLORMOD	   (1u << 24) /*rgb tints. 1/16th*/
#define UF_GLOW		   (1u << 25) /*tbh only useful as an extra 'renderable' field for csqc...*/
#define UF_FATNESS	   (1u << 26) /*push the entity's normals out by this distance*/
#define UF_MODELINDEX2 (1u << 27) /*for lame visible weapon models, like q2. just adds a second ent at the same point*/
#define UF_GRAVITYDIR  (1u << 28) /*yay prediction*/
#define UF_EFFECTS2	   (1u << 29) /*effects is 16bit, or if both effects flags are set then 32bit*/
#define UF_UNUSED2	   (1u << 30)
#define UF_UNUSED1	   (1u << 31)

/*these flags are generally not deltaed as they're changing constantly*/
#define UFP_FORWARD			(1u << 0)
#define UFP_SIDE			(1u << 1)
#define UFP_UP				(1u << 2)
#define UFP_MOVETYPE		(1u << 3) /*deltaed*/
#define UFP_VELOCITYXY		(1u << 4)
#define UFP_VELOCITYZ		(1u << 5)
#define UFP_MSEC			(1u << 6)
#define UFP_WEAPONFRAME_OLD (1u << 7) // no longer used. just a stat now that I rewrote stat deltas.
#define UFP_VIEWANGLE		(1u << 7)
// spike

#define SU_VIEWHEIGHT	(1 << 0)
#define SU_IDEALPITCH	(1 << 1)
#define SU_PUNCH1		(1 << 2)
#define SU_PUNCH2		(1 << 3)
#define SU_PUNCH3		(1 << 4)
#define SU_VELOCITY1	(1 << 5)
#define SU_VELOCITY2	(1 << 6)
#define SU_VELOCITY3	(1 << 7)
#define SU_UNUSED8		(1 << 8) // AVAILABLE BIT
#define SU_ITEMS		(1 << 9)
#define SU_ONGROUND		(1 << 10) // no data follows, the bit is it
#define SU_INWATER		(1 << 11) // no data follows, the bit is it
#define SU_WEAPONFRAME	(1 << 12)
#define SU_ARMOR		(1 << 13)
#define SU_WEAPON		(1 << 14)
// johnfitz -- PROTOCOL_FITZQUAKE -- new bits
#define SU_EXTEND1		(1 << 15) // another byte to follow
#define SU_WEAPON2		(1 << 16) // 1 byte, this is .weaponmodel & 0xFF00 (second byte)
#define SU_ARMOR2		(1 << 17) // 1 byte, this is .armorvalue & 0xFF00 (second byte)
#define SU_AMMO2		(1 << 18) // 1 byte, this is .currentammo & 0xFF00 (second byte)
#define SU_SHELLS2		(1 << 19) // 1 byte, this is .ammo_shells & 0xFF00 (second byte)
#define SU_NAILS2		(1 << 20) // 1 byte, this is .ammo_nails & 0xFF00 (second byte)
#define SU_ROCKETS2		(1 << 21) // 1 byte, this is .ammo_rockets & 0xFF00 (second byte)
#define SU_CELLS2		(1 << 22) // 1 byte, this is .ammo_cells & 0xFF00 (second byte)
#define SU_EXTEND2		(1 << 23) // another byte to follow
#define SU_WEAPONFRAME2 (1 << 24) // 1 byte, this is .weaponframe & 0xFF00 (second byte)
#define SU_WEAPONALPHA	(1 << 25) // 1 byte, this is alpha for weaponmodel, uses ENTALPHA_ENCODE, not sent if ENTALPHA_DEFAULT
#define SU_UNUSED26		(1 << 26)
#define SU_UNUSED27		(1 << 27)
#define SU_UNUSED28		(1 << 28)
#define SU_UNUSED29		(1 << 29)
#define SU_UNUSED30		(1 << 30)
#define SU_EXTEND3		(1 << 31) // another byte to follow, future expansion
// johnfitz
// spike dp
#define DPSU_VIEWZOOM	(1 << 19) // byte factor (0 = 0.0 (not valid), 255 = 1.0)
// spike

// a sound with no channel is a local only sound
#define SND_VOLUME		(1 << 0) // a byte
#define SND_ATTENUATION (1 << 1) // a byte
#define SND_LOOPING		(1 << 2) // a long

#define DEFAULT_SOUND_PACKET_VOLUME		 255
#define DEFAULT_SOUND_PACKET_ATTENUATION 1.0

// johnfitz -- PROTOCOL_FITZQUAKE -- new bits
#define SND_LARGEENTITY	  (1 << 3) // a short + byte (instead of just a short)
#define SND_LARGESOUND	  (1 << 4) // a short soundindex (instead of a byte)
// johnfitz
// spike -- parsing, but not using at this time
#define SND_FTE_MOREFLAGS (1 << 2) // a byte, for channel flags
#define SND_DP_PITCH	  (1 << 5) // dp uses this for pitch...
#define SND_FTE_TIMEOFS	  (1 << 6) // signed short, in milliseconds.
#define SND_FTE_PITCHADJ  (1 << 7) // a byte (speed percent (0=100%))
#define SND_FTE_VELOCITY  (1 << 8) // 3 shorts (1/8th), for doppler or whatever.
// spike

// johnfitz -- PROTOCOL_FITZQUAKE -- flags for entity baseline messages
#define B_LARGEMODEL (1 << 0) // modelindex is short instead of byte
#define B_LARGEFRAME (1 << 1) // frame is short instead of byte
#define B_ALPHA		 (1 << 2) // 1 byte, uses ENTALPHA_ENCODE, not sent if ENTALPHA_DEFAULT
#define B_SCALE		 (1 << 3) // added as part of rmq 999 (NOT valid for 666)
// johnfitz

// johnfitz -- PROTOCOL_FITZQUAKE -- alpha encoding
#define ENTALPHA_DEFAULT   0   // entity's alpha is "default" (i.e. water obeys r_wateralpha) -- must be zero so zeroed out memory works
#define ENTALPHA_ZERO	   1   // entity is invisible (lowest possible alpha)
#define ENTALPHA_ONE	   255 // entity is fully opaque (highest possible alpha)
#define ENTALPHA_ENCODE(a) (((a) == 0) ? ENTALPHA_DEFAULT : Q_rint (CLAMP (1, (a)*254.0f + 1, 255))) // server convert to byte to send to client
#define ENTALPHA_DECODE(a) (((a) == ENTALPHA_DEFAULT) ? 1.0f : ((float)(a)-1) / (254))				 // client convert to float for rendering
#define ENTALPHA_TOSAVE(a) \
	(((a) == ENTALPHA_DEFAULT) ? 0.0f : (((a) == ENTALPHA_ZERO) ? -1.0f : ((float)(a)-1) / (254))) // server convert to float for
																								   // savegame
// johnfitz

#define ENTSCALE_DEFAULT	16 // 4.4 fixed point
#define ENTSCALE_ENCODE(f)	((f) ? CLAMP (1, (int)(ENTSCALE_DEFAULT * (f)), 255) : ENTSCALE_DEFAULT)
#define ENTSCALE_DECODE(es) ((es) / (float)ENTSCALE_DEFAULT)

// defaults for clientinfo messages
#define DEFAULT_VIEWHEIGHT 22

// game types sent by serverinfo
// these determine which intermission screen plays
#define GAME_COOP		0
#define GAME_DEATHMATCH 1

//
// server to client
//
#define svc_bad		   0
#define svc_nop		   1
#define svc_disconnect 2
#define svc_updatestat 3 // [byte] [long]
#define svc_version	   4 // [long] server version
#define svc_setview	   5 // [short] entity number
#define svc_sound	   6 // <see code>
#define svc_time	   7 // [float] server time
#define svc_print	   8 // [string] null terminated string
#define svc_stufftext \
	9					// [string] stuffed into client's console buffer
						// the string should be \n terminated
#define svc_setangle 10 // [angle3] set the view angle to this absolute value
#define svc_serverinfo \
	11							// [long] version
								// [string] signon string
								// [string]..[0]model cache
								// [string]...[0]sounds cache
#define svc_lightstyle		 12 // [byte] [string]
#define svc_updatename		 13 // [byte] [string]
#define svc_updatefrags		 14 // [byte] [short]
#define svc_clientdata		 15 // <shortbits + data>
#define svc_stopsound		 16 // <see code>
#define svc_updatecolors	 17 // [byte] [byte]
#define svc_particle		 18 // [vec3] <variable>
#define svc_damage			 19
#define svc_spawnstatic		 20
// #define svc_spawnbinary		21
#define svcfte_spawnstatic2	 21
#define svc_spawnbaseline	 22
#define svc_temp_entity		 23
#define svc_setpause		 24 // [byte] on / off
#define svc_signonnum		 25 // [byte]  used for the signon sequence
#define svc_centerprint		 26 // [string] to put in center of the screen
#define svc_killedmonster	 27
#define svc_foundsecret		 28
#define svc_spawnstaticsound 29 // [coord3] [byte] samp [byte] vol [byte] aten
#define svc_intermission	 30 // [string] music
#define svc_finale			 31 // [string] music [string] text
#define svc_cdtrack			 32 // [byte] track [byte] looptrack
#define svc_sellscreen		 33
#define svc_cutscene		 34

// johnfitz -- PROTOCOL_FITZQUAKE -- new server messages
#define svc_skybox			  37 // [string] name
#define svc_bf				  40
#define svc_fog				  41 // [byte] density [byte] red [byte] green [byte] blue [float] time
#define svc_spawnbaseline2	  42 // support for large modelindex, large framenum, alpha, using flags
#define svc_spawnstatic2	  43 // support for large modelindex, large framenum, alpha, using flags
#define svc_spawnstaticsound2 44 // [coord3] [short] samp [byte] vol [byte] aten
// johnfitz

// 2021 re-release server messages - see:
// https://steamcommunity.com/sharedfiles/filedetails/?id=2679459726
#define svc_botchat		   38
#define svc_setviews	   45
#define svc_updateping	   46
#define svc_updatesocial   47
#define svc_updateplinfo   48
#define svc_rawprint	   49
#define svc_servervars	   50
#define svc_seq			   51
// Note: svc_achievement has same value as svcdp_effect!
#define svc_achievement	   52 // [string] id
#define svc_chat		   53
#define svc_levelcompleted 54
#define svc_backtolobby	   55
#define svc_localsound	   56

// spike -- some extensions for particles.
// some extra stuff for fte's pext2_replacementdeltas, including stats
// fte reuses the dp svcs for nq (instead of qw-specific ones), at least where the protocol is identical. this should make dpp7 support a little easier if you
// ever want to implement that. dp has a tendancy to use the svcs even when told to use protocol 15, so supporting them helps there too.
#define svcdp_downloaddata	 50
#define svcdp_updatestatbyte 51
#define svcdp_effect		 52 // [vector] org [byte] modelindex [byte] startframe [byte] framecount [byte] framerate
#define svcdp_effect2		 53 // [vector] org [short] modelindex [short] startframe [byte] framecount [byte] framerate
#define svcdp_precache \
	54 // [short] precacheindex [string] filename. index&0x8000 = sound, 0x4000 = particle, 0xc000 = reserved (probably to reclaim these bits eventually),
	   // otherwise model.
#define svcdp_spawnbaseline2	55
// Note: svcdp_spawnstatic2 has the same value of svc_localsound from 2021 re-release!
#define svcdp_spawnstatic2		56
#define svcdp_entities			57
#define svcdp_csqcentities		58
#define svcdp_spawnstaticsound2 59 // [coord3] [short] samp [byte] vol [byte] aten
#define svcdp_trailparticles	60 // [short] entnum [short] effectnum [vector] start [vector] end
#define svcdp_pointparticles	61 // [short] effectnum [vector] start [vector] velocity [short] count
#define svcdp_pointparticles1	62 // [short] effectnum [vector] start, same as svc_pointparticles except velocity is zero and count is 1
#define svcfte_spawnbaseline2	66
#define svcfte_updatestatstring 78
#define svcfte_updatestatfloat	79
#define svcfte_cgamepacket		83
#define svcfte_voicechat		84
#define svcfte_setangledelta	85
#define svcfte_updateentities	86
// spike -- end

//
// client to server
//
#define clc_bad			0
#define clc_nop			1
#define clc_disconnect	2
#define clc_move		3  // [usercmd_t]
#define clc_stringcmd	4  // [string] message
#define clcdp_ackframe	50 // [long] frame sequence. reused by fte replacement deltas
//
// temp entity events
//
#define TE_SPIKE		0
#define TE_SUPERSPIKE	1
#define TE_GUNSHOT		2
#define TE_EXPLOSION	3
#define TE_TAREXPLOSION 4
#define TE_LIGHTNING1	5
#define TE_LIGHTNING2	6
#define TE_WIZSPIKE		7
#define TE_KNIGHTSPIKE	8
#define TE_LIGHTNING3	9
#define TE_LAVASPLASH	10
#define TE_TELEPORT		11
#define TE_EXPLOSION2	12

// PGM 01/21/97
#define TE_BEAM 13
// PGM 01/21/97

#define TEDP_PARTICLERAIN 55 // [vector] min [vector] max [vector] dir [short] count [byte] color
#define TEDP_PARTICLESNOW 56 // [vector] min [vector] max [vector] dir [short] count [byte] color

typedef struct entity_state_s
{
	vec3_t		   origin;
	vec3_t		   angles;
	unsigned short modelindex; // johnfitz -- was int
	unsigned short frame;	   // johnfitz -- was int
	unsigned int   effects;
	unsigned char  colormap;	   // johnfitz -- was int
	unsigned char  skin;		   // johnfitz -- was int
	unsigned char  scale;		   // spike -- *16
	unsigned char  pmovetype;	   // spike
	unsigned short traileffectnum; // spike -- for qc-defined particle trails. typically evilly used for things that are not trails.
	unsigned short emiteffectnum;  // spike -- for qc-defined particle trails. typically evilly used for things that are not trails.
	short		   velocity[3];	   // spike -- the player's velocity.
	unsigned char  eflags;
	unsigned char  tagindex;
	unsigned short tagentity;
	unsigned short pad;
	unsigned char  colormod[3]; // spike -- entity tints, *32
	unsigned char  alpha;		// johnfitz -- added
	unsigned int   solidsize;	// for csqc prediction logic.
#define ES_SOLID_NOT   0
#define ES_SOLID_BSP   31
#define ES_SOLID_HULL1 0x80201810
#define ES_SOLID_HULL2 0x80401820
#ifdef LERP_BANDAID
	unsigned short lerp;
#endif
} entity_state_t;
#define EFLAGS_STEP			 1
// #define EFLAGS_GLOWTRAIL		2
#define EFLAGS_VIEWMODEL	 4 // does not appear in reflections/third person. attached to the view.
#define EFLAGS_EXTERIORMODEL 8 // only appears in reflections/third person
// #define EFLAGS_				16
#define EFLAGS_COLOURMAPPED	 32 //.colormap=1024|(top<<4)|bottom), instead of a player number
// #define EFLAGS_				64
#define EFLAGS_ONGROUND		 128 // for bobbing more than anything else. *sigh*.

extern entity_state_t nullentitystate; // note: not all null.

typedef struct
{
	float  servertime;
	float  seconds; // servertime-previous->servertime
	vec3_t viewangles;

	// intended velocities
	float forwardmove;
	float sidemove;
	float upmove;

	// used by client for mouse-based movements that should accumulate over multiple client frames
	float forwardmove_accumulator;
	float sidemove_accumulator;
	float upmove_accumulator;

	unsigned int buttons;
	unsigned int impulse;

	unsigned int sequence;

	int weapon;
} usercmd_t;

#endif /* _QUAKE_PROTOCOL_H */
