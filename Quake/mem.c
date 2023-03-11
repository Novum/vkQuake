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

#include "quakedef.h"
#include "mem.h"

#if !defined(USE_CRT_MALLOC) && !defined(USE_MI_MALLOC) && !defined(USE_SDL_MALLOC)
#if defined(USE_HELGRIND) || (!defined(__APPLE__) && !defined(__linux__) && !defined(_WIN32))
#define USE_CRT_MALLOC
#else
#define USE_MI_MALLOC
#endif
#endif

#if defined(USE_MI_MALLOC)
#undef snprintf
#define snprintf q_snprintf
#undef vsnprintf
#define vsnprintf q_vsnprintf
#include "mimalloc/static.c"
#elif defined(USE_CRT_MALLOC)
#include <stdlib.h>
#endif

#ifndef _WIN32
#include <pthread.h>
#endif

#define THREAD_STACK_RESERVATION (128ll * 1024ll)
#define MAX_STACK_ALLOC_SIZE	 (512ll * 1024ll)

size_t THREAD_LOCAL thread_stack_alloc_size = 0;
size_t				max_thread_stack_alloc_size = 0;

/*
====================
Mem_Init
====================
*/
void Mem_Init ()
{
#ifdef _WIN32
	max_thread_stack_alloc_size = MAX_STACK_ALLOC_SIZE;
#else /* unix: */
	pthread_attr_t attr;
	size_t		   stacksize;
	if (pthread_attr_init (&attr) != 0)
		return;
	if (pthread_attr_getstacksize (&attr, &stacksize) != 0)
		return;
	max_thread_stack_alloc_size = (size_t)CLAMP (0ll, (int64_t)stacksize - THREAD_STACK_RESERVATION, MAX_STACK_ALLOC_SIZE);
	pthread_attr_destroy (&attr);
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
Mem_AllocNonZero
====================
*/
void *Mem_AllocNonZero (const size_t size)
{
#if defined(USE_MI_MALLOC)
	return mi_malloc (size);
#elif defined(USE_SDL_MALLOC)
	return SDL_malloc (size);
#elif defined(USE_CRT_MALLOC)
	return malloc (size);
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
