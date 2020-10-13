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

// net_wipx.c

#include "q_stdinc.h"
#include "arch_def.h"
#include "net_sys.h"
#include <wsipx.h>
#include "quakedef.h"
#include "net_defs.h"
#include "net_wipx.h"

extern cvar_t hostname;

static sys_socket_t net_acceptsocket = INVALID_SOCKET;	// socket for fielding new connections
static sys_socket_t net_controlsocket;
static struct sockaddr_ipx broadcastaddr;

/* externs from net_wins.c: */
extern qboolean winsock_initialized;
extern WSADATA		winsockdata;
extern const char *__WSAE_StrError (int);

#define IPXSOCKETS 18
static sys_socket_t ipxsocket[IPXSOCKETS];
static int sequence[IPXSOCKETS];

//=============================================================================

sys_socket_t WIPX_Init (void)
{
	int	i, err;
	char	*colon;
	char	buff[MAXHOSTNAMELEN];
	struct qsockaddr	addr;

	if (COM_CheckParm ("-noipx"))
		return INVALID_SOCKET;

	if (winsock_initialized == 0)
	{
		err = WSAStartup(MAKEWORD(1,1), &winsockdata);
		if (err != 0)
		{
			Con_SafePrintf("Winsock initialization failed (%s)\n",
					socketerror(err));
			return INVALID_SOCKET;
		}
	}
	winsock_initialized++;

	for (i = 0; i < IPXSOCKETS; i++)
		ipxsocket[i] = 0;

	// determine my name & address
	if (gethostname(buff, MAXHOSTNAMELEN) != 0)
	{
		err = SOCKETERRNO;
		Con_SafePrintf("WIPX_Init: gethostname failed (%s)\n",
							socketerror(err));
	}
	else
	{
		buff[MAXHOSTNAMELEN - 1] = 0;
	}

	if ((net_controlsocket = WIPX_OpenSocket(0)) == INVALID_SOCKET)
	{
		Con_SafePrintf("WIPX_Init: Unable to open control socket, IPX disabled\n");
		if (--winsock_initialized == 0)
			WSACleanup ();
		return INVALID_SOCKET;
	}

	broadcastaddr.sa_family = AF_IPX;
	memset(broadcastaddr.sa_netnum, 0, 4);
	memset(broadcastaddr.sa_nodenum, 0xff, 6);
	broadcastaddr.sa_socket = htons((unsigned short)net_hostport);

	WIPX_GetSocketAddr (net_controlsocket, &addr);
	Q_strcpy(my_ipx_address, WIPX_AddrToString (&addr));
	colon = Q_strrchr (my_ipx_address, ':');
	if (colon)
		*colon = 0;

	Con_SafePrintf("IPX Initialized\n");
	ipxAvailable = true;

	return net_controlsocket;
}

//=============================================================================

void WIPX_Shutdown (void)
{
	WIPX_Listen (false);
	WIPX_CloseSocket (net_controlsocket);
	if (--winsock_initialized == 0)
		WSACleanup ();
}

//=============================================================================

void WIPX_Listen (qboolean state)
{
	// enable listening
	if (state)
	{
		if (net_acceptsocket != INVALID_SOCKET)
			return;
		if ((net_acceptsocket = WIPX_OpenSocket (net_hostport)) == INVALID_SOCKET)
			Sys_Error ("WIPX_Listen: Unable to open accept socket");
		return;
	}

	// disable listening
	if (net_acceptsocket == INVALID_SOCKET)
		return;
	WIPX_CloseSocket (net_acceptsocket);
	net_acceptsocket = INVALID_SOCKET;
}

//=============================================================================

sys_socket_t WIPX_OpenSocket (int port)
{
	int	err;
	sys_socket_t	handle, newsocket;
	struct sockaddr_ipx address;
	u_long _true = 1;

	for (handle = 0; handle < IPXSOCKETS; handle++)
	{
		if (ipxsocket[handle] == 0)
			break;
	}
	if (handle == IPXSOCKETS)
	{
		Con_SafePrintf("WIPX_OpenSocket: Out of free IPX handles.\n");
		return INVALID_SOCKET;
	}

	if ((newsocket = socket (AF_IPX, SOCK_DGRAM, NSPROTO_IPX)) == INVALID_SOCKET)
	{
		err = SOCKETERRNO;
		Con_SafePrintf("WIPX_OpenSocket: %s\n", socketerror(err));
		return INVALID_SOCKET;
	}

	if (ioctlsocket (newsocket, FIONBIO, &_true) == SOCKET_ERROR)
		goto ErrorReturn;

	if (setsockopt(newsocket, SOL_SOCKET, SO_BROADCAST, (char *)&_true, sizeof(_true))
									 == SOCKET_ERROR)
		goto ErrorReturn;

	address.sa_family = AF_IPX;
	memset(address.sa_netnum, 0, 4);
	memset(address.sa_nodenum, 0, 6);;
	address.sa_socket = htons((unsigned short)port);
	if (bind (newsocket, (struct sockaddr *)&address, sizeof(address)) == 0)
	{
		ipxsocket[handle] = newsocket;
		sequence[handle] = 0;
		return handle;
	}

	if (ipxAvailable)
	{
		err = SOCKETERRNO;
		Sys_Error ("IPX bind failed (%s)", socketerror(err));
		return INVALID_SOCKET;	/* not reached */
	}
	/* else: we are still in init phase, no need to error */

ErrorReturn:
	err = SOCKETERRNO;
	Con_SafePrintf("WIPX_OpenSocket: %s\n", socketerror(err));
	closesocket (newsocket);
	return INVALID_SOCKET;
}

//=============================================================================

int WIPX_CloseSocket (sys_socket_t handle)
{
	sys_socket_t socketid = ipxsocket[handle];
	int ret;

	ret =  closesocket (socketid);
	ipxsocket[handle] = 0;
	return ret;
}

//=============================================================================

int WIPX_Connect (sys_socket_t handle, struct qsockaddr *addr)
{
	return 0;
}

//=============================================================================

sys_socket_t WIPX_CheckNewConnections (void)
{
	u_long	available;

	if (net_acceptsocket == INVALID_SOCKET)
		return INVALID_SOCKET;

	if (ioctlsocket (ipxsocket[net_acceptsocket], FIONREAD, &available) == SOCKET_ERROR)
	{
		int err = SOCKETERRNO;
		Sys_Error ("WIPX: ioctlsocket (FIONREAD) failed (%s)", socketerror(err));
	}
	if (available)
		return net_acceptsocket;
	return INVALID_SOCKET;
}

//=============================================================================

static byte netpacketBuffer[NET_DATAGRAMSIZE + 4];

int WIPX_Read (sys_socket_t handle, byte *buf, int len, struct qsockaddr *addr)
{
	socklen_t addrlen = sizeof(struct qsockaddr);
	sys_socket_t socketid = ipxsocket[handle];
	int ret;

	ret = recvfrom (socketid, (char *)netpacketBuffer, len+4, 0, (struct sockaddr *)addr, &addrlen);
	if (ret == SOCKET_ERROR)
	{
		int err = SOCKETERRNO;
		if (err == NET_EWOULDBLOCK || err == NET_ECONNREFUSED)
			return 0;
		Con_SafePrintf ("WIPX_Read, recvfrom: %s\n", socketerror(err));
	}

	if (ret < 4)
		return 0;

	// remove sequence number, it's only needed for DOS IPX
	ret -= 4;
	memcpy(buf, netpacketBuffer+4, ret);

	return ret;
}

//=============================================================================

int WIPX_Broadcast (sys_socket_t handle, byte *buf, int len)
{
	return WIPX_Write (handle, buf, len, (struct qsockaddr *)&broadcastaddr);
}

//=============================================================================

int WIPX_Write (sys_socket_t handle, byte *buf, int len, struct qsockaddr *addr)
{
	sys_socket_t socketid = ipxsocket[handle];
	int ret;

	// build packet with sequence number
	memcpy(&netpacketBuffer[0], &sequence[handle], 4);
	sequence[handle]++;
	memcpy(&netpacketBuffer[4], buf, len);
	len += 4;

	ret = sendto (socketid, (char *)netpacketBuffer, len, 0, (struct sockaddr *)addr, sizeof(struct qsockaddr));
	if (ret == SOCKET_ERROR)
	{
		int err = SOCKETERRNO;
		if (err == NET_EWOULDBLOCK)
			return 0;
		Con_SafePrintf ("WIPX_Write, sendto: %s\n", socketerror(err));
	}

	return ret;
}

//=============================================================================

const char *WIPX_AddrToString (struct qsockaddr *addr)
{
	static char buf[28];

	sprintf(buf, "%02x%02x%02x%02x:%02x%02x%02x%02x%02x%02x:%u",
		((struct sockaddr_ipx *)addr)->sa_netnum[0] & 0xff,
		((struct sockaddr_ipx *)addr)->sa_netnum[1] & 0xff,
		((struct sockaddr_ipx *)addr)->sa_netnum[2] & 0xff,
		((struct sockaddr_ipx *)addr)->sa_netnum[3] & 0xff,
		((struct sockaddr_ipx *)addr)->sa_nodenum[0] & 0xff,
		((struct sockaddr_ipx *)addr)->sa_nodenum[1] & 0xff,
		((struct sockaddr_ipx *)addr)->sa_nodenum[2] & 0xff,
		((struct sockaddr_ipx *)addr)->sa_nodenum[3] & 0xff,
		((struct sockaddr_ipx *)addr)->sa_nodenum[4] & 0xff,
		((struct sockaddr_ipx *)addr)->sa_nodenum[5] & 0xff,
		ntohs(((struct sockaddr_ipx *)addr)->sa_socket)
		);
	return buf;
}

//=============================================================================

int WIPX_StringToAddr (const char *string, struct qsockaddr *addr)
{
	int	val;
	char	buf[3];

	buf[2] = 0;
	Q_memset(addr, 0, sizeof(struct qsockaddr));
	addr->qsa_family = AF_IPX;

#define DO(src,dest) do {				\
	buf[0] = string[src];				\
	buf[1] = string[src + 1];			\
	if (sscanf (buf, "%x", &val) != 1)		\
		return -1;				\
	((struct sockaddr_ipx *)addr)->dest = val;	\
      } while (0)

	DO(0, sa_netnum[0]);
	DO(2, sa_netnum[1]);
	DO(4, sa_netnum[2]);
	DO(6, sa_netnum[3]);
	DO(9, sa_nodenum[0]);
	DO(11, sa_nodenum[1]);
	DO(13, sa_nodenum[2]);
	DO(15, sa_nodenum[3]);
	DO(17, sa_nodenum[4]);
	DO(19, sa_nodenum[5]);
#undef DO

	sscanf (&string[22], "%u", &val);
	((struct sockaddr_ipx *)addr)->sa_socket = htons((unsigned short)val);

	return 0;
}

//=============================================================================

int WIPX_GetSocketAddr (sys_socket_t handle, struct qsockaddr *addr)
{
	sys_socket_t socketid = ipxsocket[handle];
	socklen_t addrlen = sizeof(struct qsockaddr);

	Q_memset(addr, 0, sizeof(struct qsockaddr));
	if (getsockname(socketid, (struct sockaddr *)addr, &addrlen) != 0)
	{
		int err = SOCKETERRNO;
		/* FIXME: what action should be taken?... */
		Con_SafePrintf ("WIPX, getsockname: %s\n", socketerror(err));
	}

	return 0;
}

//=============================================================================

int WIPX_GetNameFromAddr (struct qsockaddr *addr, char *name)
{
	Q_strcpy(name, WIPX_AddrToString(addr));
	return 0;
}

//=============================================================================

int WIPX_GetAddrFromName (const char *name, struct qsockaddr *addr)
{
	int		n;
	char	buf[32];

	n = Q_strlen(name);

	if (n == 12)
	{
		sprintf(buf, "00000000:%s:%u", name, net_hostport);
		return WIPX_StringToAddr (buf, addr);
	}
	if (n == 21)
	{
		sprintf(buf, "%s:%u", name, net_hostport);
		return WIPX_StringToAddr (buf, addr);
	}
	if (n > 21 && n <= 27)
		return WIPX_StringToAddr (name, addr);

	return -1;
}

//=============================================================================

int WIPX_AddrCompare (struct qsockaddr *addr1, struct qsockaddr *addr2)
{
	if (addr1->qsa_family != addr2->qsa_family)
		return -1;

	if (*((struct sockaddr_ipx *)addr1)->sa_netnum && *((struct sockaddr_ipx *)addr2)->sa_netnum)
	{
		if (memcmp(((struct sockaddr_ipx *)addr1)->sa_netnum, ((struct sockaddr_ipx *)addr2)->sa_netnum, 4) != 0)
			return -1;
	}

	if (memcmp(((struct sockaddr_ipx *)addr1)->sa_nodenum, ((struct sockaddr_ipx *)addr2)->sa_nodenum, 6) != 0)
		return -1;

	if (((struct sockaddr_ipx *)addr1)->sa_socket != ((struct sockaddr_ipx *)addr2)->sa_socket)
			return 1;

	return 0;
}

//=============================================================================

int WIPX_GetSocketPort (struct qsockaddr *addr)
{
	return ntohs(((struct sockaddr_ipx *)addr)->sa_socket);
}


int WIPX_SetSocketPort (struct qsockaddr *addr, int port)
{
	((struct sockaddr_ipx *)addr)->sa_socket = htons((unsigned short)port);
	return 0;
}

//=============================================================================

