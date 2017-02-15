/*
 * arch_def.h
 * platform specific definitions
 * - standalone header
 * - doesn't and must not include any other headers
 * - shouldn't depend on compiler.h, q_stdinc.h, or
 *   any other headers
 *
 * Copyright (C) 2007-2016  O.Sezer <sezero@users.sourceforge.net>
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

#ifndef ARCHDEFS_H
#define ARCHDEFS_H


#if defined(__DJGPP__) || defined(__MSDOS__) || defined(__DOS__) || defined(_MSDOS)

#   if !defined(PLATFORM_DOS)
#	define	PLATFORM_DOS		1
#   endif

#elif defined(__OS2__) || defined(__EMX__)

#   if !defined(PLATFORM_OS2)
#	define	PLATFORM_OS2		1
#   endif

#elif defined(_WIN32) || defined(__WIN32__) || defined(_WIN64) || defined(__NT__) || defined(_Windows)

#   if !defined(PLATFORM_WINDOWS)
#	define	PLATFORM_WINDOWS	1
#   endif

#elif defined(__APPLE__) && defined(__MACH__)		/* Mac OS X */

#   if !defined(PLATFORM_OSX)
#	define	PLATFORM_OSX		1
#   endif

#elif defined(macintosh)			/* Mac OS classic */

#   if !defined(PLATFORM_MAC)
#	define	PLATFORM_MAC		1
#   endif

#elif defined(__MORPHOS__) || defined(__AROS__) || defined(AMIGAOS)	|| \
      defined(__amigaos__) || defined(__amigaos4__) ||defined(__amigados__) || \
      defined(AMIGA) || defined(_AMIGA) || defined(__AMIGA__)

#   if !defined(PLATFORM_AMIGA)
#	define	PLATFORM_AMIGA		1
#   endif

#elif defined(__riscos__)

#   if !defined(PLATFORM_RISCOS)
#	define	PLATFORM_RISCOS		1
#   endif

#else	/* here goes the unix platforms */

#if defined(__unix) || defined(__unix__) || defined(unix)	|| \
    defined(__linux__) || defined(__linux)			|| \
    defined(__FreeBSD__) || defined(__DragonFly__)		|| \
    defined(__FreeBSD_kernel__) /* Debian GNU/kFreeBSD */	|| \
    defined(__OpenBSD__) || defined(__NetBSD__)			|| \
    defined(__hpux) || defined(__hpux__) || defined(_hpux)	|| \
    defined(__sun) || defined(sun)				|| \
    defined(__sgi) || defined(sgi) || defined(__sgi__)		|| \
    defined(__GNU__) /* GNU/Hurd */				|| \
    defined(__QNX__) || defined(__QNXNTO__)
#   if !defined(PLATFORM_UNIX)
#	define	PLATFORM_UNIX		1
#   endif
#endif

#endif	/* PLATFORM_xxx */


#if defined (PLATFORM_OSX)		/* OS X is unix-based */
#   if !defined(PLATFORM_UNIX)
#	define	PLATFORM_UNIX		2
#   endif
#endif	/* OS X -> PLATFORM_UNIX */


#if defined(__FreeBSD__) || defined(__DragonFly__)		|| \
    defined(__FreeBSD_kernel__) /* Debian GNU/kFreeBSD */	|| \
    defined(__OpenBSD__) || defined(__NetBSD__)
#   if !defined(PLATFORM_BSD)
#	define	PLATFORM_BSD		1
#   endif
#endif	/* PLATFORM_BSD (for convenience) */


#if defined(PLATFORM_AMIGA) && !defined(PLATFORM_AMIGAOS3)
#   if !defined(__MORPHOS__) && !defined(__AROS__) && !defined(__amigaos4__)
#	define	PLATFORM_AMIGAOS3	1
#   endif
#endif	/* PLATFORM_AMIGAOS3 (for convenience) */


#if defined(_WIN64)
#	define	PLATFORM_STRING	"Win64"
#elif defined(PLATFORM_WINDOWS)
#	define	PLATFORM_STRING	"Windows"
#elif defined(PLATFORM_DOS)
#	define	PLATFORM_STRING	"DOS"
#elif defined(PLATFORM_OS2)
#	define	PLATFORM_STRING	"OS/2"
#elif defined(__linux__) || defined(__linux)
#	define	PLATFORM_STRING	"Linux"
#elif defined(__DragonFly__)
#	define	PLATFORM_STRING "DragonFly"
#elif defined(__FreeBSD__)
#	define	PLATFORM_STRING	"FreeBSD"
#elif defined(__NetBSD__)
#	define	PLATFORM_STRING	"NetBSD"
#elif defined(__OpenBSD__)
#	define	PLATFORM_STRING	"OpenBSD"
#elif defined(__MORPHOS__)
#	define	PLATFORM_STRING	"MorphOS"
#elif defined(__AROS__)
#	define	PLATFORM_STRING	"AROS"
#elif defined(__amigaos4__)
#	define	PLATFORM_STRING	"AmigaOS4"
#elif defined(PLATFORM_AMIGA)
#	define	PLATFORM_STRING	"AmigaOS"
#elif defined(__QNX__) || defined(__QNXNTO__)
#	define	PLATFORM_STRING	"QNX"
#elif defined(PLATFORM_OSX)
#	define	PLATFORM_STRING	"MacOSX"
#elif defined(PLATFORM_MAC)
#	define	PLATFORM_STRING	"MacOS"
#elif defined(__hpux) || defined(__hpux__) || defined(_hpux)
#	define	PLATFORM_STRING	"HP-UX"
#elif (defined(__sun) || defined(sun)) && (defined(__svr4__) || defined(__SVR4))
#	define	PLATFORM_STRING	"Solaris"
#elif defined(__sun) || defined(sun)
#	define	PLATFORM_STRING	"SunOS"
#elif defined(__sgi) || defined(sgi) || defined(__sgi__)
#	define	PLATFORM_STRING	"Irix"
#elif defined(PLATFORM_RISCOS)
#	define	PLATFORM_STRING	"RiscOS"
#elif defined(__GNU__)
#	define	PLATFORM_STRING	"GNU/Hurd"
#elif defined(PLATFORM_UNIX)
#	define	PLATFORM_STRING	"Unix"
#else
#	define	PLATFORM_STRING	"Unknown"
#	warning "Platform is UNKNOWN."
#endif	/* PLATFORM_STRING */

#endif  /* ARCHDEFS_H */

