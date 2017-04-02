/*
 * net_defs.h -- functions and data private to the network layer
 * net_sys.h and its dependencies must be included before net_defs.h.
 *
 * Copyright (C) 1996-1997  Id Software, Inc.
 * Copyright (C) 2005-2012  O.Sezer <sezero@users.sourceforge.net>
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

#ifndef __NET_DEFS_H
#define __NET_DEFS_H

struct qsockaddr
{
#if defined(HAVE_SA_LEN)
	unsigned char qsa_len;
	unsigned char qsa_family;
#else
	short qsa_family;
#endif	/* BSD, sockaddr */
	unsigned char qsa_data[14];
};

#define NET_HEADERSIZE		(2 * sizeof(unsigned int))
#define NET_DATAGRAMSIZE	(MAX_DATAGRAM + NET_HEADERSIZE)

// NetHeader flags
#define NETFLAG_LENGTH_MASK	0x0000ffff
#define NETFLAG_DATA		0x00010000
#define NETFLAG_ACK		0x00020000
#define NETFLAG_NAK		0x00040000
#define NETFLAG_EOM		0x00080000
#define NETFLAG_UNRELIABLE	0x00100000
#define NETFLAG_CTL		0x80000000

#if (NETFLAG_LENGTH_MASK & NET_MAXMESSAGE) != NET_MAXMESSAGE
#error "NET_MAXMESSAGE must fit within NETFLAG_LENGTH_MASK"
#endif

#define NET_PROTOCOL_VERSION	3

/**

This is the network info/connection protocol.  It is used to find Quake
servers, get info about them, and connect to them.  Once connected, the
Quake game protocol (documented elsewhere) is used.


General notes:
	game_name is currently always "QUAKE", but is there so this same protocol
		can be used for future games as well; can you say Quake2?

CCREQ_CONNECT
		string	game_name		"QUAKE"
		byte	net_protocol_version	NET_PROTOCOL_VERSION

CCREQ_SERVER_INFO
		string	game_name		"QUAKE"
		byte	net_protocol_version	NET_PROTOCOL_VERSION

CCREQ_PLAYER_INFO
		byte	player_number

CCREQ_RULE_INFO
		string	rule

CCREP_ACCEPT
		long	port

CCREP_REJECT
		string	reason

CCREP_SERVER_INFO
		string	server_address
		string	host_name
		string	level_name
		byte	current_players
		byte	max_players
		byte	protocol_version	NET_PROTOCOL_VERSION

CCREP_PLAYER_INFO
		byte	player_number
		string	name
		long	colors
		long	frags
		long	connect_time
		string	address

CCREP_RULE_INFO
		string	rule
		string	value

	note:
		There are two address forms used above.  The short form is just a
		port number.  The address that goes along with the port is defined as
		"whatever address you receive this reponse from".  This lets us use
		the host OS to solve the problem of multiple host addresses (possibly
		with no routing between them); the host will use the right address
		when we reply to the inbound connection request.  The long from is
		a full address and port in a string.  It is used for returning the
		address of a server that is not running locally.

**/

#define CCREQ_CONNECT		0x01
#define CCREQ_SERVER_INFO	0x02
#define CCREQ_PLAYER_INFO	0x03
#define CCREQ_RULE_INFO		0x04

#define CCREP_ACCEPT		0x81
#define CCREP_REJECT		0x82
#define CCREP_SERVER_INFO	0x83
#define CCREP_PLAYER_INFO	0x84
#define CCREP_RULE_INFO		0x85

typedef struct qsocket_s
{
	struct qsocket_s	*next;
	double		connecttime;
	double		lastMessageTime;
	double		lastSendTime;

	qboolean	disconnected;
	qboolean	canSend;
	qboolean	sendNext;

	int		driver;
	int		landriver;
	sys_socket_t	socket;
	void		*driverdata;

	unsigned int	ackSequence;
	unsigned int	sendSequence;
	unsigned int	unreliableSendSequence;
	int		sendMessageLength;
	byte		sendMessage [NET_MAXMESSAGE];

	unsigned int	receiveSequence;
	unsigned int	unreliableReceiveSequence;
	int		receiveMessageLength;
	byte		receiveMessage [NET_MAXMESSAGE];

	struct qsockaddr	addr;
	char		address[NET_NAMELEN];

} qsocket_t;

extern qsocket_t	*net_activeSockets;
extern qsocket_t	*net_freeSockets;
extern int		net_numsockets;

typedef struct
{
	const char	*name;
	qboolean	initialized;
	sys_socket_t	controlSock;
	sys_socket_t	(*Init) (void);
	void		(*Shutdown) (void);
	void		(*Listen) (qboolean state);
	sys_socket_t	(*Open_Socket) (int port);
	int		(*Close_Socket) (sys_socket_t socketid);
	int		(*Connect) (sys_socket_t socketid, struct qsockaddr *addr);
	sys_socket_t	(*CheckNewConnections) (void);
	int		(*Read) (sys_socket_t socketid, byte *buf, int len, struct qsockaddr *addr);
	int		(*Write) (sys_socket_t socketid, byte *buf, int len, struct qsockaddr *addr);
	int		(*Broadcast) (sys_socket_t socketid, byte *buf, int len);
	const char *	(*AddrToString) (struct qsockaddr *addr);
	int		(*StringToAddr) (const char *string, struct qsockaddr *addr);
	int		(*GetSocketAddr) (sys_socket_t socketid, struct qsockaddr *addr);
	int		(*GetNameFromAddr) (struct qsockaddr *addr, char *name);
	int		(*GetAddrFromName) (const char *name, struct qsockaddr *addr);
	int		(*AddrCompare) (struct qsockaddr *addr1, struct qsockaddr *addr2);
	int		(*GetSocketPort) (struct qsockaddr *addr);
	int		(*SetSocketPort) (struct qsockaddr *addr, int port);
} net_landriver_t;

#define	MAX_NET_DRIVERS		8
extern net_landriver_t	net_landrivers[];
extern const int	net_numlandrivers;

typedef struct
{
	const char	*name;
	qboolean	initialized;
	int		(*Init) (void);
	void		(*Listen) (qboolean state);
	void		(*SearchForHosts) (qboolean xmit);
	qsocket_t	*(*Connect) (const char *host);
	qsocket_t	*(*CheckNewConnections) (void);
	int		(*QGetMessage) (qsocket_t *sock);
	int		(*QSendMessage) (qsocket_t *sock, sizebuf_t *data);
	int		(*SendUnreliableMessage) (qsocket_t *sock, sizebuf_t *data);
	qboolean	(*CanSendMessage) (qsocket_t *sock);
	qboolean	(*CanSendUnreliableMessage) (qsocket_t *sock);
	void		(*Close) (qsocket_t *sock);
	void		(*Shutdown) (void);
} net_driver_t;

extern net_driver_t	net_drivers[];
extern const int	net_numdrivers;

/* Loop driver must always be registered the first */
#define IS_LOOP_DRIVER(p)	((p) == 0)

extern int		net_driverlevel;

extern int		messagesSent;
extern int		messagesReceived;
extern int		unreliableMessagesSent;
extern int		unreliableMessagesReceived;

qsocket_t *NET_NewQSocket (void);
void NET_FreeQSocket(qsocket_t *);
double SetNetTime(void);


#define HOSTCACHESIZE	8

typedef struct
{
	char	name[16];
	char	map[16];
	char	cname[32];
	int		users;
	int		maxusers;
	int		driver;
	int		ldriver;
	struct qsockaddr addr;
} hostcache_t;

extern int hostCacheCount;
extern hostcache_t hostcache[HOSTCACHESIZE];


typedef struct _PollProcedure
{
	struct _PollProcedure	*next;
	double				nextTime;
	void				(*procedure)(void *);
	void				*arg;
} PollProcedure;

void SchedulePollProcedure(PollProcedure *pp, double timeOffset);

#endif	/* __NET_DEFS_H */

