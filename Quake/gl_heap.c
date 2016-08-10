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
	Dumbest possible allocator for device memory.

================================================================================
*/

/*
===============
GL_CreateHeap
===============
*/
glheap_t * GL_CreateHeap(VkDeviceSize size, uint32_t memory_type_index, const char * name)
{
	glheap_t * heap = malloc(sizeof(glheap_t));

	VkMemoryAllocateInfo memory_allocate_info;
	memset(&memory_allocate_info, 0, sizeof(memory_allocate_info));
	memory_allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	memory_allocate_info.allocationSize = size;
	memory_allocate_info.memoryTypeIndex = memory_type_index;

	VkResult err = vkAllocateMemory(vulkan_globals.device, &memory_allocate_info, NULL, &heap->memory);
	if (err != VK_SUCCESS)
		Sys_Error("vkAllocateMemory failed");

	GL_SetObjectName((uint64_t)heap->memory, VK_DEBUG_REPORT_OBJECT_TYPE_DEVICE_MEMORY_EXT, name);

	heap->head = malloc(sizeof(glheapnode_t));
	heap->head->offset = 0;
	heap->head->size = size;
	heap->head->prev = NULL;
	heap->head->next = NULL;
	heap->head->free = true;

	return heap;
}

/*
===============
GL_DestroyHeap
===============
*/
void GL_DestroyHeap(glheap_t * heap)
{
	GL_WaitForDeviceIdle();
	vkFreeMemory(vulkan_globals.device, heap->memory, NULL);
	free(heap->head);
	free(heap);
}

/*
===============
GL_HeapAllocate
===============
*/
glheapnode_t * GL_HeapAllocate(glheap_t * heap, VkDeviceSize size, VkDeviceSize alignment, VkDeviceSize * aligned_offset)
{
	for(glheapnode_t * current_node = heap->head; current_node != NULL; current_node = current_node->next)
	{
		if (!current_node->free)
			continue;

		const VkDeviceSize align_mod = current_node->offset % alignment;
		VkDeviceSize align_padding = (align_mod == 0) ? 0 : (alignment - align_mod);
		VkDeviceSize aligned_size = size + align_padding;

		if (current_node->size > aligned_size)
		{
			glheapnode_t * new_node = malloc(sizeof(glheapnode_t));
			*new_node = *current_node;
			new_node->prev = current_node->prev;
			new_node->next = current_node;
			if(current_node->prev)
				current_node->prev->next = new_node;
			current_node->prev = new_node;
			new_node->free = false;

			new_node->size = aligned_size;
			current_node->size -= aligned_size;
			current_node->offset += aligned_size;

			if (current_node == heap->head)
				heap->head = new_node;

			*aligned_offset = new_node->offset + align_padding;
			return new_node;
		}
		else if (current_node->size == aligned_size)
		{
			current_node->free = false;
			*aligned_offset = current_node->offset + align_padding;
			return current_node;
		}
	}

	*aligned_offset = 0;
	return NULL;
}

/*
===============
GL_HeapFree
===============
*/
void GL_HeapFree(glheap_t * heap, glheapnode_t * node)
{
	if(node->free)
		Sys_Error("Trying to free a node that is already freed");

	node->free = true;
	if(node->prev && node->prev->free)
	{
		glheapnode_t * prev = node->prev;

		prev->next = node->next;
		if (node->next)
			node->next->prev = prev;

		prev->size += node->size;

		free(node);
		node = prev;
	}

	if(node->next && node->next->free)
	{
		glheapnode_t * next = node->next;

		if(next->next)
			next->next->prev = node;
		node->next = next->next;

		node->size += next->size;

		free(next);
	}
}

/*
===============
GL_IsHeapEmpty
===============
*/
qboolean GL_IsHeapEmpty(glheap_t * heap)
{
	return heap->head->next == NULL;
}

/*
================
GL_AllocateFromHeaps
================
*/
VkDeviceSize GL_AllocateFromHeaps(int num_heaps, glheap_t ** heaps, VkDeviceSize heap_size, uint32_t memory_type_index,
	VkDeviceSize size, VkDeviceSize alignment, glheap_t ** heap, glheapnode_t ** heap_node, int * num_allocations, const char * heap_name)
{
	for(int i = 0; i < num_heaps; ++i)
	{
		qboolean new_heap = false;
		if(!heaps[i])
		{
			heaps[i] = GL_CreateHeap(heap_size, memory_type_index, heap_name);
			*num_allocations += 1;
			new_heap = true;
		}

		VkDeviceSize aligned_offset;
		glheapnode_t * node = GL_HeapAllocate(heaps[i], size, alignment, &aligned_offset);
		if(node)
		{
			*heap_node = node;
			*heap = heaps[i];
			return aligned_offset;
		} else if(new_heap)
			break;
	}

	Sys_Error("Could not allocate memory for texture");
	return 0;
}

/*
================
GL_FreeFromHeaps
================
*/
void GL_FreeFromHeaps(int num_heaps, glheap_t ** heaps, glheap_t * heap, glheapnode_t * heap_node, int * num_allocations)
{
	GL_HeapFree(heap, heap_node);
	if(GL_IsHeapEmpty(heap))
	{
		*num_allocations -= 1;
		GL_DestroyHeap(heap);
		for(int i = 0; i < num_heaps; ++i)
			if(heaps[i] == heap)
				heaps[i]  = NULL;
	}
}
