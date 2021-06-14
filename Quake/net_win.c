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


#include "net_wins.h"
#include "net_wipx.h"

net_landriver_t	net_landrivers[] =
{
	{	"Winsock TCPIP",
		false,
		0,
		WINIPv4_Init,
		WINIPv4_Shutdown,
		WINIPv4_Listen,
		WINIPv4_GetAddresses,
		WINIPv4_OpenSocket,
		WINS_CloseSocket,
		WINS_Connect,
		WINIPv4_CheckNewConnections,
		WINS_Read,
		WINS_Write,
		WINIPv4_Broadcast,
		WINS_AddrToString,
		WINIPv4_StringToAddr,
		WINS_GetSocketAddr,
		WINIPv4_GetNameFromAddr,
		WINIPv4_GetAddrFromName,
		WINS_AddrCompare,
		WINS_GetSocketPort,
		WINS_SetSocketPort
	},
#ifdef IPPROTO_IPV6
	{	"Winsock IPv6",
		false,
		0,
		WINIPv6_Init,
		WINIPv6_Shutdown,
		WINIPv6_Listen,
		WINIPv6_GetAddresses,
		WINIPv6_OpenSocket,
		WINS_CloseSocket,
		WINS_Connect,
		WINIPv6_CheckNewConnections,
		WINS_Read,
		WINS_Write,
		WINIPv6_Broadcast,
		WINS_AddrToString,
		WINIPv6_StringToAddr,
		WINS_GetSocketAddr,
		WINIPv6_GetNameFromAddr,
		WINIPv6_GetAddrFromName,
		WINS_AddrCompare,
		WINS_GetSocketPort,
		WINS_SetSocketPort
	},
#endif
	{	"Winsock IPX",
		false,
		0,
		WIPX_Init,
		WIPX_Shutdown,
		WIPX_Listen,
		WIPX_GetAddresses,
		WIPX_OpenSocket,
		WIPX_CloseSocket,
		WIPX_Connect,
		WIPX_CheckNewConnections,
		WIPX_Read,
		WIPX_Write,
		WIPX_Broadcast,
		WIPX_AddrToString,
		WIPX_StringToAddr,
		WIPX_GetSocketAddr,
		WIPX_GetNameFromAddr,
		WIPX_GetAddrFromName,
		WIPX_AddrCompare,
		WIPX_GetSocketPort,
		WIPX_SetSocketPort
	}
};

const int net_numlandrivers = (sizeof(net_landrivers) / sizeof(net_landrivers[0]));

