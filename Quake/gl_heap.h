/*
Copyright (C) 2016 Axel Gneiting

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

#ifndef __HEAP__
#define __HEAP__

typedef struct glheap_s			  glheap_t;
typedef struct glheapallocation_s glheapallocation_t;

typedef struct glheapstats_s
{
	uint32_t num_segments;
	uint32_t num_allocations;
	uint32_t num_small_allocations;
	uint32_t num_block_allocations;
	uint32_t num_dedicated_allocations;
	uint32_t num_blocks_used;
	uint32_t num_blocks_free;
	uint32_t num_pages_allocated;
	uint32_t num_pages_free;
	uint64_t num_bytes_allocated;
	uint64_t num_bytes_free;
	uint64_t num_bytes_wasted;
} glheapstats_t;

glheap_t *GL_HeapCreate (VkDeviceSize segment_size, uint32_t page_size, uint32_t memory_type_index, vulkan_memory_type_t memory_type, const char *heap_name);
void	  GL_HeapDestroy (glheap_t *heap, atomic_uint32_t *num_allocations);
glheapallocation_t *GL_HeapAllocate (glheap_t *heap, VkDeviceSize size, VkDeviceSize alignment, atomic_uint32_t *num_allocations);
void				GL_HeapFree (glheap_t *heap, glheapallocation_t *allocation, atomic_uint32_t *num_allocations);
VkDeviceMemory		GL_HeapGetAllocationMemory (glheapallocation_t *allocation);
VkDeviceSize		GL_HeapGetAllocationOffset (glheapallocation_t *allocation);
glheapstats_t	   *GL_HeapGetStats (glheap_t *heap);

#ifdef _DEBUG
void GL_HeapTest_f (void);
#endif

#endif
