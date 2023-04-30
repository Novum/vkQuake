/*
Copyright (C) 2023 Axel Gneiting

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

#define NUM_SMALL_ALLOC_SIZES  6 // 64 bit mask
#define NUM_BLOCK_SIZE_CLASSES 8
#define MAX_PAGES			   (UINT16_MAX - 1)
#define INVALID_PAGE_INDEX	   UINT16_MAX

typedef uint16_t page_index_t;

static uint32_t SMALL_SLOTS_PER_PAGE[NUM_SMALL_ALLOC_SIZES] = {
	64, 32, 16, 8, 4, 2,
};

static uint64_t SLOTS_FULL_MASK[NUM_SMALL_ALLOC_SIZES] = {
	0xFFFFFFFFFFFFFFFFull, 0xFFFFFFFFull, 0xFFFFull, 0xFFull, 0xFull, 0x3ull,
};

typedef struct glheappagehdr_s
{
	page_index_t size_in_pages;
	page_index_t prev_block_page_index;
} glheappagehdr_t;

typedef struct glheapsmallalloclinks_s
{
	page_index_t prev_small_alloc_page;
	page_index_t next_small_alloc_page;
} glheapsmallalloclinks_t;

static const glheappagehdr_t EMPTY_PAGE_HDR = {
	0,
	0,
};

static const glheapsmallalloclinks_t EMPTY_SMALL_ALLOC_LINKS = {
	INVALID_PAGE_INDEX,
	INVALID_PAGE_INDEX,
};

typedef struct glheapsegment_s
{
	vulkan_memory_t			 memory;
	glheappagehdr_t			*page_hdrs;
	glheapsmallalloclinks_t *small_alloc_links;
	uint64_t				*small_alloc_masks;
	uint64_t				*free_blocks_bitfields[NUM_BLOCK_SIZE_CLASSES];
	uint64_t				*free_blocks_skip_bitfields[NUM_BLOCK_SIZE_CLASSES];
	page_index_t			 small_alloc_free_list_heads[NUM_SMALL_ALLOC_SIZES];
	page_index_t			 num_pages_allocated;
} glheapsegment_t;

typedef struct glheap_s
{
	const char			*name;
	VkDeviceSize		 segment_size;
	uint32_t			 page_size;
	uint32_t			 page_size_shift;
	uint32_t			 min_small_alloc_size;
	uint32_t			 small_alloc_shift;
	uint32_t			 memory_type_index;
	vulkan_memory_type_t memory_type;
	uint32_t			 num_segments;
	page_index_t		 num_pages_per_segment;
	page_index_t		 num_masks_per_segment;
	glheapsegment_t	   **segments;
	uint64_t			 dedicated_alloc_bytes;
	glheapstats_t		 stats;
} glheap_t;

typedef struct allocinfo_s
{
	qboolean	 is_small_alloc;
	int			 small_alloc_size;
	int			 small_alloc_bucket;
	page_index_t alloc_size_in_pages;
	page_index_t alignment_in_pages;
	int			 size_class;
} allocinfo_t;

static allocinfo_t ONE_PAGE_ALLOC_INFO = {
	.alloc_size_in_pages = 1,
	.alignment_in_pages = 1,
	.size_class = 0,
};

typedef enum
{
	ALLOC_TYPE_NONE,
	ALLOC_TYPE_PAGES,
	ALLOC_TYPE_DEDICATED,
	ALLOC_TYPE_SMALL_ALLOC,
} alloc_type_t;

typedef struct glheapallocation_s
{
	union
	{
		glheapsegment_t *segment;
		vulkan_memory_t *memory;
	};
	VkDeviceSize size;
	VkDeviceSize offset;
	alloc_type_t alloc_type;
#ifndef NDEBUG
	uint32_t small_alloc_slot;
	uint32_t small_alloc_size;
#endif
} glheapallocation_t;

#define SET_BIT(bitfield, index)   bitfield[(index) / 64] |= 1ull << ((index) % 64)
#define CLEAR_BIT(bitfield, index) bitfield[(index) / 64] &= ~(1ull << ((index) % 64))
#define GET_BIT(bitfield, index)   ((bitfield[(index) / 64] & (1ull << ((index) % 64))) != 0)

// #define HEAP_TRACE_LOG
#if defined(HEAP_TRACE_LOG)
#define TRACE_LOG(...) Sys_Printf (__VA_ARGS__)
#else
#define TRACE_LOG(...)
#endif

/*
===============
GL_HeapMarkBlockFree
===============
*/
static void GL_HeapMarkBlockFree (glheap_t *heap, glheapsegment_t *segment, page_index_t size_in_pages, uint32_t block_page_index)
{
	const int size_class = q_min (Q_log2 (size_in_pages), NUM_BLOCK_SIZE_CLASSES - 1);
	for (int i = 0; i <= size_class; ++i)
	{
		SET_BIT (segment->free_blocks_bitfields[i], block_page_index);
		SET_BIT (segment->free_blocks_skip_bitfields[i], block_page_index / 64);
	}
}

/*
===============
GL_HeapMarkBlockUsed
===============
*/
static void GL_HeapMarkBlockUsed (glheap_t *heap, glheapsegment_t *segment, uint32_t block_page_index)
{
	for (int i = 0; i < NUM_BLOCK_SIZE_CLASSES; ++i)
	{
		CLEAR_BIT (segment->free_blocks_bitfields[i], block_page_index);
		if (segment->free_blocks_bitfields[i][block_page_index / 64] == 0ull)
			CLEAR_BIT (segment->free_blocks_skip_bitfields[i], block_page_index / 64);
	}
}

/*
===============
GL_HeapIsBlockFree
===============
*/
static qboolean GL_HeapIsBlockFree (glheap_t *heap, glheapsegment_t *segment, uint32_t block_page_index)
{
	assert (block_page_index < heap->num_pages_per_segment);
	return GET_BIT (segment->free_blocks_bitfields[0], block_page_index);
}

/*
===============
GL_CreateHeapSegment
===============
*/
static glheapsegment_t *GL_CreateHeapSegment (glheap_t *heap, atomic_uint32_t *num_allocations)
{
	glheapsegment_t *segment = (glheapsegment_t *)Mem_Alloc (sizeof (glheapsegment_t));

	ZEROED_STRUCT (VkMemoryAllocateInfo, memory_allocate_info);
	memory_allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	memory_allocate_info.allocationSize = heap->segment_size;
	memory_allocate_info.memoryTypeIndex = heap->memory_type_index;

	R_AllocateVulkanMemory (&segment->memory, &memory_allocate_info, heap->memory_type, num_allocations);
	if (segment->memory.handle != VK_NULL_HANDLE)
		GL_SetObjectName ((uint64_t)segment->memory.handle, VK_OBJECT_TYPE_DEVICE_MEMORY, heap->name);

	segment->page_hdrs = Mem_Alloc (heap->num_pages_per_segment * sizeof (glheappagehdr_t));
	segment->small_alloc_links = Mem_Alloc (heap->num_pages_per_segment * sizeof (glheapsmallalloclinks_t));
	segment->small_alloc_masks = Mem_Alloc (heap->num_pages_per_segment * sizeof (uint64_t));
	for (int i = 0; i < NUM_BLOCK_SIZE_CLASSES; ++i)
	{
		segment->free_blocks_bitfields[i] = Mem_Alloc (((heap->num_pages_per_segment + 63) / 64) * sizeof (uint64_t));
		segment->free_blocks_skip_bitfields[i] = Mem_Alloc (((heap->num_pages_per_segment + 4095) / 4096) * sizeof (uint64_t));
		segment->free_blocks_bitfields[i][0] = 0x1;
		segment->free_blocks_skip_bitfields[i][0] = 0x1;
	}
	segment->num_pages_allocated = 0;
	for (int i = 0; i < heap->num_pages_per_segment; ++i)
		segment->small_alloc_links[i] = EMPTY_SMALL_ALLOC_LINKS;
	segment->page_hdrs[0].size_in_pages = heap->num_pages_per_segment;
	for (int i = 0; i < NUM_SMALL_ALLOC_SIZES; ++i)
		segment->small_alloc_free_list_heads[i] = INVALID_PAGE_INDEX;

	return segment;
}

/*
===============
GL_HeapAllocateBlockFromSegment
===============
*/
static qboolean GL_HeapAllocateBlockFromSegment (glheap_t *heap, glheapsegment_t *segment, allocinfo_t *alloc_info, page_index_t *page_index)
{
	if (alloc_info->alloc_size_in_pages > (heap->num_pages_per_segment - segment->num_pages_allocated))
		return false;

	for (page_index_t mask_offset = 0; mask_offset < heap->num_masks_per_segment; mask_offset += 64)
	{
		uint64_t skip_mask = segment->free_blocks_skip_bitfields[alloc_info->size_class][mask_offset / 64];
		while (skip_mask != 0)
		{
			const int mask_index = FindFirstBitNonZero64 (skip_mask);
			skip_mask &= ~(1ull << mask_index);

			uint64_t mask = segment->free_blocks_bitfields[alloc_info->size_class][mask_offset + mask_index];
			while (mask != 0)
			{
				const int mask_page_index = FindFirstBitNonZero64 (mask);
				mask &= ~(1ull << mask_page_index);

				const uint16_t block_page_index = ((mask_offset + mask_index) * 64) + mask_page_index;
				const uint16_t block_page_index_aligned = q_align (block_page_index, alloc_info->alignment_in_pages);

				const uint32_t alignment_pages = block_page_index_aligned - block_page_index;
				const uint32_t total_pages = alignment_pages + alloc_info->alloc_size_in_pages;

				assert (block_page_index < heap->num_pages_per_segment);
				glheappagehdr_t *block_page_hdr = &segment->page_hdrs[block_page_index];
				const uint32_t	 block_size_in_pages = block_page_hdr->size_in_pages;

				if (total_pages <= block_size_in_pages)
				{
					TRACE_LOG ("Allocating block at page %u with size %u pages\n", block_page_index_aligned, alloc_size_in_pages);

					assert (block_page_index_aligned < heap->num_pages_per_segment);
					glheappagehdr_t *aligned_block_page_hdr = &segment->page_hdrs[block_page_index_aligned];

					if (alignment_pages == 0)
					{
						TRACE_LOG (" No alignment padding\n");
						// No alignment, remove free bits for this block_page_hdr
						--heap->stats.num_blocks_free;
						GL_HeapMarkBlockUsed (heap, segment, block_page_index);
					}
					else
					{
						TRACE_LOG (" %u pages alignment padding\n", alignment_pages);
						// Keep free bit, but resize to just leftover alignment page_hdrs
						for (int k = 0; k < NUM_BLOCK_SIZE_CLASSES; ++k)
							assert (!GET_BIT (segment->free_blocks_bitfields[k], block_page_index_aligned));
						block_page_hdr->size_in_pages = alignment_pages;
						assert ((block_page_index + block_page_hdr->size_in_pages) <= heap->num_pages_per_segment);
						aligned_block_page_hdr->prev_block_page_index = block_page_index;
					}

					const page_index_t next_block_page_index = block_page_index_aligned + alloc_info->alloc_size_in_pages;
					if (total_pages < block_size_in_pages)
					{
						// There is leftover space to the right, create a new free block_page_hdr accordingly
						assert (next_block_page_index < heap->num_pages_per_segment);
						glheappagehdr_t *next_block_page_hdr = &segment->page_hdrs[next_block_page_index];
						next_block_page_hdr->size_in_pages = block_size_in_pages - total_pages;

						++heap->stats.num_blocks_free;
						GL_HeapMarkBlockFree (heap, segment, next_block_page_hdr->size_in_pages, next_block_page_index);

						assert ((next_block_page_index + next_block_page_hdr->size_in_pages) <= heap->num_pages_per_segment);
						next_block_page_hdr->prev_block_page_index = block_page_index_aligned;
						TRACE_LOG (" Leftover free block at page %u size %u\n", next_block_page_index, next_block_page_hdr->size_in_pages);

						const uint32_t nn_block_page_index = next_block_page_index + next_block_page_hdr->size_in_pages;
						if (nn_block_page_index < heap->num_pages_per_segment)
						{
							glheappagehdr_t *nn_block_page_hdr = &segment->page_hdrs[nn_block_page_index];
							nn_block_page_hdr->prev_block_page_index = next_block_page_index;
						}
					}
					else if (next_block_page_index < heap->num_pages_per_segment)
					{
						glheappagehdr_t *next_block_page_hdr = &segment->page_hdrs[next_block_page_index];
						next_block_page_hdr->prev_block_page_index = block_page_index_aligned;
					}

					assert (segment->small_alloc_links[block_page_index_aligned].prev_small_alloc_page == INVALID_PAGE_INDEX);
					assert (segment->small_alloc_links[block_page_index_aligned].next_small_alloc_page == INVALID_PAGE_INDEX);
					assert (segment->small_alloc_masks[block_page_index_aligned] == 0ull);

					aligned_block_page_hdr->size_in_pages = alloc_info->alloc_size_in_pages;
					segment->num_pages_allocated += alloc_info->alloc_size_in_pages;
					*page_index = block_page_index_aligned;
					++heap->stats.num_blocks_used;
					return true;
				}
			}
		}
	}

	TRACE_LOG (" Failed to allocate block with size %u pages\n", alloc_size_in_pages);
	return false;
}

/*
===============
GL_HeapAddPageToSmallFreeList
===============
*/
static void GL_HeapAddPageToSmallFreeList (glheapsegment_t *segment, const page_index_t page_index, const int small_alloc_bucket)
{
	TRACE_LOG (" Adding page %u to bucket %u free list\n", page_index, small_alloc_bucket);

	// This page needs to be unlinked at this point in time
	assert (segment->small_alloc_links[page_index].prev_small_alloc_page == INVALID_PAGE_INDEX);
	assert (segment->small_alloc_links[page_index].next_small_alloc_page == INVALID_PAGE_INDEX);

	const page_index_t prev_head_index = segment->small_alloc_free_list_heads[small_alloc_bucket];
	if (prev_head_index != INVALID_PAGE_INDEX)
	{
		segment->small_alloc_links[prev_head_index].prev_small_alloc_page = page_index;
		segment->small_alloc_links[page_index].next_small_alloc_page = prev_head_index;
	}
	segment->small_alloc_free_list_heads[small_alloc_bucket] = page_index;
}

/*
===============
GL_HeapRemovePageFromSmallFreeList
===============
*/
static void GL_HeapRemovePageFromSmallFreeList (glheapsegment_t *segment, const page_index_t page_index, const int small_alloc_bucket)
{
	TRACE_LOG (" Removing page %u from bucket %u free list\n", page_index, small_alloc_bucket);
	glheapsmallalloclinks_t *page_links = &segment->small_alloc_links[page_index];
	const page_index_t		 prev_page_index = page_links->prev_small_alloc_page;
	const page_index_t		 next_page_index = page_links->next_small_alloc_page;

	if (prev_page_index != INVALID_PAGE_INDEX)
	{
		assert (segment->small_alloc_links[prev_page_index].next_small_alloc_page == page_index);
		segment->small_alloc_links[prev_page_index].next_small_alloc_page = next_page_index;
	}
	if (next_page_index != INVALID_PAGE_INDEX)
	{
		assert (segment->small_alloc_links[next_page_index].prev_small_alloc_page == page_index);
		segment->small_alloc_links[next_page_index].prev_small_alloc_page = prev_page_index;
	}

	page_links->prev_small_alloc_page = INVALID_PAGE_INDEX;
	page_links->next_small_alloc_page = INVALID_PAGE_INDEX;

	// If this page was the head replace it with next page in linked list
	if (segment->small_alloc_free_list_heads[small_alloc_bucket] == page_index)
		segment->small_alloc_free_list_heads[small_alloc_bucket] = next_page_index;
}

/*
===============
GL_HeapSmallAllocateFromBlock
===============
*/
static void GL_HeapSmallAllocateFromBlock (
	glheapallocation_t *allocation, glheap_t *heap, glheapsegment_t *segment, uint32_t block_page_index, const int small_alloc_size,
	const int small_alloc_bucket)
{
	uint64_t *small_alloc_mask = &segment->small_alloc_masks[block_page_index];
	if (*small_alloc_mask == 0ull)
	{
		// New page, add to free list
		GL_HeapAddPageToSmallFreeList (segment, block_page_index, small_alloc_bucket);
	}

	const uint32_t slot_index = FindFirstBitNonZero (~(*small_alloc_mask));
	TRACE_LOG (" Allocated slot %d from page %u\n", slot_index, block_page_index);
	*small_alloc_mask |= 1ull << slot_index;

#ifndef NDEBUG
	const uint32_t num_slots = SMALL_SLOTS_PER_PAGE[small_alloc_bucket];
	assert ((num_slots * small_alloc_size) <= heap->page_size);
	assert (slot_index < num_slots);
#endif

	if (*small_alloc_mask == SLOTS_FULL_MASK[small_alloc_bucket])
	{
		// Page is full, remove from free list
		GL_HeapRemovePageFromSmallFreeList (segment, block_page_index, small_alloc_bucket);
	}

	allocation->alloc_type = ALLOC_TYPE_SMALL_ALLOC + small_alloc_bucket;
	allocation->offset = (block_page_index * heap->page_size) + (slot_index * small_alloc_size);
#ifndef NDEBUG
	allocation->small_alloc_slot = slot_index;
	allocation->small_alloc_size = small_alloc_size;
#endif
}

/*
===============
GL_HeapSmallFreeFromBlock
===============
*/
static qboolean GL_HeapSmallFreeFromBlock (glheap_t *heap, glheapsegment_t *segment, glheapallocation_t *allocation)
{
	const uint32_t block_page_index = allocation->offset >> heap->page_size_shift;
	const uint32_t offset_in_page = allocation->offset & (heap->page_size - 1);
	const uint32_t small_alloc_bucket = allocation->alloc_type - ALLOC_TYPE_SMALL_ALLOC;
	const uint32_t small_alloc_size_shift = heap->small_alloc_shift + small_alloc_bucket;
	const uint32_t slot_index = offset_in_page >> small_alloc_size_shift;

#ifndef NDEBUG
	const uint32_t num_slots = SMALL_SLOTS_PER_PAGE[small_alloc_bucket];
	assert (offset_in_page < heap->page_size);
	assert (slot_index < num_slots);
	assert (slot_index == allocation->small_alloc_slot);
	assert ((heap->page_size / SMALL_SLOTS_PER_PAGE[small_alloc_bucket]) == allocation->small_alloc_size);
#endif

	TRACE_LOG (" Free slot %d from page %u\n", slot_index, block_page_index);
	uint64_t *small_alloc_mask = &segment->small_alloc_masks[block_page_index];
	if (*small_alloc_mask == SLOTS_FULL_MASK[small_alloc_bucket])
	{
		// Page was full, add to free list
		GL_HeapAddPageToSmallFreeList (segment, block_page_index, small_alloc_bucket);
	}

	assert ((*small_alloc_mask & (1ull << slot_index)) != 0);
	*small_alloc_mask &= ~(1ull << slot_index);

	qboolean page_empty = *small_alloc_mask == 0ull;
	if (page_empty)
	{
		// Page is now empty, remove from free list
		GL_HeapRemovePageFromSmallFreeList (segment, block_page_index, small_alloc_bucket);
	}

	return page_empty;
}

/*
===============
GL_HeapAllocateFromSegment
===============
*/
static qboolean GL_HeapAllocateFromSegment (glheapallocation_t *allocation, glheap_t *heap, glheapsegment_t *segment, allocinfo_t *alloc_info)
{
	if (alloc_info->is_small_alloc)
	{
		TRACE_LOG ("Small alloc size %u bucket %u\n", small_alloc_size, small_alloc_bucket);
		assert (alloc_info->small_alloc_bucket < NUM_SMALL_ALLOC_SIZES);
		page_index_t page_index = segment->small_alloc_free_list_heads[alloc_info->small_alloc_bucket];
		if (page_index != INVALID_PAGE_INDEX)
		{
			// If there is a page_index we know it is pointing to a page with at least one small alloc slot free of this size
			GL_HeapSmallAllocateFromBlock (allocation, heap, segment, page_index, alloc_info->small_alloc_size, alloc_info->small_alloc_bucket);
		}
		else
		{
			// If we hit this there are no pages with buckets for this size. Create a new page & set it as the first small alloc page.
			if (!GL_HeapAllocateBlockFromSegment (heap, segment, &ONE_PAGE_ALLOC_INFO, &page_index))
				return false;
			GL_HeapSmallAllocateFromBlock (allocation, heap, segment, page_index, alloc_info->small_alloc_size, alloc_info->small_alloc_bucket);
		}

		allocation->segment = segment;
		return true;
	}
	else
	{
		page_index_t page_index;
		if (GL_HeapAllocateBlockFromSegment (heap, segment, alloc_info, &page_index))
		{
			allocation->segment = segment;
			allocation->offset = page_index * heap->page_size;
			allocation->alloc_type = ALLOC_TYPE_PAGES;
			return true;
		}
	}

	return false;
}

/*
===============
GL_HeapFreeBlockFromSegment
===============
*/
static void GL_HeapFreeBlockFromSegment (glheap_t *heap, glheapsegment_t *segment, uint32_t page_size_shift, VkDeviceSize offset)
{
	page_index_t block_page_index = offset >> page_size_shift;
	TRACE_LOG ("Freeing block at page %u\n", block_page_index);
	for (int mask_page_offset = 0; mask_page_offset < NUM_BLOCK_SIZE_CLASSES; ++mask_page_offset)
		assert (!GET_BIT (segment->free_blocks_bitfields[mask_page_offset], block_page_index));
	glheappagehdr_t *block_page_hdr = &segment->page_hdrs[block_page_index];
	assert (block_page_hdr->size_in_pages > 0);
	segment->num_pages_allocated -= block_page_hdr->size_in_pages;
	--heap->stats.num_blocks_used;

	if (block_page_index > 0)
	{
		assert (block_page_hdr->prev_block_page_index < block_page_index);
		const qboolean prev_block_free = GL_HeapIsBlockFree (heap, segment, block_page_hdr->prev_block_page_index);
		if (prev_block_free)
		{
			// Merge with previous free block_page_hdr
			const page_index_t prev_block_page_index = block_page_hdr->prev_block_page_index;
			TRACE_LOG (" Merging with prev free block at %u\n", prev_block_page_index);
			glheappagehdr_t *prev_block_page_hdr = &segment->page_hdrs[prev_block_page_index];
			prev_block_page_hdr->size_in_pages += block_page_hdr->size_in_pages;
			assert ((prev_block_page_index + prev_block_page_hdr->size_in_pages) <= heap->num_pages_per_segment);
			block_page_index = block_page_hdr->prev_block_page_index;
			*block_page_hdr = EMPTY_PAGE_HDR;
			block_page_hdr = prev_block_page_hdr;
		}
	}

	{
		const page_index_t next_block_page_index = block_page_index + block_page_hdr->size_in_pages;
		if (next_block_page_index < heap->num_pages_per_segment)
		{
			const qboolean	 next_block_free = GL_HeapIsBlockFree (heap, segment, next_block_page_index);
			glheappagehdr_t *next_block_page_hdr = &segment->page_hdrs[next_block_page_index];
			if (next_block_free)
			{
				TRACE_LOG (" Merging with next free block at %u\n", next_block_page_index);
				// Merge with next free block_page_hdr
				--heap->stats.num_blocks_free;
				GL_HeapMarkBlockUsed (heap, segment, next_block_page_index);
				block_page_hdr->size_in_pages += next_block_page_hdr->size_in_pages;
				assert ((block_page_index + block_page_hdr->size_in_pages) <= heap->num_pages_per_segment);
				*next_block_page_hdr = EMPTY_PAGE_HDR;
			}
		}
	}

	{
		const page_index_t next_block_page_index = block_page_index + block_page_hdr->size_in_pages;
		if (next_block_page_index < heap->num_pages_per_segment)
		{
			glheappagehdr_t *next_block_page_hdr = &segment->page_hdrs[next_block_page_index];
			next_block_page_hdr->prev_block_page_index = block_page_index;
		}
	}

	TRACE_LOG (" Free block at %u size %u pages\n", block_page_index, block_page_hdr->size_in_pages);
	if (!GL_HeapIsBlockFree (heap, segment, block_page_index))
		++heap->stats.num_blocks_free;
	GL_HeapMarkBlockFree (heap, segment, block_page_hdr->size_in_pages, block_page_index);
}

/*
===============
GL_HeapCreate
===============
*/
glheap_t *GL_HeapCreate (VkDeviceSize segment_size, uint32_t page_size, uint32_t memory_type_index, vulkan_memory_type_t memory_type, const char *heap_name)
{
	assert (Q_nextPow2 (page_size) == page_size);
	assert (page_size >= (1 << (NUM_SMALL_ALLOC_SIZES + 1)));
	assert (segment_size >= page_size);
	assert ((segment_size % page_size) == 0);
	assert ((segment_size / page_size) <= MAX_PAGES);
	glheap_t *heap = Mem_Alloc (sizeof (glheap_t));
	heap->segment_size = segment_size;
	heap->num_pages_per_segment = segment_size / page_size;
	heap->num_masks_per_segment = (heap->num_pages_per_segment + 63) / 64;
	heap->page_size = page_size;
	heap->min_small_alloc_size = heap->page_size / 64;
	heap->page_size_shift = Q_log2 (page_size);
	heap->small_alloc_shift = Q_log2 (page_size / (1 << NUM_SMALL_ALLOC_SIZES));
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
	for (uint32_t mask_page_offset = 0; mask_page_offset < heap->num_segments; ++mask_page_offset)
	{
		glheapsegment_t *segment = heap->segments[mask_page_offset];
		R_FreeVulkanMemory (&segment->memory, num_allocations);
		Mem_Free (segment->page_hdrs);
		Mem_Free (segment->small_alloc_links);
		Mem_Free (segment->small_alloc_masks);
		for (int mask_page_index = 0; mask_page_index < NUM_BLOCK_SIZE_CLASSES; ++mask_page_index)
		{
			Mem_Free (segment->free_blocks_bitfields[mask_page_index]);
			Mem_Free (segment->free_blocks_skip_bitfields[mask_page_index]);
		}
	}
	Mem_Free (heap->segments);
}

/*
===============
GL_HeapAllocate
===============
*/
glheapallocation_t *GL_HeapAllocate (glheap_t *heap, VkDeviceSize size, VkDeviceSize alignment, atomic_uint32_t *num_allocations)
{
	assert (size > 0);
	assert (alignment > 0);

	glheapallocation_t *allocation = Mem_Alloc (sizeof (glheapallocation_t));
	allocation->size = size;

	++heap->stats.num_allocations;
	heap->stats.num_bytes_allocated += size;

	if (size < heap->segment_size)
	{
		ZEROED_STRUCT (allocinfo_t, alloc_info);
		const VkDeviceSize size_alignment_max = q_max (size, alignment);
		alloc_info.is_small_alloc = size_alignment_max <= heap->page_size / 2;
		if (alloc_info.is_small_alloc)
		{
			++heap->stats.num_small_allocations;
			alloc_info.small_alloc_size = q_max (Q_nextPow2 (size_alignment_max), heap->min_small_alloc_size);
			alloc_info.small_alloc_bucket = Q_log2 (alloc_info.small_alloc_size >> heap->small_alloc_shift);
		}
		else
		{
			++heap->stats.num_block_allocations;
			alloc_info.alloc_size_in_pages = (size + heap->page_size - 1) >> heap->page_size_shift;
			alloc_info.alignment_in_pages = (alignment + heap->page_size - 1) >> heap->page_size_shift;
			alloc_info.size_class = q_min (Q_log2 (alloc_info.alloc_size_in_pages), NUM_BLOCK_SIZE_CLASSES - 1);
		}

		const uint32_t num_segments = heap->num_segments;
		for (uint32_t mask_page_offset = 0; mask_page_offset < (num_segments + 1); ++mask_page_offset)
		{
			if (mask_page_offset == num_segments)
			{
				heap->segments = Mem_Realloc (heap->segments, sizeof (glheapsegment_t *) * (num_segments + 1));
				heap->segments[mask_page_offset] = GL_CreateHeapSegment (heap, num_allocations);
				++heap->stats.num_blocks_free;
				++heap->num_segments;
			}

			const qboolean success = GL_HeapAllocateFromSegment (allocation, heap, heap->segments[mask_page_offset], &alloc_info);
			if (success)
				return allocation;
		}

		Sys_Error ("GL_HeapAllocate failed to allocate");
	}
	else
	{
		++heap->stats.num_dedicated_allocations;
		heap->dedicated_alloc_bytes += allocation->size;
		allocation->alloc_type = ALLOC_TYPE_DEDICATED;
		allocation->memory = Mem_Alloc (sizeof (vulkan_memory_t));

		ZEROED_STRUCT (VkMemoryAllocateInfo, memory_allocate_info);
		memory_allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		memory_allocate_info.allocationSize = size;
		memory_allocate_info.memoryTypeIndex = heap->memory_type_index;

		R_AllocateVulkanMemory (allocation->memory, &memory_allocate_info, heap->memory_type, num_allocations);
		if (allocation->memory->handle != VK_NULL_HANDLE)
			GL_SetObjectName ((uint64_t)allocation->memory->handle, VK_OBJECT_TYPE_DEVICE_MEMORY, heap->name);
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
	--heap->stats.num_allocations;
	heap->stats.num_bytes_allocated -= allocation->size;

	if (allocation->alloc_type == ALLOC_TYPE_PAGES)
	{
		--heap->stats.num_block_allocations;
		GL_HeapFreeBlockFromSegment (heap, allocation->segment, heap->page_size_shift, allocation->offset);
	}
	else if (allocation->alloc_type == ALLOC_TYPE_DEDICATED)
	{
		--heap->stats.num_dedicated_allocations;
		heap->dedicated_alloc_bytes -= allocation->size;
		R_FreeVulkanMemory (allocation->memory, num_allocations);
		Mem_Free (allocation->memory);
	}
	else if (allocation->alloc_type >= ALLOC_TYPE_SMALL_ALLOC)
	{
		--heap->stats.num_small_allocations;
		if (GL_HeapSmallFreeFromBlock (heap, allocation->segment, allocation))
			GL_HeapFreeBlockFromSegment (heap, allocation->segment, heap->page_size_shift, allocation->offset);
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
	return (allocation->alloc_type == ALLOC_TYPE_DEDICATED) ? allocation->memory->handle : allocation->segment->memory.handle;
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

/*
===============
GL_HeapGetStats
===============
*/
glheapstats_t *GL_HeapGetStats (glheap_t *heap)
{
	heap->stats.num_pages_allocated = 0;
	uint32_t num_total_pages = 0;
	uint64_t total_allocated_page_bytes = 0;
	uint32_t small_alloc_pages_bytes = 0;
	uint64_t small_alloc_bytes = 0;
	for (uint32_t mask_page_offset = 0; mask_page_offset < heap->num_segments; ++mask_page_offset)
	{
		glheapsegment_t *segment = heap->segments[mask_page_offset];
		num_total_pages += heap->num_pages_per_segment;
		heap->stats.num_pages_allocated += segment->num_pages_allocated;
		total_allocated_page_bytes += segment->num_pages_allocated * heap->page_size;
		for (int i = 0; i < NUM_SMALL_ALLOC_SIZES; ++i)
		{
			const uint32_t slots_per_page = SMALL_SLOTS_PER_PAGE[i];
			const uint32_t slot_size = heap->page_size / slots_per_page;
			page_index_t   small_alloc_page_index = segment->small_alloc_free_list_heads[i];
			while (small_alloc_page_index != INVALID_PAGE_INDEX)
			{
				small_alloc_pages_bytes += heap->page_size;
				glheapsmallalloclinks_t *links = &segment->small_alloc_links[small_alloc_page_index];
				uint64_t				 small_alloc_mask = segment->small_alloc_masks[small_alloc_page_index];
				for (uint32_t slot_index = 0; slot_index < slots_per_page; ++slot_index)
				{
					if ((small_alloc_mask & (1ull << slot_index)) != 0)
						small_alloc_bytes += slot_size;
				}
				small_alloc_page_index = links->next_small_alloc_page;
			}
		}
	}
	heap->stats.num_segments = heap->num_segments;
	heap->stats.num_pages_free = num_total_pages - heap->stats.num_pages_allocated;
	heap->stats.num_bytes_free = heap->stats.num_pages_free * heap->page_size;
	heap->stats.num_bytes_wasted =
		(total_allocated_page_bytes - small_alloc_pages_bytes) + heap->dedicated_alloc_bytes + small_alloc_bytes - heap->stats.num_bytes_allocated;
	return &heap->stats;
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
		Con_Printf ("%s\n", what);   \
		abort ();                    \
	}

/*
=================
TestHeapCleanState
=================
*/
static void TestHeapCleanState (glheap_t *heap)
{
	for (uint32_t i = 0; i < heap->num_segments; ++i)
	{
		glheapsegment_t *segment = heap->segments[i];
		HEAP_TEST_ASSERT (segment->num_pages_allocated == 0, "num_pages_allocated needs to be 0");
		HEAP_TEST_ASSERT (segment->page_hdrs[0].size_in_pages = heap->num_pages_per_segment, "Empty heap first block needs to fill all pages");
		for (int j = 0; j < NUM_BLOCK_SIZE_CLASSES; ++j)
		{
			HEAP_TEST_ASSERT (segment->free_blocks_bitfields[j][0] == 1, "first bitfield bit needs to be 1");
			HEAP_TEST_ASSERT (segment->free_blocks_skip_bitfields[j][0] == 1, "first skip bitfield bit needs to be 1");
		}
		for (page_index_t j = 1; j < heap->num_pages_per_segment; ++j)
		{
			HEAP_TEST_ASSERT (memcmp (&segment->page_hdrs[j], &EMPTY_PAGE_HDR, sizeof (glheappagehdr_t)) == 0, "Page block header needs to be empty");
			HEAP_TEST_ASSERT (
				memcmp (&segment->small_alloc_links[j], &EMPTY_SMALL_ALLOC_LINKS, sizeof (glheapsmallalloclinks_t)) == 0,
				"Page small alloc links need to be empty");
			HEAP_TEST_ASSERT (segment->small_alloc_masks[j] == 0, "Page small alloc masks needs to be empty");
		}
		for (page_index_t j = 1; j < ((heap->num_pages_per_segment + 63) / 64); ++j)
			for (int k = 0; k < NUM_BLOCK_SIZE_CLASSES; ++k)
				HEAP_TEST_ASSERT (segment->free_blocks_bitfields[k][j] == 0, "bitfield is not 0");
		for (page_index_t j = 1; j < ((heap->num_pages_per_segment + 4095) / 4096); ++j)
			for (int k = 0; k < NUM_BLOCK_SIZE_CLASSES; ++k)
				HEAP_TEST_ASSERT (segment->free_blocks_skip_bitfields[k][j] == 0, "skip bitfield is not 0");
		for (page_index_t j = 0; j < NUM_SMALL_ALLOC_SIZES; ++j)
			HEAP_TEST_ASSERT (segment->small_alloc_free_list_heads[j] == INVALID_PAGE_INDEX, "free list head is not empty");
	}
	HEAP_TEST_ASSERT (heap->num_segments == heap->stats.num_blocks_free, "Invalid number of free blocks");

	glheapstats_t *stats = GL_HeapGetStats (heap);
	HEAP_TEST_ASSERT (stats->num_allocations == 0, "Invalid num_allocations counter");
	HEAP_TEST_ASSERT (stats->num_small_allocations == 0, "Invalid num_small_allocations counter");
	HEAP_TEST_ASSERT (stats->num_block_allocations == 0, "Invalid num_block_allocations counter");
	HEAP_TEST_ASSERT (stats->num_dedicated_allocations == 0, "Invalid num_dedicated_allocations counter");
	HEAP_TEST_ASSERT (stats->num_blocks_free == heap->num_segments, "Invalid num_blocks_free counter");
}

/*
=================
TestHeapConsistency
=================
*/
static void TestHeapConsistency (glheap_t *heap)
{
	for (uint32_t i = 0; i < heap->num_segments; ++i)
	{
		page_index_t	 current_block_index = 0;
		page_index_t	 prev_block = 0;
		qboolean		 prev_block_free = false;
		page_index_t	 num_allocated_pages = 0;
		glheapsegment_t *segment = heap->segments[i];
		while (current_block_index < heap->num_pages_per_segment)
		{
			glheappagehdr_t *block = &segment->page_hdrs[current_block_index];
			const qboolean	 block_free = GET_BIT (segment->free_blocks_bitfields[0], current_block_index);
			if (current_block_index > 0)
			{
				HEAP_TEST_ASSERT (block->prev_block_page_index == prev_block, "Invalid prev block");
				if (prev_block_free)
					HEAP_TEST_ASSERT (!block_free, "Found two consecutive free blocks");
			}
			prev_block = current_block_index;
			for (page_index_t j = 1; j < block->size_in_pages; ++j)
				for (int k = 0; k < NUM_BLOCK_SIZE_CLASSES; ++k)
					HEAP_TEST_ASSERT (!GET_BIT (segment->free_blocks_bitfields[k], current_block_index + j), "Free bit set for non block page");
			if (!block_free)
				num_allocated_pages += block->size_in_pages;
			prev_block_free = block_free;
			current_block_index += block->size_in_pages;
		}
		HEAP_TEST_ASSERT (current_block_index == heap->num_pages_per_segment, "Blocks need to add up to num pages");
		HEAP_TEST_ASSERT (num_allocated_pages == segment->num_pages_allocated, "Invalid number of allocated pages found");
	}

	glheapstats_t *stats = GL_HeapGetStats (heap);
	HEAP_TEST_ASSERT (
		stats->num_allocations == (stats->num_small_allocations + stats->num_block_allocations + stats->num_dedicated_allocations), "Invalid alloc counter");
}

/*
=================
GL_HeapTest_f
=================
*/
void GL_HeapTest_f (void)
{
	const VkDeviceSize TEST_HEAP_SIZE = 1ull * 1024ull * 1024ull;
	const VkDeviceSize TEST_HEAP_PAGE_SIZE = 4096;
	const int		   NUM_ITERATIONS = 100;
	const int		   NUM_ALLOCS_PER_ITERATION = 500;
	const int		   MAX_ALLOC_SIZE = 64ull * 1024ull;
	const VkDeviceSize ALIGNMENTS[] = {
		1, 1, 1, 1, 2, 2, 2, 2, 4, 4, 4, 4, 8, 8, 16, 16, 32, 32, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384,
	};
	const int NUM_ALIGNMENTS = countof (ALIGNMENTS);

	atomic_uint32_t num_allocations;
	Atomic_StoreUInt32 (&num_allocations, 0);
	glheap_t *test_heap = GL_HeapCreate (TEST_HEAP_SIZE, TEST_HEAP_PAGE_SIZE, 0, VULKAN_MEMORY_TYPE_NONE, "Test Heap");
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
					const VkDeviceSize size = (rand () % (MAX_ALLOC_SIZE - 1) + 1);
					const VkDeviceSize alignment = ALIGNMENTS[rand () % NUM_ALIGNMENTS];
					HEAP_TEST_ASSERT (allocations[i] == NULL, "allocation is not NULL");

					glheapallocation_t *allocation = GL_HeapAllocate (test_heap, size, alignment, &num_allocations);
					const VkDeviceSize	offset = GL_HeapGetAllocationOffset (allocation);
					HEAP_TEST_ASSERT ((offset % alignment) == 0, "wrong alignment");
					allocations[i] = allocation;
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
