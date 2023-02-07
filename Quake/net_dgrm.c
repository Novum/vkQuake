/*
Copyright (C) 1996-2001 Id Software, Inc.
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

// This is enables a simple IP banning mechanism
// #define BAN_TEST

#include "quakedef.h"
#include "q_stdinc.h"
#include "arch_def.h"
#include "net_sys.h"
#include "net_defs.h"
#include "net_dgrm.h"

// these two macros are to make the code more readable
#define sfunc net_landrivers[sock->landriver]
#define dfunc net_landrivers[net_landriverlevel]

static int net_landriverlevel;

/* statistic counters */
static int packetsSent = 0;
static int packetsReSent = 0;
static int packetsReceived = 0;
static int receivedDuplicateCount = 0;
static int shortPacketCount = 0;
static int droppedDatagrams;

// cvars controlling dpmaster support:
// our servers might as well claim to be 'FTE-Quake' servers. this means FTE can see us, we can see FTE (when its pretending to be nq).
// we additionally look for 'DarkPlaces-Quake' servers too, because we can, but most of those servers will be using dpp7 and will (safely) not respond to our
// ccreq_server_info requests. we are not visible to DarkPlaces users - dp does not support fitz666 so that's not a viable option, at least by default, feel
// free to switch the order if you also change sv_protocol back to 15.
cvar_t sv_reportheartbeats = {"sv_reportheartbeats", "0"};
cvar_t sv_public = {"sv_public", NULL};
cvar_t com_protocolname = {"com_protocolname", "FTE-Quake DarkPlaces-Quake"};
cvar_t net_masters[] = {
	{"net_master1", ""},
	{"net_master2", ""},
	{"net_master3", ""},
	{"net_master4", ""},
	{"net_masterextra1", "master.frag-net.com:27950"},
	{"net_masterextra2", "dpmaster.deathmask.net:27950"},
	{"net_masterextra3", "dpmaster.tchr.no:27950"},
	{NULL}};
cvar_t		  rcon_password = {"rcon_password", ""};
extern cvar_t net_messagetimeout;
extern cvar_t net_connecttimeout;

static struct
{
	unsigned int length;
	unsigned int sequence;
	byte		 data[MAX_DATAGRAM];
} packetBuffer;

static int myDriverLevel;

extern qboolean m_return_onerror;
extern char		m_return_reason[32];
static double	heartbeat_time; // when this is reached, send a heartbeat to all masters.

static char *StrAddr (struct qsockaddr *addr)
{
	static char buf[34];
	byte	   *p = (byte *)addr;
	int			n;

	for (n = 0; n < 16; n++)
		q_snprintf (buf + n * 2, sizeof (buf) - (n * 2), "%02x", *p++);
	return buf;
}

#ifdef BAN_TEST

static struct in_addr banAddr;
static struct in_addr banMask;

static void NET_Ban_f (void)
{
	char addrStr[32];
	char maskStr[32];
	void (*print_fn) (const char *fmt, ...) FUNCP_PRINTF (1, 2);

	if (cmd_source != src_client)
	{
		if (!sv.active)
		{
			Cmd_ForwardToServer ();
			return;
		}
		print_fn = Con_Printf;
	}
	else
	{
		if (pr_global_struct->deathmatch)
			return;
		print_fn = SV_ClientPrintf;
	}

	switch (Cmd_Argc ())
	{
	case 1:
		if (banAddr.s_addr != INADDR_ANY)
		{
			strcpy (addrStr, inet_ntoa (banAddr));
			strcpy (maskStr, inet_ntoa (banMask));
			print_fn ("Banning %s [%s]\n", addrStr, maskStr);
		}
		else
			print_fn ("Banning not active\n");
		break;

	case 2:
		if (q_strcasecmp (Cmd_Argv (1), "off") == 0)
			banAddr.s_addr = INADDR_ANY;
		else
			banAddr.s_addr = inet_addr (Cmd_Argv (1));
		banMask.s_addr = INADDR_NONE;
		break;

	case 3:
		banAddr.s_addr = inet_addr (Cmd_Argv (1));
		banMask.s_addr = inet_addr (Cmd_Argv (2));
		break;

	default:
		print_fn ("BAN ip_address [mask]\n");
		break;
	}
}
#endif // BAN_TEST

int Datagram_SendMessage (qsocket_t *sock, sizebuf_t *data)
{
	unsigned int packetLen;
	unsigned int dataLen;
	unsigned int eom;

#ifdef DEBUG
	if (data->cursize == 0)
		Sys_Error ("Datagram_SendMessage: zero length message");

	if (data->cursize > NET_MAXMESSAGE)
		Sys_Error ("Datagram_SendMessage: message too big: %u", data->cursize);

	if (sock->canSend == false)
		Sys_Error ("SendMessage: called with canSend == false");
#endif

	memcpy (sock->sendMessage, data->data, data->cursize);
	sock->sendMessageLength = data->cursize;

	sock->max_datagram = sock->pending_max_datagram; // this can apply only at the start of a reliable, to avoid issues with acks if its resized later.

	if (data->cursize <= sock->max_datagram)
	{
		dataLen = data->cursize;
		eom = NETFLAG_EOM;
	}
	else
	{
		dataLen = sock->max_datagram;
		eom = 0;
	}
	packetLen = NET_HEADERSIZE + dataLen;

	packetBuffer.length = BigLong (packetLen | (NETFLAG_DATA | eom));
	packetBuffer.sequence = BigLong (sock->sendSequence++);
	memcpy (packetBuffer.data, sock->sendMessage, dataLen);

	sock->canSend = false;

	if (sfunc.Write (sock->socket, (byte *)&packetBuffer, packetLen, &sock->addr) == -1)
		return -1;

	sock->lastSendTime = net_time;
	packetsSent++;
	return 1;
}

static int SendMessageNext (qsocket_t *sock)
{
	unsigned int packetLen;
	unsigned int dataLen;
	unsigned int eom;

	if (sock->sendMessageLength <= sock->max_datagram)
	{
		dataLen = sock->sendMessageLength;
		eom = NETFLAG_EOM;
	}
	else
	{
		dataLen = sock->max_datagram;
		eom = 0;
	}
	packetLen = NET_HEADERSIZE + dataLen;

	packetBuffer.length = BigLong (packetLen | (NETFLAG_DATA | eom));
	packetBuffer.sequence = BigLong (sock->sendSequence++);
	memcpy (packetBuffer.data, sock->sendMessage, dataLen);

	sock->sendNext = false;

	if (sfunc.Write (sock->socket, (byte *)&packetBuffer, packetLen, &sock->addr) == -1)
		return -1;

	sock->lastSendTime = net_time;
	packetsSent++;
	return 1;
}

static int ReSendMessage (qsocket_t *sock)
{
	unsigned int packetLen;
	unsigned int dataLen;
	unsigned int eom;

	if (sock->sendMessageLength <= sock->max_datagram)
	{
		dataLen = sock->sendMessageLength;
		eom = NETFLAG_EOM;
	}
	else
	{
		dataLen = sock->max_datagram;
		eom = 0;
	}
	packetLen = NET_HEADERSIZE + dataLen;

	packetBuffer.length = BigLong (packetLen | (NETFLAG_DATA | eom));
	packetBuffer.sequence = BigLong (sock->sendSequence - 1);
	memcpy (packetBuffer.data, sock->sendMessage, dataLen);

	if (sfunc.Write (sock->socket, (byte *)&packetBuffer, packetLen, &sock->addr) == -1)
		return -1;

	sock->lastSendTime = net_time;
	packetsReSent++;
	return 1;
}

qboolean Datagram_CanSendMessage (qsocket_t *sock)
{
	if (sock->sendNext)
		SendMessageNext (sock);

	return sock->canSend;
}

qboolean Datagram_CanSendUnreliableMessage (qsocket_t *sock)
{
	return true;
}

int Datagram_SendUnreliableMessage (qsocket_t *sock, sizebuf_t *data)
{
	int packetLen;

#ifdef DEBUG
	if (data->cursize == 0)
		Sys_Error ("Datagram_SendUnreliableMessage: zero length message");

	if (data->cursize > MAX_DATAGRAM)
		Sys_Error ("Datagram_SendUnreliableMessage: message too big: %u", data->cursize);
#endif

	packetLen = NET_HEADERSIZE + data->cursize;

	packetBuffer.length = BigLong (packetLen | NETFLAG_UNRELIABLE);
	packetBuffer.sequence = BigLong (sock->unreliableSendSequence++);
	memcpy (packetBuffer.data, data->data, data->cursize);

	if (sfunc.Write (sock->socket, (byte *)&packetBuffer, packetLen, &sock->addr) == -1)
		return -1;

	packetsSent++;
	return 1;
}

static void _Datagram_ServerControlPacket (sys_socket_t acceptsock, struct qsockaddr *clientaddr, byte *data, unsigned int length);

qboolean Datagram_ProcessPacket (unsigned int length, qsocket_t *sock)
{
	unsigned int flags;
	unsigned int sequence;
	unsigned int count;

	if (length < NET_HEADERSIZE)
	{
		shortPacketCount++;
		return false;
	}

	length = BigLong (packetBuffer.length);
	flags = length & (~NETFLAG_LENGTH_MASK);
	length &= NETFLAG_LENGTH_MASK;

	if (flags & NETFLAG_CTL)
		return false; // should only be for OOB packets.

	sequence = BigLong (packetBuffer.sequence);
	packetsReceived++;

	if (flags & NETFLAG_UNRELIABLE)
	{
		if (sequence < sock->unreliableReceiveSequence)
		{
			Con_DPrintf ("Got a stale datagram\n");
			return false;
		}
		if (sequence != sock->unreliableReceiveSequence)
		{
			count = sequence - sock->unreliableReceiveSequence;
			droppedDatagrams += count;
			Con_DPrintf ("Dropped %u datagram(s)\n", count);
		}
		sock->unreliableReceiveSequence = sequence + 1;

		length -= NET_HEADERSIZE;

		if (length > (unsigned int)net_message.maxsize)
		{ // is this even possible? maybe it will be in the future! either way, no sys_errors please.
			Con_Printf ("Over-sized unreliable\n");
			return true;
		}
		SZ_Clear (&net_message);
		SZ_Write (&net_message, packetBuffer.data, length);

		unreliableMessagesReceived++;
		return true; // parse the unreliable
	}

	if (flags & NETFLAG_ACK)
	{
		if (sequence != (sock->sendSequence - 1))
		{
			Con_DPrintf ("Stale ACK received\n");
			return false;
		}
		if (sequence == sock->ackSequence)
		{
			sock->ackSequence++;
			if (sock->ackSequence != sock->sendSequence)
				Con_DPrintf ("ack sequencing error\n");
		}
		else
		{
			Con_DPrintf ("Duplicate ACK received\n");
			return false;
		}
		sock->sendMessageLength -= sock->max_datagram;
		if (sock->sendMessageLength > 0)
		{
			memmove (sock->sendMessage, sock->sendMessage + sock->max_datagram, sock->sendMessageLength);
			sock->sendNext = true;
		}
		else
		{
			sock->sendMessageLength = 0;
			sock->canSend = true;
		}
		return false;
	}

	if (flags & NETFLAG_DATA)
	{
		packetBuffer.length = BigLong (NET_HEADERSIZE | NETFLAG_ACK);
		packetBuffer.sequence = BigLong (sequence);
		sfunc.Write (sock->socket, (byte *)&packetBuffer, NET_HEADERSIZE, &sock->addr);

		if (sequence != sock->receiveSequence)
		{
			receivedDuplicateCount++;
			return false;
		}
		sock->receiveSequence++;

		length -= NET_HEADERSIZE;

		if (flags & NETFLAG_EOM)
		{
			if (sock->receiveMessageLength + length > (unsigned int)net_message.maxsize)
			{
				Con_Printf ("Over-sized reliable\n");
				return true;
			}
			SZ_Clear (&net_message);
			SZ_Write (&net_message, sock->receiveMessage, sock->receiveMessageLength);
			SZ_Write (&net_message, packetBuffer.data, length);
			sock->receiveMessageLength = 0;

			messagesReceived++;
			return true; // parse this reliable!
		}

		if (sock->receiveMessageLength + length > sizeof (sock->receiveMessage))
		{
			Con_Printf ("Over-sized reliable\n");
			return true;
		}
		memcpy (sock->receiveMessage + sock->receiveMessageLength, packetBuffer.data, length);
		sock->receiveMessageLength += length;
		return false; // still watiting for the eom
	}
	// unknown flags
	Con_DPrintf ("Unknown packet flags\n");
	return false;
}

qsocket_t *Datagram_GetAnyMessage (void)
{
	qsocket_t		*s;
	struct qsockaddr addr;
	int				 length;
	for (net_landriverlevel = 0; net_landriverlevel < net_numlandrivers; net_landriverlevel++)
	{
		sys_socket_t sock;
		if (!dfunc.initialized)
			continue;
		sock = dfunc.listeningSock;
		if (sock == INVALID_SOCKET)
			continue;

		while (1)
		{
			length = dfunc.Read (sock, (byte *)&packetBuffer, NET_DATAGRAMSIZE, &addr);
			if (length == -1 || !length)
			{
				// no more packets, move on to the next.
				break;
			}

			if (length < 4)
				continue;
			if (BigLong (packetBuffer.length) & NETFLAG_CTL)
			{
				_Datagram_ServerControlPacket (sock, &addr, (byte *)&packetBuffer, length);
				continue;
			}

			// figure out which qsocket it was for
			for (s = net_activeSockets; s; s = s->next)
			{
				if (s->driver != net_driverlevel)
					continue;
				if (s->disconnected)
					continue;
				if (!s->isvirtual)
					continue;
				if (dfunc.AddrCompare (&addr, &s->addr) == 0)
				{
					// okay, looks like this is us. try to process it, and if there's new data
					if (Datagram_ProcessPacket (length, s))
					{
						s->lastMessageTime = net_time;
						return s; // the server needs to parse that packet.
					}
				}
			}
			// stray packet... ignore it and just try the next
		}
	}
	for (s = net_activeSockets; s; s = s->next)
	{
		if (s->driver != net_driverlevel)
			continue;
		if (!s->isvirtual)
			continue;

		if (s->sendNext)
			SendMessageNext (s);
		if (!s->canSend)
			if ((net_time - s->lastSendTime) > 1.0)
				ReSendMessage (s);

		if (net_time - s->lastMessageTime > ((!s->ackSequence) ? net_connecttimeout.value : net_messagetimeout.value))
		{ // timed out, kick them
			// FIXME: add a proper challenge rather than assuming spoofers won't fake acks
			int i;
			for (i = 0; i < svs.maxclients; i++)
			{
				if (svs.clients[i].netconnection == s)
				{
					host_client = &svs.clients[i];
					SV_DropClient (false);
					break;
				}
			}
		}
	}

	return NULL;
}

int Datagram_GetMessage (qsocket_t *sock)
{
	unsigned int	 length;
	unsigned int	 flags;
	int				 ret = 0;
	struct qsockaddr readaddr;
	unsigned int	 sequence;
	unsigned int	 count;

	if (!sock->canSend)
		if ((net_time - sock->lastSendTime) > 1.0)
			ReSendMessage (sock);

	while (1)
	{
		length = (unsigned int)sfunc.Read (sock->socket, (byte *)&packetBuffer, NET_DATAGRAMSIZE, &readaddr);

		//	if ((rand() & 255) > 220)
		//		continue;

		if (length == 0)
			break;

		if (length == (unsigned int)-1)
		{
			Con_Printf ("Read error\n");
			return -1;
		}

		if (sfunc.AddrCompare (&readaddr, &sock->addr) != 0)
		{
			Con_Printf ("Stray/Forged packet received\n");
			Con_Printf ("Expected: %s\n", sfunc.AddrToString (&sock->addr, false));
			Con_Printf ("Received: %s\n", sfunc.AddrToString (&readaddr, false));
			continue;
		}

		if (length < NET_HEADERSIZE)
		{
			shortPacketCount++;
			continue;
		}

		length = BigLong (packetBuffer.length);
		flags = length & (~NETFLAG_LENGTH_MASK);
		length &= NETFLAG_LENGTH_MASK;

		if (flags & NETFLAG_CTL)
			continue;

		sequence = BigLong (packetBuffer.sequence);
		packetsReceived++;

		if (flags & NETFLAG_UNRELIABLE)
		{
			if (sequence < sock->unreliableReceiveSequence)
			{
				Con_DPrintf ("Got a stale datagram\n");
				ret = 0;
				break;
			}
			if (sequence != sock->unreliableReceiveSequence)
			{
				count = sequence - sock->unreliableReceiveSequence;
				droppedDatagrams += count;
				Con_DPrintf ("Dropped %u datagram(s)\n", count);
			}
			sock->unreliableReceiveSequence = sequence + 1;

			length -= NET_HEADERSIZE;

			SZ_Clear (&net_message);
			SZ_Write (&net_message, packetBuffer.data, length);

			ret = 2;
			break;
		}

		if (flags & NETFLAG_ACK)
		{
			if (sequence != (sock->sendSequence - 1))
			{
				Con_DPrintf ("Stale ACK received\n");
				continue;
			}
			if (sequence == sock->ackSequence)
			{
				sock->ackSequence++;
				if (sock->ackSequence != sock->sendSequence)
					Con_DPrintf ("ack sequencing error\n");
			}
			else
			{
				Con_DPrintf ("Duplicate ACK received\n");
				continue;
			}
			sock->sendMessageLength -= sock->max_datagram;
			if (sock->sendMessageLength > 0)
			{
				memmove (sock->sendMessage, sock->sendMessage + sock->max_datagram, sock->sendMessageLength);
				sock->sendNext = true;
			}
			else
			{
				sock->sendMessageLength = 0;
				sock->canSend = true;
			}
			continue;
		}

		if (flags & NETFLAG_DATA)
		{
			packetBuffer.length = BigLong (NET_HEADERSIZE | NETFLAG_ACK);
			packetBuffer.sequence = BigLong (sequence);
			sfunc.Write (sock->socket, (byte *)&packetBuffer, NET_HEADERSIZE, &readaddr);

			if (sequence != sock->receiveSequence)
			{
				receivedDuplicateCount++;
				continue;
			}
			sock->receiveSequence++;

			length -= NET_HEADERSIZE;

			if (flags & NETFLAG_EOM)
			{
				if (sock->receiveMessageLength + length > (unsigned int)net_message.maxsize)
				{
					Con_Printf ("Over-sized reliable\n");
					return -1;
				}
				SZ_Clear (&net_message);
				SZ_Write (&net_message, sock->receiveMessage, sock->receiveMessageLength);
				SZ_Write (&net_message, packetBuffer.data, length);
				sock->receiveMessageLength = 0;

				ret = 1;
				break;
			}

			if (sock->receiveMessageLength + length > sizeof (sock->receiveMessage))
			{
				Con_Printf ("Over-sized reliable\n");
				return -1;
			}
			memcpy (sock->receiveMessage + sock->receiveMessageLength, packetBuffer.data, length);
			sock->receiveMessageLength += length;
			continue;
		}
	}

	if (sock->sendNext)
		SendMessageNext (sock);

	return ret;
}

static void PrintStats (qsocket_t *s)
{
	Con_Printf ("canSend = %4u   \n", s->canSend);
	Con_Printf ("sendSeq = %4u   ", s->sendSequence);
	Con_Printf ("recvSeq = %4u   \n", s->receiveSequence);
	Con_Printf ("\n");
}

static void NET_Stats_f (void)
{
	qsocket_t *s;

	if (Cmd_Argc () == 1)
	{
		Con_Printf ("unreliable messages sent   = %i\n", unreliableMessagesSent);
		Con_Printf ("unreliable messages recv   = %i\n", unreliableMessagesReceived);
		Con_Printf ("reliable messages sent     = %i\n", messagesSent);
		Con_Printf ("reliable messages received = %i\n", messagesReceived);
		Con_Printf ("packetsSent                = %i\n", packetsSent);
		Con_Printf ("packetsReSent              = %i\n", packetsReSent);
		Con_Printf ("packetsReceived            = %i\n", packetsReceived);
		Con_Printf ("receivedDuplicateCount     = %i\n", receivedDuplicateCount);
		Con_Printf ("shortPacketCount           = %i\n", shortPacketCount);
		Con_Printf ("droppedDatagrams           = %i\n", droppedDatagrams);
	}
	else if (strcmp (Cmd_Argv (1), "*") == 0)
	{
		for (s = net_activeSockets; s; s = s->next)
			PrintStats (s);
		for (s = net_freeSockets; s; s = s->next)
			PrintStats (s);
	}
	else
	{
		for (s = net_activeSockets; s; s = s->next)
		{
			if (q_strcasecmp (Cmd_Argv (1), s->trueaddress) == 0 || q_strcasecmp (Cmd_Argv (1), s->maskedaddress) == 0)
				break;
		}

		if (s == NULL)
		{
			for (s = net_freeSockets; s; s = s->next)
			{
				if (q_strcasecmp (Cmd_Argv (1), s->trueaddress) == 0 || q_strcasecmp (Cmd_Argv (1), s->maskedaddress) == 0)
					break;
			}
		}

		if (s == NULL)
			return;

		PrintStats (s);
	}
}

// recognize ip:port (based on ProQuake)
static const char *Strip_Port (const char *host)
{
	static char noport[MAX_QPATH];
	/* array size as in Host_Connect_f() */
	char	   *p;
	int			port;

	if (!host || !*host)
		return host;
	q_strlcpy (noport, host, sizeof (noport));
	if ((p = strrchr (noport, ':')) == NULL)
		return host;
	if (strchr (p, ']'))
		return host; //[::] should not be considered port 0
	*p++ = '\0';
	port = atoi (p);
	if (port > 0 && port < 65536 && port != net_hostport)
	{
		net_hostport = port;
		Con_Printf ("Port set to %d\n", net_hostport);
	}
	return noport;
}

static qboolean		testInProgress = false;
static int			testPollCount;
static int			testDriver;
static sys_socket_t testSocket;

static void			 Test_Poll (void *);
static PollProcedure testPollProcedure = {NULL, 0.0, Test_Poll};

static void Test_Poll (void *unused)
{
	struct qsockaddr clientaddr;
	int				 control;
	int				 len;
	char			 name[32];
	char			 address[64];
	int				 colors;
	int				 frags;
	int				 connectTime;

	net_landriverlevel = testDriver;

	while (1)
	{
		len = dfunc.Read (testSocket, net_message.data, net_message.maxsize, &clientaddr);
		if (len < (int)sizeof (int))
			break;

		net_message.cursize = len;

		MSG_BeginReading ();
		control = BigLong (*((int *)net_message.data));
		MSG_ReadLong ();
		if (control == -1)
			break;
		if ((control & (~NETFLAG_LENGTH_MASK)) != (int)NETFLAG_CTL)
			break;
		if ((control & NETFLAG_LENGTH_MASK) != len)
			break;

		if (MSG_ReadByte () != CCREP_PLAYER_INFO)
			Sys_Error ("Unexpected repsonse to Player Info request\n");

		MSG_ReadByte (); /* playerNumber */
		strcpy (name, MSG_ReadString ());
		colors = MSG_ReadLong ();
		frags = MSG_ReadLong ();
		connectTime = MSG_ReadLong ();
		strcpy (address, MSG_ReadString ());

		Con_Printf ("%s\n  frags:%3i  colors:%d %d  time:%d\n  %s\n", name, frags, colors >> 4, colors & 0x0f, connectTime / 60, address);
	}

	testPollCount--;
	if (testPollCount)
	{
		SchedulePollProcedure (&testPollProcedure, 0.1);
	}
	else
	{
		dfunc.Close_Socket (testSocket);
		testInProgress = false;
	}
}

static void Test_f (void)
{
	const char		*host;
	size_t			 n;
	size_t			 maxusers = MAX_SCOREBOARD;
	struct qsockaddr sendaddr;

	if (testInProgress)
		return;

	host = Strip_Port (Cmd_Argv (1));

	if (host && hostCacheCount)
	{
		for (n = 0; n < hostCacheCount; n++)
		{
			if (q_strcasecmp (host, hostcache[n].name) == 0)
			{
				if (hostcache[n].driver != myDriverLevel)
					continue;
				net_landriverlevel = hostcache[n].ldriver;
				maxusers = hostcache[n].maxusers;
				memcpy (&sendaddr, &hostcache[n].addr, sizeof (struct qsockaddr));
				break;
			}
		}

		if (n < hostCacheCount)
			goto JustDoIt;
	}

	for (net_landriverlevel = 0; net_landriverlevel < net_numlandrivers; net_landriverlevel++)
	{
		if (!net_landrivers[net_landriverlevel].initialized)
			continue;

		// see if we can resolve the host name
		if (dfunc.GetAddrFromName (host, &sendaddr) != -1)
			break;
	}

	if (net_landriverlevel == net_numlandrivers)
	{
		Con_Printf ("Could not resolve %s\n", host);
		return;
	}

JustDoIt:
	testSocket = dfunc.Open_Socket (0);
	if (testSocket == INVALID_SOCKET)
		return;

	testInProgress = true;
	testPollCount = 20;
	testDriver = net_landriverlevel;

	for (n = 0; n < maxusers; n++)
	{
		SZ_Clear (&net_message);
		// save space for the header, filled in later
		MSG_WriteLong (&net_message, 0);
		MSG_WriteByte (&net_message, CCREQ_PLAYER_INFO);
		MSG_WriteByte (&net_message, n);
		*((int *)net_message.data) = BigLong (NETFLAG_CTL | (net_message.cursize & NETFLAG_LENGTH_MASK));
		dfunc.Write (testSocket, net_message.data, net_message.cursize, &sendaddr);
	}
	SZ_Clear (&net_message);
	SchedulePollProcedure (&testPollProcedure, 0.1);
}

static qboolean		test2InProgress = false;
static int			test2Driver;
static sys_socket_t test2Socket;

static void			 Test2_Poll (void *);
static PollProcedure test2PollProcedure = {NULL, 0.0, Test2_Poll};

static void Test2_Poll (void *unused)
{
	struct qsockaddr clientaddr;
	int				 control;
	int				 len;
	char			 name[256];
	char			 value[256];

	net_landriverlevel = test2Driver;
	name[0] = 0;

	len = dfunc.Read (test2Socket, net_message.data, net_message.maxsize, &clientaddr);
	if (len < (int)sizeof (int))
		goto Reschedule;

	net_message.cursize = len;

	MSG_BeginReading ();
	control = BigLong (*((int *)net_message.data));
	MSG_ReadLong ();
	if (control == -1)
		goto Error;
	if ((control & (~NETFLAG_LENGTH_MASK)) != (int)NETFLAG_CTL)
		goto Error;
	if ((control & NETFLAG_LENGTH_MASK) != len)
		goto Error;

	if (MSG_ReadByte () != CCREP_RULE_INFO)
		goto Error;

	strcpy (name, MSG_ReadString ());
	if (name[0] == 0)
		goto Done;
	strcpy (value, MSG_ReadString ());

	Con_Printf ("%-16.16s  %-16.16s\n", name, value);

	SZ_Clear (&net_message);
	// save space for the header, filled in later
	MSG_WriteLong (&net_message, 0);
	MSG_WriteByte (&net_message, CCREQ_RULE_INFO);
	MSG_WriteString (&net_message, name);
	*((int *)net_message.data) = BigLong (NETFLAG_CTL | (net_message.cursize & NETFLAG_LENGTH_MASK));
	dfunc.Write (test2Socket, net_message.data, net_message.cursize, &clientaddr);
	SZ_Clear (&net_message);

Reschedule:
	SchedulePollProcedure (&test2PollProcedure, 0.05);
	return;

Error:
	Con_Printf ("Unexpected repsonse to Rule Info request\n");
Done:
	dfunc.Close_Socket (test2Socket);
	test2InProgress = false;
	return;
}

static void Test2_f (void)
{
	const char		*host;
	size_t			 n;
	struct qsockaddr sendaddr;

	if (test2InProgress)
		return;

	host = Strip_Port (Cmd_Argv (1));

	if (host && hostCacheCount)
	{
		for (n = 0; n < hostCacheCount; n++)
		{
			if (q_strcasecmp (host, hostcache[n].name) == 0)
			{
				if (hostcache[n].driver != myDriverLevel)
					continue;
				net_landriverlevel = hostcache[n].ldriver;
				memcpy (&sendaddr, &hostcache[n].addr, sizeof (struct qsockaddr));
				break;
			}
		}

		if (n < hostCacheCount)
			goto JustDoIt;
	}

	for (net_landriverlevel = 0; net_landriverlevel < net_numlandrivers; net_landriverlevel++)
	{
		if (!net_landrivers[net_landriverlevel].initialized)
			continue;

		// see if we can resolve the host name
		if (dfunc.GetAddrFromName (host, &sendaddr) != -1)
			break;
	}

	if (net_landriverlevel == net_numlandrivers)
	{
		Con_Printf ("Could not resolve %s\n", host);
		return;
	}

JustDoIt:
	test2Socket = dfunc.Open_Socket (0);
	if (test2Socket == INVALID_SOCKET)
		return;

	test2InProgress = true;
	test2Driver = net_landriverlevel;

	SZ_Clear (&net_message);
	// save space for the header, filled in later
	MSG_WriteLong (&net_message, 0);
	MSG_WriteByte (&net_message, CCREQ_RULE_INFO);
	MSG_WriteString (&net_message, "");
	*((int *)net_message.data) = BigLong (NETFLAG_CTL | (net_message.cursize & NETFLAG_LENGTH_MASK));
	dfunc.Write (test2Socket, net_message.data, net_message.cursize, &sendaddr);
	SZ_Clear (&net_message);
	SchedulePollProcedure (&test2PollProcedure, 0.05);
}

int Datagram_Init (void)
{
	int			 i, num_inited;
	sys_socket_t csock;

#ifdef BAN_TEST
	banAddr.s_addr = INADDR_ANY;
	banMask.s_addr = INADDR_NONE;
#endif
	myDriverLevel = net_driverlevel;

	Cmd_AddCommand ("net_stats", NET_Stats_f);

	if (safemode || COM_CheckParm ("-nolan"))
		return -1;

	num_inited = 0;
	for (i = 0; i < net_numlandrivers; i++)
	{
		csock = net_landrivers[i].Init ();
		if (csock == INVALID_SOCKET)
			continue;
		net_landrivers[i].initialized = true;
		net_landrivers[i].controlSock = csock;
		net_landrivers[i].listeningSock = INVALID_SOCKET;
		num_inited++;
	}

	if (num_inited == 0)
		return -1;

	Cmd_AddCommand ("test", Test_f);
	Cmd_AddCommand ("test2", Test2_f);

	return 0;
}

void Datagram_Shutdown (void)
{
	int i;

	Datagram_Listen (false);

	//
	// shutdown the lan drivers
	//
	for (i = 0; i < net_numlandrivers; i++)
	{
		if (net_landrivers[i].initialized)
		{
			net_landrivers[i].Shutdown ();
			net_landrivers[i].initialized = false;
		}
	}
}

void Datagram_Close (qsocket_t *sock)
{
	if (sock->isvirtual)
	{
		sock->isvirtual = false;
		sock->socket = INVALID_SOCKET;
	}
	else
		sfunc.Close_Socket (sock->socket);
}

void Datagram_Listen (qboolean state)
{
	qsocket_t *s;
	int		   i;
	qboolean   islistening = false;

	heartbeat_time = 0; // reset it

	for (i = 0; i < net_numlandrivers; i++)
	{
		if (net_landrivers[i].initialized)
		{
			net_landrivers[i].listeningSock = net_landrivers[i].Listen (state);
			if (net_landrivers[i].listeningSock != INVALID_SOCKET)
				islistening = true;

			for (s = net_activeSockets; s; s = s->next)
			{
				if (s->isvirtual)
				{
					s->isvirtual = false;
					s->socket = INVALID_SOCKET;
				}
			}
		}
	}
	if (state && !islistening)
	{
		if (isDedicated)
			Sys_Error ("Unable to open any listening sockets\n");
		Con_Warning ("Unable to open any listening sockets\n");
	}
}

static struct qsockaddr rcon_response_address;
static sys_socket_t		rcon_response_socket;
static sys_socket_t		rcon_response_landriver;
void					Datagram_Rcon_Flush (const char *text)
{
	sizebuf_t msg;
	byte	  buffer[8192];
	msg.data = buffer;
	msg.maxsize = sizeof (buffer);
	msg.allowoverflow = true;
	SZ_Clear (&msg);
	// save space for the header, filled in later
	MSG_WriteLong (&msg, 0);
	MSG_WriteByte (&msg, CCREP_RCON);
	MSG_WriteString (&msg, text);
	if (msg.overflowed)
		return;
	*((int *)msg.data) = BigLong (NETFLAG_CTL | (msg.cursize & NETFLAG_LENGTH_MASK));
	net_landrivers[rcon_response_landriver].Write (rcon_response_socket, msg.data, msg.cursize, &rcon_response_address);
}

static void _Datagram_ServerControlPacket (sys_socket_t acceptsock, struct qsockaddr *clientaddr, byte *data, unsigned int length)
{
	struct qsockaddr newaddr;
	qsocket_t		*sock;
	qsocket_t		*s;
	int				 command;
	int				 control;
	int				 ret;
	int				 plnum;
	int				 mod; //, mod_ver, mod_flags, mod_passwd;	//proquake extensions

	control = BigLong (*((int *)data));
	if (control == -1)
	{
		if (!sv_public.value)
			return;
		data[length] = 0;
		Cmd_TokenizeString ((char *)data + 4);
		if (!strcmp (Cmd_Argv (0), "getinfo") || !strcmp (Cmd_Argv (0), "getstatus"))
		{
			// master, as well as other clients, may send us one of these two packets to get our serverinfo data
			// masters only really need gamename and player counts. actual clients might want player names too.
			qboolean	 full = !strcmp (Cmd_Argv (0), "getstatus");
			char		 cookie[128];
			const char	*str = Cmd_Args ();
			const char	*gamedir = COM_GetGameNames (false);
			unsigned int numclients = 0, numbots = 0;
			int			 i;
			size_t		 j;
			if (!str)
				str = "";
			q_strlcpy (cookie, str, sizeof (cookie));

			for (i = 0; i < svs.maxclients; i++)
			{
				if (svs.clients[i].active)
				{
					numclients++;
					if (!svs.clients[i].netconnection)
						numbots++;
				}
			}

			SZ_Clear (&net_message);
			MSG_WriteLong (&net_message, -1);
			MSG_WriteString (&net_message, full ? "statusResponse" : "infoResponse\n");
			net_message.cursize--;
			COM_Parse (com_protocolname.string);
			if (*com_token) // the master server needs this. This tells the master which game we should be listed as.
			{
				MSG_WriteString (&net_message, va ("\\gamename\\%s", com_token));
				net_message.cursize--;
			}
			MSG_WriteString (&net_message, "\\protocol\\3");
			net_message.cursize--; // this is stupid
			MSG_WriteString (&net_message, "\\ver\\" ENGINE_NAME_AND_VER);
			net_message.cursize--;
			MSG_WriteString (&net_message, va ("\\nqprotocol\\%u", sv.protocol));
			net_message.cursize--;
			if (*gamedir)
			{
				MSG_WriteString (&net_message, va ("\\modname\\%s", gamedir));
				net_message.cursize--;
			}
			if (*sv.name)
			{
				MSG_WriteString (&net_message, va ("\\mapname\\%s", sv.name));
				net_message.cursize--;
			}
			if (*deathmatch.string)
			{
				MSG_WriteString (&net_message, va ("\\deathmatch\\%s", deathmatch.string));
				net_message.cursize--;
			}
			if (*teamplay.string)
			{
				MSG_WriteString (&net_message, va ("\\teamplay\\%s", teamplay.string));
				net_message.cursize--;
			}
			if (*hostname.string)
			{
				MSG_WriteString (&net_message, va ("\\hostname\\%s", hostname.string));
				net_message.cursize--;
			}
			MSG_WriteString (&net_message, va ("\\clients\\%u", numclients));
			net_message.cursize--;
			if (numbots)
			{
				MSG_WriteString (&net_message, va ("\\bots\\%u", numbots));
				net_message.cursize--;
			}
			MSG_WriteString (&net_message, va ("\\sv_maxclients\\%i", svs.maxclients));
			net_message.cursize--;
			if (*cookie)
			{
				MSG_WriteString (&net_message, va ("\\challenge\\%s", cookie));
				net_message.cursize--;
			}

			if (full)
			{
				for (i = 0; i < svs.maxclients; i++)
				{
					if (svs.clients[i].active)
					{
						float total = 0;
						for (j = 0; j < NUM_PING_TIMES; j++)
							total += svs.clients[i].ping_times[j];
						total /= NUM_PING_TIMES;
						total *= 1000; // put it in ms

						MSG_WriteString (
							&net_message, va ("\n%i %i %i_%i \"%s\"", svs.clients[i].old_frags, (int)total, svs.clients[i].colors & 15,
											  svs.clients[i].colors >> 4, svs.clients[i].name));
						net_message.cursize--;
					}
				}
			}

			dfunc.Write (acceptsock, net_message.data, net_message.cursize, clientaddr);
			SZ_Clear (&net_message);
		}
		return;
	}
	if ((control & (~NETFLAG_LENGTH_MASK)) != (int)NETFLAG_CTL)
		return;
	if ((control & NETFLAG_LENGTH_MASK) != length)
		return;

	// sigh... FIXME: potentially abusive memcpy
	SZ_Clear (&net_message);
	SZ_Write (&net_message, data, length);

	MSG_BeginReading ();
	MSG_ReadLong ();

	command = MSG_ReadByte ();
	if (command == CCREQ_SERVER_INFO)
	{
		if (strcmp (MSG_ReadString (), "QUAKE") != 0)
			return;

		SZ_Clear (&net_message);
		// save space for the header, filled in later
		MSG_WriteLong (&net_message, 0);
		MSG_WriteByte (&net_message, CCREP_SERVER_INFO);
		dfunc.GetSocketAddr (acceptsock, &newaddr);
		MSG_WriteString (&net_message, dfunc.AddrToString (&newaddr, false));
		MSG_WriteString (&net_message, hostname.string);
		MSG_WriteString (&net_message, sv.name);
		MSG_WriteByte (&net_message, net_activeconnections);
		MSG_WriteByte (&net_message, svs.maxclients);
		MSG_WriteByte (&net_message, NET_PROTOCOL_VERSION);
		*((int *)net_message.data) = BigLong (NETFLAG_CTL | (net_message.cursize & NETFLAG_LENGTH_MASK));
		dfunc.Write (acceptsock, net_message.data, net_message.cursize, clientaddr);
		SZ_Clear (&net_message);
		return;
	}

	if (command == CCREQ_PLAYER_INFO)
	{
		int		  playerNumber;
		int		  activeNumber;
		int		  clientNumber;
		client_t *client;

		playerNumber = MSG_ReadByte ();
		activeNumber = -1;

		for (clientNumber = 0, client = svs.clients; clientNumber < svs.maxclients; clientNumber++, client++)
		{
			if (client->active)
			{
				activeNumber++;
				if (activeNumber == playerNumber)
					break;
			}
		}

		if (clientNumber == svs.maxclients)
			return;

		SZ_Clear (&net_message);
		// save space for the header, filled in later
		MSG_WriteLong (&net_message, 0);
		MSG_WriteByte (&net_message, CCREP_PLAYER_INFO);
		MSG_WriteByte (&net_message, playerNumber);
		MSG_WriteString (&net_message, client->name);
		MSG_WriteLong (&net_message, client->colors);
		MSG_WriteLong (&net_message, (int)client->edict->v.frags);
		if (!client->netconnection)
		{
			MSG_WriteLong (&net_message, 0);
			MSG_WriteString (&net_message, "Bot");
		}
		else
		{
			MSG_WriteLong (&net_message, (int)(net_time - client->netconnection->connecttime));
			MSG_WriteString (&net_message, NET_QSocketGetMaskedAddressString (client->netconnection));
		}
		*((int *)net_message.data) = BigLong (NETFLAG_CTL | (net_message.cursize & NETFLAG_LENGTH_MASK));
		dfunc.Write (acceptsock, net_message.data, net_message.cursize, clientaddr);
		SZ_Clear (&net_message);

		return;
	}

	if (command == CCREQ_RULE_INFO)
	{
		const char *prevCvarName;
		cvar_t	   *var;

		// find the search start location
		prevCvarName = MSG_ReadString ();
		var = Cvar_FindVarAfter (prevCvarName, CVAR_SERVERINFO);

		// send the response
		SZ_Clear (&net_message);
		// save space for the header, filled in later
		MSG_WriteLong (&net_message, 0);
		MSG_WriteByte (&net_message, CCREP_RULE_INFO);
		if (var)
		{
			MSG_WriteString (&net_message, var->name);
			MSG_WriteString (&net_message, var->string);
		}
		*((int *)net_message.data) = BigLong (NETFLAG_CTL | (net_message.cursize & NETFLAG_LENGTH_MASK));
		dfunc.Write (acceptsock, net_message.data, net_message.cursize, clientaddr);
		SZ_Clear (&net_message);

		return;
	}

	if (command == CCREQ_RCON)
	{
		const char *password = MSG_ReadString (); // FIXME: this really needs crypto
		const char *response;

		rcon_response_address = *clientaddr;
		rcon_response_socket = acceptsock;
		rcon_response_landriver = net_landriverlevel;

		if (!*rcon_password.string)
			response = "rcon is not enabled on this server";
		else if (!strcmp (password, rcon_password.string))
		{
			Con_Redirect (Datagram_Rcon_Flush);
			Cmd_ExecuteString (MSG_ReadString (), src_command);
			Con_Redirect (NULL);
			return;
		}
		else if (!strcmp (password, "password"))
			response = "What, you really thought that would work? Seriously?";
		else if (!strcmp (password, "thebackdoor"))
			response = "Oh look! You found the backdoor. Don't let it slam you in the face on your way out.";
		else
			response = "Your password is just WRONG dude.";

		Datagram_Rcon_Flush (response);
		return;
	}

	if (command != CCREQ_CONNECT)
		return;

	if (strcmp (MSG_ReadString (), "QUAKE") != 0)
		return;

	if (MSG_ReadByte () != NET_PROTOCOL_VERSION)
	{
		SZ_Clear (&net_message);
		// save space for the header, filled in later
		MSG_WriteLong (&net_message, 0);
		MSG_WriteByte (&net_message, CCREP_REJECT);
		MSG_WriteString (&net_message, "Incompatible version.\n");
		*((int *)net_message.data) = BigLong (NETFLAG_CTL | (net_message.cursize & NETFLAG_LENGTH_MASK));
		dfunc.Write (acceptsock, net_message.data, net_message.cursize, clientaddr);
		SZ_Clear (&net_message);
		return;
	}

	// read proquake extensions
	mod = MSG_ReadByte ();
	if (msg_badread)
		mod = 0;
#if 0
	mod_ver = MSG_ReadByte();
	if (msg_badread) mod_ver = 0;
	mod_flags = MSG_ReadByte();
	if (msg_badread) mod_flags = 0;
	mod_passwd = MSG_ReadLong();
	if (msg_badread) mod_passwd = 0;
	(void)mod_ver;
	(void)mod_flags;
	(void)mod_passwd;
#endif

#ifdef BAN_TEST
	// check for a ban
	// fixme: no ipv6
	// fixme: only a single address? someone seriously underestimates tor.
	if (clientaddr->qsa_family == AF_INET)
	{
		in_addr_t testAddr;
		testAddr = ((struct sockaddr_in *)clientaddr)->sin_addr.s_addr;
		if ((testAddr & banMask.s_addr) == banAddr.s_addr)
		{
			SZ_Clear (&net_message);
			// save space for the header, filled in later
			MSG_WriteLong (&net_message, 0);
			MSG_WriteByte (&net_message, CCREP_REJECT);
			MSG_WriteString (&net_message, "You have been banned.\n");
			*((int *)net_message.data) = BigLong (NETFLAG_CTL | (net_message.cursize & NETFLAG_LENGTH_MASK));
			dfunc.Write (acceptsock, net_message.data, net_message.cursize, clientaddr);
			SZ_Clear (&net_message);
			return;
		}
	}
#endif

	// see if this guy is already connected
	for (s = net_activeSockets; s; s = s->next)
	{
		if (s->driver != net_driverlevel)
			continue;
		if (s->disconnected)
			continue;
		ret = dfunc.AddrCompare (clientaddr, &s->addr);
		if (ret == 0)
		{
			int i;

			// is this a duplicate connection reqeust?
			if (ret == 0 && net_time - s->connecttime < 2.0)
			{
				// yes, so send a duplicate reply
				SZ_Clear (&net_message);
				// save space for the header, filled in later
				MSG_WriteLong (&net_message, 0);
				MSG_WriteByte (&net_message, CCREP_ACCEPT);
				dfunc.GetSocketAddr (s->socket, &newaddr);
				MSG_WriteLong (&net_message, dfunc.GetSocketPort (&newaddr));
				if (s->proquake_angle_hack)
				{
					MSG_WriteByte (&net_message, 1);  // proquake
					MSG_WriteByte (&net_message, 30); // ver 30 should be safe. 34 screws with our single-server-socket stuff.
					MSG_WriteByte (&net_message, 0);  // no flags
				}
				*((int *)net_message.data) = BigLong (NETFLAG_CTL | (net_message.cursize & NETFLAG_LENGTH_MASK));
				dfunc.Write (acceptsock, net_message.data, net_message.cursize, clientaddr);
				SZ_Clear (&net_message);
				return;
			}
			// it's somebody coming back in from a crash/disconnect
			// so close the old qsocket and let their retry get them back in
			//			NET_Close(s);
			//			return;

			// FIXME: ideally we would just switch the connection over and restart it with a serverinfo packet.
			// warning: there might be packets in-flight which might mess up unreliable sequences.
			// so we attempt to ignore the request, and let the user restart.
			// FIXME: if this is an issue, it should be possible to reuse the previous connection's outgoing unreliable sequence. reliables should be less of an
			// issue as stray ones will be ignored anyway.
			// FIXME: needs challenges, so that other clients can't determine ip's and spoof a reconnect.
			for (i = 0; i < svs.maxclients; i++)
			{
				if (svs.clients[i].netconnection == s)
				{
					NET_Close (s); // close early, to avoid svc_disconnects confusing things.
					host_client = &svs.clients[i];
					SV_DropClient (false);
					break;
				}
			}
			return;
		}
	}

	// find a free player slot
	for (plnum = 0; plnum < svs.maxclients; plnum++)
		if (!svs.clients[plnum].active)
			break;
	if (plnum < svs.maxclients)
		sock = NET_NewQSocket ();
	else
		sock = NULL; // can happen due to botclients.

	if (sock == NULL) // no room; try to let him know
	{
		SZ_Clear (&net_message);
		// save space for the header, filled in later
		MSG_WriteLong (&net_message, 0);
		MSG_WriteByte (&net_message, CCREP_REJECT);
		MSG_WriteString (&net_message, "Server is full.\n");
		*((int *)net_message.data) = BigLong (NETFLAG_CTL | (net_message.cursize & NETFLAG_LENGTH_MASK));
		dfunc.Write (acceptsock, net_message.data, net_message.cursize, clientaddr);
		SZ_Clear (&net_message);
		return;
	}

	sock->proquake_angle_hack = (mod == 1);

	// everything is allocated, just fill in the details
	sock->isvirtual = true;
	sock->socket = acceptsock;
	sock->landriver = net_landriverlevel;
	sock->addr = *clientaddr;
	strcpy (sock->trueaddress, dfunc.AddrToString (clientaddr, false));
	strcpy (sock->maskedaddress, dfunc.AddrToString (clientaddr, true));

	// send him back the info about the server connection he has been allocated
	SZ_Clear (&net_message);
	// save space for the header, filled in later
	MSG_WriteLong (&net_message, 0);
	MSG_WriteByte (&net_message, CCREP_ACCEPT);
	dfunc.GetSocketAddr (sock->socket, &newaddr);
	MSG_WriteLong (&net_message, dfunc.GetSocketPort (&newaddr));
	//	MSG_WriteString(&net_message, dfunc.AddrToString(&newaddr));
	if (sock->proquake_angle_hack)
	{
		MSG_WriteByte (&net_message, 1);  // proquake
		MSG_WriteByte (&net_message, 30); // ver 30 should be safe. 34 screws with our single-server-socket stuff.
		MSG_WriteByte (&net_message, 0);
	}
	*((int *)net_message.data) = BigLong (NETFLAG_CTL | (net_message.cursize & NETFLAG_LENGTH_MASK));
	dfunc.Write (acceptsock, net_message.data, net_message.cursize, clientaddr);
	SZ_Clear (&net_message);

	// spawn the client.
	// FIXME: come up with some challenge mechanism so that we don't go to the expense of spamming serverinfos+modellists+etc until we know that its an actual
	// connection attempt.
	svs.clients[plnum].netconnection = sock;
	SV_ConnectClient (plnum);
}

qsocket_t *Datagram_CheckNewConnections (void)
{
	// only needs to do master stuff now
	if (sv_public.value > 0)
	{
		if (Sys_DoubleTime () > heartbeat_time)
		{
			// darkplaces here refers to the master server protocol, rather than the game protocol
			//(specifies that the server responds to infoRequest packets from the master)
			char			 str[] = "\377\377\377\377heartbeat DarkPlaces\n";
			size_t			 k;
			struct qsockaddr addr;
			heartbeat_time = Sys_DoubleTime () + 300;

			for (k = 0; net_masters[k].string; k++)
			{
				if (!*net_masters[k].string)
					continue;
				for (net_landriverlevel = 0; net_landriverlevel < net_numlandrivers; net_landriverlevel++)
				{
					if (net_landrivers[net_landriverlevel].initialized && dfunc.listeningSock != INVALID_SOCKET)
					{
						if (dfunc.GetAddrFromName (net_masters[k].string, &addr) >= 0)
						{
							if (sv_reportheartbeats.value)
								Con_Printf ("Sending heartbeat to %s\n", net_masters[k].string);
							dfunc.Write (dfunc.listeningSock, (byte *)str, strlen (str), &addr);
						}
						else
						{
							if (sv_reportheartbeats.value)
								Con_Printf ("Unable to resolve %s\n", net_masters[k].string);
						}
					}
				}
			}
		}
	}

	return NULL;
}

static void _Datagram_SendServerQuery (struct qsockaddr *addr, qboolean master)
{
	SZ_Clear (&net_message);
	if (master) // assume false if you want only the protocol 15 servers.
	{
		MSG_WriteLong (&net_message, ~0);
		MSG_WriteString (&net_message, "getinfo");
	}
	else
	{
		// save space for the header, filled in later
		MSG_WriteLong (&net_message, 0);
		MSG_WriteByte (&net_message, CCREQ_SERVER_INFO);
		MSG_WriteString (&net_message, "QUAKE");
		MSG_WriteByte (&net_message, NET_PROTOCOL_VERSION);
		*((int *)net_message.data) = BigLong (NETFLAG_CTL | (net_message.cursize & NETFLAG_LENGTH_MASK));
	}
	dfunc.Write (dfunc.controlSock, net_message.data, net_message.cursize, addr);
	SZ_Clear (&net_message);
}
static struct
{
	int				 driver;
	qboolean		 requery;
	qboolean		 master;
	struct qsockaddr addr;
}		   *hostlist;
size_t		hostlist_count;
size_t		hostlist_max;
static void _Datagram_AddPossibleHost (struct qsockaddr *addr, qboolean master)
{
	size_t u;
	for (u = 0; u < hostlist_count; u++)
	{
		if (!memcmp (&hostlist[u].addr, addr, sizeof (struct qsockaddr)) && hostlist[u].driver == net_landriverlevel)
		{ // we already know about it. it must have come from some other master. don't respam.
			return;
		}
	}
	if (hostlist_count == hostlist_max)
	{
		hostlist_max = hostlist_count + 16;
		hostlist = Mem_Realloc (hostlist, sizeof (*hostlist) * hostlist_max);
	}
	hostlist[hostlist_count].addr = *addr;
	hostlist[hostlist_count].requery = true;
	hostlist[hostlist_count].master = master;
	hostlist[hostlist_count].driver = net_landriverlevel;
	hostlist_count++;
}

static void Info_ReadKey (const char *info, const char *key, char *out, size_t outsize)
{
	size_t keylen = strlen (key);
	while (*info)
	{
		if (*info++ != '\\')
			break; // error / end-of-string

		if (!strncmp (info, key, keylen) && info[keylen] == '\\')
		{
			char *o = out, *e = out + outsize - 1;

			// skip the key name
			info += keylen + 1;
			// this is the old value for the key. copy it to the result
			while (*info && *info != '\\' && o < e)
				*o++ = *info++;
			*o++ = 0;

			// success!
			return;
		}
		else
		{
			// skip the key
			while (*info && *info != '\\')
				info++;

			// validate that its a value now
			if (*info++ != '\\')
				break; // error
			// skip the value
			while (*info && *info != '\\')
				info++;
		}
	}
	*out = 0;
}

static qboolean _Datagram_SearchForHosts (qboolean xmit)
{
	int				 ret;
	size_t			 n;
	size_t			 i;
	struct qsockaddr readaddr;
	struct qsockaddr myaddr;
	int				 control;
	qboolean		 sentsomething = false;

	dfunc.GetSocketAddr (dfunc.controlSock, &myaddr);
	if (xmit)
	{
		for (i = 0; i < hostlist_count; i++)
			hostlist[i].requery = true;

		SZ_Clear (&net_message);
		// save space for the header, filled in later
		MSG_WriteLong (&net_message, 0);
		MSG_WriteByte (&net_message, CCREQ_SERVER_INFO);
		MSG_WriteString (&net_message, "QUAKE");
		MSG_WriteByte (&net_message, NET_PROTOCOL_VERSION);
		*((int *)net_message.data) = BigLong (NETFLAG_CTL | (net_message.cursize & NETFLAG_LENGTH_MASK));
		dfunc.Broadcast (dfunc.controlSock, net_message.data, net_message.cursize);
		SZ_Clear (&net_message);

		if (slist_scope == SLIST_INTERNET)
		{
			struct qsockaddr masteraddr;
			char			*str;
			size_t			 m;
			for (m = 0; net_masters[m].string; m++)
			{
				if (!*net_masters[m].string)
					continue;
				if (dfunc.GetAddrFromName (net_masters[m].string, &masteraddr) >= 0)
				{
					const char *prot = com_protocolname.string;
					while (*prot)
					{ // send a request for each protocol
						prot = COM_Parse (prot);
						if (!prot)
							break;
						if (*com_token)
						{
							if (masteraddr.qsa_family == AF_INET6)
								str = va ("%c%c%c%cgetserversExt %s %u empty full ipv6" /*\x0A\n"*/, 255, 255, 255, 255, com_token, NET_PROTOCOL_VERSION);
							else
								str = va ("%c%c%c%cgetservers %s %u empty full" /*\x0A\n"*/, 255, 255, 255, 255, com_token, NET_PROTOCOL_VERSION);
							dfunc.Write (dfunc.controlSock, (byte *)str, strlen (str), &masteraddr);
						}
					}
				}
			}
		}
		sentsomething = true;
	}

	while ((ret = dfunc.Read (dfunc.controlSock, net_message.data, net_message.maxsize, &readaddr)) > 0)
	{
		if (ret < (int)sizeof (int))
			continue;
		net_message.cursize = ret;

		// don't answer our own query
		// Note: this doesn't really work too well if we're multi-homed.
		// we should probably just refuse to respond to serverinfo requests while we're scanning (chances are our server is going to die anyway).
		if (dfunc.AddrCompare (&readaddr, &myaddr) >= 0)
			continue;

		// is the cache full?
		if (hostCacheCount == HOSTCACHESIZE)
			continue;

		MSG_BeginReading ();
		control = BigLong (*((int *)net_message.data));
		MSG_ReadLong ();
		if (control == -1)
		{
			if (msg_readcount + 19 <= net_message.cursize && !strncmp ((char *)net_message.data + msg_readcount, "getserversResponse", 18))
			{
				struct qsockaddr addr;
				int				 j;
				msg_readcount += 18;
				for (;;)
				{
					switch (MSG_ReadByte ())
					{
					case '\\':
						memset (&addr, 0, sizeof (addr));
						addr.qsa_family = AF_INET;
						for (j = 0; j < 4; j++)
							((byte *)&((struct sockaddr_in *)&addr)->sin_addr)[j] = MSG_ReadByte ();
						((byte *)&((struct sockaddr_in *)&addr)->sin_port)[0] = MSG_ReadByte ();
						((byte *)&((struct sockaddr_in *)&addr)->sin_port)[1] = MSG_ReadByte ();
						if (!((struct sockaddr_in *)&addr)->sin_port)
							msg_badread = true;
						break;
					case '/':
						memset (&addr, 0, sizeof (addr));
						addr.qsa_family = AF_INET6;
						for (j = 0; j < 16; j++)
							((byte *)&((struct sockaddr_in6 *)&addr)->sin6_addr)[j] = MSG_ReadByte ();
						((byte *)&((struct sockaddr_in6 *)&addr)->sin6_port)[0] = MSG_ReadByte ();
						((byte *)&((struct sockaddr_in6 *)&addr)->sin6_port)[1] = MSG_ReadByte ();
						if (!((struct sockaddr_in6 *)&addr)->sin6_port)
							msg_badread = true;
						break;
					default:
						memset (&addr, 0, sizeof (addr));
						msg_badread = true;
						break;
					}
					if (msg_badread)
						break;
					_Datagram_AddPossibleHost (&addr, true);
					sentsomething = true;
				}
			}
			else if (msg_readcount + 13 <= net_message.cursize && !strncmp ((char *)net_message.data + msg_readcount, "infoResponse\n", 13))
			{ // response from a dpp7 server (or possibly 15, no idea really)
				char		tmp[1024];
				const char *info = MSG_ReadString () + 13;

				// search the cache for this server
				for (n = 0; n < hostCacheCount; n++)
				{
					if (dfunc.AddrCompare (&readaddr, &hostcache[n].addr) == 0)
						break;
				}

				// is it already there?
				if (n < hostCacheCount)
				{
					if (*hostcache[n].cname)
						continue;
				}
				else
				{
					// add it
					hostCacheCount++;
				}
				Info_ReadKey (info, "hostname", hostcache[n].name, sizeof (hostcache[n].name));
				if (!*hostcache[n].name)
					q_strlcpy (hostcache[n].name, "UNNAMED", sizeof (hostcache[n].name));
				Info_ReadKey (info, "mapname", hostcache[n].map, sizeof (hostcache[n].map));
				Info_ReadKey (info, "modname", hostcache[n].gamedir, sizeof (hostcache[n].gamedir));

				Info_ReadKey (info, "clients", tmp, sizeof (tmp));
				hostcache[n].users = atoi (tmp);
				Info_ReadKey (info, "sv_maxclients", tmp, sizeof (tmp));
				hostcache[n].maxusers = atoi (tmp);
				Info_ReadKey (info, "protocol", tmp, sizeof (tmp));
				if (atoi (tmp) != NET_PROTOCOL_VERSION)
				{
					strcpy (hostcache[n].cname, hostcache[n].name);
					strcpy (hostcache[n].name, "*");
					strcat (hostcache[n].name, hostcache[n].cname);
				}
				memcpy (&hostcache[n].addr, &readaddr, sizeof (struct qsockaddr));
				hostcache[n].driver = net_driverlevel;
				hostcache[n].ldriver = net_landriverlevel;
				q_strlcpy (hostcache[n].cname, dfunc.AddrToString (&readaddr, false), sizeof (hostcache[n].cname));

				// check for a name conflict
				for (i = 0; i < hostCacheCount; i++)
				{
					if (i == n)
						continue;
					if (q_strcasecmp (hostcache[n].cname, hostcache[i].cname) == 0)
					{ // this is a dupe.
						hostCacheCount--;
						break;
					}
					if (q_strcasecmp (hostcache[n].name, hostcache[i].name) == 0)
					{
						i = strlen (hostcache[n].name);
						if (i < 15 && hostcache[n].name[i - 1] > '8')
						{
							hostcache[n].name[i] = '0';
							hostcache[n].name[i + 1] = 0;
						}
						else
							hostcache[n].name[i - 1]++;

						i = (size_t)-1;
					}
				}
			}
			continue;
		}
		if ((control & (~NETFLAG_LENGTH_MASK)) != (int)NETFLAG_CTL)
			continue;
		if ((control & NETFLAG_LENGTH_MASK) != ret)
			continue;

		if (MSG_ReadByte () != CCREP_SERVER_INFO)
			continue;

		MSG_ReadString ();
		// dfunc.GetAddrFromName(MSG_ReadString(), &peeraddr);
		/*if (dfunc.AddrCompare(&readaddr, &peeraddr) != 0)
		{
			char read[NET_NAMELEN];
			char peer[NET_NAMELEN];
			q_strlcpy(read, dfunc.AddrToString(&readaddr), sizeof(read));
			q_strlcpy(peer, dfunc.AddrToString(&peeraddr), sizeof(peer));
			Con_SafePrintf("Server at %s claimed to be at %s\n", read, peer);
		}*/

		// search the cache for this server
		for (n = 0; n < hostCacheCount; n++)
		{
			if (dfunc.AddrCompare (&readaddr, &hostcache[n].addr) == 0)
				break;
		}

		// is it already there?
		if (n < hostCacheCount)
		{
			if (*hostcache[n].cname)
				continue;
		}
		else
		{
			// add it
			hostCacheCount++;
		}
		q_strlcpy (hostcache[n].name, MSG_ReadString (), sizeof (hostcache[n].name));
		if (!*hostcache[n].name)
			q_strlcpy (hostcache[n].name, "UNNAMED", sizeof (hostcache[n].name));
		q_strlcpy (hostcache[n].map, MSG_ReadString (), sizeof (hostcache[n].map));
		hostcache[n].users = MSG_ReadByte ();
		hostcache[n].maxusers = MSG_ReadByte ();
		if (MSG_ReadByte () != NET_PROTOCOL_VERSION)
		{
			strcpy (hostcache[n].cname, hostcache[n].name);
			hostcache[n].cname[14] = 0;
			strcpy (hostcache[n].name, "*");
			strcat (hostcache[n].name, hostcache[n].cname);
		}
		memcpy (&hostcache[n].addr, &readaddr, sizeof (struct qsockaddr));
		hostcache[n].driver = net_driverlevel;
		hostcache[n].ldriver = net_landriverlevel;
		q_strlcpy (hostcache[n].cname, dfunc.AddrToString (&readaddr, false), sizeof (hostcache[n].cname));

		// check for a name conflict
		for (i = 0; i < hostCacheCount; i++)
		{
			if (i == n)
				continue;
			if (q_strcasecmp (hostcache[n].cname, hostcache[i].cname) == 0)
			{ // this is a dupe.
				hostCacheCount--;
				break;
			}
			if (q_strcasecmp (hostcache[n].name, hostcache[i].name) == 0)
			{
				i = strlen (hostcache[n].name);
				if (i < 15 && hostcache[n].name[i - 1] > '8')
				{
					hostcache[n].name[i] = '0';
					hostcache[n].name[i + 1] = 0;
				}
				else
					hostcache[n].name[i - 1]++;

				i = (size_t)-1;
			}
		}
	}

	if (!xmit)
	{
		n = 4; // should be time-based. meh.
		for (i = 0; i < hostlist_count; i++)
		{
			if (hostlist[i].requery && hostlist[i].driver == net_landriverlevel)
			{
				hostlist[i].requery = false;
				_Datagram_SendServerQuery (&hostlist[i].addr, hostlist[i].master);
				sentsomething = true;
				n--;
				if (!n)
					break;
			}
		}
	}
	return sentsomething;
}

qboolean Datagram_SearchForHosts (qboolean xmit)
{
	qboolean ret = false;
	for (net_landriverlevel = 0; net_landriverlevel < net_numlandrivers; net_landriverlevel++)
	{
		if (hostCacheCount == HOSTCACHESIZE)
			break;
		if (net_landrivers[net_landriverlevel].initialized)
			ret |= _Datagram_SearchForHosts (xmit);
	}
	return ret;
}

static qsocket_t *_Datagram_Connect (struct qsockaddr *serveraddr)
{
	struct qsockaddr readaddr;
	qsocket_t		*sock;
	sys_socket_t	 newsock;
	int				 ret;
	int				 reps;
	double			 start_time;
	int				 control;
	const char		*reason;

	newsock = dfunc.Open_Socket (0);
	if (newsock == INVALID_SOCKET)
		return NULL;

	sock = NET_NewQSocket ();
	if (sock == NULL)
		goto ErrorReturn2;
	sock->socket = newsock;
	sock->landriver = net_landriverlevel;

	// connect to the host
	if (dfunc.Connect (newsock, serveraddr) == -1)
		goto ErrorReturn;

	sock->proquake_angle_hack = true;

	// send the connection request
	Con_SafePrintf ("trying...\n");
	SCR_UpdateScreen (false);
	start_time = net_time;

	for (reps = 0; reps < 3; reps++)
	{
		SZ_Clear (&net_message);
		// save space for the header, filled in later
		MSG_WriteLong (&net_message, 0);
		MSG_WriteByte (&net_message, CCREQ_CONNECT);
		MSG_WriteString (&net_message, "QUAKE");
		MSG_WriteByte (&net_message, NET_PROTOCOL_VERSION);
		if (sock->proquake_angle_hack)
		{ /*Spike -- proquake compat. if both engines claim to be using mod==1 then 16bit client->server angles can be used. server->client angles remain
			 16bit*/
			Con_DWarning ("Attempting to use ProQuake angle hack\n");
			MSG_WriteByte (&net_message, 1);  /*'mod', 1=proquake*/
			MSG_WriteByte (&net_message, 34); /*'mod' version*/
			MSG_WriteByte (&net_message, 0);  /*flags*/
			MSG_WriteLong (&net_message, 0);  // strtoul(password.string, NULL, 0)); /*password*/
		}
		*((int *)net_message.data) = BigLong (NETFLAG_CTL | (net_message.cursize & NETFLAG_LENGTH_MASK));
		dfunc.Write (newsock, net_message.data, net_message.cursize, serveraddr);
		SZ_Clear (&net_message);

// for dp compat. DP sends these in addition to the above packet.
// if the (DP) server is running using vanilla protocols, it replies to the above, otherwise to the following, requiring both to be sent.
//(challenges hinder a DOS issue known as smurfing, in that the client must prove that it owns the IP that it might be spoofing before any serious resources are
// used)
#define DPGETCHALLENGE "\xff\xff\xff\xffgetchallenge\n"
		dfunc.Write (newsock, (byte *)DPGETCHALLENGE, strlen (DPGETCHALLENGE), serveraddr);

		do
		{
			ret = dfunc.Read (newsock, net_message.data, net_message.maxsize, &readaddr);
			// if we got something, validate it
			if (ret > 0)
			{
				// is it from the right place?
				if (dfunc.AddrCompare (&readaddr, serveraddr) != 0)
				{
					Con_SafePrintf ("wrong reply address\n");
					Con_SafePrintf ("Expected: %s | %s\n", dfunc.AddrToString (serveraddr, false), StrAddr (serveraddr));
					Con_SafePrintf ("Received: %s | %s\n", dfunc.AddrToString (&readaddr, false), StrAddr (&readaddr));
					SCR_UpdateScreen (false);
					ret = 0;
					continue;
				}

				if (ret < (int)sizeof (int))
				{
					ret = 0;
					continue;
				}

				net_message.cursize = ret;
				MSG_BeginReading ();

				control = BigLong (*((int *)net_message.data));
				MSG_ReadLong ();
				if (control == -1)
				{
					const char *s = MSG_ReadString ();
					if (!strncmp (s, "challenge ", 10))
					{ // either a q2 or dp server...
						char buf[1024];
						q_snprintf (
							buf, sizeof (buf), "%c%c%c%cconnect\\protocol\\darkplaces 3\\protocols\\RMQ FITZ DP7 NEHAHRABJP3 QUAKE\\challenge\\%s", 255, 255,
							255, 255, s + 10);
						dfunc.Write (newsock, (byte *)buf, strlen (buf), serveraddr);
					}
					else if (!strcmp (s, "accept"))
					{
						memcpy (&sock->addr, serveraddr, sizeof (struct qsockaddr));
						sock->proquake_angle_hack = false;
						goto dpserveraccepted;
					}
					/*else if (!strcmp(s, "reject"))
					{
						reason = MSG_ReadString();
						Con_Printf("%s\n", reason);
						q_strlcpy(m_return_reason, reason, sizeof(m_return_reason));
						goto ErrorReturn;
					}*/

					ret = 0;
					continue;
				}
				if ((control & (~NETFLAG_LENGTH_MASK)) != (int)NETFLAG_CTL)
				{
					ret = 0;
					continue;
				}
				if ((control & NETFLAG_LENGTH_MASK) != ret)
				{
					ret = 0;
					continue;
				}
			}
		} while (ret == 0 && (SetNetTime () - start_time) < 2.5);

		if (ret)
			break;

		Con_SafePrintf ("still trying...\n");
		SCR_UpdateScreen (false);
		start_time = SetNetTime ();
	}

	if (ret == 0)
	{
		reason = "No Response";
		Con_Printf ("%s\n", reason);
		strcpy (m_return_reason, reason);
		goto ErrorReturn;
	}

	if (ret == -1)
	{
		reason = "Network Error";
		Con_Printf ("%s\n", reason);
		strcpy (m_return_reason, reason);
		goto ErrorReturn;
	}

	ret = MSG_ReadByte ();
	if (ret == CCREP_REJECT)
	{
		reason = MSG_ReadString ();
		Con_Printf ("%s\n", reason);
		q_strlcpy (m_return_reason, reason, sizeof (m_return_reason));
		goto ErrorReturn;
	}

	if (ret == CCREP_ACCEPT)
	{
		int port;
		memcpy (&sock->addr, serveraddr, sizeof (struct qsockaddr));
		port = MSG_ReadLong ();
		if (port) // spike --- don't change the remote port if the server doesn't want us to. this allows servers to use port forwarding with less issues,
				  // assuming the server uses the same port for all clients.
			dfunc.SetSocketPort (&sock->addr, port);
	}
	else
	{
		reason = "Bad Response";
		Con_Printf ("%s\n", reason);
		strcpy (m_return_reason, reason);
		goto ErrorReturn;
	}

	if (sock->proquake_angle_hack)
	{
		byte mod = (msg_readcount < net_message.cursize) ? MSG_ReadByte () : 0;
		byte ver = (msg_readcount < net_message.cursize) ? MSG_ReadByte () : 0;
		byte flags = (msg_readcount < net_message.cursize) ? MSG_ReadByte () : 0;
		(void)ver;

		if (mod == 1 /*MOD_PROQUAKE*/)
		{
			if (flags & 1 /*CHEATFREE*/)
			{
				reason = "Server is incompatible";
				Con_Printf ("%s\n", reason);
				strcpy (m_return_reason, reason);
				goto ErrorReturn;
			}
			sock->proquake_angle_hack = true;
		}
		else
			sock->proquake_angle_hack = false;
	}

dpserveraccepted:

	dfunc.GetNameFromAddr (serveraddr, sock->trueaddress);
	dfunc.GetNameFromAddr (serveraddr, sock->maskedaddress);

	Con_Printf ("Connection accepted\n");
	sock->lastMessageTime = SetNetTime ();

	// switch the connection to the specified address
	if (dfunc.Connect (newsock, &sock->addr) == -1)
	{
		reason = "Connect to Game failed";
		Con_Printf ("%s\n", reason);
		strcpy (m_return_reason, reason);
		goto ErrorReturn;
	}

	/*Spike's rant about NATs:
	We sent a packet to the server's control port.
	The server replied from that control port. all is well so far.
	The server is now about(or already did, yay resends) to send us a packet from its data port to our address.
	The nat will (correctly) see a packet from a different remote address:port.
	The local nat has two options here. 1) assume that the wrong port is fine. 2) drop it. Dropping it is far more likely.
	The NQ code will not send any unreliables until we have received the serverinfo. There are no reliables that need to be sent either.
	Normally we won't send ANYTHING until we get that packet.
	Which will never happen because the NAT will never let it through.
	So, if we want to get away without fixing everyone else's server (which is also quite messy),
		the easy way around this dilema is to just send some (small) useless packet to what we believe to be the server's data port.
	A single unreliable clc_nop should do it. There's unlikely to be much packetloss on our local lan (so long as our host buffers outgoing packets on a
	per-socket basis or something), so we don't normally need to resend. We don't really care if the server can even read it properly, but its best to avoid
	warning prints. With that small outgoing packet, our local nat will think we initiated the request. HOPEFULLY it'll reuse the same public port+address. Most
	home routers will, but not all, most hole-punching techniques depend upon such behaviour. Note that proquake 3.4+ will actually wait for a packet from the
	new client, which solves that (but makes the nop mandatory, so needs to be reliable).

	the nop is actually sent inside CL_EstablishConnection where it has cleaner access to the client's pending reliable message.

	Note that we do fix our own server. This means that we can easily run on a home nat. the heartbeats to the master will open up a public port with most
	routers. And if that doesn't work, then its easy enough to port-forward a single known port instead of having to DMZ the entire network. I don't really
	expect that many people will use this, but it'll be nice for the occasional coop game. (note that this makes the nop redundant, but that's a different can
	of worms)
	*/

	m_return_onerror = false;
	return sock;

ErrorReturn:
	NET_FreeQSocket (sock);
ErrorReturn2:
	dfunc.Close_Socket (newsock);
	if (m_return_onerror)
	{
		key_dest = key_menu;
		m_state = m_return_state;
		m_return_onerror = false;
	}
	return NULL;
}

qsocket_t *Datagram_Connect (const char *host)
{
	qsocket_t		*ret = NULL;
	qboolean		 resolved = false;
	struct qsockaddr addr;

	host = Strip_Port (host);
	for (net_landriverlevel = 0; net_landriverlevel < net_numlandrivers; net_landriverlevel++)
	{
		if (net_landrivers[net_landriverlevel].initialized)
		{
			// see if we can resolve the host name
			// Spike -- moved name resolution to here to avoid extraneous 'could not resolves' when using other address families
			if (dfunc.GetAddrFromName (host, &addr) != -1)
			{
				resolved = true;
				if ((ret = _Datagram_Connect (&addr)) != NULL)
					break;
			}
		}
	}
	if (!resolved)
		Con_SafePrintf ("Could not resolve %s\n", host);
	return ret;
}

/*
Spike: added this to list more than one ipv4 address (many people are still multi-homed)
*/
int Datagram_QueryAddresses (qhostaddr_t *addresses, int maxaddresses)
{
	int result = 0;
	for (net_landriverlevel = 0; net_landriverlevel < net_numlandrivers; net_landriverlevel++)
	{
		if (!net_landrivers[net_landriverlevel].initialized)
			continue;
		if (result == maxaddresses)
			break;
		if (net_landrivers[net_landriverlevel].QueryAddresses)
			result += net_landrivers[net_landriverlevel].QueryAddresses (addresses + result, maxaddresses - result);
	}
	return result;
}
