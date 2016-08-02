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

typedef struct glheapnode_s
{
	VkDeviceSize offset;
	VkDeviceSize size;
	glheapnode_t * prev;
	glheapnode_t * next;
	qboolean free;
} glheapnode_t;

typedef struct glheap_s
{
	VkDeviceMemory	memory;
	glheapnode_t * head;
} glheap_t;

glheap_t * GL_CreateHeap(VkDeviceSize size, uint32_t memory_type_index);
void GL_DestroyHeap(glheap_t * heap);

glheapnode_t * GL_HeapAllocate(glheap_t * heap, VkDeviceSize size, VkDeviceSize alignment, VkDeviceSize * aligned_offset);
void GL_HeapFree(glheap_t * heap, glheapnode_t * node);

qboolean GL_IsHeapEmpty(glheap_t * heap);

#endif
