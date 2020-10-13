/*
Copyright (C) 1996-1997 Id Software, Inc.
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

#ifndef __net_udp_h
#define __net_udp_h

sys_socket_t  UDP4_Init (void);
void UDP4_Shutdown (void);
sys_socket_t UDP4_Listen (qboolean state);
sys_socket_t  UDP4_OpenSocket (int port);
int  UDP_CloseSocket (sys_socket_t socketid);
int  UDP_Connect (sys_socket_t socketid, struct qsockaddr *addr);
sys_socket_t  UDP4_CheckNewConnections (void);
int  UDP_Read (sys_socket_t socketid, byte *buf, int len, struct qsockaddr *addr);
int  UDP_Write (sys_socket_t socketid, byte *buf, int len, struct qsockaddr *addr);
int  UDP4_Broadcast (sys_socket_t socketid, byte *buf, int len);
const char *UDP_AddrToString (struct qsockaddr *addr, qboolean masked);
int  UDP4_StringToAddr (const char *string, struct qsockaddr *addr);
int  UDP_GetSocketAddr (sys_socket_t socketid, struct qsockaddr *addr);
int  UDP_GetNameFromAddr (struct qsockaddr *addr, char *name);
int  UDP4_GetAddrFromName (const char *name, struct qsockaddr *addr);
int  UDP_AddrCompare (struct qsockaddr *addr1, struct qsockaddr *addr2);
int  UDP_GetSocketPort (struct qsockaddr *addr);
int  UDP_SetSocketPort (struct qsockaddr *addr, int port);
int  UDP4_GetAddresses (qhostaddr_t *addresses, int maxaddresses);


sys_socket_t  UDP6_Init (void);
void UDP6_Shutdown (void);
sys_socket_t UDP6_Listen (qboolean state);
sys_socket_t  UDP6_OpenSocket (int port);
int  UDP_CloseSocket (sys_socket_t socketid);
int  UDP_Connect (sys_socket_t socketid, struct qsockaddr *addr);
sys_socket_t  UDP6_CheckNewConnections (void);
int  UDP_Read (sys_socket_t socketid, byte *buf, int len, struct qsockaddr *addr);
int  UDP_Write (sys_socket_t socketid, byte *buf, int len, struct qsockaddr *addr);
int  UDP6_Broadcast (sys_socket_t socketid, byte *buf, int len);
const char *UDP_AddrToString (struct qsockaddr *addr, qboolean masked);
int  UDP6_StringToAddr (const char *string, struct qsockaddr *addr);
int  UDP_GetSocketAddr (sys_socket_t socketid, struct qsockaddr *addr);
int  UDP_GetNameFromAddr (struct qsockaddr *addr, char *name);
int  UDP6_GetAddrFromName (const char *name, struct qsockaddr *addr);
int  UDP_AddrCompare (struct qsockaddr *addr1, struct qsockaddr *addr2);
int  UDP_GetSocketPort (struct qsockaddr *addr);
int  UDP_SetSocketPort (struct qsockaddr *addr, int port);
int  UDP6_GetAddresses (qhostaddr_t *addresses, int maxaddresses);

#endif	/* __net_udp_h */

