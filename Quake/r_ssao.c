// Experimental screen-space contact occlusion: entity occluders, world receivers.
#include "quakedef.h"
#include "r_ssao.h"

typedef enum
{
	SSAO_DEPTH_PYRAMID,
	SSAO_AO,
	SSAO_FILTERED,
	SSAO_EDGES,
	SSAO_HILBERT,
	SSAO_IMAGE_COUNT
} ssao_image_t;

enum
{
	SSAO_ENTITY_MASK,
	SSAO_SCENE_DEPTH,
	SSAO_DEPTH_COUNT
};

cvar_t					 r_ssao = {"r_ssao", "3", CVAR_ARCHIVE};
static cvar_t			 r_ssao_radius = {"r_ssao_radius", "32", CVAR_ARCHIVE};
static cvar_t			 r_ssao_strength = {"r_ssao_strength", "1.0", CVAR_ARCHIVE};
vulkan_pipeline_layout_t ssao_layout;
vulkan_pipeline_t		 ssao_pipelines[MAIN_RENDER_PASS_VARIANT_COUNT];
vulkan_pipeline_layout_t ssao_compute_layout;
vulkan_pipeline_t		 ssao_prepare_pipeline, ssao_evaluate_pipeline, ssao_filter_pipeline;
static VkImage			 scene_depth;
static VkImageView		 views[SSAO_DEPTH_COUNT];
static VkDescriptorSet	 descriptors[SSAO_DEPTH_COUNT];
static VkSampler		 sampler;
#ifdef _DEBUG
static cvar_t r_ssao_debug = {"r_ssao_debug", "0", CVAR_NONE};
#endif
vulkan_pipeline_t		 ssao_mip_pipeline;
vulkan_desc_set_layout_t ssao_mip_set_layout;
static VkDescriptorSet	 mip_descriptors;

static VkImage		   working_images[SSAO_IMAGE_COUNT];
static VkImageView	   working_views[SSAO_IMAGE_COUNT];
static vulkan_memory_t working_memory[SSAO_IMAGE_COUNT];
static VkDescriptorSet working_read[SSAO_IMAGE_COUNT], working_write[SSAO_IMAGE_COUNT];

static VkImageView	   mip_views[5];
static VkDescriptorSet prepared_read, prepared_write;

void R_InitSSAO (void)
{
	Cvar_RegisterVariable (&r_ssao);
	Cvar_RegisterVariable (&r_ssao_radius);
	Cvar_RegisterVariable (&r_ssao_strength);
#ifdef _DEBUG
	Cvar_RegisterVariable (&r_ssao_debug);
#endif
}

// The spatial noise repeats every 64 pixels; build its index once per resource creation.
static void R_UploadSSAOHilbert (void)
{
	VkBuffer			 buffer;
	VkCommandBuffer		 cb;
	int					 offset;
	uint16_t			*data = (uint16_t *)R_StagingAllocate (64 * 64 * sizeof (*data), 4, &cb, &buffer, &offset);
	VkImageMemoryBarrier barrier = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
		.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.image = working_images[SSAO_HILBERT],
		.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
	vkCmdPipelineBarrier (cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &barrier);
	const VkBufferImageCopy region = {.bufferOffset = offset, .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, .imageExtent = {64, 64, 1}};
	vkCmdCopyBufferToImage (cb, buffer, working_images[SSAO_HILBERT], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
	barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
	vkCmdPipelineBarrier (cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, NULL, 0, NULL, 1, &barrier);
	R_StagingBeginCopy ();
	for (uint32_t y = 0; y < 64; ++y)
		for (uint32_t x = 0; x < 64; ++x)
		{
			uint32_t px = x, py = y, index = 0;
			for (uint32_t level = 32; level; level /= 2)
			{
				uint32_t rx = (px & level) != 0, ry = (py & level) != 0;
				index += level * level * ((3 * rx) ^ ry);
				if (!ry)
				{
					if (rx)
					{
						px = 63 - px;
						py = 63 - py;
					}
					uint32_t temp = px;
					px = py;
					py = temp;
				}
			}
			data[y * 64 + x] = index;
		}
	R_StagingEndCopy ();
}

void R_CreateSSAO (VkImage depth)
{
	if (r_ssao.value <= 0)
		return;
	scene_depth = depth;
	const VkSamplerCreateInfo sampler_info = {
		.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
		.magFilter = VK_FILTER_NEAREST,
		.minFilter = VK_FILTER_NEAREST,
		.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
		.maxLod = 4,
		.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
	};
	if (vkCreateSampler (vulkan_globals.device, &sampler_info, NULL, &sampler) != VK_SUCCESS)
		Sys_Error ("Couldn't create SSAO sampler");
	for (int i = 0; i < SSAO_DEPTH_COUNT + SSAO_IMAGE_COUNT; ++i)
	{
		const bool working = i >= SSAO_DEPTH_COUNT;
		const int  image_index = i - SSAO_DEPTH_COUNT;

		VkImage				   *target = working ? &working_images[image_index] : &scene_depth;
		VkImageView			   *view = working ? &working_views[image_index] : &views[i];
		vulkan_memory_t		   *allocation_memory = working ? &working_memory[image_index] : NULL;
		VkDescriptorSet		   *descriptor = working ? &working_read[image_index] : &descriptors[i];
		const VkImageCreateInfo image_info = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
			.imageType = VK_IMAGE_TYPE_2D,
			.format = !working							  ? vulkan_globals.depth_format
					  : image_index == SSAO_DEPTH_PYRAMID ? VK_FORMAT_R16G16_SFLOAT
					  : image_index == SSAO_EDGES		  ? VK_FORMAT_R8_UNORM
					  : image_index == SSAO_HILBERT		  ? VK_FORMAT_R16_UINT
														  : VK_FORMAT_R8_UNORM,
			.extent = {image_index == SSAO_HILBERT ? 64 : vid.width, image_index == SSAO_HILBERT ? 64 : vid.height, 1},
			.mipLevels = image_index == SSAO_DEPTH_PYRAMID ? 5 : 1,
			.arrayLayers = 1,
			.samples = working ? VK_SAMPLE_COUNT_1_BIT : vulkan_globals.sample_count,
			.tiling = VK_IMAGE_TILING_OPTIMAL,
			.usage = VK_IMAGE_USAGE_SAMPLED_BIT | (image_index == SSAO_HILBERT ? VK_IMAGE_USAGE_TRANSFER_DST_BIT : VK_IMAGE_USAGE_STORAGE_BIT),
		};
		// Depth and stencil views both borrow the main attachment; only working images own memory.
		if (working)
		{
			if (vkCreateImage (vulkan_globals.device, &image_info, NULL, target) != VK_SUCCESS)
				Sys_Error ("Couldn't create SSAO working image");
			VkMemoryRequirements requirements;
			vkGetImageMemoryRequirements (vulkan_globals.device, *target, &requirements);
			const VkMemoryDedicatedAllocateInfoKHR dedicated = {
				.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO_KHR,
				.image = *target,
			};
			VkMemoryAllocateInfo allocation = {
				.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
				.pNext = vulkan_globals.dedicated_allocation ? &dedicated : NULL,
				.allocationSize = requirements.size,
				.memoryTypeIndex = GL_MemoryTypeFromProperties (requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0),
			};
			R_AllocateVulkanMemory (allocation_memory, &allocation, VULKAN_MEMORY_TYPE_DEVICE, &num_vulkan_misc_allocations);
			if (vkBindImageMemory (vulkan_globals.device, *target, allocation_memory->handle, 0) != VK_SUCCESS)
				Sys_Error ("Couldn't bind SSAO depth memory");
		}
		const VkImageViewCreateInfo view_info = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
			.image = *target,
			.viewType = VK_IMAGE_VIEW_TYPE_2D,
			.format = image_info.format,
			.subresourceRange =
				{working				 ? VK_IMAGE_ASPECT_COLOR_BIT
				 : i == SSAO_ENTITY_MASK ? VK_IMAGE_ASPECT_STENCIL_BIT
										 : VK_IMAGE_ASPECT_DEPTH_BIT,
				 0, image_info.mipLevels, 0, 1},
		};
		if (vkCreateImageView (vulkan_globals.device, &view_info, NULL, view) != VK_SUCCESS)
			Sys_Error ("Couldn't create SSAO depth view");
		*descriptor = R_AllocateDescriptorSet (&vulkan_globals.single_texture_set_layout);
		VkDescriptorImageInfo image = {sampler, *view, working ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};
		VkWriteDescriptorSet  write = {
			 .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			 .dstSet = *descriptor,
			 .descriptorCount = 1,
			 .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			 .pImageInfo = &image,
		 };
		vkUpdateDescriptorSets (vulkan_globals.device, 1, &write, 0, NULL);
		if (working)
		{
			if (image_index == SSAO_DEPTH_PYRAMID || image_index == SSAO_HILBERT)
				continue; // The LUT is read-only; depth storage uses the individual mip views below.
			working_write[image_index] = R_AllocateDescriptorSet (&vulkan_globals.single_texture_cs_write_set_layout);
			write.dstSet = working_write[image_index];
			write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
			vkUpdateDescriptorSets (vulkan_globals.device, 1, &write, 0, NULL);
		}
	}
	R_UploadSSAOHilbert ();
	for (int mip = 0; mip < 5; ++mip)
	{
		const VkImageViewCreateInfo info = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
			.image = working_images[SSAO_DEPTH_PYRAMID],
			.viewType = VK_IMAGE_VIEW_TYPE_2D,
			.format = VK_FORMAT_R16G16_SFLOAT,
			.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, mip, 1, 0, 1}};
		if (vkCreateImageView (vulkan_globals.device, &info, NULL, &mip_views[mip]) != VK_SUCCESS)
			Sys_Error ("Couldn't create GTAO mip view");
		if (mip != 0)
			continue;
		prepared_read = R_AllocateDescriptorSet (&vulkan_globals.single_texture_set_layout);
		prepared_write = R_AllocateDescriptorSet (&vulkan_globals.single_texture_cs_write_set_layout);
		const VkDescriptorImageInfo image = {sampler, mip_views[mip], VK_IMAGE_LAYOUT_GENERAL};
		VkWriteDescriptorSet		write = {
				   .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				   .dstSet = prepared_read,
				   .descriptorCount = 1,
				   .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				   .pImageInfo = &image};
		vkUpdateDescriptorSets (vulkan_globals.device, 1, &write, 0, NULL);
		write.dstSet = prepared_write;
		write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		vkUpdateDescriptorSets (vulkan_globals.device, 1, &write, 0, NULL);
	}
	mip_descriptors = R_AllocateDescriptorSet (&ssao_mip_set_layout);
	for (int binding = 0; binding < 6; ++binding)
	{
		const VkDescriptorImageInfo image = {
			sampler, binding == 0 ? views[SSAO_SCENE_DEPTH] : mip_views[binding - 1],
			binding == 0 ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_GENERAL};
		const VkWriteDescriptorSet write = {
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = mip_descriptors,
			.dstBinding = binding,
			.descriptorCount = 1,
			.descriptorType = binding == 0 ? VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
			.pImageInfo = &image};
		vkUpdateDescriptorSets (vulkan_globals.device, 1, &write, 0, NULL);
	}
}

void R_DestroySSAO (void)
{
	if (mip_descriptors)
		R_FreeDescriptorSet (mip_descriptors, &ssao_mip_set_layout);
	mip_descriptors = VK_NULL_HANDLE;
	if (prepared_read)
		R_FreeDescriptorSet (prepared_read, &vulkan_globals.single_texture_set_layout);
	if (prepared_write)
		R_FreeDescriptorSet (prepared_write, &vulkan_globals.single_texture_cs_write_set_layout);
	prepared_read = prepared_write = VK_NULL_HANDLE;
	for (int i = 0; i < countof (mip_views); ++i)
	{
		vkDestroyImageView (vulkan_globals.device, mip_views[i], NULL);
		mip_views[i] = VK_NULL_HANDLE;
	}
	for (int i = 0; i < SSAO_IMAGE_COUNT; ++i)
	{
		if (working_read[i])
			R_FreeDescriptorSet (working_read[i], &vulkan_globals.single_texture_set_layout);
		if (working_write[i])
			R_FreeDescriptorSet (working_write[i], &vulkan_globals.single_texture_cs_write_set_layout);
		vkDestroyImageView (vulkan_globals.device, working_views[i], NULL);
		vkDestroyImage (vulkan_globals.device, working_images[i], NULL);
		if (working_memory[i].handle)
			R_FreeVulkanMemory (&working_memory[i], &num_vulkan_misc_allocations);
		working_read[i] = working_write[i] = VK_NULL_HANDLE;
		working_views[i] = VK_NULL_HANDLE;
		working_images[i] = VK_NULL_HANDLE;
	}
	for (int i = 0; i < SSAO_DEPTH_COUNT; ++i)
	{
		if (descriptors[i])
			R_FreeDescriptorSet (descriptors[i], &vulkan_globals.single_texture_set_layout);
		vkDestroyImageView (vulkan_globals.device, views[i], NULL);
		descriptors[i] = VK_NULL_HANDLE;
		views[i] = VK_NULL_HANDLE;
	}
	scene_depth = VK_NULL_HANDLE;
	vkDestroySampler (vulkan_globals.device, sampler, NULL);
	sampler = VK_NULL_HANDLE;
}

static ssao_constants_t R_SSAOConstants (void)
{
	return (ssao_constants_t){
		.viewport = {r_refdef.vrect.x, vid.height - glheight + r_refdef.vrect.y, 1.0f / r_refdef.vrect.width, 1.0f / r_refdef.vrect.height},
		.projection =
			{1.0f / vulkan_globals.projection_matrix[0], 1.0f / vulkan_globals.projection_matrix[5], vulkan_globals.projection_matrix[14],
			 vulkan_globals.sample_count},
		.settings =
			{CLAMP (1, r_ssao_radius.value, 128), CLAMP (0, r_ssao_strength.value, 1), Fog_GetDensity () / 64.0f,
#ifdef _DEBUG
			 CLAMP (0, r_ssao_debug.value, 3)
#else
			 0
#endif
			},
	};
}

void R_PrepareSSAOWorldDepth (cb_context_t *cbx)
{
	const VkCommandBuffer cb = cbx->cb;
	if (!r_refdef.vrect.width || !r_refdef.vrect.height)
		return;
	VkImageMemoryBarrier barriers[] = {
		{.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		 .srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
		 .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
		 .oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
		 .newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
		 .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		 .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		 .image = scene_depth,
		 .subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, 0, 1, 0, 1}},
		{.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		 .srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
		 .dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
		 .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		 .newLayout = VK_IMAGE_LAYOUT_GENERAL,
		 .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		 .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		 .image = working_images[SSAO_DEPTH_PYRAMID],
		 .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 5, 0, 1}}};
	vulkan_globals.vk_cmd_pipeline_barrier (
		cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, NULL, 0, NULL, countof (barriers), barriers);
	ssao_constants_t	  constants = R_SSAOConstants ();
	const VkDescriptorSet sets[] = {descriptors[SSAO_ENTITY_MASK], descriptors[SSAO_SCENE_DEPTH], prepared_write};
	vulkan_globals.vk_cmd_bind_pipeline (cb, VK_PIPELINE_BIND_POINT_COMPUTE, ssao_prepare_pipeline.handle);
	vulkan_globals.vk_cmd_bind_descriptor_sets (cb, VK_PIPELINE_BIND_POINT_COMPUTE, ssao_prepare_pipeline.layout.handle, 0, countof (sets), sets, 0, NULL);
	vulkan_globals.vk_cmd_push_constants (cb, ssao_prepare_pipeline.layout.handle, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof (constants), &constants);
	vulkan_globals.vk_cmd_dispatch (cb, (vid.width + 7) / 8, (vid.height + 7) / 8, 1);
	barriers[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
	barriers[0].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
	barriers[0].oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
	barriers[0].newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	vulkan_globals.vk_cmd_pipeline_barrier (
		cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, 0, 0, NULL, 0, NULL,
		1, barriers);
}

void R_ComputeSSAO (cb_context_t *cbx)
{
	const VkCommandBuffer cb = cbx->cb;
	if (!r_refdef.vrect.width || !r_refdef.vrect.height)
		return;
	// Read the live combined depth directly; the composite subpass keeps it read-only.
	const VkImageMemoryBarrier depth_barrier = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
		.dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
		.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
		.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.image = scene_depth,
		.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, 0, 1, 0, 1}};
	ssao_constants_t	 constants = R_SSAOConstants ();
	// Preserve prepared world depth; discard the other outputs after previous readers finish.
	VkImageMemoryBarrier barriers[SSAO_HILBERT + 1];
	for (int i = 0; i < SSAO_HILBERT; ++i)
		barriers[i] = (VkImageMemoryBarrier){
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
			.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
			.oldLayout = i == SSAO_DEPTH_PYRAMID ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED,
			.newLayout = VK_IMAGE_LAYOUT_GENERAL,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.image = working_images[i],
			.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, i == SSAO_DEPTH_PYRAMID ? 5 : 1, 0, 1}};
	barriers[SSAO_HILBERT] = depth_barrier;
	vulkan_globals.vk_cmd_pipeline_barrier (
		cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, NULL, 0, NULL,
		countof (barriers), barriers);
	const uint32_t		  width = vid.width, height = vid.height;
	const uint32_t		  groups_x = (width + 7) / 8, groups_y = (height + 7) / 8;
	const VkMemoryBarrier read_barrier = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT, .dstAccessMask = VK_ACCESS_SHADER_READ_BIT};

	// Resolve combined depth and build all depth levels in one dispatch.
	vulkan_globals.vk_cmd_bind_pipeline (cb, VK_PIPELINE_BIND_POINT_COMPUTE, ssao_mip_pipeline.handle);
	vulkan_globals.vk_cmd_bind_descriptor_sets (cb, VK_PIPELINE_BIND_POINT_COMPUTE, ssao_mip_pipeline.layout.handle, 0, 1, &mip_descriptors, 0, NULL);
	vulkan_globals.vk_cmd_push_constants (cb, ssao_mip_pipeline.layout.handle, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof (constants), &constants);
	vulkan_globals.vk_cmd_dispatch (cb, (width + 15) / 16, (height + 15) / 16, 1);
	vulkan_globals.vk_cmd_pipeline_barrier (
		cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &read_barrier, 0, NULL, 0, NULL);

	// Match texture coordinates to the actual view rectangle.
	constants.settings[2] = 0; // Only the second denoiser pass applies final weighting and visibility scaling.
	constants.viewport[0] = width;
	constants.viewport[1] = height;
	constants.viewport[2] = 2.0f * width / r_refdef.vrect.width / vulkan_globals.projection_matrix[0];
	constants.viewport[3] = 2.0f * height / r_refdef.vrect.height / vulkan_globals.projection_matrix[5];
	constants.projection[0] = (-1.0f - 2.0f * r_refdef.vrect.x / r_refdef.vrect.width) / vulkan_globals.projection_matrix[0];
	constants.projection[1] = (-1.0f - 2.0f * (vid.height - glheight + r_refdef.vrect.y) / r_refdef.vrect.height) / vulkan_globals.projection_matrix[5];

	// The evaluator uses settings.w for quality; the composite gets its own debug constants.
	constants.settings[3] = (int)CLAMP (1, r_ssao.value, 3);
	// Evaluate AO and receiver edges.
	const VkDescriptorSet evaluate_sets[] = {working_read[SSAO_DEPTH_PYRAMID], working_read[SSAO_HILBERT], working_write[SSAO_AO], working_write[SSAO_EDGES]};
	vulkan_globals.vk_cmd_bind_pipeline (cb, VK_PIPELINE_BIND_POINT_COMPUTE, ssao_evaluate_pipeline.handle);
	vulkan_globals.vk_cmd_bind_descriptor_sets (
		cb, VK_PIPELINE_BIND_POINT_COMPUTE, ssao_evaluate_pipeline.layout.handle, 0, countof (evaluate_sets), evaluate_sets, 0, NULL);
	vulkan_globals.vk_cmd_push_constants (cb, ssao_evaluate_pipeline.layout.handle, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof (constants), &constants);
	vulkan_globals.vk_cmd_dispatch (cb, groups_x, groups_y, 1);
	vulkan_globals.vk_cmd_pipeline_barrier (
		cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &read_barrier, 0, NULL, 0, NULL);

	// Each denoiser invocation writes two horizontal pixels.
	const uint32_t		  filter_groups_x = (width + 15) / 16;
	const VkDescriptorSet filter_sets[] = {working_read[SSAO_AO], working_read[SSAO_EDGES], working_write[SSAO_FILTERED]};
	vulkan_globals.vk_cmd_bind_pipeline (cb, VK_PIPELINE_BIND_POINT_COMPUTE, ssao_filter_pipeline.handle);
	vulkan_globals.vk_cmd_bind_descriptor_sets (
		cb, VK_PIPELINE_BIND_POINT_COMPUTE, ssao_filter_pipeline.layout.handle, 0, countof (filter_sets), filter_sets, 0, NULL);
	vulkan_globals.vk_cmd_push_constants (cb, ssao_filter_pipeline.layout.handle, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof (constants), &constants);
	vulkan_globals.vk_cmd_dispatch (cb, filter_groups_x, groups_y, 1);
	// Finish reading the raw AO before reusing its image for the final output.
	const VkMemoryBarrier filter_barrier = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
		.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
		.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT};
	vulkan_globals.vk_cmd_pipeline_barrier (
		cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &filter_barrier, 0, NULL, 0, NULL);

	const VkDescriptorSet final_sets[] = {working_read[SSAO_FILTERED], working_read[SSAO_EDGES], working_write[SSAO_AO]};
	constants.settings[2] = 1; // Apply the final visibility scale.
	vulkan_globals.vk_cmd_bind_pipeline (cb, VK_PIPELINE_BIND_POINT_COMPUTE, ssao_filter_pipeline.handle);
	vulkan_globals.vk_cmd_bind_descriptor_sets (
		cb, VK_PIPELINE_BIND_POINT_COMPUTE, ssao_filter_pipeline.layout.handle, 0, countof (final_sets), final_sets, 0, NULL);
	vulkan_globals.vk_cmd_push_constants (cb, ssao_filter_pipeline.layout.handle, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof (constants), &constants);
	vulkan_globals.vk_cmd_dispatch (cb, filter_groups_x, groups_y, 1);
	// The composite render pass external dependency makes the final AO visible to fragment shaders.
}

void R_DrawSSAOTask (void *unused)
{
	if (r_ssao.value <= 0)
		return;
	cb_context_t  *cbx = vulkan_globals.secondary_cb_contexts[SCBX_ENTITY_SSAO];
	const VkRect2D rect = {{r_refdef.vrect.x, vid.height - glheight + r_refdef.vrect.y}, {r_refdef.vrect.width, r_refdef.vrect.height}};
	if (!rect.extent.width || !rect.extent.height)
		return;
	const VkViewport viewport = {rect.offset.x, rect.offset.y, rect.extent.width, rect.extent.height, 0, 1};
	vkCmdSetViewport (cbx->cb, 0, 1, &viewport);
	vkCmdSetScissor (cbx->cb, 0, 1, &rect);
	R_BindPipeline (cbx, VK_PIPELINE_BIND_POINT_GRAPHICS, ssao_pipelines[cbx->pipeline_variant]);
	const VkDescriptorSet sets[] = {descriptors[SSAO_ENTITY_MASK], descriptors[SSAO_SCENE_DEPTH], working_read[SSAO_AO], prepared_read};
	vulkan_globals.vk_cmd_bind_descriptor_sets (cbx->cb, VK_PIPELINE_BIND_POINT_GRAPHICS, ssao_layout.handle, 0, 4, sets, 0, NULL);
	const ssao_constants_t constants = R_SSAOConstants ();
	R_PushConstants (cbx, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof (constants), &constants);
	vulkan_globals.vk_cmd_draw (cbx->cb, 3, 1, 0, 0);
}
