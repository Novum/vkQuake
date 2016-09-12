/*
Copyright (C) 1996-2001 Id Software, Inc.
Copyright (C) 2002-2009 John Fitzgibbons and others
Copyright (C) 2007-2008 Kristian Duske
Copyright (C) 2010-2014 QuakeSpasm developers
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
// r_misc.c

#include "quakedef.h"
#include "float.h"

//johnfitz -- new cvars
extern cvar_t r_clearcolor;
extern cvar_t r_drawflat;
extern cvar_t r_flatlightstyles;
extern cvar_t gl_fullbrights;
extern cvar_t gl_farclip;
extern cvar_t r_waterquality;
extern cvar_t r_waterwarp;
extern cvar_t r_oldskyleaf;
extern cvar_t r_drawworld;
extern cvar_t r_showtris;
extern cvar_t r_showbboxes;
extern cvar_t r_lerpmodels;
extern cvar_t r_lerpmove;
extern cvar_t r_nolerp_list;
extern cvar_t r_noshadow_list;
//johnfitz
extern cvar_t gl_zfix; // QuakeSpasm z-fighting fix

extern gltexture_t *playertextures[MAX_SCOREBOARD]; //johnfitz

vulkanglobals_t vulkan_globals;

int num_vulkan_tex_allocations = 0;
int num_vulkan_bmodel_allocations = 0;
int num_vulkan_mesh_allocations = 0;
int num_vulkan_misc_allocations = 0;
int num_vulkan_dynbuf_allocations = 0;

/*
================
Staging
================
*/
#define STAGING_BUFFER_SIZE_KB	16384
#define NUM_STAGING_BUFFERS		2

typedef struct
{
	VkBuffer			buffer;
	VkCommandBuffer		command_buffer;
	VkFence				fence;
	int					current_offset;
	qboolean			submitted;
	unsigned char *		data;
} stagingbuffer_t;

static VkCommandPool	staging_command_pool;
static VkDeviceMemory	staging_memory;
static stagingbuffer_t	staging_buffers[NUM_STAGING_BUFFERS];
static int				current_staging_buffer = 0;

/*
================
Dynamic vertex/index & uniform buffer
================
*/
#define DYNAMIC_VERTEX_BUFFER_SIZE_KB	2048
#define DYNAMIC_INDEX_BUFFER_SIZE_KB	2048
#define DYNAMIC_UNIFORM_BUFFER_SIZE_KB	512
#define NUM_DYNAMIC_BUFFERS				2
#define MAX_UNIFORM_ALLOC				2048

typedef struct
{
	VkBuffer			buffer;
	uint32_t			current_offset;
	unsigned char *		data;
} dynbuffer_t;

static VkDeviceMemory	dyn_vertex_buffer_memory;
static VkDeviceMemory	dyn_index_buffer_memory;
static VkDeviceMemory	dyn_uniform_buffer_memory;
static dynbuffer_t		dyn_vertex_buffers[NUM_DYNAMIC_BUFFERS];
static dynbuffer_t		dyn_index_buffers[NUM_DYNAMIC_BUFFERS];
static dynbuffer_t		dyn_uniform_buffers[NUM_DYNAMIC_BUFFERS];
static int				current_dyn_buffer_index = 0;
static VkDescriptorSet	ubo_descriptor_sets[2];

void R_VulkanMemStats_f (void);

/*
================
GL_MemoryTypeFromProperties
================
*/
int GL_MemoryTypeFromProperties(uint32_t type_bits, VkFlags requirements_mask, VkFlags preferred_mask)
{
	uint32_t current_type_bits = type_bits;
	uint32_t i;

	for (i = 0; i < VK_MAX_MEMORY_TYPES; i++) {
		if ((current_type_bits & 1) == 1)
		{
			if ((vulkan_globals.memory_properties.memoryTypes[i].propertyFlags & (requirements_mask | preferred_mask)) == (requirements_mask | preferred_mask))
				return i;
		}
		current_type_bits >>= 1;
	}

	current_type_bits = type_bits;
	for (i = 0; i < VK_MAX_MEMORY_TYPES; i++) {
		if ((current_type_bits & 1) == 1)
		{
			if ((vulkan_globals.memory_properties.memoryTypes[i].propertyFlags & requirements_mask) == requirements_mask)
				return i;
		}
		current_type_bits >>= 1;
	}

	Sys_Error("Could not find memory type");
	return 0;
}

/*
====================
GL_Fullbrights_f -- johnfitz
====================
*/
static void GL_Fullbrights_f (cvar_t *var)
{
	TexMgr_ReloadNobrightImages ();
}

/*
====================
R_SetClearColor_f -- johnfitz
====================
*/
static void R_SetClearColor_f (cvar_t *var)
{
	byte	*rgb;
	int		s;

	s = (int)r_clearcolor.value & 0xFF;
	rgb = (byte*)(d_8to24table + s);
	vulkan_globals.color_clear_value.color.float32[0] = rgb[0]/255;
	vulkan_globals.color_clear_value.color.float32[1] = rgb[1]/255;
	vulkan_globals.color_clear_value.color.float32[2] = rgb[2]/255;
	vulkan_globals.color_clear_value.color.float32[3] = 0.0f;
}

/*
====================
R_Novis_f -- johnfitz
====================
*/
static void R_VisChanged (cvar_t *var)
{
	extern int vis_changed;
	vis_changed = 1;
}

/*
===============
R_Model_ExtraFlags_List_f -- johnfitz -- called when r_nolerp_list or r_noshadow_list cvar changes
===============
*/
static void R_Model_ExtraFlags_List_f (cvar_t *var)
{
	int i;
	for (i=0; i < MAX_MODELS; i++)
		Mod_SetExtraFlags (cl.model_precache[i]);
}

/*
====================
R_SetWateralpha_f -- ericw
====================
*/
static void R_SetWateralpha_f (cvar_t *var)
{
	map_wateralpha = var->value;
}

/*
====================
R_SetLavaalpha_f -- ericw
====================
*/
static void R_SetLavaalpha_f (cvar_t *var)
{
	map_lavaalpha = var->value;
}

/*
====================
R_SetTelealpha_f -- ericw
====================
*/
static void R_SetTelealpha_f (cvar_t *var)
{
	map_telealpha = var->value;
}

/*
====================
R_SetSlimealpha_f -- ericw
====================
*/
static void R_SetSlimealpha_f (cvar_t *var)
{
	map_slimealpha = var->value;
}

/*
====================
GL_WaterAlphaForSurfface -- ericw
====================
*/
float GL_WaterAlphaForSurface (msurface_t *fa)
{
	if (fa->flags & SURF_DRAWLAVA)
		return map_lavaalpha > 0 ? map_lavaalpha : map_wateralpha;
	else if (fa->flags & SURF_DRAWTELE)
		return map_telealpha > 0 ? map_telealpha : map_wateralpha;
	else if (fa->flags & SURF_DRAWSLIME)
		return map_slimealpha > 0 ? map_slimealpha : map_wateralpha;
	else
		return map_wateralpha;
}

/*
===============
R_InitStagingBuffers
===============
*/
void R_InitStagingBuffers()
{
	int i;

	Con_Printf("Initializing staging\n");

	VkResult err;

	VkBufferCreateInfo buffer_create_info;
	memset(&buffer_create_info, 0, sizeof(buffer_create_info));
	buffer_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	buffer_create_info.size = STAGING_BUFFER_SIZE_KB * 1024;
	buffer_create_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

	for(i = 0; i < NUM_STAGING_BUFFERS; ++i)
	{
		staging_buffers[i].current_offset = 0;
		staging_buffers[i].submitted = false;

		err = vkCreateBuffer(vulkan_globals.device, &buffer_create_info, NULL, &staging_buffers[i].buffer);
		if (err != VK_SUCCESS)
			Sys_Error("vkCreateBuffer failed");

		GL_SetObjectName((uint64_t)staging_buffers[i].buffer, VK_DEBUG_REPORT_OBJECT_TYPE_BUFFER_EXT, "Staging Buffer");
	}

	VkMemoryRequirements memory_requirements;
	vkGetBufferMemoryRequirements(vulkan_globals.device, staging_buffers[0].buffer, &memory_requirements);

	const int align_mod = memory_requirements.size % memory_requirements.alignment;
	const int aligned_size = ((memory_requirements.size % memory_requirements.alignment) == 0 ) 
		? memory_requirements.size 
		: (memory_requirements.size + memory_requirements.alignment - align_mod);

	VkMemoryAllocateInfo memory_allocate_info;
	memset(&memory_allocate_info, 0, sizeof(memory_allocate_info));
	memory_allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	memory_allocate_info.allocationSize = NUM_STAGING_BUFFERS * aligned_size;
	memory_allocate_info.memoryTypeIndex = GL_MemoryTypeFromProperties(memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, VK_MEMORY_PROPERTY_HOST_CACHED_BIT);

	num_vulkan_misc_allocations += 1;
	err = vkAllocateMemory(vulkan_globals.device, &memory_allocate_info, NULL, &staging_memory);
	if (err != VK_SUCCESS)
		Sys_Error("vkAllocateMemory failed");

	GL_SetObjectName((uint64_t)staging_memory, VK_DEBUG_REPORT_OBJECT_TYPE_DEVICE_MEMORY_EXT, "Staging Buffers");

	for(i = 0; i < NUM_STAGING_BUFFERS; ++i)
	{
		err = vkBindBufferMemory(vulkan_globals.device, staging_buffers[i].buffer, staging_memory, i * aligned_size);
		if (err != VK_SUCCESS)
			Sys_Error("vkBindBufferMemory failed");
	}

	unsigned char * data;
	err = vkMapMemory(vulkan_globals.device, staging_memory, 0, NUM_STAGING_BUFFERS * aligned_size, 0, &data);
	if (err != VK_SUCCESS)
		Sys_Error("vkMapMemory failed");

	VkCommandPoolCreateInfo command_pool_create_info;
	memset(&command_pool_create_info, 0, sizeof(command_pool_create_info));
	command_pool_create_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	command_pool_create_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	command_pool_create_info.queueFamilyIndex = vulkan_globals.gfx_queue_family_index;

	err = vkCreateCommandPool(vulkan_globals.device, &command_pool_create_info, NULL, &staging_command_pool);
	if (err != VK_SUCCESS)
		Sys_Error("vkCreateCommandPool failed");

	VkCommandBufferAllocateInfo command_buffer_allocate_info;
	memset(&command_buffer_allocate_info, 0, sizeof(command_buffer_allocate_info));
	command_buffer_allocate_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	command_buffer_allocate_info.commandPool = staging_command_pool;
	command_buffer_allocate_info.commandBufferCount = NUM_STAGING_BUFFERS;

	VkCommandBuffer command_buffers[NUM_STAGING_BUFFERS];
	err = vkAllocateCommandBuffers(vulkan_globals.device, &command_buffer_allocate_info, command_buffers);
	if (err != VK_SUCCESS)
		Sys_Error("vkAllocateCommandBuffers failed");

	VkFenceCreateInfo fence_create_info;
	memset(&fence_create_info, 0, sizeof(fence_create_info));
	fence_create_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;

	VkCommandBufferBeginInfo command_buffer_begin_info;
	memset(&command_buffer_begin_info, 0, sizeof(command_buffer_begin_info));
	command_buffer_begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

	for(i = 0; i < NUM_STAGING_BUFFERS; ++i)
	{
		err = vkCreateFence(vulkan_globals.device, &fence_create_info, NULL, &staging_buffers[i].fence);
		if (err != VK_SUCCESS)
			Sys_Error("vkCreateFence failed");

		staging_buffers[i].command_buffer = command_buffers[i];
		staging_buffers[i].data = data + (i * aligned_size);

		err = vkBeginCommandBuffer(staging_buffers[i].command_buffer, &command_buffer_begin_info);
		if (err != VK_SUCCESS)
			Sys_Error("vkBeginCommandBuffer failed");
	}
}

/*
===============
R_SubmitStagingBuffer
===============
*/
static void R_SubmitStagingBuffer(int index)
{
	vkEndCommandBuffer(staging_buffers[index].command_buffer);
	
	VkMappedMemoryRange range;
	memset(&range, 0, sizeof(range));
	range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
    range.memory = staging_memory;
    range.size = VK_WHOLE_SIZE;
	vkFlushMappedMemoryRanges(vulkan_globals.device, 1, &range);

	VkSubmitInfo submit_info;
	memset(&submit_info, 0, sizeof(submit_info));
	submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submit_info.commandBufferCount = 1;
	submit_info.pCommandBuffers = &staging_buffers[index].command_buffer;
	
	vkQueueSubmit(vulkan_globals.queue, 1, &submit_info, staging_buffers[index].fence);

	staging_buffers[index].submitted = true;
	current_staging_buffer = (current_staging_buffer + 1) % NUM_STAGING_BUFFERS;
}

/*
===============
R_SubmitStagingBuffers
===============
*/
void R_SubmitStagingBuffers()
{
	int i;
	for (i = 0; i<NUM_STAGING_BUFFERS; ++i)
	{
		if (!staging_buffers[i].submitted && staging_buffers[i].current_offset > 0)
			R_SubmitStagingBuffer(i);
	}
}

/*
===============
R_StagingAllocate
===============
*/
byte * R_StagingAllocate(int size, int alignment, VkCommandBuffer * command_buffer, VkBuffer * buffer, int * buffer_offset)
{
	vulkan_globals.device_idle = false;

	stagingbuffer_t * staging_buffer = &staging_buffers[current_staging_buffer];
	const int align_mod = staging_buffer->current_offset % alignment;
	staging_buffer->current_offset = ((staging_buffer->current_offset % alignment) == 0) 
		? staging_buffer->current_offset 
		: (staging_buffer->current_offset + alignment - align_mod);

	if (size > (STAGING_BUFFER_SIZE_KB * 1024))
		Sys_Error("Cannot allocate staging buffer space");

	if ((staging_buffer->current_offset + size) >= (STAGING_BUFFER_SIZE_KB * 1024) && !staging_buffer->submitted)
		R_SubmitStagingBuffer(current_staging_buffer);

	staging_buffer = &staging_buffers[current_staging_buffer];
	if (staging_buffer->submitted)
	{
		VkResult err;

		err = vkWaitForFences(vulkan_globals.device, 1, &staging_buffer->fence, VK_TRUE, UINT64_MAX);
		if (err != VK_SUCCESS)
			Sys_Error("vkWaitForFences failed");

		err = vkResetFences(vulkan_globals.device, 1, &staging_buffer->fence);
		if (err != VK_SUCCESS)
			Sys_Error("vkResetFences failed");

		staging_buffer->current_offset = 0;
		staging_buffer->submitted = false;

		VkCommandBufferBeginInfo command_buffer_begin_info;
		memset(&command_buffer_begin_info, 0, sizeof(command_buffer_begin_info));
		command_buffer_begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

		err = vkBeginCommandBuffer(staging_buffer->command_buffer, &command_buffer_begin_info);
		if (err != VK_SUCCESS)
			Sys_Error("vkBeginCommandBuffer failed");
	}

	*command_buffer = staging_buffer->command_buffer;
	*buffer = staging_buffer->buffer;
	*buffer_offset = staging_buffer->current_offset;

	unsigned char *data = staging_buffer->data + staging_buffer->current_offset;
	staging_buffer->current_offset += size;

	return data;
}

/*
===============
R_InitDynamicVertexBuffers
===============
*/
static void R_InitDynamicVertexBuffers()
{
	int i;

	Con_Printf("Initializing dynamic vertex buffers\n");

	VkResult err;

	VkBufferCreateInfo buffer_create_info;
	memset(&buffer_create_info, 0, sizeof(buffer_create_info));
	buffer_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	buffer_create_info.size = DYNAMIC_VERTEX_BUFFER_SIZE_KB * 1024;
	buffer_create_info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;

	for(i = 0; i < NUM_DYNAMIC_BUFFERS; ++i)
	{
		dyn_vertex_buffers[i].current_offset = 0;

		err = vkCreateBuffer(vulkan_globals.device, &buffer_create_info, NULL, &dyn_vertex_buffers[i].buffer);
		if (err != VK_SUCCESS)
			Sys_Error("vkCreateBuffer failed");

		GL_SetObjectName((uint64_t)dyn_vertex_buffers[i].buffer, VK_DEBUG_REPORT_OBJECT_TYPE_BUFFER_EXT, "Dynamic Vertex Buffer");
	}

	VkMemoryRequirements memory_requirements;
	vkGetBufferMemoryRequirements(vulkan_globals.device, dyn_vertex_buffers[0].buffer, &memory_requirements);

	const int align_mod = memory_requirements.size % memory_requirements.alignment;
	const int aligned_size = ((memory_requirements.size % memory_requirements.alignment) == 0) 
		? memory_requirements.size 
		: (memory_requirements.size + memory_requirements.alignment - align_mod);

	VkMemoryAllocateInfo memory_allocate_info;
	memset(&memory_allocate_info, 0, sizeof(memory_allocate_info));
	memory_allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	memory_allocate_info.allocationSize = NUM_DYNAMIC_BUFFERS * aligned_size;
	memory_allocate_info.memoryTypeIndex = GL_MemoryTypeFromProperties(memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, VK_MEMORY_PROPERTY_HOST_CACHED_BIT);

	num_vulkan_dynbuf_allocations += 1;
	err = vkAllocateMemory(vulkan_globals.device, &memory_allocate_info, NULL, &dyn_vertex_buffer_memory);
	if (err != VK_SUCCESS)
		Sys_Error("vkAllocateMemory failed");

	GL_SetObjectName((uint64_t)dyn_vertex_buffer_memory, VK_DEBUG_REPORT_OBJECT_TYPE_DEVICE_MEMORY_EXT, "Dynamic Vertex Buffers");

	for(i = 0; i < NUM_DYNAMIC_BUFFERS; ++i)
	{
		err = vkBindBufferMemory(vulkan_globals.device, dyn_vertex_buffers[i].buffer, dyn_vertex_buffer_memory, i * aligned_size);
		if (err != VK_SUCCESS)
			Sys_Error("vkBindBufferMemory failed");
	}

	unsigned char * data;
	err = vkMapMemory(vulkan_globals.device, dyn_vertex_buffer_memory, 0, NUM_DYNAMIC_BUFFERS * aligned_size, 0, &data);
	if (err != VK_SUCCESS)
		Sys_Error("vkMapMemory failed");

	for(i = 0; i < NUM_DYNAMIC_BUFFERS; ++i)
		dyn_vertex_buffers[i].data = data + (i * aligned_size);
}

/*
===============
R_InitDynamicIndexBuffers
===============
*/
static void R_InitDynamicIndexBuffers()
{
	int i;

	Con_Printf("Initializing dynamic index buffers\n");

	VkResult err;

	VkBufferCreateInfo buffer_create_info;
	memset(&buffer_create_info, 0, sizeof(buffer_create_info));
	buffer_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	buffer_create_info.size = DYNAMIC_INDEX_BUFFER_SIZE_KB * 1024;
	buffer_create_info.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;

	for(i = 0; i < NUM_DYNAMIC_BUFFERS; ++i)
	{
		dyn_index_buffers[i].current_offset = 0;

		err = vkCreateBuffer(vulkan_globals.device, &buffer_create_info, NULL, &dyn_index_buffers[i].buffer);
		if (err != VK_SUCCESS)
			Sys_Error("vkCreateBuffer failed");

		GL_SetObjectName((uint64_t)dyn_index_buffers[i].buffer, VK_DEBUG_REPORT_OBJECT_TYPE_BUFFER_EXT, "Dynamic Index Buffer");
	}

	VkMemoryRequirements memory_requirements;
	vkGetBufferMemoryRequirements(vulkan_globals.device, dyn_index_buffers[0].buffer, &memory_requirements);

	const int align_mod = memory_requirements.size % memory_requirements.alignment;
	const int aligned_size = ((memory_requirements.size % memory_requirements.alignment) == 0) 
		? memory_requirements.size 
		: (memory_requirements.size + memory_requirements.alignment - align_mod);

	VkMemoryAllocateInfo memory_allocate_info;
	memset(&memory_allocate_info, 0, sizeof(memory_allocate_info));
	memory_allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	memory_allocate_info.allocationSize = NUM_DYNAMIC_BUFFERS * aligned_size;
	memory_allocate_info.memoryTypeIndex = GL_MemoryTypeFromProperties(memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, VK_MEMORY_PROPERTY_HOST_CACHED_BIT);

	num_vulkan_dynbuf_allocations += 1;
	err = vkAllocateMemory(vulkan_globals.device, &memory_allocate_info, NULL, &dyn_index_buffer_memory);
	if (err != VK_SUCCESS)
		Sys_Error("vkAllocateMemory failed");

	GL_SetObjectName((uint64_t)dyn_index_buffer_memory, VK_DEBUG_REPORT_OBJECT_TYPE_DEVICE_MEMORY_EXT, "Dynamic Index Buffers");

	for(i = 0; i < NUM_DYNAMIC_BUFFERS; ++i)
	{
		err = vkBindBufferMemory(vulkan_globals.device, dyn_index_buffers[i].buffer, dyn_index_buffer_memory, i * aligned_size);
		if (err != VK_SUCCESS)
			Sys_Error("vkBindBufferMemory failed");
	}

	unsigned char * data;
	err = vkMapMemory(vulkan_globals.device, dyn_index_buffer_memory, 0, NUM_DYNAMIC_BUFFERS * aligned_size, 0, &data);
	if (err != VK_SUCCESS)
		Sys_Error("vkMapMemory failed");

	for(i = 0; i < NUM_DYNAMIC_BUFFERS; ++i)
		dyn_index_buffers[i].data = data + (i * aligned_size);
}

/*
===============
R_InitDynamicUniformBuffers
===============
*/
static void R_InitDynamicUniformBuffers()
{
	int i;

	Con_Printf("Initializing dynamic uniform buffers\n");

	VkResult err;

	VkBufferCreateInfo buffer_create_info;
	memset(&buffer_create_info, 0, sizeof(buffer_create_info));
	buffer_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	buffer_create_info.size = DYNAMIC_UNIFORM_BUFFER_SIZE_KB * 1024;
	buffer_create_info.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;

	for(i = 0; i < NUM_DYNAMIC_BUFFERS; ++i)
	{
		dyn_uniform_buffers[i].current_offset = 0;

		err = vkCreateBuffer(vulkan_globals.device, &buffer_create_info, NULL, &dyn_uniform_buffers[i].buffer);
		if (err != VK_SUCCESS)
			Sys_Error("vkCreateBuffer failed");

		GL_SetObjectName((uint64_t)dyn_uniform_buffers[i].buffer, VK_DEBUG_REPORT_OBJECT_TYPE_BUFFER_EXT, "Dynamic Uniform Buffer");
	}

	VkMemoryRequirements memory_requirements;
	vkGetBufferMemoryRequirements(vulkan_globals.device, dyn_uniform_buffers[0].buffer, &memory_requirements);

	const int align_mod = memory_requirements.size % memory_requirements.alignment;
	const int aligned_size = ((memory_requirements.size % memory_requirements.alignment) == 0) 
		? memory_requirements.size 
		: (memory_requirements.size + memory_requirements.alignment - align_mod);

	VkMemoryAllocateInfo memory_allocate_info;
	memset(&memory_allocate_info, 0, sizeof(memory_allocate_info));
	memory_allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	memory_allocate_info.allocationSize = NUM_DYNAMIC_BUFFERS * aligned_size;
	memory_allocate_info.memoryTypeIndex = GL_MemoryTypeFromProperties(memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, VK_MEMORY_PROPERTY_HOST_CACHED_BIT);

	num_vulkan_dynbuf_allocations += 1;
	err = vkAllocateMemory(vulkan_globals.device, &memory_allocate_info, NULL, &dyn_uniform_buffer_memory);
	if (err != VK_SUCCESS)
		Sys_Error("vkAllocateMemory failed");

	GL_SetObjectName((uint64_t)dyn_uniform_buffer_memory, VK_DEBUG_REPORT_OBJECT_TYPE_DEVICE_MEMORY_EXT, "Dynamic Uniform Buffers");

	for(i = 0; i < NUM_DYNAMIC_BUFFERS; ++i)
	{
		err = vkBindBufferMemory(vulkan_globals.device, dyn_uniform_buffers[i].buffer, dyn_uniform_buffer_memory, i * aligned_size);
		if (err != VK_SUCCESS)
			Sys_Error("vkBindBufferMemory failed");
	}

	unsigned char * data;
	err = vkMapMemory(vulkan_globals.device, dyn_uniform_buffer_memory, 0, NUM_DYNAMIC_BUFFERS * aligned_size, 0, &data);
	if (err != VK_SUCCESS)
		Sys_Error("vkMapMemory failed");

	for(i = 0; i < NUM_DYNAMIC_BUFFERS; ++i)
		dyn_uniform_buffers[i].data = data + (i * aligned_size);

	VkDescriptorSetAllocateInfo descriptor_set_allocate_info;
	memset(&descriptor_set_allocate_info, 0, sizeof(descriptor_set_allocate_info));
	descriptor_set_allocate_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	descriptor_set_allocate_info.descriptorPool = vulkan_globals.descriptor_pool;
	descriptor_set_allocate_info.descriptorSetCount = 1;
	descriptor_set_allocate_info.pSetLayouts = &vulkan_globals.ubo_set_layout;

	vkAllocateDescriptorSets(vulkan_globals.device, &descriptor_set_allocate_info, &ubo_descriptor_sets[0]);
	vkAllocateDescriptorSets(vulkan_globals.device, &descriptor_set_allocate_info, &ubo_descriptor_sets[1]);

	VkDescriptorBufferInfo buffer_info;
	memset(&buffer_info, 0, sizeof(buffer_info));
	buffer_info.offset = 0;
	buffer_info.range = MAX_UNIFORM_ALLOC;

	VkWriteDescriptorSet ubo_write;
	memset(&ubo_write, 0, sizeof(ubo_write));
	ubo_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	ubo_write.dstBinding = 0;
	ubo_write.dstArrayElement = 0;
	ubo_write.descriptorCount = 1;
	ubo_write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
	ubo_write.pBufferInfo = &buffer_info;

	for(i = 0; i < NUM_DYNAMIC_BUFFERS; ++i)
	{
		buffer_info.buffer = dyn_uniform_buffers[i].buffer;
		ubo_write.dstSet = ubo_descriptor_sets[i];
		vkUpdateDescriptorSets(vulkan_globals.device, 1, &ubo_write, 0, NULL);
	}
}

/*
===============
R_SwapDynamicBuffers
===============
*/
void R_SwapDynamicBuffers()
{
	current_dyn_buffer_index = (current_dyn_buffer_index + 1) % NUM_DYNAMIC_BUFFERS;
	dyn_vertex_buffers[current_dyn_buffer_index].current_offset = 0;
	dyn_index_buffers[current_dyn_buffer_index].current_offset = 0;
	dyn_uniform_buffers[current_dyn_buffer_index].current_offset = 0;
}

/*
===============
R_FlushDynamicBuffers
===============
*/
void R_FlushDynamicBuffers()
{
	VkMappedMemoryRange ranges[3];
	memset(&ranges, 0, sizeof(ranges));
	ranges[0].sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
    ranges[0].memory = dyn_vertex_buffer_memory;
    ranges[0].size = VK_WHOLE_SIZE;
	ranges[1].sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
    ranges[1].memory = dyn_index_buffer_memory;
    ranges[1].size = VK_WHOLE_SIZE;
	ranges[2].sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
    ranges[2].memory = dyn_uniform_buffer_memory;
    ranges[2].size = VK_WHOLE_SIZE;
	vkFlushMappedMemoryRanges(vulkan_globals.device, 3, ranges);
}

/*
===============
R_VertexAllocate
===============
*/
byte * R_VertexAllocate(int size, VkBuffer * buffer, VkDeviceSize * buffer_offset)
{
	dynbuffer_t *dyn_vb = &dyn_vertex_buffers[current_dyn_buffer_index];

	if ((dyn_vb->current_offset + size) > (DYNAMIC_VERTEX_BUFFER_SIZE_KB * 1024))
		Sys_Error("Out of dynamic vertex buffer space, increase DYNAMIC_VERTEX_BUFFER_SIZE_KB");

	*buffer = dyn_vb->buffer;
	*buffer_offset = dyn_vb->current_offset;

	unsigned char *data = dyn_vb->data + dyn_vb->current_offset;
	dyn_vb->current_offset += size;

	return data;
}

/*
===============
R_IndexAllocate
===============
*/
byte * R_IndexAllocate(int size, VkBuffer * buffer, VkDeviceSize * buffer_offset)
{
	dynbuffer_t *dyn_ib = &dyn_index_buffers[current_dyn_buffer_index];

	if ((dyn_ib->current_offset + size) > (DYNAMIC_INDEX_BUFFER_SIZE_KB * 1024))
		Sys_Error("Out of dynamic index buffer space, increase DYNAMIC_INDEX_BUFFER_SIZE_KB");

	*buffer = dyn_ib->buffer;
	*buffer_offset = dyn_ib->current_offset;

	unsigned char *data = dyn_ib->data + dyn_ib->current_offset;
	dyn_ib->current_offset += size;

	return data;
}

/*
===============
R_UniformAllocate

UBO offsets need to be 256 byte aligned on NVIDIA hardware
This is also the maximum required alignment by the Vulkan spec
===============
*/
byte * R_UniformAllocate(int size, VkBuffer * buffer, uint32_t * buffer_offset, VkDescriptorSet * descriptor_set)
{
	if (size > MAX_UNIFORM_ALLOC)
		Sys_Error("Increase MAX_UNIFORM_ALLOC");

	const int align_mod = size % 256;
	const int aligned_size = ((size % 256) == 0) ? size : (size + 256 - align_mod);

	dynbuffer_t *dyn_ub = &dyn_uniform_buffers[current_dyn_buffer_index];

	if ((dyn_ub->current_offset + MAX_UNIFORM_ALLOC) > (DYNAMIC_UNIFORM_BUFFER_SIZE_KB * 1024))
		Sys_Error("Out of dynamic uniform buffer space, increase DYNAMIC_UNIFORM_BUFFER_SIZE_KB");

	*buffer = dyn_ub->buffer;
	*buffer_offset = dyn_ub->current_offset;

	unsigned char *data = dyn_ub->data + dyn_ub->current_offset;
	dyn_ub->current_offset += aligned_size;

	*descriptor_set = ubo_descriptor_sets[current_dyn_buffer_index];

	return data;
}

/*
===============
R_InitDynamicBuffers
===============
*/
void R_InitDynamicBuffers()
{
	R_InitDynamicVertexBuffers();
	R_InitDynamicIndexBuffers();
	R_InitDynamicUniformBuffers();
}

/*
===============
R_CreateDescriptorSetLayouts
===============
*/
void R_CreateDescriptorSetLayouts()
{
	Con_Printf("Creating descriptor set layouts\n");

	VkResult err;

	VkDescriptorSetLayoutBinding single_texture_layout_binding;
	memset(&single_texture_layout_binding, 0, sizeof(single_texture_layout_binding));
	single_texture_layout_binding.binding = 0;
	single_texture_layout_binding.descriptorCount = 1;
	single_texture_layout_binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	single_texture_layout_binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

	VkDescriptorSetLayoutCreateInfo descriptor_set_layout_create_info;
	memset(&descriptor_set_layout_create_info, 0, sizeof(descriptor_set_layout_create_info));
	descriptor_set_layout_create_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	descriptor_set_layout_create_info.bindingCount = 1;
	descriptor_set_layout_create_info.pBindings = &single_texture_layout_binding;

	err = vkCreateDescriptorSetLayout(vulkan_globals.device, &descriptor_set_layout_create_info, NULL, &vulkan_globals.single_texture_set_layout);
	if (err != VK_SUCCESS)
		Sys_Error("vkCreateDescriptorSetLayout failed");

	VkDescriptorSetLayoutBinding ubo_sampler_layout_bindings;
	memset(&ubo_sampler_layout_bindings, 0, sizeof(ubo_sampler_layout_bindings));
	ubo_sampler_layout_bindings.binding = 0;
	ubo_sampler_layout_bindings.descriptorCount = 1;
	ubo_sampler_layout_bindings.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
	ubo_sampler_layout_bindings.stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS;

	descriptor_set_layout_create_info.bindingCount = 1;
	descriptor_set_layout_create_info.pBindings = &ubo_sampler_layout_bindings;
	
	err = vkCreateDescriptorSetLayout(vulkan_globals.device, &descriptor_set_layout_create_info, NULL, &vulkan_globals.ubo_set_layout);
	if (err != VK_SUCCESS)
		Sys_Error("vkCreateDescriptorSetLayout failed");

	VkDescriptorSetLayoutBinding input_attachment_layout_bindings;
	memset(&input_attachment_layout_bindings, 0, sizeof(input_attachment_layout_bindings));
	input_attachment_layout_bindings.binding = 0;
	input_attachment_layout_bindings.descriptorCount = 1;
	input_attachment_layout_bindings.descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
	input_attachment_layout_bindings.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

	descriptor_set_layout_create_info.bindingCount = 1;
	descriptor_set_layout_create_info.pBindings = &input_attachment_layout_bindings;
	
	err = vkCreateDescriptorSetLayout(vulkan_globals.device, &descriptor_set_layout_create_info, NULL, &vulkan_globals.input_attachment_set_layout);
	if (err != VK_SUCCESS)
		Sys_Error("vkCreateDescriptorSetLayout failed");
}

/*
===============
R_CreateDescriptorPool
===============
*/
void R_CreateDescriptorPool()
{
	VkDescriptorPoolSize pool_sizes[3];
	pool_sizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	pool_sizes[0].descriptorCount = MAX_GLTEXTURES;
	pool_sizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
	pool_sizes[1].descriptorCount = 16;
	pool_sizes[2].type = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
	pool_sizes[2].descriptorCount = 2;

	VkDescriptorPoolCreateInfo descriptor_pool_create_info;
	memset(&descriptor_pool_create_info, 0, sizeof(descriptor_pool_create_info));
	descriptor_pool_create_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	descriptor_pool_create_info.maxSets = MAX_GLTEXTURES + 16 + 2;
	descriptor_pool_create_info.poolSizeCount = 3;
	descriptor_pool_create_info.pPoolSizes = pool_sizes;
	descriptor_pool_create_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;

	vkCreateDescriptorPool(vulkan_globals.device, &descriptor_pool_create_info, NULL, &vulkan_globals.descriptor_pool);
}

/*
===============
R_CreatePipelineLayouts
===============
*/
void R_CreatePipelineLayouts()
{
	Con_Printf("Creating pipeline layouts\n");

	VkResult err;

	// Basic
	VkDescriptorSetLayout basic_descriptor_set_layouts[1] = { vulkan_globals.single_texture_set_layout };
	
	VkPushConstantRange push_constant_range;
	memset(&push_constant_range, 0, sizeof(push_constant_range));
	push_constant_range.offset = 0;
	push_constant_range.size = 20 * sizeof(float);
	push_constant_range.stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS;

	VkPipelineLayoutCreateInfo pipeline_layout_create_info;
	memset(&pipeline_layout_create_info, 0, sizeof(pipeline_layout_create_info));
	pipeline_layout_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipeline_layout_create_info.setLayoutCount = 1;
	pipeline_layout_create_info.pSetLayouts = basic_descriptor_set_layouts;
	pipeline_layout_create_info.pushConstantRangeCount = 1;
	pipeline_layout_create_info.pPushConstantRanges = &push_constant_range;

	err = vkCreatePipelineLayout(vulkan_globals.device, &pipeline_layout_create_info, NULL, &vulkan_globals.basic_pipeline_layout);
	if (err != VK_SUCCESS)
		Sys_Error("vkCreatePipelineLayout failed");

	// World
	VkDescriptorSetLayout world_descriptor_set_layouts[3] = {
		vulkan_globals.single_texture_set_layout,
		vulkan_globals.single_texture_set_layout,
		vulkan_globals.single_texture_set_layout
	};

	pipeline_layout_create_info.setLayoutCount = 3;
	pipeline_layout_create_info.pSetLayouts = world_descriptor_set_layouts;

	err = vkCreatePipelineLayout(vulkan_globals.device, &pipeline_layout_create_info, NULL, &vulkan_globals.world_pipeline_layout);
	if (err != VK_SUCCESS)
		Sys_Error("vkCreatePipelineLayout failed");

	// Alias
	VkDescriptorSetLayout alias_descriptor_set_layouts[3] = {
		vulkan_globals.single_texture_set_layout,
		vulkan_globals.single_texture_set_layout,
		vulkan_globals.ubo_set_layout
	};

	pipeline_layout_create_info.setLayoutCount = 3;
	pipeline_layout_create_info.pSetLayouts = alias_descriptor_set_layouts;

	err = vkCreatePipelineLayout(vulkan_globals.device, &pipeline_layout_create_info, NULL, &vulkan_globals.alias_pipeline_layout);
	if (err != VK_SUCCESS)
		Sys_Error("vkCreatePipelineLayout failed");

	// Sky
	VkDescriptorSetLayout sky_layer_descriptor_set_layouts[2] = {
		vulkan_globals.single_texture_set_layout,
		vulkan_globals.single_texture_set_layout,
	};

	pipeline_layout_create_info.setLayoutCount = 2;
	pipeline_layout_create_info.pSetLayouts = sky_layer_descriptor_set_layouts;

	err = vkCreatePipelineLayout(vulkan_globals.device, &pipeline_layout_create_info, NULL, &vulkan_globals.sky_layer_pipeline_layout);
	if (err != VK_SUCCESS)
		Sys_Error("vkCreatePipelineLayout failed");

	// Postprocess
	VkDescriptorSetLayout postprocess_descriptor_set_layouts[1] = {
		vulkan_globals.input_attachment_set_layout,
	};

	memset(&push_constant_range, 0, sizeof(push_constant_range));
	push_constant_range.offset = 0;
	push_constant_range.size = 2 * sizeof(float);
	push_constant_range.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

	pipeline_layout_create_info.setLayoutCount = 1;
	pipeline_layout_create_info.pSetLayouts = postprocess_descriptor_set_layouts;
	pipeline_layout_create_info.pushConstantRangeCount = 1;
	pipeline_layout_create_info.pPushConstantRanges = &push_constant_range;

	err = vkCreatePipelineLayout(vulkan_globals.device, &pipeline_layout_create_info, NULL, &vulkan_globals.postprocess_pipeline_layout);
	if (err != VK_SUCCESS)
		Sys_Error("vkCreatePipelineLayout failed");
}

/*
===============
R_InitSamplers
===============
*/
void R_InitSamplers()
{
	Con_Printf("Initializing samplers\n");

	VkResult err;

	if (vulkan_globals.point_sampler == VK_NULL_HANDLE)
	{
		VkSamplerCreateInfo sampler_create_info;
		memset(&sampler_create_info, 0, sizeof(sampler_create_info));
		sampler_create_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		sampler_create_info.magFilter = VK_FILTER_NEAREST;
		sampler_create_info.minFilter = VK_FILTER_NEAREST;
		sampler_create_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		sampler_create_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		sampler_create_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		sampler_create_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		sampler_create_info.mipLodBias = 0.0f;
		sampler_create_info.maxAnisotropy = 1.0f;
		sampler_create_info.minLod = 0;
		sampler_create_info.maxLod = FLT_MAX;

		err = vkCreateSampler(vulkan_globals.device, &sampler_create_info, NULL, &vulkan_globals.point_sampler);
		if (err != VK_SUCCESS)
			Sys_Error("vkCreateSampler failed");

		GL_SetObjectName((uint64_t)vulkan_globals.point_sampler, VK_DEBUG_REPORT_OBJECT_TYPE_SAMPLER_EXT, "point");

		sampler_create_info.anisotropyEnable = VK_TRUE;
		sampler_create_info.maxAnisotropy = vulkan_globals.device_properties.limits.maxSamplerAnisotropy;
		err = vkCreateSampler(vulkan_globals.device, &sampler_create_info, NULL, &vulkan_globals.point_aniso_sampler);
		if (err != VK_SUCCESS)
			Sys_Error("vkCreateSampler failed");

		GL_SetObjectName((uint64_t)vulkan_globals.point_aniso_sampler, VK_DEBUG_REPORT_OBJECT_TYPE_SAMPLER_EXT, "point_aniso");

		sampler_create_info.magFilter = VK_FILTER_LINEAR;
		sampler_create_info.minFilter = VK_FILTER_LINEAR;
		sampler_create_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		sampler_create_info.anisotropyEnable = VK_FALSE;
		sampler_create_info.maxAnisotropy = 0.0f;

		err = vkCreateSampler(vulkan_globals.device, &sampler_create_info, NULL, &vulkan_globals.linear_sampler);
		if (err != VK_SUCCESS)
			Sys_Error("vkCreateSampler failed");

		GL_SetObjectName((uint64_t)vulkan_globals.linear_sampler, VK_DEBUG_REPORT_OBJECT_TYPE_SAMPLER_EXT, "linear");

		sampler_create_info.anisotropyEnable = VK_TRUE;
		sampler_create_info.maxAnisotropy = vulkan_globals.device_properties.limits.maxSamplerAnisotropy;
		err = vkCreateSampler(vulkan_globals.device, &sampler_create_info, NULL, &vulkan_globals.linear_aniso_sampler);
		if (err != VK_SUCCESS)
			Sys_Error("vkCreateSampler failed");

		GL_SetObjectName((uint64_t)vulkan_globals.linear_aniso_sampler, VK_DEBUG_REPORT_OBJECT_TYPE_SAMPLER_EXT, "linear_aniso");
	}

	TexMgr_UpdateTextureDescriptorSets();
}

/*
===============
R_CreateShaderModule
===============
*/
static VkShaderModule R_CreateShaderModule(byte *code, int size)
{
	VkShaderModuleCreateInfo module_create_info;
	memset(&module_create_info, 0, sizeof(module_create_info));
	module_create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	module_create_info.pNext = NULL;
	module_create_info.codeSize = size;
	module_create_info.pCode = (void*)code;

	VkShaderModule module;
	VkResult err = vkCreateShaderModule(vulkan_globals.device, &module_create_info, NULL, &module);
	if (err != VK_SUCCESS)
		Sys_Error("vkCreateShaderModule failed");

	return module;
}

/*
===============
R_CreatePipelines
===============
*/
void R_CreatePipelines()
{
	Con_Printf("Creating pipelines\n");

	VkResult err;

	VkShaderModule basic_vert_module = R_CreateShaderModule(basic_vert_spv, basic_vert_spv_size);
	VkShaderModule basic_frag_module = R_CreateShaderModule(basic_frag_spv, basic_frag_spv_size);
	VkShaderModule basic_alphatest_frag_module = R_CreateShaderModule(basic_alphatest_frag_spv, basic_alphatest_frag_spv_size);
	VkShaderModule basic_char_frag_module = R_CreateShaderModule(basic_char_frag_spv, basic_char_frag_spv_size);
	VkShaderModule basic_notex_frag_module = R_CreateShaderModule(basic_notex_frag_spv, basic_notex_frag_spv_size);
	VkShaderModule world_vert_module = R_CreateShaderModule(world_vert_spv, world_vert_spv_size);
	VkShaderModule world_frag_module = R_CreateShaderModule(world_frag_spv, world_frag_spv_size);
	VkShaderModule alias_vert_module = R_CreateShaderModule(alias_vert_spv, alias_vert_spv_size);
	VkShaderModule alias_frag_module = R_CreateShaderModule(alias_frag_spv, alias_frag_spv_size);
	VkShaderModule sky_layer_vert_module = R_CreateShaderModule(sky_layer_vert_spv, sky_layer_vert_spv_size);
	VkShaderModule sky_layer_frag_module = R_CreateShaderModule(sky_layer_frag_spv, sky_layer_frag_spv_size);
	VkShaderModule postprocess_vert_module = R_CreateShaderModule(postprocess_vert_spv, postprocess_vert_spv_size);
	VkShaderModule postprocess_frag_module = R_CreateShaderModule(postprocess_frag_spv, postprocess_frag_spv_size);

	VkPipelineDynamicStateCreateInfo dynamic_state_create_info;
	memset(&dynamic_state_create_info, 0, sizeof(dynamic_state_create_info));
	dynamic_state_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	VkDynamicState dynamic_states[VK_DYNAMIC_STATE_RANGE_SIZE];
	dynamic_state_create_info.pDynamicStates = dynamic_states;

	VkPipelineShaderStageCreateInfo shader_stages[2];
	memset(&shader_stages, 0, 2 * sizeof(VkPipelineShaderStageCreateInfo));

	shader_stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	shader_stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	shader_stages[0].module = basic_vert_module;
	shader_stages[0].pName = "main";

	shader_stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	shader_stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	shader_stages[1].module = basic_alphatest_frag_module;
	shader_stages[1].pName = "main";

	VkVertexInputAttributeDescription basic_vertex_input_attribute_descriptions[3];
	basic_vertex_input_attribute_descriptions[0].binding = 0;
	basic_vertex_input_attribute_descriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
	basic_vertex_input_attribute_descriptions[0].location = 0;
	basic_vertex_input_attribute_descriptions[0].offset = 0;
	basic_vertex_input_attribute_descriptions[1].binding = 0;
	basic_vertex_input_attribute_descriptions[1].format = VK_FORMAT_R32G32_SFLOAT;
	basic_vertex_input_attribute_descriptions[1].location = 1;
	basic_vertex_input_attribute_descriptions[1].offset = 12;
	basic_vertex_input_attribute_descriptions[2].binding = 0;
	basic_vertex_input_attribute_descriptions[2].format = VK_FORMAT_R8G8B8A8_UNORM;
	basic_vertex_input_attribute_descriptions[2].location = 2;
	basic_vertex_input_attribute_descriptions[2].offset = 20;

	VkVertexInputBindingDescription basic_vertex_binding_description;
	basic_vertex_binding_description.binding = 0;
	basic_vertex_binding_description.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
	basic_vertex_binding_description.stride = 24;

	VkPipelineVertexInputStateCreateInfo vertex_input_state_create_info;
	memset(&vertex_input_state_create_info, 0, sizeof(vertex_input_state_create_info));
	vertex_input_state_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vertex_input_state_create_info.vertexAttributeDescriptionCount = 3;
	vertex_input_state_create_info.pVertexAttributeDescriptions = basic_vertex_input_attribute_descriptions;
	vertex_input_state_create_info.vertexBindingDescriptionCount = 1;
	vertex_input_state_create_info.pVertexBindingDescriptions = &basic_vertex_binding_description;

	VkPipelineInputAssemblyStateCreateInfo input_assembly_state_create_info;
	memset(&input_assembly_state_create_info, 0, sizeof(input_assembly_state_create_info));
	input_assembly_state_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	input_assembly_state_create_info.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

	VkPipelineViewportStateCreateInfo viewport_state_create_info;
	memset(&viewport_state_create_info, 0, sizeof(viewport_state_create_info));
	viewport_state_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewport_state_create_info.viewportCount = 1;
	dynamic_states[dynamic_state_create_info.dynamicStateCount++] = VK_DYNAMIC_STATE_VIEWPORT;
	viewport_state_create_info.scissorCount = 1;
	dynamic_states[dynamic_state_create_info.dynamicStateCount++] = VK_DYNAMIC_STATE_SCISSOR;

	VkPipelineRasterizationStateCreateInfo rasterization_state_create_info;
	memset(&rasterization_state_create_info, 0, sizeof(rasterization_state_create_info));
	rasterization_state_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rasterization_state_create_info.polygonMode = VK_POLYGON_MODE_FILL;
	rasterization_state_create_info.cullMode = VK_CULL_MODE_NONE;
	rasterization_state_create_info.frontFace = VK_FRONT_FACE_CLOCKWISE;
	rasterization_state_create_info.depthClampEnable = VK_FALSE;
	rasterization_state_create_info.rasterizerDiscardEnable = VK_FALSE;
	rasterization_state_create_info.depthBiasEnable = VK_FALSE;
	rasterization_state_create_info.lineWidth = 1.0f;

	VkPipelineMultisampleStateCreateInfo multisample_state_create_info;
	memset(&multisample_state_create_info, 0, sizeof(multisample_state_create_info));
	multisample_state_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisample_state_create_info.rasterizationSamples = vulkan_globals.sample_count;

	VkPipelineDepthStencilStateCreateInfo depth_stencil_state_create_info;
	memset(&depth_stencil_state_create_info, 0, sizeof(depth_stencil_state_create_info));
	depth_stencil_state_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	depth_stencil_state_create_info.depthTestEnable = VK_FALSE;
	depth_stencil_state_create_info.depthWriteEnable = VK_FALSE;
	depth_stencil_state_create_info.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
	depth_stencil_state_create_info.depthBoundsTestEnable = VK_FALSE;
	depth_stencil_state_create_info.back.failOp = VK_STENCIL_OP_KEEP;
	depth_stencil_state_create_info.back.passOp = VK_STENCIL_OP_KEEP;
	depth_stencil_state_create_info.back.compareOp = VK_COMPARE_OP_ALWAYS;
	depth_stencil_state_create_info.stencilTestEnable = VK_FALSE;
	depth_stencil_state_create_info.front = depth_stencil_state_create_info.back;

	VkPipelineColorBlendStateCreateInfo color_blend_state_create_info;
	VkPipelineColorBlendAttachmentState blend_attachment_state;
	memset(&color_blend_state_create_info, 0, sizeof(color_blend_state_create_info));
	color_blend_state_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	memset(&blend_attachment_state, 0, sizeof(blend_attachment_state));
	blend_attachment_state.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	blend_attachment_state.blendEnable = VK_FALSE;
	color_blend_state_create_info.attachmentCount = 1;
	color_blend_state_create_info.pAttachments = &blend_attachment_state;

	VkGraphicsPipelineCreateInfo pipeline_create_info;
	memset(&pipeline_create_info, 0, sizeof(pipeline_create_info));
	pipeline_create_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pipeline_create_info.stageCount = 2;
	pipeline_create_info.pStages = shader_stages;
	pipeline_create_info.pVertexInputState = &vertex_input_state_create_info;
	pipeline_create_info.pInputAssemblyState = &input_assembly_state_create_info;
	pipeline_create_info.pViewportState = &viewport_state_create_info;
	pipeline_create_info.pRasterizationState = &rasterization_state_create_info;
	pipeline_create_info.pMultisampleState = &multisample_state_create_info;
	pipeline_create_info.pDepthStencilState = &depth_stencil_state_create_info;
	pipeline_create_info.pColorBlendState = &color_blend_state_create_info;
	pipeline_create_info.pDynamicState = &dynamic_state_create_info;
	pipeline_create_info.layout = vulkan_globals.basic_pipeline_layout;
	pipeline_create_info.renderPass = vulkan_globals.main_render_pass;

	//================
	// Basic pipelines
	//================
	input_assembly_state_create_info.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

	err = vkCreateGraphicsPipelines(vulkan_globals.device, VK_NULL_HANDLE, 1, &pipeline_create_info, NULL, &vulkan_globals.basic_alphatest_pipeline);
	if (err != VK_SUCCESS)
		Sys_Error("vkCreateGraphicsPipelines failed");

	GL_SetObjectName((uint64_t)vulkan_globals.basic_alphatest_pipeline, VK_DEBUG_REPORT_OBJECT_TYPE_PIPELINE_EXT, "basic_alphatest");

	shader_stages[1].module = basic_char_frag_module;

	err = vkCreateGraphicsPipelines(vulkan_globals.device, VK_NULL_HANDLE, 1, &pipeline_create_info, NULL, &vulkan_globals.basic_char_pipeline);
	if (err != VK_SUCCESS)
		Sys_Error("vkCreateGraphicsPipelines failed");

	GL_SetObjectName((uint64_t)vulkan_globals.basic_char_pipeline, VK_DEBUG_REPORT_OBJECT_TYPE_PIPELINE_EXT, "basic_char");

	shader_stages[1].module = basic_notex_frag_module;

	blend_attachment_state.blendEnable = VK_TRUE;
	blend_attachment_state.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
	blend_attachment_state.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	blend_attachment_state.colorBlendOp = VK_BLEND_OP_ADD;
	blend_attachment_state.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
	blend_attachment_state.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	blend_attachment_state.alphaBlendOp = VK_BLEND_OP_ADD;

	err = vkCreateGraphicsPipelines(vulkan_globals.device, VK_NULL_HANDLE, 1, &pipeline_create_info, NULL, &vulkan_globals.basic_notex_blend_pipeline);
	if (err != VK_SUCCESS)
		Sys_Error("vkCreateGraphicsPipelines failed");

	GL_SetObjectName((uint64_t)vulkan_globals.basic_notex_blend_pipeline, VK_DEBUG_REPORT_OBJECT_TYPE_PIPELINE_EXT, "basic_notex_blend");

	input_assembly_state_create_info.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;

	err = vkCreateGraphicsPipelines(vulkan_globals.device, VK_NULL_HANDLE, 1, &pipeline_create_info, NULL, &vulkan_globals.basic_poly_blend_pipeline);
	if (err != VK_SUCCESS)
		Sys_Error("vkCreateGraphicsPipelines failed");

	GL_SetObjectName((uint64_t)vulkan_globals.basic_poly_blend_pipeline, VK_DEBUG_REPORT_OBJECT_TYPE_PIPELINE_EXT, "basic_poly_blend");

	input_assembly_state_create_info.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

	shader_stages[1].module = basic_frag_module;

	err = vkCreateGraphicsPipelines(vulkan_globals.device, VK_NULL_HANDLE, 1, &pipeline_create_info, NULL, &vulkan_globals.basic_blend_pipeline);
	if (err != VK_SUCCESS)
		Sys_Error("vkCreateGraphicsPipelines failed");

	GL_SetObjectName((uint64_t)vulkan_globals.basic_blend_pipeline, VK_DEBUG_REPORT_OBJECT_TYPE_PIPELINE_EXT, "basic_blend");

	//================
	// Warp
	//================
	multisample_state_create_info.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	input_assembly_state_create_info.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;

	blend_attachment_state.blendEnable = VK_FALSE;

	shader_stages[0].module = basic_vert_module;
	shader_stages[1].module = basic_frag_module;

	pipeline_create_info.renderPass = vulkan_globals.warp_render_pass;

	err = vkCreateGraphicsPipelines(vulkan_globals.device, VK_NULL_HANDLE, 1, &pipeline_create_info, NULL, &vulkan_globals.warp_pipeline);
	if (err != VK_SUCCESS)
		Sys_Error("vkCreateGraphicsPipelines failed");

	GL_SetObjectName((uint64_t)vulkan_globals.warp_pipeline, VK_DEBUG_REPORT_OBJECT_TYPE_PIPELINE_EXT, "warp");

	//================
	// Particles
	//================
	multisample_state_create_info.rasterizationSamples = vulkan_globals.sample_count;

	input_assembly_state_create_info.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	
	depth_stencil_state_create_info.depthTestEnable = VK_TRUE;
	depth_stencil_state_create_info.depthWriteEnable = VK_FALSE;

	pipeline_create_info.renderPass = vulkan_globals.main_render_pass;

	blend_attachment_state.blendEnable = VK_TRUE;

	err = vkCreateGraphicsPipelines(vulkan_globals.device, VK_NULL_HANDLE, 1, &pipeline_create_info, NULL, &vulkan_globals.particle_pipeline);
	if (err != VK_SUCCESS)
		Sys_Error("vkCreateGraphicsPipelines failed");

	//================
	// Water
	//================
	input_assembly_state_create_info.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;
	depth_stencil_state_create_info.depthTestEnable = VK_TRUE;
	depth_stencil_state_create_info.depthWriteEnable = VK_TRUE;
	depth_stencil_state_create_info.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
	blend_attachment_state.blendEnable = VK_FALSE;

	err = vkCreateGraphicsPipelines(vulkan_globals.device, VK_NULL_HANDLE, 1, &pipeline_create_info, NULL, &vulkan_globals.water_pipeline);
	if (err != VK_SUCCESS)
		Sys_Error("vkCreateGraphicsPipelines failed");

	GL_SetObjectName((uint64_t)vulkan_globals.water_pipeline, VK_DEBUG_REPORT_OBJECT_TYPE_PIPELINE_EXT, "water");

	depth_stencil_state_create_info.depthWriteEnable = VK_FALSE;
	blend_attachment_state.blendEnable = VK_TRUE;

	err = vkCreateGraphicsPipelines(vulkan_globals.device, VK_NULL_HANDLE, 1, &pipeline_create_info, NULL, &vulkan_globals.water_blend_pipeline);
	if (err != VK_SUCCESS)
		Sys_Error("vkCreateGraphicsPipelines failed");

	GL_SetObjectName((uint64_t)vulkan_globals.water_blend_pipeline, VK_DEBUG_REPORT_OBJECT_TYPE_PIPELINE_EXT, "water_blend");
	
	//================
	// Sprites
	//================
	shader_stages[1].module = basic_alphatest_frag_module;
	blend_attachment_state.blendEnable = VK_FALSE;

	dynamic_states[dynamic_state_create_info.dynamicStateCount++] = VK_DYNAMIC_STATE_DEPTH_BIAS;

	err = vkCreateGraphicsPipelines(vulkan_globals.device, VK_NULL_HANDLE, 1, &pipeline_create_info, NULL, &vulkan_globals.sprite_pipeline);
	if (err != VK_SUCCESS)
		Sys_Error("vkCreateGraphicsPipelines failed");

	GL_SetObjectName((uint64_t)vulkan_globals.sprite_pipeline, VK_DEBUG_REPORT_OBJECT_TYPE_PIPELINE_EXT, "sprite");

	dynamic_state_create_info.dynamicStateCount--;

	//================
	// Sky
	//================
	pipeline_create_info.renderPass = vulkan_globals.main_render_pass;

	shader_stages[1].module = basic_notex_frag_module;

	depth_stencil_state_create_info.depthTestEnable = VK_TRUE;
	depth_stencil_state_create_info.depthWriteEnable = VK_FALSE;
	depth_stencil_state_create_info.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

	err = vkCreateGraphicsPipelines(vulkan_globals.device, VK_NULL_HANDLE, 1, &pipeline_create_info, NULL, &vulkan_globals.sky_color_pipeline);
	if (err != VK_SUCCESS)
		Sys_Error("vkCreateGraphicsPipelines failed");

	GL_SetObjectName((uint64_t)vulkan_globals.sky_color_pipeline, VK_DEBUG_REPORT_OBJECT_TYPE_PIPELINE_EXT, "sky_color");

	shader_stages[1].module = basic_frag_module;

	err = vkCreateGraphicsPipelines(vulkan_globals.device, VK_NULL_HANDLE, 1, &pipeline_create_info, NULL, &vulkan_globals.sky_box_pipeline);
	if (err != VK_SUCCESS)
		Sys_Error("vkCreateGraphicsPipelines failed");

	GL_SetObjectName((uint64_t)vulkan_globals.sky_box_pipeline, VK_DEBUG_REPORT_OBJECT_TYPE_PIPELINE_EXT, "sky_box");

	VkVertexInputAttributeDescription sky_layer_vertex_input_attribute_descriptions[4];
	sky_layer_vertex_input_attribute_descriptions[0].binding = 0;
	sky_layer_vertex_input_attribute_descriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
	sky_layer_vertex_input_attribute_descriptions[0].location = 0;
	sky_layer_vertex_input_attribute_descriptions[0].offset = 0;
	sky_layer_vertex_input_attribute_descriptions[1].binding = 0;
	sky_layer_vertex_input_attribute_descriptions[1].format = VK_FORMAT_R32G32_SFLOAT;
	sky_layer_vertex_input_attribute_descriptions[1].location = 1;
	sky_layer_vertex_input_attribute_descriptions[1].offset = 12;
	sky_layer_vertex_input_attribute_descriptions[2].binding = 0;
	sky_layer_vertex_input_attribute_descriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
	sky_layer_vertex_input_attribute_descriptions[2].location = 2;
	sky_layer_vertex_input_attribute_descriptions[2].offset = 20;
	sky_layer_vertex_input_attribute_descriptions[3].binding = 0;
	sky_layer_vertex_input_attribute_descriptions[3].format = VK_FORMAT_R8G8B8A8_UNORM;
	sky_layer_vertex_input_attribute_descriptions[3].location = 3;
	sky_layer_vertex_input_attribute_descriptions[3].offset = 28;

	VkVertexInputBindingDescription sky_layer_vertex_binding_description;
	sky_layer_vertex_binding_description.binding = 0;
	sky_layer_vertex_binding_description.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
	sky_layer_vertex_binding_description.stride = 32;

	vertex_input_state_create_info.vertexAttributeDescriptionCount = 4;
	vertex_input_state_create_info.pVertexAttributeDescriptions = sky_layer_vertex_input_attribute_descriptions;
	vertex_input_state_create_info.vertexBindingDescriptionCount = 1;
	vertex_input_state_create_info.pVertexBindingDescriptions = &sky_layer_vertex_binding_description;

	shader_stages[0].module = sky_layer_vert_module;
	shader_stages[1].module = sky_layer_frag_module;
	blend_attachment_state.blendEnable = VK_FALSE;

	pipeline_create_info.layout = vulkan_globals.sky_layer_pipeline_layout;

	err = vkCreateGraphicsPipelines(vulkan_globals.device, VK_NULL_HANDLE, 1, &pipeline_create_info, NULL, &vulkan_globals.sky_layer_pipeline);
	if (err != VK_SUCCESS)
		Sys_Error("vkCreateGraphicsPipelines failed");

	GL_SetObjectName((uint64_t)vulkan_globals.sky_layer_pipeline, VK_DEBUG_REPORT_OBJECT_TYPE_PIPELINE_EXT, "sky_layer");

	//================
	// World pipelines
	//================
	rasterization_state_create_info.cullMode = VK_CULL_MODE_BACK_BIT;
	depth_stencil_state_create_info.depthTestEnable = VK_TRUE;
	depth_stencil_state_create_info.depthWriteEnable = VK_TRUE;

	input_assembly_state_create_info.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

	VkVertexInputAttributeDescription world_vertex_input_attribute_descriptions[3];
	world_vertex_input_attribute_descriptions[0].binding = 0;
	world_vertex_input_attribute_descriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
	world_vertex_input_attribute_descriptions[0].location = 0;
	world_vertex_input_attribute_descriptions[0].offset = 0;
	world_vertex_input_attribute_descriptions[1].binding = 0;
	world_vertex_input_attribute_descriptions[1].format = VK_FORMAT_R32G32_SFLOAT;
	world_vertex_input_attribute_descriptions[1].location = 1;
	world_vertex_input_attribute_descriptions[1].offset = 12;
	world_vertex_input_attribute_descriptions[2].binding = 0;
	world_vertex_input_attribute_descriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
	world_vertex_input_attribute_descriptions[2].location = 2;
	world_vertex_input_attribute_descriptions[2].offset = 20;

	VkVertexInputBindingDescription world_vertex_binding_description;
	world_vertex_binding_description.binding = 0;
	world_vertex_binding_description.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
	world_vertex_binding_description.stride = 28;

	vertex_input_state_create_info.vertexAttributeDescriptionCount = 3;
	vertex_input_state_create_info.pVertexAttributeDescriptions = world_vertex_input_attribute_descriptions;
	vertex_input_state_create_info.vertexBindingDescriptionCount = 1;
	vertex_input_state_create_info.pVertexBindingDescriptions = &world_vertex_binding_description;

	VkSpecializationMapEntry specialization_entries[2];
	specialization_entries[0].constantID = 0;
	specialization_entries[0].offset = 0;
	specialization_entries[0].size = 4;
	specialization_entries[1].constantID = 1;
	specialization_entries[1].offset = 4;
	specialization_entries[1].size = 4;

	uint32_t specialization_data[2];
	specialization_data[0] = 0;
	specialization_data[1] = 0;

	VkSpecializationInfo specialization_info;
	specialization_info.mapEntryCount = 2;
	specialization_info.pMapEntries = specialization_entries;
	specialization_info.dataSize = 8;
	specialization_info.pData = specialization_data;
	
	pipeline_create_info.layout = vulkan_globals.world_pipeline_layout;

	shader_stages[0].module = world_vert_module;
	shader_stages[1].module = world_frag_module;
	shader_stages[1].pSpecializationInfo = &specialization_info;

	blend_attachment_state.blendEnable = VK_FALSE;

	err = vkCreateGraphicsPipelines(vulkan_globals.device, VK_NULL_HANDLE, 1, &pipeline_create_info, NULL, &vulkan_globals.world_pipelines[world_pipeline_base]);
	if (err != VK_SUCCESS)
		Sys_Error("vkCreateGraphicsPipelines failed");

	GL_SetObjectName((uint64_t)vulkan_globals.world_pipelines[world_pipeline_base], VK_DEBUG_REPORT_OBJECT_TYPE_PIPELINE_EXT, "world_base");

	specialization_data[0] = 1;
	specialization_data[1] = 0;

	err = vkCreateGraphicsPipelines(vulkan_globals.device, VK_NULL_HANDLE, 1, &pipeline_create_info, NULL, &vulkan_globals.world_pipelines[world_pipeline_fullbright]);
	if (err != VK_SUCCESS)
		Sys_Error("vkCreateGraphicsPipelines failed");

	GL_SetObjectName((uint64_t)vulkan_globals.world_pipelines[world_pipeline_fullbright], VK_DEBUG_REPORT_OBJECT_TYPE_PIPELINE_EXT, "world_fullbright");

	specialization_data[0] = 0;
	specialization_data[1] = 1;

	err = vkCreateGraphicsPipelines(vulkan_globals.device, VK_NULL_HANDLE, 1, &pipeline_create_info, NULL, &vulkan_globals.world_pipelines[world_pipeline_alpha_test]);
	if (err != VK_SUCCESS)
		Sys_Error("vkCreateGraphicsPipelines failed");

	GL_SetObjectName((uint64_t)vulkan_globals.world_pipelines[world_pipeline_alpha_test], VK_DEBUG_REPORT_OBJECT_TYPE_PIPELINE_EXT, "world_alpha_test");

	specialization_data[0] = 1;
	specialization_data[1] = 1;

	err = vkCreateGraphicsPipelines(vulkan_globals.device, VK_NULL_HANDLE, 1, &pipeline_create_info, NULL, &vulkan_globals.world_pipelines[world_pipeline_fullbright_alpha_test]);
	if (err != VK_SUCCESS)
		Sys_Error("vkCreateGraphicsPipelines failed");

	GL_SetObjectName((uint64_t)vulkan_globals.world_pipelines[world_pipeline_fullbright_alpha_test], VK_DEBUG_REPORT_OBJECT_TYPE_PIPELINE_EXT, "world_fullbright_alpha_test");

	shader_stages[1].pSpecializationInfo = NULL;

	//================
	// Alias pipeline
	//================
	VkVertexInputAttributeDescription alias_vertex_input_attribute_descriptions[5];
	alias_vertex_input_attribute_descriptions[0].binding = 0;
	alias_vertex_input_attribute_descriptions[0].format = VK_FORMAT_R32G32_SFLOAT;
	alias_vertex_input_attribute_descriptions[0].location = 0;
	alias_vertex_input_attribute_descriptions[0].offset = 0;
	alias_vertex_input_attribute_descriptions[1].binding = 1;
	alias_vertex_input_attribute_descriptions[1].format = VK_FORMAT_R8G8B8A8_USCALED;
	alias_vertex_input_attribute_descriptions[1].location = 1;
	alias_vertex_input_attribute_descriptions[1].offset = 0;
	alias_vertex_input_attribute_descriptions[2].binding = 1;
	alias_vertex_input_attribute_descriptions[2].format = VK_FORMAT_R8G8B8A8_SNORM;
	alias_vertex_input_attribute_descriptions[2].location = 2;
	alias_vertex_input_attribute_descriptions[2].offset = 4;
	alias_vertex_input_attribute_descriptions[3].binding = 2;
	alias_vertex_input_attribute_descriptions[3].format = VK_FORMAT_R8G8B8A8_USCALED;
	alias_vertex_input_attribute_descriptions[3].location = 3;
	alias_vertex_input_attribute_descriptions[3].offset = 0;
	alias_vertex_input_attribute_descriptions[4].binding = 2;
	alias_vertex_input_attribute_descriptions[4].format = VK_FORMAT_R8G8B8A8_SNORM;
	alias_vertex_input_attribute_descriptions[4].location = 4;
	alias_vertex_input_attribute_descriptions[4].offset = 4;

	VkVertexInputBindingDescription alias_vertex_binding_descriptions[3];
	alias_vertex_binding_descriptions[0].binding = 0;
	alias_vertex_binding_descriptions[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
	alias_vertex_binding_descriptions[0].stride = 8;
	alias_vertex_binding_descriptions[1].binding = 1;
	alias_vertex_binding_descriptions[1].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
	alias_vertex_binding_descriptions[1].stride = 8;
	alias_vertex_binding_descriptions[2].binding = 2;
	alias_vertex_binding_descriptions[2].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
	alias_vertex_binding_descriptions[2].stride = 8;

	vertex_input_state_create_info.vertexAttributeDescriptionCount = 5;
	vertex_input_state_create_info.pVertexAttributeDescriptions = alias_vertex_input_attribute_descriptions;
	vertex_input_state_create_info.vertexBindingDescriptionCount = 3;
	vertex_input_state_create_info.pVertexBindingDescriptions = alias_vertex_binding_descriptions;

	shader_stages[0].module = alias_vert_module;
	shader_stages[1].module = alias_frag_module;

	pipeline_create_info.layout = vulkan_globals.alias_pipeline_layout;

	err = vkCreateGraphicsPipelines(vulkan_globals.device, VK_NULL_HANDLE, 1, &pipeline_create_info, NULL, &vulkan_globals.alias_pipeline);
	if (err != VK_SUCCESS)
		Sys_Error("vkCreateGraphicsPipelines failed");

	GL_SetObjectName((uint64_t)vulkan_globals.alias_pipeline, VK_DEBUG_REPORT_OBJECT_TYPE_PIPELINE_EXT, "alias");

	depth_stencil_state_create_info.depthWriteEnable = VK_FALSE;
	blend_attachment_state.blendEnable = VK_TRUE;

	err = vkCreateGraphicsPipelines(vulkan_globals.device, VK_NULL_HANDLE, 1, &pipeline_create_info, NULL, &vulkan_globals.alias_blend_pipeline);
	if (err != VK_SUCCESS)
		Sys_Error("vkCreateGraphicsPipelines failed");

	GL_SetObjectName((uint64_t)vulkan_globals.alias_blend_pipeline, VK_DEBUG_REPORT_OBJECT_TYPE_PIPELINE_EXT, "alias_blend");

	//================
	// Postprocess pipeline
	//================
	multisample_state_create_info.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
	rasterization_state_create_info.cullMode = VK_CULL_MODE_NONE;
	depth_stencil_state_create_info.depthTestEnable = VK_FALSE;
	blend_attachment_state.blendEnable = VK_FALSE;

	vertex_input_state_create_info.vertexAttributeDescriptionCount = 0;
	vertex_input_state_create_info.pVertexAttributeDescriptions = NULL;
	vertex_input_state_create_info.vertexBindingDescriptionCount = 0;
	vertex_input_state_create_info.pVertexBindingDescriptions = NULL;

	shader_stages[0].module = postprocess_vert_module;
	shader_stages[1].module = postprocess_frag_module;
	pipeline_create_info.layout = vulkan_globals.postprocess_pipeline_layout;
	pipeline_create_info.subpass = 1;

	err = vkCreateGraphicsPipelines(vulkan_globals.device, VK_NULL_HANDLE, 1, &pipeline_create_info, NULL, &vulkan_globals.postprocess_pipeline);
	if (err != VK_SUCCESS)
		Sys_Error("vkCreateGraphicsPipelines failed");

	GL_SetObjectName((uint64_t)vulkan_globals.postprocess_pipeline, VK_DEBUG_REPORT_OBJECT_TYPE_PIPELINE_EXT, "postprocess");

	vkDestroyShaderModule(vulkan_globals.device, postprocess_frag_module, NULL);
	vkDestroyShaderModule(vulkan_globals.device, postprocess_vert_module, NULL);
	vkDestroyShaderModule(vulkan_globals.device, sky_layer_frag_module, NULL);
	vkDestroyShaderModule(vulkan_globals.device, sky_layer_vert_module, NULL);
	vkDestroyShaderModule(vulkan_globals.device, alias_frag_module, NULL);
	vkDestroyShaderModule(vulkan_globals.device, alias_vert_module, NULL);
	vkDestroyShaderModule(vulkan_globals.device, world_frag_module, NULL);
	vkDestroyShaderModule(vulkan_globals.device, world_vert_module, NULL);
	vkDestroyShaderModule(vulkan_globals.device, basic_notex_frag_module, NULL);
	vkDestroyShaderModule(vulkan_globals.device, basic_alphatest_frag_module, NULL);
	vkDestroyShaderModule(vulkan_globals.device, basic_char_frag_module, NULL);
	vkDestroyShaderModule(vulkan_globals.device, basic_frag_module, NULL);
	vkDestroyShaderModule(vulkan_globals.device, basic_vert_module, NULL);
}

/*
===============
R_DestroyPipelines
===============
*/
void R_DestroyPipelines(void)
{
	int i;
	vkDestroyPipeline(vulkan_globals.device, vulkan_globals.basic_char_pipeline, NULL);
	vkDestroyPipeline(vulkan_globals.device, vulkan_globals.basic_alphatest_pipeline, NULL);
	vkDestroyPipeline(vulkan_globals.device, vulkan_globals.basic_blend_pipeline, NULL);
	vkDestroyPipeline(vulkan_globals.device, vulkan_globals.basic_notex_blend_pipeline, NULL);
	vkDestroyPipeline(vulkan_globals.device, vulkan_globals.basic_poly_blend_pipeline, NULL);
	for (i = 0; i < world_pipeline_count; ++i)
		vkDestroyPipeline(vulkan_globals.device, vulkan_globals.world_pipelines[i], NULL);
	vkDestroyPipeline(vulkan_globals.device, vulkan_globals.water_pipeline, NULL);
	vkDestroyPipeline(vulkan_globals.device, vulkan_globals.water_blend_pipeline, NULL);
	vkDestroyPipeline(vulkan_globals.device, vulkan_globals.warp_pipeline, NULL);
	vkDestroyPipeline(vulkan_globals.device, vulkan_globals.particle_pipeline, NULL);
	vkDestroyPipeline(vulkan_globals.device, vulkan_globals.sprite_pipeline, NULL);
	vkDestroyPipeline(vulkan_globals.device, vulkan_globals.sky_color_pipeline, NULL);
	vkDestroyPipeline(vulkan_globals.device, vulkan_globals.sky_box_pipeline, NULL);
	vkDestroyPipeline(vulkan_globals.device, vulkan_globals.sky_layer_pipeline, NULL);
	vkDestroyPipeline(vulkan_globals.device, vulkan_globals.alias_pipeline, NULL);
	vkDestroyPipeline(vulkan_globals.device, vulkan_globals.alias_blend_pipeline, NULL);
	vkDestroyPipeline(vulkan_globals.device, vulkan_globals.postprocess_pipeline, NULL);
}

/*
===============
R_Init
===============
*/
void R_Init (void)
{
	extern cvar_t gl_finish;

	Cmd_AddCommand ("timerefresh", R_TimeRefresh_f);
	Cmd_AddCommand ("pointfile", R_ReadPointFile_f);
	Cmd_AddCommand ("vkmemstats", R_VulkanMemStats_f);

	Cvar_RegisterVariable (&r_norefresh);
	Cvar_RegisterVariable (&r_lightmap);
	Cvar_RegisterVariable (&r_fullbright);
	Cvar_RegisterVariable (&r_drawentities);
	Cvar_RegisterVariable (&r_drawviewmodel);
	Cvar_RegisterVariable (&r_shadows);
	Cvar_RegisterVariable (&r_wateralpha);
	Cvar_SetCallback (&r_wateralpha, R_SetWateralpha_f);
	Cvar_RegisterVariable (&r_dynamic);
	Cvar_RegisterVariable (&r_novis);
	Cvar_SetCallback (&r_novis, R_VisChanged);
	Cvar_RegisterVariable (&r_speeds);
	Cvar_RegisterVariable (&r_pos);

	Cvar_RegisterVariable (&gl_finish);
	Cvar_RegisterVariable (&gl_clear);
	Cvar_RegisterVariable (&gl_cull);
	Cvar_RegisterVariable (&gl_smoothmodels);
	Cvar_RegisterVariable (&gl_affinemodels);
	Cvar_RegisterVariable (&gl_polyblend);
	Cvar_RegisterVariable (&gl_flashblend);
	Cvar_RegisterVariable (&gl_playermip);
	Cvar_RegisterVariable (&gl_nocolors);

	//johnfitz -- new cvars
	Cvar_RegisterVariable (&r_clearcolor);
	Cvar_SetCallback (&r_clearcolor, R_SetClearColor_f);
	Cvar_RegisterVariable (&r_waterquality);
	Cvar_RegisterVariable (&r_waterwarp);
	Cvar_RegisterVariable (&r_drawflat);
	Cvar_RegisterVariable (&r_flatlightstyles);
	Cvar_RegisterVariable (&r_oldskyleaf);
	Cvar_SetCallback (&r_oldskyleaf, R_VisChanged);
	Cvar_RegisterVariable (&r_drawworld);
	Cvar_RegisterVariable (&r_showtris);
	Cvar_RegisterVariable (&r_showbboxes);
	Cvar_RegisterVariable (&gl_farclip);
	Cvar_RegisterVariable (&gl_fullbrights);
	Cvar_SetCallback (&gl_fullbrights, GL_Fullbrights_f);
	Cvar_RegisterVariable (&r_lerpmodels);
	Cvar_RegisterVariable (&r_lerpmove);
	Cvar_RegisterVariable (&r_nolerp_list);
	Cvar_SetCallback (&r_nolerp_list, R_Model_ExtraFlags_List_f);
	Cvar_RegisterVariable (&r_noshadow_list);
	Cvar_SetCallback (&r_noshadow_list, R_Model_ExtraFlags_List_f);
	//johnfitz

	Cvar_RegisterVariable (&gl_zfix); // QuakeSpasm z-fighting fix
	Cvar_RegisterVariable (&r_lavaalpha);
	Cvar_RegisterVariable (&r_telealpha);
	Cvar_RegisterVariable (&r_slimealpha);
	Cvar_SetCallback (&r_lavaalpha, R_SetLavaalpha_f);
	Cvar_SetCallback (&r_telealpha, R_SetTelealpha_f);
	Cvar_SetCallback (&r_slimealpha, R_SetSlimealpha_f);

	R_InitParticles ();
	R_SetClearColor_f (&r_clearcolor); //johnfitz

	Sky_Init (); //johnfitz
	Fog_Init (); //johnfitz
}

/*
===============
R_TranslatePlayerSkin -- johnfitz -- rewritten.  also, only handles new colors, not new skins
===============
*/
void R_TranslatePlayerSkin (int playernum)
{
	int			top, bottom;

	top = (cl.scores[playernum].colors & 0xf0)>>4;
	bottom = cl.scores[playernum].colors &15;

	//FIXME: if gl_nocolors is on, then turned off, the textures may be out of sync with the scoreboard colors.
	if (!gl_nocolors.value)
		if (playertextures[playernum])
			TexMgr_ReloadImage (playertextures[playernum], top, bottom);
}

/*
===============
R_TranslateNewPlayerSkin -- johnfitz -- split off of TranslatePlayerSkin -- this is called when
the skin or model actually changes, instead of just new colors
added bug fix from bengt jardup
===============
*/
void R_TranslateNewPlayerSkin (int playernum)
{
	char		name[64];
	byte		*pixels;
	aliashdr_t	*paliashdr;
	int		skinnum;

//get correct texture pixels
	currententity = &cl_entities[1+playernum];

	if (!currententity->model || currententity->model->type != mod_alias)
		return;

	paliashdr = (aliashdr_t *)Mod_Extradata (currententity->model);

	skinnum = currententity->skinnum;

	//TODO: move these tests to the place where skinnum gets received from the server
	if (skinnum < 0 || skinnum >= paliashdr->numskins)
	{
		Con_DPrintf("(%d): Invalid player skin #%d\n", playernum, skinnum);
		skinnum = 0;
	}

	pixels = (byte *)paliashdr + paliashdr->texels[skinnum]; // This is not a persistent place!

//upload new image
	q_snprintf(name, sizeof(name), "player_%i", playernum);
	playertextures[playernum] = TexMgr_LoadImage (currententity->model, name, paliashdr->skinwidth, paliashdr->skinheight,
		SRC_INDEXED, pixels, paliashdr->gltextures[skinnum][0]->source_file, paliashdr->gltextures[skinnum][0]->source_offset, TEXPREF_PAD | TEXPREF_OVERWRITE);

//now recolor it
	R_TranslatePlayerSkin (playernum);
}

/*
===============
R_NewGame -- johnfitz -- handle a game switch
===============
*/
void R_NewGame (void)
{
	int i;

	//clear playertexture pointers (the textures themselves were freed by texmgr_newgame)
	for (i=0; i<MAX_SCOREBOARD; i++)
		playertextures[i] = NULL;
}

/*
=============
R_ParseWorldspawn

called at map load
=============
*/
static void R_ParseWorldspawn (void)
{
	char key[128], value[4096];
	const char *data;

	map_wateralpha = r_wateralpha.value;
	map_lavaalpha = r_lavaalpha.value;
	map_telealpha = r_telealpha.value;
	map_slimealpha = r_slimealpha.value;

	data = COM_Parse(cl.worldmodel->entities);
	if (!data)
		return; // error
	if (com_token[0] != '{')
		return; // error
	while (1)
	{
		data = COM_Parse(data);
		if (!data)
			return; // error
		if (com_token[0] == '}')
			break; // end of worldspawn
		if (com_token[0] == '_')
			strcpy(key, com_token + 1);
		else
			strcpy(key, com_token);
		while (key[strlen(key)-1] == ' ') // remove trailing spaces
			key[strlen(key)-1] = 0;
		data = COM_Parse(data);
		if (!data)
			return; // error
		strcpy(value, com_token);

		if (!strcmp("wateralpha", key))
			map_wateralpha = atof(value);

		if (!strcmp("lavaalpha", key))
			map_lavaalpha = atof(value);

		if (!strcmp("telealpha", key))
			map_telealpha = atof(value);

		if (!strcmp("slimealpha", key))
			map_slimealpha = atof(value);
	}
}


/*
===============
R_NewMap
===============
*/
void R_NewMap (void)
{
	int		i;

	for (i=0 ; i<256 ; i++)
		d_lightstylevalue[i] = 264;		// normal light value

// clear out efrags in case the level hasn't been reloaded
// FIXME: is this one short?
	for (i=0 ; i<cl.worldmodel->numleafs ; i++)
		cl.worldmodel->leafs[i].efrags = NULL;

	r_viewleaf = NULL;
	R_ClearParticles ();
	GL_DeleteBModelVertexBuffer();

	GL_BuildLightmaps ();
	GL_BuildBModelVertexBuffer ();
	//ericw -- no longer load alias models into a VBO here, it's done in Mod_LoadAliasModel

	r_framecount = 0; //johnfitz -- paranoid?
	r_visframecount = 0; //johnfitz -- paranoid?

	Sky_NewMap (); //johnfitz -- skybox in worldspawn
	Fog_NewMap (); //johnfitz -- global fog in worldspawn
	R_ParseWorldspawn (); //ericw -- wateralpha, lavaalpha, telealpha, slimealpha in worldspawn

	load_subdivide_size = gl_subdivide_size.value; //johnfitz -- is this the right place to set this?
}

/*
====================
R_TimeRefresh_f

For program optimization
====================
*/
void R_TimeRefresh_f (void)
{
	int		i;
	float		start, stop, time;

	if (cls.state != ca_connected)
	{
		Con_Printf("Not connected to a server\n");
		return;
	}

	start = Sys_DoubleTime ();
	for (i = 0; i < 128; i++)
	{
		GL_BeginRendering(&glx, &gly, &glwidth, &glheight);
		r_refdef.viewangles[1] = i/128.0*360.0;
		R_RenderView ();
		GL_EndRendering ();
	}

	//glFinish ();
	stop = Sys_DoubleTime ();
	time = stop-start;
	Con_Printf ("%f seconds (%f fps)\n", time, 128/time);
}

/*
====================
R_VulkanMemStats_f
====================
*/
void R_VulkanMemStats_f(void)
{
	Con_Printf("Vulkan allocations:\n");
	Con_Printf(" Tex:    %d\n", num_vulkan_tex_allocations);
	Con_Printf(" BModel: %d\n", num_vulkan_bmodel_allocations);
	Con_Printf(" Mesh:   %d\n", num_vulkan_mesh_allocations);
	Con_Printf(" Misc:   %d\n", num_vulkan_misc_allocations);
	Con_Printf(" DynBuf: %d\n", num_vulkan_dynbuf_allocations);

}
