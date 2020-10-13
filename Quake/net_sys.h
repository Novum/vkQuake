/*
 * net_sys.h -- common network system header.
 * - depends on arch_def.h
 * - may depend on q_stdinc.h
 *
 * Copyright (C) 2007-2012  O.Sezer <sezero@users.sourceforge.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifndef __NET_SYS_H__
#define __NET_SYS_H__

#include <sys/types.h>
#include <errno.h>
#include <stddef.h>
#include <limits.h>

#if defined(PLATFORM_BSD) || defined(PLATFORM_OSX)	|| \
    defined(PLATFORM_AMIGA) /* bsdsocket.library */	|| \
    defined(__GNU__) /* GNU/Hurd */ || defined(__riscos__)
/* struct sockaddr has unsigned char sa_len as the first member in BSD
 * variants and the family member is also an unsigned char instead of an
 * unsigned short. This should matter only when PLATFORM_UNIX is defined,
 * however, checking for the offset of sa_family in every platform that
 * provide a struct sockaddr doesn't hurt either (see down below for the
 * compile time asserts.) */
/* FIXME : GET RID OF THIS ABOMINATION !!! */
#define	HAVE_SA_LEN	1
#define	SA_FAM_OFFSET	1
#else
#undef	HAVE_SA_LEN
#define	SA_FAM_OFFSET	0
#endif	/* BSD, sockaddr */

/* unix includes and compatibility macros */
#if defined(PLATFORM_UNIX) || defined(PLATFORM_RISCOS)

#include <sys/param.h>
#include <sys/ioctl.h>
#if defined(__sun) || defined(sun)
#include <sys/filio.h>
#include <sys/sockio.h>
#endif	/* __sunos__ */
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

typedef int	sys_socket_t;
#define	INVALID_SOCKET	(-1)
#define	SOCKET_ERROR	(-1)

#if defined(__APPLE__) && defined(SO_NKE) && !defined(SO_NOADDRERR)
				/* ancient Mac OS X SDKs 10.2 and older are missing socklen_t */
typedef int	socklen_t;			/* defining as signed int to match the old api */
#endif	/* ancient OSX SDKs */

#define	SOCKETERRNO	errno
#define	ioctlsocket	ioctl
#define	closesocket	close
#define	selectsocket	select
#define	IOCTLARG_P(x)	/* (char *) */ x

#define	NET_EWOULDBLOCK		EWOULDBLOCK
#define	NET_ECONNREFUSED	ECONNREFUSED

#define	socketerror(x)	strerror((x))

/* Verify that we defined HAVE_SA_LEN correctly: */
COMPILE_TIME_ASSERT(sockaddr, offsetof(struct sockaddr, sa_family) == SA_FAM_OFFSET);

#endif	/* end of unix stuff */


/* amiga includes and compatibility macros */
#if defined(PLATFORM_AMIGA) /* Amiga bsdsocket.library */

#include <sys/param.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <proto/exec.h>
#include <proto/socket.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

typedef int	sys_socket_t;
#define	INVALID_SOCKET	(-1)
#define	SOCKET_ERROR	(-1)

#if !(defined(__AROS__) || defined(__amigaos4__))
typedef LONG	socklen_t;	/* int32_t */
#endif
#if !defined(__amigaos4__)
#if (LONG_MAX <= 2147483647L)
typedef unsigned long	in_addr_t;	/* u_int32_t */
#else
typedef unsigned int	in_addr_t;	/* u_int32_t */
#endif
#endif

#define	SOCKETERRNO	Errno()
#define	ioctlsocket	IoctlSocket
#define	closesocket	CloseSocket
#define	selectsocket(_N,_R,_W,_E,_T)		\
	WaitSelect((_N),(_R),(_W),(_E),(_T),NULL)
#define	IOCTLARG_P(x)	(char *) x
#if defined(__amigaos4__) || defined(PLATFORM_AMIGAOS3)
#define	inet_ntoa(x) Inet_NtoA(x.s_addr) /* Inet_NtoA(*(ULONG*)&x) */
#define	h_errno Errno()
#endif

#define	NET_EWOULDBLOCK		EWOULDBLOCK
#define	NET_ECONNREFUSED	ECONNREFUSED

#define	socketerror(x)	strerror((x))
/* there is h_errno but no hstrerror() */
#define	hstrerror(x)	strerror((x))

/* Verify that we defined HAVE_SA_LEN correctly: */
COMPILE_TIME_ASSERT(sockaddr, offsetof(struct sockaddr, sa_family) == SA_FAM_OFFSET);

#endif	/* end of amiga bsdsocket.library stuff */


/* windows includes and compatibility macros */
#if defined(PLATFORM_WINDOWS)

/* NOTE: winsock[2].h already includes windows.h */
#if !defined(_USE_WINSOCK2)
#include <winsock.h>
#else
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

/* there is no in_addr_t on windows: define it as
   the type of the S_addr of in_addr structure */
typedef u_long	in_addr_t;	/* uint32_t */

/* on windows, socklen_t is to be a winsock2 thing */
#if !defined(IP_MSFILTER_SIZE)
typedef int	socklen_t;
#endif	/* socklen_t type */

typedef SOCKET	sys_socket_t;

#define	selectsocket	select
#define	IOCTLARG_P(x)	/* (u_long *) */ x

#define	SOCKETERRNO	WSAGetLastError()
#define	NET_EWOULDBLOCK		WSAEWOULDBLOCK
#define	NET_ECONNREFUSED	WSAECONNREFUSED
/* must #include "wsaerror.h" for this : */
#define	socketerror(x)	__WSAE_StrError((x))

/* Verify that we defined HAVE_SA_LEN correctly: */
COMPILE_TIME_ASSERT(sockaddr, offsetof(struct sockaddr, sa_family) == SA_FAM_OFFSET);

#endif	/* end of windows stuff */


/* macros which may still be missing */

#if !defined(INADDR_NONE)
#define	INADDR_NONE	((in_addr_t) 0xffffffff)
#endif	/* INADDR_NONE */

#if !defined(INADDR_LOOPBACK)
#define	INADDR_LOOPBACK	((in_addr_t) 0x7f000001)	/* 127.0.0.1	*/
#endif	/* INADDR_LOOPBACK */


#if !defined(MAXHOSTNAMELEN)
/* SUSv2 guarantees that `Host names are limited to 255 bytes'.
   POSIX 1003.1-2001 guarantees that `Host names (not including
   the terminating NUL) are limited to HOST_NAME_MAX bytes'. */
#define	MAXHOSTNAMELEN		256
#endif	/* MAXHOSTNAMELEN */


#endif	/* __NET_SYS_H__ */

