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
===============
GL_CreateHeap
===============
*/
glheap_t * GL_CreateHeap(VkDeviceSize size, uint32_t memory_type_index)
{
	glheap_t * heap = malloc(sizeof(glheap_t));

	VkMemoryAllocateInfo memory_allocate_info;
	memset(&memory_allocate_info, 0, sizeof(memory_allocate_info));
	memory_allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	memory_allocate_info.allocationSize = size;
	memory_allocate_info.memoryTypeIndex = memory_type_index;

	VkResult err = vkAllocateMemory(vulkan_globals.device, &memory_allocate_info, NULL, &heap->heap_memory);
	if (err != VK_SUCCESS)
		Sys_Error("vkAllocateMemory failed");

	heap->free_head = malloc(sizeof(glheapnode_t));
	heap->free_head->offset = 0;
	heap->free_head->size = size;

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
	vkFreeMemory(vulkan_globals.device, heap->heap_memory, NULL);
	free(heap->free_head);
	free(heap);
}

/*
===============
GL_HeapAllocate
===============
*/
glheapnode_t * GL_HeapAllocate(glheap_t * heap, VkDeviceSize size, VkDeviceSize alignment)
{
	return NULL;
}

/*
===============
GL_HeapFree
===============
*/
void GL_HeapFree(glheap_t * heap, glheapnode_t * node)
{
}
