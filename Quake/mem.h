/*
 * mem.h -- memory allocator
 *
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

#ifndef __MEM_H
#define __MEM_H

#include "quakedef.h"

// Mem_Alloc will always return zero initialized memory
// A lot of old code was assuming this and overhead is negligible

void *Mem_Alloc (const size_t size);
void *Mem_Realloc (void *ptr, const size_t size);
void  Mem_Free (const void *ptr);

#define SAFE_FREE(ptr)  \
	do                  \
	{                   \
		Mem_Free (ptr); \
		ptr = NULL;     \
	} while (false)
#endif

#define MAX_THREAD_STACK_ALLOC_SIZE (320 * 1024)
extern THREAD_LOCAL int thread_stack_alloc_size;

#define TEMP_ALLOC(type, var, size)                                                        \
	qboolean     temp_alloc_##var##_on_heap = false;                                       \
	const size_t temp_alloc_##var##_size = sizeof (type) * (size);                         \
	if ((thread_stack_alloc_size + temp_alloc_##var##_size) > MAX_THREAD_STACK_ALLOC_SIZE) \
	{                                                                                      \
		var = (type *)Mem_Alloc (temp_alloc_##var##_size);                                 \
		temp_alloc_##var##_on_heap = true;                                                 \
	}                                                                                      \
	else                                                                                   \
	{                                                                                      \
		var = (type *)alloca (temp_alloc_##var##_size);                                    \
		thread_stack_alloc_size += temp_alloc_##var##_size;                                \
	}
#define TEMP_FREE(var)                                      \
	if (temp_alloc_##var##_on_heap)                         \
	{                                                       \
		Mem_Free (var);                                     \
	}                                                       \
	else                                                    \
	{                                                       \
		thread_stack_alloc_size -= temp_alloc_##var##_size; \
	}
