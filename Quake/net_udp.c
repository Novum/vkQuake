/*
Copyright (C) 1996-2001 Id Software, Inc.
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

#include "q_stdinc.h"
#include "arch_def.h"
#include "net_sys.h"
#include "quakedef.h"
#include "net_defs.h"

static sys_socket_t net_acceptsocket = INVALID_SOCKET;	// socket for fielding new connections
static sys_socket_t net_controlsocket;
static sys_socket_t net_broadcastsocket = 0;
static struct sockaddr_in broadcastaddr;

static in_addr_t	myAddr;

#include "net_udp.h"

//=============================================================================

sys_socket_t UDP_Init (void)
{
	int	err;
	char	*tst;
	char	buff[MAXHOSTNAMELEN];
	struct hostent		*local;
	struct qsockaddr	addr;

	if (COM_CheckParm ("-noudp"))
		return INVALID_SOCKET;

	// determine my name & address
	myAddr = htonl(INADDR_LOOPBACK);
	if (gethostname(buff, MAXHOSTNAMELEN) != 0)
	{
		err = SOCKETERRNO;
		Con_SafePrintf("UDP_Init: gethostname failed (%s)\n",
							socketerror(err));
	}
	else
	{
		buff[MAXHOSTNAMELEN - 1] = 0;
#ifdef PLATFORM_OSX
		// ericw -- if our hostname ends in ".local" (a macOS thing),
		// don't bother calling gethostbyname(), because it blocks for a few seconds
		// and then fails (on my system anyway.)
		tst = strstr(buff, ".local");
		if (tst && tst[6] == '\0')
		{
			Con_SafePrintf("UDP_Init: skipping gethostbyname for %s\n", buff);
		}
		else
#endif
		if (!(local = gethostbyname(buff)))
		{
			Con_SafePrintf("UDP_Init: gethostbyname failed (%s)\n",
							hstrerror(h_errno));
		}
		else if (local->h_addrtype != AF_INET)
		{
			Con_SafePrintf("UDP_Init: address from gethostbyname not IPv4\n");
		}
		else
		{
			myAddr = *(in_addr_t *)local->h_addr_list[0];
		}
	}

	if ((net_controlsocket = UDP_OpenSocket(0)) == INVALID_SOCKET)
	{
		Con_SafePrintf("UDP_Init: Unable to open control socket, UDP disabled\n");
		return INVALID_SOCKET;
	}

	broadcastaddr.sin_family = AF_INET;
	broadcastaddr.sin_addr.s_addr = INADDR_BROADCAST;
	broadcastaddr.sin_port = htons((unsigned short)net_hostport);

	UDP_GetSocketAddr (net_controlsocket, &addr);
	strcpy(my_tcpip_address, UDP_AddrToString (&addr));
	tst = strrchr(my_tcpip_address, ':');
	if (tst) *tst = 0;

	Con_SafePrintf("UDP Initialized\n");
	tcpipAvailable = true;

	return net_controlsocket;
}

//=============================================================================

void UDP_Shutdown (void)
{
	UDP_Listen (false);
	UDP_CloseSocket (net_controlsocket);
}

//=============================================================================

void UDP_Listen (qboolean state)
{
	// enable listening
	if (state)
	{
		if (net_acceptsocket != INVALID_SOCKET)
			return;
		if ((net_acceptsocket = UDP_OpenSocket (net_hostport)) == INVALID_SOCKET)
			Sys_Error ("UDP_Listen: Unable to open accept socket");
		return;
	}

	// disable listening
	if (net_acceptsocket == INVALID_SOCKET)
		return;
	UDP_CloseSocket (net_acceptsocket);
	net_acceptsocket = INVALID_SOCKET;
}

//=============================================================================

sys_socket_t UDP_OpenSocket (int port)
{
	sys_socket_t newsocket;
	struct sockaddr_in address;
	int _true = 1;
	int err;

	if ((newsocket = socket (PF_INET, SOCK_DGRAM, IPPROTO_UDP)) == INVALID_SOCKET)
	{
		err = SOCKETERRNO;
		Con_SafePrintf("UDP_OpenSocket: %s\n", socketerror(err));
		return INVALID_SOCKET;
	}

	if (ioctlsocket (newsocket, FIONBIO, &_true) == SOCKET_ERROR)
		goto ErrorReturn;

	memset(&address, 0, sizeof(struct sockaddr_in));
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = INADDR_ANY;
	address.sin_port = htons((unsigned short)port);
	if (bind (newsocket, (struct sockaddr *)&address, sizeof(address)) == 0)
		return newsocket;

ErrorReturn:
	err = SOCKETERRNO;
	Con_SafePrintf("UDP_OpenSocket: %s\n", socketerror(err));
	UDP_CloseSocket (newsocket);
	return INVALID_SOCKET;
}

//=============================================================================

int UDP_CloseSocket (sys_socket_t socketid)
{
	if (socketid == net_broadcastsocket)
		net_broadcastsocket = 0;
	return closesocket (socketid);
}

//=============================================================================

/*
============
PartialIPAddress

this lets you type only as much of the net address as required, using
the local network components to fill in the rest
============
*/
static int PartialIPAddress (const char *in, struct qsockaddr *hostaddr)
{
	char	buff[256];
	char	*b;
	int	addr, mask, num, port, run;

	buff[0] = '.';
	b = buff;
	strcpy(buff+1, in);
	if (buff[1] == '.')
		b++;

	addr = 0;
	mask = -1;
	while (*b == '.')
	{
		b++;
		num = 0;
		run = 0;
		while (!( *b < '0' || *b > '9'))
		{
			num = num*10 + *b++ - '0';
			if (++run > 3)
				return -1;
		}
		if ((*b < '0' || *b > '9') && *b != '.' && *b != ':' && *b != 0)
			return -1;
		if (num < 0 || num > 255)
			return -1;
		mask <<= 8;
		addr = (addr<<8) + num;
	}

	if (*b++ == ':')
		port = atoi(b);
	else
		port = net_hostport;

	hostaddr->qsa_family = AF_INET;
	((struct sockaddr_in *)hostaddr)->sin_port = htons((unsigned short)port);
	((struct sockaddr_in *)hostaddr)->sin_addr.s_addr =
					(myAddr & htonl(mask)) | htonl(addr);

	return 0;
}

//=============================================================================

int UDP_Connect (sys_socket_t socketid, struct qsockaddr *addr)
{
	return 0;
}

//=============================================================================

sys_socket_t UDP_CheckNewConnections (void)
{
	int		available;
	struct sockaddr_in	from;
	socklen_t	fromlen;
	char		buff[1];

	if (net_acceptsocket == INVALID_SOCKET)
		return INVALID_SOCKET;

	if (ioctl (net_acceptsocket, FIONREAD, &available) == -1)
	{
		int err = SOCKETERRNO;
		Sys_Error ("UDP: ioctlsocket (FIONREAD) failed (%s)", socketerror(err));
	}
	if (available)
		return net_acceptsocket;
	// quietly absorb empty packets
	recvfrom (net_acceptsocket, buff, 0, 0, (struct sockaddr *) &from, &fromlen);
	return INVALID_SOCKET;
}

//=============================================================================

int UDP_Read (sys_socket_t socketid, byte *buf, int len, struct qsockaddr *addr)
{
	socklen_t addrlen = sizeof(struct qsockaddr);
	int ret;

	ret = recvfrom (socketid, buf, len, 0, (struct sockaddr *)addr, &addrlen);
	if (ret == SOCKET_ERROR)
	{
		int err = SOCKETERRNO;
		if (err == NET_EWOULDBLOCK || err == NET_ECONNREFUSED)
			return 0;
		Con_SafePrintf ("UDP_Read, recvfrom: %s\n", socketerror(err));
	}
	return ret;
}

//=============================================================================

static int UDP_MakeSocketBroadcastCapable (sys_socket_t socketid)
{
	int	i = 1;

	// make this socket broadcast capable
	if (setsockopt(socketid, SOL_SOCKET, SO_BROADCAST, (char *)&i, sizeof(i))
								 == SOCKET_ERROR)
	{
		int err = SOCKETERRNO;
		Con_SafePrintf ("UDP, setsockopt: %s\n", socketerror(err));
		return -1;
	}
	net_broadcastsocket = socketid;

	return 0;
}

//=============================================================================

int UDP_Broadcast (sys_socket_t socketid, byte *buf, int len)
{
	int	ret;

	if (socketid != net_broadcastsocket)
	{
		if (net_broadcastsocket != 0)
			Sys_Error("Attempted to use multiple broadcasts sockets");
		ret = UDP_MakeSocketBroadcastCapable (socketid);
		if (ret == -1)
		{
			Con_Printf("Unable to make socket broadcast capable\n");
			return ret;
		}
	}

	return UDP_Write (socketid, buf, len, (struct qsockaddr *)&broadcastaddr);
}

//=============================================================================

int UDP_Write (sys_socket_t socketid, byte *buf, int len, struct qsockaddr *addr)
{
	int	ret;

	ret = sendto (socketid, buf, len, 0, (struct sockaddr *)addr,
							sizeof(struct qsockaddr));
	if (ret == SOCKET_ERROR)
	{
		int err = SOCKETERRNO;
		if (err == NET_EWOULDBLOCK)
			return 0;
		Con_SafePrintf ("UDP_Write, sendto: %s\n", socketerror(err));
	}
	return ret;
}

//=============================================================================

const char *UDP_AddrToString (struct qsockaddr *addr)
{
	static char buffer[22];
	int		haddr;

	haddr = ntohl(((struct sockaddr_in *)addr)->sin_addr.s_addr);
	q_snprintf (buffer, sizeof(buffer), "%d.%d.%d.%d:%d", (haddr >> 24) & 0xff,
			   (haddr >> 16) & 0xff, (haddr >> 8) & 0xff, haddr & 0xff,
			    ntohs(((struct sockaddr_in *)addr)->sin_port));
	return buffer;
}

//=============================================================================

int UDP_StringToAddr (const char *string, struct qsockaddr *addr)
{
	int	ha1, ha2, ha3, ha4, hp, ipaddr;

	sscanf(string, "%d.%d.%d.%d:%d", &ha1, &ha2, &ha3, &ha4, &hp);
	ipaddr = (ha1 << 24) | (ha2 << 16) | (ha3 << 8) | ha4;

	addr->qsa_family = AF_INET;
	((struct sockaddr_in *)addr)->sin_addr.s_addr = htonl(ipaddr);
	((struct sockaddr_in *)addr)->sin_port = htons((unsigned short)hp);
	return 0;
}

//=============================================================================

int UDP_GetSocketAddr (sys_socket_t socketid, struct qsockaddr *addr)
{
	socklen_t addrlen = sizeof(struct qsockaddr);
	in_addr_t a;

	memset(addr, 0, sizeof(struct qsockaddr));
	if (getsockname(socketid, (struct sockaddr *)addr, &addrlen) != 0)
		return -1;

	a = ((struct sockaddr_in *)addr)->sin_addr.s_addr;
	if (a == 0 || a == htonl(INADDR_LOOPBACK))
		((struct sockaddr_in *)addr)->sin_addr.s_addr = myAddr;

	return 0;
}

//=============================================================================

int UDP_GetNameFromAddr (struct qsockaddr *addr, char *name)
{
	struct hostent *hostentry;

	hostentry = gethostbyaddr ((char *)&((struct sockaddr_in *)addr)->sin_addr,
						sizeof(struct in_addr), AF_INET);
	if (hostentry)
	{
		strncpy (name, (char *)hostentry->h_name, NET_NAMELEN - 1);
		return 0;
	}

	strcpy (name, UDP_AddrToString (addr));
	return 0;
}

//=============================================================================

int UDP_GetAddrFromName (const char *name, struct qsockaddr *addr)
{
	struct hostent *hostentry;

	if (name[0] >= '0' && name[0] <= '9')
		return PartialIPAddress (name, addr);

	hostentry = gethostbyname (name);
	if (!hostentry)
		return -1;

	addr->qsa_family = AF_INET;
	((struct sockaddr_in *)addr)->sin_port = htons((unsigned short)net_hostport);
	((struct sockaddr_in *)addr)->sin_addr.s_addr =
						*(in_addr_t *)hostentry->h_addr_list[0];

	return 0;
}

//=============================================================================

int UDP_AddrCompare (struct qsockaddr *addr1, struct qsockaddr *addr2)
{
	if (addr1->qsa_family != addr2->qsa_family)
		return -1;

	if (((struct sockaddr_in *)addr1)->sin_addr.s_addr !=
	    ((struct sockaddr_in *)addr2)->sin_addr.s_addr)
		return -1;

	if (((struct sockaddr_in *)addr1)->sin_port !=
	    ((struct sockaddr_in *)addr2)->sin_port)
		return 1;

	return 0;
}

//=============================================================================

int UDP_GetSocketPort (struct qsockaddr *addr)
{
	return ntohs(((struct sockaddr_in *)addr)->sin_port);
}


int UDP_SetSocketPort (struct qsockaddr *addr, int port)
{
	((struct sockaddr_in *)addr)->sin_port = htons((unsigned short)port);
	return 0;
}

//=============================================================================

