/*
Copyright (C) 2022 Axel Gneiting

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
// mem.c -- memory allocator

#include "mem.h"
#include "quakedef.h"

#if !defined(USE_MI_MALLOC) && !defined(USE_SDL_MALLOC) && !defined(USE_CRT_MALLOC)
#define USE_MI_MALLOC
#endif

#if defined(USE_MI_MALLOC)
#include "mimalloc/static.c"
#elif defined(USE_CRT_MALLOC)
#include <stdlib.h>
#endif

#ifndef _MSC_VER
#include <sys/time.h>
#include <sys/resource.h>
#endif

#define THREAD_STACK_RESERVATION (128ll * 1024ll)
#define MAX_STACK_ALLOC_SIZE     (512ll * 1024ll)

THREAD_LOCAL size_t thread_stack_alloc_size = 0;
THREAD_LOCAL size_t max_thread_stack_alloc_size = 0;

/*
====================
Mem_InitThread
====================
*/
void Mem_InitThread ()
{
#ifdef _MSC_VER
	max_thread_stack_alloc_size = MAX_STACK_ALLOC_SIZE;
#else
	struct rlimit limit;
	if (getrlimit (RLIMIT_STACK, &limit) == 0)
	{
		max_thread_stack_alloc_size = (size_t)CLAMP (0ll, (int64_t)limit.rlim_cur - THREAD_STACK_RESERVATION, MAX_STACK_ALLOC_SIZE);
	}
#endif
}

/*
====================
Mem_Alloc
====================
*/
void *Mem_Alloc (const size_t size)
{
#if defined(USE_MI_MALLOC)
	return mi_calloc (1, size);
#elif defined(USE_SDL_MALLOC)
	return SDL_calloc (1, size);
#elif defined(USE_CRT_MALLOC)
	return calloc (1, size);
#endif
}

/*
====================
Mem_Realloc
====================
*/
void *Mem_Realloc (void *ptr, const size_t size)
{
#if defined(USE_MI_MALLOC)
	return mi_realloc (ptr, size);
#elif defined(USE_SDL_MALLOC)
	return SDL_realloc (ptr, size);
#elif defined(USE_CRT_MALLOC)
	return realloc (ptr, size);
#endif
}

/*
====================
Mem_Free
====================
*/
void Mem_Free (const void *ptr)
{
#if defined(USE_MI_MALLOC)
	mi_free ((void *)ptr);
#elif defined(USE_SDL_MALLOC)
	SDL_free ((void *)ptr);
#elif defined(USE_CRT_MALLOC)
	free ((void *)ptr);
#endif
}
