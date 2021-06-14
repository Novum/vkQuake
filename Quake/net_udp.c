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

static sys_socket_t net_acceptsocket4 = INVALID_SOCKET;	// socket for fielding new connections
static sys_socket_t net_controlsocket4;
static sys_socket_t net_broadcastsocket4 = INVALID_SOCKET;
static struct sockaddr_in broadcastaddr4;

static in_addr_t	myAddr4;

static sys_socket_t net_acceptsocket6 = INVALID_SOCKET;	// socket for fielding new connections
static sys_socket_t net_controlsocket6;

static struct in6_addr	myAddrv6;

#include "net_udp.h"

//=============================================================================

sys_socket_t UDP4_Init (void)
{
	char	*tst;
	struct qsockaddr	addr;

	if (COM_CheckParm ("-noudp") || COM_CheckParm ("-noudp4"))
		return INVALID_SOCKET;

	myAddr4 = htonl(INADDR_LOOPBACK);
#ifdef __linux__
	//gethostbyname(gethostname()) is only supported if the hostname can be looked up on an actual name server
	//this means its usable as a test to see if other hosts can see it, but means we cannot use it to find out the local address.
	//it also means stalls and slow loading and other undesirable behaviour, so lets stop doing this legacy junk.
	//we probably have multiple interfaces nowadays anyway (wan and lan and wifi and localhost and linklocal addresses and zomgwtf).
#else
	{
		// determine my name & address
		int	err;
		char	buff[MAXHOSTNAMELEN];
		struct hostent		*local;
		if (gethostname(buff, MAXHOSTNAMELEN) != 0)
		{
			err = SOCKETERRNO;
			Con_SafePrintf("UDP4_Init: gethostname failed (%s)\n",
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
				Con_SafePrintf("UDP4_Init: gethostbyname failed (%s)\n",
								hstrerror(h_errno));
			}
			else if (local->h_addrtype != AF_INET)
			{
				Con_SafePrintf("UDP4_Init: address from gethostbyname not IPv4\n");
			}
			else
			{
				myAddr4 = *(in_addr_t *)local->h_addr_list[0];
			}
		}
	}
#endif

	if ((net_controlsocket4 = UDP4_OpenSocket(0)) == INVALID_SOCKET)
	{
		Con_SafePrintf("UDP4_Init: Unable to open control socket, UDP disabled\n");
		return INVALID_SOCKET;
	}

	broadcastaddr4.sin_family = AF_INET;
	broadcastaddr4.sin_addr.s_addr = INADDR_BROADCAST;
	broadcastaddr4.sin_port = htons((unsigned short)net_hostport);

	UDP_GetSocketAddr (net_controlsocket4, &addr);
	strcpy(my_ipv4_address, UDP_AddrToString (&addr, false));
	tst = strrchr (my_ipv4_address, ':');
	if (tst) *tst = 0;

	Con_SafePrintf("UDP4 Initialized\n");
	ipv4Available = true;

	return net_controlsocket4;
}

//=============================================================================

void UDP4_Shutdown (void)
{
	UDP4_Listen (false);
	UDP_CloseSocket (net_controlsocket4);
}

//=============================================================================

sys_socket_t UDP4_Listen (qboolean state)
{
	if (state)
	{
		// enable listening
		if (net_acceptsocket4 == INVALID_SOCKET)
{
			if ((net_acceptsocket4 = UDP4_OpenSocket (net_hostport)) == INVALID_SOCKET)
				Sys_Error ("UDP4_Listen: Unable to open accept socket");
}
	}
	else
	{
		// disable listening
		if (net_acceptsocket4 != INVALID_SOCKET)
		{
			UDP_CloseSocket (net_acceptsocket4);
			net_acceptsocket4 = INVALID_SOCKET;
		}
	}
	return net_acceptsocket4;
}

//=============================================================================

sys_socket_t UDP4_OpenSocket (int port)
{
	sys_socket_t newsocket;
	struct sockaddr_in address;
	int _true = 1;
	int err;

	if ((newsocket = socket (PF_INET, SOCK_DGRAM, IPPROTO_UDP)) == INVALID_SOCKET)
	{
		err = SOCKETERRNO;
		Con_SafePrintf("UDP4_OpenSocket: %s\n", socketerror(err));
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
	Con_SafePrintf("UDP4_OpenSocket: %s\n", socketerror(err));
	UDP_CloseSocket (newsocket);
	return INVALID_SOCKET;
}

//=============================================================================

int UDP_CloseSocket (sys_socket_t socketid)
{
	if (socketid == net_broadcastsocket4)
		net_broadcastsocket4 = INVALID_SOCKET;
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
					(myAddr4 & htonl(mask)) | htonl(addr);

	return 0;
}

//=============================================================================

int UDP_Connect (sys_socket_t socketid, struct qsockaddr *addr)
{
	return 0;
}

//=============================================================================

sys_socket_t UDP4_CheckNewConnections (void)
{
	int		available;
	struct sockaddr_in	from;
	socklen_t	fromlen;
	char		buff[1];

	if (net_acceptsocket4 == INVALID_SOCKET)
		return INVALID_SOCKET;

	if (ioctl (net_acceptsocket4, FIONREAD, &available) == -1)
	{
		int err = SOCKETERRNO;
		Sys_Error ("UDP: ioctlsocket (FIONREAD) failed (%s)", socketerror(err));
	}
	if (available)
		return net_acceptsocket4;
	// quietly absorb empty packets
	recvfrom (net_acceptsocket4, buff, 0, 0, (struct sockaddr *) &from, &fromlen);
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

static int UDP4_MakeSocketBroadcastCapable (sys_socket_t socketid)
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
	net_broadcastsocket4 = socketid;

	return 0;
}

//=============================================================================

int UDP4_Broadcast (sys_socket_t socketid, byte *buf, int len)
{
	int	ret;

	if (socketid != net_broadcastsocket4)
	{
		if (net_broadcastsocket4 != INVALID_SOCKET)
			Sys_Error("Attempted to use multiple broadcasts sockets");
		ret = UDP4_MakeSocketBroadcastCapable (socketid);
		if (ret == -1)
		{
			Con_SafePrintf("Unable to make socket broadcast capable\n");
			return ret;
		}
	}

	return UDP_Write (socketid, buf, len, (struct qsockaddr *)&broadcastaddr4);
}

//=============================================================================

int UDP_Write (sys_socket_t socketid, byte *buf, int len, struct qsockaddr *addr)
{
	int	ret;
	socklen_t addrsize;
	if (addr->qsa_family == AF_INET)
		addrsize = sizeof(struct sockaddr_in);
	else if (addr->qsa_family == AF_INET6)
		addrsize = sizeof(struct sockaddr_in6);
	else
	{
		Con_SafePrintf ("UDP_Write: unknown family\n");
		return -1;	//some kind of error. a few systems get pissy if the size doesn't exactly match the address family
	}

	ret = sendto (socketid, buf, len, 0, (struct sockaddr *)addr, addrsize);
	if (!addr->qsa_family)
		Con_SafePrintf ("UDP_Write: family was cleared\n");
	if (ret == SOCKET_ERROR)
	{
		int err = SOCKETERRNO;
		if (err == NET_EWOULDBLOCK)
			return 0;
		if (err == ENETUNREACH)
			Con_SafePrintf ("UDP_Write: %s (%s)\n", socketerror(err), UDP_AddrToString(addr, false));
		else
			Con_SafePrintf ("UDP_Write, sendto: %s\n", socketerror(err));
	}
	return ret;
}

//=============================================================================

const char *UDP_AddrToString (struct qsockaddr *addr, qboolean masked)
{
	static char buffer[64];

	if (addr->qsa_family == AF_INET)
	{
		int		haddr = ntohl(((struct sockaddr_in *)addr)->sin_addr.s_addr);
		q_snprintf (buffer, sizeof(buffer), "%d.%d.%d.%d:%d", (haddr >> 24) & 0xff,
				   (haddr >> 16) & 0xff, (haddr >> 8) & 0xff, haddr & 0xff,
					ntohs(((struct sockaddr_in *)addr)->sin_port));
	}
	else if (addr->qsa_family == AF_INET6)
	{
		//evil type punning.
		unsigned short *s = (unsigned short*)&((struct sockaddr_in6 *)addr)->sin6_addr;
		if (((struct sockaddr_in6 *)addr)->sin6_scope_id)
		{
			q_snprintf(buffer, sizeof(buffer), "[%x:%x:%x:%x:%x:%x:%x:%x%%%i]:%d", 
					ntohs(s[0]),
					ntohs(s[1]),
					ntohs(s[2]),
					ntohs(s[3]),
					ntohs(s[4]),
					ntohs(s[5]),
					ntohs(s[6]),
					ntohs(s[7]),
					(int)((struct sockaddr_in6 *)addr)->sin6_scope_id,
					ntohs(((struct sockaddr_in6 *)addr)->sin6_port));
		}
		else
		{
			q_snprintf(buffer, sizeof(buffer), "[%x:%x:%x:%x:%x:%x:%x:%x]:%d", 
					ntohs(s[0]),
					ntohs(s[1]),
					ntohs(s[2]),
					ntohs(s[3]),
					ntohs(s[4]),
					ntohs(s[5]),
					ntohs(s[6]),
					ntohs(s[7]),
					ntohs(((struct sockaddr_in6 *)addr)->sin6_port));
		}
	}
	else
		strcpy(buffer, "?");
	return buffer;
}

//=============================================================================

int UDP4_StringToAddr (const char *string, struct qsockaddr *addr)
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

	if (addr->qsa_family == AF_INET)
	{
		a = ((struct sockaddr_in *)addr)->sin_addr.s_addr;
		if (a == 0 || a == htonl(INADDR_LOOPBACK))
			((struct sockaddr_in *)addr)->sin_addr.s_addr = myAddr4;
	}
	else if (addr->qsa_family == AF_INET6)
	{
		static const struct in6_addr in6addr_any = IN6ADDR_ANY_INIT;
		if (!memcmp(&((struct sockaddr_in6 *)addr)->sin6_addr, &in6addr_any, sizeof(in6addr_any)))
			memcpy(&((struct sockaddr_in6 *)addr)->sin6_addr, &myAddrv6, sizeof(((struct sockaddr_in6 *)addr)->sin6_addr));
	}

	return 0;
}

//=============================================================================

int UDP_GetNameFromAddr (struct qsockaddr *addr, char *name)
{
	if (addr->qsa_family == AF_INET)
	{
		struct hostent *hostentry;

		hostentry = gethostbyaddr ((char *)&((struct sockaddr_in *)addr)->sin_addr,
							sizeof(struct in_addr), AF_INET);
		if (hostentry)
		{
			strncpy (name, (char *)hostentry->h_name, NET_NAMELEN - 1);
			return 0;
		}
	}
	else if (addr->qsa_family == AF_INET6)
	{
		//meh, don't bother, its unreliable anyway.
	}

	strcpy (name, UDP_AddrToString (addr, false));
	return 0;
}

//=============================================================================

int UDP4_GetAddrFromName (const char *name, struct qsockaddr *addr)
{
	struct hostent *hostentry;
	char *colon;
	unsigned short port = net_hostport;

	if (name[0] >= '0' && name[0] <= '9')
		return PartialIPAddress (name, addr);

	colon = strrchr(name, ':');
	if (colon)
	{
		char dupe[MAXHOSTNAMELEN];
		if (colon-name+1 > MAXHOSTNAMELEN)
			return -1;
		memcpy(dupe, name, colon-name);
		dupe[colon-name] = 0;
		hostentry = gethostbyname (dupe);
		port = strtoul(colon+1, NULL, 10);
	}
	else
		hostentry = gethostbyname (name);
	if (!hostentry || hostentry->h_addrtype != AF_INET)
		return -1;

	addr->qsa_family = AF_INET;
	((struct sockaddr_in *)addr)->sin_port = htons(port);
	((struct sockaddr_in *)addr)->sin_addr.s_addr =
						*(in_addr_t *)hostentry->h_addr_list[0];

	return 0;
}

//=============================================================================

int UDP_AddrCompare (struct qsockaddr *addr1, struct qsockaddr *addr2)
{
	if (addr1->qsa_family != addr2->qsa_family)
		return -1;

	if (addr1->qsa_family == AF_INET)
	{
		if (((struct sockaddr_in *)addr1)->sin_addr.s_addr !=
		    ((struct sockaddr_in *)addr2)->sin_addr.s_addr)
			return -1;

		if (((struct sockaddr_in *)addr1)->sin_port !=
		    ((struct sockaddr_in *)addr2)->sin_port)
			return 1;

		return 0;
	}
	else if (addr1->qsa_family == AF_INET6)
	{
		if (memcmp(	&((struct sockaddr_in6 *)addr1)->sin6_addr,
					&((struct sockaddr_in6 *)addr2)->sin6_addr,
					sizeof(((struct sockaddr_in6 *)addr1)->sin6_addr)))
			return -1;

		if (((struct sockaddr_in6 *)addr1)->sin6_port !=
		    ((struct sockaddr_in6 *)addr2)->sin6_port)
			return 1;

		if (((struct sockaddr_in6 *)addr1)->sin6_scope_id &&
			((struct sockaddr_in6 *)addr2)->sin6_scope_id &&
			((struct sockaddr_in6 *)addr1)->sin6_scope_id !=
			((struct sockaddr_in6 *)addr2)->sin6_scope_id)	//the ipv6 scope id is for use with link-local addresses, to identify the specific interface.
			return 1;

		return 0;
	}
	else
		return -1;
}

//=============================================================================

int UDP_GetSocketPort (struct qsockaddr *addr)
{
	if (addr->qsa_family == AF_INET)
		return ntohs(((struct sockaddr_in *)addr)->sin_port);
	else if (addr->qsa_family == AF_INET6)
		return ntohs(((struct sockaddr_in6 *)addr)->sin6_port);
	else
		return -1;
}


int UDP_SetSocketPort (struct qsockaddr *addr, int port)
{
	if (addr->qsa_family == AF_INET)
		((struct sockaddr_in *)addr)->sin_port = htons((unsigned short)port);
	else if (addr->qsa_family == AF_INET6)
		((struct sockaddr_in6 *)addr)->sin6_port = htons((unsigned short)port);
	else
		return -1;
	return 0;
}

//=============================================================================











sys_socket_t UDP6_Init (void)
{
	char	*colon;
	struct qsockaddr	addr;

	if (COM_CheckParm ("-noudp") || COM_CheckParm ("-noudp6"))
		return INVALID_SOCKET;

	// TODO: determine my name & address
	
	if ((net_controlsocket6 = UDP6_OpenSocket(0)) == INVALID_SOCKET)
	{
		Con_SafePrintf("UDP6_Init: Unable to open control socket, UDPv6 disabled\n");
		return INVALID_SOCKET;
	}

	UDP_GetSocketAddr (net_controlsocket6, &addr);
	strcpy(my_ipv6_address, UDP_AddrToString (&addr, false));
	colon = strrchr (my_ipv6_address, ':');
	if (colon)
		*colon = 0;

	Con_SafePrintf("UDPv6 Initialized\n");
	ipv6Available = true;

	return net_controlsocket6;
}

//=============================================================================

void UDP6_Shutdown (void)
{
	UDP6_Listen (false);
	UDP_CloseSocket (net_controlsocket6);
}

//=============================================================================

sys_socket_t UDP6_Listen (qboolean state)
{
	if (state)
	{
		// enable listening
		if (net_acceptsocket6 == INVALID_SOCKET)
		{
			if ((net_acceptsocket6 = UDP6_OpenSocket (net_hostport)) == INVALID_SOCKET)
				Sys_Error ("UDP6_Listen: Unable to open accept socket");
		}
	}
	else
	{
		// disable listening
		if (net_acceptsocket6 != INVALID_SOCKET)
		{
			UDP_CloseSocket (net_acceptsocket6);
			net_acceptsocket6 = INVALID_SOCKET;
		}
	}
	return net_acceptsocket6;
}

//=============================================================================

sys_socket_t UDP6_OpenSocket (int port)
{
	sys_socket_t newsocket;
	struct sockaddr_in6 address;
	int _true = 1;
	int err;

	if ((newsocket = socket (PF_INET6, SOCK_DGRAM, IPPROTO_UDP)) == INVALID_SOCKET)
	{
		err = SOCKETERRNO;
		Con_SafePrintf("UDP6_OpenSocket: %s\n", socketerror(err));
		return INVALID_SOCKET;
	}

	setsockopt(newsocket, IPPROTO_IPV6, IPV6_V6ONLY, (char *)&_true, sizeof(_true));

	if (ioctlsocket (newsocket, FIONBIO, &_true) == SOCKET_ERROR)
		goto ErrorReturn;

	memset(&address, 0, sizeof(struct sockaddr_in6));
	address.sin6_family = AF_INET6;
	memset(&address.sin6_addr, 0, sizeof(address.sin6_addr));
	address.sin6_port = htons((unsigned short)port);
	if (bind (newsocket, (struct sockaddr *)&address, sizeof(address)) == 0)
	{
		//we don't know if we're the server or not. oh well.
		struct ipv6_mreq req;
		memset(&req, 0, sizeof(req));
		req.ipv6mr_multiaddr.s6_addr[0] = 0xff;
		req.ipv6mr_multiaddr.s6_addr[1] = 0x03;
		req.ipv6mr_multiaddr.s6_addr[15] = 0x01;
		req.ipv6mr_interface = 0;
		setsockopt(newsocket, IPPROTO_IPV6, IPV6_JOIN_GROUP, (char *)&req, sizeof(req));

		return newsocket;
	}

ErrorReturn:
	err = SOCKETERRNO;
	Con_SafePrintf("UDP6_OpenSocket: %s\n", socketerror(err));
	UDP_CloseSocket (newsocket);
	return INVALID_SOCKET;
}

//=============================================================================

sys_socket_t UDP6_CheckNewConnections (void)
{
	int		available;
	struct sockaddr_in	from;
	socklen_t	fromlen;
	char		buff[1];

	if (net_acceptsocket6 == INVALID_SOCKET)
		return INVALID_SOCKET;

	if (ioctl (net_acceptsocket6, FIONREAD, &available) == -1)
	{
		int err = SOCKETERRNO;
		Sys_Error ("UDP6: ioctlsocket (FIONREAD) failed (%s)", socketerror(err));
	}
	if (available)
		return net_acceptsocket6;
	// quietly absorb empty packets
	fromlen = sizeof(from);
	recvfrom (net_acceptsocket6, buff, 0, 0, (struct sockaddr *) &from, &fromlen);
	return INVALID_SOCKET;
}

//=============================================================================

int UDP6_Broadcast (sys_socket_t socketid, byte *buf, int len)
{
	struct sockaddr_in6	address;

	memset(&address, 0, sizeof(struct sockaddr_in6));
	address.sin6_family = AF_INET6;
	memset(&address.sin6_addr, 0, sizeof(address.sin6_addr));
	address.sin6_addr.s6_addr[0] = 0xff;
	address.sin6_addr.s6_addr[1] = 0x03;
	address.sin6_addr.s6_addr[15] = 0x1;
	address.sin6_port = htons((unsigned short)net_hostport);

	return UDP_Write (socketid, buf, len, (struct qsockaddr *)&address);
}

//=============================================================================

int UDP6_StringToAddr (const char *string, struct qsockaddr *addr)
{	//This is never actually called...
	Con_SafePrintf("UDP6_StringToAddr: %s\n", string);
	return UDP6_GetAddrFromName(string, addr);
}

//=============================================================================

int UDP6_GetAddrFromName (const char *name, struct qsockaddr *addr)
{
	struct addrinfo *addrinfo = NULL;
	struct addrinfo *pos;
	struct addrinfo udp6hint;
	int error;
	char *port;
	char dupbase[256];
	size_t len;
	qboolean success = false;

	memset(&udp6hint, 0, sizeof(udp6hint));
	udp6hint.ai_family = 0;//Any... we check for AF_INET6 or 4
	udp6hint.ai_socktype = SOCK_DGRAM;
	udp6hint.ai_protocol = IPPROTO_UDP;

	if (*name == '[')
	{
		port = strstr(name, "]");
		if (!port)
			error = EAI_NONAME;
		else
		{
			len = port - (name+1);
			if (len >= sizeof(dupbase))
				len = sizeof(dupbase)-1;
			strncpy(dupbase, name+1, len);
			dupbase[len] = '\0';
			error = getaddrinfo(dupbase, (port[1] == ':')?port+2:NULL, &udp6hint, &addrinfo);
		}
	}
	else
	{
		port = strrchr(name, ':');

		if (port)
		{
			len = port - name;
			if (len >= sizeof(dupbase))
				len = sizeof(dupbase)-1;
			strncpy(dupbase, name, len);
			dupbase[len] = '\0';
			error = getaddrinfo(dupbase, port+1, &udp6hint, &addrinfo);
		}
		else
			error = EAI_NONAME;
		if (error)	//failed, try string with no port.
			error = getaddrinfo(name, NULL, &udp6hint, &addrinfo);	//remember, this func will return any address family that could be using the udp protocol... (ip4 or ip6)
	}

	if (!error)
	{
		((struct sockaddr*)addr)->sa_family = 0;
		for (pos = addrinfo; pos; pos = pos->ai_next)
		{
			if (0)//pos->ai_family == AF_INET)
			{
				memcpy(addr, pos->ai_addr, pos->ai_addrlen);
				success = true;
				break;
			}
			if (pos->ai_family == AF_INET6 && !success)
			{
				memcpy(addr, pos->ai_addr, pos->ai_addrlen);
				success = true;
			}
		}
		freeaddrinfo (addrinfo);
	}

	if (success)
	{
		if (((struct sockaddr*)addr)->sa_family == AF_INET)
		{
			if (!((struct sockaddr_in *)addr)->sin_port)
				((struct sockaddr_in *)addr)->sin_port = htons(net_hostport);
		}
		else if (((struct sockaddr*)addr)->sa_family == AF_INET6)
		{
			if (!((struct sockaddr_in6 *)addr)->sin6_port)
				((struct sockaddr_in6 *)addr)->sin6_port = htons(net_hostport);
		}
		return 0;
	}
	return -1;
}

//=============================================================================

#ifdef __linux__ //sadly there is no posix standard for querying all ipv4+ipv6 addresses.
#include <ifaddrs.h>
static struct ifaddrs *iflist;
static double iftime; //requery sometimes.
static int UDP_GetAddresses(qhostaddr_t *addresses, int maxaddresses, int fam)
{
	struct ifaddrs *ifa;
	int result = 0;
	double time = Sys_DoubleTime();
	size_t l;
	if (time - iftime > 1 && iflist)
	{
		freeifaddrs(iflist);
		iflist = NULL;
	}
	if (!iflist)
	{
		iftime = time;
		getifaddrs(&iflist);
	}

	for (ifa = iflist; ifa && result < maxaddresses; ifa = ifa->ifa_next)
	{
		//can happen if the interface is not bound.
		if (ifa->ifa_addr == NULL)
			continue;
		if (fam == ifa->ifa_addr->sa_family)
		{
			q_strlcpy(addresses[result], UDP_AddrToString((struct qsockaddr*)ifa->ifa_addr, false), sizeof(addresses[0]));
			l = strlen(addresses[result]);	//trim any useless :0 port numbers.
			if (l > 2 && !strcmp(addresses[result]+l-2, ":0"))
				addresses[result][l-2] = 0;
			result++;
		}
	}
	return result;
}
#else
//for other systems, like macs, where we don't know how to query this stuff properly.
//FIXME: there is a posix standard for ipv4 at least.
static int UDP_GetAddresses(qhostaddr_t *addresses, int maxaddresses, int fam)
{
	return 0;
}
#endif
int     UDP4_GetAddresses (qhostaddr_t *addresses, int maxaddresses)
{
	return UDP_GetAddresses(addresses, maxaddresses, AF_INET);
}
int     UDP6_GetAddresses (qhostaddr_t *addresses, int maxaddresses)
{
	return UDP_GetAddresses(addresses, maxaddresses, AF_INET6);
}

