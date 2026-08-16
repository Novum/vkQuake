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

void  Mem_Init ();
void *Mem_Alloc (const size_t size);
void *Mem_AllocNonZero (const size_t size);
void *Mem_Realloc (void *ptr, const size_t size);
void  Mem_Free (const void *ptr);

// clang-format off

#define SAFE_FREE(ptr)  \
	do                  \
	{                   \
		Mem_Free (ptr); \
		ptr = NULL;     \
	} while (false)
#endif

extern THREAD_LOCAL size_t thread_stack_alloc_size;
extern size_t			   max_thread_stack_alloc_size;

#define TEMP_ALLOC_DECL(type, var)               \
	type	*var = NULL;                         \
	qboolean temp_alloc_##var##_on_heap = false; \
	size_t	 temp_alloc_##var##_size = 0;

#define TEMP_ALLOC_ASSIGN_IMPL(var, size, zeroed, cond)                 \
	do                                                                  \
	{                                                                   \
		temp_alloc_##var##_size = sizeof (*var) * (size);               \
		temp_alloc_##var##_on_heap = false;                             \
		if (cond)                                                       \
		{                                                               \
			if ((thread_stack_alloc_size + temp_alloc_##var##_size) >   \
				max_thread_stack_alloc_size)                            \
			{                                                           \
				if (zeroed)                                             \
					var = Mem_Alloc (temp_alloc_##var##_size);          \
				else                                                    \
					var = Mem_AllocNonZero (temp_alloc_##var##_size);   \
				temp_alloc_##var##_on_heap = true;                      \
			}                                                           \
			else                                                        \
			{                                                           \
				var = alloca (temp_alloc_##var##_size);                 \
				if (zeroed)                                             \
					memset (var, 0, temp_alloc_##var##_size);           \
				thread_stack_alloc_size += temp_alloc_##var##_size;     \
			}                                                           \
		}                                                               \
		else                                                            \
			var = NULL;                                                 \
	} while (false)

#define TEMP_ALLOC_TEMPLATE(type, var, size, zeroed, cond) \
	TEMP_ALLOC_DECL (type, var)                            \
	TEMP_ALLOC_ASSIGN_IMPL (var, size, zeroed, cond)

#define TEMP_ALLOC(type, var, size)                        TEMP_ALLOC_TEMPLATE (type, var, size, false, true)
#define TEMP_ALLOC_ZEROED(type, var, size)                 TEMP_ALLOC_TEMPLATE (type, var, size, true, true)
#define TEMP_ALLOC_COND(type, var, size, cond)             TEMP_ALLOC_TEMPLATE (type, var, size, false, cond)
#define TEMP_ALLOC_ZEROED_COND(type, var, size, cond)      TEMP_ALLOC_TEMPLATE (type, var, size, true, cond)

#define TEMP_ALLOC_ASSIGN(var, size)                       TEMP_ALLOC_ASSIGN_IMPL (var, size, false, true)
#define TEMP_ALLOC_ASSIGN_ZEROED(var, size)                TEMP_ALLOC_ASSIGN_IMPL (var, size, true, true)
#define TEMP_ALLOC_ASSIGN_COND(var, size, cond)            TEMP_ALLOC_ASSIGN_IMPL (var, size, false, cond)
#define TEMP_ALLOC_ASSIGN_ZEROED_COND(var, size, cond)     TEMP_ALLOC_ASSIGN_IMPL (var, size, true, cond)

#define TEMP_FREE(var)                                              \
	do                                                              \
	{                                                               \
		if (var)                                                    \
		{                                                           \
			if (temp_alloc_##var##_on_heap)                         \
				Mem_Free (var);                                     \
			else                                                    \
				thread_stack_alloc_size -= temp_alloc_##var##_size; \
			var = NULL;                                             \
			temp_alloc_##var##_on_heap = false;                     \
			temp_alloc_##var##_size = 0;                            \
		}                                                           \
	} while (false)

// clang-format on
