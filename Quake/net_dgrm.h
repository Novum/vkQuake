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

#ifndef __NET_DATAGRAM_H
#define __NET_DATAGRAM_H

int			Datagram_Init (void);
void		Datagram_Listen (qboolean state);
void		Datagram_SearchForHosts (qboolean xmit);
qsocket_t	*Datagram_Connect (const char *host);
qsocket_t	*Datagram_CheckNewConnections (void);
int			Datagram_GetMessage (qsocket_t *sock);
int			Datagram_SendMessage (qsocket_t *sock, sizebuf_t *data);
int			Datagram_SendUnreliableMessage (qsocket_t *sock, sizebuf_t *data);
qboolean	Datagram_CanSendMessage (qsocket_t *sock);
qboolean	Datagram_CanSendUnreliableMessage (qsocket_t *sock);
void		Datagram_Close (qsocket_t *sock);
void		Datagram_Shutdown (void);

#endif	/* __NET_DATAGRAM_H */

