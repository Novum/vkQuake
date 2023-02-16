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

#include "quakedef.h"
#include "gl_heap.h"

/*
================================================================================

	DEVICE MEMORY HEAP
	Allocator for GPU memory

================================================================================
*/

#define HEAP_PAGE_SIZE 4096

typedef struct glheapblock_s
{
	uint32_t size_in_pages;
	uint32_t prev_block_page_index;
} glheapblock_t;

typedef struct glheapmemory_s
{
	vulkan_memory_t memory;
	glheapblock_t  *pages;
	uint64_t	   *free_blocks_bitfield;
	uint32_t		num_pages;
	uint32_t		num_pages_allocated;
} glheapmemory_t;

typedef struct glheap_s
{
	const char			*name;
	VkDeviceSize		 memory_size;
	uint32_t			 memory_type_index;
	vulkan_memory_type_t memory_type;
	uint32_t			 num_memories;
	glheapmemory_t	   **memories;
} glheap_t;

typedef struct glheapallocation_s
{
	union
	{
		glheapmemory_t	*heap_memory;
		vulkan_memory_t *device_memory;
	};
	VkDeviceSize offset;
	qboolean	 dedicated;
} glheapallocation_t;

#define SET_BIT(bitfield, bitfield_size, index)           \
	do                                                    \
	{                                                     \
		assert (index < bitfield_size);                   \
		bitfield[(index) / 64] |= 1ull << ((index) % 64); \
	} while (false)
#define CLEAR_BIT(bitfield, bitfield_size, index)            \
	do                                                       \
	{                                                        \
		assert (index < bitfield_size);                      \
		bitfield[(index) / 64] &= ~(1ull << ((index) % 64)); \
	} while (false)
#define GET_BIT(bitfield, index) ((bitfield[(index) / 64] & (1ull << ((index) % 64))) != 0)

// #define HEAP_TRACE_LOG
#if defined(HEAP_TRACE_LOG)
#define TRACE_LOG(...) Sys_Printf (__VA_ARGS__)
#else
#define TRACE_LOG(...)
#endif

/*
===============
GL_CreateHeapMemory
===============
*/
glheapmemory_t *
GL_CreateHeapMemory (VkDeviceSize size, uint32_t memory_type_index, vulkan_memory_type_t memory_type, const char *name, atomic_uint32_t *num_allocations)
{
	const VkDeviceSize aligned_size = q_align (size, HEAP_PAGE_SIZE);
	glheapmemory_t	  *heap_memory = (glheapmemory_t *)Mem_Alloc (sizeof (glheapmemory_t));

	ZEROED_STRUCT (VkMemoryAllocateInfo, memory_allocate_info);
	memory_allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	memory_allocate_info.allocationSize = aligned_size;
	memory_allocate_info.memoryTypeIndex = memory_type_index;

	R_AllocateVulkanMemory (&heap_memory->memory, &memory_allocate_info, memory_type, num_allocations);
	GL_SetObjectName ((uint64_t)heap_memory->memory.handle, VK_OBJECT_TYPE_DEVICE_MEMORY, name);

	heap_memory->num_pages = aligned_size / HEAP_PAGE_SIZE;
	heap_memory->pages = Mem_Alloc (heap_memory->num_pages * sizeof (glheapblock_t));
	heap_memory->free_blocks_bitfield = Mem_Alloc ((heap_memory->num_pages / 64) * sizeof (uint64_t));
	heap_memory->free_blocks_bitfield[0] = 0x1;
	heap_memory->num_pages_allocated = 0;
	heap_memory->pages[0].size_in_pages = heap_memory->num_pages;

	return heap_memory;
}

/*
===============
GL_HeapAllocateFromMemory
===============
*/
static qboolean GL_HeapAllocateFromMemory (glheapmemory_t *memory, VkDeviceSize size, VkDeviceSize alignment, VkDeviceSize *aligned_offset)
{
	assert (size > 0);

	const uint32_t alloc_size_in_pages = (size + HEAP_PAGE_SIZE - 1) / HEAP_PAGE_SIZE;
	const uint32_t alignment_in_pages = (alignment + HEAP_PAGE_SIZE - 1) / HEAP_PAGE_SIZE;
	for (uint32_t i = 0; i < memory->num_pages; i += 64)
	{
		uint64_t mask = memory->free_blocks_bitfield[i / 64];
		while (mask != 0)
		{
			const int j = FindFirstBitNonZero64 (mask);
			mask &= ~(1ull << j);

			const uint32_t block_page_index = i + j;
			const uint32_t block_page_index_aligned = q_align (block_page_index, alignment_in_pages);

			const uint32_t alignment_pages = block_page_index_aligned - block_page_index;
			const uint32_t total_pages = alignment_pages + alloc_size_in_pages;

			assert (block_page_index < memory->num_pages);
			glheapblock_t *block = &memory->pages[block_page_index];
			const uint32_t block_size_in_pages = block->size_in_pages;

			if (total_pages <= block_size_in_pages)
			{
				TRACE_LOG ("Allocating block at page %u with size %u pages\n", block_page_index_aligned, alloc_size_in_pages);

				assert (block_page_index_aligned < memory->num_pages);
				glheapblock_t *aligned_block = &memory->pages[block_page_index_aligned];

				if (alignment_pages == 0)
				{
					TRACE_LOG (" No alignment padding\n");
					// No alignment, remove free bit for this block
					CLEAR_BIT (memory->free_blocks_bitfield, memory->num_pages, block_page_index);
				}
				else
				{
					TRACE_LOG (" %u pages alignment padding\n", alignment_pages);
					// Keep free bit, but resize to just leftover alignment pages
					assert (!GET_BIT (memory->free_blocks_bitfield, block_page_index_aligned));
					block->size_in_pages = alignment_pages;
					assert ((block_page_index + block->size_in_pages) <= memory->num_pages);
					aligned_block->prev_block_page_index = block_page_index;
				}

				const uint32_t next_block_page_index = block_page_index_aligned + alloc_size_in_pages;
				if (total_pages < block_size_in_pages)
				{
					// There is leftover space to the right, create a new free block accordingly
					assert (next_block_page_index < memory->num_pages);
					assert (!GET_BIT (memory->free_blocks_bitfield, next_block_page_index));
					SET_BIT (memory->free_blocks_bitfield, memory->num_pages, next_block_page_index);
					glheapblock_t *next_block = &memory->pages[next_block_page_index];
					next_block->size_in_pages = block_size_in_pages - total_pages;
					assert ((next_block_page_index + next_block->size_in_pages) <= memory->num_pages);
					next_block->prev_block_page_index = block_page_index_aligned;
					TRACE_LOG (" Leftover free block at page %u size %u\n", next_block_page_index, next_block->size_in_pages);
					const uint32_t next_next_block_page_index = next_block_page_index + next_block->size_in_pages;
					if (next_next_block_page_index < memory->num_pages)
					{
						glheapblock_t *next_next_block = &memory->pages[next_next_block_page_index];
						next_next_block->prev_block_page_index = next_block_page_index;
					}
				}
				else if (next_block_page_index < memory->num_pages)
				{
					glheapblock_t *next_block = &memory->pages[next_block_page_index];
					next_block->prev_block_page_index = block_page_index_aligned;
				}

				aligned_block->size_in_pages = alloc_size_in_pages;
				memory->num_pages_allocated += alloc_size_in_pages;
				*aligned_offset = (VkDeviceAddress)block_page_index_aligned * HEAP_PAGE_SIZE;
				return true;
			}
			else if (block_size_in_pages > 64)
			{
				// Skip over masks that are covered by the free block
				i = ((block_page_index + block_size_in_pages) & ~0x3F) - 64;
				break;
			}
		}
	}

	return false;
}

/*
===============
GL_HeapFreeFromMemory
===============
*/
static void GL_HeapFreeFromMemory (glheapmemory_t *memory, VkDeviceSize offset)
{
	uint32_t block_page_index = offset / HEAP_PAGE_SIZE;
	assert (!GET_BIT (memory->free_blocks_bitfield, block_page_index));
	glheapblock_t *block = &memory->pages[block_page_index];
	assert (block->size_in_pages > 0);
	memory->num_pages_allocated -= block->size_in_pages;
	TRACE_LOG ("Freeing block at page %u\n", block_page_index);

	if (block_page_index > 0)
	{
		assert (block->prev_block_page_index < block_page_index);
		const qboolean prev_block_free = GET_BIT (memory->free_blocks_bitfield, block->prev_block_page_index);
		if (prev_block_free)
		{
			// Merge with previous free block
			const uint32_t prev_block_page_index = block->prev_block_page_index;
			TRACE_LOG (" Merging with prev free block at %u\n", prev_block_page_index);
			glheapblock_t *prev_block = &memory->pages[prev_block_page_index];
			prev_block->size_in_pages += block->size_in_pages;
			assert ((prev_block_page_index + prev_block->size_in_pages) <= memory->num_pages);
			block_page_index = block->prev_block_page_index;
			memset (block, 0, sizeof (glheapblock_t));
			block = prev_block;
		}
	}

	{
		const uint32_t next_block_page_index = block_page_index + block->size_in_pages;
		if (next_block_page_index < memory->num_pages)
		{
			const qboolean next_block_free = GET_BIT (memory->free_blocks_bitfield, next_block_page_index);
			glheapblock_t *next_block = &memory->pages[next_block_page_index];
			if (next_block_free)
			{
				TRACE_LOG (" Merging with next free block at %u\n", next_block_page_index);
				// Merge with next free block
				CLEAR_BIT (memory->free_blocks_bitfield, memory->num_pages, next_block_page_index);
				block->size_in_pages += next_block->size_in_pages;
				assert ((block_page_index + block->size_in_pages) <= memory->num_pages);
				memset (next_block, 0, sizeof (glheapblock_t));
			}
		}
	}

	{
		const uint32_t next_block_page_index = block_page_index + block->size_in_pages;
		if (next_block_page_index < memory->num_pages)
		{
			glheapblock_t *next_block = &memory->pages[next_block_page_index];
			next_block->prev_block_page_index = block_page_index;
		}
	}

	TRACE_LOG (" Free block at %u size %u pages\n", block_page_index, block->size_in_pages);
	assert (block_page_index < memory->num_pages);
	SET_BIT (memory->free_blocks_bitfield, memory->num_pages, block_page_index);
}

/*
===============
GL_HeapCreate
===============
*/
glheap_t *GL_HeapCreate (VkDeviceSize memory_size, uint32_t memory_type_index, vulkan_memory_type_t memory_type, const char *heap_name)
{
	assert(memory_size > HEAP_PAGE_SIZE);
	assert((memory_size % HEAP_PAGE_SIZE) == 0);
	glheap_t *heap = Mem_Alloc (sizeof (glheap_t));
	heap->memory_size = memory_size;
	heap->memory_type_index = memory_type_index;
	heap->memory_type = memory_type;
	heap->name = heap_name;
	return heap;
}

/*
===============
GL_HeapDestroy
===============
*/
void GL_HeapDestroy (glheap_t *heap, atomic_uint32_t *num_allocations)
{
	for (uint32_t i = 0; i < heap->num_memories; ++i)
	{
		glheapmemory_t *memory = heap->memories[i];
		R_FreeVulkanMemory (&memory->memory, num_allocations);
		Mem_Free (memory->pages);
		Mem_Free (memory->free_blocks_bitfield);
	}
	Mem_Free (heap->memories);
}

/*
===============
GL_HeapAllocate
===============
*/
glheapallocation_t *GL_HeapAllocate (glheap_t *heap, VkDeviceSize size, VkDeviceSize alignment, atomic_uint32_t *num_allocations)
{
	glheapallocation_t *allocation = Mem_Alloc (sizeof (glheapallocation_t));

	if (size < heap->memory_size)
	{
		for (uint32_t i = 0; i < heap->num_memories; ++i)
		{
			VkDeviceSize   aligned_offset;
			const qboolean success = GL_HeapAllocateFromMemory (heap->memories[i], size, alignment, &aligned_offset);
			if (success)
			{
				allocation->heap_memory = heap->memories[i];
				allocation->offset = aligned_offset;
				return allocation;
			}
		}

		const uint32_t num_memories = heap->num_memories;
		heap->memories = Mem_Realloc (heap->memories, sizeof (glheapmemory_t *) * (num_memories + 1));
		heap->memories[num_memories] = GL_CreateHeapMemory (heap->memory_size, heap->memory_type_index, heap->memory_type, heap->name, num_allocations);
		VkDeviceSize   aligned_offset;
		const qboolean success = GL_HeapAllocateFromMemory (heap->memories[num_memories], size, alignment, &aligned_offset);
		assert (success);
		if (!success)
			return NULL;
		allocation->heap_memory = heap->memories[num_memories];
		allocation->offset = aligned_offset;
		++heap->num_memories;
	}
	else
	{
		allocation->device_memory = Mem_Alloc (sizeof (vulkan_memory_t));
		allocation->dedicated = true;

		ZEROED_STRUCT (VkMemoryAllocateInfo, memory_allocate_info);
		memory_allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		memory_allocate_info.allocationSize = size;
		memory_allocate_info.memoryTypeIndex = heap->memory_type_index;

		R_AllocateVulkanMemory (allocation->device_memory, &memory_allocate_info, heap->memory_type, num_allocations);
		GL_SetObjectName ((uint64_t)allocation->device_memory->handle, VK_OBJECT_TYPE_DEVICE_MEMORY, heap->name);
	}

	return allocation;
}

/*
===============
GL_HeapFree
===============
*/
void GL_HeapFree (glheap_t *heap, glheapallocation_t *allocation, atomic_uint32_t *num_allocations)
{
	if (!allocation->dedicated)
	{
		GL_HeapFreeFromMemory (allocation->heap_memory, allocation->offset);
	}
	else
	{
		R_FreeVulkanMemory (allocation->device_memory, num_allocations);
		Mem_Free (allocation->device_memory);
	}
	Mem_Free (allocation);
}

/*
===============
GL_HeapGetAllocationMemory
===============
*/
VkDeviceMemory GL_HeapGetAllocationMemory (glheapallocation_t *allocation)
{
	return allocation->dedicated ? allocation->device_memory->handle : allocation->heap_memory->memory.handle;
}

/*
===============
GL_HeapGetAllocationOffset
===============
*/
VkDeviceSize GL_HeapGetAllocationOffset (glheapallocation_t *allocation)
{
	return allocation->offset;
}

#ifdef _DEBUG
/*
=================
HEAP_TEST_ASSERT
=================
*/
#define HEAP_TEST_ASSERT(cond, what) \
	if (!(cond))                     \
	{                                \
		TRACE_LOG ("%s\n", what);    \
		abort ();                    \
	}

/*
=================
TestHeapCleanState
=================
*/
static void TestHeapCleanState (glheap_t *heap)
{
	ZEROED_STRUCT (glheapblock_t, empty_block);
	for (uint32_t i = 0; i < heap->num_memories; ++i)
	{
		glheapmemory_t *memory = heap->memories[i];
		HEAP_TEST_ASSERT (memory->num_pages_allocated == 0, "num_pages_allocated needs to be 0");
		HEAP_TEST_ASSERT (memory->pages[0].size_in_pages = memory->num_pages, "Empty heap first block needs to fill all pages");
		HEAP_TEST_ASSERT (memory->free_blocks_bitfield[0] == 1, "first bitfield bit needs to be 1");
		for (uint32_t j = 1; j < memory->num_pages; ++j)
			HEAP_TEST_ASSERT (memcmp (&memory->pages[j], &empty_block, sizeof (glheapblock_t)) == 0, "Page block header zeroed");
		for (uint32_t j = 1; j < (memory->num_pages / HEAP_PAGE_SIZE); ++j)
			HEAP_TEST_ASSERT (memory->free_blocks_bitfield[j] == 0, "bitfield is not 0");
	}
}

/*
=================
TestHeapConsistency
=================
*/
static void TestHeapConsistency (glheap_t *heap)
{

	for (uint32_t i = 0; i < heap->num_memories; ++i)
	{
		uint32_t		current_block_index = 0;
		uint32_t		prev_block = 0;
		qboolean		prev_block_free = false;
		uint32_t		num_allocated_pages = 0;
		glheapmemory_t *memory = heap->memories[i];
		while (current_block_index < memory->num_pages)
		{
			glheapblock_t *block = &memory->pages[current_block_index];
			const qboolean block_free = GET_BIT (memory->free_blocks_bitfield, current_block_index);
			if (current_block_index > 0)
			{
				HEAP_TEST_ASSERT (block->prev_block_page_index == prev_block, "Invalid prev block");
				if (prev_block_free)
					HEAP_TEST_ASSERT (!block_free, "Found two consecutive free blocks");
			}
			prev_block = current_block_index;
			for (uint32_t j = 1; j < block->size_in_pages; ++j)
				HEAP_TEST_ASSERT (!GET_BIT (memory->free_blocks_bitfield, current_block_index + j), "Free bit set for non block page");
			if (!block_free)
				num_allocated_pages += block->size_in_pages;
			prev_block_free = block_free;
			current_block_index += block->size_in_pages;
		}
		HEAP_TEST_ASSERT (current_block_index == memory->num_pages, "Blocks need to add up to num pages");
		HEAP_TEST_ASSERT (num_allocated_pages == memory->num_pages_allocated, "Invalid number of allocated pages found");
	}
}

/*
=================
GL_HeapTest_f
=================
*/
void GL_HeapTest_f (void)
{
	const VkDeviceSize TEST_HEAP_SIZE = 1ull * 1024ull * 1024ull;
	const int		   NUM_ITERATIONS = 20;
	const int		   NUM_ALLOCS_PER_ITERATION = 500;
	const int		   MAX_ALLOC_SIZE = 64ull * 1024ull;
	const VkDeviceSize ALIGNMENTS[] = {
		1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384,
	};
	const int NUM_ALIGNMENTS = countof (ALIGNMENTS);

	atomic_uint32_t num_allocations = {0};
	glheap_t	   *test_heap = GL_HeapCreate (TEST_HEAP_SIZE, 0, VULKAN_MEMORY_TYPE_NONE, "Test Heap");
	TestHeapCleanState (test_heap);
	srand (0);
	TEMP_ALLOC_ZEROED (glheapallocation_t *, allocations, NUM_ALLOCS_PER_ITERATION);
	for (int j = 0; j < NUM_ITERATIONS; ++j)
	{
		const int STRIDE = 3;
		for (int k = 0; k <= STRIDE; ++k)
		{
			if (k < STRIDE)
			{
				for (int i = k; i < NUM_ALLOCS_PER_ITERATION; i += STRIDE)
				{
					const VkDeviceSize size = (rand () + 1) % MAX_ALLOC_SIZE;
					const VkDeviceSize alignment = ALIGNMENTS[rand () % NUM_ALIGNMENTS];
					HEAP_TEST_ASSERT (allocations[i] == NULL, "allocation is not NULL");

					allocations[i] = GL_HeapAllocate (test_heap, size, alignment, &num_allocations);
					HEAP_TEST_ASSERT ((GL_HeapGetAllocationOffset (allocations[i]) % alignment) == 0, "wrong alignment");
					TestHeapConsistency (test_heap);
				}
			}
			if (k > 0)
			{
				for (int i = k - 1; i < NUM_ALLOCS_PER_ITERATION; i += STRIDE)
				{
					HEAP_TEST_ASSERT (allocations[i] != NULL, "allocation is NULL");
					GL_HeapFree (test_heap, allocations[i], &num_allocations);
					allocations[i] = NULL;
					TestHeapConsistency (test_heap);
				}
			}
		}
		for (int i = 0; i < NUM_ALLOCS_PER_ITERATION; ++i)
			HEAP_TEST_ASSERT (allocations[i] == NULL, "allocation is not NULL");
		TestHeapCleanState (test_heap);
	}
	TEMP_FREE (allocations);
	{
		glheapallocation_t *large_alloc = GL_HeapAllocate (test_heap, TEST_HEAP_SIZE * 2, 1, &num_allocations);
		TestHeapConsistency (test_heap);
		GL_HeapFree (test_heap, large_alloc, &num_allocations);
		TestHeapConsistency (test_heap);
		TestHeapCleanState (test_heap);
	}
	GL_HeapDestroy (test_heap, &num_allocations);
}
#endif
