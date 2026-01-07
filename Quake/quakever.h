/*
Copyright (C) 2016-2024 vkQuake developers

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

#ifndef QUAKEVER_H
#define QUAKEVER_H

#define VERSION			 1.09
#define GLQUAKE_VERSION	 1.00
#define D3DQUAKE_VERSION 0.01
#define WINQUAKE_VERSION 0.996
#define X11_VERSION		 1.10

#define FITZQUAKE_VERSION	 0.85 // johnfitz
#define QUAKESPASM_VERSION	 0.97
#define QUAKESPASM_VER_PATCH 0 // helper to print a string like 0.97.0
#ifndef QUAKESPASM_VER_SUFFIX
#define QUAKESPASM_VER_SUFFIX // optional version suffix string literal like "-beta1"
#endif

#define VKQUAKE_VERSION_MAJOR 1
#define VKQUAKE_VERSION_MINOR 33
#define VKQUAKE_VER_PATCH	  2

#define VKQUAKE_VERSION		   1.33
#define VKQUAKE_COPYRIGHT_YEAR "2026"

#define LINUX_VERSION VKQUAKE_VERSION

#ifndef VKQUAKE_VER_SUFFIX
#define VKQUAKE_VER_SUFFIX "" // optional version suffix like -beta1
#endif

#define QS_STRINGIFY_(x) #x
#define QS_STRINGIFY(x)	 QS_STRINGIFY_ (x)

// combined version string like "0.92.1-beta1"
#define QUAKESPASM_VER_STRING QS_STRINGIFY (QUAKESPASM_VERSION) "." QS_STRINGIFY (QUAKESPASM_VER_PATCH) QUAKESPASM_VER_SUFFIX
#define VKQUAKE_VER_STRING	  QS_STRINGIFY (VKQUAKE_VERSION) "." QS_STRINGIFY (VKQUAKE_VER_PATCH) VKQUAKE_VER_SUFFIX

#ifdef QSS_DATE
// combined version string like "2020-10-20-beta1"
#define ENGINE_NAME_AND_VER "vkQuake " QS_STRINGIFY (QSS_DATE) VKQUAKE_VER_SUFFIX
#else
#define ENGINE_NAME_AND_VER \
	"vkQuake"               \
	" " VKQUAKE_VER_STRING
#endif
#endif /* QUAKEVER_H */
