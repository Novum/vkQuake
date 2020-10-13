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

#ifndef __NET_LOOP_H
#define __NET_LOOP_H

// net_loop.h
int		Loop_Init (void);
void		Loop_Listen (qboolean state);
#define		Loop_QueryAddresses NULL
qboolean	Loop_SearchForHosts (qboolean xmit);
qsocket_t	*Loop_Connect (const char *host);
qsocket_t	*Loop_CheckNewConnections (void);
qsocket_t	*Loop_GetAnyMessage(void);
int		Loop_GetMessage (qsocket_t *sock);
int		Loop_SendMessage (qsocket_t *sock, sizebuf_t *data);
int		Loop_SendUnreliableMessage (qsocket_t *sock, sizebuf_t *data);
qboolean	Loop_CanSendMessage (qsocket_t *sock);
qboolean	Loop_CanSendUnreliableMessage (qsocket_t *sock);
void		Loop_Close (qsocket_t *sock);
void		Loop_Shutdown (void);

#endif	/* __NET_LOOP_H */

