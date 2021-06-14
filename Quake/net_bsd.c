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

#include "q_stdinc.h"
#include "arch_def.h"
#include "net_sys.h"
#include "quakedef.h"
#include "net_defs.h"

#include "net_dgrm.h"
#include "net_loop.h"

net_driver_t net_drivers[] =
{
	{	"Loopback",
		false,
		Loop_Init,
		Loop_Listen,
		Loop_QueryAddresses,
		Loop_SearchForHosts,
		Loop_Connect,
		Loop_CheckNewConnections,
		Loop_GetAnyMessage,
		Loop_GetMessage,
		Loop_SendMessage,
		Loop_SendUnreliableMessage,
		Loop_CanSendMessage,
		Loop_CanSendUnreliableMessage,
		Loop_Close,
		Loop_Shutdown
	},

	{	"Datagram",
		false,
		Datagram_Init,
		Datagram_Listen,
		Datagram_QueryAddresses,
		Datagram_SearchForHosts,
		Datagram_Connect,
		Datagram_CheckNewConnections,
		Datagram_GetAnyMessage,
		Datagram_GetMessage,
		Datagram_SendMessage,
		Datagram_SendUnreliableMessage,
		Datagram_CanSendMessage,
		Datagram_CanSendUnreliableMessage,
		Datagram_Close,
		Datagram_Shutdown
	}
};

const int net_numdrivers = (sizeof(net_drivers) / sizeof(net_drivers[0]));

#include "net_udp.h"

net_landriver_t	net_landrivers[] =
{
	{	"UDP",
		false,
		0,
		UDP4_Init,
		UDP4_Shutdown,
		UDP4_Listen,
		UDP4_GetAddresses,
		UDP4_OpenSocket,
		UDP_CloseSocket,
		UDP_Connect,
		UDP4_CheckNewConnections,
		UDP_Read,
		UDP_Write,
		UDP4_Broadcast,
		UDP_AddrToString,
		UDP4_StringToAddr,
		UDP_GetSocketAddr,
		UDP_GetNameFromAddr,
		UDP4_GetAddrFromName,
		UDP_AddrCompare,
		UDP_GetSocketPort,
		UDP_SetSocketPort
	},
	{	"UDP6",
		false,
		0,
		UDP6_Init,
		UDP6_Shutdown,
		UDP6_Listen,
		UDP6_GetAddresses,
		UDP6_OpenSocket,
		UDP_CloseSocket,
		UDP_Connect,
		UDP6_CheckNewConnections,
		UDP_Read,
		UDP_Write,
		UDP6_Broadcast,
		UDP_AddrToString,
		UDP6_StringToAddr,
		UDP_GetSocketAddr,
		UDP_GetNameFromAddr,
		UDP6_GetAddrFromName,
		UDP_AddrCompare,
		UDP_GetSocketPort,
		UDP_SetSocketPort
	}
};

const int net_numlandrivers = (sizeof(net_landrivers) / sizeof(net_landrivers[0]));

