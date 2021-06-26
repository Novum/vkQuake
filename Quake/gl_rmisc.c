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

#if defined(SDL_FRAMEWORK) || defined(NO_SDL_CONFIG)
#include <SDL2/SDL.h>
#else
#include "SDL.h"
#endif

//johnfitz -- new cvars
extern cvar_t r_clearcolor;
extern cvar_t r_flatlightstyles;
extern cvar_t gl_fullbrights;
extern cvar_t gl_farclip;
extern cvar_t r_waterquality;
extern cvar_t r_waterwarp;
extern cvar_t r_waterwarpcompute;
extern cvar_t r_oldskyleaf;
extern cvar_t r_drawworld;
extern cvar_t r_showtris;
extern cvar_t r_lerpmodels;
extern cvar_t r_lerpmove;
extern cvar_t r_nolerp_list;
//johnfitz
extern cvar_t gl_zfix; // QuakeSpasm z-fighting fix

#if defined(USE_SIMD)
extern cvar_t r_simd;
#endif
extern gltexture_t *playertextures[MAX_SCOREBOARD]; //johnfitz

vulkanglobals_t vulkan_globals;

int num_vulkan_tex_allocations = 0;
int num_vulkan_bmodel_allocations = 0;
int num_vulkan_mesh_allocations = 0;
int num_vulkan_misc_allocations = 0;
int num_vulkan_dynbuf_allocations = 0;
int num_vulkan_combined_image_samplers = 0;
int num_vulkan_ubos_dynamic = 0;
int num_vulkan_input_attachments = 0;
int num_vulkan_storage_images = 0;

qboolean use_simd;

/*
================
Staging
================
*/
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
#define INITIAL_DYNAMIC_VERTEX_BUFFER_SIZE_KB	256
#define INITIAL_DYNAMIC_INDEX_BUFFER_SIZE_KB	1024
#define INITIAL_DYNAMIC_UNIFORM_BUFFER_SIZE_KB	256
#define NUM_DYNAMIC_BUFFERS						2
#define GARBAGE_FRAME_COUNT						3
#define MAX_UNIFORM_ALLOC						2048

typedef struct
{
	VkBuffer			buffer;
	uint32_t			current_offset;
	unsigned char *		data;
} dynbuffer_t;

static uint32_t			current_dyn_vertex_buffer_size = INITIAL_DYNAMIC_VERTEX_BUFFER_SIZE_KB * 1024;
static uint32_t			current_dyn_index_buffer_size = INITIAL_DYNAMIC_INDEX_BUFFER_SIZE_KB * 1024;
static uint32_t			current_dyn_uniform_buffer_size = INITIAL_DYNAMIC_UNIFORM_BUFFER_SIZE_KB * 1024;
static VkDeviceMemory	dyn_vertex_buffer_memory;
static VkDeviceMemory	dyn_index_buffer_memory;
static VkDeviceMemory	dyn_uniform_buffer_memory;
static dynbuffer_t		dyn_vertex_buffers[NUM_DYNAMIC_BUFFERS];
static dynbuffer_t		dyn_index_buffers[NUM_DYNAMIC_BUFFERS];
static dynbuffer_t		dyn_uniform_buffers[NUM_DYNAMIC_BUFFERS];
static int				current_dyn_buffer_index = 0;
static VkDescriptorSet	ubo_descriptor_sets[2];

static int					current_garbage_index = 0;
static int					num_device_memory_garbage[GARBAGE_FRAME_COUNT];
static int					num_buffer_garbage[GARBAGE_FRAME_COUNT];
static int					num_desc_set_garbage[GARBAGE_FRAME_COUNT];
static VkDeviceMemory *		device_memory_garbage[GARBAGE_FRAME_COUNT];
static VkDescriptorSet *	descriptor_set_garbage[GARBAGE_FRAME_COUNT];
static VkBuffer *			buffer_garbage[GARBAGE_FRAME_COUNT];

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

	for (i = 0; i < VK_MAX_MEMORY_TYPES; i++)
	{
		if ((current_type_bits & 1) == 1)
		{
			if ((vulkan_globals.memory_properties.memoryTypes[i].propertyFlags & (requirements_mask | preferred_mask)) == (requirements_mask | preferred_mask))
				return i;
		}
		current_type_bits >>= 1;
	}

	current_type_bits = type_bits;
	for (i = 0; i < VK_MAX_MEMORY_TYPES; i++)
	{
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
===============
R_Model_ExtraFlags_List_f -- johnfitz -- called when r_nolerp_list cvar changes
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
	if (cls.signon == SIGNONS && cl.worldmodel && !(cl.worldmodel->contentstransparent&SURF_DRAWWATER) && var->value < 1)
		Con_Warning("Map does not appear to be water-vised\n");
	map_wateralpha = var->value;
	map_fallbackalpha = var->value;
}

#if defined(USE_SIMD)
/*
====================
R_SIMD_f
====================
*/
static void R_SIMD_f (cvar_t *var)
{
#if defined(USE_SSE2)
	use_simd = SDL_HasSSE() && SDL_HasSSE2() && (var->value != 0.0f);
#else
	#error not implemented
#endif
}
#endif

/*
====================
R_SetLavaalpha_f -- ericw
====================
*/
static void R_SetLavaalpha_f (cvar_t *var)
{
	if (cls.signon == SIGNONS && cl.worldmodel && !(cl.worldmodel->contentstransparent&SURF_DRAWLAVA) && var->value && var->value < 1)
		Con_Warning("Map does not appear to be lava-vised\n");
	map_lavaalpha = var->value;
}

/*
====================
R_SetTelealpha_f -- ericw
====================
*/
static void R_SetTelealpha_f (cvar_t *var)
{
	if (cls.signon == SIGNONS && cl.worldmodel && !(cl.worldmodel->contentstransparent&SURF_DRAWTELE) && var->value && var->value < 1)
		Con_Warning("Map does not appear to be tele-vised\n");
	map_telealpha = var->value;
}

/*
====================
R_SetSlimealpha_f -- ericw
====================
*/
static void R_SetSlimealpha_f (cvar_t *var)
{
	if (cls.signon == SIGNONS && cl.worldmodel && !(cl.worldmodel->contentstransparent&SURF_DRAWSLIME) && var->value && var->value < 1)
		Con_Warning("Map does not appear to be slime-vised\n");
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
		return map_lavaalpha > 0 ? map_lavaalpha : map_fallbackalpha;
	else if (fa->flags & SURF_DRAWTELE)
		return map_telealpha > 0 ? map_telealpha : map_fallbackalpha;
	else if (fa->flags & SURF_DRAWSLIME)
		return map_slimealpha > 0 ? map_slimealpha : map_fallbackalpha;
	else
		return map_wateralpha;
}

/*
===============
R_CreateStagingBuffers
===============
*/
static void R_CreateStagingBuffers()
{
	int i;
	VkResult err;

	VkBufferCreateInfo buffer_create_info;
	memset(&buffer_create_info, 0, sizeof(buffer_create_info));
	buffer_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	buffer_create_info.size = vulkan_globals.staging_buffer_size;
	buffer_create_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

	for (i = 0; i < NUM_STAGING_BUFFERS; ++i)
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
	const int aligned_size = ((memory_requirements.size % memory_requirements.alignment) == 0)
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

	for (i = 0; i < NUM_STAGING_BUFFERS; ++i)
	{
		err = vkBindBufferMemory(vulkan_globals.device, staging_buffers[i].buffer, staging_memory, i * aligned_size);
		if (err != VK_SUCCESS)
			Sys_Error("vkBindBufferMemory failed");
	}

	void * data;
	err = vkMapMemory(vulkan_globals.device, staging_memory, 0, NUM_STAGING_BUFFERS * aligned_size, 0, &data);
	if (err != VK_SUCCESS)
		Sys_Error("vkMapMemory failed");

	for (i = 0; i < NUM_STAGING_BUFFERS; ++i)
		staging_buffers[i].data = (unsigned char *)data + (i * aligned_size);
}

/*
===============
R_DestroyStagingBuffers
===============
*/
static void R_DestroyStagingBuffers()
{
	int i;

	vkUnmapMemory(vulkan_globals.device, staging_memory);
	vkFreeMemory(vulkan_globals.device, staging_memory, NULL);
	for (i = 0; i < NUM_STAGING_BUFFERS; ++i) {
		vkDestroyBuffer(vulkan_globals.device, staging_buffers[i].buffer, NULL);
	}
}

/*
===============
R_InitStagingBuffers
===============
*/
void R_InitStagingBuffers()
{
	int i;
	VkResult err;

	Con_Printf("Initializing staging\n");

	R_CreateStagingBuffers();

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
	command_buffer_begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

	for (i = 0; i < NUM_STAGING_BUFFERS; ++i)
	{
		err = vkCreateFence(vulkan_globals.device, &fence_create_info, NULL, &staging_buffers[i].fence);
		if (err != VK_SUCCESS)
			Sys_Error("vkCreateFence failed");

		staging_buffers[i].command_buffer = command_buffers[i];

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
	VkMemoryBarrier memory_barrier;
	memset(&memory_barrier, 0, sizeof(memory_barrier));
	memory_barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
	memory_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	memory_barrier.dstAccessMask = VK_ACCESS_INDEX_READ_BIT | VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
	vkCmdPipelineBarrier(staging_buffers[index].command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, 0, 1, &memory_barrier, 0, NULL, 0, NULL);

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
R_FlushStagingBuffer
===============
*/
static void R_FlushStagingBuffer(stagingbuffer_t * staging_buffer)
{
	VkResult err;

	if (!staging_buffer->submitted)
		return;

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
	command_buffer_begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

	err = vkBeginCommandBuffer(staging_buffer->command_buffer, &command_buffer_begin_info);
	if (err != VK_SUCCESS)
		Sys_Error("vkBeginCommandBuffer failed");
}

/*
===============
R_StagingAllocate
===============
*/
byte * R_StagingAllocate(int size, int alignment, VkCommandBuffer * command_buffer, VkBuffer * buffer, int * buffer_offset)
{
	vulkan_globals.device_idle = false;

	if (size > vulkan_globals.staging_buffer_size)
	{
		R_SubmitStagingBuffers();

		for (int i = 0; i < NUM_STAGING_BUFFERS; ++i)
			R_FlushStagingBuffer(&staging_buffers[i]);

		vulkan_globals.staging_buffer_size = size;

		R_DestroyStagingBuffers();
		R_CreateStagingBuffers();
	}

	stagingbuffer_t * staging_buffer = &staging_buffers[current_staging_buffer];
	const int align_mod = staging_buffer->current_offset % alignment;
	staging_buffer->current_offset = ((staging_buffer->current_offset % alignment) == 0) 
		? staging_buffer->current_offset 
		: (staging_buffer->current_offset + alignment - align_mod);

	if ((staging_buffer->current_offset + size) >= vulkan_globals.staging_buffer_size && !staging_buffer->submitted)
		R_SubmitStagingBuffer(current_staging_buffer);

	staging_buffer = &staging_buffers[current_staging_buffer];
	R_FlushStagingBuffer(staging_buffer);

	if (command_buffer)
		*command_buffer = staging_buffer->command_buffer;
	if (buffer)
		*buffer = staging_buffer->buffer;
	if (buffer_offset)
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

	Sys_Printf("Reallocating dynamic VBs (%u KB)\n", current_dyn_vertex_buffer_size / 1024);

	VkResult err;

	VkBufferCreateInfo buffer_create_info;
	memset(&buffer_create_info, 0, sizeof(buffer_create_info));
	buffer_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	buffer_create_info.size = current_dyn_vertex_buffer_size;
	buffer_create_info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;

	for (i = 0; i < NUM_DYNAMIC_BUFFERS; ++i)
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

	for (i = 0; i < NUM_DYNAMIC_BUFFERS; ++i)
	{
		err = vkBindBufferMemory(vulkan_globals.device, dyn_vertex_buffers[i].buffer, dyn_vertex_buffer_memory, i * aligned_size);
		if (err != VK_SUCCESS)
			Sys_Error("vkBindBufferMemory failed");
	}

	void * data;
	err = vkMapMemory(vulkan_globals.device, dyn_vertex_buffer_memory, 0, NUM_DYNAMIC_BUFFERS * aligned_size, 0, &data);
	if (err != VK_SUCCESS)
		Sys_Error("vkMapMemory failed");

	for (i = 0; i < NUM_DYNAMIC_BUFFERS; ++i)
		dyn_vertex_buffers[i].data = (unsigned char *)data + (i * aligned_size);
}

/*
===============
R_InitDynamicIndexBuffers
===============
*/
static void R_InitDynamicIndexBuffers()
{
	int i;

	Sys_Printf("Reallocating dynamic IBs (%u KB)\n", current_dyn_index_buffer_size / 1024);

	VkResult err;

	VkBufferCreateInfo buffer_create_info;
	memset(&buffer_create_info, 0, sizeof(buffer_create_info));
	buffer_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	buffer_create_info.size = current_dyn_index_buffer_size;
	buffer_create_info.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;

	for (i = 0; i < NUM_DYNAMIC_BUFFERS; ++i)
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

	for (i = 0; i < NUM_DYNAMIC_BUFFERS; ++i)
	{
		err = vkBindBufferMemory(vulkan_globals.device, dyn_index_buffers[i].buffer, dyn_index_buffer_memory, i * aligned_size);
		if (err != VK_SUCCESS)
			Sys_Error("vkBindBufferMemory failed");
	}

	void * data;
	err = vkMapMemory(vulkan_globals.device, dyn_index_buffer_memory, 0, NUM_DYNAMIC_BUFFERS * aligned_size, 0, &data);
	if (err != VK_SUCCESS)
		Sys_Error("vkMapMemory failed");

	for (i = 0; i < NUM_DYNAMIC_BUFFERS; ++i)
		dyn_index_buffers[i].data = (unsigned char *)data + (i * aligned_size);
}

/*
===============
R_InitDynamicUniformBuffers
===============
*/
static void R_InitDynamicUniformBuffers()
{
	int i;

	Sys_Printf("Reallocating dynamic UBs (%u KB)\n", current_dyn_uniform_buffer_size / 1024);

	VkResult err;

	VkBufferCreateInfo buffer_create_info;
	memset(&buffer_create_info, 0, sizeof(buffer_create_info));
	buffer_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	buffer_create_info.size = current_dyn_uniform_buffer_size;
	buffer_create_info.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;

	for (i = 0; i < NUM_DYNAMIC_BUFFERS; ++i)
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

	for (i = 0; i < NUM_DYNAMIC_BUFFERS; ++i)
	{
		err = vkBindBufferMemory(vulkan_globals.device, dyn_uniform_buffers[i].buffer, dyn_uniform_buffer_memory, i * aligned_size);
		if (err != VK_SUCCESS)
			Sys_Error("vkBindBufferMemory failed");
	}

	void * data;
	err = vkMapMemory(vulkan_globals.device, dyn_uniform_buffer_memory, 0, NUM_DYNAMIC_BUFFERS * aligned_size, 0, &data);
	if (err != VK_SUCCESS)
		Sys_Error("vkMapMemory failed");

	for (i = 0; i < NUM_DYNAMIC_BUFFERS; ++i)
		dyn_uniform_buffers[i].data = (unsigned char *)data + (i * aligned_size);

	VkDescriptorSetAllocateInfo descriptor_set_allocate_info;
	memset(&descriptor_set_allocate_info, 0, sizeof(descriptor_set_allocate_info));
	descriptor_set_allocate_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	descriptor_set_allocate_info.descriptorPool = vulkan_globals.descriptor_pool;
	descriptor_set_allocate_info.descriptorSetCount = 1;
	descriptor_set_allocate_info.pSetLayouts = &vulkan_globals.ubo_set_layout.handle;

	ubo_descriptor_sets[0] = R_AllocateDescriptorSet(&vulkan_globals.ubo_set_layout);
	ubo_descriptor_sets[1] = R_AllocateDescriptorSet(&vulkan_globals.ubo_set_layout);

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

	for (i = 0; i < NUM_DYNAMIC_BUFFERS; ++i)
	{
		buffer_info.buffer = dyn_uniform_buffers[i].buffer;
		ubo_write.dstSet = ubo_descriptor_sets[i];
		vkUpdateDescriptorSets(vulkan_globals.device, 1, &ubo_write, 0, NULL);
	}
}

/*
===============
R_InitFanIndexBuffer
===============
*/
static void R_InitFanIndexBuffer()
{
	VkResult err;
	VkDeviceMemory memory;
	const int bufferSize = sizeof(uint16_t) * FAN_INDEX_BUFFER_SIZE;

	VkBufferCreateInfo buffer_create_info;
	memset(&buffer_create_info, 0, sizeof(buffer_create_info));
	buffer_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	buffer_create_info.size = bufferSize;
	buffer_create_info.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

	err = vkCreateBuffer(vulkan_globals.device, &buffer_create_info, NULL, &vulkan_globals.fan_index_buffer);
	if (err != VK_SUCCESS)
		Sys_Error("vkCreateBuffer failed");

	GL_SetObjectName((uint64_t)vulkan_globals.fan_index_buffer, VK_DEBUG_REPORT_OBJECT_TYPE_BUFFER_EXT, "Quad Index Buffer");

	VkMemoryRequirements memory_requirements;
	vkGetBufferMemoryRequirements(vulkan_globals.device, vulkan_globals.fan_index_buffer, &memory_requirements);

	VkMemoryAllocateInfo memory_allocate_info;
	memset(&memory_allocate_info, 0, sizeof(memory_allocate_info));
	memory_allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	memory_allocate_info.allocationSize = memory_requirements.size;
	memory_allocate_info.memoryTypeIndex = GL_MemoryTypeFromProperties(memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0);

	num_vulkan_dynbuf_allocations += 1;
	err = vkAllocateMemory(vulkan_globals.device, &memory_allocate_info, NULL, &memory);
	if (err != VK_SUCCESS)
		Sys_Error("vkAllocateMemory failed");

	err = vkBindBufferMemory(vulkan_globals.device, vulkan_globals.fan_index_buffer, memory, 0);
	if (err != VK_SUCCESS)
		Sys_Error("vkBindBufferMemory failed");

	{
		VkBuffer staging_buffer;
		VkCommandBuffer command_buffer;
		int staging_offset;
		int current_index = 0;
		int i;
		uint16_t * staging_mem = (uint16_t*)R_StagingAllocate(bufferSize, 1, &command_buffer, &staging_buffer, &staging_offset);

		for (i = 0; i < FAN_INDEX_BUFFER_SIZE / 3; ++i)
		{
			staging_mem[current_index++] = 0;
			staging_mem[current_index++] = 1 + i;
			staging_mem[current_index++] = 2 + i;
		}

		VkBufferCopy region;
		region.srcOffset = staging_offset;
		region.dstOffset = 0;
		region.size = bufferSize;
		vkCmdCopyBuffer(command_buffer, staging_buffer, vulkan_globals.fan_index_buffer, 1, &region);
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
R_AddDynamicBufferGarbage
===============
*/
static void R_AddDynamicBufferGarbage(VkDeviceMemory device_memory, dynbuffer_t * buffers, VkDescriptorSet * descriptor_sets)
{
	{
		int * num_garbage = &num_device_memory_garbage[current_garbage_index];
		int old_num_memory_garbage = *num_garbage;
		*num_garbage += 1;
		if (device_memory_garbage[current_garbage_index] == NULL)
			device_memory_garbage[current_garbage_index] = malloc(sizeof(VkDeviceMemory) * (*num_garbage));
		else
			device_memory_garbage[current_garbage_index] = realloc(device_memory_garbage[current_garbage_index], sizeof(VkDeviceMemory) * (*num_garbage));
		device_memory_garbage[current_garbage_index][old_num_memory_garbage] = device_memory;
	}

	{
		int * num_garbage = &num_buffer_garbage[current_garbage_index];
		int old_num_buffer_garbage = *num_garbage;
		*num_garbage += NUM_DYNAMIC_BUFFERS;
		if (buffer_garbage[current_garbage_index] == NULL)
			buffer_garbage[current_garbage_index] = malloc(sizeof(VkBuffer) * (*num_garbage));
		else
			buffer_garbage[current_garbage_index] = realloc(buffer_garbage[current_garbage_index], sizeof(VkBuffer) * (*num_garbage));
		for (int i = 0; i < NUM_DYNAMIC_BUFFERS; ++i)
			buffer_garbage[current_garbage_index][old_num_buffer_garbage + i] = buffers[i].buffer;
	}

	if (descriptor_sets)
	{
		int * num_garbage = &num_desc_set_garbage[current_garbage_index];
		int old_num_desc_set_garbage = *num_garbage;
		*num_garbage += 2;
		if (descriptor_set_garbage[current_garbage_index] == NULL)
			descriptor_set_garbage[current_garbage_index] = malloc(sizeof(VkDescriptorSet) * (*num_garbage));
		else
			descriptor_set_garbage[current_garbage_index] = realloc(descriptor_set_garbage[current_garbage_index], sizeof(VkDescriptorSet) * (*num_garbage));
		for (int i = 0; i < 2; ++i)
			descriptor_set_garbage[current_garbage_index][old_num_desc_set_garbage + i] = descriptor_sets[i];
	}
}

/*
===============
R_CollectDynamicBufferGarbage
===============
*/
void R_CollectDynamicBufferGarbage()
{
	current_garbage_index = (current_garbage_index + 1) % GARBAGE_FRAME_COUNT;
	const int collect_garbage_index = (current_garbage_index + 1) % GARBAGE_FRAME_COUNT;

	if (num_desc_set_garbage[collect_garbage_index] > 0) {
		for (int i = 0; i < num_desc_set_garbage[collect_garbage_index]; ++i)
			R_FreeDescriptorSet(descriptor_set_garbage[collect_garbage_index][i], &vulkan_globals.ubo_set_layout);
		free(descriptor_set_garbage[collect_garbage_index]);
		descriptor_set_garbage[collect_garbage_index] = NULL;
		num_desc_set_garbage[collect_garbage_index] = 0;
	}

	if (num_buffer_garbage[collect_garbage_index] > 0) {
		for (int i = 0; i < num_buffer_garbage[collect_garbage_index]; ++i)
			vkDestroyBuffer(vulkan_globals.device, buffer_garbage[collect_garbage_index][i], NULL);
		free(buffer_garbage[collect_garbage_index]);
		buffer_garbage[collect_garbage_index] = NULL;
		num_buffer_garbage[collect_garbage_index] = 0;
	}

	if (num_device_memory_garbage[collect_garbage_index] > 0) {
		for (int i = 0; i < num_device_memory_garbage[collect_garbage_index]; ++i)
			vkFreeMemory(vulkan_globals.device, device_memory_garbage[collect_garbage_index][i], NULL);
		free(device_memory_garbage[collect_garbage_index]);
		device_memory_garbage[collect_garbage_index] = NULL;
		num_device_memory_garbage[collect_garbage_index] = 0;
	}
}

/*
===============
R_VertexAllocate
===============
*/
byte * R_VertexAllocate(int size, VkBuffer * buffer, VkDeviceSize * buffer_offset)
{
	dynbuffer_t *dyn_vb = &dyn_vertex_buffers[current_dyn_buffer_index];

	if ((dyn_vb->current_offset + size) > current_dyn_vertex_buffer_size)
	{
		R_AddDynamicBufferGarbage(dyn_vertex_buffer_memory, dyn_vertex_buffers, NULL);
		current_dyn_vertex_buffer_size = q_max(current_dyn_vertex_buffer_size * 2, (uint32_t)Q_nextPow2(size));
		vkUnmapMemory(vulkan_globals.device, dyn_vertex_buffer_memory);
		R_InitDynamicVertexBuffers();
	}

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
	// Align to 4 bytes because we allocate both uint16 and uint32
	// index buffers and alignment must match index size
	const int align_mod = size % 4;
	const int aligned_size = ((size % 4) == 0) ? size : (size + 4 - align_mod);

	dynbuffer_t *dyn_ib = &dyn_index_buffers[current_dyn_buffer_index];

	if ((dyn_ib->current_offset + aligned_size) > current_dyn_index_buffer_size)
	{
		R_AddDynamicBufferGarbage(dyn_index_buffer_memory, dyn_index_buffers, NULL);
		current_dyn_index_buffer_size = q_max(current_dyn_index_buffer_size * 2, (uint32_t)Q_nextPow2(size));
		vkUnmapMemory(vulkan_globals.device, dyn_index_buffer_memory);
		R_InitDynamicIndexBuffers();
	}

	*buffer = dyn_ib->buffer;
	*buffer_offset = dyn_ib->current_offset;

	unsigned char *data = dyn_ib->data + dyn_ib->current_offset;
	dyn_ib->current_offset += aligned_size;

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

	if ((dyn_ub->current_offset + MAX_UNIFORM_ALLOC) > current_dyn_uniform_buffer_size)
	{
		R_AddDynamicBufferGarbage(dyn_uniform_buffer_memory, dyn_uniform_buffers, ubo_descriptor_sets);
		current_dyn_uniform_buffer_size = q_max(current_dyn_uniform_buffer_size * 2, (uint32_t)Q_nextPow2(size));
		vkUnmapMemory(vulkan_globals.device, dyn_uniform_buffer_memory);
		R_InitDynamicUniformBuffers();
	}

	*buffer = dyn_ub->buffer;
	*buffer_offset = dyn_ub->current_offset;

	unsigned char *data = dyn_ub->data + dyn_ub->current_offset;
	dyn_ub->current_offset += aligned_size;

	*descriptor_set = ubo_descriptor_sets[current_dyn_buffer_index];

	return data;
}

/*
===============
R_InitGPUBuffers
===============
*/
void R_InitGPUBuffers()
{
	R_InitDynamicVertexBuffers();
	R_InitDynamicIndexBuffers();
	R_InitDynamicUniformBuffers();
	R_InitFanIndexBuffer();
}

/*
===============
R_CreateDescriptorSetLayouts
===============
*/
void R_CreateDescriptorSetLayouts()
{
	Sys_Printf("Creating descriptor set layouts\n");

	VkResult err;

	VkDescriptorSetLayoutBinding single_texture_layout_binding;
	memset(&single_texture_layout_binding, 0, sizeof(single_texture_layout_binding));
	single_texture_layout_binding.binding = 0;
	single_texture_layout_binding.descriptorCount = 1;
	single_texture_layout_binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	single_texture_layout_binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT;

	VkDescriptorSetLayoutCreateInfo descriptor_set_layout_create_info;
	memset(&descriptor_set_layout_create_info, 0, sizeof(descriptor_set_layout_create_info));
	descriptor_set_layout_create_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	descriptor_set_layout_create_info.bindingCount = 1;
	descriptor_set_layout_create_info.pBindings = &single_texture_layout_binding;

	memset(&vulkan_globals.single_texture_set_layout, 0, sizeof(vulkan_globals.single_texture_set_layout));
	vulkan_globals.single_texture_set_layout.num_combined_image_samplers = 1;

	err = vkCreateDescriptorSetLayout(vulkan_globals.device, &descriptor_set_layout_create_info, NULL, &vulkan_globals.single_texture_set_layout.handle);
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

	memset(&vulkan_globals.ubo_set_layout, 0, sizeof(vulkan_globals.ubo_set_layout));
	vulkan_globals.ubo_set_layout.num_ubos_dynamic = 1;

	err = vkCreateDescriptorSetLayout(vulkan_globals.device, &descriptor_set_layout_create_info, NULL, &vulkan_globals.ubo_set_layout.handle);
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

	memset(&vulkan_globals.input_attachment_set_layout, 0, sizeof(vulkan_globals.input_attachment_set_layout));
	vulkan_globals.input_attachment_set_layout.num_input_attachments = 1;
	
	err = vkCreateDescriptorSetLayout(vulkan_globals.device, &descriptor_set_layout_create_info, NULL, &vulkan_globals.input_attachment_set_layout.handle);
	if (err != VK_SUCCESS)
		Sys_Error("vkCreateDescriptorSetLayout failed");

	VkDescriptorSetLayoutBinding screen_warp_layout_bindings[2];
	memset(&screen_warp_layout_bindings, 0, sizeof(screen_warp_layout_bindings));
	screen_warp_layout_bindings[0].binding = 0;
	screen_warp_layout_bindings[0].descriptorCount = 1;
	screen_warp_layout_bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	screen_warp_layout_bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
	screen_warp_layout_bindings[1].binding = 1;
	screen_warp_layout_bindings[1].descriptorCount = 1;
	screen_warp_layout_bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	screen_warp_layout_bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

	descriptor_set_layout_create_info.bindingCount = 2;
	descriptor_set_layout_create_info.pBindings = screen_warp_layout_bindings;
	
	memset(&vulkan_globals.screen_warp_set_layout, 0, sizeof(vulkan_globals.screen_warp_set_layout));
	vulkan_globals.screen_warp_set_layout.num_combined_image_samplers = 1;
	vulkan_globals.screen_warp_set_layout.num_storage_images = 1;

	err = vkCreateDescriptorSetLayout(vulkan_globals.device, &descriptor_set_layout_create_info, NULL, &vulkan_globals.screen_warp_set_layout.handle);
	if (err != VK_SUCCESS)
		Sys_Error("vkCreateDescriptorSetLayout failed");

	VkDescriptorSetLayoutBinding single_texture_cs_write_layout_binding;
	memset(&single_texture_cs_write_layout_binding, 0, sizeof(single_texture_cs_write_layout_binding));
	single_texture_cs_write_layout_binding.binding = 0;
	single_texture_cs_write_layout_binding.descriptorCount = 1;
	single_texture_cs_write_layout_binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	single_texture_cs_write_layout_binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

	descriptor_set_layout_create_info.bindingCount = 1;
	descriptor_set_layout_create_info.pBindings = &single_texture_cs_write_layout_binding;

	memset(&vulkan_globals.single_texture_cs_write_set_layout, 0, sizeof(vulkan_globals.single_texture_cs_write_set_layout));
	vulkan_globals.single_texture_cs_write_set_layout.num_storage_images = 1;

	err = vkCreateDescriptorSetLayout(vulkan_globals.device, &descriptor_set_layout_create_info, NULL, &vulkan_globals.single_texture_cs_write_set_layout.handle);
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
	VkDescriptorPoolSize pool_sizes[4];
	pool_sizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	pool_sizes[0].descriptorCount = MAX_GLTEXTURES + 1;
	pool_sizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
	pool_sizes[1].descriptorCount = 16;
	pool_sizes[2].type = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
	pool_sizes[2].descriptorCount = 2;
	pool_sizes[3].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	pool_sizes[3].descriptorCount = MAX_GLTEXTURES;

	VkDescriptorPoolCreateInfo descriptor_pool_create_info;
	memset(&descriptor_pool_create_info, 0, sizeof(descriptor_pool_create_info));
	descriptor_pool_create_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	descriptor_pool_create_info.maxSets = MAX_GLTEXTURES + 32;
	descriptor_pool_create_info.poolSizeCount = 4;
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
	Sys_Printf("Creating pipeline layouts\n");

	VkResult err;

	// Basic
	VkDescriptorSetLayout basic_descriptor_set_layouts[1] = { vulkan_globals.single_texture_set_layout.handle };
	
	VkPushConstantRange push_constant_range;
	memset(&push_constant_range, 0, sizeof(push_constant_range));
	push_constant_range.offset = 0;
	push_constant_range.size = 21 * sizeof(float);
	push_constant_range.stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS;

	VkPipelineLayoutCreateInfo pipeline_layout_create_info;
	memset(&pipeline_layout_create_info, 0, sizeof(pipeline_layout_create_info));
	pipeline_layout_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipeline_layout_create_info.setLayoutCount = 1;
	pipeline_layout_create_info.pSetLayouts = basic_descriptor_set_layouts;
	pipeline_layout_create_info.pushConstantRangeCount = 1;
	pipeline_layout_create_info.pPushConstantRanges = &push_constant_range;

	err = vkCreatePipelineLayout(vulkan_globals.device, &pipeline_layout_create_info, NULL, &vulkan_globals.basic_pipeline_layout.handle);
	if (err != VK_SUCCESS)
		Sys_Error("vkCreatePipelineLayout failed");
	vulkan_globals.basic_pipeline_layout.push_constant_range = push_constant_range;

	// World
	VkDescriptorSetLayout world_descriptor_set_layouts[3] = {
		vulkan_globals.single_texture_set_layout.handle,
		vulkan_globals.single_texture_set_layout.handle,
		vulkan_globals.single_texture_set_layout.handle
	};

	pipeline_layout_create_info.setLayoutCount = 3;
	pipeline_layout_create_info.pSetLayouts = world_descriptor_set_layouts;

	err = vkCreatePipelineLayout(vulkan_globals.device, &pipeline_layout_create_info, NULL, &vulkan_globals.world_pipeline_layout.handle);
	if (err != VK_SUCCESS)
		Sys_Error("vkCreatePipelineLayout failed");
	vulkan_globals.world_pipeline_layout.push_constant_range = push_constant_range;

	// Alias
	VkDescriptorSetLayout alias_descriptor_set_layouts[3] = {
		vulkan_globals.single_texture_set_layout.handle,
		vulkan_globals.single_texture_set_layout.handle,
		vulkan_globals.ubo_set_layout.handle
	};

	pipeline_layout_create_info.setLayoutCount = 3;
	pipeline_layout_create_info.pSetLayouts = alias_descriptor_set_layouts;

	err = vkCreatePipelineLayout(vulkan_globals.device, &pipeline_layout_create_info, NULL, &vulkan_globals.alias_pipeline.layout.handle);
	if (err != VK_SUCCESS)
		Sys_Error("vkCreatePipelineLayout failed");
	vulkan_globals.alias_pipeline.layout.push_constant_range = push_constant_range;

	// Sky
	VkDescriptorSetLayout sky_layer_descriptor_set_layouts[2] = {
		vulkan_globals.single_texture_set_layout.handle,
		vulkan_globals.single_texture_set_layout.handle,
	};

	pipeline_layout_create_info.setLayoutCount = 2;
	pipeline_layout_create_info.pSetLayouts = sky_layer_descriptor_set_layouts;

	err = vkCreatePipelineLayout(vulkan_globals.device, &pipeline_layout_create_info, NULL, &vulkan_globals.sky_layer_pipeline.layout.handle);
	if (err != VK_SUCCESS)
		Sys_Error("vkCreatePipelineLayout failed");
	vulkan_globals.sky_layer_pipeline.layout.push_constant_range = push_constant_range;

	// Postprocess
	VkDescriptorSetLayout postprocess_descriptor_set_layouts[1] = {
		vulkan_globals.input_attachment_set_layout.handle,
	};

	memset(&push_constant_range, 0, sizeof(push_constant_range));
	push_constant_range.offset = 0;
	push_constant_range.size = 2 * sizeof(float);
	push_constant_range.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

	pipeline_layout_create_info.setLayoutCount = 1;
	pipeline_layout_create_info.pSetLayouts = postprocess_descriptor_set_layouts;
	pipeline_layout_create_info.pushConstantRangeCount = 1;
	pipeline_layout_create_info.pPushConstantRanges = &push_constant_range;

	err = vkCreatePipelineLayout(vulkan_globals.device, &pipeline_layout_create_info, NULL, &vulkan_globals.postprocess_pipeline.layout.handle);
	if (err != VK_SUCCESS)
		Sys_Error("vkCreatePipelineLayout failed");
	vulkan_globals.postprocess_pipeline.layout.push_constant_range = push_constant_range;

	// Screen warp
	VkDescriptorSetLayout screen_warp_descriptor_set_layouts[1] = {
		vulkan_globals.screen_warp_set_layout.handle,
	};

	memset(&push_constant_range, 0, sizeof(push_constant_range));
	push_constant_range.offset = 0;
	push_constant_range.size = 5 * sizeof(float);
	push_constant_range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

	pipeline_layout_create_info.setLayoutCount = 1;
	pipeline_layout_create_info.pSetLayouts = screen_warp_descriptor_set_layouts;
	pipeline_layout_create_info.pushConstantRangeCount = 1;
	pipeline_layout_create_info.pPushConstantRanges = &push_constant_range;

	err = vkCreatePipelineLayout(vulkan_globals.device, &pipeline_layout_create_info, NULL, &vulkan_globals.screen_effects_pipeline.layout.handle);
	if (err != VK_SUCCESS)
		Sys_Error("vkCreatePipelineLayout failed");
	vulkan_globals.screen_effects_pipeline.layout.push_constant_range = push_constant_range;

	vulkan_globals.screen_effects_scale_pipeline.layout.handle = vulkan_globals.screen_effects_pipeline.layout.handle;
	vulkan_globals.screen_effects_scale_pipeline.layout.push_constant_range = push_constant_range;
	vulkan_globals.screen_effects_scale_sops_pipeline.layout.handle = vulkan_globals.screen_effects_pipeline.layout.handle;
	vulkan_globals.screen_effects_scale_sops_pipeline.layout.push_constant_range = push_constant_range;

	// Texture warp
	VkDescriptorSetLayout tex_warp_descriptor_set_layouts[2] = {
		vulkan_globals.single_texture_set_layout.handle,
		vulkan_globals.single_texture_cs_write_set_layout.handle,
	};

	memset(&push_constant_range, 0, sizeof(push_constant_range));
	push_constant_range.offset = 0;
	push_constant_range.size = 1 * sizeof(float);
	push_constant_range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

	pipeline_layout_create_info.setLayoutCount = 2;
	pipeline_layout_create_info.pSetLayouts = tex_warp_descriptor_set_layouts;
	pipeline_layout_create_info.pushConstantRangeCount = 1;
	pipeline_layout_create_info.pPushConstantRanges = &push_constant_range;

	err = vkCreatePipelineLayout(vulkan_globals.device, &pipeline_layout_create_info, NULL, &vulkan_globals.cs_tex_warp_pipeline.layout.handle);
	if (err != VK_SUCCESS)
		Sys_Error("vkCreatePipelineLayout failed");
	vulkan_globals.cs_tex_warp_pipeline.layout.push_constant_range = push_constant_range;

	// Show triangles
	pipeline_layout_create_info.setLayoutCount = 0;
	pipeline_layout_create_info.pushConstantRangeCount = 0;

	err = vkCreatePipelineLayout(vulkan_globals.device, &pipeline_layout_create_info, NULL, &vulkan_globals.showtris_pipeline.layout.handle);
	if (err != VK_SUCCESS)
		Sys_Error("vkCreatePipelineLayout failed");
	vulkan_globals.showtris_pipeline.layout.push_constant_range = push_constant_range;
}

/*
===============
R_InitSamplers
===============
*/
void R_InitSamplers()
{
	Sys_Printf("Initializing samplers\n");

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
		sampler_create_info.maxAnisotropy = 1.0f;

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
static VkShaderModule R_CreateShaderModule(byte *code, int size, const char * name)
{
	VkShaderModuleCreateInfo module_create_info;
	memset(&module_create_info, 0, sizeof(module_create_info));
	module_create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	module_create_info.pNext = NULL;
	module_create_info.codeSize = size;
	module_create_info.pCode = (uint32_t *)code;

	VkShaderModule module;
	VkResult err = vkCreateShaderModule(vulkan_globals.device, &module_create_info, NULL, &module);
	if (err != VK_SUCCESS)
		Sys_Error("vkCreateShaderModule failed");

	GL_SetObjectName((uint64_t)module, VK_OBJECT_TYPE_SHADER_MODULE, name);

	return module;
}

#define CREATE_SHADER_MODULE(name) VkShaderModule name##_module = R_CreateShaderModule(name##_spv, name##_spv_size, #name)
#define CREATE_SHADER_MODULE_COND(name, cond) VkShaderModule name##_module = cond ? R_CreateShaderModule(name##_spv, name##_spv_size, #name) : VK_NULL_HANDLE

/*
===============
R_CreatePipelines
===============
*/
void R_CreatePipelines()
{
	int render_pass;
	int alpha_blend, alpha_test, fullbright_enabled;
	VkResult err;

	Sys_Printf("Creating pipelines\n");

	CREATE_SHADER_MODULE(basic_vert);
	CREATE_SHADER_MODULE(basic_frag);
	CREATE_SHADER_MODULE(basic_alphatest_frag);
	CREATE_SHADER_MODULE(basic_notex_frag);
	CREATE_SHADER_MODULE(world_vert);
	CREATE_SHADER_MODULE(world_frag);
	CREATE_SHADER_MODULE(alias_vert);
	CREATE_SHADER_MODULE(alias_frag);
	CREATE_SHADER_MODULE(alias_alphatest_frag);
	CREATE_SHADER_MODULE(sky_layer_vert);
	CREATE_SHADER_MODULE(sky_layer_frag);
	CREATE_SHADER_MODULE(sky_box_frag);
	CREATE_SHADER_MODULE(postprocess_vert);
	CREATE_SHADER_MODULE(postprocess_frag);
	CREATE_SHADER_MODULE(screen_effects_8bit_comp);
	CREATE_SHADER_MODULE(screen_effects_8bit_scale_comp);
	CREATE_SHADER_MODULE_COND(screen_effects_8bit_scale_sops_comp, vulkan_globals.screen_effects_sops);
	CREATE_SHADER_MODULE(screen_effects_10bit_comp);
	CREATE_SHADER_MODULE(screen_effects_10bit_scale_comp);
	CREATE_SHADER_MODULE_COND(screen_effects_10bit_scale_sops_comp, vulkan_globals.screen_effects_sops);
	CREATE_SHADER_MODULE(cs_tex_warp_comp);
	CREATE_SHADER_MODULE(showtris_vert);
	CREATE_SHADER_MODULE(showtris_frag);

	VkPipelineDynamicStateCreateInfo dynamic_state_create_info;
	memset(&dynamic_state_create_info, 0, sizeof(dynamic_state_create_info));
	dynamic_state_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	VkDynamicState dynamic_states[3];
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
	if (vulkan_globals.supersampling)
	{
		multisample_state_create_info.sampleShadingEnable = VK_TRUE;
		multisample_state_create_info.minSampleShading = 1.0f;
	}

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
	pipeline_create_info.layout = vulkan_globals.basic_pipeline_layout.handle;
	pipeline_create_info.renderPass = vulkan_globals.main_render_pass;

	//================
	// Basic pipelines
	//================
	input_assembly_state_create_info.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

	depth_stencil_state_create_info.depthTestEnable = VK_TRUE;
	depth_stencil_state_create_info.depthWriteEnable = VK_TRUE;
	for (render_pass = 0; render_pass < 2; ++render_pass)
	{
		pipeline_create_info.renderPass = (render_pass == 0) ? vulkan_globals.main_render_pass : vulkan_globals.ui_render_pass;
		multisample_state_create_info.rasterizationSamples = (render_pass == 0) ? vulkan_globals.sample_count : VK_SAMPLE_COUNT_1_BIT;

		assert(vulkan_globals.basic_alphatest_pipeline[render_pass].handle == VK_NULL_HANDLE);
		err = vkCreateGraphicsPipelines(vulkan_globals.device, VK_NULL_HANDLE, 1, &pipeline_create_info, NULL, &vulkan_globals.basic_alphatest_pipeline[render_pass].handle);
		if (err != VK_SUCCESS)
			Sys_Error("vkCreateGraphicsPipelines failed");
		vulkan_globals.basic_alphatest_pipeline[render_pass].layout = vulkan_globals.basic_pipeline_layout;

		GL_SetObjectName((uint64_t)vulkan_globals.basic_alphatest_pipeline[render_pass].handle, VK_DEBUG_REPORT_OBJECT_TYPE_PIPELINE_EXT, "basic_alphatest");
	}

	shader_stages[1].module = basic_notex_frag_module;

	blend_attachment_state.blendEnable = VK_TRUE;
	blend_attachment_state.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
	blend_attachment_state.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	blend_attachment_state.colorBlendOp = VK_BLEND_OP_ADD;
	blend_attachment_state.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
	blend_attachment_state.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	blend_attachment_state.alphaBlendOp = VK_BLEND_OP_ADD;

	depth_stencil_state_create_info.depthTestEnable = VK_FALSE;
	depth_stencil_state_create_info.depthWriteEnable = VK_FALSE;

	for (render_pass = 0; render_pass < 2; ++render_pass)
	{
		pipeline_create_info.renderPass = (render_pass == 0) ? vulkan_globals.main_render_pass : vulkan_globals.ui_render_pass;
		multisample_state_create_info.rasterizationSamples = (render_pass == 0) ? vulkan_globals.sample_count : VK_SAMPLE_COUNT_1_BIT;

		assert(vulkan_globals.basic_notex_blend_pipeline[render_pass].handle == VK_NULL_HANDLE);
		err = vkCreateGraphicsPipelines(vulkan_globals.device, VK_NULL_HANDLE, 1, &pipeline_create_info, NULL, &vulkan_globals.basic_notex_blend_pipeline[render_pass].handle);
		if (err != VK_SUCCESS)
			Sys_Error("vkCreateGraphicsPipelines failed");
		vulkan_globals.basic_notex_blend_pipeline[render_pass].layout = vulkan_globals.basic_pipeline_layout;

		GL_SetObjectName((uint64_t)vulkan_globals.basic_notex_blend_pipeline[render_pass].handle, VK_DEBUG_REPORT_OBJECT_TYPE_PIPELINE_EXT, "basic_notex_blend");
	}

	pipeline_create_info.renderPass = vulkan_globals.main_render_pass;
	pipeline_create_info.subpass = 0;
	multisample_state_create_info.rasterizationSamples = vulkan_globals.sample_count;

	assert(vulkan_globals.basic_poly_blend_pipeline.handle == VK_NULL_HANDLE);
	err = vkCreateGraphicsPipelines(vulkan_globals.device, VK_NULL_HANDLE, 1, &pipeline_create_info, NULL, &vulkan_globals.basic_poly_blend_pipeline.handle);
	if (err != VK_SUCCESS)
		Sys_Error("vkCreateGraphicsPipelines failed");
	vulkan_globals.basic_poly_blend_pipeline.layout = vulkan_globals.basic_pipeline_layout;

	GL_SetObjectName((uint64_t)vulkan_globals.basic_poly_blend_pipeline.handle, VK_DEBUG_REPORT_OBJECT_TYPE_PIPELINE_EXT, "basic_poly_blend");

	shader_stages[1].module = basic_frag_module;

	for (render_pass = 0; render_pass < 2; ++render_pass)
	{
		pipeline_create_info.renderPass = (render_pass == 0) ? vulkan_globals.main_render_pass : vulkan_globals.ui_render_pass;
		multisample_state_create_info.rasterizationSamples = (render_pass == 0) ? vulkan_globals.sample_count : VK_SAMPLE_COUNT_1_BIT;

		assert(vulkan_globals.basic_blend_pipeline[render_pass].handle == VK_NULL_HANDLE);
		err = vkCreateGraphicsPipelines(vulkan_globals.device, VK_NULL_HANDLE, 1, &pipeline_create_info, NULL, &vulkan_globals.basic_blend_pipeline[render_pass].handle);
		if (err != VK_SUCCESS)
			Sys_Error("vkCreateGraphicsPipelines failed");
		vulkan_globals.basic_blend_pipeline[render_pass].layout = vulkan_globals.basic_pipeline_layout;

		GL_SetObjectName((uint64_t)vulkan_globals.basic_blend_pipeline[render_pass].handle, VK_DEBUG_REPORT_OBJECT_TYPE_PIPELINE_EXT, "basic_blend");
	}

	multisample_state_create_info.rasterizationSamples = vulkan_globals.sample_count;

	//================
	// Warp
	//================
	multisample_state_create_info.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	input_assembly_state_create_info.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;

	blend_attachment_state.blendEnable = VK_FALSE;

	shader_stages[0].module = basic_vert_module;
	shader_stages[1].module = basic_frag_module;

	pipeline_create_info.renderPass = vulkan_globals.warp_render_pass;

	assert(vulkan_globals.raster_tex_warp_pipeline.handle == VK_NULL_HANDLE);
	err = vkCreateGraphicsPipelines(vulkan_globals.device, VK_NULL_HANDLE, 1, &pipeline_create_info, NULL, &vulkan_globals.raster_tex_warp_pipeline.handle);
	if (err != VK_SUCCESS)
		Sys_Error("vkCreateGraphicsPipelines failed");
	vulkan_globals.raster_tex_warp_pipeline.layout = vulkan_globals.basic_pipeline_layout;

	GL_SetObjectName((uint64_t)vulkan_globals.raster_tex_warp_pipeline.handle, VK_DEBUG_REPORT_OBJECT_TYPE_PIPELINE_EXT, "warp");

	//================
	// Particles
	//================
	multisample_state_create_info.rasterizationSamples = vulkan_globals.sample_count;

	input_assembly_state_create_info.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	
	depth_stencil_state_create_info.depthTestEnable = VK_TRUE;
	depth_stencil_state_create_info.depthWriteEnable = VK_FALSE;

	pipeline_create_info.renderPass = vulkan_globals.main_render_pass;

	blend_attachment_state.blendEnable = VK_TRUE;

	assert(vulkan_globals.particle_pipeline.handle == VK_NULL_HANDLE);
	err = vkCreateGraphicsPipelines(vulkan_globals.device, VK_NULL_HANDLE, 1, &pipeline_create_info, NULL, &vulkan_globals.particle_pipeline.handle);
	if (err != VK_SUCCESS)
		Sys_Error("vkCreateGraphicsPipelines failed");
	vulkan_globals.particle_pipeline.layout = vulkan_globals.basic_pipeline_layout;

	//================
	// Water
	//================
	input_assembly_state_create_info.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	depth_stencil_state_create_info.depthTestEnable = VK_TRUE;
	depth_stencil_state_create_info.depthWriteEnable = VK_TRUE;
	depth_stencil_state_create_info.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
	blend_attachment_state.blendEnable = VK_FALSE;

	assert(vulkan_globals.water_pipeline.handle == VK_NULL_HANDLE);
	err = vkCreateGraphicsPipelines(vulkan_globals.device, VK_NULL_HANDLE, 1, &pipeline_create_info, NULL, &vulkan_globals.water_pipeline.handle);
	if (err != VK_SUCCESS)
		Sys_Error("vkCreateGraphicsPipelines failed");
	vulkan_globals.water_pipeline.layout = vulkan_globals.basic_pipeline_layout;

	GL_SetObjectName((uint64_t)vulkan_globals.water_pipeline.handle, VK_DEBUG_REPORT_OBJECT_TYPE_PIPELINE_EXT, "water");

	depth_stencil_state_create_info.depthWriteEnable = VK_FALSE;
	blend_attachment_state.blendEnable = VK_TRUE;

	assert(vulkan_globals.water_blend_pipeline.handle == VK_NULL_HANDLE);
	err = vkCreateGraphicsPipelines(vulkan_globals.device, VK_NULL_HANDLE, 1, &pipeline_create_info, NULL, &vulkan_globals.water_blend_pipeline.handle);
	if (err != VK_SUCCESS)
		Sys_Error("vkCreateGraphicsPipelines failed");
	vulkan_globals.water_blend_pipeline.layout = vulkan_globals.basic_pipeline_layout;

	GL_SetObjectName((uint64_t)vulkan_globals.water_blend_pipeline.handle, VK_DEBUG_REPORT_OBJECT_TYPE_PIPELINE_EXT, "water_blend");
	
	//================
	// Sprites
	//================
	shader_stages[1].module = basic_alphatest_frag_module;
	blend_attachment_state.blendEnable = VK_FALSE;
	depth_stencil_state_create_info.depthTestEnable = VK_TRUE;
	depth_stencil_state_create_info.depthWriteEnable = VK_TRUE;

	dynamic_states[dynamic_state_create_info.dynamicStateCount++] = VK_DYNAMIC_STATE_DEPTH_BIAS;

	assert(vulkan_globals.sprite_pipeline.handle == VK_NULL_HANDLE);
	err = vkCreateGraphicsPipelines(vulkan_globals.device, VK_NULL_HANDLE, 1, &pipeline_create_info, NULL, &vulkan_globals.sprite_pipeline.handle);
	if (err != VK_SUCCESS)
		Sys_Error("vkCreateGraphicsPipelines failed");
	vulkan_globals.sprite_pipeline.layout = vulkan_globals.basic_pipeline_layout;

	GL_SetObjectName((uint64_t)vulkan_globals.sprite_pipeline.handle, VK_DEBUG_REPORT_OBJECT_TYPE_PIPELINE_EXT, "sprite");

	dynamic_state_create_info.dynamicStateCount--;

	//================
	// Sky
	//================
	pipeline_create_info.renderPass = vulkan_globals.main_render_pass;

	pipeline_create_info.stageCount = 1;
	shader_stages[1].module = VK_NULL_HANDLE;

	depth_stencil_state_create_info.depthTestEnable = VK_TRUE;
	depth_stencil_state_create_info.depthWriteEnable = VK_TRUE;
	depth_stencil_state_create_info.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
	depth_stencil_state_create_info.stencilTestEnable = VK_TRUE;
	depth_stencil_state_create_info.front.compareOp = VK_COMPARE_OP_ALWAYS;
	depth_stencil_state_create_info.front.failOp = VK_STENCIL_OP_KEEP;
	depth_stencil_state_create_info.front.depthFailOp = VK_STENCIL_OP_KEEP;
	depth_stencil_state_create_info.front.passOp = VK_STENCIL_OP_REPLACE;
	depth_stencil_state_create_info.front.compareMask = 0xFF;
	depth_stencil_state_create_info.front.writeMask = 0xFF;
	depth_stencil_state_create_info.front.reference = 0x1;

	blend_attachment_state.colorWriteMask = 0; 	// We only want to write stencil

	assert(vulkan_globals.sky_stencil_pipeline.handle == VK_NULL_HANDLE);
	err = vkCreateGraphicsPipelines(vulkan_globals.device, VK_NULL_HANDLE, 1, &pipeline_create_info, NULL, &vulkan_globals.sky_stencil_pipeline.handle);
	if (err != VK_SUCCESS)
		Sys_Error("vkCreateGraphicsPipelines failed");
	vulkan_globals.sky_stencil_pipeline.layout = vulkan_globals.basic_pipeline_layout;

	GL_SetObjectName((uint64_t)vulkan_globals.sky_stencil_pipeline.handle, VK_DEBUG_REPORT_OBJECT_TYPE_PIPELINE_EXT, "sky_stencil");

	depth_stencil_state_create_info.stencilTestEnable = VK_FALSE;
	blend_attachment_state.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	pipeline_create_info.stageCount = 2;

	shader_stages[1].module = basic_notex_frag_module;

	assert(vulkan_globals.sky_color_pipeline.handle == VK_NULL_HANDLE);
	err = vkCreateGraphicsPipelines(vulkan_globals.device, VK_NULL_HANDLE, 1, &pipeline_create_info, NULL, &vulkan_globals.sky_color_pipeline.handle);
	if (err != VK_SUCCESS)
		Sys_Error("vkCreateGraphicsPipelines failed");
	vulkan_globals.sky_color_pipeline.layout = vulkan_globals.basic_pipeline_layout;

	GL_SetObjectName((uint64_t)vulkan_globals.sky_color_pipeline.handle, VK_DEBUG_REPORT_OBJECT_TYPE_PIPELINE_EXT, "sky_color");

	depth_stencil_state_create_info.depthTestEnable = VK_FALSE;
	depth_stencil_state_create_info.depthWriteEnable = VK_FALSE;
	depth_stencil_state_create_info.stencilTestEnable = VK_TRUE;
	depth_stencil_state_create_info.front.compareOp = VK_COMPARE_OP_EQUAL;
	depth_stencil_state_create_info.front.failOp = VK_STENCIL_OP_KEEP;
	depth_stencil_state_create_info.front.depthFailOp = VK_STENCIL_OP_KEEP;
	depth_stencil_state_create_info.front.passOp = VK_STENCIL_OP_KEEP;
	depth_stencil_state_create_info.front.compareMask = 0xFF;
	depth_stencil_state_create_info.front.writeMask = 0x0;
	depth_stencil_state_create_info.front.reference = 0x1;
	shader_stages[1].module = sky_box_frag_module;

	assert(vulkan_globals.sky_box_pipeline.handle == VK_NULL_HANDLE);
	err = vkCreateGraphicsPipelines(vulkan_globals.device, VK_NULL_HANDLE, 1, &pipeline_create_info, NULL, &vulkan_globals.sky_box_pipeline.handle);
	if (err != VK_SUCCESS)
		Sys_Error("vkCreateGraphicsPipelines failed");

	GL_SetObjectName((uint64_t)vulkan_globals.sky_box_pipeline.handle, VK_DEBUG_REPORT_OBJECT_TYPE_PIPELINE_EXT, "sky_box");
	vulkan_globals.sky_box_pipeline.layout = vulkan_globals.basic_pipeline_layout;

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

	pipeline_create_info.layout = vulkan_globals.sky_layer_pipeline.layout.handle;

	assert(vulkan_globals.sky_layer_pipeline.handle == VK_NULL_HANDLE);
	err = vkCreateGraphicsPipelines(vulkan_globals.device, VK_NULL_HANDLE, 1, &pipeline_create_info, NULL, &vulkan_globals.sky_layer_pipeline.handle);
	if (err != VK_SUCCESS)
		Sys_Error("vkCreateGraphicsPipelines failed");

	GL_SetObjectName((uint64_t)vulkan_globals.sky_layer_pipeline.handle, VK_DEBUG_REPORT_OBJECT_TYPE_PIPELINE_EXT, "sky_layer");

	depth_stencil_state_create_info.stencilTestEnable = VK_FALSE;

	//================
	// Show triangles
	//================
	if (vulkan_globals.non_solid_fill)
	{
		rasterization_state_create_info.cullMode = VK_CULL_MODE_NONE;
		rasterization_state_create_info.polygonMode = VK_POLYGON_MODE_LINE;
		depth_stencil_state_create_info.depthTestEnable = VK_FALSE;
		depth_stencil_state_create_info.depthWriteEnable = VK_FALSE;
		input_assembly_state_create_info.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

		VkVertexInputAttributeDescription showtris_vertex_input_attribute_descriptions;
		showtris_vertex_input_attribute_descriptions.binding = 0;
		showtris_vertex_input_attribute_descriptions.format = VK_FORMAT_R32G32B32_SFLOAT;
		showtris_vertex_input_attribute_descriptions.location = 0;
		showtris_vertex_input_attribute_descriptions.offset = 0;

		VkVertexInputBindingDescription showtris_vertex_binding_description;
		showtris_vertex_binding_description.binding = 0;
		showtris_vertex_binding_description.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
		showtris_vertex_binding_description.stride = 24;

		vertex_input_state_create_info.vertexAttributeDescriptionCount = 1;
		vertex_input_state_create_info.pVertexAttributeDescriptions = &showtris_vertex_input_attribute_descriptions;
		vertex_input_state_create_info.vertexBindingDescriptionCount = 1;
		vertex_input_state_create_info.pVertexBindingDescriptions = &showtris_vertex_binding_description;

		shader_stages[0].module = showtris_vert_module;
		shader_stages[1].module = showtris_frag_module;

		pipeline_create_info.layout = vulkan_globals.basic_pipeline_layout.handle;

		assert(vulkan_globals.showtris_pipeline.handle == VK_NULL_HANDLE);
		err = vkCreateGraphicsPipelines(vulkan_globals.device, VK_NULL_HANDLE, 1, &pipeline_create_info, NULL, &vulkan_globals.showtris_pipeline.handle);
		if (err != VK_SUCCESS)
			Sys_Error("vkCreateGraphicsPipelines failed");
		vulkan_globals.showtris_pipeline.layout = vulkan_globals.basic_pipeline_layout;

		GL_SetObjectName((uint64_t)vulkan_globals.showtris_pipeline.handle, VK_DEBUG_REPORT_OBJECT_TYPE_PIPELINE_EXT, "showtris");

		depth_stencil_state_create_info.depthTestEnable = VK_TRUE;
		rasterization_state_create_info.depthBiasEnable = VK_TRUE;
		rasterization_state_create_info.depthBiasConstantFactor = (vulkan_globals.depth_format != VK_FORMAT_D16_UNORM) ? -250.0f : -1.25f;
		rasterization_state_create_info.depthBiasSlopeFactor = 0.0f;

		assert(vulkan_globals.showtris_depth_test_pipeline.handle == VK_NULL_HANDLE);
		err = vkCreateGraphicsPipelines(vulkan_globals.device, VK_NULL_HANDLE, 1, &pipeline_create_info, NULL, &vulkan_globals.showtris_depth_test_pipeline.handle);
		if (err != VK_SUCCESS)
			Sys_Error("vkCreateGraphicsPipelines failed");
		vulkan_globals.showtris_depth_test_pipeline.layout = vulkan_globals.basic_pipeline_layout;

		GL_SetObjectName((uint64_t)vulkan_globals.showtris_depth_test_pipeline.handle, VK_DEBUG_REPORT_OBJECT_TYPE_PIPELINE_EXT, "showtris_depth_test");
	}

	//================
	// World pipelines
	//================
	rasterization_state_create_info.cullMode = VK_CULL_MODE_BACK_BIT;
	rasterization_state_create_info.polygonMode = VK_POLYGON_MODE_FILL;
	depth_stencil_state_create_info.depthTestEnable = VK_TRUE;
	depth_stencil_state_create_info.depthWriteEnable = VK_TRUE;
	rasterization_state_create_info.depthBiasEnable = VK_FALSE;
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

	VkSpecializationMapEntry specialization_entries[3];
	specialization_entries[0].constantID = 0;
	specialization_entries[0].offset = 0;
	specialization_entries[0].size = 4;
	specialization_entries[1].constantID = 1;
	specialization_entries[1].offset = 4;
	specialization_entries[1].size = 4;
	specialization_entries[2].constantID = 2;
	specialization_entries[2].offset = 8;
	specialization_entries[2].size = 4;

	uint32_t specialization_data[3];
	specialization_data[0] = 0;
	specialization_data[1] = 0;
	specialization_data[2] = 0;

	VkSpecializationInfo specialization_info;
	specialization_info.mapEntryCount = 3;
	specialization_info.pMapEntries = specialization_entries;
	specialization_info.dataSize = 12;
	specialization_info.pData = specialization_data;
	
	pipeline_create_info.layout = vulkan_globals.world_pipeline_layout.handle;

	shader_stages[0].module = world_vert_module;
	shader_stages[1].module = world_frag_module;
	shader_stages[1].pSpecializationInfo = &specialization_info;

	for (alpha_blend = 0; alpha_blend < 2; ++alpha_blend) {
		for (alpha_test = 0; alpha_test < 2; ++alpha_test) {
			for (fullbright_enabled = 0; fullbright_enabled < 2; ++fullbright_enabled) {
				int pipeline_index = fullbright_enabled + (alpha_test * 2) + (alpha_blend * 4);

				specialization_data[0] = fullbright_enabled;
				specialization_data[1] = alpha_test;
				specialization_data[2] = alpha_blend;

				blend_attachment_state.blendEnable = alpha_blend ? VK_TRUE : VK_FALSE;
				depth_stencil_state_create_info.depthWriteEnable = alpha_blend ? VK_FALSE : VK_TRUE;
				if ( pipeline_index > 0 ) {
					pipeline_create_info.flags = VK_PIPELINE_CREATE_DERIVATIVE_BIT;
					pipeline_create_info.basePipelineHandle = vulkan_globals.world_pipelines[0].handle;
					pipeline_create_info.basePipelineIndex = -1;
				} else {
					pipeline_create_info.flags = VK_PIPELINE_CREATE_ALLOW_DERIVATIVES_BIT;
				}

				assert(vulkan_globals.world_pipelines[pipeline_index].handle == VK_NULL_HANDLE);
				err = vkCreateGraphicsPipelines(vulkan_globals.device, VK_NULL_HANDLE, 1, &pipeline_create_info, NULL, &vulkan_globals.world_pipelines[pipeline_index].handle);
				if (err != VK_SUCCESS)
					Sys_Error("vkCreateGraphicsPipelines failed");
				vulkan_globals.world_pipelines[pipeline_index].layout = vulkan_globals.world_pipeline_layout;

				GL_SetObjectName((uint64_t)vulkan_globals.world_pipelines[pipeline_index].handle, VK_DEBUG_REPORT_OBJECT_TYPE_PIPELINE_EXT, va("world %d", pipeline_index));
			}
		}
	}

	depth_stencil_state_create_info.depthTestEnable = VK_TRUE;
	depth_stencil_state_create_info.depthWriteEnable = VK_TRUE;
	pipeline_create_info.flags = 0;
	blend_attachment_state.blendEnable = VK_FALSE;
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
	alias_vertex_input_attribute_descriptions[1].format = VK_FORMAT_R8G8B8A8_UNORM;
	alias_vertex_input_attribute_descriptions[1].location = 1;
	alias_vertex_input_attribute_descriptions[1].offset = 0;
	alias_vertex_input_attribute_descriptions[2].binding = 1;
	alias_vertex_input_attribute_descriptions[2].format = VK_FORMAT_R8G8B8A8_SNORM;
	alias_vertex_input_attribute_descriptions[2].location = 2;
	alias_vertex_input_attribute_descriptions[2].offset = 4;
	alias_vertex_input_attribute_descriptions[3].binding = 2;
	alias_vertex_input_attribute_descriptions[3].format = VK_FORMAT_R8G8B8A8_UNORM;
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

	pipeline_create_info.layout = vulkan_globals.alias_pipeline.layout.handle;

	assert(vulkan_globals.alias_pipeline.handle == VK_NULL_HANDLE);
	err = vkCreateGraphicsPipelines(vulkan_globals.device, VK_NULL_HANDLE, 1, &pipeline_create_info, NULL, &vulkan_globals.alias_pipeline.handle);
	if (err != VK_SUCCESS)
		Sys_Error("vkCreateGraphicsPipelines failed");

	GL_SetObjectName((uint64_t)vulkan_globals.alias_pipeline.handle, VK_DEBUG_REPORT_OBJECT_TYPE_PIPELINE_EXT, "alias");

	shader_stages[1].module = alias_alphatest_frag_module;

	assert(vulkan_globals.alias_alphatest_pipeline.handle == VK_NULL_HANDLE);
	err = vkCreateGraphicsPipelines(vulkan_globals.device, VK_NULL_HANDLE, 1, &pipeline_create_info, NULL, &vulkan_globals.alias_alphatest_pipeline.handle);
	if (err != VK_SUCCESS)
		Sys_Error("vkCreateGraphicsPipelines failed");
	vulkan_globals.alias_alphatest_pipeline.layout = vulkan_globals.alias_pipeline.layout;

	GL_SetObjectName((uint64_t)vulkan_globals.alias_alphatest_pipeline.handle, VK_DEBUG_REPORT_OBJECT_TYPE_PIPELINE_EXT, "alias_alphatest");

	depth_stencil_state_create_info.depthWriteEnable = VK_FALSE;
	blend_attachment_state.blendEnable = VK_TRUE;
	shader_stages[1].module = alias_frag_module;

	assert(vulkan_globals.alias_blend_pipeline.handle == VK_NULL_HANDLE);
	err = vkCreateGraphicsPipelines(vulkan_globals.device, VK_NULL_HANDLE, 1, &pipeline_create_info, NULL, &vulkan_globals.alias_blend_pipeline.handle);
	if (err != VK_SUCCESS)
		Sys_Error("vkCreateGraphicsPipelines failed");
	vulkan_globals.alias_blend_pipeline.layout = vulkan_globals.alias_pipeline.layout;

	GL_SetObjectName((uint64_t)vulkan_globals.alias_blend_pipeline.handle, VK_DEBUG_REPORT_OBJECT_TYPE_PIPELINE_EXT, "alias_blend");

	if (vulkan_globals.non_solid_fill)
	{
		rasterization_state_create_info.cullMode = VK_CULL_MODE_NONE;
		rasterization_state_create_info.polygonMode = VK_POLYGON_MODE_LINE;
		depth_stencil_state_create_info.depthTestEnable = VK_FALSE;
		depth_stencil_state_create_info.depthWriteEnable = VK_FALSE;
		blend_attachment_state.blendEnable = VK_FALSE;
		input_assembly_state_create_info.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

		shader_stages[0].module = alias_vert_module;
		shader_stages[1].module = showtris_frag_module;

		pipeline_create_info.layout = vulkan_globals.alias_pipeline.layout.handle;

		assert(vulkan_globals.alias_showtris_pipeline.handle == VK_NULL_HANDLE);
		err = vkCreateGraphicsPipelines(vulkan_globals.device, VK_NULL_HANDLE, 1, &pipeline_create_info, NULL, &vulkan_globals.alias_showtris_pipeline.handle);
		if (err != VK_SUCCESS)
			Sys_Error("vkCreateGraphicsPipelines failed");
		vulkan_globals.alias_showtris_pipeline.layout = vulkan_globals.alias_pipeline.layout;

		GL_SetObjectName((uint64_t)vulkan_globals.alias_showtris_pipeline.handle, VK_DEBUG_REPORT_OBJECT_TYPE_PIPELINE_EXT, "alias_showtris");

		depth_stencil_state_create_info.depthTestEnable = VK_TRUE;
		rasterization_state_create_info.depthBiasEnable = VK_TRUE;
		rasterization_state_create_info.depthBiasConstantFactor = (vulkan_globals.depth_format != VK_FORMAT_D16_UNORM) ? -250.0f : -1.25f;
		rasterization_state_create_info.depthBiasSlopeFactor = 0.0f;

		assert(vulkan_globals.alias_showtris_depth_test_pipeline.handle == VK_NULL_HANDLE);
		err = vkCreateGraphicsPipelines(vulkan_globals.device, VK_NULL_HANDLE, 1, &pipeline_create_info, NULL, &vulkan_globals.alias_showtris_depth_test_pipeline.handle);
		if (err != VK_SUCCESS)
			Sys_Error("vkCreateGraphicsPipelines failed");
		vulkan_globals.alias_showtris_depth_test_pipeline.layout = vulkan_globals.alias_pipeline.layout;

		GL_SetObjectName((uint64_t)vulkan_globals.alias_showtris_depth_test_pipeline.handle, VK_DEBUG_REPORT_OBJECT_TYPE_PIPELINE_EXT, "alias_showtris_depth_test");
	}

	//================
	// Postprocess pipeline
	//================
	multisample_state_create_info.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
	rasterization_state_create_info.polygonMode = VK_POLYGON_MODE_FILL;
	rasterization_state_create_info.cullMode = VK_CULL_MODE_NONE;
	rasterization_state_create_info.depthBiasEnable = VK_FALSE;
	depth_stencil_state_create_info.depthTestEnable = VK_TRUE;
	depth_stencil_state_create_info.depthWriteEnable = VK_TRUE;
	blend_attachment_state.blendEnable = VK_FALSE;

	vertex_input_state_create_info.vertexAttributeDescriptionCount = 0;
	vertex_input_state_create_info.pVertexAttributeDescriptions = NULL;
	vertex_input_state_create_info.vertexBindingDescriptionCount = 0;
	vertex_input_state_create_info.pVertexBindingDescriptions = NULL;

	shader_stages[0].module = postprocess_vert_module;
	shader_stages[1].module = postprocess_frag_module;
	pipeline_create_info.renderPass = vulkan_globals.ui_render_pass;
	pipeline_create_info.layout = vulkan_globals.postprocess_pipeline.layout.handle;
	pipeline_create_info.subpass = 1;

	assert(vulkan_globals.postprocess_pipeline.handle == VK_NULL_HANDLE);
	err = vkCreateGraphicsPipelines(vulkan_globals.device, VK_NULL_HANDLE, 1, &pipeline_create_info, NULL, &vulkan_globals.postprocess_pipeline.handle);
	if (err != VK_SUCCESS)
		Sys_Error("vkCreateGraphicsPipelines failed");

	GL_SetObjectName((uint64_t)vulkan_globals.postprocess_pipeline.handle, VK_DEBUG_REPORT_OBJECT_TYPE_PIPELINE_EXT, "postprocess");

	//================
	// Screen Effects
	//================
	VkPipelineShaderStageCreateInfo compute_shader_stage;
	memset(&compute_shader_stage, 0, sizeof(compute_shader_stage));
	compute_shader_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	compute_shader_stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	compute_shader_stage.module = (vulkan_globals.color_format == VK_FORMAT_A2B10G10R10_UNORM_PACK32) ? screen_effects_10bit_comp_module : screen_effects_8bit_comp_module;
	compute_shader_stage.pName = "main";

	VkComputePipelineCreateInfo compute_pipeline_create_info;
	memset(&compute_pipeline_create_info, 0, sizeof(compute_pipeline_create_info));
	compute_pipeline_create_info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
	compute_pipeline_create_info.stage = compute_shader_stage;
	compute_pipeline_create_info.layout = vulkan_globals.screen_effects_pipeline.layout.handle;

	assert(vulkan_globals.screen_effects_pipeline.handle == VK_NULL_HANDLE);
	err = vkCreateComputePipelines(vulkan_globals.device, VK_NULL_HANDLE, 1, &compute_pipeline_create_info, NULL, &vulkan_globals.screen_effects_pipeline.handle);
	if (err != VK_SUCCESS)
		Sys_Error("vkCreateGraphicsPipelines failed");
	GL_SetObjectName((uint64_t)vulkan_globals.screen_effects_pipeline.handle, VK_DEBUG_REPORT_OBJECT_TYPE_PIPELINE_EXT, "screen_effects");

	compute_shader_stage.module = (vulkan_globals.color_format == VK_FORMAT_A2B10G10R10_UNORM_PACK32) ? screen_effects_10bit_scale_comp_module : screen_effects_8bit_scale_comp_module;
	compute_pipeline_create_info.stage = compute_shader_stage;
	assert(vulkan_globals.screen_effects_scale_pipeline.handle == VK_NULL_HANDLE);
	err = vkCreateComputePipelines(vulkan_globals.device, VK_NULL_HANDLE, 1, &compute_pipeline_create_info, NULL, &vulkan_globals.screen_effects_scale_pipeline.handle);
	if (err != VK_SUCCESS)
		Sys_Error("vkCreateGraphicsPipelines failed");
	GL_SetObjectName((uint64_t)vulkan_globals.screen_effects_scale_pipeline.handle, VK_DEBUG_REPORT_OBJECT_TYPE_PIPELINE_EXT, "screen_effects_scale");

	if (vulkan_globals.screen_effects_sops)
	{
		compute_shader_stage.module = (vulkan_globals.color_format == VK_FORMAT_A2B10G10R10_UNORM_PACK32) ? screen_effects_10bit_scale_sops_comp_module : screen_effects_8bit_scale_sops_comp_module;
		compute_pipeline_create_info.stage = compute_shader_stage;
		assert(vulkan_globals.screen_effects_scale_sops_pipeline.handle == VK_NULL_HANDLE);
		err = vkCreateComputePipelines(vulkan_globals.device, VK_NULL_HANDLE, 1, &compute_pipeline_create_info, NULL, &vulkan_globals.screen_effects_scale_sops_pipeline.handle);
		if (err != VK_SUCCESS)
			Sys_Error("vkCreateGraphicsPipelines failed");
		GL_SetObjectName((uint64_t)vulkan_globals.screen_effects_scale_sops_pipeline.handle, VK_DEBUG_REPORT_OBJECT_TYPE_PIPELINE_EXT, "screen_effects_scale_sops");
	}

	//================
	// Texture Warp
	//================
	memset(&compute_shader_stage, 0, sizeof(compute_shader_stage));
	compute_shader_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	compute_shader_stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	compute_shader_stage.module = cs_tex_warp_comp_module;
	compute_shader_stage.pName = "main";

	memset(&compute_pipeline_create_info, 0, sizeof(compute_pipeline_create_info));
	compute_pipeline_create_info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
	compute_pipeline_create_info.stage = compute_shader_stage;
	compute_pipeline_create_info.layout = vulkan_globals.cs_tex_warp_pipeline.layout.handle;

	assert(vulkan_globals.cs_tex_warp_pipeline.handle == VK_NULL_HANDLE);
	err = vkCreateComputePipelines(vulkan_globals.device, VK_NULL_HANDLE, 1, &compute_pipeline_create_info, NULL, &vulkan_globals.cs_tex_warp_pipeline.handle);
	if (err != VK_SUCCESS)
		Sys_Error("vkCreateGraphicsPipelines failed");

	GL_SetObjectName((uint64_t)vulkan_globals.cs_tex_warp_pipeline.handle, VK_DEBUG_REPORT_OBJECT_TYPE_PIPELINE_EXT, "cs_tex_warp");

	vkDestroyShaderModule(vulkan_globals.device, showtris_frag_module, NULL);
	vkDestroyShaderModule(vulkan_globals.device, showtris_vert_module, NULL);
	vkDestroyShaderModule(vulkan_globals.device, cs_tex_warp_comp_module, NULL);
	vkDestroyShaderModule(vulkan_globals.device, screen_effects_8bit_comp_module, NULL);
	vkDestroyShaderModule(vulkan_globals.device, screen_effects_8bit_scale_comp_module, NULL);
	if (screen_effects_8bit_scale_sops_comp_module != VK_NULL_HANDLE)
		vkDestroyShaderModule(vulkan_globals.device, screen_effects_8bit_scale_sops_comp_module, NULL);
	vkDestroyShaderModule(vulkan_globals.device, screen_effects_10bit_comp_module, NULL);
	vkDestroyShaderModule(vulkan_globals.device, screen_effects_10bit_scale_comp_module, NULL);
	if (screen_effects_10bit_scale_sops_comp_module != VK_NULL_HANDLE)
		vkDestroyShaderModule(vulkan_globals.device, screen_effects_10bit_scale_sops_comp_module, NULL);
	vkDestroyShaderModule(vulkan_globals.device, postprocess_frag_module, NULL);
	vkDestroyShaderModule(vulkan_globals.device, postprocess_vert_module, NULL);
	vkDestroyShaderModule(vulkan_globals.device, sky_layer_frag_module, NULL);
	vkDestroyShaderModule(vulkan_globals.device, sky_layer_vert_module, NULL);
	vkDestroyShaderModule(vulkan_globals.device, alias_frag_module, NULL);
	vkDestroyShaderModule(vulkan_globals.device, alias_alphatest_frag_module, NULL);
	vkDestroyShaderModule(vulkan_globals.device, alias_vert_module, NULL);
	vkDestroyShaderModule(vulkan_globals.device, world_frag_module, NULL);
	vkDestroyShaderModule(vulkan_globals.device, world_vert_module, NULL);
	vkDestroyShaderModule(vulkan_globals.device, basic_notex_frag_module, NULL);
	vkDestroyShaderModule(vulkan_globals.device, basic_alphatest_frag_module, NULL);
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
	for (i = 0; i < 2; ++i)
	{
		vkDestroyPipeline(vulkan_globals.device, vulkan_globals.basic_alphatest_pipeline[i].handle, NULL);
		vulkan_globals.basic_alphatest_pipeline[i].handle = VK_NULL_HANDLE;
		vkDestroyPipeline(vulkan_globals.device, vulkan_globals.basic_blend_pipeline[i].handle, NULL);
		vulkan_globals.basic_blend_pipeline[i].handle = VK_NULL_HANDLE;
		vkDestroyPipeline(vulkan_globals.device, vulkan_globals.basic_notex_blend_pipeline[i].handle, NULL);
		vulkan_globals.basic_notex_blend_pipeline[i].handle = VK_NULL_HANDLE;
	}
	vkDestroyPipeline(vulkan_globals.device, vulkan_globals.basic_poly_blend_pipeline.handle, NULL);
	vulkan_globals.basic_poly_blend_pipeline.handle = VK_NULL_HANDLE;
	for (i = 0; i < WORLD_PIPELINE_COUNT; ++i) {
		vkDestroyPipeline(vulkan_globals.device, vulkan_globals.world_pipelines[i].handle, NULL);
		vulkan_globals.world_pipelines[i].handle = VK_NULL_HANDLE;
	}
	vkDestroyPipeline(vulkan_globals.device, vulkan_globals.water_pipeline.handle, NULL);
	vulkan_globals.water_pipeline.handle = VK_NULL_HANDLE;
	vkDestroyPipeline(vulkan_globals.device, vulkan_globals.water_blend_pipeline.handle, NULL);
	vulkan_globals.water_blend_pipeline.handle = VK_NULL_HANDLE;
	vkDestroyPipeline(vulkan_globals.device, vulkan_globals.raster_tex_warp_pipeline.handle, NULL);
	vulkan_globals.raster_tex_warp_pipeline.handle = VK_NULL_HANDLE;
	vkDestroyPipeline(vulkan_globals.device, vulkan_globals.particle_pipeline.handle, NULL);
	vulkan_globals.particle_pipeline.handle = VK_NULL_HANDLE;
	vkDestroyPipeline(vulkan_globals.device, vulkan_globals.sprite_pipeline.handle, NULL);
	vulkan_globals.sprite_pipeline.handle = VK_NULL_HANDLE;
	vkDestroyPipeline(vulkan_globals.device, vulkan_globals.sky_stencil_pipeline.handle, NULL);
	vulkan_globals.sky_stencil_pipeline.handle = VK_NULL_HANDLE;
	vkDestroyPipeline(vulkan_globals.device, vulkan_globals.sky_color_pipeline.handle, NULL);
	vulkan_globals.sky_color_pipeline.handle = VK_NULL_HANDLE;
	vkDestroyPipeline(vulkan_globals.device, vulkan_globals.sky_box_pipeline.handle, NULL);
	vulkan_globals.sky_box_pipeline.handle = VK_NULL_HANDLE;
	vkDestroyPipeline(vulkan_globals.device, vulkan_globals.sky_layer_pipeline.handle, NULL);
	vulkan_globals.sky_layer_pipeline.handle = VK_NULL_HANDLE;
	vkDestroyPipeline(vulkan_globals.device, vulkan_globals.alias_pipeline.handle, NULL);
	vulkan_globals.alias_pipeline.handle = VK_NULL_HANDLE;
	vkDestroyPipeline(vulkan_globals.device, vulkan_globals.alias_alphatest_pipeline.handle, NULL);
	vulkan_globals.alias_alphatest_pipeline.handle = VK_NULL_HANDLE;
	vkDestroyPipeline(vulkan_globals.device, vulkan_globals.alias_blend_pipeline.handle, NULL);
	vulkan_globals.alias_blend_pipeline.handle = VK_NULL_HANDLE;
	vkDestroyPipeline(vulkan_globals.device, vulkan_globals.postprocess_pipeline.handle, NULL);
	vulkan_globals.postprocess_pipeline.handle = VK_NULL_HANDLE;
	vkDestroyPipeline(vulkan_globals.device, vulkan_globals.screen_effects_pipeline.handle, NULL);
	vulkan_globals.screen_effects_pipeline.handle = VK_NULL_HANDLE;
	vkDestroyPipeline(vulkan_globals.device, vulkan_globals.screen_effects_scale_pipeline.handle, NULL);
	vulkan_globals.screen_effects_scale_pipeline.handle = VK_NULL_HANDLE;
	if (vulkan_globals.screen_effects_scale_sops_pipeline.handle != VK_NULL_HANDLE)
	{
		vkDestroyPipeline(vulkan_globals.device, vulkan_globals.screen_effects_scale_sops_pipeline.handle, NULL);
		vulkan_globals.screen_effects_scale_sops_pipeline.handle = VK_NULL_HANDLE;
	}
	vkDestroyPipeline(vulkan_globals.device, vulkan_globals.cs_tex_warp_pipeline.handle, NULL);
	vulkan_globals.cs_tex_warp_pipeline.handle = VK_NULL_HANDLE;
	if (vulkan_globals.showtris_pipeline.handle != VK_NULL_HANDLE)
	{
		vkDestroyPipeline(vulkan_globals.device, vulkan_globals.showtris_pipeline.handle, NULL);
		vulkan_globals.showtris_pipeline.handle = VK_NULL_HANDLE;
		vkDestroyPipeline(vulkan_globals.device, vulkan_globals.showtris_depth_test_pipeline.handle, NULL);
		vulkan_globals.showtris_depth_test_pipeline.handle = VK_NULL_HANDLE;
	}
	if (vulkan_globals.alias_showtris_pipeline.handle != VK_NULL_HANDLE)
	{
		vkDestroyPipeline(vulkan_globals.device, vulkan_globals.alias_showtris_pipeline.handle, NULL);
		vulkan_globals.alias_showtris_pipeline.handle = VK_NULL_HANDLE;
		vkDestroyPipeline(vulkan_globals.device, vulkan_globals.alias_showtris_depth_test_pipeline.handle, NULL);
		vulkan_globals.alias_showtris_depth_test_pipeline.handle = VK_NULL_HANDLE;
	}
}

/*
===============
R_Init
===============
*/
void R_Init (void)
{
	Cmd_AddCommand ("timerefresh", R_TimeRefresh_f);
	Cmd_AddCommand ("pointfile", R_ReadPointFile_f);
	Cmd_AddCommand ("vkmemstats", R_VulkanMemStats_f);

	Cvar_RegisterVariable (&r_fullbright);
	Cvar_RegisterVariable (&r_lightmap);
	Cvar_RegisterVariable (&r_drawentities);
	Cvar_RegisterVariable (&r_drawviewmodel);
	Cvar_RegisterVariable (&r_wateralpha);
	Cvar_SetCallback (&r_wateralpha, R_SetWateralpha_f);
	Cvar_RegisterVariable (&r_dynamic);
	Cvar_RegisterVariable (&r_novis);
#if defined(USE_SIMD)
	Cvar_RegisterVariable (&r_simd);
	Cvar_SetCallback (&r_simd, R_SIMD_f);
	R_SIMD_f(&r_simd);
#endif
	Cvar_RegisterVariable (&r_speeds);
	Cvar_RegisterVariable (&r_pos);
	Cvar_RegisterVariable (&gl_polyblend);
	Cvar_RegisterVariable (&gl_nocolors);

	//johnfitz -- new cvars
	Cvar_RegisterVariable (&r_clearcolor);
	Cvar_SetCallback (&r_clearcolor, R_SetClearColor_f);
	Cvar_RegisterVariable (&r_waterquality);
	Cvar_RegisterVariable (&r_waterwarp);
	Cvar_RegisterVariable (&r_waterwarpcompute);
	Cvar_RegisterVariable (&r_flatlightstyles);
	Cvar_RegisterVariable (&r_oldskyleaf);
	Cvar_RegisterVariable (&r_drawworld);
	Cvar_RegisterVariable (&r_showtris);
	Cvar_RegisterVariable (&gl_farclip);
	Cvar_RegisterVariable (&gl_fullbrights);
	Cvar_SetCallback (&gl_fullbrights, GL_Fullbrights_f);
	Cvar_RegisterVariable (&r_lerpmodels);
	Cvar_RegisterVariable (&r_lerpmove);
	Cvar_RegisterVariable (&r_nolerp_list);
	Cvar_SetCallback (&r_nolerp_list, R_Model_ExtraFlags_List_f);
	//johnfitz

	Cvar_RegisterVariable (&gl_zfix); // QuakeSpasm z-fighting fix
	Cvar_RegisterVariable (&r_lavaalpha);
	Cvar_RegisterVariable (&r_telealpha);
	Cvar_RegisterVariable (&r_slimealpha);
	Cvar_RegisterVariable (&r_scale);
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
R_AllocateDescriptorSet
===============
*/
VkDescriptorSet R_AllocateDescriptorSet(vulkan_desc_set_layout_t * layout)
{
	VkDescriptorSetAllocateInfo descriptor_set_allocate_info;
	memset(&descriptor_set_allocate_info, 0, sizeof(descriptor_set_allocate_info));
	descriptor_set_allocate_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	descriptor_set_allocate_info.descriptorPool = vulkan_globals.descriptor_pool;
	descriptor_set_allocate_info.descriptorSetCount = 1;
	descriptor_set_allocate_info.pSetLayouts = &layout->handle;

	VkDescriptorSet handle;
	vkAllocateDescriptorSets(vulkan_globals.device, &descriptor_set_allocate_info, &handle);

	num_vulkan_combined_image_samplers += layout->num_combined_image_samplers;
	num_vulkan_ubos_dynamic += layout->num_ubos_dynamic;
	num_vulkan_input_attachments += layout->num_input_attachments;
	num_vulkan_storage_images += layout->num_storage_images;

	return handle;
}

/*
===============
R_FreeDescriptorSet
===============
*/

void R_FreeDescriptorSet(VkDescriptorSet desc_set, vulkan_desc_set_layout_t * layout)
{
	vkFreeDescriptorSets(vulkan_globals.device, vulkan_globals.descriptor_pool, 1, &desc_set);

	num_vulkan_combined_image_samplers -= layout->num_combined_image_samplers;
	num_vulkan_ubos_dynamic -= layout->num_ubos_dynamic;
	num_vulkan_input_attachments -= layout->num_input_attachments;
	num_vulkan_storage_images -= layout->num_storage_images;
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
	currententity = &cl.entities[1+playernum];

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

	map_fallbackalpha = r_wateralpha.value;
	map_wateralpha = (cl.worldmodel->contentstransparent&SURF_DRAWWATER)?r_wateralpha.value:1;
	map_lavaalpha = (cl.worldmodel->contentstransparent&SURF_DRAWLAVA)?r_lavaalpha.value:1;
	map_telealpha = (cl.worldmodel->contentstransparent&SURF_DRAWTELE)?r_telealpha.value:1;
	map_slimealpha = (cl.worldmodel->contentstransparent&SURF_DRAWSLIME)?r_slimealpha.value:1;

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
			q_strlcpy(key, com_token + 1, sizeof(key));
		else
			q_strlcpy(key, com_token, sizeof(key));
		while (key[0] && key[strlen(key)-1] == ' ') // remove trailing spaces
			key[strlen(key)-1] = 0;
		data = COM_Parse(data);
		if (!data)
			return; // error
		q_strlcpy(value, com_token, sizeof(value));

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
		GL_EndRendering (true);
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
	Con_Printf("Descriptors:\n");
	Con_Printf(" Combined image samplers: %d\n", num_vulkan_combined_image_samplers );
	Con_Printf(" Dynamic UBOs: %d\n", num_vulkan_ubos_dynamic );
	Con_Printf(" Input attachments: %d\n", num_vulkan_input_attachments );
	Con_Printf(" Storage images: %d\n", num_vulkan_storage_images );
}
