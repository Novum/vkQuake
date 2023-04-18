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
#include "gl_heap.h"
#include <float.h>

cvar_t r_lodbias = {"r_lodbias", "1", CVAR_ARCHIVE};
cvar_t gl_lodbias = {"gl_lodbias", "0", CVAR_ARCHIVE};

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
extern cvar_t r_showbboxes_filter;
extern cvar_t r_lerpmodels;
extern cvar_t r_lerpmove;
extern cvar_t r_lerpturn;
extern cvar_t r_nolerp_list;
// johnfitz
extern cvar_t gl_zfix; // QuakeSpasm z-fighting fix
extern cvar_t r_alphasort;

extern cvar_t r_gpulightmapupdate;
extern cvar_t r_rtshadows;
extern cvar_t r_indirect;
extern cvar_t r_tasks;
extern cvar_t r_parallelmark;
extern cvar_t r_usesops;

#if defined(USE_SIMD)
extern cvar_t r_simd;
#endif
extern gltexture_t *playertextures[MAX_SCOREBOARD]; // johnfitz

vulkanglobals_t vulkan_globals;

atomic_uint32_t num_vulkan_tex_allocations;
atomic_uint32_t num_vulkan_bmodel_allocations;
atomic_uint32_t num_vulkan_mesh_allocations;
atomic_uint32_t num_vulkan_misc_allocations;
atomic_uint32_t num_vulkan_dynbuf_allocations;
atomic_uint32_t num_vulkan_combined_image_samplers;
atomic_uint32_t num_vulkan_ubos_dynamic;
atomic_uint32_t num_vulkan_ubos;
atomic_uint32_t num_vulkan_storage_buffers;
atomic_uint32_t num_vulkan_input_attachments;
atomic_uint32_t num_vulkan_storage_images;
atomic_uint32_t num_vulkan_sampled_images;
atomic_uint32_t num_acceleration_structures;
atomic_uint64_t total_device_vulkan_allocation_size;
atomic_uint64_t total_host_vulkan_allocation_size;

qboolean use_simd;

static SDL_mutex *vertex_allocate_mutex;
static SDL_mutex *index_allocate_mutex;
static SDL_mutex *uniform_allocate_mutex;
static SDL_mutex *storage_allocate_mutex;
static SDL_mutex *garbage_mutex;

/*
================
Staging
================
*/
#define NUM_STAGING_BUFFERS 2

typedef struct
{
	VkBuffer		buffer;
	VkCommandBuffer command_buffer;
	VkFence			fence;
	int				current_offset;
	qboolean		submitted;
	unsigned char  *data;
} stagingbuffer_t;

static VkCommandPool   staging_command_pool;
static vulkan_memory_t staging_memory;
static stagingbuffer_t staging_buffers[NUM_STAGING_BUFFERS];
static int			   current_staging_buffer = 0;
static int			   num_stagings_in_flight = 0;
static qboolean		   staging_submitting = false;
static SDL_mutex	  *staging_mutex;
static SDL_cond		  *staging_cond;
/*
================
Dynamic vertex/index & uniform buffer
================
*/
#define INITIAL_DYNAMIC_VERTEX_BUFFER_SIZE_KB  256
#define INITIAL_DYNAMIC_INDEX_BUFFER_SIZE_KB   1024
#define INITIAL_DYNAMIC_UNIFORM_BUFFER_SIZE_KB 256
#define NUM_DYNAMIC_BUFFERS					   2
#define GARBAGE_FRAME_COUNT					   3
#define MAX_UNIFORM_ALLOC					   2048

typedef struct
{
	VkBuffer		buffer;
	uint32_t		current_offset;
	unsigned char  *data;
	VkDeviceAddress device_address;
} dynbuffer_t;

static uint32_t		   current_dyn_vertex_buffer_size = INITIAL_DYNAMIC_VERTEX_BUFFER_SIZE_KB * 1024;
static uint32_t		   current_dyn_index_buffer_size = INITIAL_DYNAMIC_INDEX_BUFFER_SIZE_KB * 1024;
static uint32_t		   current_dyn_uniform_buffer_size = INITIAL_DYNAMIC_UNIFORM_BUFFER_SIZE_KB * 1024;
static uint32_t		   current_dyn_storage_buffer_size = 0; // Only used for RT so allocate lazily
static vulkan_memory_t dyn_vertex_buffer_memory;
static vulkan_memory_t dyn_index_buffer_memory;
static vulkan_memory_t dyn_uniform_buffer_memory;
static vulkan_memory_t dyn_storage_buffer_memory;
extern vulkan_memory_t lights_buffer_memory;
static dynbuffer_t	   dyn_vertex_buffers[NUM_DYNAMIC_BUFFERS];
static dynbuffer_t	   dyn_index_buffers[NUM_DYNAMIC_BUFFERS];
static dynbuffer_t	   dyn_uniform_buffers[NUM_DYNAMIC_BUFFERS];
static dynbuffer_t	   dyn_storage_buffers[NUM_DYNAMIC_BUFFERS];
static int			   current_dyn_buffer_index = 0;
static VkDescriptorSet ubo_descriptor_sets[2];

static int				current_garbage_index = 0;
static int				num_device_memory_garbage[GARBAGE_FRAME_COUNT];
static int				num_buffer_garbage[GARBAGE_FRAME_COUNT];
static int				num_desc_set_garbage[GARBAGE_FRAME_COUNT];
static vulkan_memory_t *device_memory_garbage[GARBAGE_FRAME_COUNT];
static VkDescriptorSet *descriptor_set_garbage[GARBAGE_FRAME_COUNT];
static VkBuffer		   *buffer_garbage[GARBAGE_FRAME_COUNT];

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
R_SetShowbboxesFilter_f
====================
*/
static void R_SetShowbboxesFilter_f (cvar_t *var)
{
	extern char *r_showbboxes_filter_strings;

	Mem_Free (r_showbboxes_filter_strings);
	r_showbboxes_filter_strings = NULL;

	if (*var->string)
	{
		char	   *filter, *p, *token;
		const char *delim = ",";
		int			len = strlen (var->string);
		int			size = len + 2;

		r_showbboxes_filter_strings = (char *)Mem_Alloc (size);
		filter = q_strdup (var->string);

		p = r_showbboxes_filter_strings;
		token = strtok (filter, delim);
		while (token != NULL)
		{
			strcpy (p, token);
			p += strlen (token) + 1;
			token = strtok (NULL, delim);
		}
		*p = '\0';

		Mem_Free (filter);
	}
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
	int	  s;

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
#elif defined(USE_NEON)
	// We only enable USE_NEON on AArch64 which always has support for it
	use_simd = var->value != 0.0f;
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
R_SetRTShadows_f
====================
*/
static void R_SetRTShadows_f (cvar_t *var)
{
	if (var->value)
		GL_BuildBModelAccelerationStructures ();
	else
		GL_DeleteBModelAccelerationStructures ();
	GL_UpdateLightmapDescriptorSets ();
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
	int		 i;
	VkResult err;

	ZEROED_STRUCT (VkBufferCreateInfo, buffer_create_info);
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
	const size_t aligned_size = q_align (memory_requirements.size, memory_requirements.alignment);

	ZEROED_STRUCT (VkMemoryAllocateInfo, memory_allocate_info);
	memory_allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	memory_allocate_info.allocationSize = NUM_STAGING_BUFFERS * aligned_size;
	memory_allocate_info.memoryTypeIndex =
		GL_MemoryTypeFromProperties (memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, VK_MEMORY_PROPERTY_HOST_CACHED_BIT);

	R_AllocateVulkanMemory (&staging_memory, &memory_allocate_info, VULKAN_MEMORY_TYPE_HOST, &num_vulkan_misc_allocations);
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
static void R_DestroyStagingBuffers (void)
{
	int i;

	R_FreeVulkanMemory (&staging_memory, NULL);
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
void R_InitStagingBuffers (void)
{
	int		 i;
	VkResult err;

	Con_Printf ("Initializing staging\n");

	R_CreateStagingBuffers ();

	ZEROED_STRUCT (VkCommandPoolCreateInfo, command_pool_create_info);
	command_pool_create_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	command_pool_create_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	command_pool_create_info.queueFamilyIndex = vulkan_globals.gfx_queue_family_index;

	err = vkCreateCommandPool (vulkan_globals.device, &command_pool_create_info, NULL, &staging_command_pool);
	if (err != VK_SUCCESS)
		Sys_Error ("vkCreateCommandPool failed");

	ZEROED_STRUCT (VkCommandBufferAllocateInfo, command_buffer_allocate_info);
	command_buffer_allocate_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	command_buffer_allocate_info.commandPool = staging_command_pool;
	command_buffer_allocate_info.commandBufferCount = NUM_STAGING_BUFFERS;

	VkCommandBuffer command_buffers[NUM_STAGING_BUFFERS];
	err = vkAllocateCommandBuffers (vulkan_globals.device, &command_buffer_allocate_info, command_buffers);
	if (err != VK_SUCCESS)
		Sys_Error ("vkAllocateCommandBuffers failed");

	ZEROED_STRUCT (VkFenceCreateInfo, fence_create_info);
	fence_create_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;

	ZEROED_STRUCT (VkCommandBufferBeginInfo, command_buffer_begin_info);
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
	storage_allocate_mutex = SDL_CreateMutex ();
	garbage_mutex = SDL_CreateMutex ();
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

	ZEROED_STRUCT (VkMemoryBarrier, memory_barrier);
	memory_barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
	memory_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	memory_barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
	vkCmdPipelineBarrier (
		staging_buffers[index].command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &memory_barrier, 0, NULL, 0, NULL);

	vkEndCommandBuffer (staging_buffers[index].command_buffer);

	ZEROED_STRUCT (VkMappedMemoryRange, range);
	range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
	range.memory = staging_memory.handle;
	range.size = VK_WHOLE_SIZE;
	vkFlushMappedMemoryRanges (vulkan_globals.device, 1, &range);

	ZEROED_STRUCT (VkSubmitInfo, submit_info);
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
void R_SubmitStagingBuffers (void)
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

	ZEROED_STRUCT (VkCommandBufferBeginInfo, command_buffer_begin_info);
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
	assert (alignment == Q_nextPow2 (alignment));
	staging_buffer->current_offset = q_align (staging_buffer->current_offset, alignment);

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
void R_StagingBeginCopy (void)
{
	SDL_UnlockMutex (staging_mutex);
}

/*
===============
R_StagingEndCopy
===============
*/
void R_StagingEndCopy (void)
{
	SDL_LockMutex (staging_mutex);
	num_stagings_in_flight -= 1;
	SDL_CondBroadcast (staging_cond);
	SDL_UnlockMutex (staging_mutex);
}

/*
===============
R_StagingUploadBuffer
===============
*/
void R_StagingUploadBuffer (const VkBuffer buffer, const size_t size, const byte *data)
{
	size_t remaining_size = size;
	size_t copy_offset = 0;

	while (remaining_size > 0)
	{
		const int		size_to_copy = q_min (remaining_size, vulkan_globals.staging_buffer_size);
		VkBuffer		staging_buffer;
		VkCommandBuffer command_buffer;
		int				staging_offset;
		unsigned char  *staging_ptr = R_StagingAllocate (size_to_copy, 1, &command_buffer, &staging_buffer, &staging_offset);

		VkBufferCopy region;
		region.srcOffset = staging_offset;
		region.dstOffset = copy_offset;
		region.size = size_to_copy;
		vkCmdCopyBuffer (command_buffer, staging_buffer, buffer, 1, &region);

		R_StagingBeginCopy ();
		memcpy (staging_ptr, (byte *)data + copy_offset, size_to_copy);
		R_StagingEndCopy ();

		copy_offset += size_to_copy;
		remaining_size -= size_to_copy;
	}
}

/*
===============
R_InitDynamicBuffers
===============
*/
static void R_InitDynamicBuffers (
	dynbuffer_t *buffers, vulkan_memory_t *memory, uint32_t *current_size, VkBufferUsageFlags usage_flags, qboolean get_device_address, const char *name)
{
	int i;

	Sys_Printf ("Reallocating dynamic %ss (%u KB)\n", name, *current_size / 1024);

	VkResult err;

	if (get_device_address)
		usage_flags |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT_KHR;

	ZEROED_STRUCT (VkBufferCreateInfo, buffer_create_info);
	buffer_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	buffer_create_info.size = *current_size;
	buffer_create_info.usage = usage_flags;

	for (i = 0; i < NUM_DYNAMIC_BUFFERS; ++i)
	{
		buffers[i].current_offset = 0;

		err = vkCreateBuffer (vulkan_globals.device, &buffer_create_info, NULL, &buffers[i].buffer);
		if (err != VK_SUCCESS)
			Sys_Error ("vkCreateBuffer failed");

		GL_SetObjectName ((uint64_t)buffers[i].buffer, VK_OBJECT_TYPE_BUFFER, name);
	}

	VkMemoryRequirements memory_requirements;
	vkGetBufferMemoryRequirements (vulkan_globals.device, buffers[0].buffer, &memory_requirements);

	const size_t aligned_size = q_align (memory_requirements.size, memory_requirements.alignment);

	ZEROED_STRUCT (VkMemoryAllocateFlagsInfo, memory_allocate_flags_info);
	if (get_device_address)
	{
		memory_allocate_flags_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO_KHR;
		memory_allocate_flags_info.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
	}

	ZEROED_STRUCT (VkMemoryAllocateInfo, memory_allocate_info);
	memory_allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	memory_allocate_info.pNext = get_device_address ? &memory_allocate_flags_info : NULL;
	memory_allocate_info.allocationSize = NUM_DYNAMIC_BUFFERS * aligned_size;
	memory_allocate_info.memoryTypeIndex =
		GL_MemoryTypeFromProperties (memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, VK_MEMORY_PROPERTY_HOST_CACHED_BIT);

	R_AllocateVulkanMemory (memory, &memory_allocate_info, VULKAN_MEMORY_TYPE_HOST, &num_vulkan_dynbuf_allocations);
	GL_SetObjectName ((uint64_t)memory->handle, VK_OBJECT_TYPE_DEVICE_MEMORY, name);

	for (i = 0; i < NUM_DYNAMIC_BUFFERS; ++i)
	{
		err = vkBindBufferMemory (vulkan_globals.device, buffers[i].buffer, memory->handle, i * aligned_size);
		if (err != VK_SUCCESS)
			Sys_Error ("vkBindBufferMemory failed");
	}

	void *data;
	err = vkMapMemory (vulkan_globals.device, memory->handle, 0, NUM_DYNAMIC_BUFFERS * aligned_size, 0, &data);
	if (err != VK_SUCCESS)
		Sys_Error ("vkMapMemory failed");

	for (i = 0; i < NUM_DYNAMIC_BUFFERS; ++i)
	{
		buffers[i].data = (unsigned char *)data + (i * aligned_size);

		if (get_device_address)
		{
			VkBufferDeviceAddressInfoKHR buffer_device_address_info;
			memset (&buffer_device_address_info, 0, sizeof (buffer_device_address_info));
			buffer_device_address_info.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO_KHR;
			buffer_device_address_info.buffer = buffers[i].buffer;
			VkDeviceAddress device_address = vulkan_globals.vk_get_buffer_device_address (vulkan_globals.device, &buffer_device_address_info);
			buffers[i].device_address = device_address;
		}
	}
}

/*
===============
R_InitDynamicVertexBuffers
===============
*/
static void R_InitDynamicVertexBuffers (void)
{
	R_InitDynamicBuffers (
		dyn_vertex_buffers, &dyn_vertex_buffer_memory, &current_dyn_vertex_buffer_size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, false, "vertex buffer");
}

/*
===============
R_InitDynamicIndexBuffers
===============
*/
static void R_InitDynamicIndexBuffers (void)
{
	R_InitDynamicBuffers (dyn_index_buffers, &dyn_index_buffer_memory, &current_dyn_index_buffer_size, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, false, "index buffer");
}

/*
===============
R_InitDynamicUniformBuffers
===============
*/
static void R_InitDynamicUniformBuffers (void)
{
	R_InitDynamicBuffers (
		dyn_uniform_buffers, &dyn_uniform_buffer_memory, &current_dyn_uniform_buffer_size, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, false, "uniform buffer");

	ZEROED_STRUCT (VkDescriptorSetAllocateInfo, descriptor_set_allocate_info);
	descriptor_set_allocate_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	descriptor_set_allocate_info.descriptorPool = vulkan_globals.descriptor_pool;
	descriptor_set_allocate_info.descriptorSetCount = 1;
	descriptor_set_allocate_info.pSetLayouts = &vulkan_globals.ubo_set_layout.handle;

	ubo_descriptor_sets[0] = R_AllocateDescriptorSet (&vulkan_globals.ubo_set_layout);
	ubo_descriptor_sets[1] = R_AllocateDescriptorSet (&vulkan_globals.ubo_set_layout);

	ZEROED_STRUCT (VkDescriptorBufferInfo, buffer_info);
	buffer_info.offset = 0;
	buffer_info.range = MAX_UNIFORM_ALLOC;

	ZEROED_STRUCT (VkWriteDescriptorSet, ubo_write);
	ubo_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	ubo_write.dstBinding = 0;
	ubo_write.dstArrayElement = 0;
	ubo_write.descriptorCount = 1;
	ubo_write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
	ubo_write.pBufferInfo = &buffer_info;

	for (int i = 0; i < NUM_DYNAMIC_BUFFERS; ++i)
	{
		buffer_info.buffer = dyn_uniform_buffers[i].buffer;
		ubo_write.dstSet = ubo_descriptor_sets[i];
		vkUpdateDescriptorSets (vulkan_globals.device, 1, &ubo_write, 0, NULL);
	}
}

/*
===============
R_InitDynamicStorageBuffers
===============
*/
static void R_InitDynamicStorageBuffers (void)
{
	VkBufferUsageFlags usage_flags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
	qboolean		   get_device_address = false;
	if (vulkan_globals.ray_query)
	{
		usage_flags |= VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
		get_device_address = true;
	}

	R_InitDynamicBuffers (dyn_storage_buffers, &dyn_storage_buffer_memory, &current_dyn_storage_buffer_size, usage_flags, get_device_address, "storage buffer");
}

/*
===============
R_InitFanIndexBuffer
===============
*/
static void R_InitFanIndexBuffer ()
{
	VkResult	   err;
	VkDeviceMemory memory;
	const int	   bufferSize = sizeof (uint16_t) * FAN_INDEX_BUFFER_SIZE;

	ZEROED_STRUCT (VkBufferCreateInfo, buffer_create_info);
	buffer_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	buffer_create_info.size = bufferSize;
	buffer_create_info.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

	err = vkCreateBuffer (vulkan_globals.device, &buffer_create_info, NULL, &vulkan_globals.fan_index_buffer);
	if (err != VK_SUCCESS)
		Sys_Error ("vkCreateBuffer failed");

	GL_SetObjectName ((uint64_t)vulkan_globals.fan_index_buffer, VK_OBJECT_TYPE_BUFFER, "Quad Index Buffer");

	VkMemoryRequirements memory_requirements;
	vkGetBufferMemoryRequirements (vulkan_globals.device, vulkan_globals.fan_index_buffer, &memory_requirements);

	ZEROED_STRUCT (VkMemoryAllocateInfo, memory_allocate_info);
	memory_allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	memory_allocate_info.allocationSize = memory_requirements.size;
	memory_allocate_info.memoryTypeIndex = GL_MemoryTypeFromProperties (memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0);

	Atomic_IncrementUInt32 (&num_vulkan_dynbuf_allocations);
	Atomic_AddUInt64 (&total_device_vulkan_allocation_size, memory_requirements.size);
	err = vkAllocateMemory (vulkan_globals.device, &memory_allocate_info, NULL, &memory);
	if (err != VK_SUCCESS)
		Sys_Error ("vkAllocateMemory failed");

	err = vkBindBufferMemory (vulkan_globals.device, vulkan_globals.fan_index_buffer, memory, 0);
	if (err != VK_SUCCESS)
		Sys_Error ("vkBindBufferMemory failed");

	{
		VkBuffer		staging_buffer;
		VkCommandBuffer command_buffer;
		int				staging_offset;
		int				current_index = 0;
		int				i;
		uint16_t	   *staging_mem = (uint16_t *)R_StagingAllocate (bufferSize, 1, &command_buffer, &staging_buffer, &staging_offset);

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
void R_SwapDynamicBuffers (void)
{
	current_dyn_buffer_index = (current_dyn_buffer_index + 1) % NUM_DYNAMIC_BUFFERS;
	dyn_vertex_buffers[current_dyn_buffer_index].current_offset = 0;
	dyn_index_buffers[current_dyn_buffer_index].current_offset = 0;
	dyn_uniform_buffers[current_dyn_buffer_index].current_offset = 0;
	dyn_storage_buffers[current_dyn_buffer_index].current_offset = 0;
}

/*
===============
R_FlushDynamicBuffers
===============
*/
void R_FlushDynamicBuffers (void)
{
	ZEROED_STRUCT_ARRAY (VkMappedMemoryRange, ranges, 5);
	int num_ranges = 0;
	ranges[num_ranges].sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
	ranges[num_ranges].memory = dyn_vertex_buffer_memory.handle;
	ranges[num_ranges++].size = VK_WHOLE_SIZE;
	ranges[num_ranges].sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
	ranges[num_ranges].memory = dyn_index_buffer_memory.handle;
	ranges[num_ranges++].size = VK_WHOLE_SIZE;
	ranges[num_ranges].sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
	ranges[num_ranges].memory = dyn_uniform_buffer_memory.handle;
	ranges[num_ranges++].size = VK_WHOLE_SIZE;
	ranges[num_ranges].sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
	ranges[num_ranges].memory = lights_buffer_memory.handle;
	ranges[num_ranges++].size = VK_WHOLE_SIZE;
	if (dyn_storage_buffer_memory.handle != VK_NULL_HANDLE)
	{
		ranges[num_ranges].sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
		ranges[num_ranges].memory = dyn_storage_buffer_memory.handle;
		ranges[num_ranges++].size = VK_WHOLE_SIZE;
	}
	vkFlushMappedMemoryRanges (vulkan_globals.device, num_ranges, ranges);
}

/*
===============
R_AddDynamicBufferGarbage
===============
*/
static void R_AddDynamicBufferGarbage (vulkan_memory_t memory, dynbuffer_t *buffers, VkDescriptorSet *descriptor_sets)
{
	SDL_LockMutex (garbage_mutex);

	{
		int *num_garbage = &num_device_memory_garbage[current_garbage_index];
		int	 old_num_memory_garbage = *num_garbage;
		*num_garbage += 1;
		if (device_memory_garbage[current_garbage_index] == NULL)
			device_memory_garbage[current_garbage_index] = Mem_Alloc (sizeof (vulkan_memory_t) * (*num_garbage));
		else
			device_memory_garbage[current_garbage_index] =
				Mem_Realloc (device_memory_garbage[current_garbage_index], sizeof (vulkan_memory_t) * (*num_garbage));
		device_memory_garbage[current_garbage_index][old_num_memory_garbage] = memory;
	}

	{
		int *num_garbage = &num_buffer_garbage[current_garbage_index];
		int	 old_num_buffer_garbage = *num_garbage;
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
		int	 old_num_desc_set_garbage = *num_garbage;
		*num_garbage += 2;
		if (descriptor_set_garbage[current_garbage_index] == NULL)
			descriptor_set_garbage[current_garbage_index] = Mem_Alloc (sizeof (VkDescriptorSet) * (*num_garbage));
		else
			descriptor_set_garbage[current_garbage_index] =
				Mem_Realloc (descriptor_set_garbage[current_garbage_index], sizeof (VkDescriptorSet) * (*num_garbage));
		for (int i = 0; i < 2; ++i)
			descriptor_set_garbage[current_garbage_index][old_num_desc_set_garbage + i] = descriptor_sets[i];
	}

	SDL_UnlockMutex (garbage_mutex);
}

/*
===============
R_CollectDynamicBufferGarbage
===============
*/
void R_CollectDynamicBufferGarbage (void)
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
			R_FreeVulkanMemory (&device_memory_garbage[collect_garbage_index][i], &num_vulkan_dynbuf_allocations);
		Mem_Free (device_memory_garbage[collect_garbage_index]);
		device_memory_garbage[collect_garbage_index] = NULL;
		num_device_memory_garbage[collect_garbage_index] = 0;
	}
}

/*
===============
R_DynBufferAllocate
===============
*/
byte *R_DynBufferAllocate (
	int size, int alignment, int min_tail_size, VkBuffer *buffer, VkDeviceSize *buffer_offset, VkDeviceAddress *device_address, SDL_mutex **mutex,
	dynbuffer_t *dyn_buffers, vulkan_memory_t *memory, uint32_t *current_size, VkDescriptorSet *descriptor_set, VkDescriptorSet *descriptor_sets,
	void (*init_func) (void))
{
	SDL_LockMutex (*mutex);
	dynbuffer_t *dyn_buffer = &dyn_buffers[current_dyn_buffer_index];
	const int	 aligned_size = q_align (size, alignment);

	if ((dyn_buffer->current_offset + q_max (size, min_tail_size)) > *current_size)
	{
		R_AddDynamicBufferGarbage (*memory, dyn_buffers, descriptor_sets);
		*current_size = q_max (*current_size * 2, (uint32_t)Q_nextPow2 (size));
		init_func ();
	}

	if (buffer)
		*buffer = dyn_buffer->buffer;
	if (buffer_offset)
		*buffer_offset = dyn_buffer->current_offset;
	if (device_address)
		*device_address = dyn_buffer->device_address + dyn_buffer->current_offset;

	unsigned char *data = dyn_buffer->data + dyn_buffer->current_offset;
	dyn_buffer->current_offset += aligned_size;

	if (descriptor_set)
		*descriptor_set = descriptor_sets[current_dyn_buffer_index];

	SDL_UnlockMutex (*mutex);
	return data;
}

/*
===============
R_VertexAllocate
Vertex buffers need to be aligned by the maximum format component size, i.e. sizeof(float)
===============
*/
byte *R_VertexAllocate (int size, VkBuffer *buffer, VkDeviceSize *buffer_offset)
{
	return R_DynBufferAllocate (
		size, sizeof (float), 0, buffer, buffer_offset, NULL, &vertex_allocate_mutex, dyn_vertex_buffers, &dyn_vertex_buffer_memory,
		&current_dyn_vertex_buffer_size, NULL, NULL, &R_InitDynamicVertexBuffers);
}

/*
===============
R_IndexAllocate
Align to 4 bytes because we allocate both uint16 and uint32
index buffers and alignment must match index size
===============
*/
byte *R_IndexAllocate (int size, VkBuffer *buffer, VkDeviceSize *buffer_offset)
{
	return R_DynBufferAllocate (
		size, sizeof (uint32_t), 0, buffer, buffer_offset, NULL, &index_allocate_mutex, dyn_index_buffers, &dyn_index_buffer_memory,
		&current_dyn_index_buffer_size, NULL, NULL, &R_InitDynamicIndexBuffers);
}

/*
===============
R_StorageAllocate
===============
*/
byte *R_StorageAllocate (int size, VkBuffer *buffer, VkDeviceSize *buffer_offset, VkDeviceAddress *device_address)
{
	return R_DynBufferAllocate (
		size, vulkan_globals.device_properties.limits.minStorageBufferOffsetAlignment, 0, buffer, buffer_offset, device_address, &storage_allocate_mutex,
		dyn_storage_buffers, &dyn_storage_buffer_memory, &current_dyn_storage_buffer_size, NULL, NULL, &R_InitDynamicStorageBuffers);
}

/*
===============
R_UniformAllocate
===============
*/
byte *R_UniformAllocate (int size, VkBuffer *buffer, uint32_t *buffer_offset, VkDescriptorSet *descriptor_set)
{
	if (size > MAX_UNIFORM_ALLOC)
		Sys_Error ("Increase MAX_UNIFORM_ALLOC");

	VkDeviceSize device_size_offset = 0;

	byte *data = R_DynBufferAllocate (
		size, vulkan_globals.device_properties.limits.minUniformBufferOffsetAlignment, MAX_UNIFORM_ALLOC, buffer, &device_size_offset, NULL,
		&uniform_allocate_mutex, dyn_uniform_buffers, &dyn_uniform_buffer_memory, &current_dyn_uniform_buffer_size, descriptor_set, ubo_descriptor_sets,
		&R_InitDynamicUniformBuffers);

	*buffer_offset = device_size_offset;
	return data;
}

/*
===============
R_InitGPUBuffers
===============
*/
void R_InitGPUBuffers (void)
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

	VkResult err;
	ZEROED_STRUCT (VkDescriptorSetLayoutCreateInfo, descriptor_set_layout_create_info);
	descriptor_set_layout_create_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;

	{
		ZEROED_STRUCT (VkDescriptorSetLayoutBinding, single_texture_layout_binding);
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
		GL_SetObjectName ((uint64_t)vulkan_globals.single_texture_set_layout.handle, VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, "single texture");
	}

	{
		ZEROED_STRUCT (VkDescriptorSetLayoutBinding, ubo_layout_bindings);
		ubo_layout_bindings.binding = 0;
		ubo_layout_bindings.descriptorCount = 1;
		ubo_layout_bindings.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
		ubo_layout_bindings.stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS;

		descriptor_set_layout_create_info.bindingCount = 1;
		descriptor_set_layout_create_info.pBindings = &ubo_layout_bindings;

		memset (&vulkan_globals.ubo_set_layout, 0, sizeof (vulkan_globals.ubo_set_layout));
		vulkan_globals.ubo_set_layout.num_ubos_dynamic = 1;

		err = vkCreateDescriptorSetLayout (vulkan_globals.device, &descriptor_set_layout_create_info, NULL, &vulkan_globals.ubo_set_layout.handle);
		if (err != VK_SUCCESS)
			Sys_Error ("vkCreateDescriptorSetLayout failed");
		GL_SetObjectName ((uint64_t)vulkan_globals.ubo_set_layout.handle, VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, "single dynamic UBO");
	}

	{
		ZEROED_STRUCT (VkDescriptorSetLayoutBinding, joints_buffer_layout_bindings);
		joints_buffer_layout_bindings.binding = 0;
		joints_buffer_layout_bindings.descriptorCount = 1;
		joints_buffer_layout_bindings.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		joints_buffer_layout_bindings.stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS;

		descriptor_set_layout_create_info.bindingCount = 1;
		descriptor_set_layout_create_info.pBindings = &joints_buffer_layout_bindings;

		memset (&vulkan_globals.joints_buffer_set_layout, 0, sizeof (vulkan_globals.joints_buffer_set_layout));
		vulkan_globals.joints_buffer_set_layout.num_storage_buffers = 1;

		err = vkCreateDescriptorSetLayout (vulkan_globals.device, &descriptor_set_layout_create_info, NULL, &vulkan_globals.joints_buffer_set_layout.handle);
		if (err != VK_SUCCESS)
			Sys_Error ("vkCreateDescriptorSetLayout failed");
		GL_SetObjectName ((uint64_t)vulkan_globals.joints_buffer_set_layout.handle, VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, "joints buffer");
	}

	{
		ZEROED_STRUCT (VkDescriptorSetLayoutBinding, input_attachment_layout_bindings);
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
		GL_SetObjectName ((uint64_t)vulkan_globals.input_attachment_set_layout.handle, VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, "input attachment");
	}

	{
		ZEROED_STRUCT_ARRAY (VkDescriptorSetLayoutBinding, screen_effects_layout_bindings, 5);
		screen_effects_layout_bindings[0].binding = 0;
		screen_effects_layout_bindings[0].descriptorCount = 1;
		screen_effects_layout_bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		screen_effects_layout_bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		screen_effects_layout_bindings[1].binding = 1;
		screen_effects_layout_bindings[1].descriptorCount = 1;
		screen_effects_layout_bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		screen_effects_layout_bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		screen_effects_layout_bindings[2].binding = 2;
		screen_effects_layout_bindings[2].descriptorCount = 1;
		screen_effects_layout_bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		screen_effects_layout_bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		screen_effects_layout_bindings[3].binding = 3;
		screen_effects_layout_bindings[3].descriptorCount = 1;
		screen_effects_layout_bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
		screen_effects_layout_bindings[3].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		screen_effects_layout_bindings[4].binding = 4;
		screen_effects_layout_bindings[4].descriptorCount = 1;
		screen_effects_layout_bindings[4].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		screen_effects_layout_bindings[4].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

		descriptor_set_layout_create_info.bindingCount = countof (screen_effects_layout_bindings);
		descriptor_set_layout_create_info.pBindings = screen_effects_layout_bindings;

		memset (&vulkan_globals.screen_effects_set_layout, 0, sizeof (vulkan_globals.screen_effects_set_layout));
		vulkan_globals.screen_effects_set_layout.num_combined_image_samplers = 2;
		vulkan_globals.screen_effects_set_layout.num_storage_images = 1;

		err = vkCreateDescriptorSetLayout (vulkan_globals.device, &descriptor_set_layout_create_info, NULL, &vulkan_globals.screen_effects_set_layout.handle);
		if (err != VK_SUCCESS)
			Sys_Error ("vkCreateDescriptorSetLayout failed");
		GL_SetObjectName ((uint64_t)vulkan_globals.screen_effects_set_layout.handle, VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, "screen effects");
	}

	{
		ZEROED_STRUCT (VkDescriptorSetLayoutBinding, single_texture_cs_write_layout_binding);
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
		GL_SetObjectName ((uint64_t)vulkan_globals.single_texture_cs_write_set_layout.handle, VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, "single storage image");
	}

	{
		int num_descriptors = 0;
		ZEROED_STRUCT_ARRAY (VkDescriptorSetLayoutBinding, lightmap_compute_layout_bindings, 9);
		lightmap_compute_layout_bindings[0].binding = num_descriptors++;
		lightmap_compute_layout_bindings[0].descriptorCount = 1;
		lightmap_compute_layout_bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		lightmap_compute_layout_bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		lightmap_compute_layout_bindings[1].binding = num_descriptors++;
		lightmap_compute_layout_bindings[1].descriptorCount = 1;
		lightmap_compute_layout_bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
		lightmap_compute_layout_bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		lightmap_compute_layout_bindings[2].binding = num_descriptors++;
		lightmap_compute_layout_bindings[2].descriptorCount = MAXLIGHTMAPS * 3 / 4;
		lightmap_compute_layout_bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
		lightmap_compute_layout_bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		lightmap_compute_layout_bindings[3].binding = num_descriptors++;
		lightmap_compute_layout_bindings[3].descriptorCount = 1;
		lightmap_compute_layout_bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		lightmap_compute_layout_bindings[3].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		lightmap_compute_layout_bindings[4].binding = num_descriptors++;
		lightmap_compute_layout_bindings[4].descriptorCount = 1;
		lightmap_compute_layout_bindings[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		lightmap_compute_layout_bindings[4].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		lightmap_compute_layout_bindings[5].binding = num_descriptors++;
		lightmap_compute_layout_bindings[5].descriptorCount = 1;
		lightmap_compute_layout_bindings[5].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
		lightmap_compute_layout_bindings[5].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		lightmap_compute_layout_bindings[6].binding = num_descriptors++;
		lightmap_compute_layout_bindings[6].descriptorCount = 1;
		lightmap_compute_layout_bindings[6].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
		lightmap_compute_layout_bindings[6].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		lightmap_compute_layout_bindings[7].binding = num_descriptors++;
		lightmap_compute_layout_bindings[7].descriptorCount = 1;
		lightmap_compute_layout_bindings[7].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		lightmap_compute_layout_bindings[7].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

		descriptor_set_layout_create_info.bindingCount = num_descriptors;
		descriptor_set_layout_create_info.pBindings = lightmap_compute_layout_bindings;

		memset (&vulkan_globals.lightmap_compute_set_layout, 0, sizeof (vulkan_globals.lightmap_compute_set_layout));
		vulkan_globals.lightmap_compute_set_layout.num_storage_images = 1;
		vulkan_globals.lightmap_compute_set_layout.num_sampled_images = 1 + MAXLIGHTMAPS * 3 / 4;
		vulkan_globals.lightmap_compute_set_layout.num_storage_buffers = 3;
		vulkan_globals.lightmap_compute_set_layout.num_ubos_dynamic = 2;

		err = vkCreateDescriptorSetLayout (vulkan_globals.device, &descriptor_set_layout_create_info, NULL, &vulkan_globals.lightmap_compute_set_layout.handle);
		if (err != VK_SUCCESS)
			Sys_Error ("vkCreateDescriptorSetLayout failed");
		GL_SetObjectName ((uint64_t)vulkan_globals.lightmap_compute_set_layout.handle, VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, "lightmap compute");

		if (vulkan_globals.ray_query)
		{
			lightmap_compute_layout_bindings[8].binding = num_descriptors++;
			lightmap_compute_layout_bindings[8].descriptorCount = 1;
			lightmap_compute_layout_bindings[8].descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
			lightmap_compute_layout_bindings[8].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
			descriptor_set_layout_create_info.bindingCount = num_descriptors;

			vulkan_globals.lightmap_compute_rt_set_layout.num_storage_images = 1;
			vulkan_globals.lightmap_compute_rt_set_layout.num_sampled_images = 1 + MAXLIGHTMAPS * 3 / 4;
			vulkan_globals.lightmap_compute_rt_set_layout.num_storage_buffers = 3;
			vulkan_globals.lightmap_compute_rt_set_layout.num_ubos_dynamic = 2;
			vulkan_globals.lightmap_compute_rt_set_layout.num_acceleration_structures = 1;

			err = vkCreateDescriptorSetLayout (
				vulkan_globals.device, &descriptor_set_layout_create_info, NULL, &vulkan_globals.lightmap_compute_rt_set_layout.handle);
			if (err != VK_SUCCESS)
				Sys_Error ("vkCreateDescriptorSetLayout failed");
			GL_SetObjectName ((uint64_t)vulkan_globals.lightmap_compute_rt_set_layout.handle, VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, "lightmap compute rt");
		}
	}

	{
		ZEROED_STRUCT_ARRAY (VkDescriptorSetLayoutBinding, indirect_compute_layout_bindings, 4);
		indirect_compute_layout_bindings[0].binding = 0;
		indirect_compute_layout_bindings[0].descriptorCount = 1;
		indirect_compute_layout_bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		indirect_compute_layout_bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		indirect_compute_layout_bindings[1].binding = 1;
		indirect_compute_layout_bindings[1].descriptorCount = 1;
		indirect_compute_layout_bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		indirect_compute_layout_bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		indirect_compute_layout_bindings[2].binding = 2;
		indirect_compute_layout_bindings[2].descriptorCount = 1;
		indirect_compute_layout_bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		indirect_compute_layout_bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		indirect_compute_layout_bindings[3].binding = 3;
		indirect_compute_layout_bindings[3].descriptorCount = 1;
		indirect_compute_layout_bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		indirect_compute_layout_bindings[3].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

		descriptor_set_layout_create_info.bindingCount = countof (indirect_compute_layout_bindings);
		descriptor_set_layout_create_info.pBindings = indirect_compute_layout_bindings;

		memset (&vulkan_globals.indirect_compute_set_layout, 0, sizeof (vulkan_globals.indirect_compute_set_layout));
		vulkan_globals.indirect_compute_set_layout.num_storage_buffers = 4;

		err = vkCreateDescriptorSetLayout (vulkan_globals.device, &descriptor_set_layout_create_info, NULL, &vulkan_globals.indirect_compute_set_layout.handle);
		if (err != VK_SUCCESS)
			Sys_Error ("vkCreateDescriptorSetLayout failed");
		GL_SetObjectName ((uint64_t)vulkan_globals.indirect_compute_set_layout.handle, VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, "indirect compute");
	}

#if defined(_DEBUG)
	if (vulkan_globals.ray_query)
	{
		ZEROED_STRUCT_ARRAY (VkDescriptorSetLayoutBinding, ray_debug_layout_bindings, 2);
		ray_debug_layout_bindings[0].binding = 0;
		ray_debug_layout_bindings[0].descriptorCount = 1;
		ray_debug_layout_bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		ray_debug_layout_bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		ray_debug_layout_bindings[1].binding = 1;
		ray_debug_layout_bindings[1].descriptorCount = 1;
		ray_debug_layout_bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
		ray_debug_layout_bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

		descriptor_set_layout_create_info.bindingCount = countof (ray_debug_layout_bindings);
		descriptor_set_layout_create_info.pBindings = ray_debug_layout_bindings;

		memset (&vulkan_globals.ray_debug_set_layout, 0, sizeof (vulkan_globals.ray_debug_set_layout));
		vulkan_globals.ray_debug_set_layout.num_storage_images = 1;

		err = vkCreateDescriptorSetLayout (vulkan_globals.device, &descriptor_set_layout_create_info, NULL, &vulkan_globals.ray_debug_set_layout.handle);
		if (err != VK_SUCCESS)
			Sys_Error ("vkCreateDescriptorSetLayout failed");
		GL_SetObjectName ((uint64_t)vulkan_globals.screen_effects_set_layout.handle, VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, "ray debug");
	}
#endif
}

/*
===============
R_CreateDescriptorPool
===============
*/
void R_CreateDescriptorPool ()
{
	ZEROED_STRUCT_ARRAY (VkDescriptorPoolSize, pool_sizes, 9);
	pool_sizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	pool_sizes[0].descriptorCount = 32 + (MAX_SANITY_LIGHTMAPS * 2) + (MAX_GLTEXTURES + 1);
	pool_sizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	pool_sizes[1].descriptorCount = 32 + MAX_GLTEXTURES + MAX_SANITY_LIGHTMAPS;
	pool_sizes[2].type = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
	pool_sizes[2].descriptorCount = 32;
	pool_sizes[3].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	pool_sizes[3].descriptorCount = 32;
	pool_sizes[4].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	pool_sizes[4].descriptorCount = 32 + MAX_SANITY_LIGHTMAPS * 2;
	pool_sizes[5].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
	pool_sizes[5].descriptorCount = 32 + (MAX_SANITY_LIGHTMAPS * 2);
	pool_sizes[6].type = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
	pool_sizes[6].descriptorCount = 32;
	pool_sizes[7].type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
	pool_sizes[7].descriptorCount = 32;
	int num_sizes = 8;
	if (vulkan_globals.ray_query)
	{
		pool_sizes[8].type = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
		pool_sizes[8].descriptorCount = 32 + MAX_SANITY_LIGHTMAPS;
		num_sizes = 9;
	}

	ZEROED_STRUCT (VkDescriptorPoolCreateInfo, descriptor_pool_create_info);
	descriptor_pool_create_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	descriptor_pool_create_info.maxSets = MAX_GLTEXTURES + MAX_SANITY_LIGHTMAPS + 128;
	descriptor_pool_create_info.poolSizeCount = num_sizes;
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

	{
		// Basic
		VkDescriptorSetLayout basic_descriptor_set_layouts[1] = {vulkan_globals.single_texture_set_layout.handle};

		ZEROED_STRUCT (VkPushConstantRange, push_constant_range);
		push_constant_range.offset = 0;
		push_constant_range.size = 21 * sizeof (float);
		push_constant_range.stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS;

		ZEROED_STRUCT (VkPipelineLayoutCreateInfo, pipeline_layout_create_info);
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
	}

	{
		// World
		VkDescriptorSetLayout world_descriptor_set_layouts[3] = {
			vulkan_globals.single_texture_set_layout.handle, vulkan_globals.single_texture_set_layout.handle, vulkan_globals.single_texture_set_layout.handle};

		ZEROED_STRUCT (VkPushConstantRange, push_constant_range);
		push_constant_range.offset = 0;
		push_constant_range.size = 21 * sizeof (float);
		push_constant_range.stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS;

		ZEROED_STRUCT (VkPipelineLayoutCreateInfo, pipeline_layout_create_info);
		pipeline_layout_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		pipeline_layout_create_info.setLayoutCount = 3;
		pipeline_layout_create_info.pSetLayouts = world_descriptor_set_layouts;
		pipeline_layout_create_info.pushConstantRangeCount = 1;
		pipeline_layout_create_info.pPushConstantRanges = &push_constant_range;

		err = vkCreatePipelineLayout (vulkan_globals.device, &pipeline_layout_create_info, NULL, &vulkan_globals.world_pipeline_layout.handle);
		if (err != VK_SUCCESS)
			Sys_Error ("vkCreatePipelineLayout failed");
		GL_SetObjectName ((uint64_t)vulkan_globals.world_pipeline_layout.handle, VK_OBJECT_TYPE_PIPELINE_LAYOUT, "world_pipeline_layout");
		vulkan_globals.world_pipeline_layout.push_constant_range = push_constant_range;
	}

	{
		// Alias
		VkDescriptorSetLayout alias_descriptor_set_layouts[3] = {
			vulkan_globals.single_texture_set_layout.handle, vulkan_globals.single_texture_set_layout.handle, vulkan_globals.ubo_set_layout.handle};

		ZEROED_STRUCT (VkPushConstantRange, push_constant_range);
		push_constant_range.offset = 0;
		push_constant_range.size = 21 * sizeof (float);
		push_constant_range.stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS;

		ZEROED_STRUCT (VkPipelineLayoutCreateInfo, pipeline_layout_create_info);
		pipeline_layout_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		pipeline_layout_create_info.setLayoutCount = 3;
		pipeline_layout_create_info.pSetLayouts = alias_descriptor_set_layouts;
		pipeline_layout_create_info.pushConstantRangeCount = 1;
		pipeline_layout_create_info.pPushConstantRanges = &push_constant_range;

		err = vkCreatePipelineLayout (vulkan_globals.device, &pipeline_layout_create_info, NULL, &vulkan_globals.alias_pipelines[0].layout.handle);
		if (err != VK_SUCCESS)
			Sys_Error ("vkCreatePipelineLayout failed");
		GL_SetObjectName ((uint64_t)vulkan_globals.alias_pipelines[0].layout.handle, VK_OBJECT_TYPE_PIPELINE_LAYOUT, "alias_pipeline_layout");
		vulkan_globals.alias_pipelines[0].layout.push_constant_range = push_constant_range;
	}

	{
		// MD5
		VkDescriptorSetLayout md5_descriptor_set_layouts[4] = {
			vulkan_globals.single_texture_set_layout.handle, vulkan_globals.single_texture_set_layout.handle, vulkan_globals.ubo_set_layout.handle,
			vulkan_globals.joints_buffer_set_layout.handle};

		ZEROED_STRUCT (VkPushConstantRange, push_constant_range);
		push_constant_range.offset = 0;
		push_constant_range.size = 21 * sizeof (float);
		push_constant_range.stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS;

		ZEROED_STRUCT (VkPipelineLayoutCreateInfo, pipeline_layout_create_info);
		pipeline_layout_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		pipeline_layout_create_info.setLayoutCount = 4;
		pipeline_layout_create_info.pSetLayouts = md5_descriptor_set_layouts;
		pipeline_layout_create_info.pushConstantRangeCount = 1;
		pipeline_layout_create_info.pPushConstantRanges = &push_constant_range;

		err = vkCreatePipelineLayout (vulkan_globals.device, &pipeline_layout_create_info, NULL, &vulkan_globals.md5_pipelines[0].layout.handle);
		if (err != VK_SUCCESS)
			Sys_Error ("vkCreatePipelineLayout failed");
		GL_SetObjectName ((uint64_t)vulkan_globals.md5_pipelines[0].layout.handle, VK_OBJECT_TYPE_PIPELINE_LAYOUT, "md5_pipeline_layout");
		vulkan_globals.md5_pipelines[0].layout.push_constant_range = push_constant_range;
	}

	{
		// Sky
		VkDescriptorSetLayout sky_layer_descriptor_set_layouts[2] = {
			vulkan_globals.single_texture_set_layout.handle,
			vulkan_globals.single_texture_set_layout.handle,
		};

		ZEROED_STRUCT (VkPushConstantRange, push_constant_range);
		push_constant_range.offset = 0;
		push_constant_range.size = 23 * sizeof (float);
		push_constant_range.stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS;

		ZEROED_STRUCT (VkPipelineLayoutCreateInfo, pipeline_layout_create_info);
		pipeline_layout_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		pipeline_layout_create_info.setLayoutCount = 1;
		pipeline_layout_create_info.pSetLayouts = sky_layer_descriptor_set_layouts;
		pipeline_layout_create_info.pushConstantRangeCount = 1;
		pipeline_layout_create_info.pPushConstantRanges = &push_constant_range;

		err = vkCreatePipelineLayout (vulkan_globals.device, &pipeline_layout_create_info, NULL, &vulkan_globals.sky_pipeline_layout[0].handle);
		if (err != VK_SUCCESS)
			Sys_Error ("vkCreatePipelineLayout failed");
		GL_SetObjectName ((uint64_t)vulkan_globals.sky_pipeline_layout[0].handle, VK_OBJECT_TYPE_PIPELINE_LAYOUT, "sky_pipeline_layout");
		vulkan_globals.sky_pipeline_layout[0].push_constant_range = push_constant_range;

		push_constant_range.size = 25 * sizeof (float);
		pipeline_layout_create_info.setLayoutCount = 2;

		err = vkCreatePipelineLayout (vulkan_globals.device, &pipeline_layout_create_info, NULL, &vulkan_globals.sky_pipeline_layout[1].handle);
		if (err != VK_SUCCESS)
			Sys_Error ("vkCreatePipelineLayout failed");
		GL_SetObjectName ((uint64_t)vulkan_globals.sky_pipeline_layout[1].handle, VK_OBJECT_TYPE_PIPELINE_LAYOUT, "sky_layer_pipeline_layout");
		vulkan_globals.sky_pipeline_layout[1].push_constant_range = push_constant_range;
	}

	{
		// Postprocess
		VkDescriptorSetLayout postprocess_descriptor_set_layouts[1] = {
			vulkan_globals.input_attachment_set_layout.handle,
		};

		ZEROED_STRUCT (VkPushConstantRange, push_constant_range);
		push_constant_range.offset = 0;
		push_constant_range.size = 2 * sizeof (float);
		push_constant_range.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

		ZEROED_STRUCT (VkPipelineLayoutCreateInfo, pipeline_layout_create_info);
		pipeline_layout_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		pipeline_layout_create_info.setLayoutCount = 1;
		pipeline_layout_create_info.pSetLayouts = postprocess_descriptor_set_layouts;
		pipeline_layout_create_info.pushConstantRangeCount = 1;
		pipeline_layout_create_info.pPushConstantRanges = &push_constant_range;

		err = vkCreatePipelineLayout (vulkan_globals.device, &pipeline_layout_create_info, NULL, &vulkan_globals.postprocess_pipeline.layout.handle);
		if (err != VK_SUCCESS)
			Sys_Error ("vkCreatePipelineLayout failed");
		GL_SetObjectName ((uint64_t)vulkan_globals.postprocess_pipeline.layout.handle, VK_OBJECT_TYPE_PIPELINE_LAYOUT, "postprocess_pipeline_layout");
		vulkan_globals.postprocess_pipeline.layout.push_constant_range = push_constant_range;
	}

	{
		// Screen effects
		VkDescriptorSetLayout screen_effects_descriptor_set_layouts[1] = {
			vulkan_globals.screen_effects_set_layout.handle,
		};

		ZEROED_STRUCT (VkPushConstantRange, push_constant_range);
		push_constant_range.offset = 0;
		push_constant_range.size = 3 * sizeof (uint32_t) + 8 * sizeof (float);
		push_constant_range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

		ZEROED_STRUCT (VkPipelineLayoutCreateInfo, pipeline_layout_create_info);
		pipeline_layout_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
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
	}

	{
		// Texture warp
		VkDescriptorSetLayout tex_warp_descriptor_set_layouts[2] = {
			vulkan_globals.single_texture_set_layout.handle,
			vulkan_globals.single_texture_cs_write_set_layout.handle,
		};

		ZEROED_STRUCT (VkPushConstantRange, push_constant_range);
		push_constant_range.offset = 0;
		push_constant_range.size = 1 * sizeof (float);
		push_constant_range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

		ZEROED_STRUCT (VkPipelineLayoutCreateInfo, pipeline_layout_create_info);
		pipeline_layout_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		pipeline_layout_create_info.setLayoutCount = 2;
		pipeline_layout_create_info.pSetLayouts = tex_warp_descriptor_set_layouts;
		pipeline_layout_create_info.pushConstantRangeCount = 1;
		pipeline_layout_create_info.pPushConstantRanges = &push_constant_range;

		err = vkCreatePipelineLayout (vulkan_globals.device, &pipeline_layout_create_info, NULL, &vulkan_globals.cs_tex_warp_pipeline.layout.handle);
		if (err != VK_SUCCESS)
			Sys_Error ("vkCreatePipelineLayout failed");
		GL_SetObjectName ((uint64_t)vulkan_globals.cs_tex_warp_pipeline.layout.handle, VK_OBJECT_TYPE_PIPELINE_LAYOUT, "cs_tex_warp_pipeline_layout");
		vulkan_globals.cs_tex_warp_pipeline.layout.push_constant_range = push_constant_range;
	}

	{
		// Show triangles
		ZEROED_STRUCT (VkPushConstantRange, push_constant_range);

		ZEROED_STRUCT (VkPipelineLayoutCreateInfo, pipeline_layout_create_info);
		pipeline_layout_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		pipeline_layout_create_info.setLayoutCount = 0;
		pipeline_layout_create_info.pushConstantRangeCount = 0;

		err = vkCreatePipelineLayout (vulkan_globals.device, &pipeline_layout_create_info, NULL, &vulkan_globals.showtris_pipeline.layout.handle);
		if (err != VK_SUCCESS)
			Sys_Error ("vkCreatePipelineLayout failed");
		GL_SetObjectName ((uint64_t)vulkan_globals.showtris_pipeline.layout.handle, VK_OBJECT_TYPE_PIPELINE_LAYOUT, "showtris_pipeline_layout");
		vulkan_globals.showtris_pipeline.layout.push_constant_range = push_constant_range;
	}

	{
		// Update lightmaps
		VkDescriptorSetLayout update_lightmap_descriptor_set_layouts[1] = {
			vulkan_globals.lightmap_compute_set_layout.handle,
		};

		ZEROED_STRUCT (VkPushConstantRange, push_constant_range);
		push_constant_range.offset = 0;
		push_constant_range.size = 6 * sizeof (uint32_t);
		push_constant_range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

		ZEROED_STRUCT (VkPipelineLayoutCreateInfo, pipeline_layout_create_info);
		pipeline_layout_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
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

	if (vulkan_globals.ray_query)
	{
		// Update lightmaps RT
		VkDescriptorSetLayout update_lightmap_rt_descriptor_set_layouts[1] = {
			vulkan_globals.lightmap_compute_rt_set_layout.handle,
		};

		ZEROED_STRUCT (VkPushConstantRange, push_constant_range);
		push_constant_range.offset = 0;
		push_constant_range.size = 6 * sizeof (uint32_t);
		push_constant_range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

		ZEROED_STRUCT (VkPipelineLayoutCreateInfo, pipeline_layout_create_info);
		pipeline_layout_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		pipeline_layout_create_info.setLayoutCount = 1;
		pipeline_layout_create_info.pSetLayouts = update_lightmap_rt_descriptor_set_layouts;
		pipeline_layout_create_info.pushConstantRangeCount = 1;
		pipeline_layout_create_info.pPushConstantRanges = &push_constant_range;

		err = vkCreatePipelineLayout (vulkan_globals.device, &pipeline_layout_create_info, NULL, &vulkan_globals.update_lightmap_rt_pipeline.layout.handle);
		if (err != VK_SUCCESS)
			Sys_Error ("vkCreatePipelineLayout failed");
		GL_SetObjectName (
			(uint64_t)vulkan_globals.update_lightmap_rt_pipeline.layout.handle, VK_OBJECT_TYPE_PIPELINE_LAYOUT, "update_lightmap_rt_pipeline_layout");
		vulkan_globals.update_lightmap_rt_pipeline.layout.push_constant_range = push_constant_range;
	}

	{
		// Indirect draw
		VkDescriptorSetLayout indirect_draw_descriptor_set_layouts[1] = {
			vulkan_globals.indirect_compute_set_layout.handle,
		};

		ZEROED_STRUCT (VkPushConstantRange, push_constant_range);
		push_constant_range.offset = 0;
		push_constant_range.size = 6 * sizeof (uint32_t);
		push_constant_range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

		ZEROED_STRUCT (VkPipelineLayoutCreateInfo, pipeline_layout_create_info);
		pipeline_layout_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		pipeline_layout_create_info.setLayoutCount = 1;
		pipeline_layout_create_info.pSetLayouts = indirect_draw_descriptor_set_layouts;
		pipeline_layout_create_info.pushConstantRangeCount = 1;
		pipeline_layout_create_info.pPushConstantRanges = &push_constant_range;

		err = vkCreatePipelineLayout (vulkan_globals.device, &pipeline_layout_create_info, NULL, &vulkan_globals.indirect_draw_pipeline.layout.handle);
		if (err != VK_SUCCESS)
			Sys_Error ("vkCreatePipelineLayout failed");
		GL_SetObjectName ((uint64_t)vulkan_globals.indirect_draw_pipeline.layout.handle, VK_OBJECT_TYPE_PIPELINE_LAYOUT, "indirect_draw_pipeline_layout");
		vulkan_globals.indirect_draw_pipeline.layout.push_constant_range = push_constant_range;

		err = vkCreatePipelineLayout (vulkan_globals.device, &pipeline_layout_create_info, NULL, &vulkan_globals.indirect_clear_pipeline.layout.handle);
		if (err != VK_SUCCESS)
			Sys_Error ("vkCreatePipelineLayout failed");
		GL_SetObjectName ((uint64_t)vulkan_globals.indirect_clear_pipeline.layout.handle, VK_OBJECT_TYPE_PIPELINE_LAYOUT, "indirect_clear_pipeline_layout");
		vulkan_globals.indirect_clear_pipeline.layout.push_constant_range = push_constant_range;
	}

#if defined(_DEBUG)
	if (vulkan_globals.ray_query)
	{
		// Ray debug
		VkDescriptorSetLayout ray_debug_descriptor_set_layouts[1] = {
			vulkan_globals.ray_debug_set_layout.handle,
		};

		ZEROED_STRUCT (VkPushConstantRange, push_constant_range);
		push_constant_range.offset = 0;
		push_constant_range.size = 15 * sizeof (float);
		push_constant_range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

		ZEROED_STRUCT (VkPipelineLayoutCreateInfo, pipeline_layout_create_info);
		pipeline_layout_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		pipeline_layout_create_info.setLayoutCount = 1;
		pipeline_layout_create_info.pSetLayouts = ray_debug_descriptor_set_layouts;
		pipeline_layout_create_info.pushConstantRangeCount = 1;
		pipeline_layout_create_info.pPushConstantRanges = &push_constant_range;

		err = vkCreatePipelineLayout (vulkan_globals.device, &pipeline_layout_create_info, NULL, &vulkan_globals.ray_debug_pipeline.layout.handle);
		if (err != VK_SUCCESS)
			Sys_Error ("vkCreatePipelineLayout failed");
		GL_SetObjectName ((uint64_t)vulkan_globals.ray_debug_pipeline.layout.handle, VK_OBJECT_TYPE_PIPELINE_LAYOUT, "ray_debug_pipeline_layout");
		vulkan_globals.ray_debug_pipeline.layout.push_constant_range = push_constant_range;
	}
#endif
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
		ZEROED_STRUCT (VkSamplerCreateInfo, sampler_create_info);
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

		lod_bias += gl_lodbias.value;

		Sys_Printf ("Texture lod bias: %f\n", lod_bias);

		ZEROED_STRUCT (VkSamplerCreateInfo, sampler_create_info);
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
static VkShaderModule R_CreateShaderModule (const byte *code, const int size, const char *name)
{
	ZEROED_STRUCT (VkShaderModuleCreateInfo, module_create_info);
	module_create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	module_create_info.pNext = NULL;
	module_create_info.codeSize = size;
	module_create_info.pCode = (const uint32_t *)code;

	VkShaderModule module;
	VkResult	   err = vkCreateShaderModule (vulkan_globals.device, &module_create_info, NULL, &module);
	if (err != VK_SUCCESS)
		Sys_Error ("vkCreateShaderModule failed");

	GL_SetObjectName ((uint64_t)module, VK_OBJECT_TYPE_SHADER_MODULE, name);

	return module;
}

typedef struct pipeline_create_infos_s
{
	VkPipelineShaderStageCreateInfo		   shader_stages[2];
	VkPipelineDynamicStateCreateInfo	   dynamic_state;
	VkDynamicState						   dynamic_states[3];
	VkPipelineVertexInputStateCreateInfo   vertex_input_state;
	VkPipelineInputAssemblyStateCreateInfo input_assembly_state;
	VkPipelineViewportStateCreateInfo	   viewport_state;
	VkPipelineRasterizationStateCreateInfo rasterization_state;
	VkPipelineMultisampleStateCreateInfo   multisample_state;
	VkPipelineDepthStencilStateCreateInfo  depth_stencil_state;
	VkPipelineColorBlendStateCreateInfo	   color_blend_state;
	VkPipelineColorBlendAttachmentState	   blend_attachment_state;
	VkGraphicsPipelineCreateInfo		   graphics_pipeline;
	VkComputePipelineCreateInfo			   compute_pipeline;
} pipeline_create_infos_t;

static VkVertexInputAttributeDescription basic_vertex_input_attribute_descriptions[3];
static VkVertexInputBindingDescription	 basic_vertex_binding_description;
static VkVertexInputAttributeDescription world_vertex_input_attribute_descriptions[3];
static VkVertexInputBindingDescription	 world_vertex_binding_description;
static VkVertexInputAttributeDescription alias_vertex_input_attribute_descriptions[5];
static VkVertexInputBindingDescription	 alias_vertex_binding_descriptions[3];
static VkVertexInputAttributeDescription md5_vertex_input_attribute_descriptions[5];
static VkVertexInputBindingDescription	 md5_vertex_binding_description;

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
DECLARE_SHADER_MODULE (md5_vert);
DECLARE_SHADER_MODULE (sky_layer_vert);
DECLARE_SHADER_MODULE (sky_layer_frag);
DECLARE_SHADER_MODULE (sky_box_frag);
DECLARE_SHADER_MODULE (sky_cube_vert);
DECLARE_SHADER_MODULE (sky_cube_frag);
DECLARE_SHADER_MODULE (postprocess_vert);
DECLARE_SHADER_MODULE (postprocess_frag);
DECLARE_SHADER_MODULE (screen_effects_8bit_comp);
DECLARE_SHADER_MODULE (screen_effects_8bit_scale_comp);
DECLARE_SHADER_MODULE (screen_effects_8bit_scale_sops_comp);
DECLARE_SHADER_MODULE (screen_effects_10bit_comp);
DECLARE_SHADER_MODULE (screen_effects_10bit_scale_comp);
DECLARE_SHADER_MODULE (screen_effects_10bit_scale_sops_comp);
DECLARE_SHADER_MODULE (cs_tex_warp_comp);
DECLARE_SHADER_MODULE (indirect_comp);
DECLARE_SHADER_MODULE (indirect_clear_comp);
DECLARE_SHADER_MODULE (showtris_vert);
DECLARE_SHADER_MODULE (showtris_frag);
DECLARE_SHADER_MODULE (update_lightmap_comp);
DECLARE_SHADER_MODULE (update_lightmap_rt_comp);
DECLARE_SHADER_MODULE (ray_debug_comp);

/*
===============
R_InitVertexAttributes
===============
*/
static void R_InitVertexAttributes ()
{
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
	}

	{
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
	}

	{
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

	{
		// Matches md5vert_t
		md5_vertex_input_attribute_descriptions[0].binding = 0;
		md5_vertex_input_attribute_descriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
		md5_vertex_input_attribute_descriptions[0].location = 0;
		md5_vertex_input_attribute_descriptions[0].offset = 0;
		md5_vertex_input_attribute_descriptions[1].binding = 0;
		md5_vertex_input_attribute_descriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
		md5_vertex_input_attribute_descriptions[1].location = 1;
		md5_vertex_input_attribute_descriptions[1].offset = 12;
		md5_vertex_input_attribute_descriptions[2].binding = 0;
		md5_vertex_input_attribute_descriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
		md5_vertex_input_attribute_descriptions[2].location = 2;
		md5_vertex_input_attribute_descriptions[2].offset = 24;
		md5_vertex_input_attribute_descriptions[3].binding = 0;
		md5_vertex_input_attribute_descriptions[3].format = VK_FORMAT_R8G8B8A8_UNORM;
		md5_vertex_input_attribute_descriptions[3].location = 3;
		md5_vertex_input_attribute_descriptions[3].offset = 32;
		md5_vertex_input_attribute_descriptions[4].binding = 0;
		md5_vertex_input_attribute_descriptions[4].format = VK_FORMAT_R8G8B8A8_UINT;
		md5_vertex_input_attribute_descriptions[4].location = 4;
		md5_vertex_input_attribute_descriptions[4].offset = 36;

		md5_vertex_binding_description.binding = 0;
		md5_vertex_binding_description.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
		md5_vertex_binding_description.stride = 40;
	}
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
	infos->graphics_pipeline.renderPass = vulkan_globals.secondary_cb_contexts[SCBX_WORLD][0].render_pass;
}

/*
===============
R_CreateBasicPipelines
===============
*/
static void R_CreateBasicPipelines ()
{
	int						render_pass;
	VkResult				err;
	pipeline_create_infos_t infos;
	R_InitDefaultStates (&infos);

	VkRenderPass main_render_pass = vulkan_globals.secondary_cb_contexts[SCBX_WORLD][0].render_pass;
	VkRenderPass ui_render_pass = vulkan_globals.secondary_cb_contexts[SCBX_GUI]->render_pass;

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
	VkResult				err;
	pipeline_create_infos_t infos;
	R_InitDefaultStates (&infos);

	infos.multisample_state.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	infos.input_assembly_state.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
	infos.rasterization_state.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

	infos.blend_attachment_state.blendEnable = VK_FALSE;

	infos.shader_stages[0].module = basic_vert_module;
	infos.shader_stages[1].module = basic_frag_module;

	infos.graphics_pipeline.renderPass = vulkan_globals.warp_render_pass;

	assert (vulkan_globals.raster_tex_warp_pipeline.handle == VK_NULL_HANDLE);
	err = vkCreateGraphicsPipelines (vulkan_globals.device, VK_NULL_HANDLE, 1, &infos.graphics_pipeline, NULL, &vulkan_globals.raster_tex_warp_pipeline.handle);
	if (err != VK_SUCCESS)
		Sys_Error ("vkCreateGraphicsPipelines failed (raster_tex_warp_pipeline)");
	vulkan_globals.raster_tex_warp_pipeline.layout = vulkan_globals.basic_pipeline_layout;
	GL_SetObjectName ((uint64_t)vulkan_globals.raster_tex_warp_pipeline.handle, VK_OBJECT_TYPE_PIPELINE, "warp");

	ZEROED_STRUCT (VkPipelineShaderStageCreateInfo, compute_shader_stage);
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
		Sys_Error ("vkCreateComputePipelines failed (cs_tex_warp_pipeline)");
	GL_SetObjectName ((uint64_t)vulkan_globals.cs_tex_warp_pipeline.handle, VK_OBJECT_TYPE_PIPELINE, "cs_tex_warp");
}

/*
===============
R_CreateParticlesPipelines
===============
*/
static void R_CreateParticlesPipelines ()
{
	VkResult				err;
	pipeline_create_infos_t infos;
	R_InitDefaultStates (&infos);

	infos.depth_stencil_state.depthTestEnable = VK_TRUE;
	infos.depth_stencil_state.depthWriteEnable = VK_FALSE;

	infos.graphics_pipeline.renderPass = vulkan_globals.secondary_cb_contexts[SCBX_WORLD][0].render_pass;

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
R_CreateRayDebugPipelines
===============
*/
static void R_CreateRayDebugPipelines ()
{
#if defined(_DEBUG)
	if (!vulkan_globals.ray_query)
		return;

	VkResult				err;
	pipeline_create_infos_t infos;
	R_InitDefaultStates (&infos);

	ZEROED_STRUCT (VkPipelineShaderStageCreateInfo, compute_shader_stage);
	compute_shader_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	compute_shader_stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	compute_shader_stage.module = ray_debug_comp_module;
	compute_shader_stage.pName = "main";

	memset (&infos.compute_pipeline, 0, sizeof (infos.compute_pipeline));
	infos.compute_pipeline.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
	infos.compute_pipeline.stage = compute_shader_stage;
	infos.compute_pipeline.layout = vulkan_globals.ray_debug_pipeline.layout.handle;

	assert (vulkan_globals.ray_debug_pipeline.handle == VK_NULL_HANDLE);
	err = vkCreateComputePipelines (vulkan_globals.device, VK_NULL_HANDLE, 1, &infos.compute_pipeline, NULL, &vulkan_globals.ray_debug_pipeline.handle);
	if (err != VK_SUCCESS)
		Sys_Error ("vkCreateComputePipelines failed (ray_debug_pipeline)");
	GL_SetObjectName ((uint64_t)vulkan_globals.ray_debug_pipeline.handle, VK_OBJECT_TYPE_PIPELINE, "ray_debug_pipeline");
#endif
}

/*
===============
R_CreateFTEParticlesPipelines
===============
*/
static void R_CreateFTEParticlesPipelines ()
{
#ifdef PSET_SCRIPT
	VkResult				err;
	pipeline_create_infos_t infos;
	R_InitDefaultStates (&infos);

	static const VkBlendFactor source_blend_factors[8] = {
		VK_BLEND_FACTOR_SRC_ALPHA, // BM_BLEND
		VK_BLEND_FACTOR_SRC_COLOR, // BM_BLENDCOLOUR
		VK_BLEND_FACTOR_SRC_ALPHA, // BM_ADDA
		VK_BLEND_FACTOR_SRC_COLOR, // BM_ADDC
		VK_BLEND_FACTOR_SRC_ALPHA, // BM_SUBTRACT
		VK_BLEND_FACTOR_ZERO,	   // BM_INVMODA
		VK_BLEND_FACTOR_ZERO,	   // BM_INVMODC
		VK_BLEND_FACTOR_ONE		   // BM_PREMUL
	};
	static const VkBlendFactor dest_blend_factors[8] = {
		VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, // BM_BLEND
		VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR, // BM_BLENDCOLOUR
		VK_BLEND_FACTOR_ONE,				 // BM_ADDA
		VK_BLEND_FACTOR_ONE,				 // BM_ADDC
		VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR, // BM_SUBTRACT
		VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, // BM_INVMODA
		VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR, // BM_INVMODC
		VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA	 // BM_PREMUL
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
	infos.graphics_pipeline.renderPass = vulkan_globals.secondary_cb_contexts[SCBX_WORLD][0].render_pass;
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
			Sys_Error ("vkCreateGraphicsPipelines failed (fte_particle_pipelines[%d]", i);
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
				Sys_Error ("vkCreateGraphicsPipelines failed (vulkan_globals.fte_particle_pipelines[%d])", i + 8);
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
	VkResult				err;
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
		Sys_Error ("vkCreateGraphicsPipelines failed (sprite_pipeline)");
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
	for (int i = 0; i < 2; i++)
	{
		VkResult				err;
		pipeline_create_infos_t infos;
		R_InitDefaultStates (&infos);

		if (i) // indirect vertex buffer has stride 28 (world) instead of 24 (basic)
		{
			infos.vertex_input_state.vertexAttributeDescriptionCount = 3;
			infos.vertex_input_state.pVertexAttributeDescriptions = world_vertex_input_attribute_descriptions;
			infos.vertex_input_state.vertexBindingDescriptionCount = 1;
			infos.vertex_input_state.pVertexBindingDescriptions = &world_vertex_binding_description;
		}

		infos.graphics_pipeline.layout = vulkan_globals.sky_pipeline_layout[0].handle;

		infos.graphics_pipeline.renderPass = vulkan_globals.secondary_cb_contexts[SCBX_WORLD][0].render_pass;

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

		assert (vulkan_globals.sky_stencil_pipeline[i].handle == VK_NULL_HANDLE);
		err = vkCreateGraphicsPipelines (
			vulkan_globals.device, VK_NULL_HANDLE, 1, &infos.graphics_pipeline, NULL, &vulkan_globals.sky_stencil_pipeline[i].handle);
		if (err != VK_SUCCESS)
			Sys_Error ("vkCreateGraphicsPipelines failed (sky_stencil_pipeline)");
		vulkan_globals.sky_stencil_pipeline[i].layout = vulkan_globals.sky_pipeline_layout[0];
		GL_SetObjectName ((uint64_t)vulkan_globals.sky_stencil_pipeline[i].handle, VK_OBJECT_TYPE_PIPELINE, i ? "sky_stencil_indirect" : "sky_stencil");

		infos.depth_stencil_state.stencilTestEnable = VK_FALSE;
		infos.blend_attachment_state.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
		infos.graphics_pipeline.stageCount = 2;

		infos.shader_stages[1].module = basic_notex_frag_module;

		assert (vulkan_globals.sky_color_pipeline[i].handle == VK_NULL_HANDLE);
		err =
			vkCreateGraphicsPipelines (vulkan_globals.device, VK_NULL_HANDLE, 1, &infos.graphics_pipeline, NULL, &vulkan_globals.sky_color_pipeline[i].handle);
		if (err != VK_SUCCESS)
			Sys_Error ("vkCreateGraphicsPipelines failed (sky_color_pipeline)");
		vulkan_globals.sky_color_pipeline[i].layout = vulkan_globals.sky_pipeline_layout[0];
		GL_SetObjectName ((uint64_t)vulkan_globals.sky_color_pipeline[i].handle, VK_OBJECT_TYPE_PIPELINE, i ? "sky_color_indirect" : "sky_color");

		infos.shader_stages[0].module = sky_cube_vert_module;
		infos.shader_stages[1].module = sky_cube_frag_module;
		assert (vulkan_globals.sky_cube_pipeline[i].handle == VK_NULL_HANDLE);
		err = vkCreateGraphicsPipelines (vulkan_globals.device, VK_NULL_HANDLE, 1, &infos.graphics_pipeline, NULL, &vulkan_globals.sky_cube_pipeline[i].handle);
		if (err != VK_SUCCESS)
			Sys_Error ("vkCreateGraphicsPipelines failed (sky_cube_pipeline)");
		GL_SetObjectName ((uint64_t)vulkan_globals.sky_cube_pipeline[i].handle, VK_OBJECT_TYPE_PIPELINE, i ? "sky_cube_indirect" : "sky_cube");
		vulkan_globals.sky_cube_pipeline[i].layout = vulkan_globals.sky_pipeline_layout[0];

		infos.shader_stages[0].module = sky_layer_vert_module;
		infos.shader_stages[1].module = sky_layer_frag_module;
		infos.graphics_pipeline.layout = vulkan_globals.sky_pipeline_layout[1].handle;
		assert (vulkan_globals.sky_layer_pipeline[i].handle == VK_NULL_HANDLE);
		err =
			vkCreateGraphicsPipelines (vulkan_globals.device, VK_NULL_HANDLE, 1, &infos.graphics_pipeline, NULL, &vulkan_globals.sky_layer_pipeline[i].handle);
		if (err != VK_SUCCESS)
			Sys_Error ("vkCreateGraphicsPipelines failed (sky_layer_pipeline)");
		GL_SetObjectName ((uint64_t)vulkan_globals.sky_layer_pipeline[i].handle, VK_OBJECT_TYPE_PIPELINE, i ? "sky_layer_indirect" : "sky_layer");
		vulkan_globals.sky_layer_pipeline[i].layout = vulkan_globals.sky_pipeline_layout[1];

		if (i > 0)
			break;

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
		infos.shader_stages[0].module = basic_vert_module;
		infos.shader_stages[1].module = sky_box_frag_module;
		infos.graphics_pipeline.layout = vulkan_globals.sky_pipeline_layout[0].handle;

		assert (vulkan_globals.sky_box_pipeline.handle == VK_NULL_HANDLE);
		err = vkCreateGraphicsPipelines (vulkan_globals.device, VK_NULL_HANDLE, 1, &infos.graphics_pipeline, NULL, &vulkan_globals.sky_box_pipeline.handle);
		if (err != VK_SUCCESS)
			Sys_Error ("vkCreateGraphicsPipelines failed (sky_box_pipeline)");
		GL_SetObjectName ((uint64_t)vulkan_globals.sky_box_pipeline.handle, VK_OBJECT_TYPE_PIPELINE, "sky_box");

		vulkan_globals.sky_box_pipeline.layout = vulkan_globals.sky_pipeline_layout[0];
	}
}

/*
===============
R_CreateShowTrisPipelines
===============
*/
static void R_CreateShowTrisPipelines ()
{
	VkResult				err;
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
			Sys_Error ("vkCreateGraphicsPipelines failed (showtris_pipeline)");
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
			Sys_Error ("vkCreateGraphicsPipelines failed (showtris_depth_test_pipeline)");
		vulkan_globals.showtris_depth_test_pipeline.layout = vulkan_globals.basic_pipeline_layout;

		GL_SetObjectName ((uint64_t)vulkan_globals.showtris_depth_test_pipeline.handle, VK_OBJECT_TYPE_PIPELINE, "showtris_depth_test");

		infos.depth_stencil_state.depthTestEnable = VK_FALSE;
		infos.rasterization_state.depthBiasEnable = VK_FALSE;
		infos.input_assembly_state.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
		assert (vulkan_globals.showbboxes_pipeline.handle == VK_NULL_HANDLE);
		err = vkCreateGraphicsPipelines (vulkan_globals.device, VK_NULL_HANDLE, 1, &infos.graphics_pipeline, NULL, &vulkan_globals.showbboxes_pipeline.handle);
		if (err != VK_SUCCESS)
			Sys_Error ("vkCreateGraphicsPipelines failed (showtris_depth_test)");
		vulkan_globals.showbboxes_pipeline.layout = vulkan_globals.basic_pipeline_layout;

		GL_SetObjectName ((uint64_t)vulkan_globals.showbboxes_pipeline.handle, VK_OBJECT_TYPE_PIPELINE, "showbboxes");

		infos.input_assembly_state.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		infos.vertex_input_state.vertexAttributeDescriptionCount = 3;
		infos.vertex_input_state.pVertexAttributeDescriptions = world_vertex_input_attribute_descriptions;
		infos.vertex_input_state.vertexBindingDescriptionCount = 1;
		infos.vertex_input_state.pVertexBindingDescriptions = &world_vertex_binding_description;

		assert (vulkan_globals.showtris_indirect_pipeline.handle == VK_NULL_HANDLE);
		err = vkCreateGraphicsPipelines (
			vulkan_globals.device, VK_NULL_HANDLE, 1, &infos.graphics_pipeline, NULL, &vulkan_globals.showtris_indirect_pipeline.handle);
		if (err != VK_SUCCESS)
			Sys_Error ("vkCreateGraphicsPipelines failed (showtris_indirect_pipeline)");
		vulkan_globals.showtris_indirect_pipeline.layout = vulkan_globals.basic_pipeline_layout;
		GL_SetObjectName ((uint64_t)vulkan_globals.showtris_indirect_pipeline.handle, VK_OBJECT_TYPE_PIPELINE, "showtris_indirect");

		infos.depth_stencil_state.depthTestEnable = VK_TRUE;
		infos.rasterization_state.depthBiasEnable = VK_TRUE;

		assert (vulkan_globals.showtris_indirect_depth_test_pipeline.handle == VK_NULL_HANDLE);
		err = vkCreateGraphicsPipelines (
			vulkan_globals.device, VK_NULL_HANDLE, 1, &infos.graphics_pipeline, NULL, &vulkan_globals.showtris_indirect_depth_test_pipeline.handle);
		if (err != VK_SUCCESS)
			Sys_Error ("vkCreateGraphicsPipelines failed (showtris_indirect_depth_test_pipeline)");
		vulkan_globals.showtris_indirect_depth_test_pipeline.layout = vulkan_globals.basic_pipeline_layout;
		GL_SetObjectName ((uint64_t)vulkan_globals.showtris_indirect_depth_test_pipeline.handle, VK_OBJECT_TYPE_PIPELINE, "showtris_indirect_depth_test");
	}
}

/*
===============
R_CreateWorldPipelines
===============
*/
static void R_CreateWorldPipelines ()
{
	VkResult				err;
	int						alpha_blend, alpha_test, fullbright_enabled, quantize_lm;
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

	VkSpecializationMapEntry specialization_entries[5];
	specialization_entries[0].constantID = 0;
	specialization_entries[0].offset = 0;
	specialization_entries[0].size = 4;
	specialization_entries[1].constantID = 1;
	specialization_entries[1].offset = 4;
	specialization_entries[1].size = 4;
	specialization_entries[2].constantID = 2;
	specialization_entries[2].offset = 8;
	specialization_entries[2].size = 4;
	specialization_entries[3].constantID = 3;
	specialization_entries[3].offset = 12;
	specialization_entries[3].size = 4;
	specialization_entries[4].constantID = 4;
	specialization_entries[4].offset = 16;
	specialization_entries[4].size = 4;

	uint32_t specialization_data[5];
	specialization_data[0] = 0;
	specialization_data[1] = 0;
	specialization_data[2] = 0;
	specialization_data[3] = 0;
	specialization_data[4] = vulkan_globals.color_format == VK_FORMAT_A2B10G10R10_UNORM_PACK32; // 10-bit lightmap

	VkSpecializationInfo specialization_info;
	specialization_info.mapEntryCount = 5;
	specialization_info.pMapEntries = specialization_entries;
	specialization_info.dataSize = 20;
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
				for (quantize_lm = 0; quantize_lm < 2; ++quantize_lm)
				{
					int pipeline_index = fullbright_enabled + (alpha_test * 2) + (alpha_blend * 4) + (quantize_lm * 8);

					specialization_data[0] = fullbright_enabled;
					specialization_data[1] = alpha_test;
					specialization_data[2] = alpha_blend;
					specialization_data[3] = quantize_lm;

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
						Sys_Error ("vkCreateGraphicsPipelines failed (world_pipelines[%d])", pipeline_index);
					GL_SetObjectName (
						(uint64_t)vulkan_globals.world_pipelines[pipeline_index].handle, VK_OBJECT_TYPE_PIPELINE, va ("world %d", pipeline_index));
					vulkan_globals.world_pipelines[pipeline_index].layout = vulkan_globals.world_pipeline_layout;
				}
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
	VkResult				err;
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

	infos.graphics_pipeline.layout = vulkan_globals.alias_pipelines[0].layout.handle;

	assert (vulkan_globals.alias_pipelines[0].handle == VK_NULL_HANDLE);
	err = vkCreateGraphicsPipelines (vulkan_globals.device, VK_NULL_HANDLE, 1, &infos.graphics_pipeline, NULL, &vulkan_globals.alias_pipelines[0].handle);
	if (err != VK_SUCCESS)
		Sys_Error ("vkCreateGraphicsPipelines failed (alias_pipeline)");
	GL_SetObjectName ((uint64_t)vulkan_globals.alias_pipelines[0].handle, VK_OBJECT_TYPE_PIPELINE, "alias");

	infos.shader_stages[1].module = alias_alphatest_frag_module;

	assert (vulkan_globals.alias_pipelines[1].handle == VK_NULL_HANDLE);
	err = vkCreateGraphicsPipelines (vulkan_globals.device, VK_NULL_HANDLE, 1, &infos.graphics_pipeline, NULL, &vulkan_globals.alias_pipelines[1].handle);
	if (err != VK_SUCCESS)
		Sys_Error ("vkCreateGraphicsPipelines failed (alias_alphatest_pipeline)");
	GL_SetObjectName ((uint64_t)vulkan_globals.alias_pipelines[1].handle, VK_OBJECT_TYPE_PIPELINE, "alias_alphatest");
	vulkan_globals.alias_pipelines[1].layout = vulkan_globals.alias_pipelines[0].layout;

	infos.depth_stencil_state.depthWriteEnable = VK_FALSE;
	infos.blend_attachment_state.blendEnable = VK_TRUE;
	infos.shader_stages[1].module = alias_frag_module;

	assert (vulkan_globals.alias_pipelines[2].handle == VK_NULL_HANDLE);
	err = vkCreateGraphicsPipelines (vulkan_globals.device, VK_NULL_HANDLE, 1, &infos.graphics_pipeline, NULL, &vulkan_globals.alias_pipelines[2].handle);
	if (err != VK_SUCCESS)
		Sys_Error ("vkCreateGraphicsPipelines failed (alias_blend_pipeline)");
	GL_SetObjectName ((uint64_t)vulkan_globals.alias_pipelines[2].handle, VK_OBJECT_TYPE_PIPELINE, "alias_blend");
	vulkan_globals.alias_pipelines[2].layout = vulkan_globals.alias_pipelines[0].layout;

	infos.shader_stages[1].module = alias_alphatest_frag_module;

	assert (vulkan_globals.alias_pipelines[3].handle == VK_NULL_HANDLE);
	err = vkCreateGraphicsPipelines (vulkan_globals.device, VK_NULL_HANDLE, 1, &infos.graphics_pipeline, NULL, &vulkan_globals.alias_pipelines[3].handle);
	if (err != VK_SUCCESS)
		Sys_Error ("vkCreateGraphicsPipelines failed");
	GL_SetObjectName ((uint64_t)vulkan_globals.alias_pipelines[3].handle, VK_OBJECT_TYPE_PIPELINE, "alias_alphatest_blend");
	vulkan_globals.alias_pipelines[3].layout = vulkan_globals.alias_pipelines[0].layout;

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

		infos.graphics_pipeline.layout = vulkan_globals.alias_pipelines[0].layout.handle;

		assert (vulkan_globals.alias_pipelines[4].handle == VK_NULL_HANDLE);
		err = vkCreateGraphicsPipelines (vulkan_globals.device, VK_NULL_HANDLE, 1, &infos.graphics_pipeline, NULL, &vulkan_globals.alias_pipelines[4].handle);
		if (err != VK_SUCCESS)
			Sys_Error ("vkCreateGraphicsPipelines failed");
		GL_SetObjectName ((uint64_t)vulkan_globals.alias_pipelines[4].handle, VK_OBJECT_TYPE_PIPELINE, "alias_showtris");
		vulkan_globals.alias_pipelines[4].layout = vulkan_globals.alias_pipelines[0].layout;

		infos.depth_stencil_state.depthTestEnable = VK_TRUE;
		infos.rasterization_state.depthBiasEnable = VK_TRUE;
		infos.rasterization_state.depthBiasConstantFactor = 500.0f;
		infos.rasterization_state.depthBiasSlopeFactor = 0.0f;

		assert (vulkan_globals.alias_pipelines[5].handle == VK_NULL_HANDLE);
		err = vkCreateGraphicsPipelines (vulkan_globals.device, VK_NULL_HANDLE, 1, &infos.graphics_pipeline, NULL, &vulkan_globals.alias_pipelines[5].handle);
		if (err != VK_SUCCESS)
			Sys_Error ("vkCreateGraphicsPipelines failed");
		GL_SetObjectName ((uint64_t)vulkan_globals.alias_pipelines[5].handle, VK_OBJECT_TYPE_PIPELINE, "alias_showtris_depth_test");
		vulkan_globals.alias_pipelines[5].layout = vulkan_globals.alias_pipelines[0].layout;
	}
}

/*
===============
R_CreateMD5Pipelines
===============
*/
static void R_CreateMD5Pipelines ()
{
	VkResult				err;
	pipeline_create_infos_t infos;
	R_InitDefaultStates (&infos);

	infos.depth_stencil_state.depthTestEnable = VK_TRUE;
	infos.depth_stencil_state.depthWriteEnable = VK_TRUE;
	infos.rasterization_state.depthBiasEnable = VK_FALSE;
	infos.blend_attachment_state.blendEnable = VK_FALSE;
	infos.shader_stages[1].pSpecializationInfo = NULL;

	infos.vertex_input_state.vertexAttributeDescriptionCount = 5;
	infos.vertex_input_state.pVertexAttributeDescriptions = md5_vertex_input_attribute_descriptions;
	infos.vertex_input_state.vertexBindingDescriptionCount = 1;
	infos.vertex_input_state.pVertexBindingDescriptions = &md5_vertex_binding_description;

	infos.shader_stages[0].module = md5_vert_module;
	infos.shader_stages[1].module = alias_frag_module;

	infos.graphics_pipeline.layout = vulkan_globals.md5_pipelines[0].layout.handle;

	assert (vulkan_globals.md5_pipelines[0].handle == VK_NULL_HANDLE);
	err = vkCreateGraphicsPipelines (vulkan_globals.device, VK_NULL_HANDLE, 1, &infos.graphics_pipeline, NULL, &vulkan_globals.md5_pipelines[0].handle);
	if (err != VK_SUCCESS)
		Sys_Error ("vkCreateGraphicsPipelines failed (md5_pipeline)");
	GL_SetObjectName ((uint64_t)vulkan_globals.md5_pipelines[0].handle, VK_OBJECT_TYPE_PIPELINE, "md5");

	infos.shader_stages[1].module = alias_alphatest_frag_module;

	assert (vulkan_globals.md5_pipelines[1].handle == VK_NULL_HANDLE);
	err = vkCreateGraphicsPipelines (vulkan_globals.device, VK_NULL_HANDLE, 1, &infos.graphics_pipeline, NULL, &vulkan_globals.md5_pipelines[1].handle);
	if (err != VK_SUCCESS)
		Sys_Error ("vkCreateGraphicsPipelines failed (md5_alphatest_pipeline)");
	GL_SetObjectName ((uint64_t)vulkan_globals.md5_pipelines[1].handle, VK_OBJECT_TYPE_PIPELINE, "md5_alphatest");
	vulkan_globals.md5_pipelines[1].layout = vulkan_globals.md5_pipelines[0].layout;

	infos.depth_stencil_state.depthWriteEnable = VK_FALSE;
	infos.blend_attachment_state.blendEnable = VK_TRUE;
	infos.shader_stages[1].module = alias_frag_module;

	assert (vulkan_globals.md5_pipelines[2].handle == VK_NULL_HANDLE);
	err = vkCreateGraphicsPipelines (vulkan_globals.device, VK_NULL_HANDLE, 1, &infos.graphics_pipeline, NULL, &vulkan_globals.md5_pipelines[2].handle);
	if (err != VK_SUCCESS)
		Sys_Error ("vkCreateGraphicsPipelines failed (md5_blend_pipeline)");
	GL_SetObjectName ((uint64_t)vulkan_globals.md5_pipelines[2].handle, VK_OBJECT_TYPE_PIPELINE, "md5_blend");
	vulkan_globals.md5_pipelines[2].layout = vulkan_globals.md5_pipelines[0].layout;

	infos.shader_stages[1].module = alias_alphatest_frag_module;

	assert (vulkan_globals.md5_pipelines[3].handle == VK_NULL_HANDLE);
	err = vkCreateGraphicsPipelines (vulkan_globals.device, VK_NULL_HANDLE, 1, &infos.graphics_pipeline, NULL, &vulkan_globals.md5_pipelines[3].handle);
	if (err != VK_SUCCESS)
		Sys_Error ("vkCreateGraphicsPipelines failed");
	GL_SetObjectName ((uint64_t)vulkan_globals.md5_pipelines[3].handle, VK_OBJECT_TYPE_PIPELINE, "md5_alphatest_blend");
	vulkan_globals.md5_pipelines[3].layout = vulkan_globals.md5_pipelines[0].layout;

	if (vulkan_globals.non_solid_fill)
	{
		infos.rasterization_state.cullMode = VK_CULL_MODE_NONE;
		infos.rasterization_state.polygonMode = VK_POLYGON_MODE_LINE;
		infos.depth_stencil_state.depthTestEnable = VK_FALSE;
		infos.depth_stencil_state.depthWriteEnable = VK_FALSE;
		infos.blend_attachment_state.blendEnable = VK_FALSE;
		infos.input_assembly_state.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

		infos.shader_stages[0].module = md5_vert_module;
		infos.shader_stages[1].module = showtris_frag_module;

		infos.graphics_pipeline.layout = vulkan_globals.md5_pipelines[0].layout.handle;

		assert (vulkan_globals.md5_pipelines[4].handle == VK_NULL_HANDLE);
		err = vkCreateGraphicsPipelines (vulkan_globals.device, VK_NULL_HANDLE, 1, &infos.graphics_pipeline, NULL, &vulkan_globals.md5_pipelines[4].handle);
		if (err != VK_SUCCESS)
			Sys_Error ("vkCreateGraphicsPipelines failed");
		GL_SetObjectName ((uint64_t)vulkan_globals.md5_pipelines[4].handle, VK_OBJECT_TYPE_PIPELINE, "md5_showtris");
		vulkan_globals.md5_pipelines[4].layout = vulkan_globals.md5_pipelines[0].layout;

		infos.depth_stencil_state.depthTestEnable = VK_TRUE;
		infos.rasterization_state.depthBiasEnable = VK_TRUE;
		infos.rasterization_state.depthBiasConstantFactor = 500.0f;
		infos.rasterization_state.depthBiasSlopeFactor = 0.0f;

		assert (vulkan_globals.md5_pipelines[5].handle == VK_NULL_HANDLE);
		err = vkCreateGraphicsPipelines (vulkan_globals.device, VK_NULL_HANDLE, 1, &infos.graphics_pipeline, NULL, &vulkan_globals.md5_pipelines[5].handle);
		if (err != VK_SUCCESS)
			Sys_Error ("vkCreateGraphicsPipelines failed");
		GL_SetObjectName ((uint64_t)vulkan_globals.md5_pipelines[5].handle, VK_OBJECT_TYPE_PIPELINE, "md5_showtris_depth_test");
		vulkan_globals.md5_pipelines[5].layout = vulkan_globals.md5_pipelines[0].layout;
	}
}

/*
===============
R_CreatePostprocessPipelines
===============
*/
static void R_CreatePostprocessPipelines ()
{
	VkResult				err;
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
	infos.graphics_pipeline.renderPass = vulkan_globals.secondary_cb_contexts[SCBX_GUI]->render_pass;
	infos.graphics_pipeline.layout = vulkan_globals.postprocess_pipeline.layout.handle;
	infos.graphics_pipeline.subpass = 1;

	assert (vulkan_globals.postprocess_pipeline.handle == VK_NULL_HANDLE);
	err = vkCreateGraphicsPipelines (vulkan_globals.device, VK_NULL_HANDLE, 1, &infos.graphics_pipeline, NULL, &vulkan_globals.postprocess_pipeline.handle);
	if (err != VK_SUCCESS)
		Sys_Error ("vkCreateGraphicsPipelines failed (postprocess_pipeline)");
	GL_SetObjectName ((uint64_t)vulkan_globals.postprocess_pipeline.handle, VK_OBJECT_TYPE_PIPELINE, "postprocess");
}

/*
===============
R_CreateScreenEffectsPipelines
===============
*/
static void R_CreateScreenEffectsPipelines ()
{
	VkResult				err;
	pipeline_create_infos_t infos;
	R_InitDefaultStates (&infos);

	ZEROED_STRUCT (VkPipelineShaderStageCreateInfo, compute_shader_stage);
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
		Sys_Error ("vkCreateComputePipelines failed (screen_effects_pipeline)");
	GL_SetObjectName ((uint64_t)vulkan_globals.screen_effects_pipeline.handle, VK_OBJECT_TYPE_PIPELINE, "screen_effects");

	compute_shader_stage.module =
		(vulkan_globals.color_format == VK_FORMAT_A2B10G10R10_UNORM_PACK32) ? screen_effects_10bit_scale_comp_module : screen_effects_8bit_scale_comp_module;
	infos.compute_pipeline.stage = compute_shader_stage;
	assert (vulkan_globals.screen_effects_scale_pipeline.handle == VK_NULL_HANDLE);
	err = vkCreateComputePipelines (
		vulkan_globals.device, VK_NULL_HANDLE, 1, &infos.compute_pipeline, NULL, &vulkan_globals.screen_effects_scale_pipeline.handle);
	if (err != VK_SUCCESS)
		Sys_Error ("vkCreateComputePipelines failed (screen_effects_scale_pipeline)");
	GL_SetObjectName ((uint64_t)vulkan_globals.screen_effects_scale_pipeline.handle, VK_OBJECT_TYPE_PIPELINE, "screen_effects_scale");

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
			Sys_Error ("vkCreateComputePipelines failed (screen_effects_scale_sops_pipeline)");
		GL_SetObjectName ((uint64_t)vulkan_globals.screen_effects_scale_sops_pipeline.handle, VK_OBJECT_TYPE_PIPELINE, "screen_effects_scale_sops");
		compute_shader_stage.flags = 0;
	}
}

/*
===============
R_CreateUpdateLightmapPipelines
===============
*/
static void R_CreateUpdateLightmapPipelines ()
{
	VkResult				err;
	pipeline_create_infos_t infos;
	R_InitDefaultStates (&infos);

	VkSpecializationMapEntry specialization_entry;
	specialization_entry.constantID = 0;
	specialization_entry.offset = 0;
	specialization_entry.size = 4;
	uint32_t			 specialization_data = vulkan_globals.color_format == VK_FORMAT_A2B10G10R10_UNORM_PACK32; // 10-bit lightmap
	VkSpecializationInfo specialization_info;
	specialization_info.mapEntryCount = 1;
	specialization_info.pMapEntries = &specialization_entry;
	specialization_info.dataSize = 4;
	specialization_info.pData = &specialization_data;

	ZEROED_STRUCT (VkPipelineShaderStageCreateInfo, compute_shader_stage);
	compute_shader_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	compute_shader_stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	compute_shader_stage.module = update_lightmap_comp_module;
	compute_shader_stage.pName = "main";
	compute_shader_stage.pSpecializationInfo = &specialization_info;

	memset (&infos.compute_pipeline, 0, sizeof (infos.compute_pipeline));
	infos.compute_pipeline.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
	infos.compute_pipeline.stage = compute_shader_stage;
	infos.compute_pipeline.layout = vulkan_globals.update_lightmap_pipeline.layout.handle;

	assert (vulkan_globals.update_lightmap_pipeline.handle == VK_NULL_HANDLE);
	err = vkCreateComputePipelines (vulkan_globals.device, VK_NULL_HANDLE, 1, &infos.compute_pipeline, NULL, &vulkan_globals.update_lightmap_pipeline.handle);
	if (err != VK_SUCCESS)
		Sys_Error ("vkCreateComputePipelines failed (update_lightmap_pipeline)");
	GL_SetObjectName ((uint64_t)vulkan_globals.update_lightmap_pipeline.handle, VK_OBJECT_TYPE_PIPELINE, "update_lightmap");

	if (vulkan_globals.ray_query)
	{
		compute_shader_stage.module = update_lightmap_rt_comp_module;
		infos.compute_pipeline.stage = compute_shader_stage;
		infos.compute_pipeline.layout = vulkan_globals.update_lightmap_rt_pipeline.layout.handle;
		assert (vulkan_globals.update_lightmap_rt_pipeline.handle == VK_NULL_HANDLE);
		err = vkCreateComputePipelines (
			vulkan_globals.device, VK_NULL_HANDLE, 1, &infos.compute_pipeline, NULL, &vulkan_globals.update_lightmap_rt_pipeline.handle);
		if (err != VK_SUCCESS)
			Sys_Error ("vkCreateComputePipelines failed (update_lightmap_rt_pipeline)");
		GL_SetObjectName ((uint64_t)vulkan_globals.update_lightmap_rt_pipeline.handle, VK_OBJECT_TYPE_PIPELINE, "update_lightmap_rt");
	}
}

/*
===============
R_CreateIndirectComputePipelines
===============
*/
static void R_CreateIndirectComputePipelines ()
{
	VkResult				err;
	pipeline_create_infos_t infos;
	R_InitDefaultStates (&infos);

	ZEROED_STRUCT (VkPipelineShaderStageCreateInfo, compute_shader_stage);
	compute_shader_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	compute_shader_stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	compute_shader_stage.module = indirect_comp_module;
	compute_shader_stage.pName = "main";

	memset (&infos.compute_pipeline, 0, sizeof (infos.compute_pipeline));
	infos.compute_pipeline.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
	infos.compute_pipeline.stage = compute_shader_stage;
	infos.compute_pipeline.layout = vulkan_globals.indirect_draw_pipeline.layout.handle;

	assert (vulkan_globals.indirect_draw_pipeline.handle == VK_NULL_HANDLE);
	err = vkCreateComputePipelines (vulkan_globals.device, VK_NULL_HANDLE, 1, &infos.compute_pipeline, NULL, &vulkan_globals.indirect_draw_pipeline.handle);
	if (err != VK_SUCCESS)
		Sys_Error ("vkCreateComputePipelines failed (indirect_draw_pipeline)");
	GL_SetObjectName ((uint64_t)vulkan_globals.indirect_draw_pipeline.handle, VK_OBJECT_TYPE_PIPELINE, "indirect_draw");

	compute_shader_stage.module = indirect_clear_comp_module;
	infos.compute_pipeline.stage = compute_shader_stage;

	assert (vulkan_globals.indirect_clear_pipeline.handle == VK_NULL_HANDLE);
	err = vkCreateComputePipelines (vulkan_globals.device, VK_NULL_HANDLE, 1, &infos.compute_pipeline, NULL, &vulkan_globals.indirect_clear_pipeline.handle);
	if (err != VK_SUCCESS)
		Sys_Error ("vkCreateComputePipelines failed (indirect_clear_pipeline)");
	GL_SetObjectName ((uint64_t)vulkan_globals.indirect_clear_pipeline.handle, VK_OBJECT_TYPE_PIPELINE, "indirect_clear");
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
	CREATE_SHADER_MODULE (md5_vert);
	CREATE_SHADER_MODULE (sky_layer_vert);
	CREATE_SHADER_MODULE (sky_layer_frag);
	CREATE_SHADER_MODULE (sky_box_frag);
	CREATE_SHADER_MODULE (sky_cube_vert);
	CREATE_SHADER_MODULE (sky_cube_frag);
	CREATE_SHADER_MODULE (postprocess_vert);
	CREATE_SHADER_MODULE (postprocess_frag);
	CREATE_SHADER_MODULE (screen_effects_8bit_comp);
	CREATE_SHADER_MODULE (screen_effects_8bit_scale_comp);
	CREATE_SHADER_MODULE_COND (screen_effects_8bit_scale_sops_comp, vulkan_globals.screen_effects_sops);
	CREATE_SHADER_MODULE (screen_effects_10bit_comp);
	CREATE_SHADER_MODULE (screen_effects_10bit_scale_comp);
	CREATE_SHADER_MODULE_COND (screen_effects_10bit_scale_sops_comp, vulkan_globals.screen_effects_sops);
	CREATE_SHADER_MODULE (cs_tex_warp_comp);
	CREATE_SHADER_MODULE (indirect_comp);
	CREATE_SHADER_MODULE (indirect_clear_comp);
	CREATE_SHADER_MODULE (showtris_vert);
	CREATE_SHADER_MODULE (showtris_frag);
	CREATE_SHADER_MODULE (update_lightmap_comp);
	CREATE_SHADER_MODULE_COND (update_lightmap_rt_comp, vulkan_globals.ray_query);
#ifdef _DEBUG
	CREATE_SHADER_MODULE_COND (ray_debug_comp, vulkan_globals.ray_query);
#endif
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
	DESTROY_SHADER_MODULE (md5_vert);
	DESTROY_SHADER_MODULE (sky_layer_vert);
	DESTROY_SHADER_MODULE (sky_layer_frag);
	DESTROY_SHADER_MODULE (sky_box_frag);
	DESTROY_SHADER_MODULE (sky_cube_vert);
	DESTROY_SHADER_MODULE (sky_cube_frag);
	DESTROY_SHADER_MODULE (postprocess_vert);
	DESTROY_SHADER_MODULE (postprocess_frag);
	DESTROY_SHADER_MODULE (screen_effects_8bit_comp);
	DESTROY_SHADER_MODULE (screen_effects_8bit_scale_comp);
	DESTROY_SHADER_MODULE (screen_effects_8bit_scale_sops_comp);
	DESTROY_SHADER_MODULE (screen_effects_10bit_comp);
	DESTROY_SHADER_MODULE (screen_effects_10bit_scale_comp);
	DESTROY_SHADER_MODULE (screen_effects_10bit_scale_sops_comp);
	DESTROY_SHADER_MODULE (cs_tex_warp_comp);
	DESTROY_SHADER_MODULE (indirect_comp);
	DESTROY_SHADER_MODULE (indirect_clear_comp);
	DESTROY_SHADER_MODULE (showtris_vert);
	DESTROY_SHADER_MODULE (showtris_frag);
	DESTROY_SHADER_MODULE (update_lightmap_comp);
	DESTROY_SHADER_MODULE (update_lightmap_rt_comp);
	DESTROY_SHADER_MODULE (ray_debug_comp);
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
	R_CreateMD5Pipelines ();
	R_CreatePostprocessPipelines ();
	R_CreateScreenEffectsPipelines ();
	R_CreateUpdateLightmapPipelines ();
	R_CreateIndirectComputePipelines ();
	R_CreateRayDebugPipelines ();

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

	for (i = 0; i < 2; ++i)
	{
		vkDestroyPipeline (vulkan_globals.device, vulkan_globals.sky_stencil_pipeline[i].handle, NULL);
		vulkan_globals.sky_stencil_pipeline[i].handle = VK_NULL_HANDLE;
		vkDestroyPipeline (vulkan_globals.device, vulkan_globals.sky_color_pipeline[i].handle, NULL);
		vulkan_globals.sky_color_pipeline[i].handle = VK_NULL_HANDLE;
		vkDestroyPipeline (vulkan_globals.device, vulkan_globals.sky_cube_pipeline[i].handle, NULL);
		vulkan_globals.sky_cube_pipeline[i].handle = VK_NULL_HANDLE;
		vkDestroyPipeline (vulkan_globals.device, vulkan_globals.sky_layer_pipeline[i].handle, NULL);
		vulkan_globals.sky_layer_pipeline[i].handle = VK_NULL_HANDLE;
	}
	vkDestroyPipeline (vulkan_globals.device, vulkan_globals.sky_box_pipeline.handle, NULL);
	vulkan_globals.sky_box_pipeline.handle = VK_NULL_HANDLE;
	for (i = 0; i < MODEL_PIPELINE_COUNT; ++i)
	{
		if (vulkan_globals.alias_pipelines[i].handle != VK_NULL_HANDLE)
		{
			vkDestroyPipeline (vulkan_globals.device, vulkan_globals.alias_pipelines[i].handle, NULL);
			vulkan_globals.alias_pipelines[i].handle = VK_NULL_HANDLE;
		}
		if (vulkan_globals.md5_pipelines[i].handle != VK_NULL_HANDLE)
		{
			vkDestroyPipeline (vulkan_globals.device, vulkan_globals.md5_pipelines[i].handle, NULL);
			vulkan_globals.md5_pipelines[i].handle = VK_NULL_HANDLE;
		}
	}
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
		vkDestroyPipeline (vulkan_globals.device, vulkan_globals.showtris_indirect_pipeline.handle, NULL);
		vulkan_globals.showtris_indirect_pipeline.handle = VK_NULL_HANDLE;
		vkDestroyPipeline (vulkan_globals.device, vulkan_globals.showtris_depth_test_pipeline.handle, NULL);
		vulkan_globals.showtris_depth_test_pipeline.handle = VK_NULL_HANDLE;
		vkDestroyPipeline (vulkan_globals.device, vulkan_globals.showtris_indirect_depth_test_pipeline.handle, NULL);
		vulkan_globals.showtris_indirect_depth_test_pipeline.handle = VK_NULL_HANDLE;
		vkDestroyPipeline (vulkan_globals.device, vulkan_globals.showbboxes_pipeline.handle, NULL);
		vulkan_globals.showbboxes_pipeline.handle = VK_NULL_HANDLE;
	}
	vkDestroyPipeline (vulkan_globals.device, vulkan_globals.update_lightmap_pipeline.handle, NULL);
	vulkan_globals.update_lightmap_pipeline.handle = VK_NULL_HANDLE;
	if (vulkan_globals.update_lightmap_rt_pipeline.handle != VK_NULL_HANDLE)
	{
		vkDestroyPipeline (vulkan_globals.device, vulkan_globals.update_lightmap_rt_pipeline.handle, NULL);
		vulkan_globals.update_lightmap_rt_pipeline.handle = VK_NULL_HANDLE;
	}
	if (vulkan_globals.ray_debug_pipeline.handle != VK_NULL_HANDLE)
	{
		vkDestroyPipeline (vulkan_globals.device, vulkan_globals.ray_debug_pipeline.handle, NULL);
		vulkan_globals.ray_debug_pipeline.handle = VK_NULL_HANDLE;
	}
	vkDestroyPipeline (vulkan_globals.device, vulkan_globals.indirect_draw_pipeline.handle, NULL);
	vulkan_globals.indirect_draw_pipeline.handle = VK_NULL_HANDLE;
	vkDestroyPipeline (vulkan_globals.device, vulkan_globals.indirect_clear_pipeline.handle, NULL);
	vulkan_globals.indirect_clear_pipeline.handle = VK_NULL_HANDLE;
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
	Cvar_RegisterVariable (&r_alphasort);
	Cvar_RegisterVariable (&r_speeds);
	Cvar_RegisterVariable (&r_pos);
	Cvar_RegisterVariable (&gl_polyblend);
	Cvar_RegisterVariable (&gl_nocolors);
	Cvar_SetCallback (&gl_nocolors, Mod_RefreshSkins_f);

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
	Cvar_RegisterVariable (&r_showbboxes_filter);
	Cvar_SetCallback (&r_showbboxes_filter, R_SetShowbboxesFilter_f);
	Cvar_RegisterVariable (&gl_farclip);
	Cvar_RegisterVariable (&gl_fullbrights);
	Cvar_SetCallback (&gl_fullbrights, GL_Fullbrights_f);
	Cvar_RegisterVariable (&r_lerpmodels);
	Cvar_RegisterVariable (&r_lerpmove);
	Cvar_RegisterVariable (&r_lerpturn);
	Cvar_RegisterVariable (&r_nolerp_list);
	Cvar_SetCallback (&r_nolerp_list, R_Model_ExtraFlags_List_f);
	// johnfitz

	Cvar_RegisterVariable (&gl_zfix); // QuakeSpasm z-fighting fix
	Cvar_RegisterVariable (&r_lavaalpha);
	Cvar_RegisterVariable (&r_telealpha);
	Cvar_RegisterVariable (&r_slimealpha);
	Cvar_RegisterVariable (&r_scale);
	Cvar_RegisterVariable (&r_lodbias);
	Cvar_RegisterVariable (&gl_lodbias);
	Cvar_SetCallback (&r_scale, R_ScaleChanged_f);
	Cvar_SetCallback (&r_lodbias, R_ScaleChanged_f);
	Cvar_SetCallback (&gl_lodbias, R_ScaleChanged_f);
	Cvar_SetCallback (&r_lavaalpha, R_SetLavaalpha_f);
	Cvar_SetCallback (&r_telealpha, R_SetTelealpha_f);
	Cvar_SetCallback (&r_slimealpha, R_SetSlimealpha_f);

	Cvar_RegisterVariable (&r_gpulightmapupdate);
	Cvar_RegisterVariable (&r_rtshadows);
	Cvar_SetCallback (&r_rtshadows, R_SetRTShadows_f);
	Cvar_RegisterVariable (&r_indirect);
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
	ZEROED_STRUCT (VkDescriptorSetAllocateInfo, descriptor_set_allocate_info);
	descriptor_set_allocate_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	descriptor_set_allocate_info.descriptorPool = vulkan_globals.descriptor_pool;
	descriptor_set_allocate_info.descriptorSetCount = 1;
	descriptor_set_allocate_info.pSetLayouts = &layout->handle;

	VkDescriptorSet handle;
	vkAllocateDescriptorSets (vulkan_globals.device, &descriptor_set_allocate_info, &handle);

	Atomic_AddUInt32 (&num_vulkan_combined_image_samplers, layout->num_combined_image_samplers);
	Atomic_AddUInt32 (&num_vulkan_ubos_dynamic, layout->num_ubos_dynamic);
	Atomic_AddUInt32 (&num_vulkan_ubos, layout->num_ubos);
	Atomic_AddUInt32 (&num_vulkan_storage_buffers, layout->num_storage_buffers);
	Atomic_AddUInt32 (&num_vulkan_input_attachments, layout->num_input_attachments);
	Atomic_AddUInt32 (&num_vulkan_storage_images, layout->num_storage_images);
	Atomic_AddUInt32 (&num_vulkan_sampled_images, layout->num_sampled_images);
	Atomic_AddUInt32 (&num_acceleration_structures, layout->num_acceleration_structures);

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

	Atomic_SubUInt32 (&num_vulkan_combined_image_samplers, layout->num_combined_image_samplers);
	Atomic_SubUInt32 (&num_vulkan_ubos_dynamic, layout->num_ubos_dynamic);
	Atomic_SubUInt32 (&num_vulkan_ubos, layout->num_ubos);
	Atomic_SubUInt32 (&num_vulkan_storage_buffers, layout->num_storage_buffers);
	Atomic_SubUInt32 (&num_vulkan_input_attachments, layout->num_input_attachments);
	Atomic_SubUInt32 (&num_vulkan_storage_images, layout->num_storage_images);
	Atomic_SubUInt32 (&num_vulkan_sampled_images, layout->num_sampled_images);
	Atomic_SubUInt32 (&num_acceleration_structures, layout->num_acceleration_structures);
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
	byte	   *pixels;
	aliashdr_t *paliashdr;
	int			skinnum;

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

	pixels = (byte *)paliashdr->texels[skinnum];
	if (!pixels)
	{
		static qboolean warned = false;
		if (!warned)
		{
			warned = true;
			Con_Warning ("can't recolor non-indexed player skin\n");
		}
		playertextures[playernum] = NULL;
		return;
	}

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
	char		key[128], value[4096];
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

	for (i = 0; i < MAX_LIGHTSTYLES; i++)
		d_lightstylevalue[i] = 264; // normal light value

	// clear out efrags in case the level hasn't been reloaded
	// FIXME: is this one short?
	for (i = 0; i < cl.worldmodel->numleafs; i++)
		cl.worldmodel->leafs[i].efrags = NULL;

	r_viewleaf = NULL;
	R_ClearParticles ();
#ifdef PSET_SCRIPT
	PScript_ClearParticles (true);
#endif

	qboolean need_GL_init = false;
	for (i = 1; i < MAX_MODELS; ++i)
	{
		qmodel_t *m = cl.model_precache[i];
		if (!m)
			break;
		if (m->type == mod_brush && !m->primed)
			need_GL_init = true;
	}

	Con_DPrintf ("%sGL init\n", need_GL_init ? "" : "Skipping ");

	if (need_GL_init)
	{
		TexMgr_FreeTextures (TEXPREF_ISLIGHTMAP, TEXPREF_ISLIGHTMAP);
		Mod_UnPrimeAll ();
		GL_DeleteBModelVertexBuffer ();

		GL_BuildLightmaps ();
		GL_BuildBModelVertexBuffer ();
		GL_BuildBModelAccelerationStructures ();
		GL_PrepareSIMDAndParallelData ();
		GL_SetupIndirectDraws ();
		GL_SetupLightmapCompute ();
		GL_UpdateLightmapDescriptorSets ();

		for (i = 1; i < MAX_MODELS; ++i)
		{
			qmodel_t *m = cl.model_precache[i];
			if (!m)
				break;
			m->primed = true;
		}
	}
	// ericw -- no longer load alias models into a VBO here, it's done in Mod_LoadAliasModel

	r_framecount = 0;	 // johnfitz -- paranoid?
	r_visframecount = 0; // johnfitz -- paranoid?

	Sky_NewMap ();		  // johnfitz -- skybox in worldspawn
	Fog_NewMap ();		  // johnfitz -- global fog in worldspawn
	R_ParseWorldspawn (); // ericw -- wateralpha, lavaalpha, telealpha, slimealpha in worldspawn

	GL_UpdateDescriptorSets ();
}

/*
====================
R_TimeRefresh_f

For program optimization
====================
*/
void R_TimeRefresh_f (void)
{
	int	  i;
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
		GL_BeginRendering (false, NULL, &glwidth, &glheight);
		r_refdef.viewangles[1] = i / 128.0 * 360.0;
		R_RenderView (false, INVALID_TASK_HANDLE, INVALID_TASK_HANDLE, INVALID_TASK_HANDLE);
		GL_EndRendering (false, false);
	}

	// glFinish ();
	stop = Sys_DoubleTime ();
	time = stop - start;
	Con_Printf ("%f seconds (%f fps)\n", time, 128 / time);
}

/*
====================
R_AllocateVulkanMemory
====================
*/
void R_AllocateVulkanMemory (vulkan_memory_t *memory, VkMemoryAllocateInfo *memory_allocate_info, vulkan_memory_type_t type, atomic_uint32_t *num_allocations)
{
	memory->type = type;
	if (memory->type != VULKAN_MEMORY_TYPE_NONE)
	{
		VkResult err = vkAllocateMemory (vulkan_globals.device, memory_allocate_info, NULL, &memory->handle);
		if (err != VK_SUCCESS)
			Sys_Error ("vkAllocateMemory failed");
		if (num_allocations)
			Atomic_IncrementUInt32 (num_allocations);
	}
	memory->size = memory_allocate_info->allocationSize;
	if (memory->type == VULKAN_MEMORY_TYPE_DEVICE)
		Atomic_AddUInt64 (&total_device_vulkan_allocation_size, memory->size);
	else if (memory->type == VULKAN_MEMORY_TYPE_HOST)
		Atomic_AddUInt64 (&total_host_vulkan_allocation_size, memory->size);
}

/*
====================
R_FreeVulkanMemory
====================
*/
void R_FreeVulkanMemory (vulkan_memory_t *memory, atomic_uint32_t *num_allocations)
{
	if (memory->type == VULKAN_MEMORY_TYPE_DEVICE)
		Atomic_SubUInt64 (&total_device_vulkan_allocation_size, memory->size);
	else if (memory->type == VULKAN_MEMORY_TYPE_HOST)
		Atomic_SubUInt64 (&total_host_vulkan_allocation_size, memory->size);
	if (memory->type != VULKAN_MEMORY_TYPE_NONE)
	{
		vkFreeMemory (vulkan_globals.device, memory->handle, NULL);
		if (num_allocations)
			Atomic_DecrementUInt32 (num_allocations);
	}
	memory->handle = VK_NULL_HANDLE;
	memory->size = 0;
}

/*
====================
R_CreateBuffer
====================
*/
void R_CreateBuffer (
	VkBuffer *buffer, vulkan_memory_t *memory, const size_t size, VkBufferUsageFlags usage, const VkFlags mem_requirements_mask,
	const VkFlags mem_preferred_mask, atomic_uint32_t *num_allocations, VkDeviceAddress *device_address, const char *name)
{
	VkResult err;
	qboolean get_device_address = vulkan_globals.vk_get_buffer_device_address && device_address;

	if (get_device_address)
		usage |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT_KHR;

	VkBufferCreateInfo buffer_create_info;
	memset (&buffer_create_info, 0, sizeof (buffer_create_info));
	buffer_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	buffer_create_info.size = size;
	buffer_create_info.usage = usage;
	err = vkCreateBuffer (vulkan_globals.device, &buffer_create_info, NULL, buffer);
	if (err != VK_SUCCESS)
		Sys_Error ("vkCreateBuffer failed");

	GL_SetObjectName ((uint64_t)*buffer, VK_OBJECT_TYPE_BUFFER, va ("%s buffer", name));

	VkMemoryRequirements memory_requirements;
	vkGetBufferMemoryRequirements (vulkan_globals.device, *buffer, &memory_requirements);

	ZEROED_STRUCT (VkMemoryAllocateFlagsInfo, memory_allocate_flags_info);
	if (get_device_address)
	{
		memory_allocate_flags_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO_KHR;
		memory_allocate_flags_info.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
	}

	ZEROED_STRUCT (VkMemoryAllocateInfo, memory_allocate_info);
	memory_allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	memory_allocate_info.pNext = get_device_address ? &memory_allocate_flags_info : NULL;
	memory_allocate_info.allocationSize = memory_requirements.size;
	memory_allocate_info.memoryTypeIndex = GL_MemoryTypeFromProperties (memory_requirements.memoryTypeBits, mem_requirements_mask, mem_preferred_mask);

	R_AllocateVulkanMemory (memory, &memory_allocate_info, VULKAN_MEMORY_TYPE_DEVICE, num_allocations);
	GL_SetObjectName ((uint64_t)memory->handle, VK_OBJECT_TYPE_DEVICE_MEMORY, va ("%s memory", name));

	err = vkBindBufferMemory (vulkan_globals.device, *buffer, memory->handle, 0);
	if (err != VK_SUCCESS)
		Sys_Error ("vkBindImageMemory failed");

	if (get_device_address)
	{
		VkBufferDeviceAddressInfoKHR buffer_device_address_info;
		memset (&buffer_device_address_info, 0, sizeof (buffer_device_address_info));
		buffer_device_address_info.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO_KHR;
		buffer_device_address_info.buffer = *buffer;
		*device_address = vulkan_globals.vk_get_buffer_device_address (vulkan_globals.device, &buffer_device_address_info);
	}
}

/*
====================
R_FreeBuffer
====================
*/
void R_FreeBuffer (const VkBuffer buffer, vulkan_memory_t *memory, atomic_uint32_t *num_allocations)
{
	if (buffer != VK_NULL_HANDLE)
	{
		vkDestroyBuffer (vulkan_globals.device, buffer, NULL);
		R_FreeVulkanMemory (memory, num_allocations);
	}
}

/*
====================
R_CreateBuffers
====================
*/
size_t R_CreateBuffers (
	const int num_buffers, buffer_create_info_t *create_infos, vulkan_memory_t *memory, const VkFlags mem_requirements_mask, const VkFlags mem_preferred_mask,
	atomic_uint32_t *num_allocations, const char *memory_name)
{
	VkResult		   err;
	VkBufferUsageFlags usage_union = 0;

	qboolean get_device_address = false;
	for (int i = 0; i < num_buffers; ++i)
	{
		if (vulkan_globals.vk_get_buffer_device_address)
		{
			if (create_infos[i].address)
			{
				get_device_address = true;
				create_infos[i].usage |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT_KHR;
			}
		}
		usage_union |= create_infos[i].usage;
	}

	qboolean map_memory = false;
	size_t	 total_size = 0;
	for (int i = 0; i < num_buffers; ++i)
	{
		ZEROED_STRUCT (VkBufferCreateInfo, buffer_create_info);
		buffer_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		buffer_create_info.size = create_infos[i].size;
		buffer_create_info.usage = create_infos[i].usage;
		err = vkCreateBuffer (vulkan_globals.device, &buffer_create_info, NULL, create_infos[i].buffer);
		if (err != VK_SUCCESS)
			Sys_Error ("vkCreateBuffer failed");

		GL_SetObjectName ((uint64_t)*create_infos[i].buffer, VK_OBJECT_TYPE_BUFFER, va ("%s buffer", create_infos[i].name));

		VkMemoryRequirements memory_requirements;
		vkGetBufferMemoryRequirements (vulkan_globals.device, *create_infos[i].buffer, &memory_requirements);
		const size_t alignment = q_max (memory_requirements.alignment, create_infos[i].alignment);
		total_size = q_align (total_size, alignment);
		total_size += memory_requirements.size;
		map_memory = map_memory || create_infos[i].mapped;
	}

	ZEROED_STRUCT (VkMemoryAllocateFlagsInfo, memory_allocate_flags_info);
	if (get_device_address)
	{
		memory_allocate_flags_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO_KHR;
		memory_allocate_flags_info.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
	}

	uint32_t memory_type_bits = 0;
	{
		ZEROED_STRUCT (VkBufferCreateInfo, buffer_create_info);
		buffer_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		buffer_create_info.size = total_size;
		buffer_create_info.usage = usage_union;
		VkBuffer dummy_buffer;
		err = vkCreateBuffer (vulkan_globals.device, &buffer_create_info, NULL, &dummy_buffer);
		if (err != VK_SUCCESS)
			Sys_Error ("vkCreateBuffer failed");
		VkMemoryRequirements memory_requirements;
		// Vulkan spec:
		// The memoryTypeBits member is identical for all VkBuffer objects created with the same value for the flags and usage members in the VkBufferCreateInfo
		// structure passed to vkCreateBuffer. Further, if usage1 and usage2 of type VkBufferUsageFlags are such that the bits set in usage2 are a subset of the
		// bits set in usage1, then the bits set in memoryTypeBits returned for usage1 must be a subset of the bits set in memoryTypeBits returned for usage2,
		// for all values of flags.
		vkGetBufferMemoryRequirements (vulkan_globals.device, dummy_buffer, &memory_requirements);
		memory_type_bits = memory_requirements.memoryTypeBits;
		vkDestroyBuffer (vulkan_globals.device, dummy_buffer, NULL);
	}

	ZEROED_STRUCT (VkMemoryAllocateInfo, memory_allocate_info);
	memory_allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	memory_allocate_info.pNext = get_device_address ? &memory_allocate_flags_info : NULL;
	memory_allocate_info.allocationSize = total_size;
	memory_allocate_info.memoryTypeIndex = GL_MemoryTypeFromProperties (memory_type_bits, mem_requirements_mask, mem_preferred_mask);

	R_AllocateVulkanMemory (memory, &memory_allocate_info, VULKAN_MEMORY_TYPE_DEVICE, num_allocations);
	GL_SetObjectName ((uint64_t)memory->handle, VK_OBJECT_TYPE_DEVICE_MEMORY, memory_name);

	byte *mapped_base = NULL;
	if (map_memory)
	{
		err = vkMapMemory (vulkan_globals.device, memory->handle, 0, total_size, 0, (void **)&mapped_base);
		if (err != VK_SUCCESS)
			Sys_Error ("vkMapMemory failed");
	}

	size_t current_offset = 0;
	for (int i = 0; i < num_buffers; ++i)
	{
		VkMemoryRequirements memory_requirements;
		vkGetBufferMemoryRequirements (vulkan_globals.device, *create_infos[i].buffer, &memory_requirements);
		const size_t alignment = q_max (memory_requirements.alignment, create_infos[i].alignment);
		current_offset = q_align (current_offset, alignment);

		err = vkBindBufferMemory (vulkan_globals.device, *create_infos[i].buffer, memory->handle, current_offset);
		if (err != VK_SUCCESS)
			Sys_Error ("vkBindImageMemory failed");

		if (create_infos[i].mapped)
			*create_infos[i].mapped = mapped_base + current_offset;

		current_offset += memory_requirements.size;

		if (get_device_address && create_infos[i].address)
		{
			VkBufferDeviceAddressInfoKHR buffer_device_address_info;
			memset (&buffer_device_address_info, 0, sizeof (buffer_device_address_info));
			buffer_device_address_info.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO_KHR;
			buffer_device_address_info.buffer = *create_infos[i].buffer;
			*create_infos[i].address = vulkan_globals.vk_get_buffer_device_address (vulkan_globals.device, &buffer_device_address_info);
		}
	}

	return total_size;
}

/*
====================
R_FreeBuffers
====================
*/
void R_FreeBuffers (const int num_buffers, VkBuffer *buffers, vulkan_memory_t *memory, atomic_uint32_t *num_allocations)
{
	for (int i = 0; i < num_buffers; ++i)
	{
		if (buffers[i] != VK_NULL_HANDLE)
			vkDestroyBuffer (vulkan_globals.device, buffers[i], NULL);
	}
	R_FreeVulkanMemory (memory, num_allocations);
}

/*
====================
R_PrintHeapStats
====================
*/
static void R_PrintHeapStats (const char *name, glheapstats_t *stats)
{
	Con_Printf (
		" %s:\n"
		"  segments: %" SDL_PRIu32 "\n"
		"  allocations: %" SDL_PRIu32 "\n"
		"  small allocations: %" SDL_PRIu32 "\n"
		"  block allocations: %" SDL_PRIu32 "\n"
		"  dedicated allocs: %" SDL_PRIu32 "\n"
		"  blocks used: %" SDL_PRIu32 "\n"
		"  blocks free: %" SDL_PRIu32 "\n"
		"  pages allocated: %" SDL_PRIu32 "\n"
		"  pages free: %" SDL_PRIu32 "\n"
		"  bytes allocated: %" SDL_PRIu64 "\n"
		"  bytes free: %" SDL_PRIu64 "\n"
		"  bytes wasted: %" SDL_PRIu64 " (%.3g%%)\n",
		name, stats->num_segments, stats->num_allocations, stats->num_small_allocations, stats->num_block_allocations, stats->num_dedicated_allocations,
		stats->num_blocks_used, stats->num_blocks_free, stats->num_pages_allocated, stats->num_pages_free, stats->num_bytes_allocated, stats->num_bytes_free,
		stats->num_bytes_wasted, ((double)stats->num_bytes_wasted / (double)stats->num_bytes_allocated) * 100.0f);
}

/*
====================
R_VulkanMemStats_f
====================
*/
void R_VulkanMemStats_f (void)
{
	const uint32_t num_tex_allocations = Atomic_LoadUInt32 (&num_vulkan_tex_allocations);
	const uint32_t num_bmodel_allocations = Atomic_LoadUInt32 (&num_vulkan_bmodel_allocations);
	const uint32_t num_mesh_allocations = Atomic_LoadUInt32 (&num_vulkan_mesh_allocations);
	const uint32_t num_misc_allocations = Atomic_LoadUInt32 (&num_vulkan_misc_allocations);
	const uint32_t num_dynbuf_allocations = Atomic_LoadUInt32 (&num_vulkan_dynbuf_allocations);

	Con_Printf (
		"Vulkan allocations: %" SDL_PRIu32 "\n",
		num_tex_allocations + num_bmodel_allocations + num_mesh_allocations + num_misc_allocations + num_dynbuf_allocations);
	Con_Printf (" Tex:    %" SDL_PRIu32 "\n", num_tex_allocations);
	Con_Printf (" BModel: %" SDL_PRIu32 "\n", num_bmodel_allocations);
	Con_Printf (" Mesh:   %" SDL_PRIu32 "\n", num_mesh_allocations);
	Con_Printf (" Misc:   %" SDL_PRIu32 "\n", num_misc_allocations);
	Con_Printf (" DynBuf: %" SDL_PRIu32 "\n", num_dynbuf_allocations);

	Con_Printf ("Heaps:\n");
	R_PrintHeapStats ("Tex", TexMgr_GetHeapStats ());
	R_PrintHeapStats ("Mesh", R_GetMeshHeapStats ());

	Con_Printf ("Descriptors:\n");
	Con_Printf (" Combined image samplers: %" SDL_PRIu32 "\n", Atomic_LoadUInt32 (&num_vulkan_combined_image_samplers));
	Con_Printf (" Dynamic UBOs: %" SDL_PRIu32 "\n", Atomic_LoadUInt32 (&num_vulkan_ubos_dynamic));
	Con_Printf (" UBOs: %" SDL_PRIu32 "\n", Atomic_LoadUInt32 (&num_vulkan_ubos));
	Con_Printf (" Storage buffers: %" SDL_PRIu32 "\n", Atomic_LoadUInt32 (&num_vulkan_ubos_dynamic));
	Con_Printf (" Input attachments: %" SDL_PRIu32 "\n", Atomic_LoadUInt32 (&num_vulkan_storage_buffers));
	Con_Printf (" Storage images: %" SDL_PRIu32 "\n", Atomic_LoadUInt32 (&num_vulkan_storage_images));
	Con_Printf (" Sampled images: %" SDL_PRIu32 "\n", Atomic_LoadUInt32 (&num_vulkan_sampled_images));
	Con_Printf (" Acceleration structures: %" SDL_PRIu32 "\n", Atomic_LoadUInt32 (&num_acceleration_structures));
	Con_Printf ("Device %" SDL_PRIu64 " MiB total\n", Atomic_LoadUInt64 (&total_device_vulkan_allocation_size) / 1024 / 1024);
	Con_Printf ("Host %" SDL_PRIu64 " MiB total\n", Atomic_LoadUInt64 (&total_host_vulkan_allocation_size) / 1024 / 1024);
}
