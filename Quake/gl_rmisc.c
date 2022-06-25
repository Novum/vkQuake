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
#include <float.h>

cvar_t r_lodbias = {"r_lodbias", "1", CVAR_ARCHIVE};

// johnfitz -- new cvars
extern cvar_t r_clearcolor;
extern cvar_t r_fastclear;
extern cvar_t r_flatlightstyles;
extern cvar_t r_lerplightstyles;
extern cvar_t gl_fullbrights;
extern cvar_t gl_farclip;
extern cvar_t r_waterquality;
extern cvar_t r_waterwarp;
extern cvar_t r_waterwarpcompute;
extern cvar_t r_oldskyleaf;
extern cvar_t r_drawworld;
extern cvar_t r_showtris;
extern cvar_t r_showbboxes;
extern cvar_t r_lerpmodels;
extern cvar_t r_lerpmove;
extern cvar_t r_nolerp_list;
// johnfitz
extern cvar_t gl_zfix; // QuakeSpasm z-fighting fix

extern cvar_t r_gpulightmapupdate;
extern cvar_t r_tasks;
extern cvar_t r_parallelmark;
extern cvar_t r_usesops;

#if defined(USE_SIMD)
extern cvar_t r_simd;
#endif
extern gltexture_t *playertextures[MAX_SCOREBOARD]; // johnfitz

vulkanglobals_t vulkan_globals;

int    num_vulkan_tex_allocations = 0;
int    num_vulkan_bmodel_allocations = 0;
int    num_vulkan_mesh_allocations = 0;
int    num_vulkan_misc_allocations = 0;
int    num_vulkan_dynbuf_allocations = 0;
int    num_vulkan_combined_image_samplers = 0;
int    num_vulkan_ubos_dynamic = 0;
int    num_vulkan_ubos = 0;
int    num_vulkan_storage_buffers = 0;
int    num_vulkan_input_attachments = 0;
int    num_vulkan_storage_images = 0;
size_t total_device_vulkan_allocation_size = 0;
size_t total_host_vulkan_allocation_size = 0;

qboolean use_simd;

static SDL_mutex *vertex_allocate_mutex;
static SDL_mutex *index_allocate_mutex;
static SDL_mutex *uniform_allocate_mutex;

/*
================
Staging
================
*/
#define NUM_STAGING_BUFFERS 2

typedef struct
{
	VkBuffer        buffer;
	VkCommandBuffer command_buffer;
	VkFence         fence;
	int             current_offset;
	qboolean        submitted;
	unsigned char  *data;
} stagingbuffer_t;

static VkCommandPool   staging_command_pool;
static vulkan_memory_t staging_memory;
static stagingbuffer_t staging_buffers[NUM_STAGING_BUFFERS];
static int             current_staging_buffer = 0;
static int             num_stagings_in_flight = 0;
static qboolean        staging_submitting = false;
static SDL_mutex      *staging_mutex;
static SDL_cond       *staging_cond;
/*
================
Dynamic vertex/index & uniform buffer
================
*/
#define INITIAL_DYNAMIC_VERTEX_BUFFER_SIZE_KB  256
#define INITIAL_DYNAMIC_INDEX_BUFFER_SIZE_KB   1024
#define INITIAL_DYNAMIC_UNIFORM_BUFFER_SIZE_KB 256
#define NUM_DYNAMIC_BUFFERS                    2
#define GARBAGE_FRAME_COUNT                    3
#define MAX_UNIFORM_ALLOC                      2048

typedef struct
{
	VkBuffer       buffer;
	uint32_t       current_offset;
	unsigned char *data;
} dynbuffer_t;

static uint32_t        current_dyn_vertex_buffer_size = INITIAL_DYNAMIC_VERTEX_BUFFER_SIZE_KB * 1024;
static uint32_t        current_dyn_index_buffer_size = INITIAL_DYNAMIC_INDEX_BUFFER_SIZE_KB * 1024;
static uint32_t        current_dyn_uniform_buffer_size = INITIAL_DYNAMIC_UNIFORM_BUFFER_SIZE_KB * 1024;
static vulkan_memory_t dyn_vertex_buffer_memory;
static vulkan_memory_t dyn_index_buffer_memory;
static vulkan_memory_t dyn_uniform_buffer_memory;
static dynbuffer_t     dyn_vertex_buffers[NUM_DYNAMIC_BUFFERS];
static dynbuffer_t     dyn_index_buffers[NUM_DYNAMIC_BUFFERS];
static dynbuffer_t     dyn_uniform_buffers[NUM_DYNAMIC_BUFFERS];
static int             current_dyn_buffer_index = 0;
static VkDescriptorSet ubo_descriptor_sets[2];

static int              current_garbage_index = 0;
static int              num_device_memory_garbage[GARBAGE_FRAME_COUNT];
static int              num_buffer_garbage[GARBAGE_FRAME_COUNT];
static int              num_desc_set_garbage[GARBAGE_FRAME_COUNT];
static vulkan_memory_t *device_memory_garbage[GARBAGE_FRAME_COUNT];
static VkDescriptorSet *descriptor_set_garbage[GARBAGE_FRAME_COUNT];
static VkBuffer        *buffer_garbage[GARBAGE_FRAME_COUNT];

void R_VulkanMemStats_f (void);

/*
================
GL_MemoryTypeFromProperties
================
*/
int GL_MemoryTypeFromProperties (uint32_t type_bits, VkFlags requirements_mask, VkFlags preferred_mask)
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

	Sys_Error ("Could not find memory type");
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
SetClearColor
====================
*/
static void SetClearColor ()
{
	byte *rgb;
	int   s;

	if (r_fastclear.value != 0.0f)
	{
		// Set to black so fast clear works properly on modern GPUs
		vulkan_globals.color_clear_value.color.float32[0] = 0.0f;
		vulkan_globals.color_clear_value.color.float32[1] = 0.0f;
		vulkan_globals.color_clear_value.color.float32[2] = 0.0f;
		vulkan_globals.color_clear_value.color.float32[3] = 0.0f;
	}
	else
	{
		s = (int)r_clearcolor.value & 0xFF;
		rgb = (byte *)(d_8to24table + s);
		vulkan_globals.color_clear_value.color.float32[0] = rgb[0] / 255.0f;
		vulkan_globals.color_clear_value.color.float32[1] = rgb[1] / 255.0f;
		vulkan_globals.color_clear_value.color.float32[2] = rgb[2] / 255.0f;
		vulkan_globals.color_clear_value.color.float32[3] = 0.0f;
	}
}

/*
====================
R_SetClearColor_f -- johnfitz
====================
*/
static void R_SetClearColor_f (cvar_t *var)
{
	if (r_fastclear.value != 0.0f)
		Con_Warning ("Black clear color forced by r_fastclear\n");

	SetClearColor ();
}

/*
====================
R_SetFastClear_f -- johnfitz
====================
*/
static void R_SetFastClear_f (cvar_t *var)
{
	SetClearColor ();
}

/*
===============
R_Model_ExtraFlags_List_f -- johnfitz -- called when r_nolerp_list cvar changes
===============
*/
static void R_Model_ExtraFlags_List_f (cvar_t *var)
{
	int i;
	for (i = 0; i < MAX_MODELS; i++)
		Mod_SetExtraFlags (cl.model_precache[i]);
}

/*
====================
R_SetWateralpha_f -- ericw
====================
*/
static void R_SetWateralpha_f (cvar_t *var)
{
	if (cls.signon == SIGNONS && cl.worldmodel && !(cl.worldmodel->contentstransparent & SURF_DRAWWATER) && var->value < 1)
		Con_Warning ("Map does not appear to be water-vised\n");
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
	use_simd = SDL_HasSSE () && SDL_HasSSE2 () && (var->value != 0.0f);
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
	if (cls.signon == SIGNONS && cl.worldmodel && !(cl.worldmodel->contentstransparent & SURF_DRAWLAVA) && var->value && var->value < 1)
		Con_Warning ("Map does not appear to be lava-vised\n");
	map_lavaalpha = var->value;
}

/*
====================
R_SetTelealpha_f -- ericw
====================
*/
static void R_SetTelealpha_f (cvar_t *var)
{
	if (cls.signon == SIGNONS && cl.worldmodel && !(cl.worldmodel->contentstransparent & SURF_DRAWTELE) && var->value && var->value < 1)
		Con_Warning ("Map does not appear to be tele-vised\n");
	map_telealpha = var->value;
}

/*
====================
R_SetSlimealpha_f -- ericw
====================
*/
static void R_SetSlimealpha_f (cvar_t *var)
{
	if (cls.signon == SIGNONS && cl.worldmodel && !(cl.worldmodel->contentstransparent & SURF_DRAWSLIME) && var->value && var->value < 1)
		Con_Warning ("Map does not appear to be slime-vised\n");
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
static void R_CreateStagingBuffers ()
{
	int      i;
	VkResult err;

	VkBufferCreateInfo buffer_create_info;
	memset (&buffer_create_info, 0, sizeof (buffer_create_info));
	buffer_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	buffer_create_info.size = vulkan_globals.staging_buffer_size;
	buffer_create_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

	for (i = 0; i < NUM_STAGING_BUFFERS; ++i)
	{
		staging_buffers[i].current_offset = 0;
		staging_buffers[i].submitted = false;

		err = vkCreateBuffer (vulkan_globals.device, &buffer_create_info, NULL, &staging_buffers[i].buffer);
		if (err != VK_SUCCESS)
			Sys_Error ("vkCreateBuffer failed");

		GL_SetObjectName ((uint64_t)staging_buffers[i].buffer, VK_OBJECT_TYPE_BUFFER, "Staging Buffer");
	}

	VkMemoryRequirements memory_requirements;
	vkGetBufferMemoryRequirements (vulkan_globals.device, staging_buffers[0].buffer, &memory_requirements);

	const int align_mod = memory_requirements.size % memory_requirements.alignment;
	const int aligned_size = ((memory_requirements.size % memory_requirements.alignment) == 0)
	                             ? memory_requirements.size
	                             : (memory_requirements.size + memory_requirements.alignment - align_mod);

	VkMemoryAllocateInfo memory_allocate_info;
	memset (&memory_allocate_info, 0, sizeof (memory_allocate_info));
	memory_allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	memory_allocate_info.allocationSize = NUM_STAGING_BUFFERS * aligned_size;
	memory_allocate_info.memoryTypeIndex =
		GL_MemoryTypeFromProperties (memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, VK_MEMORY_PROPERTY_HOST_CACHED_BIT);

	num_vulkan_misc_allocations += 1;
	R_AllocateVulkanMemory (&staging_memory, &memory_allocate_info, VULKAN_MEMORY_TYPE_HOST);
	GL_SetObjectName ((uint64_t)staging_memory.handle, VK_OBJECT_TYPE_DEVICE_MEMORY, "Staging Buffers");

	for (i = 0; i < NUM_STAGING_BUFFERS; ++i)
	{
		err = vkBindBufferMemory (vulkan_globals.device, staging_buffers[i].buffer, staging_memory.handle, i * aligned_size);
		if (err != VK_SUCCESS)
			Sys_Error ("vkBindBufferMemory failed");
	}

	void *data;
	err = vkMapMemory (vulkan_globals.device, staging_memory.handle, 0, NUM_STAGING_BUFFERS * aligned_size, 0, &data);
	if (err != VK_SUCCESS)
		Sys_Error ("vkMapMemory failed");

	for (i = 0; i < NUM_STAGING_BUFFERS; ++i)
		staging_buffers[i].data = (unsigned char *)data + (i * aligned_size);
}

/*
===============
R_DestroyStagingBuffers
===============
*/
static void R_DestroyStagingBuffers ()
{
	int i;

	vkUnmapMemory (vulkan_globals.device, staging_memory.handle);
	R_FreeVulkanMemory (&staging_memory);
	for (i = 0; i < NUM_STAGING_BUFFERS; ++i)
	{
		vkDestroyBuffer (vulkan_globals.device, staging_buffers[i].buffer, NULL);
	}
}

/*
===============
R_InitStagingBuffers
===============
*/
void R_InitStagingBuffers ()
{
	int      i;
	VkResult err;

	Con_Printf ("Initializing staging\n");

	R_CreateStagingBuffers ();

	VkCommandPoolCreateInfo command_pool_create_info;
	memset (&command_pool_create_info, 0, sizeof (command_pool_create_info));
	command_pool_create_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	command_pool_create_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	command_pool_create_info.queueFamilyIndex = vulkan_globals.gfx_queue_family_index;

	err = vkCreateCommandPool (vulkan_globals.device, &command_pool_create_info, NULL, &staging_command_pool);
	if (err != VK_SUCCESS)
		Sys_Error ("vkCreateCommandPool failed");

	VkCommandBufferAllocateInfo command_buffer_allocate_info;
	memset (&command_buffer_allocate_info, 0, sizeof (command_buffer_allocate_info));
	command_buffer_allocate_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	command_buffer_allocate_info.commandPool = staging_command_pool;
	command_buffer_allocate_info.commandBufferCount = NUM_STAGING_BUFFERS;

	VkCommandBuffer command_buffers[NUM_STAGING_BUFFERS];
	err = vkAllocateCommandBuffers (vulkan_globals.device, &command_buffer_allocate_info, command_buffers);
	if (err != VK_SUCCESS)
		Sys_Error ("vkAllocateCommandBuffers failed");

	VkFenceCreateInfo fence_create_info;
	memset (&fence_create_info, 0, sizeof (fence_create_info));
	fence_create_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;

	VkCommandBufferBeginInfo command_buffer_begin_info;
	memset (&command_buffer_begin_info, 0, sizeof (command_buffer_begin_info));
	command_buffer_begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	command_buffer_begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

	for (i = 0; i < NUM_STAGING_BUFFERS; ++i)
	{
		err = vkCreateFence (vulkan_globals.device, &fence_create_info, NULL, &staging_buffers[i].fence);
		if (err != VK_SUCCESS)
			Sys_Error ("vkCreateFence failed");

		staging_buffers[i].command_buffer = command_buffers[i];

		err = vkBeginCommandBuffer (staging_buffers[i].command_buffer, &command_buffer_begin_info);
		if (err != VK_SUCCESS)
			Sys_Error ("vkBeginCommandBuffer failed");
	}

	vertex_allocate_mutex = SDL_CreateMutex ();
	index_allocate_mutex = SDL_CreateMutex ();
	uniform_allocate_mutex = SDL_CreateMutex ();
	staging_mutex = SDL_CreateMutex ();
	staging_cond = SDL_CreateCond ();
}

/*
===============
R_SubmitStagingBuffer
===============
*/
static void R_SubmitStagingBuffer (int index)
{
	staging_submitting = true;
	while (num_stagings_in_flight > 0)
		SDL_CondWait (staging_cond, staging_mutex);
	staging_submitting = false;
	SDL_CondBroadcast (staging_cond);

	VkMemoryBarrier memory_barrier;
	memset (&memory_barrier, 0, sizeof (memory_barrier));
	memory_barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
	memory_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	memory_barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
	vkCmdPipelineBarrier (
		staging_buffers[index].command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &memory_barrier, 0, NULL, 0, NULL);

	vkEndCommandBuffer (staging_buffers[index].command_buffer);

	VkMappedMemoryRange range;
	memset (&range, 0, sizeof (range));
	range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
	range.memory = staging_memory.handle;
	range.size = VK_WHOLE_SIZE;
	vkFlushMappedMemoryRanges (vulkan_globals.device, 1, &range);

	VkSubmitInfo submit_info;
	memset (&submit_info, 0, sizeof (submit_info));
	submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submit_info.commandBufferCount = 1;
	submit_info.pCommandBuffers = &staging_buffers[index].command_buffer;

	vkQueueSubmit (vulkan_globals.queue, 1, &submit_info, staging_buffers[index].fence);

	staging_buffers[index].submitted = true;
	current_staging_buffer = (current_staging_buffer + 1) % NUM_STAGING_BUFFERS;
}

/*
===============
R_SubmitStagingBuffers
===============
*/
void R_SubmitStagingBuffers ()
{
	SDL_LockMutex (staging_mutex);
	if (staging_submitting)
	{
		while (staging_submitting || (num_stagings_in_flight > 0))
			SDL_CondWait (staging_cond, staging_mutex);
	}

	int i;
	for (i = 0; i < NUM_STAGING_BUFFERS; ++i)
	{
		if (!staging_buffers[i].submitted && staging_buffers[i].current_offset > 0)
			R_SubmitStagingBuffer (i);
	}

	SDL_UnlockMutex (staging_mutex);
}

/*
===============
R_FlushStagingCommandBuffer
===============
*/
static void R_FlushStagingCommandBuffer (stagingbuffer_t *staging_buffer)
{
	VkResult err;

	if (!staging_buffer->submitted)
		return;

	err = vkWaitForFences (vulkan_globals.device, 1, &staging_buffer->fence, VK_TRUE, UINT64_MAX);
	if (err != VK_SUCCESS)
		Sys_Error ("vkWaitForFences failed");

	err = vkResetFences (vulkan_globals.device, 1, &staging_buffer->fence);
	if (err != VK_SUCCESS)
		Sys_Error ("vkResetFences failed");

	staging_buffer->current_offset = 0;
	staging_buffer->submitted = false;

	VkCommandBufferBeginInfo command_buffer_begin_info;
	memset (&command_buffer_begin_info, 0, sizeof (command_buffer_begin_info));
	command_buffer_begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	command_buffer_begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

	err = vkBeginCommandBuffer (staging_buffer->command_buffer, &command_buffer_begin_info);
	if (err != VK_SUCCESS)
		Sys_Error ("vkBeginCommandBuffer failed");
}

/*
===============
R_StagingAllocate
===============
*/
byte *R_StagingAllocate (int size, int alignment, VkCommandBuffer *command_buffer, VkBuffer *buffer, int *buffer_offset)
{
	SDL_LockMutex (staging_mutex);
	if (staging_submitting)
	{
		while (staging_submitting || (num_stagings_in_flight > 0))
			SDL_CondWait (staging_cond, staging_mutex);
	}

	vulkan_globals.device_idle = false;

	if (size > vulkan_globals.staging_buffer_size)
	{
		R_SubmitStagingBuffers ();

		for (int i = 0; i < NUM_STAGING_BUFFERS; ++i)
			R_FlushStagingCommandBuffer (&staging_buffers[i]);

		vulkan_globals.staging_buffer_size = size;

		R_DestroyStagingBuffers ();
		R_CreateStagingBuffers ();
	}

	stagingbuffer_t *staging_buffer = &staging_buffers[current_staging_buffer];
	const int        align_mod = staging_buffer->current_offset % alignment;
	staging_buffer->current_offset =
		((staging_buffer->current_offset % alignment) == 0) ? staging_buffer->current_offset : (staging_buffer->current_offset + alignment - align_mod);

	if ((staging_buffer->current_offset + size) >= vulkan_globals.staging_buffer_size && !staging_buffer->submitted)
		R_SubmitStagingBuffer (current_staging_buffer);

	staging_buffer = &staging_buffers[current_staging_buffer];
	R_FlushStagingCommandBuffer (staging_buffer);

	if (command_buffer)
		*command_buffer = staging_buffer->command_buffer;
	if (buffer)
		*buffer = staging_buffer->buffer;
	if (buffer_offset)
		*buffer_offset = staging_buffer->current_offset;

	unsigned char *data = staging_buffer->data + staging_buffer->current_offset;
	staging_buffer->current_offset += size;
	num_stagings_in_flight += 1;

	return data;
}

/*
===============
R_StagingBeginCopy
===============
*/
void R_StagingBeginCopy ()
{
	SDL_UnlockMutex (staging_mutex);
}

/*
===============
R_StagingEndCopy
===============
*/
void R_StagingEndCopy ()
{
	SDL_LockMutex (staging_mutex);
	num_stagings_in_flight -= 1;
	SDL_CondBroadcast (staging_cond);
	SDL_UnlockMutex (staging_mutex);
}

/*
===============
R_InitDynamicVertexBuffers
===============
*/
static void R_InitDynamicVertexBuffers ()
{
	int i;

	Sys_Printf ("Reallocating dynamic VBs (%u KB)\n", current_dyn_vertex_buffer_size / 1024);

	VkResult err;

	VkBufferCreateInfo buffer_create_info;
	memset (&buffer_create_info, 0, sizeof (buffer_create_info));
	buffer_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	buffer_create_info.size = current_dyn_vertex_buffer_size;
	buffer_create_info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;

	for (i = 0; i < NUM_DYNAMIC_BUFFERS; ++i)
	{
		dyn_vertex_buffers[i].current_offset = 0;

		err = vkCreateBuffer (vulkan_globals.device, &buffer_create_info, NULL, &dyn_vertex_buffers[i].buffer);
		if (err != VK_SUCCESS)
			Sys_Error ("vkCreateBuffer failed");

		GL_SetObjectName ((uint64_t)dyn_vertex_buffers[i].buffer, VK_OBJECT_TYPE_BUFFER, "Dynamic Vertex Buffer");
	}

	VkMemoryRequirements memory_requirements;
	vkGetBufferMemoryRequirements (vulkan_globals.device, dyn_vertex_buffers[0].buffer, &memory_requirements);

	const int align_mod = memory_requirements.size % memory_requirements.alignment;
	const int aligned_size = ((memory_requirements.size % memory_requirements.alignment) == 0)
	                             ? memory_requirements.size
	                             : (memory_requirements.size + memory_requirements.alignment - align_mod);

	VkMemoryAllocateInfo memory_allocate_info;
	memset (&memory_allocate_info, 0, sizeof (memory_allocate_info));
	memory_allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	memory_allocate_info.allocationSize = NUM_DYNAMIC_BUFFERS * aligned_size;
	memory_allocate_info.memoryTypeIndex =
		GL_MemoryTypeFromProperties (memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, VK_MEMORY_PROPERTY_HOST_CACHED_BIT);

	num_vulkan_dynbuf_allocations += 1;
	R_AllocateVulkanMemory (&dyn_vertex_buffer_memory, &memory_allocate_info, VULKAN_MEMORY_TYPE_HOST);
	GL_SetObjectName ((uint64_t)dyn_vertex_buffer_memory.handle, VK_OBJECT_TYPE_DEVICE_MEMORY, "Dynamic Vertex Buffers");

	for (i = 0; i < NUM_DYNAMIC_BUFFERS; ++i)
	{
		err = vkBindBufferMemory (vulkan_globals.device, dyn_vertex_buffers[i].buffer, dyn_vertex_buffer_memory.handle, i * aligned_size);
		if (err != VK_SUCCESS)
			Sys_Error ("vkBindBufferMemory failed");
	}

	void *data;
	err = vkMapMemory (vulkan_globals.device, dyn_vertex_buffer_memory.handle, 0, NUM_DYNAMIC_BUFFERS * aligned_size, 0, &data);
	if (err != VK_SUCCESS)
		Sys_Error ("vkMapMemory failed");

	for (i = 0; i < NUM_DYNAMIC_BUFFERS; ++i)
		dyn_vertex_buffers[i].data = (unsigned char *)data + (i * aligned_size);
}

/*
===============
R_InitDynamicIndexBuffers
===============
*/
static void R_InitDynamicIndexBuffers ()
{
	int i;

	Sys_Printf ("Reallocating dynamic IBs (%u KB)\n", current_dyn_index_buffer_size / 1024);

	VkResult err;

	VkBufferCreateInfo buffer_create_info;
	memset (&buffer_create_info, 0, sizeof (buffer_create_info));
	buffer_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	buffer_create_info.size = current_dyn_index_buffer_size;
	buffer_create_info.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;

	for (i = 0; i < NUM_DYNAMIC_BUFFERS; ++i)
	{
		dyn_index_buffers[i].current_offset = 0;

		err = vkCreateBuffer (vulkan_globals.device, &buffer_create_info, NULL, &dyn_index_buffers[i].buffer);
		if (err != VK_SUCCESS)
			Sys_Error ("vkCreateBuffer failed");

		GL_SetObjectName ((uint64_t)dyn_index_buffers[i].buffer, VK_OBJECT_TYPE_BUFFER, "Dynamic Index Buffer");
	}

	VkMemoryRequirements memory_requirements;
	vkGetBufferMemoryRequirements (vulkan_globals.device, dyn_index_buffers[0].buffer, &memory_requirements);

	const int align_mod = memory_requirements.size % memory_requirements.alignment;
	const int aligned_size = ((memory_requirements.size % memory_requirements.alignment) == 0)
	                             ? memory_requirements.size
	                             : (memory_requirements.size + memory_requirements.alignment - align_mod);

	VkMemoryAllocateInfo memory_allocate_info;
	memset (&memory_allocate_info, 0, sizeof (memory_allocate_info));
	memory_allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	memory_allocate_info.allocationSize = NUM_DYNAMIC_BUFFERS * aligned_size;
	memory_allocate_info.memoryTypeIndex =
		GL_MemoryTypeFromProperties (memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, VK_MEMORY_PROPERTY_HOST_CACHED_BIT);

	num_vulkan_dynbuf_allocations += 1;
	R_AllocateVulkanMemory (&dyn_index_buffer_memory, &memory_allocate_info, VULKAN_MEMORY_TYPE_HOST);
	GL_SetObjectName ((uint64_t)dyn_index_buffer_memory.handle, VK_OBJECT_TYPE_DEVICE_MEMORY, "Dynamic Index Buffers");

	for (i = 0; i < NUM_DYNAMIC_BUFFERS; ++i)
	{
		err = vkBindBufferMemory (vulkan_globals.device, dyn_index_buffers[i].buffer, dyn_index_buffer_memory.handle, i * aligned_size);
		if (err != VK_SUCCESS)
			Sys_Error ("vkBindBufferMemory failed");
	}

	void *data;
	err = vkMapMemory (vulkan_globals.device, dyn_index_buffer_memory.handle, 0, NUM_DYNAMIC_BUFFERS * aligned_size, 0, &data);
	if (err != VK_SUCCESS)
		Sys_Error ("vkMapMemory failed");

	for (i = 0; i < NUM_DYNAMIC_BUFFERS; ++i)
		dyn_index_buffers[i].data = (unsigned char *)data + (i * aligned_size);
}

/*
===============
R_InitDynamicUniformBuffers
===============
*/
static void R_InitDynamicUniformBuffers ()
{
	int i;

	Sys_Printf ("Reallocating dynamic UBs (%u KB)\n", current_dyn_uniform_buffer_size / 1024);

	VkResult err;

	VkBufferCreateInfo buffer_create_info;
	memset (&buffer_create_info, 0, sizeof (buffer_create_info));
	buffer_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	buffer_create_info.size = current_dyn_uniform_buffer_size;
	buffer_create_info.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;

	for (i = 0; i < NUM_DYNAMIC_BUFFERS; ++i)
	{
		dyn_uniform_buffers[i].current_offset = 0;

		err = vkCreateBuffer (vulkan_globals.device, &buffer_create_info, NULL, &dyn_uniform_buffers[i].buffer);
		if (err != VK_SUCCESS)
			Sys_Error ("vkCreateBuffer failed");

		GL_SetObjectName ((uint64_t)dyn_uniform_buffers[i].buffer, VK_OBJECT_TYPE_BUFFER, "Dynamic Uniform Buffer");
	}

	VkMemoryRequirements memory_requirements;
	vkGetBufferMemoryRequirements (vulkan_globals.device, dyn_uniform_buffers[0].buffer, &memory_requirements);

	const int align_mod = memory_requirements.size % memory_requirements.alignment;
	const int aligned_size = ((memory_requirements.size % memory_requirements.alignment) == 0)
	                             ? memory_requirements.size
	                             : (memory_requirements.size + memory_requirements.alignment - align_mod);

	VkMemoryAllocateInfo memory_allocate_info;
	memset (&memory_allocate_info, 0, sizeof (memory_allocate_info));
	memory_allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	memory_allocate_info.allocationSize = NUM_DYNAMIC_BUFFERS * aligned_size;
	memory_allocate_info.memoryTypeIndex =
		GL_MemoryTypeFromProperties (memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, VK_MEMORY_PROPERTY_HOST_CACHED_BIT);

	num_vulkan_dynbuf_allocations += 1;
	R_AllocateVulkanMemory (&dyn_uniform_buffer_memory, &memory_allocate_info, VULKAN_MEMORY_TYPE_HOST);
	GL_SetObjectName ((uint64_t)dyn_uniform_buffer_memory.handle, VK_OBJECT_TYPE_DEVICE_MEMORY, "Dynamic Uniform Buffers");

	for (i = 0; i < NUM_DYNAMIC_BUFFERS; ++i)
	{
		err = vkBindBufferMemory (vulkan_globals.device, dyn_uniform_buffers[i].buffer, dyn_uniform_buffer_memory.handle, i * aligned_size);
		if (err != VK_SUCCESS)
			Sys_Error ("vkBindBufferMemory failed");
	}

	void *data;
	err = vkMapMemory (vulkan_globals.device, dyn_uniform_buffer_memory.handle, 0, NUM_DYNAMIC_BUFFERS * aligned_size, 0, &data);
	if (err != VK_SUCCESS)
		Sys_Error ("vkMapMemory failed");

	for (i = 0; i < NUM_DYNAMIC_BUFFERS; ++i)
		dyn_uniform_buffers[i].data = (unsigned char *)data + (i * aligned_size);

	VkDescriptorSetAllocateInfo descriptor_set_allocate_info;
	memset (&descriptor_set_allocate_info, 0, sizeof (descriptor_set_allocate_info));
	descriptor_set_allocate_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	descriptor_set_allocate_info.descriptorPool = vulkan_globals.descriptor_pool;
	descriptor_set_allocate_info.descriptorSetCount = 1;
	descriptor_set_allocate_info.pSetLayouts = &vulkan_globals.ubo_set_layout.handle;

	ubo_descriptor_sets[0] = R_AllocateDescriptorSet (&vulkan_globals.ubo_set_layout);
	ubo_descriptor_sets[1] = R_AllocateDescriptorSet (&vulkan_globals.ubo_set_layout);

	VkDescriptorBufferInfo buffer_info;
	memset (&buffer_info, 0, sizeof (buffer_info));
	buffer_info.offset = 0;
	buffer_info.range = MAX_UNIFORM_ALLOC;

	VkWriteDescriptorSet ubo_write;
	memset (&ubo_write, 0, sizeof (ubo_write));
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
		vkUpdateDescriptorSets (vulkan_globals.device, 1, &ubo_write, 0, NULL);
	}
}

/*
===============
R_InitFanIndexBuffer
===============
*/
static void R_InitFanIndexBuffer ()
{
	VkResult       err;
	VkDeviceMemory memory;
	const int      bufferSize = sizeof (uint16_t) * FAN_INDEX_BUFFER_SIZE;

	VkBufferCreateInfo buffer_create_info;
	memset (&buffer_create_info, 0, sizeof (buffer_create_info));
	buffer_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	buffer_create_info.size = bufferSize;
	buffer_create_info.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

	err = vkCreateBuffer (vulkan_globals.device, &buffer_create_info, NULL, &vulkan_globals.fan_index_buffer);
	if (err != VK_SUCCESS)
		Sys_Error ("vkCreateBuffer failed");

	GL_SetObjectName ((uint64_t)vulkan_globals.fan_index_buffer, VK_OBJECT_TYPE_BUFFER, "Quad Index Buffer");

	VkMemoryRequirements memory_requirements;
	vkGetBufferMemoryRequirements (vulkan_globals.device, vulkan_globals.fan_index_buffer, &memory_requirements);

	VkMemoryAllocateInfo memory_allocate_info;
	memset (&memory_allocate_info, 0, sizeof (memory_allocate_info));
	memory_allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	memory_allocate_info.allocationSize = memory_requirements.size;
	memory_allocate_info.memoryTypeIndex = GL_MemoryTypeFromProperties (memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0);

	num_vulkan_dynbuf_allocations += 1;
	total_device_vulkan_allocation_size += memory_requirements.size;
	err = vkAllocateMemory (vulkan_globals.device, &memory_allocate_info, NULL, &memory);
	if (err != VK_SUCCESS)
		Sys_Error ("vkAllocateMemory failed");

	err = vkBindBufferMemory (vulkan_globals.device, vulkan_globals.fan_index_buffer, memory, 0);
	if (err != VK_SUCCESS)
		Sys_Error ("vkBindBufferMemory failed");

	{
		VkBuffer        staging_buffer;
		VkCommandBuffer command_buffer;
		int             staging_offset;
		int             current_index = 0;
		int             i;
		uint16_t       *staging_mem = (uint16_t *)R_StagingAllocate (bufferSize, 1, &command_buffer, &staging_buffer, &staging_offset);

		VkBufferCopy region;
		region.srcOffset = staging_offset;
		region.dstOffset = 0;
		region.size = bufferSize;
		vkCmdCopyBuffer (command_buffer, staging_buffer, vulkan_globals.fan_index_buffer, 1, &region);

		R_StagingBeginCopy ();
		for (i = 0; i < FAN_INDEX_BUFFER_SIZE / 3; ++i)
		{
			staging_mem[current_index++] = 0;
			staging_mem[current_index++] = 1 + i;
			staging_mem[current_index++] = 2 + i;
		}
		R_StagingEndCopy ();
	}
}

/*
===============
R_SwapDynamicBuffers
===============
*/
void R_SwapDynamicBuffers ()
{
	SDL_LockMutex (vertex_allocate_mutex);
	SDL_LockMutex (index_allocate_mutex);
	SDL_LockMutex (uniform_allocate_mutex);

	current_dyn_buffer_index = (current_dyn_buffer_index + 1) % NUM_DYNAMIC_BUFFERS;
	dyn_vertex_buffers[current_dyn_buffer_index].current_offset = 0;
	dyn_index_buffers[current_dyn_buffer_index].current_offset = 0;
	dyn_uniform_buffers[current_dyn_buffer_index].current_offset = 0;

	SDL_UnlockMutex (uniform_allocate_mutex);
	SDL_UnlockMutex (index_allocate_mutex);
	SDL_UnlockMutex (vertex_allocate_mutex);
}

/*
===============
R_FlushDynamicBuffers
===============
*/
void R_FlushDynamicBuffers ()
{
	VkMappedMemoryRange ranges[3];
	memset (&ranges, 0, sizeof (ranges));
	ranges[0].sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
	ranges[0].memory = dyn_vertex_buffer_memory.handle;
	ranges[0].size = VK_WHOLE_SIZE;
	ranges[1].sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
	ranges[1].memory = dyn_index_buffer_memory.handle;
	ranges[1].size = VK_WHOLE_SIZE;
	ranges[2].sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
	ranges[2].memory = dyn_uniform_buffer_memory.handle;
	ranges[2].size = VK_WHOLE_SIZE;
	vkFlushMappedMemoryRanges (vulkan_globals.device, 3, ranges);
}

/*
===============
R_AddDynamicBufferGarbage
===============
*/
static void R_AddDynamicBufferGarbage (vulkan_memory_t device_memory, dynbuffer_t *buffers, VkDescriptorSet *descriptor_sets)
{
	{
		int *num_garbage = &num_device_memory_garbage[current_garbage_index];
		int  old_num_memory_garbage = *num_garbage;
		*num_garbage += 1;
		if (device_memory_garbage[current_garbage_index] == NULL)
			device_memory_garbage[current_garbage_index] = Mem_Alloc (sizeof (vulkan_memory_t) * (*num_garbage));
		else
			device_memory_garbage[current_garbage_index] =
				Mem_Realloc (device_memory_garbage[current_garbage_index], sizeof (vulkan_memory_t) * (*num_garbage));
		device_memory_garbage[current_garbage_index][old_num_memory_garbage] = device_memory;
	}

	{
		int *num_garbage = &num_buffer_garbage[current_garbage_index];
		int  old_num_buffer_garbage = *num_garbage;
		*num_garbage += NUM_DYNAMIC_BUFFERS;
		if (buffer_garbage[current_garbage_index] == NULL)
			buffer_garbage[current_garbage_index] = Mem_Alloc (sizeof (VkBuffer) * (*num_garbage));
		else
			buffer_garbage[current_garbage_index] = Mem_Realloc (buffer_garbage[current_garbage_index], sizeof (VkBuffer) * (*num_garbage));
		for (int i = 0; i < NUM_DYNAMIC_BUFFERS; ++i)
			buffer_garbage[current_garbage_index][old_num_buffer_garbage + i] = buffers[i].buffer;
	}

	if (descriptor_sets)
	{
		int *num_garbage = &num_desc_set_garbage[current_garbage_index];
		int  old_num_desc_set_garbage = *num_garbage;
		*num_garbage += 2;
		if (descriptor_set_garbage[current_garbage_index] == NULL)
			descriptor_set_garbage[current_garbage_index] = Mem_Alloc (sizeof (VkDescriptorSet) * (*num_garbage));
		else
			descriptor_set_garbage[current_garbage_index] =
				Mem_Realloc (descriptor_set_garbage[current_garbage_index], sizeof (VkDescriptorSet) * (*num_garbage));
		for (int i = 0; i < 2; ++i)
			descriptor_set_garbage[current_garbage_index][old_num_desc_set_garbage + i] = descriptor_sets[i];
	}
}

/*
===============
R_CollectDynamicBufferGarbage
===============
*/
void R_CollectDynamicBufferGarbage ()
{
	current_garbage_index = (current_garbage_index + 1) % GARBAGE_FRAME_COUNT;
	const int collect_garbage_index = (current_garbage_index + 1) % GARBAGE_FRAME_COUNT;

	if (num_desc_set_garbage[collect_garbage_index] > 0)
	{
		for (int i = 0; i < num_desc_set_garbage[collect_garbage_index]; ++i)
			R_FreeDescriptorSet (descriptor_set_garbage[collect_garbage_index][i], &vulkan_globals.ubo_set_layout);
		Mem_Free (descriptor_set_garbage[collect_garbage_index]);
		descriptor_set_garbage[collect_garbage_index] = NULL;
		num_desc_set_garbage[collect_garbage_index] = 0;
	}

	if (num_buffer_garbage[collect_garbage_index] > 0)
	{
		for (int i = 0; i < num_buffer_garbage[collect_garbage_index]; ++i)
			vkDestroyBuffer (vulkan_globals.device, buffer_garbage[collect_garbage_index][i], NULL);
		Mem_Free (buffer_garbage[collect_garbage_index]);
		buffer_garbage[collect_garbage_index] = NULL;
		num_buffer_garbage[collect_garbage_index] = 0;
	}

	if (num_device_memory_garbage[collect_garbage_index] > 0)
	{
		for (int i = 0; i < num_device_memory_garbage[collect_garbage_index]; ++i)
			R_FreeVulkanMemory (&device_memory_garbage[collect_garbage_index][i]);
		Mem_Free (device_memory_garbage[collect_garbage_index]);
		device_memory_garbage[collect_garbage_index] = NULL;
		num_device_memory_garbage[collect_garbage_index] = 0;
	}
}

/*
===============
R_VertexAllocate
===============
*/
byte *R_VertexAllocate (int size, VkBuffer *buffer, VkDeviceSize *buffer_offset)
{
	SDL_LockMutex (vertex_allocate_mutex);
	dynbuffer_t *dyn_vb = &dyn_vertex_buffers[current_dyn_buffer_index];

	if ((dyn_vb->current_offset + size) > current_dyn_vertex_buffer_size)
	{
		R_AddDynamicBufferGarbage (dyn_vertex_buffer_memory, dyn_vertex_buffers, NULL);
		current_dyn_vertex_buffer_size = q_max (current_dyn_vertex_buffer_size * 2, (uint32_t)Q_nextPow2 (size));
		vkUnmapMemory (vulkan_globals.device, dyn_vertex_buffer_memory.handle);
		R_InitDynamicVertexBuffers ();
	}

	*buffer = dyn_vb->buffer;
	*buffer_offset = dyn_vb->current_offset;

	unsigned char *data = dyn_vb->data + dyn_vb->current_offset;
	dyn_vb->current_offset += size;
	SDL_UnlockMutex (vertex_allocate_mutex);

	return data;
}

/*
===============
R_IndexAllocate
===============
*/
byte *R_IndexAllocate (int size, VkBuffer *buffer, VkDeviceSize *buffer_offset)
{
	SDL_LockMutex (index_allocate_mutex);
	// Align to 4 bytes because we allocate both uint16 and uint32
	// index buffers and alignment must match index size
	const int align_mod = size % 4;
	const int aligned_size = ((size % 4) == 0) ? size : (size + 4 - align_mod);

	dynbuffer_t *dyn_ib = &dyn_index_buffers[current_dyn_buffer_index];

	if ((dyn_ib->current_offset + aligned_size) > current_dyn_index_buffer_size)
	{
		R_AddDynamicBufferGarbage (dyn_index_buffer_memory, dyn_index_buffers, NULL);
		current_dyn_index_buffer_size = q_max (current_dyn_index_buffer_size * 2, (uint32_t)Q_nextPow2 (size));
		vkUnmapMemory (vulkan_globals.device, dyn_index_buffer_memory.handle);
		R_InitDynamicIndexBuffers ();
	}

	*buffer = dyn_ib->buffer;
	*buffer_offset = dyn_ib->current_offset;

	unsigned char *data = dyn_ib->data + dyn_ib->current_offset;
	dyn_ib->current_offset += aligned_size;
	SDL_UnlockMutex (index_allocate_mutex);

	return data;
}

/*
===============
R_UniformAllocate

UBO offsets need to be 256 byte aligned on NVIDIA hardware
This is also the maximum required alignment by the Vulkan spec
===============
*/
byte *R_UniformAllocate (int size, VkBuffer *buffer, uint32_t *buffer_offset, VkDescriptorSet *descriptor_set)
{
	SDL_LockMutex (uniform_allocate_mutex);
	if (size > MAX_UNIFORM_ALLOC)
		Sys_Error ("Increase MAX_UNIFORM_ALLOC");

	const int align_mod = size % 256;
	const int aligned_size = ((size % 256) == 0) ? size : (size + 256 - align_mod);

	dynbuffer_t *dyn_ub = &dyn_uniform_buffers[current_dyn_buffer_index];

	if ((dyn_ub->current_offset + MAX_UNIFORM_ALLOC) > current_dyn_uniform_buffer_size)
	{
		R_AddDynamicBufferGarbage (dyn_uniform_buffer_memory, dyn_uniform_buffers, ubo_descriptor_sets);
		current_dyn_uniform_buffer_size = q_max (current_dyn_uniform_buffer_size * 2, (uint32_t)Q_nextPow2 (size));
		vkUnmapMemory (vulkan_globals.device, dyn_uniform_buffer_memory.handle);
		R_InitDynamicUniformBuffers ();
	}

	*buffer = dyn_ub->buffer;
	*buffer_offset = dyn_ub->current_offset;

	unsigned char *data = dyn_ub->data + dyn_ub->current_offset;
	dyn_ub->current_offset += aligned_size;

	*descriptor_set = ubo_descriptor_sets[current_dyn_buffer_index];
	SDL_UnlockMutex (uniform_allocate_mutex);
	return data;
}

/*
===============
R_InitGPUBuffers
===============
*/
void R_InitGPUBuffers ()
{
	R_InitDynamicVertexBuffers ();
	R_InitDynamicIndexBuffers ();
	R_InitDynamicUniformBuffers ();
	R_InitFanIndexBuffer ();
}

/*
===============
R_CreateDescriptorSetLayouts
===============
*/
void R_CreateDescriptorSetLayouts ()
{
	Sys_Printf ("Creating descriptor set layouts\n");

	VkResult                        err;
	VkDescriptorSetLayoutCreateInfo descriptor_set_layout_create_info;
	memset (&descriptor_set_layout_create_info, 0, sizeof (descriptor_set_layout_create_info));
	descriptor_set_layout_create_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;

	{
		VkDescriptorSetLayoutBinding single_texture_layout_binding;
		memset (&single_texture_layout_binding, 0, sizeof (single_texture_layout_binding));
		single_texture_layout_binding.binding = 0;
		single_texture_layout_binding.descriptorCount = 1;
		single_texture_layout_binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		single_texture_layout_binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT;

		descriptor_set_layout_create_info.bindingCount = 1;
		descriptor_set_layout_create_info.pBindings = &single_texture_layout_binding;

		memset (&vulkan_globals.single_texture_set_layout, 0, sizeof (vulkan_globals.single_texture_set_layout));
		vulkan_globals.single_texture_set_layout.num_combined_image_samplers = 1;

		err = vkCreateDescriptorSetLayout (vulkan_globals.device, &descriptor_set_layout_create_info, NULL, &vulkan_globals.single_texture_set_layout.handle);
		if (err != VK_SUCCESS)
			Sys_Error ("vkCreateDescriptorSetLayout failed");
	}

	{
		VkDescriptorSetLayoutBinding ubo_sampler_layout_bindings;
		memset (&ubo_sampler_layout_bindings, 0, sizeof (ubo_sampler_layout_bindings));
		ubo_sampler_layout_bindings.binding = 0;
		ubo_sampler_layout_bindings.descriptorCount = 1;
		ubo_sampler_layout_bindings.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
		ubo_sampler_layout_bindings.stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS;

		descriptor_set_layout_create_info.bindingCount = 1;
		descriptor_set_layout_create_info.pBindings = &ubo_sampler_layout_bindings;

		memset (&vulkan_globals.ubo_set_layout, 0, sizeof (vulkan_globals.ubo_set_layout));
		vulkan_globals.ubo_set_layout.num_ubos_dynamic = 1;

		err = vkCreateDescriptorSetLayout (vulkan_globals.device, &descriptor_set_layout_create_info, NULL, &vulkan_globals.ubo_set_layout.handle);
		if (err != VK_SUCCESS)
			Sys_Error ("vkCreateDescriptorSetLayout failed");
	}

	{
		VkDescriptorSetLayoutBinding input_attachment_layout_bindings;
		memset (&input_attachment_layout_bindings, 0, sizeof (input_attachment_layout_bindings));
		input_attachment_layout_bindings.binding = 0;
		input_attachment_layout_bindings.descriptorCount = 1;
		input_attachment_layout_bindings.descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
		input_attachment_layout_bindings.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

		descriptor_set_layout_create_info.bindingCount = 1;
		descriptor_set_layout_create_info.pBindings = &input_attachment_layout_bindings;

		memset (&vulkan_globals.input_attachment_set_layout, 0, sizeof (vulkan_globals.input_attachment_set_layout));
		vulkan_globals.input_attachment_set_layout.num_input_attachments = 1;

		err = vkCreateDescriptorSetLayout (vulkan_globals.device, &descriptor_set_layout_create_info, NULL, &vulkan_globals.input_attachment_set_layout.handle);
		if (err != VK_SUCCESS)
			Sys_Error ("vkCreateDescriptorSetLayout failed");
	}

	{
		VkDescriptorSetLayoutBinding screen_effects_layout_bindings[4];
		memset (&screen_effects_layout_bindings, 0, sizeof (screen_effects_layout_bindings));
		screen_effects_layout_bindings[0].binding = 0;
		screen_effects_layout_bindings[0].descriptorCount = 1;
		screen_effects_layout_bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		screen_effects_layout_bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		screen_effects_layout_bindings[1].binding = 1;
		screen_effects_layout_bindings[1].descriptorCount = 1;
		screen_effects_layout_bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		screen_effects_layout_bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		screen_effects_layout_bindings[2].binding = 2;
		screen_effects_layout_bindings[2].descriptorCount = 1;
		screen_effects_layout_bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
		screen_effects_layout_bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		screen_effects_layout_bindings[3].binding = 3;
		screen_effects_layout_bindings[3].descriptorCount = 1;
		screen_effects_layout_bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		screen_effects_layout_bindings[3].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

		descriptor_set_layout_create_info.bindingCount = 4;
		descriptor_set_layout_create_info.pBindings = screen_effects_layout_bindings;

		memset (&vulkan_globals.screen_effects_set_layout, 0, sizeof (vulkan_globals.screen_effects_set_layout));
		vulkan_globals.screen_effects_set_layout.num_combined_image_samplers = 1;
		vulkan_globals.screen_effects_set_layout.num_storage_images = 1;

		err = vkCreateDescriptorSetLayout (vulkan_globals.device, &descriptor_set_layout_create_info, NULL, &vulkan_globals.screen_effects_set_layout.handle);
		if (err != VK_SUCCESS)
			Sys_Error ("vkCreateDescriptorSetLayout failed");
	}

	{
		VkDescriptorSetLayoutBinding single_texture_cs_write_layout_binding;
		memset (&single_texture_cs_write_layout_binding, 0, sizeof (single_texture_cs_write_layout_binding));
		single_texture_cs_write_layout_binding.binding = 0;
		single_texture_cs_write_layout_binding.descriptorCount = 1;
		single_texture_cs_write_layout_binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		single_texture_cs_write_layout_binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

		descriptor_set_layout_create_info.bindingCount = 1;
		descriptor_set_layout_create_info.pBindings = &single_texture_cs_write_layout_binding;

		memset (&vulkan_globals.single_texture_cs_write_set_layout, 0, sizeof (vulkan_globals.single_texture_cs_write_set_layout));
		vulkan_globals.single_texture_cs_write_set_layout.num_storage_images = 1;

		err = vkCreateDescriptorSetLayout (
			vulkan_globals.device, &descriptor_set_layout_create_info, NULL, &vulkan_globals.single_texture_cs_write_set_layout.handle);
		if (err != VK_SUCCESS)
			Sys_Error ("vkCreateDescriptorSetLayout failed");
	}

	{
		VkDescriptorSetLayoutBinding lightmap_compute_layout_bindings[7];
		memset (&lightmap_compute_layout_bindings, 0, sizeof (lightmap_compute_layout_bindings));
		lightmap_compute_layout_bindings[0].binding = 0;
		lightmap_compute_layout_bindings[0].descriptorCount = 1;
		lightmap_compute_layout_bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		lightmap_compute_layout_bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		lightmap_compute_layout_bindings[1].binding = 1;
		lightmap_compute_layout_bindings[1].descriptorCount = 1;
		lightmap_compute_layout_bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		lightmap_compute_layout_bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		lightmap_compute_layout_bindings[2].binding = 2;
		lightmap_compute_layout_bindings[2].descriptorCount = MAXLIGHTMAPS;
		lightmap_compute_layout_bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		lightmap_compute_layout_bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		lightmap_compute_layout_bindings[3].binding = 3;
		lightmap_compute_layout_bindings[3].descriptorCount = 1;
		lightmap_compute_layout_bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		lightmap_compute_layout_bindings[3].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		lightmap_compute_layout_bindings[4].binding = 4;
		lightmap_compute_layout_bindings[4].descriptorCount = 1;
		lightmap_compute_layout_bindings[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		lightmap_compute_layout_bindings[4].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		lightmap_compute_layout_bindings[5].binding = 5;
		lightmap_compute_layout_bindings[5].descriptorCount = 1;
		lightmap_compute_layout_bindings[5].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
		lightmap_compute_layout_bindings[5].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		lightmap_compute_layout_bindings[6].binding = 6;
		lightmap_compute_layout_bindings[6].descriptorCount = 1;
		lightmap_compute_layout_bindings[6].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
		lightmap_compute_layout_bindings[6].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

		descriptor_set_layout_create_info.bindingCount = 7;
		descriptor_set_layout_create_info.pBindings = lightmap_compute_layout_bindings;

		memset (&vulkan_globals.lightmap_compute_set_layout, 0, sizeof (vulkan_globals.lightmap_compute_set_layout));
		vulkan_globals.lightmap_compute_set_layout.num_storage_images = 1;
		vulkan_globals.lightmap_compute_set_layout.num_combined_image_samplers = 1 + MAXLIGHTMAPS;
		vulkan_globals.lightmap_compute_set_layout.num_storage_buffers = 2;
		vulkan_globals.lightmap_compute_set_layout.num_ubos_dynamic = 2;

		err = vkCreateDescriptorSetLayout (vulkan_globals.device, &descriptor_set_layout_create_info, NULL, &vulkan_globals.lightmap_compute_set_layout.handle);
		if (err != VK_SUCCESS)
			Sys_Error ("vkCreateDescriptorSetLayout failed");
	}
}

/*
===============
R_CreateDescriptorPool
===============
*/
void R_CreateDescriptorPool ()
{
	VkDescriptorPoolSize pool_sizes[6];
	pool_sizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	pool_sizes[0].descriptorCount = (MAX_SANITY_LIGHTMAPS * 2) + (MAX_GLTEXTURES + 1);
	pool_sizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
	pool_sizes[1].descriptorCount = 16 + (MAX_SANITY_LIGHTMAPS * 2);
	pool_sizes[2].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	pool_sizes[2].descriptorCount = MAX_SANITY_LIGHTMAPS * 2;
	pool_sizes[3].type = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
	pool_sizes[3].descriptorCount = 2;
	pool_sizes[4].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	pool_sizes[4].descriptorCount = MAX_GLTEXTURES + MAX_SANITY_LIGHTMAPS;

	VkDescriptorPoolCreateInfo descriptor_pool_create_info;
	memset (&descriptor_pool_create_info, 0, sizeof (descriptor_pool_create_info));
	descriptor_pool_create_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	descriptor_pool_create_info.maxSets = MAX_GLTEXTURES + MAX_SANITY_LIGHTMAPS + 32;
	descriptor_pool_create_info.poolSizeCount = 5;
	descriptor_pool_create_info.pPoolSizes = pool_sizes;
	descriptor_pool_create_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;

	vkCreateDescriptorPool (vulkan_globals.device, &descriptor_pool_create_info, NULL, &vulkan_globals.descriptor_pool);
}

/*
===============
R_CreatePipelineLayouts
===============
*/
void R_CreatePipelineLayouts ()
{
	Sys_Printf ("Creating pipeline layouts\n");

	VkResult err;

	// Basic
	VkDescriptorSetLayout basic_descriptor_set_layouts[1] = {vulkan_globals.single_texture_set_layout.handle};

	VkPushConstantRange push_constant_range;
	memset (&push_constant_range, 0, sizeof (push_constant_range));
	push_constant_range.offset = 0;
	push_constant_range.size = 21 * sizeof (float);
	push_constant_range.stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS;

	VkPipelineLayoutCreateInfo pipeline_layout_create_info;
	memset (&pipeline_layout_create_info, 0, sizeof (pipeline_layout_create_info));
	pipeline_layout_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipeline_layout_create_info.setLayoutCount = 1;
	pipeline_layout_create_info.pSetLayouts = basic_descriptor_set_layouts;
	pipeline_layout_create_info.pushConstantRangeCount = 1;
	pipeline_layout_create_info.pPushConstantRanges = &push_constant_range;

	err = vkCreatePipelineLayout (vulkan_globals.device, &pipeline_layout_create_info, NULL, &vulkan_globals.basic_pipeline_layout.handle);
	if (err != VK_SUCCESS)
		Sys_Error ("vkCreatePipelineLayout failed");
	GL_SetObjectName ((uint64_t)vulkan_globals.basic_pipeline_layout.handle, VK_OBJECT_TYPE_PIPELINE_LAYOUT, "basic_pipeline_layout");
	vulkan_globals.basic_pipeline_layout.push_constant_range = push_constant_range;

	// World
	VkDescriptorSetLayout world_descriptor_set_layouts[3] = {
		vulkan_globals.single_texture_set_layout.handle, vulkan_globals.single_texture_set_layout.handle, vulkan_globals.single_texture_set_layout.handle};

	pipeline_layout_create_info.setLayoutCount = 3;
	pipeline_layout_create_info.pSetLayouts = world_descriptor_set_layouts;

	err = vkCreatePipelineLayout (vulkan_globals.device, &pipeline_layout_create_info, NULL, &vulkan_globals.world_pipeline_layout.handle);
	if (err != VK_SUCCESS)
		Sys_Error ("vkCreatePipelineLayout failed");
	GL_SetObjectName ((uint64_t)vulkan_globals.world_pipeline_layout.handle, VK_OBJECT_TYPE_PIPELINE_LAYOUT, "world_pipeline_layout");
	vulkan_globals.world_pipeline_layout.push_constant_range = push_constant_range;

	// Alias
	VkDescriptorSetLayout alias_descriptor_set_layouts[3] = {
		vulkan_globals.single_texture_set_layout.handle, vulkan_globals.single_texture_set_layout.handle, vulkan_globals.ubo_set_layout.handle};

	pipeline_layout_create_info.setLayoutCount = 3;
	pipeline_layout_create_info.pSetLayouts = alias_descriptor_set_layouts;

	err = vkCreatePipelineLayout (vulkan_globals.device, &pipeline_layout_create_info, NULL, &vulkan_globals.alias_pipeline.layout.handle);
	if (err != VK_SUCCESS)
		Sys_Error ("vkCreatePipelineLayout failed");
	GL_SetObjectName ((uint64_t)vulkan_globals.alias_pipeline.layout.handle, VK_OBJECT_TYPE_PIPELINE_LAYOUT, "alias_pipeline_layout");
	vulkan_globals.alias_pipeline.layout.push_constant_range = push_constant_range;

	// Sky
	VkDescriptorSetLayout sky_layer_descriptor_set_layouts[2] = {
		vulkan_globals.single_texture_set_layout.handle,
		vulkan_globals.single_texture_set_layout.handle,
	};

	pipeline_layout_create_info.setLayoutCount = 2;
	pipeline_layout_create_info.pSetLayouts = sky_layer_descriptor_set_layouts;

	err = vkCreatePipelineLayout (vulkan_globals.device, &pipeline_layout_create_info, NULL, &vulkan_globals.sky_layer_pipeline.layout.handle);
	if (err != VK_SUCCESS)
		Sys_Error ("vkCreatePipelineLayout failed");
	GL_SetObjectName ((uint64_t)vulkan_globals.sky_layer_pipeline.layout.handle, VK_OBJECT_TYPE_PIPELINE_LAYOUT, "sky_layer_pipeline_layout");
	vulkan_globals.sky_layer_pipeline.layout.push_constant_range = push_constant_range;

	// Postprocess
	VkDescriptorSetLayout postprocess_descriptor_set_layouts[1] = {
		vulkan_globals.input_attachment_set_layout.handle,
	};

	memset (&push_constant_range, 0, sizeof (push_constant_range));
	push_constant_range.offset = 0;
	push_constant_range.size = 2 * sizeof (float);
	push_constant_range.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

	pipeline_layout_create_info.setLayoutCount = 1;
	pipeline_layout_create_info.pSetLayouts = postprocess_descriptor_set_layouts;
	pipeline_layout_create_info.pushConstantRangeCount = 1;
	pipeline_layout_create_info.pPushConstantRanges = &push_constant_range;

	err = vkCreatePipelineLayout (vulkan_globals.device, &pipeline_layout_create_info, NULL, &vulkan_globals.postprocess_pipeline.layout.handle);
	if (err != VK_SUCCESS)
		Sys_Error ("vkCreatePipelineLayout failed");
	GL_SetObjectName ((uint64_t)vulkan_globals.postprocess_pipeline.layout.handle, VK_OBJECT_TYPE_PIPELINE_LAYOUT, "postprocess_pipeline_layout");
	vulkan_globals.postprocess_pipeline.layout.push_constant_range = push_constant_range;

	// Screen effects
	VkDescriptorSetLayout screen_effects_descriptor_set_layouts[1] = {
		vulkan_globals.screen_effects_set_layout.handle,
	};

	memset (&push_constant_range, 0, sizeof (push_constant_range));
	push_constant_range.offset = 0;
	push_constant_range.size = 3 * sizeof (uint32_t) + 8 * sizeof (float);
	push_constant_range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

	pipeline_layout_create_info.setLayoutCount = 1;
	pipeline_layout_create_info.pSetLayouts = screen_effects_descriptor_set_layouts;
	pipeline_layout_create_info.pushConstantRangeCount = 1;
	pipeline_layout_create_info.pPushConstantRanges = &push_constant_range;

	err = vkCreatePipelineLayout (vulkan_globals.device, &pipeline_layout_create_info, NULL, &vulkan_globals.screen_effects_pipeline.layout.handle);
	if (err != VK_SUCCESS)
		Sys_Error ("vkCreatePipelineLayout failed");
	GL_SetObjectName ((uint64_t)vulkan_globals.screen_effects_pipeline.layout.handle, VK_OBJECT_TYPE_PIPELINE_LAYOUT, "screen_effects_pipeline_layout");
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

	memset (&push_constant_range, 0, sizeof (push_constant_range));
	push_constant_range.offset = 0;
	push_constant_range.size = 1 * sizeof (float);
	push_constant_range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

	pipeline_layout_create_info.setLayoutCount = 2;
	pipeline_layout_create_info.pSetLayouts = tex_warp_descriptor_set_layouts;
	pipeline_layout_create_info.pushConstantRangeCount = 1;
	pipeline_layout_create_info.pPushConstantRanges = &push_constant_range;

	err = vkCreatePipelineLayout (vulkan_globals.device, &pipeline_layout_create_info, NULL, &vulkan_globals.cs_tex_warp_pipeline.layout.handle);
	if (err != VK_SUCCESS)
		Sys_Error ("vkCreatePipelineLayout failed");
	GL_SetObjectName ((uint64_t)vulkan_globals.cs_tex_warp_pipeline.layout.handle, VK_OBJECT_TYPE_PIPELINE_LAYOUT, "cs_tex_warp_pipeline_layout");
	vulkan_globals.cs_tex_warp_pipeline.layout.push_constant_range = push_constant_range;

	// Show triangles
	pipeline_layout_create_info.setLayoutCount = 0;
	pipeline_layout_create_info.pushConstantRangeCount = 0;

	err = vkCreatePipelineLayout (vulkan_globals.device, &pipeline_layout_create_info, NULL, &vulkan_globals.showtris_pipeline.layout.handle);
	if (err != VK_SUCCESS)
		Sys_Error ("vkCreatePipelineLayout failed");
	GL_SetObjectName ((uint64_t)vulkan_globals.showtris_pipeline.layout.handle, VK_OBJECT_TYPE_PIPELINE_LAYOUT, "showtris_pipeline_layout");
	vulkan_globals.showtris_pipeline.layout.push_constant_range = push_constant_range;

	// Update lightmaps
	VkDescriptorSetLayout update_lightmap_descriptor_set_layouts[1] = {
		vulkan_globals.lightmap_compute_set_layout.handle,
	};

	memset (&push_constant_range, 0, sizeof (push_constant_range));
	push_constant_range.offset = 0;
	push_constant_range.size = 2 * sizeof (uint32_t);
	push_constant_range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

	pipeline_layout_create_info.setLayoutCount = 1;
	pipeline_layout_create_info.pSetLayouts = update_lightmap_descriptor_set_layouts;
	pipeline_layout_create_info.pushConstantRangeCount = 1;
	pipeline_layout_create_info.pPushConstantRanges = &push_constant_range;

	err = vkCreatePipelineLayout (vulkan_globals.device, &pipeline_layout_create_info, NULL, &vulkan_globals.update_lightmap_pipeline.layout.handle);
	if (err != VK_SUCCESS)
		Sys_Error ("vkCreatePipelineLayout failed");
	GL_SetObjectName ((uint64_t)vulkan_globals.update_lightmap_pipeline.layout.handle, VK_OBJECT_TYPE_PIPELINE_LAYOUT, "update_lightmap_pipeline_layout");
	vulkan_globals.update_lightmap_pipeline.layout.push_constant_range = push_constant_range;
}

/*
===============
R_InitSamplers
===============
*/
void R_InitSamplers ()
{
	GL_WaitForDeviceIdle ();
	Sys_Printf ("Initializing samplers\n");

	VkResult err;

	if (vulkan_globals.point_sampler == VK_NULL_HANDLE)
	{
		VkSamplerCreateInfo sampler_create_info;
		memset (&sampler_create_info, 0, sizeof (sampler_create_info));
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

		err = vkCreateSampler (vulkan_globals.device, &sampler_create_info, NULL, &vulkan_globals.point_sampler);
		if (err != VK_SUCCESS)
			Sys_Error ("vkCreateSampler failed");

		GL_SetObjectName ((uint64_t)vulkan_globals.point_sampler, VK_OBJECT_TYPE_SAMPLER, "point");

		sampler_create_info.anisotropyEnable = VK_TRUE;
		sampler_create_info.maxAnisotropy = vulkan_globals.device_properties.limits.maxSamplerAnisotropy;
		err = vkCreateSampler (vulkan_globals.device, &sampler_create_info, NULL, &vulkan_globals.point_aniso_sampler);
		if (err != VK_SUCCESS)
			Sys_Error ("vkCreateSampler failed");

		GL_SetObjectName ((uint64_t)vulkan_globals.point_aniso_sampler, VK_OBJECT_TYPE_SAMPLER, "point_aniso");

		sampler_create_info.magFilter = VK_FILTER_LINEAR;
		sampler_create_info.minFilter = VK_FILTER_LINEAR;
		sampler_create_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		sampler_create_info.anisotropyEnable = VK_FALSE;
		sampler_create_info.maxAnisotropy = 1.0f;

		err = vkCreateSampler (vulkan_globals.device, &sampler_create_info, NULL, &vulkan_globals.linear_sampler);
		if (err != VK_SUCCESS)
			Sys_Error ("vkCreateSampler failed");

		GL_SetObjectName ((uint64_t)vulkan_globals.linear_sampler, VK_OBJECT_TYPE_SAMPLER, "linear");

		sampler_create_info.anisotropyEnable = VK_TRUE;
		sampler_create_info.maxAnisotropy = vulkan_globals.device_properties.limits.maxSamplerAnisotropy;
		err = vkCreateSampler (vulkan_globals.device, &sampler_create_info, NULL, &vulkan_globals.linear_aniso_sampler);
		if (err != VK_SUCCESS)
			Sys_Error ("vkCreateSampler failed");

		GL_SetObjectName ((uint64_t)vulkan_globals.linear_aniso_sampler, VK_OBJECT_TYPE_SAMPLER, "linear_aniso");
	}

	if (vulkan_globals.point_sampler_lod_bias != VK_NULL_HANDLE)
	{
		vkDestroySampler (vulkan_globals.device, vulkan_globals.point_sampler_lod_bias, NULL);
		vkDestroySampler (vulkan_globals.device, vulkan_globals.point_aniso_sampler_lod_bias, NULL);
		vkDestroySampler (vulkan_globals.device, vulkan_globals.linear_sampler_lod_bias, NULL);
		vkDestroySampler (vulkan_globals.device, vulkan_globals.linear_aniso_sampler_lod_bias, NULL);
	}

	{
		float lod_bias = 0.0f;
		if (r_lodbias.value)
		{
			if (vulkan_globals.supersampling)
			{
				switch (vulkan_globals.sample_count)
				{
				case VK_SAMPLE_COUNT_2_BIT:
					lod_bias -= 0.5f;
					break;
				case VK_SAMPLE_COUNT_4_BIT:
					lod_bias -= 1.0f;
					break;
				case VK_SAMPLE_COUNT_8_BIT:
					lod_bias -= 1.5f;
					break;
				case VK_SAMPLE_COUNT_16_BIT:
					lod_bias -= 2.0f;
					break;
				default: /* silences gcc's -Wswitch */
					break;
				}
			}

			if (r_scale.value >= 8)
				lod_bias += 3.0f;
			else if (r_scale.value >= 4)
				lod_bias += 2.0f;
			else if (r_scale.value >= 2)
				lod_bias += 1.0f;
		}

		Sys_Printf ("Texture lod bias: %f\n", lod_bias);

		VkSamplerCreateInfo sampler_create_info;
		memset (&sampler_create_info, 0, sizeof (sampler_create_info));
		sampler_create_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		sampler_create_info.magFilter = VK_FILTER_NEAREST;
		sampler_create_info.minFilter = VK_FILTER_NEAREST;
		sampler_create_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		sampler_create_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		sampler_create_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		sampler_create_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		sampler_create_info.mipLodBias = lod_bias;
		sampler_create_info.maxAnisotropy = 1.0f;
		sampler_create_info.minLod = 0;
		sampler_create_info.maxLod = FLT_MAX;

		err = vkCreateSampler (vulkan_globals.device, &sampler_create_info, NULL, &vulkan_globals.point_sampler_lod_bias);
		if (err != VK_SUCCESS)
			Sys_Error ("vkCreateSampler failed");

		GL_SetObjectName ((uint64_t)vulkan_globals.point_sampler_lod_bias, VK_OBJECT_TYPE_SAMPLER, "point_lod_bias");

		sampler_create_info.anisotropyEnable = VK_TRUE;
		sampler_create_info.maxAnisotropy = vulkan_globals.device_properties.limits.maxSamplerAnisotropy;
		err = vkCreateSampler (vulkan_globals.device, &sampler_create_info, NULL, &vulkan_globals.point_aniso_sampler_lod_bias);
		if (err != VK_SUCCESS)
			Sys_Error ("vkCreateSampler failed");

		GL_SetObjectName ((uint64_t)vulkan_globals.point_aniso_sampler_lod_bias, VK_OBJECT_TYPE_SAMPLER, "point_aniso_lod_bias");

		sampler_create_info.magFilter = VK_FILTER_LINEAR;
		sampler_create_info.minFilter = VK_FILTER_LINEAR;
		sampler_create_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		sampler_create_info.anisotropyEnable = VK_FALSE;
		sampler_create_info.maxAnisotropy = 1.0f;

		err = vkCreateSampler (vulkan_globals.device, &sampler_create_info, NULL, &vulkan_globals.linear_sampler_lod_bias);
		if (err != VK_SUCCESS)
			Sys_Error ("vkCreateSampler failed");

		GL_SetObjectName ((uint64_t)vulkan_globals.linear_sampler_lod_bias, VK_OBJECT_TYPE_SAMPLER, "linear_lod_bias");

		sampler_create_info.anisotropyEnable = VK_TRUE;
		sampler_create_info.maxAnisotropy = vulkan_globals.device_properties.limits.maxSamplerAnisotropy;
		err = vkCreateSampler (vulkan_globals.device, &sampler_create_info, NULL, &vulkan_globals.linear_aniso_sampler_lod_bias);
		if (err != VK_SUCCESS)
			Sys_Error ("vkCreateSampler failed");

		GL_SetObjectName ((uint64_t)vulkan_globals.linear_aniso_sampler_lod_bias, VK_OBJECT_TYPE_SAMPLER, "linear_aniso_lod_bias");
	}

	TexMgr_UpdateTextureDescriptorSets ();
}

/*
===============
R_CreateShaderModule
===============
*/
static VkShaderModule R_CreateShaderModule (byte *code, int size, const char *name)
{
	VkShaderModuleCreateInfo module_create_info;
	memset (&module_create_info, 0, sizeof (module_create_info));
	module_create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	module_create_info.pNext = NULL;
	module_create_info.codeSize = size;
	module_create_info.pCode = (uint32_t *)code;

	VkShaderModule module;
	VkResult       err = vkCreateShaderModule (vulkan_globals.device, &module_create_info, NULL, &module);
	if (err != VK_SUCCESS)
		Sys_Error ("vkCreateShaderModule failed");

	GL_SetObjectName ((uint64_t)module, VK_OBJECT_TYPE_SHADER_MODULE, name);

	return module;
}

typedef struct pipeline_create_infos_s
{
	VkPipelineShaderStageCreateInfo        shader_stages[2];
	VkPipelineDynamicStateCreateInfo       dynamic_state;
	VkDynamicState                         dynamic_states[3];
	VkPipelineVertexInputStateCreateInfo   vertex_input_state;
	VkPipelineInputAssemblyStateCreateInfo input_assembly_state;
	VkPipelineViewportStateCreateInfo      viewport_state;
	VkPipelineRasterizationStateCreateInfo rasterization_state;
	VkPipelineMultisampleStateCreateInfo   multisample_state;
	VkPipelineDepthStencilStateCreateInfo  depth_stencil_state;
	VkPipelineColorBlendStateCreateInfo    color_blend_state;
	VkPipelineColorBlendAttachmentState    blend_attachment_state;
	VkGraphicsPipelineCreateInfo           graphics_pipeline;
	VkComputePipelineCreateInfo            compute_pipeline;
} pipeline_create_infos_t;

static VkVertexInputAttributeDescription basic_vertex_input_attribute_descriptions[3];
static VkVertexInputBindingDescription   basic_vertex_binding_description;
static VkVertexInputAttributeDescription world_vertex_input_attribute_descriptions[3];
static VkVertexInputBindingDescription   world_vertex_binding_description;
static VkVertexInputAttributeDescription alias_vertex_input_attribute_descriptions[5];
static VkVertexInputBindingDescription   alias_vertex_binding_descriptions[3];

#define DECLARE_SHADER_MODULE(name) static VkShaderModule name##_module
#define CREATE_SHADER_MODULE(name)                                                 \
	do                                                                             \
	{                                                                              \
		name##_module = R_CreateShaderModule (name##_spv, name##_spv_size, #name); \
	} while (0)
#define CREATE_SHADER_MODULE_COND(name, cond)                                                              \
	do                                                                                                     \
	{                                                                                                      \
		name##_module = cond ? R_CreateShaderModule (name##_spv, name##_spv_size, #name) : VK_NULL_HANDLE; \
	} while (0)
#define DESTROY_SHADER_MODULE(name)                                             \
	do                                                                          \
	{                                                                           \
		if (name##_module != VK_NULL_HANDLE)                                    \
			vkDestroyShaderModule (vulkan_globals.device, name##_module, NULL); \
		name##_module = VK_NULL_HANDLE;                                         \
	} while (0)

DECLARE_SHADER_MODULE (basic_vert);
DECLARE_SHADER_MODULE (basic_frag);
DECLARE_SHADER_MODULE (basic_alphatest_frag);
DECLARE_SHADER_MODULE (basic_notex_frag);
DECLARE_SHADER_MODULE (world_vert);
DECLARE_SHADER_MODULE (world_frag);
DECLARE_SHADER_MODULE (alias_vert);
DECLARE_SHADER_MODULE (alias_frag);
DECLARE_SHADER_MODULE (alias_alphatest_frag);
DECLARE_SHADER_MODULE (sky_layer_vert);
DECLARE_SHADER_MODULE (sky_layer_frag);
DECLARE_SHADER_MODULE (sky_box_frag);
DECLARE_SHADER_MODULE (postprocess_vert);
DECLARE_SHADER_MODULE (postprocess_frag);
DECLARE_SHADER_MODULE (screen_effects_8bit_comp);
DECLARE_SHADER_MODULE (screen_effects_8bit_scale_comp);
DECLARE_SHADER_MODULE (screen_effects_8bit_scale_sops_comp);
DECLARE_SHADER_MODULE (screen_effects_10bit_comp);
DECLARE_SHADER_MODULE (screen_effects_10bit_scale_comp);
DECLARE_SHADER_MODULE (screen_effects_10bit_scale_sops_comp);
DECLARE_SHADER_MODULE (cs_tex_warp_comp);
DECLARE_SHADER_MODULE (showtris_vert);
DECLARE_SHADER_MODULE (showtris_frag);
DECLARE_SHADER_MODULE (update_lightmap_comp);

/*
===============
R_InitVertexAttributes
===============
*/
static void R_InitVertexAttributes ()
{
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

	basic_vertex_binding_description.binding = 0;
	basic_vertex_binding_description.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
	basic_vertex_binding_description.stride = 24;

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

	world_vertex_binding_description.binding = 0;
	world_vertex_binding_description.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
	world_vertex_binding_description.stride = 28;

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

	alias_vertex_binding_descriptions[0].binding = 0;
	alias_vertex_binding_descriptions[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
	alias_vertex_binding_descriptions[0].stride = 8;
	alias_vertex_binding_descriptions[1].binding = 1;
	alias_vertex_binding_descriptions[1].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
	alias_vertex_binding_descriptions[1].stride = 8;
	alias_vertex_binding_descriptions[2].binding = 2;
	alias_vertex_binding_descriptions[2].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
	alias_vertex_binding_descriptions[2].stride = 8;
}

/*
===============
R_InitDefaultStates
===============
*/
static void R_InitDefaultStates (pipeline_create_infos_t *infos)
{
	memset (infos, 0, sizeof (pipeline_create_infos_t));

	infos->dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	infos->dynamic_state.pDynamicStates = infos->dynamic_states;

	infos->shader_stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	infos->shader_stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	infos->shader_stages[0].module = basic_vert_module;
	infos->shader_stages[0].pName = "main";

	infos->shader_stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	infos->shader_stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	infos->shader_stages[1].module = basic_frag_module;
	infos->shader_stages[1].pName = "main";

	infos->vertex_input_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	infos->vertex_input_state.vertexAttributeDescriptionCount = 3;
	infos->vertex_input_state.pVertexAttributeDescriptions = basic_vertex_input_attribute_descriptions;
	infos->vertex_input_state.vertexBindingDescriptionCount = 1;
	infos->vertex_input_state.pVertexBindingDescriptions = &basic_vertex_binding_description;

	infos->input_assembly_state.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	infos->input_assembly_state.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

	infos->viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	infos->viewport_state.viewportCount = 1;
	infos->dynamic_states[infos->dynamic_state.dynamicStateCount++] = VK_DYNAMIC_STATE_VIEWPORT;
	infos->viewport_state.scissorCount = 1;
	infos->dynamic_states[infos->dynamic_state.dynamicStateCount++] = VK_DYNAMIC_STATE_SCISSOR;

	infos->rasterization_state.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	infos->rasterization_state.polygonMode = VK_POLYGON_MODE_FILL;
	infos->rasterization_state.cullMode = VK_CULL_MODE_BACK_BIT;
	infos->rasterization_state.frontFace = VK_FRONT_FACE_CLOCKWISE;
	infos->rasterization_state.depthClampEnable = VK_FALSE;
	infos->rasterization_state.rasterizerDiscardEnable = VK_FALSE;
	infos->rasterization_state.depthBiasEnable = VK_FALSE;
	infos->rasterization_state.lineWidth = 1.0f;

	infos->multisample_state.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	infos->multisample_state.rasterizationSamples = vulkan_globals.sample_count;
	if (vulkan_globals.supersampling)
	{
		infos->multisample_state.sampleShadingEnable = VK_TRUE;
		infos->multisample_state.minSampleShading = 1.0f;
	}

	infos->depth_stencil_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	infos->depth_stencil_state.depthTestEnable = VK_FALSE;
	infos->depth_stencil_state.depthWriteEnable = VK_FALSE;
	infos->depth_stencil_state.depthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL;
	infos->depth_stencil_state.depthBoundsTestEnable = VK_FALSE;
	infos->depth_stencil_state.back.failOp = VK_STENCIL_OP_KEEP;
	infos->depth_stencil_state.back.passOp = VK_STENCIL_OP_KEEP;
	infos->depth_stencil_state.back.compareOp = VK_COMPARE_OP_ALWAYS;
	infos->depth_stencil_state.stencilTestEnable = VK_FALSE;
	infos->depth_stencil_state.front = infos->depth_stencil_state.back;

	infos->color_blend_state.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	infos->blend_attachment_state.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	infos->blend_attachment_state.blendEnable = VK_FALSE;
	infos->blend_attachment_state.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
	infos->blend_attachment_state.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	infos->blend_attachment_state.colorBlendOp = VK_BLEND_OP_ADD;
	infos->blend_attachment_state.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
	infos->blend_attachment_state.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	infos->blend_attachment_state.alphaBlendOp = VK_BLEND_OP_ADD;
	infos->color_blend_state.attachmentCount = 1;
	infos->color_blend_state.pAttachments = &infos->blend_attachment_state;

	infos->graphics_pipeline.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	infos->graphics_pipeline.stageCount = 2;
	infos->graphics_pipeline.pStages = infos->shader_stages;
	infos->graphics_pipeline.pVertexInputState = &infos->vertex_input_state;
	infos->graphics_pipeline.pInputAssemblyState = &infos->input_assembly_state;
	infos->graphics_pipeline.pViewportState = &infos->viewport_state;
	infos->graphics_pipeline.pRasterizationState = &infos->rasterization_state;
	infos->graphics_pipeline.pMultisampleState = &infos->multisample_state;
	infos->graphics_pipeline.pDepthStencilState = &infos->depth_stencil_state;
	infos->graphics_pipeline.pColorBlendState = &infos->color_blend_state;
	infos->graphics_pipeline.pDynamicState = &infos->dynamic_state;
	infos->graphics_pipeline.layout = vulkan_globals.basic_pipeline_layout.handle;
	infos->graphics_pipeline.renderPass = vulkan_globals.secondary_cb_contexts[CBX_WORLD_0].render_pass;
}

/*
===============
R_CreateBasicPipelines
===============
*/
static void R_CreateBasicPipelines ()
{
	int                     render_pass;
	VkResult                err;
	pipeline_create_infos_t infos;
	R_InitDefaultStates (&infos);

	VkRenderPass main_render_pass = vulkan_globals.secondary_cb_contexts[CBX_WORLD_0].render_pass;
	VkRenderPass ui_render_pass = vulkan_globals.secondary_cb_contexts[CBX_GUI].render_pass;

	infos.depth_stencil_state.depthTestEnable = VK_TRUE;
	infos.depth_stencil_state.depthWriteEnable = VK_TRUE;
	infos.shader_stages[1].module = basic_alphatest_frag_module;
	for (render_pass = 0; render_pass < 2; ++render_pass)
	{
		infos.graphics_pipeline.renderPass = (render_pass == 0) ? main_render_pass : ui_render_pass;
		infos.multisample_state.rasterizationSamples = (render_pass == 0) ? vulkan_globals.sample_count : VK_SAMPLE_COUNT_1_BIT;

		assert (vulkan_globals.basic_alphatest_pipeline[render_pass].handle == VK_NULL_HANDLE);
		err = vkCreateGraphicsPipelines (
			vulkan_globals.device, VK_NULL_HANDLE, 1, &infos.graphics_pipeline, NULL, &vulkan_globals.basic_alphatest_pipeline[render_pass].handle);
		if (err != VK_SUCCESS)
			Sys_Error ("vkCreateGraphicsPipelines failed");
		vulkan_globals.basic_alphatest_pipeline[render_pass].layout = vulkan_globals.basic_pipeline_layout;
		GL_SetObjectName ((uint64_t)vulkan_globals.basic_alphatest_pipeline[render_pass].handle, VK_OBJECT_TYPE_PIPELINE, "basic_alphatest");
	}

	infos.shader_stages[1].module = basic_notex_frag_module;
	infos.depth_stencil_state.depthTestEnable = VK_FALSE;
	infos.depth_stencil_state.depthWriteEnable = VK_FALSE;
	infos.blend_attachment_state.blendEnable = VK_TRUE;

	for (render_pass = 0; render_pass < 2; ++render_pass)
	{
		infos.graphics_pipeline.renderPass = (render_pass == 0) ? main_render_pass : ui_render_pass;
		infos.multisample_state.rasterizationSamples = (render_pass == 0) ? vulkan_globals.sample_count : VK_SAMPLE_COUNT_1_BIT;

		assert (vulkan_globals.basic_notex_blend_pipeline[render_pass].handle == VK_NULL_HANDLE);
		err = vkCreateGraphicsPipelines (
			vulkan_globals.device, VK_NULL_HANDLE, 1, &infos.graphics_pipeline, NULL, &vulkan_globals.basic_notex_blend_pipeline[render_pass].handle);
		if (err != VK_SUCCESS)
			Sys_Error ("vkCreateGraphicsPipelines failed");
		vulkan_globals.basic_notex_blend_pipeline[render_pass].layout = vulkan_globals.basic_pipeline_layout;
		GL_SetObjectName ((uint64_t)vulkan_globals.basic_notex_blend_pipeline[render_pass].handle, VK_OBJECT_TYPE_PIPELINE, "basic_notex_blend");
	}

	infos.graphics_pipeline.renderPass = main_render_pass;
	infos.graphics_pipeline.subpass = 0;
	infos.multisample_state.rasterizationSamples = vulkan_globals.sample_count;

	infos.shader_stages[1].module = basic_frag_module;

	for (render_pass = 0; render_pass < 2; ++render_pass)
	{
		infos.graphics_pipeline.renderPass = (render_pass == 0) ? main_render_pass : ui_render_pass;
		infos.multisample_state.rasterizationSamples = (render_pass == 0) ? vulkan_globals.sample_count : VK_SAMPLE_COUNT_1_BIT;

		assert (vulkan_globals.basic_blend_pipeline[render_pass].handle == VK_NULL_HANDLE);
		err = vkCreateGraphicsPipelines (
			vulkan_globals.device, VK_NULL_HANDLE, 1, &infos.graphics_pipeline, NULL, &vulkan_globals.basic_blend_pipeline[render_pass].handle);
		if (err != VK_SUCCESS)
			Sys_Error ("vkCreateGraphicsPipelines failed");
		vulkan_globals.basic_blend_pipeline[render_pass].layout = vulkan_globals.basic_pipeline_layout;

		GL_SetObjectName ((uint64_t)vulkan_globals.basic_blend_pipeline[render_pass].handle, VK_OBJECT_TYPE_PIPELINE, "basic_blend");
	}
}

/*
===============
R_CreateWarpPipelines
===============
*/
static void R_CreateWarpPipelines ()
{
	VkResult                err;
	pipeline_create_infos_t infos;
	R_InitDefaultStates (&infos);

	infos.multisample_state.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	infos.input_assembly_state.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;

	infos.blend_attachment_state.blendEnable = VK_FALSE;

	infos.shader_stages[0].module = basic_vert_module;
	infos.shader_stages[1].module = basic_frag_module;

	infos.graphics_pipeline.renderPass = vulkan_globals.warp_render_pass;

	assert (vulkan_globals.raster_tex_warp_pipeline.handle == VK_NULL_HANDLE);
	err = vkCreateGraphicsPipelines (vulkan_globals.device, VK_NULL_HANDLE, 1, &infos.graphics_pipeline, NULL, &vulkan_globals.raster_tex_warp_pipeline.handle);
	if (err != VK_SUCCESS)
		Sys_Error ("vkCreateGraphicsPipelines failed");
	vulkan_globals.raster_tex_warp_pipeline.layout = vulkan_globals.basic_pipeline_layout;
	GL_SetObjectName ((uint64_t)vulkan_globals.raster_tex_warp_pipeline.handle, VK_OBJECT_TYPE_PIPELINE, "warp");

	VkPipelineShaderStageCreateInfo compute_shader_stage;
	memset (&compute_shader_stage, 0, sizeof (compute_shader_stage));
	compute_shader_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	compute_shader_stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	compute_shader_stage.module = cs_tex_warp_comp_module;
	compute_shader_stage.pName = "main";

	memset (&infos.compute_pipeline, 0, sizeof (infos.compute_pipeline));
	infos.compute_pipeline.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
	infos.compute_pipeline.stage = compute_shader_stage;
	infos.compute_pipeline.layout = vulkan_globals.cs_tex_warp_pipeline.layout.handle;

	assert (vulkan_globals.cs_tex_warp_pipeline.handle == VK_NULL_HANDLE);
	err = vkCreateComputePipelines (vulkan_globals.device, VK_NULL_HANDLE, 1, &infos.compute_pipeline, NULL, &vulkan_globals.cs_tex_warp_pipeline.handle);
	if (err != VK_SUCCESS)
		Sys_Error ("vkCreateComputePipelines failed");
	GL_SetObjectName ((uint64_t)vulkan_globals.cs_tex_warp_pipeline.handle, VK_OBJECT_TYPE_PIPELINE, "cs_tex_warp");
}

/*
===============
R_CreateParticlesPipelines
===============
*/
static void R_CreateParticlesPipelines ()
{
	VkResult                err;
	pipeline_create_infos_t infos;
	R_InitDefaultStates (&infos);

	infos.depth_stencil_state.depthTestEnable = VK_TRUE;
	infos.depth_stencil_state.depthWriteEnable = VK_FALSE;

	infos.graphics_pipeline.renderPass = vulkan_globals.secondary_cb_contexts[CBX_WORLD_0].render_pass;

	infos.blend_attachment_state.blendEnable = VK_TRUE;

	assert (vulkan_globals.particle_pipeline.handle == VK_NULL_HANDLE);
	err = vkCreateGraphicsPipelines (vulkan_globals.device, VK_NULL_HANDLE, 1, &infos.graphics_pipeline, NULL, &vulkan_globals.particle_pipeline.handle);
	if (err != VK_SUCCESS)
		Sys_Error ("vkCreateGraphicsPipelines failed");
	vulkan_globals.particle_pipeline.layout = vulkan_globals.basic_pipeline_layout;
	GL_SetObjectName ((uint64_t)vulkan_globals.particle_pipeline.handle, VK_OBJECT_TYPE_PIPELINE, "particles");
}

/*
===============
R_CreateFTEParticlesPipelines
===============
*/
static void R_CreateFTEParticlesPipelines ()
{
#ifdef PSET_SCRIPT
	VkResult                err;
	pipeline_create_infos_t infos;
	R_InitDefaultStates (&infos);

	static const VkBlendFactor source_blend_factors[8] = {
		VK_BLEND_FACTOR_SRC_ALPHA, // BM_BLEND
		VK_BLEND_FACTOR_SRC_COLOR, // BM_BLENDCOLOUR
		VK_BLEND_FACTOR_SRC_ALPHA, // BM_ADDA
		VK_BLEND_FACTOR_SRC_COLOR, // BM_ADDC
		VK_BLEND_FACTOR_SRC_ALPHA, // BM_SUBTRACT
		VK_BLEND_FACTOR_ZERO,      // BM_INVMODA
		VK_BLEND_FACTOR_ZERO,      // BM_INVMODC
		VK_BLEND_FACTOR_ONE        // BM_PREMUL
	};
	static const VkBlendFactor dest_blend_factors[8] = {
		VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, // BM_BLEND
		VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR, // BM_BLENDCOLOUR
		VK_BLEND_FACTOR_ONE,                 // BM_ADDA
		VK_BLEND_FACTOR_ONE,                 // BM_ADDC
		VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR, // BM_SUBTRACT
		VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, // BM_INVMODA
		VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR, // BM_INVMODC
		VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA  // BM_PREMUL
	};

	static const char *fte_particle_pipeline_names[16] = {"fte_particles_blend_tris",
	                                                      "fte_particles_blend_color_tris",
	                                                      "fte_particles_add_color_tris",
	                                                      "fte_particles_add_alpha_tris",
	                                                      "fte_particles_subtract_tris",
	                                                      "fte_particles_inv_modulate_alpha_tris",
	                                                      "fte_particles_inv_modulate_color_tris",
	                                                      "fte_particles_premultiplied_tris",
	                                                      "fte_particles_blend_lines",
	                                                      "fte_particles_blend_color_lines",
	                                                      "fte_particles_add_color_lines",
	                                                      "fte_particles_add_alpha_lines",
	                                                      "fte_particles_subtract_lines",
	                                                      "fte_particles_inv_modulate_alpha_lines",
	                                                      "fte_particles_inv_modulate_color_lines",
	                                                      "fte_particles_premultiplied_lines"};

	infos.rasterization_state.cullMode = VK_CULL_MODE_NONE;
	infos.rasterization_state.depthBiasEnable = VK_TRUE;
	infos.rasterization_state.depthBiasConstantFactor = OFFSET_DECAL;
	infos.rasterization_state.depthBiasSlopeFactor = 1.0f;

	infos.depth_stencil_state.depthTestEnable = VK_TRUE;
	infos.depth_stencil_state.depthWriteEnable = VK_FALSE;
	infos.graphics_pipeline.renderPass = vulkan_globals.secondary_cb_contexts[CBX_WORLD_0].render_pass;
	infos.blend_attachment_state.blendEnable = VK_TRUE;

	infos.multisample_state.sampleShadingEnable = VK_FALSE;

	for (int i = 0; i < 8; ++i)
	{
		infos.input_assembly_state.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		infos.rasterization_state.polygonMode = VK_POLYGON_MODE_FILL;
		infos.blend_attachment_state.srcColorBlendFactor = source_blend_factors[i];
		infos.blend_attachment_state.dstColorBlendFactor = dest_blend_factors[i];
		infos.blend_attachment_state.colorBlendOp = VK_BLEND_OP_ADD;
		infos.blend_attachment_state.srcAlphaBlendFactor = source_blend_factors[i];
		infos.blend_attachment_state.dstAlphaBlendFactor = source_blend_factors[i];

		assert (vulkan_globals.fte_particle_pipelines[i].handle == VK_NULL_HANDLE);
		err = vkCreateGraphicsPipelines (
			vulkan_globals.device, VK_NULL_HANDLE, 1, &infos.graphics_pipeline, NULL, &vulkan_globals.fte_particle_pipelines[i].handle);
		if (err != VK_SUCCESS)
			Sys_Error ("vkCreateGraphicsPipelines failed");
		vulkan_globals.fte_particle_pipelines[i].layout = vulkan_globals.basic_pipeline_layout;
		GL_SetObjectName ((uint64_t)vulkan_globals.fte_particle_pipelines[i].handle, VK_OBJECT_TYPE_PIPELINE, fte_particle_pipeline_names[i]);

		if (vulkan_globals.non_solid_fill)
		{
			infos.input_assembly_state.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
			infos.rasterization_state.polygonMode = VK_POLYGON_MODE_LINE;

			assert (vulkan_globals.fte_particle_pipelines[i + 8].handle == VK_NULL_HANDLE);
			err = vkCreateGraphicsPipelines (
				vulkan_globals.device, VK_NULL_HANDLE, 1, &infos.graphics_pipeline, NULL, &vulkan_globals.fte_particle_pipelines[i + 8].handle);
			if (err != VK_SUCCESS)
				Sys_Error ("vkCreateGraphicsPipelines failed");
			vulkan_globals.fte_particle_pipelines[i + 8].layout = vulkan_globals.basic_pipeline_layout;
			GL_SetObjectName ((uint64_t)vulkan_globals.fte_particle_pipelines[i + 8].handle, VK_OBJECT_TYPE_PIPELINE, fte_particle_pipeline_names[i + 8]);
		}
	}
#endif
}

/*
===============
R_CreateSpritesPipelines
===============
*/
static void R_CreateSpritesPipelines ()
{
	VkResult                err;
	pipeline_create_infos_t infos;
	R_InitDefaultStates (&infos);

	infos.shader_stages[0].module = basic_vert_module;
	infos.shader_stages[1].module = basic_alphatest_frag_module;
	infos.blend_attachment_state.blendEnable = VK_FALSE;
	infos.depth_stencil_state.depthTestEnable = VK_TRUE;
	infos.depth_stencil_state.depthWriteEnable = VK_TRUE;

	infos.dynamic_states[infos.dynamic_state.dynamicStateCount++] = VK_DYNAMIC_STATE_DEPTH_BIAS;

	assert (vulkan_globals.sprite_pipeline.handle == VK_NULL_HANDLE);
	err = vkCreateGraphicsPipelines (vulkan_globals.device, VK_NULL_HANDLE, 1, &infos.graphics_pipeline, NULL, &vulkan_globals.sprite_pipeline.handle);
	if (err != VK_SUCCESS)
		Sys_Error ("vkCreateGraphicsPipelines failed");
	vulkan_globals.sprite_pipeline.layout = vulkan_globals.basic_pipeline_layout;
	GL_SetObjectName ((uint64_t)vulkan_globals.sprite_pipeline.handle, VK_OBJECT_TYPE_PIPELINE, "sprite");
}

/*
===============
R_CreateSkyPipelines
===============
*/
static void R_CreateSkyPipelines ()
{
	VkResult                err;
	pipeline_create_infos_t infos;
	R_InitDefaultStates (&infos);

	infos.graphics_pipeline.renderPass = vulkan_globals.secondary_cb_contexts[CBX_WORLD_0].render_pass;

	infos.graphics_pipeline.stageCount = 1;
	infos.shader_stages[1].module = VK_NULL_HANDLE;

	infos.depth_stencil_state.depthTestEnable = VK_TRUE;
	infos.depth_stencil_state.depthWriteEnable = VK_TRUE;
	infos.depth_stencil_state.depthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL;
	infos.depth_stencil_state.stencilTestEnable = VK_TRUE;
	infos.depth_stencil_state.front.compareOp = VK_COMPARE_OP_ALWAYS;
	infos.depth_stencil_state.front.failOp = VK_STENCIL_OP_KEEP;
	infos.depth_stencil_state.front.depthFailOp = VK_STENCIL_OP_KEEP;
	infos.depth_stencil_state.front.passOp = VK_STENCIL_OP_REPLACE;
	infos.depth_stencil_state.front.compareMask = 0xFF;
	infos.depth_stencil_state.front.writeMask = 0xFF;
	infos.depth_stencil_state.front.reference = 0x1;
	infos.blend_attachment_state.colorWriteMask = 0; // We only want to write stencil

	assert (vulkan_globals.sky_stencil_pipeline.handle == VK_NULL_HANDLE);
	err = vkCreateGraphicsPipelines (vulkan_globals.device, VK_NULL_HANDLE, 1, &infos.graphics_pipeline, NULL, &vulkan_globals.sky_stencil_pipeline.handle);
	if (err != VK_SUCCESS)
		Sys_Error ("vkCreateGraphicsPipelines failed");
	vulkan_globals.sky_stencil_pipeline.layout = vulkan_globals.basic_pipeline_layout;
	GL_SetObjectName ((uint64_t)vulkan_globals.sky_stencil_pipeline.handle, VK_OBJECT_TYPE_PIPELINE, "sky_stencil");

	infos.depth_stencil_state.stencilTestEnable = VK_FALSE;
	infos.blend_attachment_state.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	infos.graphics_pipeline.stageCount = 2;

	infos.shader_stages[1].module = basic_notex_frag_module;

	assert (vulkan_globals.sky_color_pipeline.handle == VK_NULL_HANDLE);
	err = vkCreateGraphicsPipelines (vulkan_globals.device, VK_NULL_HANDLE, 1, &infos.graphics_pipeline, NULL, &vulkan_globals.sky_color_pipeline.handle);
	if (err != VK_SUCCESS)
		Sys_Error ("vkCreateGraphicsPipelines failed");
	vulkan_globals.sky_color_pipeline.layout = vulkan_globals.basic_pipeline_layout;
	GL_SetObjectName ((uint64_t)vulkan_globals.sky_color_pipeline.handle, VK_OBJECT_TYPE_PIPELINE, "sky_color");

	infos.depth_stencil_state.depthTestEnable = VK_FALSE;
	infos.depth_stencil_state.depthWriteEnable = VK_FALSE;
	infos.depth_stencil_state.stencilTestEnable = VK_TRUE;
	infos.depth_stencil_state.front.compareOp = VK_COMPARE_OP_EQUAL;
	infos.depth_stencil_state.front.failOp = VK_STENCIL_OP_KEEP;
	infos.depth_stencil_state.front.depthFailOp = VK_STENCIL_OP_KEEP;
	infos.depth_stencil_state.front.passOp = VK_STENCIL_OP_KEEP;
	infos.depth_stencil_state.front.compareMask = 0xFF;
	infos.depth_stencil_state.front.writeMask = 0x0;
	infos.depth_stencil_state.front.reference = 0x1;
	infos.shader_stages[1].module = sky_box_frag_module;

	assert (vulkan_globals.sky_box_pipeline.handle == VK_NULL_HANDLE);
	err = vkCreateGraphicsPipelines (vulkan_globals.device, VK_NULL_HANDLE, 1, &infos.graphics_pipeline, NULL, &vulkan_globals.sky_box_pipeline.handle);
	if (err != VK_SUCCESS)
		Sys_Error ("vkCreateGraphicsPipelines failed");
	GL_SetObjectName ((uint64_t)vulkan_globals.sky_box_pipeline.handle, VK_OBJECT_TYPE_PIPELINE, "sky_box");

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

	infos.vertex_input_state.vertexAttributeDescriptionCount = 4;
	infos.vertex_input_state.pVertexAttributeDescriptions = sky_layer_vertex_input_attribute_descriptions;
	infos.vertex_input_state.vertexBindingDescriptionCount = 1;
	infos.vertex_input_state.pVertexBindingDescriptions = &sky_layer_vertex_binding_description;

	infos.shader_stages[0].module = sky_layer_vert_module;
	infos.shader_stages[1].module = sky_layer_frag_module;
	infos.blend_attachment_state.blendEnable = VK_FALSE;

	infos.graphics_pipeline.layout = vulkan_globals.sky_layer_pipeline.layout.handle;

	assert (vulkan_globals.sky_layer_pipeline.handle == VK_NULL_HANDLE);
	err = vkCreateGraphicsPipelines (vulkan_globals.device, VK_NULL_HANDLE, 1, &infos.graphics_pipeline, NULL, &vulkan_globals.sky_layer_pipeline.handle);
	if (err != VK_SUCCESS)
		Sys_Error ("vkCreateGraphicsPipelines failed");
	GL_SetObjectName ((uint64_t)vulkan_globals.sky_layer_pipeline.handle, VK_OBJECT_TYPE_PIPELINE, "sky_layer");
}

/*
===============
R_CreateShowTrisPipelines
===============
*/
static void R_CreateShowTrisPipelines ()
{
	VkResult                err;
	pipeline_create_infos_t infos;
	R_InitDefaultStates (&infos);

	if (vulkan_globals.non_solid_fill)
	{
		infos.rasterization_state.cullMode = VK_CULL_MODE_NONE;
		infos.rasterization_state.polygonMode = VK_POLYGON_MODE_LINE;
		infos.depth_stencil_state.depthTestEnable = VK_FALSE;
		infos.depth_stencil_state.depthWriteEnable = VK_FALSE;
		infos.input_assembly_state.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

		VkVertexInputAttributeDescription showtris_vertex_input_attribute_descriptions;
		showtris_vertex_input_attribute_descriptions.binding = 0;
		showtris_vertex_input_attribute_descriptions.format = VK_FORMAT_R32G32B32_SFLOAT;
		showtris_vertex_input_attribute_descriptions.location = 0;
		showtris_vertex_input_attribute_descriptions.offset = 0;

		VkVertexInputBindingDescription showtris_vertex_binding_description;
		showtris_vertex_binding_description.binding = 0;
		showtris_vertex_binding_description.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
		showtris_vertex_binding_description.stride = 24;

		infos.vertex_input_state.vertexAttributeDescriptionCount = 1;
		infos.vertex_input_state.pVertexAttributeDescriptions = &showtris_vertex_input_attribute_descriptions;
		infos.vertex_input_state.vertexBindingDescriptionCount = 1;
		infos.vertex_input_state.pVertexBindingDescriptions = &showtris_vertex_binding_description;

		infos.shader_stages[0].module = showtris_vert_module;
		infos.shader_stages[1].module = showtris_frag_module;

		infos.graphics_pipeline.layout = vulkan_globals.basic_pipeline_layout.handle;

		assert (vulkan_globals.showtris_pipeline.handle == VK_NULL_HANDLE);
		err = vkCreateGraphicsPipelines (vulkan_globals.device, VK_NULL_HANDLE, 1, &infos.graphics_pipeline, NULL, &vulkan_globals.showtris_pipeline.handle);
		if (err != VK_SUCCESS)
			Sys_Error ("vkCreateGraphicsPipelines failed");
		vulkan_globals.showtris_pipeline.layout = vulkan_globals.basic_pipeline_layout;
		GL_SetObjectName ((uint64_t)vulkan_globals.showtris_pipeline.handle, VK_OBJECT_TYPE_PIPELINE, "showtris");

		infos.depth_stencil_state.depthTestEnable = VK_TRUE;
		infos.rasterization_state.depthBiasEnable = VK_TRUE;
		infos.rasterization_state.depthBiasConstantFactor = 500.0f;
		infos.rasterization_state.depthBiasSlopeFactor = 0.0f;

		assert (vulkan_globals.showtris_depth_test_pipeline.handle == VK_NULL_HANDLE);
		err = vkCreateGraphicsPipelines (
			vulkan_globals.device, VK_NULL_HANDLE, 1, &infos.graphics_pipeline, NULL, &vulkan_globals.showtris_depth_test_pipeline.handle);
		if (err != VK_SUCCESS)
			Sys_Error ("vkCreateGraphicsPipelines failed");
		vulkan_globals.showtris_depth_test_pipeline.layout = vulkan_globals.basic_pipeline_layout;

		GL_SetObjectName ((uint64_t)vulkan_globals.showtris_depth_test_pipeline.handle, VK_OBJECT_TYPE_PIPELINE, "showtris_depth_test");

		infos.depth_stencil_state.depthTestEnable = VK_FALSE;
		infos.rasterization_state.depthBiasEnable = VK_FALSE;
		infos.input_assembly_state.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
		assert (vulkan_globals.showbboxes_pipeline.handle == VK_NULL_HANDLE);
		err = vkCreateGraphicsPipelines (vulkan_globals.device, VK_NULL_HANDLE, 1, &infos.graphics_pipeline, NULL, &vulkan_globals.showbboxes_pipeline.handle);
		if (err != VK_SUCCESS)
			Sys_Error ("vkCreateGraphicsPipelines failed");
		vulkan_globals.showbboxes_pipeline.layout = vulkan_globals.basic_pipeline_layout;

		GL_SetObjectName ((uint64_t)vulkan_globals.showbboxes_pipeline.handle, VK_OBJECT_TYPE_PIPELINE, "showbboxes");
	}
}

/*
===============
R_CreateWorldPipelines
===============
*/
static void R_CreateWorldPipelines ()
{
	VkResult                err;
	int                     alpha_blend, alpha_test, fullbright_enabled;
	pipeline_create_infos_t infos;
	R_InitDefaultStates (&infos);

	infos.rasterization_state.cullMode = VK_CULL_MODE_BACK_BIT;
	infos.rasterization_state.polygonMode = VK_POLYGON_MODE_FILL;
	infos.depth_stencil_state.depthTestEnable = VK_TRUE;
	infos.depth_stencil_state.depthWriteEnable = VK_TRUE;
	infos.rasterization_state.depthBiasEnable = VK_TRUE;
	infos.input_assembly_state.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	infos.dynamic_states[infos.dynamic_state.dynamicStateCount++] = VK_DYNAMIC_STATE_DEPTH_BIAS;

	infos.vertex_input_state.vertexAttributeDescriptionCount = 3;
	infos.vertex_input_state.pVertexAttributeDescriptions = world_vertex_input_attribute_descriptions;
	infos.vertex_input_state.vertexBindingDescriptionCount = 1;
	infos.vertex_input_state.pVertexBindingDescriptions = &world_vertex_binding_description;

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

	infos.graphics_pipeline.layout = vulkan_globals.world_pipeline_layout.handle;

	infos.shader_stages[0].module = world_vert_module;
	infos.shader_stages[1].module = world_frag_module;
	infos.shader_stages[1].pSpecializationInfo = &specialization_info;

	for (alpha_blend = 0; alpha_blend < 2; ++alpha_blend)
	{
		for (alpha_test = 0; alpha_test < 2; ++alpha_test)
		{
			for (fullbright_enabled = 0; fullbright_enabled < 2; ++fullbright_enabled)
			{
				int pipeline_index = fullbright_enabled + (alpha_test * 2) + (alpha_blend * 4);

				specialization_data[0] = fullbright_enabled;
				specialization_data[1] = alpha_test;
				specialization_data[2] = alpha_blend;

				infos.blend_attachment_state.blendEnable = alpha_blend ? VK_TRUE : VK_FALSE;
				infos.depth_stencil_state.depthWriteEnable = alpha_blend ? VK_FALSE : VK_TRUE;
				if (pipeline_index > 0)
				{
					infos.graphics_pipeline.flags = VK_PIPELINE_CREATE_DERIVATIVE_BIT;
					infos.graphics_pipeline.basePipelineHandle = vulkan_globals.world_pipelines[0].handle;
					infos.graphics_pipeline.basePipelineIndex = -1;
				}
				else
				{
					infos.graphics_pipeline.flags = VK_PIPELINE_CREATE_ALLOW_DERIVATIVES_BIT;
				}

				assert (vulkan_globals.world_pipelines[pipeline_index].handle == VK_NULL_HANDLE);
				err = vkCreateGraphicsPipelines (
					vulkan_globals.device, VK_NULL_HANDLE, 1, &infos.graphics_pipeline, NULL, &vulkan_globals.world_pipelines[pipeline_index].handle);
				if (err != VK_SUCCESS)
					Sys_Error ("vkCreateGraphicsPipelines failed");
				GL_SetObjectName ((uint64_t)vulkan_globals.world_pipelines[pipeline_index].handle, VK_OBJECT_TYPE_PIPELINE, va ("world %d", pipeline_index));
				vulkan_globals.world_pipelines[pipeline_index].layout = vulkan_globals.world_pipeline_layout;
			}
		}
	}
}

/*
===============
R_CreateAliasPipelines
===============
*/
static void R_CreateAliasPipelines ()
{
	VkResult                err;
	pipeline_create_infos_t infos;
	R_InitDefaultStates (&infos);

	infos.depth_stencil_state.depthTestEnable = VK_TRUE;
	infos.depth_stencil_state.depthWriteEnable = VK_TRUE;
	infos.rasterization_state.depthBiasEnable = VK_FALSE;
	infos.blend_attachment_state.blendEnable = VK_FALSE;
	infos.shader_stages[1].pSpecializationInfo = NULL;

	infos.vertex_input_state.vertexAttributeDescriptionCount = 5;
	infos.vertex_input_state.pVertexAttributeDescriptions = alias_vertex_input_attribute_descriptions;
	infos.vertex_input_state.vertexBindingDescriptionCount = 3;
	infos.vertex_input_state.pVertexBindingDescriptions = alias_vertex_binding_descriptions;

	infos.shader_stages[0].module = alias_vert_module;
	infos.shader_stages[1].module = alias_frag_module;

	infos.graphics_pipeline.layout = vulkan_globals.alias_pipeline.layout.handle;

	assert (vulkan_globals.alias_pipeline.handle == VK_NULL_HANDLE);
	err = vkCreateGraphicsPipelines (vulkan_globals.device, VK_NULL_HANDLE, 1, &infos.graphics_pipeline, NULL, &vulkan_globals.alias_pipeline.handle);
	if (err != VK_SUCCESS)
		Sys_Error ("vkCreateGraphicsPipelines failed");
	GL_SetObjectName ((uint64_t)vulkan_globals.alias_pipeline.handle, VK_OBJECT_TYPE_PIPELINE, "alias");

	infos.shader_stages[1].module = alias_alphatest_frag_module;

	assert (vulkan_globals.alias_alphatest_pipeline.handle == VK_NULL_HANDLE);
	err = vkCreateGraphicsPipelines (vulkan_globals.device, VK_NULL_HANDLE, 1, &infos.graphics_pipeline, NULL, &vulkan_globals.alias_alphatest_pipeline.handle);
	if (err != VK_SUCCESS)
		Sys_Error ("vkCreateGraphicsPipelines failed");
	GL_SetObjectName ((uint64_t)vulkan_globals.alias_alphatest_pipeline.handle, VK_OBJECT_TYPE_PIPELINE, "alias_alphatest");
	vulkan_globals.alias_alphatest_pipeline.layout = vulkan_globals.alias_pipeline.layout;

	infos.depth_stencil_state.depthWriteEnable = VK_FALSE;
	infos.blend_attachment_state.blendEnable = VK_TRUE;
	infos.shader_stages[1].module = alias_frag_module;

	assert (vulkan_globals.alias_blend_pipeline.handle == VK_NULL_HANDLE);
	err = vkCreateGraphicsPipelines (vulkan_globals.device, VK_NULL_HANDLE, 1, &infos.graphics_pipeline, NULL, &vulkan_globals.alias_blend_pipeline.handle);
	if (err != VK_SUCCESS)
		Sys_Error ("vkCreateGraphicsPipelines failed");
	GL_SetObjectName ((uint64_t)vulkan_globals.alias_blend_pipeline.handle, VK_OBJECT_TYPE_PIPELINE, "alias_blend");
	vulkan_globals.alias_blend_pipeline.layout = vulkan_globals.alias_pipeline.layout;

	infos.shader_stages[1].module = alias_alphatest_frag_module;

	assert (vulkan_globals.alias_alphatest_blend_pipeline.handle == VK_NULL_HANDLE);
	err = vkCreateGraphicsPipelines (
		vulkan_globals.device, VK_NULL_HANDLE, 1, &infos.graphics_pipeline, NULL, &vulkan_globals.alias_alphatest_blend_pipeline.handle);
	if (err != VK_SUCCESS)
		Sys_Error ("vkCreateGraphicsPipelines failed");
	GL_SetObjectName ((uint64_t)vulkan_globals.alias_alphatest_blend_pipeline.handle, VK_OBJECT_TYPE_PIPELINE, "alias_alphatest_blend");
	vulkan_globals.alias_alphatest_blend_pipeline.layout = vulkan_globals.alias_pipeline.layout;

	if (vulkan_globals.non_solid_fill)
	{
		infos.rasterization_state.cullMode = VK_CULL_MODE_NONE;
		infos.rasterization_state.polygonMode = VK_POLYGON_MODE_LINE;
		infos.depth_stencil_state.depthTestEnable = VK_FALSE;
		infos.depth_stencil_state.depthWriteEnable = VK_FALSE;
		infos.blend_attachment_state.blendEnable = VK_FALSE;
		infos.input_assembly_state.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

		infos.shader_stages[0].module = alias_vert_module;
		infos.shader_stages[1].module = showtris_frag_module;

		infos.graphics_pipeline.layout = vulkan_globals.alias_pipeline.layout.handle;

		assert (vulkan_globals.alias_showtris_pipeline.handle == VK_NULL_HANDLE);
		err = vkCreateGraphicsPipelines (
			vulkan_globals.device, VK_NULL_HANDLE, 1, &infos.graphics_pipeline, NULL, &vulkan_globals.alias_showtris_pipeline.handle);
		if (err != VK_SUCCESS)
			Sys_Error ("vkCreateGraphicsPipelines failed");
		GL_SetObjectName ((uint64_t)vulkan_globals.alias_showtris_pipeline.handle, VK_OBJECT_TYPE_PIPELINE, "alias_showtris");
		vulkan_globals.alias_showtris_pipeline.layout = vulkan_globals.alias_pipeline.layout;

		infos.depth_stencil_state.depthTestEnable = VK_TRUE;
		infos.rasterization_state.depthBiasEnable = VK_TRUE;
		infos.rasterization_state.depthBiasConstantFactor = 500.0f;
		infos.rasterization_state.depthBiasSlopeFactor = 0.0f;

		assert (vulkan_globals.alias_showtris_depth_test_pipeline.handle == VK_NULL_HANDLE);
		err = vkCreateGraphicsPipelines (
			vulkan_globals.device, VK_NULL_HANDLE, 1, &infos.graphics_pipeline, NULL, &vulkan_globals.alias_showtris_depth_test_pipeline.handle);
		if (err != VK_SUCCESS)
			Sys_Error ("vkCreateGraphicsPipelines failed");
		GL_SetObjectName ((uint64_t)vulkan_globals.alias_showtris_depth_test_pipeline.handle, VK_OBJECT_TYPE_PIPELINE, "alias_showtris_depth_test");
		vulkan_globals.alias_showtris_depth_test_pipeline.layout = vulkan_globals.alias_pipeline.layout;
	}
}

/*
===============
R_CreatePostprocessPipelines
===============
*/
static void R_CreatePostprocessPipelines ()
{
	VkResult                err;
	pipeline_create_infos_t infos;
	R_InitDefaultStates (&infos);

	infos.multisample_state.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
	infos.rasterization_state.cullMode = VK_CULL_MODE_NONE;
	infos.rasterization_state.polygonMode = VK_POLYGON_MODE_FILL;
	infos.rasterization_state.cullMode = VK_CULL_MODE_NONE;
	infos.rasterization_state.depthBiasEnable = VK_FALSE;
	infos.depth_stencil_state.depthTestEnable = VK_TRUE;
	infos.depth_stencil_state.depthWriteEnable = VK_TRUE;
	infos.blend_attachment_state.blendEnable = VK_FALSE;

	infos.vertex_input_state.vertexAttributeDescriptionCount = 0;
	infos.vertex_input_state.pVertexAttributeDescriptions = NULL;
	infos.vertex_input_state.vertexBindingDescriptionCount = 0;
	infos.vertex_input_state.pVertexBindingDescriptions = NULL;

	infos.shader_stages[0].module = postprocess_vert_module;
	infos.shader_stages[1].module = postprocess_frag_module;
	infos.graphics_pipeline.renderPass = vulkan_globals.secondary_cb_contexts[CBX_GUI].render_pass;
	infos.graphics_pipeline.layout = vulkan_globals.postprocess_pipeline.layout.handle;
	infos.graphics_pipeline.subpass = 1;

	assert (vulkan_globals.postprocess_pipeline.handle == VK_NULL_HANDLE);
	err = vkCreateGraphicsPipelines (vulkan_globals.device, VK_NULL_HANDLE, 1, &infos.graphics_pipeline, NULL, &vulkan_globals.postprocess_pipeline.handle);
	if (err != VK_SUCCESS)
		Sys_Error ("vkCreateGraphicsPipelines failed");
	GL_SetObjectName ((uint64_t)vulkan_globals.postprocess_pipeline.handle, VK_OBJECT_TYPE_PIPELINE, "postprocess");
}

/*
===============
R_CreateScreenEffectsPipelines
===============
*/
static void R_CreateScreenEffectsPipelines ()
{
	VkResult                err;
	pipeline_create_infos_t infos;
	R_InitDefaultStates (&infos);

	VkPipelineShaderStageCreateInfo compute_shader_stage;
	memset (&compute_shader_stage, 0, sizeof (compute_shader_stage));
	compute_shader_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	compute_shader_stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	compute_shader_stage.module =
		(vulkan_globals.color_format == VK_FORMAT_A2B10G10R10_UNORM_PACK32) ? screen_effects_10bit_comp_module : screen_effects_8bit_comp_module;
	compute_shader_stage.pName = "main";

	memset (&infos.compute_pipeline, 0, sizeof (infos.compute_pipeline));
	infos.compute_pipeline.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
	infos.compute_pipeline.stage = compute_shader_stage;
	infos.compute_pipeline.layout = vulkan_globals.screen_effects_pipeline.layout.handle;

	assert (vulkan_globals.screen_effects_pipeline.handle == VK_NULL_HANDLE);
	err = vkCreateComputePipelines (vulkan_globals.device, VK_NULL_HANDLE, 1, &infos.compute_pipeline, NULL, &vulkan_globals.screen_effects_pipeline.handle);
	if (err != VK_SUCCESS)
		Sys_Error ("vkCreateComputePipelines failed");
	GL_SetObjectName ((uint64_t)vulkan_globals.screen_effects_pipeline.handle, VK_OBJECT_TYPE_PIPELINE, "screen_effects");

	compute_shader_stage.module =
		(vulkan_globals.color_format == VK_FORMAT_A2B10G10R10_UNORM_PACK32) ? screen_effects_10bit_scale_comp_module : screen_effects_8bit_scale_comp_module;
	infos.compute_pipeline.stage = compute_shader_stage;
	assert (vulkan_globals.screen_effects_scale_pipeline.handle == VK_NULL_HANDLE);
	err = vkCreateComputePipelines (
		vulkan_globals.device, VK_NULL_HANDLE, 1, &infos.compute_pipeline, NULL, &vulkan_globals.screen_effects_scale_pipeline.handle);
	if (err != VK_SUCCESS)
		Sys_Error ("vkCreateComputePipelines failed");
	GL_SetObjectName ((uint64_t)vulkan_globals.screen_effects_scale_pipeline.handle, VK_OBJECT_TYPE_PIPELINE, "screen_effects_scale");

#if defined(VK_EXT_subgroup_size_control)
	if (vulkan_globals.screen_effects_sops)
	{
		compute_shader_stage.module = (vulkan_globals.color_format == VK_FORMAT_A2B10G10R10_UNORM_PACK32) ? screen_effects_10bit_scale_sops_comp_module
		                                                                                                  : screen_effects_8bit_scale_sops_comp_module;
		compute_shader_stage.flags =
			VK_PIPELINE_SHADER_STAGE_CREATE_ALLOW_VARYING_SUBGROUP_SIZE_BIT_EXT | VK_PIPELINE_SHADER_STAGE_CREATE_REQUIRE_FULL_SUBGROUPS_BIT_EXT;
		infos.compute_pipeline.stage = compute_shader_stage;
		assert (vulkan_globals.screen_effects_scale_sops_pipeline.handle == VK_NULL_HANDLE);
		err = vkCreateComputePipelines (
			vulkan_globals.device, VK_NULL_HANDLE, 1, &infos.compute_pipeline, NULL, &vulkan_globals.screen_effects_scale_sops_pipeline.handle);
		if (err != VK_SUCCESS)
			Sys_Error ("vkCreateComputePipelines failed");
		GL_SetObjectName ((uint64_t)vulkan_globals.screen_effects_scale_sops_pipeline.handle, VK_OBJECT_TYPE_PIPELINE, "screen_effects_scale_sops");
		compute_shader_stage.flags = 0;
	}
#endif
}

/*
===============
R_CreateUpdateLightmapPipelines
===============
*/
static void R_CreateUpdateLightmapPipelines ()
{
	VkResult                err;
	pipeline_create_infos_t infos;
	R_InitDefaultStates (&infos);

	VkPipelineShaderStageCreateInfo compute_shader_stage;
	memset (&compute_shader_stage, 0, sizeof (compute_shader_stage));
	compute_shader_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	compute_shader_stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	compute_shader_stage.module = update_lightmap_comp_module;
	compute_shader_stage.pName = "main";

	memset (&infos.compute_pipeline, 0, sizeof (infos.compute_pipeline));
	infos.compute_pipeline.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
	infos.compute_pipeline.stage = compute_shader_stage;
	infos.compute_pipeline.layout = vulkan_globals.update_lightmap_pipeline.layout.handle;

	assert (vulkan_globals.update_lightmap_pipeline.handle == VK_NULL_HANDLE);
	err = vkCreateComputePipelines (vulkan_globals.device, VK_NULL_HANDLE, 1, &infos.compute_pipeline, NULL, &vulkan_globals.update_lightmap_pipeline.handle);
	if (err != VK_SUCCESS)
		Sys_Error ("vkCreateComputePipelines failed");
	GL_SetObjectName ((uint64_t)vulkan_globals.update_lightmap_pipeline.handle, VK_OBJECT_TYPE_PIPELINE, "update_lightmap");
}

/*
===============
R_CreateShaderModules
===============
*/
static void R_CreateShaderModules ()
{
	CREATE_SHADER_MODULE (basic_vert);
	CREATE_SHADER_MODULE (basic_frag);
	CREATE_SHADER_MODULE (basic_alphatest_frag);
	CREATE_SHADER_MODULE (basic_notex_frag);
	CREATE_SHADER_MODULE (world_vert);
	CREATE_SHADER_MODULE (world_frag);
	CREATE_SHADER_MODULE (alias_vert);
	CREATE_SHADER_MODULE (alias_frag);
	CREATE_SHADER_MODULE (alias_alphatest_frag);
	CREATE_SHADER_MODULE (sky_layer_vert);
	CREATE_SHADER_MODULE (sky_layer_frag);
	CREATE_SHADER_MODULE (sky_box_frag);
	CREATE_SHADER_MODULE (postprocess_vert);
	CREATE_SHADER_MODULE (postprocess_frag);
	CREATE_SHADER_MODULE (screen_effects_8bit_comp);
	CREATE_SHADER_MODULE (screen_effects_8bit_scale_comp);
	CREATE_SHADER_MODULE_COND (screen_effects_8bit_scale_sops_comp, vulkan_globals.screen_effects_sops);
	CREATE_SHADER_MODULE (screen_effects_10bit_comp);
	CREATE_SHADER_MODULE (screen_effects_10bit_scale_comp);
	CREATE_SHADER_MODULE_COND (screen_effects_10bit_scale_sops_comp, vulkan_globals.screen_effects_sops);
	CREATE_SHADER_MODULE (cs_tex_warp_comp);
	CREATE_SHADER_MODULE (showtris_vert);
	CREATE_SHADER_MODULE (showtris_frag);
	CREATE_SHADER_MODULE (update_lightmap_comp);
}

/*
===============
R_DestroyShaderModules
===============
*/
static void R_DestroyShaderModules ()
{
	DESTROY_SHADER_MODULE (basic_vert);
	DESTROY_SHADER_MODULE (basic_frag);
	DESTROY_SHADER_MODULE (basic_alphatest_frag);
	DESTROY_SHADER_MODULE (basic_notex_frag);
	DESTROY_SHADER_MODULE (world_vert);
	DESTROY_SHADER_MODULE (world_frag);
	DESTROY_SHADER_MODULE (alias_vert);
	DESTROY_SHADER_MODULE (alias_frag);
	DESTROY_SHADER_MODULE (alias_alphatest_frag);
	DESTROY_SHADER_MODULE (sky_layer_vert);
	DESTROY_SHADER_MODULE (sky_layer_frag);
	DESTROY_SHADER_MODULE (sky_box_frag);
	DESTROY_SHADER_MODULE (postprocess_vert);
	DESTROY_SHADER_MODULE (postprocess_frag);
	DESTROY_SHADER_MODULE (screen_effects_8bit_comp);
	DESTROY_SHADER_MODULE (screen_effects_8bit_scale_comp);
	DESTROY_SHADER_MODULE (screen_effects_8bit_scale_sops_comp);
	DESTROY_SHADER_MODULE (screen_effects_10bit_comp);
	DESTROY_SHADER_MODULE (screen_effects_10bit_scale_comp);
	DESTROY_SHADER_MODULE (screen_effects_10bit_scale_sops_comp);
	DESTROY_SHADER_MODULE (cs_tex_warp_comp);
	DESTROY_SHADER_MODULE (showtris_vert);
	DESTROY_SHADER_MODULE (showtris_frag);
	DESTROY_SHADER_MODULE (update_lightmap_comp);
}

/*
===============
R_CreatePipelines
===============
*/
void R_CreatePipelines ()
{
	Sys_Printf ("Creating pipelines\n");

	R_CreateShaderModules ();
	R_InitVertexAttributes ();

	R_CreateBasicPipelines ();
	R_CreateWarpPipelines ();
	R_CreateParticlesPipelines ();
	R_CreateFTEParticlesPipelines ();
	R_CreateSpritesPipelines ();
	R_CreateSkyPipelines ();
	R_CreateShowTrisPipelines ();
	R_CreateWorldPipelines ();
	R_CreateAliasPipelines ();
	R_CreatePostprocessPipelines ();
	R_CreateScreenEffectsPipelines ();
	R_CreateUpdateLightmapPipelines ();

	R_DestroyShaderModules ();
}

/*
===============
R_DestroyPipelines
===============
*/
void R_DestroyPipelines (void)
{
	int i;
	for (i = 0; i < 2; ++i)
	{
		vkDestroyPipeline (vulkan_globals.device, vulkan_globals.basic_alphatest_pipeline[i].handle, NULL);
		vulkan_globals.basic_alphatest_pipeline[i].handle = VK_NULL_HANDLE;
		vkDestroyPipeline (vulkan_globals.device, vulkan_globals.basic_blend_pipeline[i].handle, NULL);
		vulkan_globals.basic_blend_pipeline[i].handle = VK_NULL_HANDLE;
		vkDestroyPipeline (vulkan_globals.device, vulkan_globals.basic_notex_blend_pipeline[i].handle, NULL);
		vulkan_globals.basic_notex_blend_pipeline[i].handle = VK_NULL_HANDLE;
	}
	for (i = 0; i < WORLD_PIPELINE_COUNT; ++i)
	{
		vkDestroyPipeline (vulkan_globals.device, vulkan_globals.world_pipelines[i].handle, NULL);
		vulkan_globals.world_pipelines[i].handle = VK_NULL_HANDLE;
	}
	vkDestroyPipeline (vulkan_globals.device, vulkan_globals.raster_tex_warp_pipeline.handle, NULL);
	vulkan_globals.raster_tex_warp_pipeline.handle = VK_NULL_HANDLE;
	vkDestroyPipeline (vulkan_globals.device, vulkan_globals.particle_pipeline.handle, NULL);
	vulkan_globals.particle_pipeline.handle = VK_NULL_HANDLE;
#ifdef PSET_SCRIPT
	for (i = 0; i < 8; ++i)
	{
		vkDestroyPipeline (vulkan_globals.device, vulkan_globals.fte_particle_pipelines[i].handle, NULL);
		vulkan_globals.fte_particle_pipelines[i].handle = VK_NULL_HANDLE;
		if (vulkan_globals.non_solid_fill)
		{
			vkDestroyPipeline (vulkan_globals.device, vulkan_globals.fte_particle_pipelines[i + 8].handle, NULL);
			vulkan_globals.fte_particle_pipelines[i + 8].handle = VK_NULL_HANDLE;
		}
	}
#endif
	vkDestroyPipeline (vulkan_globals.device, vulkan_globals.sprite_pipeline.handle, NULL);
	vulkan_globals.sprite_pipeline.handle = VK_NULL_HANDLE;
	vkDestroyPipeline (vulkan_globals.device, vulkan_globals.sky_stencil_pipeline.handle, NULL);
	vulkan_globals.sky_stencil_pipeline.handle = VK_NULL_HANDLE;
	vkDestroyPipeline (vulkan_globals.device, vulkan_globals.sky_color_pipeline.handle, NULL);
	vulkan_globals.sky_color_pipeline.handle = VK_NULL_HANDLE;
	vkDestroyPipeline (vulkan_globals.device, vulkan_globals.sky_box_pipeline.handle, NULL);
	vulkan_globals.sky_box_pipeline.handle = VK_NULL_HANDLE;
	vkDestroyPipeline (vulkan_globals.device, vulkan_globals.sky_layer_pipeline.handle, NULL);
	vulkan_globals.sky_layer_pipeline.handle = VK_NULL_HANDLE;
	vkDestroyPipeline (vulkan_globals.device, vulkan_globals.alias_pipeline.handle, NULL);
	vulkan_globals.alias_pipeline.handle = VK_NULL_HANDLE;
	vkDestroyPipeline (vulkan_globals.device, vulkan_globals.alias_alphatest_pipeline.handle, NULL);
	vulkan_globals.alias_alphatest_pipeline.handle = VK_NULL_HANDLE;
	vkDestroyPipeline (vulkan_globals.device, vulkan_globals.alias_alphatest_blend_pipeline.handle, NULL);
	vulkan_globals.alias_alphatest_blend_pipeline.handle = VK_NULL_HANDLE;
	vkDestroyPipeline (vulkan_globals.device, vulkan_globals.alias_blend_pipeline.handle, NULL);
	vulkan_globals.alias_blend_pipeline.handle = VK_NULL_HANDLE;
	vkDestroyPipeline (vulkan_globals.device, vulkan_globals.postprocess_pipeline.handle, NULL);
	vulkan_globals.postprocess_pipeline.handle = VK_NULL_HANDLE;
	vkDestroyPipeline (vulkan_globals.device, vulkan_globals.screen_effects_pipeline.handle, NULL);
	vulkan_globals.screen_effects_pipeline.handle = VK_NULL_HANDLE;
	vkDestroyPipeline (vulkan_globals.device, vulkan_globals.screen_effects_scale_pipeline.handle, NULL);
	vulkan_globals.screen_effects_scale_pipeline.handle = VK_NULL_HANDLE;
	if (vulkan_globals.screen_effects_scale_sops_pipeline.handle != VK_NULL_HANDLE)
	{
		vkDestroyPipeline (vulkan_globals.device, vulkan_globals.screen_effects_scale_sops_pipeline.handle, NULL);
		vulkan_globals.screen_effects_scale_sops_pipeline.handle = VK_NULL_HANDLE;
	}
	vkDestroyPipeline (vulkan_globals.device, vulkan_globals.cs_tex_warp_pipeline.handle, NULL);
	vulkan_globals.cs_tex_warp_pipeline.handle = VK_NULL_HANDLE;
	if (vulkan_globals.showtris_pipeline.handle != VK_NULL_HANDLE)
	{
		vkDestroyPipeline (vulkan_globals.device, vulkan_globals.showtris_pipeline.handle, NULL);
		vulkan_globals.showtris_pipeline.handle = VK_NULL_HANDLE;
		vkDestroyPipeline (vulkan_globals.device, vulkan_globals.showtris_depth_test_pipeline.handle, NULL);
		vulkan_globals.showtris_depth_test_pipeline.handle = VK_NULL_HANDLE;
		vkDestroyPipeline (vulkan_globals.device, vulkan_globals.showbboxes_pipeline.handle, NULL);
		vulkan_globals.showbboxes_pipeline.handle = VK_NULL_HANDLE;
	}
	if (vulkan_globals.alias_showtris_pipeline.handle != VK_NULL_HANDLE)
	{
		vkDestroyPipeline (vulkan_globals.device, vulkan_globals.alias_showtris_pipeline.handle, NULL);
		vulkan_globals.alias_showtris_pipeline.handle = VK_NULL_HANDLE;
		vkDestroyPipeline (vulkan_globals.device, vulkan_globals.alias_showtris_depth_test_pipeline.handle, NULL);
		vulkan_globals.alias_showtris_depth_test_pipeline.handle = VK_NULL_HANDLE;
	}
	vkDestroyPipeline (vulkan_globals.device, vulkan_globals.update_lightmap_pipeline.handle, NULL);
	vulkan_globals.update_lightmap_pipeline.handle = VK_NULL_HANDLE;
}

/*
===================
R_ScaleChanged_f
===================
*/
static void R_ScaleChanged_f (cvar_t *var)
{
	R_InitSamplers ();
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
	R_SIMD_f (&r_simd);
#endif
	Cvar_RegisterVariable (&r_speeds);
	Cvar_RegisterVariable (&r_pos);
	Cvar_RegisterVariable (&gl_polyblend);
	Cvar_RegisterVariable (&gl_nocolors);

	// johnfitz -- new cvars
	Cvar_RegisterVariable (&r_clearcolor);
	Cvar_SetCallback (&r_clearcolor, R_SetClearColor_f);
	Cvar_RegisterVariable (&r_fastclear);
	Cvar_SetCallback (&r_fastclear, R_SetFastClear_f);
	Cvar_RegisterVariable (&r_waterquality);
	Cvar_RegisterVariable (&r_waterwarp);
	Cvar_RegisterVariable (&r_waterwarpcompute);
	Cvar_RegisterVariable (&r_flatlightstyles);
	Cvar_RegisterVariable (&r_lerplightstyles);
	Cvar_RegisterVariable (&r_oldskyleaf);
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
	// johnfitz

	Cvar_RegisterVariable (&gl_zfix); // QuakeSpasm z-fighting fix
	Cvar_RegisterVariable (&r_lavaalpha);
	Cvar_RegisterVariable (&r_telealpha);
	Cvar_RegisterVariable (&r_slimealpha);
	Cvar_RegisterVariable (&r_scale);
	Cvar_RegisterVariable (&r_lodbias);
	Cvar_SetCallback (&r_scale, R_ScaleChanged_f);
	Cvar_SetCallback (&r_lodbias, R_ScaleChanged_f);
	Cvar_SetCallback (&r_lavaalpha, R_SetLavaalpha_f);
	Cvar_SetCallback (&r_telealpha, R_SetTelealpha_f);
	Cvar_SetCallback (&r_slimealpha, R_SetSlimealpha_f);

	Cvar_RegisterVariable (&r_gpulightmapupdate);
	Cvar_RegisterVariable (&r_tasks);
	Cvar_RegisterVariable (&r_parallelmark);
	Cvar_RegisterVariable (&r_usesops);

	R_InitParticles ();
	SetClearColor (); // johnfitz

	Sky_Init (); // johnfitz
	Fog_Init (); // johnfitz

	R_AllocateLightmapComputeBuffers ();

	staging_mutex = SDL_CreateMutex ();
}

/*
===============
R_AllocateDescriptorSet
===============
*/
VkDescriptorSet R_AllocateDescriptorSet (vulkan_desc_set_layout_t *layout)
{
	VkDescriptorSetAllocateInfo descriptor_set_allocate_info;
	memset (&descriptor_set_allocate_info, 0, sizeof (descriptor_set_allocate_info));
	descriptor_set_allocate_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	descriptor_set_allocate_info.descriptorPool = vulkan_globals.descriptor_pool;
	descriptor_set_allocate_info.descriptorSetCount = 1;
	descriptor_set_allocate_info.pSetLayouts = &layout->handle;

	VkDescriptorSet handle;
	vkAllocateDescriptorSets (vulkan_globals.device, &descriptor_set_allocate_info, &handle);

	num_vulkan_combined_image_samplers += layout->num_combined_image_samplers;
	num_vulkan_ubos_dynamic += layout->num_ubos_dynamic;
	num_vulkan_ubos += layout->num_ubos;
	num_vulkan_storage_buffers += layout->num_storage_buffers;
	num_vulkan_input_attachments += layout->num_input_attachments;
	num_vulkan_storage_images += layout->num_storage_images;

	return handle;
}

/*
===============
R_FreeDescriptorSet
===============
*/

void R_FreeDescriptorSet (VkDescriptorSet desc_set, vulkan_desc_set_layout_t *layout)
{
	vkFreeDescriptorSets (vulkan_globals.device, vulkan_globals.descriptor_pool, 1, &desc_set);

	num_vulkan_combined_image_samplers -= layout->num_combined_image_samplers;
	num_vulkan_ubos_dynamic -= layout->num_ubos_dynamic;
	num_vulkan_ubos -= layout->num_ubos;
	num_vulkan_storage_buffers -= layout->num_storage_buffers;
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
	int top, bottom;

	top = (cl.scores[playernum].colors & 0xf0) >> 4;
	bottom = cl.scores[playernum].colors & 15;

	// FIXME: if gl_nocolors is on, then turned off, the textures may be out of sync with the scoreboard colors.
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
	char        name[64];
	byte       *pixels;
	aliashdr_t *paliashdr;
	int         skinnum;

	// get correct texture pixels
	entity_t *currententity = &cl.entities[1 + playernum];

	if (!currententity->model || currententity->model->type != mod_alias)
		return;

	paliashdr = (aliashdr_t *)Mod_Extradata (currententity->model);

	skinnum = currententity->skinnum;

	// TODO: move these tests to the place where skinnum gets received from the server
	if (skinnum < 0 || skinnum >= paliashdr->numskins)
	{
		Con_DPrintf ("(%d): Invalid player skin #%d\n", playernum, skinnum);
		skinnum = 0;
	}

	pixels = (byte *)paliashdr + paliashdr->texels[skinnum]; // This is not a persistent place!

	// upload new image
	q_snprintf (name, sizeof (name), "player_%i", playernum);
	playertextures[playernum] = TexMgr_LoadImage (
		currententity->model, name, paliashdr->skinwidth, paliashdr->skinheight, SRC_INDEXED, pixels, paliashdr->gltextures[skinnum][0]->source_file,
		paliashdr->gltextures[skinnum][0]->source_offset, TEXPREF_PAD | TEXPREF_OVERWRITE);

	// now recolor it
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

	// clear playertexture pointers (the textures themselves were freed by texmgr_newgame)
	for (i = 0; i < MAX_SCOREBOARD; i++)
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
	char        key[128], value[4096];
	const char *data;

	map_fallbackalpha = r_wateralpha.value;
	map_wateralpha = (cl.worldmodel->contentstransparent & SURF_DRAWWATER) ? r_wateralpha.value : 1;
	map_lavaalpha = (cl.worldmodel->contentstransparent & SURF_DRAWLAVA) ? r_lavaalpha.value : 1;
	map_telealpha = (cl.worldmodel->contentstransparent & SURF_DRAWTELE) ? r_telealpha.value : 1;
	map_slimealpha = (cl.worldmodel->contentstransparent & SURF_DRAWSLIME) ? r_slimealpha.value : 1;

	data = COM_Parse (cl.worldmodel->entities);
	if (!data)
		return; // error
	if (com_token[0] != '{')
		return; // error
	while (1)
	{
		data = COM_Parse (data);
		if (!data)
			return; // error
		if (com_token[0] == '}')
			break; // end of worldspawn
		if (com_token[0] == '_')
			q_strlcpy (key, com_token + 1, sizeof (key));
		else
			q_strlcpy (key, com_token, sizeof (key));
		while (key[0] && key[strlen (key) - 1] == ' ') // remove trailing spaces
			key[strlen (key) - 1] = 0;
		data = COM_Parse (data);
		if (!data)
			return; // error
		q_strlcpy (value, com_token, sizeof (value));

		if (!strcmp ("wateralpha", key))
			map_wateralpha = atof (value);

		if (!strcmp ("lavaalpha", key))
			map_lavaalpha = atof (value);

		if (!strcmp ("telealpha", key))
			map_telealpha = atof (value);

		if (!strcmp ("slimealpha", key))
			map_slimealpha = atof (value);
	}
}

/*
===============
R_NewMap
===============
*/
void R_NewMap (void)
{
	int i;

	for (i = 0; i < 256; i++)
		d_lightstylevalue[i] = 264; // normal light value

	// clear out efrags in case the level hasn't been reloaded
	// FIXME: is this one short?
	for (i = 0; i < cl.worldmodel->numleafs; i++)
		cl.worldmodel->leafs[i].efrags = NULL;

	r_viewleaf = NULL;
	R_ClearParticles ();
#ifdef PSET_SCRIPT
	PScript_ClearParticles ();
#endif
	GL_DeleteBModelVertexBuffer ();

	GL_BuildLightmaps ();
	GL_BuildBModelVertexBuffer ();
	GL_PrepareSIMDData ();
	// ericw -- no longer load alias models into a VBO here, it's done in Mod_LoadAliasModel

	r_framecount = 0;    // johnfitz -- paranoid?
	r_visframecount = 0; // johnfitz -- paranoid?

	Sky_NewMap ();        // johnfitz -- skybox in worldspawn
	Fog_NewMap ();        // johnfitz -- global fog in worldspawn
	R_ParseWorldspawn (); // ericw -- wateralpha, lavaalpha, telealpha, slimealpha in worldspawn
}

/*
====================
R_TimeRefresh_f

For program optimization
====================
*/
void R_TimeRefresh_f (void)
{
	int   i;
	float start, stop, time;

	if (cls.state != ca_connected)
	{
		Con_Printf ("Not connected to a server\n");
		return;
	}

	GL_SynchronizeEndRenderingTask ();

	start = Sys_DoubleTime ();
	for (i = 0; i < 128; i++)
	{
		GL_BeginRendering (false, NULL, &glx, &gly, &glwidth, &glheight);
		r_refdef.viewangles[1] = i / 128.0 * 360.0;
		R_RenderView (false, INVALID_TASK_HANDLE, INVALID_TASK_HANDLE, INVALID_TASK_HANDLE);
		GL_EndRendering (false, false);
	}

	// glFinish ();
	stop = Sys_DoubleTime ();
	time = stop - start;
	Con_Printf ("%f seconds (%f fps)\n", time, 128 / time);
}

void R_AllocateVulkanMemory (vulkan_memory_t *memory, VkMemoryAllocateInfo *memory_allocate_info, vulkan_memory_type_t type)
{
	VkResult err = vkAllocateMemory (vulkan_globals.device, memory_allocate_info, NULL, &memory->handle);
	if (err != VK_SUCCESS)
		Sys_Error ("vkAllocateMemory failed");
	memory->type = type;
	memory->size = memory_allocate_info->allocationSize;
	if (memory->type == VULKAN_MEMORY_TYPE_DEVICE)
		total_device_vulkan_allocation_size += memory->size;
	else if (memory->type == VULKAN_MEMORY_TYPE_HOST)
		total_host_vulkan_allocation_size += memory->size;
}

void R_FreeVulkanMemory (vulkan_memory_t *memory)
{
	if (memory->type == VULKAN_MEMORY_TYPE_DEVICE)
		total_device_vulkan_allocation_size -= memory->size;
	else if (memory->type == VULKAN_MEMORY_TYPE_HOST)
		total_host_vulkan_allocation_size -= memory->size;
	vkFreeMemory (vulkan_globals.device, memory->handle, NULL);
	memory->handle = VK_NULL_HANDLE;
	memory->size = 0;
}

/*
====================
R_VulkanMemStats_f
====================
*/
void R_VulkanMemStats_f (void)
{
	Con_Printf ("Vulkan allocations:\n");
	Con_Printf (" Tex:    %d\n", num_vulkan_tex_allocations);
	Con_Printf (" BModel: %d\n", num_vulkan_bmodel_allocations);
	Con_Printf (" Mesh:   %d\n", num_vulkan_mesh_allocations);
	Con_Printf (" Misc:   %d\n", num_vulkan_misc_allocations);
	Con_Printf (" DynBuf: %d\n", num_vulkan_dynbuf_allocations);
	Con_Printf ("Descriptors:\n");
	Con_Printf (" Combined image samplers: %d\n", num_vulkan_combined_image_samplers);
	Con_Printf (" Dynamic UBOs: %d\n", num_vulkan_ubos_dynamic);
	Con_Printf (" UBOs: %d\n", num_vulkan_ubos);
	Con_Printf (" Storage buffers: %d\n", num_vulkan_ubos_dynamic);
	Con_Printf (" Input attachments: %d\n", num_vulkan_storage_buffers);
	Con_Printf (" Storage images: %d\n", num_vulkan_storage_images);
	Con_Printf ("Device %" SDL_PRIu64 " MiB total\n", (uint64_t)total_device_vulkan_allocation_size / 1024 / 1024);
	Con_Printf ("Host %" SDL_PRIu64 " MiB total\n", (uint64_t)total_host_vulkan_allocation_size / 1024 / 1024);
}
