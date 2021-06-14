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

#ifndef __NET_WINSOCK_H
#define __NET_WINSOCK_H

int  WINS_CloseSocket (sys_socket_t socketid);
int  WINS_Connect (sys_socket_t socketid, struct qsockaddr *addr);
int  WINS_Read (sys_socket_t socketid, byte *buf, int len, struct qsockaddr *addr);
int  WINS_Write (sys_socket_t socketid, byte *buf, int len, struct qsockaddr *addr);
const char *WINS_AddrToString (struct qsockaddr *addr, qboolean masked);
int  WINS_GetSocketAddr (sys_socket_t socketid, struct qsockaddr *addr);
int  WINS_AddrCompare (struct qsockaddr *addr1, struct qsockaddr *addr2);
int  WINS_GetSocketPort (struct qsockaddr *addr);
int  WINS_SetSocketPort (struct qsockaddr *addr, int port);

sys_socket_t  WINIPv4_Init (void);
void WINIPv4_Shutdown (void);
sys_socket_t WINIPv4_Listen (qboolean state);
int  WINIPv4_GetAddresses (qhostaddr_t *addresses, int maxaddresses);
sys_socket_t  WINIPv4_OpenSocket (int port);
sys_socket_t  WINIPv4_CheckNewConnections (void);
int  WINIPv4_Broadcast (sys_socket_t socketid, byte *buf, int len);
int  WINIPv4_StringToAddr (const char *string, struct qsockaddr *addr);
int  WINIPv4_GetNameFromAddr (struct qsockaddr *addr, char *name);
int  WINIPv4_GetAddrFromName (const char *name, struct qsockaddr *addr);


#ifdef IPPROTO_IPV6
sys_socket_t  WINIPv6_Init (void);
void WINIPv6_Shutdown (void);
sys_socket_t WINIPv6_Listen (qboolean state);
int  WINIPv6_GetAddresses (qhostaddr_t *addresses, int maxaddresses);
sys_socket_t  WINIPv6_OpenSocket (int port);
sys_socket_t  WINIPv6_CheckNewConnections (void);
int  WINIPv6_Broadcast (sys_socket_t socketid, byte *buf, int len);
int  WINIPv6_StringToAddr (const char *string, struct qsockaddr *addr);
int  WINIPv6_GetNameFromAddr (struct qsockaddr *addr, char *name);
int  WINIPv6_GetAddrFromName (const char *name, struct qsockaddr *addr);
#endif

#endif	/* __NET_WINSOCK_H */

