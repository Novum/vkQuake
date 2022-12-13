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

#ifndef _QUAKE_SERVER_H
#define _QUAKE_SERVER_H

// server.h

typedef struct
{
	int				 maxclients;
	int				 maxclientslimit;
	struct client_s *clients;			 // [maxclients]
	int				 serverflags;		 // episode completion information
	qboolean		 changelevel_issued; // cleared when at SV_SpawnServer
} server_static_t;

//=============================================================================

typedef enum
{
	ss_loading,
	ss_active
} server_state_t;

typedef struct
{
	qboolean active; // false if only a net client

	qboolean paused;
	qboolean loadgame; // handle connections specially
	char	 lastsave[128];

	int	   lastcheck; // used by PF_checkclient
	double lastchecktime;

	qcvm_t qcvm; // Spike: entire qcvm state

	char			 name[64];					 // map name
	char			 modelname[64];				 // maps/<name>.bsp, for model_precache[0]
	const char		*model_precache[MAX_MODELS]; // NULL terminated
	struct qmodel_s *models[MAX_MODELS];
	const char		*sound_precache[MAX_SOUNDS]; // NULL terminated
	const char		*lightstyles[MAX_LIGHTSTYLES];
	server_state_t	 state; // some actions are only valid during load

	sizebuf_t datagram;
	byte	  datagram_buf[MAX_DATAGRAM];

	sizebuf_t reliable_datagram; // copied to all clients at end of frame
	byte	  reliable_datagram_buf[MAX_DATAGRAM];

	sizebuf_t signon;
	byte	  signon_buf[MAX_MSGLEN - 2]; // johnfitz -- was 8192, now uses MAX_MSGLEN

	unsigned protocol; // johnfitz
	unsigned protocolflags;

	sizebuf_t multicast; // selectively copied to clients by the multicast builtin
	byte	  multicast_buf[MAX_DATAGRAM];

	const char *particle_precache[MAX_PARTICLETYPES]; // NULL terminated

	entity_state_t *static_entities;
	int				num_statics;
	int				max_statics;

	struct ambientsound_s
	{
		vec3_t		 origin;
		unsigned int soundindex;
		float		 volume;
		float		 attenuation;
	}  *ambientsounds;
	int num_ambients;
	int max_ambients;

	struct svcustomstat_s
	{
		int		idx;
		int		type;
		int		fld;
		eval_t *ptr;
	} customstats[MAX_CL_STATS * 2]; // strings or numeric...
	size_t numcustomstats;

	int effectsmask; // only enable colored quad/penta dlights in 2021 release
} server_t;

#define NUM_PING_TIMES		  16
#define NUM_BASIC_SPAWN_PARMS 16
#define NUM_TOTAL_SPAWN_PARMS 64

typedef struct client_s
{
	qboolean active;   // false = client is free
	qboolean spawned;  // false = don't send datagrams (set when client acked the first entities)
	qboolean dropasap; // has been told to go to another level
	enum
	{
		PRESPAWN_DONE,
		PRESPAWN_FLUSH = 1,
		//		PRESPAWN_SERVERINFO,
		PRESPAWN_MODELS,
		PRESPAWN_SOUNDS,
		PRESPAWN_PARTICLES,
		PRESPAWN_BASELINES,
		PRESPAWN_STATICS,
		PRESPAWN_AMBIENTS,
		PRESPAWN_SIGNONMSG,
	} sendsignon; // only valid before spawned
	int			 signonidx;
	unsigned int signon_sounds; //
	unsigned int signon_models; //

	double last_message; // reliable messages must be sent
						 // periodically

	struct qsocket_s *netconnection; // communications handle

	usercmd_t cmd;	   // movement
	vec3_t	  wishdir; // intended motion calced from cmd

	sizebuf_t message; // can be added to at any time,
					   // copied and clear once per frame
	byte	  msgbuf[MAX_MSGLEN];
	edict_t	 *edict;	// EDICT_NUM(clientnum+1)
	char	  name[32]; // for printing to other people
	int		  colors;

	float ping_times[NUM_PING_TIMES];
	int	  num_pings; // ping_times[num_pings%NUM_PING_TIMES]

	// spawn parms are carried from level to level
	float spawn_parms[NUM_TOTAL_SPAWN_PARMS];

	// client known data for deltas
	int old_frags;

	sizebuf_t datagram;
	byte	  datagram_buf[MAX_DATAGRAM];

	unsigned int limit_entities;   // vanilla is 600
	unsigned int limit_unreliable; // max allowed size for unreliables
	unsigned int limit_reliable;   // max (total) size of a reliable message.
	unsigned int limit_models;	   //
	unsigned int limit_sounds;	   //
	qboolean	 pextknown;
	unsigned int protocol_pext1;
	unsigned int protocol_pext2;
	unsigned int resendstatsnum[MAX_CL_STATS / 32]; // the stats which need to be resent.
	unsigned int resendstatsstr[MAX_CL_STATS / 32]; // the stats which need to be resent.
	int			 oldstats_i[MAX_CL_STATS];			// previous values of stats. if these differ from the current values, reflag resendstats.
	float		 oldstats_f[MAX_CL_STATS];			// previous values of stats. if these differ from the current values, reflag resendstats.
	char		*oldstats_s[MAX_CL_STATS];
	struct entity_num_state_s
	{
		unsigned int   num; // ascending order, there can be gaps.
		entity_state_t state;
	}			 *previousentities;
	size_t		  numpreviousentities;
	size_t		  maxpreviousentities;
	unsigned int  snapshotresume;
	unsigned int *pendingentities_bits; // UF_ flags for each entity
	size_t		  numpendingentities;	// realloc if too small
#define SENDFLAG_PRESENT 0x80000000u	// tracks that we previously sent one of these ents (resulting in a remove if the ent gets remove()d).
#define SENDFLAG_REMOVE	 0x40000000u	// for packetloss to signal that we need to resend a remove.
#define SENDFLAG_USABLE	 0x00ffffffu	// SendFlags bits that the qc is actually able to use (don't get confused if the mod uses SendFlags=-1).
	struct deltaframe_s
	{ // quick overview of how this stuff actually works:
		// when the server notices a gap in the ack sequence, we walk through the dropped frames and reflag everything that was dropped.
		// if the server isn't tracking enough frames, then we just treat those as dropped;
		// small note: when an entity is new, it re-flags itself as new for the next packet too, this reduces the immediate impact of packetloss on new
		// entities. reflagged state includes stats updates, entity updates, and entity removes.
		int			 sequence; // to see if its stale
		float		 timestamp;
		unsigned int resendstatsnum[MAX_CL_STATS / 32];
		unsigned int resendstatsstr[MAX_CL_STATS / 32];
		struct
		{
			unsigned int num;
			unsigned int ebits;
			unsigned int csqcbits;
		}  *ents;
		int numents; // doesn't contain an entry for every entity, just ones that were sent this frame. no 0 bits
		int maxents;
	}		*frames;
	size_t	 numframes; // preallocated power-of-two
	int		 lastacksequence;
	int		 lastmovemessage;
	double	 lastmovetime;
	qboolean knowntoqc; // putclientinserver was called
} client_t;

//=============================================================================

// edict->movetype values
#define MOVETYPE_NONE		 0 // never moves
#define MOVETYPE_ANGLENOCLIP 1
#define MOVETYPE_ANGLECLIP	 2
#define MOVETYPE_WALK		 3 // gravity
#define MOVETYPE_STEP		 4 // gravity, special edge handling
#define MOVETYPE_FLY		 5
#define MOVETYPE_TOSS		 6 // gravity
#define MOVETYPE_PUSH		 7 // no clip to world, push and crush
#define MOVETYPE_NOCLIP		 8
#define MOVETYPE_FLYMISSILE	 9 // extra size to monsters
#define MOVETYPE_BOUNCE		 10
#define MOVETYPE_GIB		 11 // 2021 rerelease gibs

// edict->solid values
#define SOLID_NOT	   0 // no interaction with other objects
#define SOLID_TRIGGER  1 // touch on edge, but not blocking
#define SOLID_BBOX	   2 // touch on edge, block
#define SOLID_SLIDEBOX 3 // touch on edge, but not an onground
#define SOLID_BSP	   4 // bsp clip, touch on edge, block

// edict->deadflag values
#define DEAD_NO	   0
#define DEAD_DYING 1
#define DEAD_DEAD  2

#define DAMAGE_NO  0
#define DAMAGE_YES 1
#define DAMAGE_AIM 2

// edict->flags
#define FL_FLY			 1
#define FL_SWIM			 2
// #define	FL_GLIMPSE				4
#define FL_CONVEYOR		 4
#define FL_CLIENT		 8
#define FL_INWATER		 16
#define FL_MONSTER		 32
#define FL_GODMODE		 64
#define FL_NOTARGET		 128
#define FL_ITEM			 256
#define FL_ONGROUND		 512
#define FL_PARTIALGROUND 1024 // not all corners are valid
#define FL_WATERJUMP	 2048 // player jumping out of water
#define FL_JUMPRELEASED	 4096 // for jump debouncing

// entity effects

#define EF_BRIGHTFIELD 1
#define EF_MUZZLEFLASH 2
#define EF_BRIGHTLIGHT 4
#define EF_DIMLIGHT	   8

#define SPAWNFLAG_NOT_EASY		 256
#define SPAWNFLAG_NOT_MEDIUM	 512
#define SPAWNFLAG_NOT_HARD		 1024
#define SPAWNFLAG_NOT_DEATHMATCH 2048

#define MSG_BROADCAST	  0 // unreliable to all
#define MSG_ONE			  1 // reliable to one (msg_entity)
#define MSG_ALL			  2 // reliable to all
#define MSG_INIT		  3 // write to the init string
#define MSG_EXT_MULTICAST 4 // temporary buffer that can be splurged more reliably / with more control.
#define MSG_EXT_ENTITY	  5 // for csqc networking. we don't actually support this. I'm just defining it for completeness.

//============================================================================

extern cvar_t teamplay;
extern cvar_t skill;
extern cvar_t deathmatch;
extern cvar_t coop;
extern cvar_t fraglimit;
extern cvar_t timelimit;

extern server_static_t svs; // persistant server info
extern server_t		   sv;	// local server

extern client_t *host_client;

extern edict_t *sv_player;

//===========================================================

void SV_Init (void);

void SV_StartParticle (vec3_t org, vec3_t dir, int color, int count);
void SV_StartSound (edict_t *entity, float *origin, int channel, const char *sample, int volume, float attenuation);
void SV_LocalSound (client_t *client, const char *sample); // for 2021 rerelease

void SV_DropClient (qboolean crash);

void SVFTE_Ack (client_t *client, int sequence);
void SVFTE_DestroyFrames (client_t *client);
void SV_BuildEntityState (edict_t *ent, entity_state_t *state);
void SV_SendClientMessages (void);
void SV_ClearDatagram (void);

int SV_ModelIndex (const char *name);

void SV_SetIdealPitch (void);

void SV_AddUpdates (void);

void SV_ClientThink (void);
void SV_AddClientToServer (struct qsocket_s *ret);

void SV_ClientPrintf (const char *fmt, ...) FUNC_PRINTF (1, 2);
void SV_BroadcastPrintf (const char *fmt, ...) FUNC_PRINTF (1, 2);

void SV_Physics (void);

qboolean SV_CheckBottom (edict_t *ent);
qboolean SV_movestep (edict_t *ent, vec3_t move, qboolean relink);

void SV_WriteClientdataToMessage (client_t *client, sizebuf_t *msg);

void SV_MoveToGoal (void);

void SV_ConnectClient (int clientnum); // called from the netcode to add new clients. also called from pr_ext to spawn new botclients.
void SV_CheckForNewClients (void);
void SV_RunClients (void);
void SV_SaveSpawnparms ();
void SV_SpawnServer (const char *server);

#endif /* _QUAKE_SERVER_H */
