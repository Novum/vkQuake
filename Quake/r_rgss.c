/* Full-frame rotated-grid supersampling. GPL-2.0-or-later. */
#include "quakedef.h"

static VkDescriptorSetLayout	empty_set_layout;
static vulkan_desc_set_layout_t jitter_set_layout;
static VkBuffer					jitter_buffer;
static vulkan_memory_t			jitter_memory;
static VkDescriptorSet			jitter_set;
static VkImage					sample_images;
static VkImageView				sample_images_view;
static vulkan_memory_t			sample_images_memory;
static VkImage					frame_image;
static VkDescriptorSet			resolve_set;

qboolean R_RGSSSupported (void)
{
	return vulkan_globals.device_properties.limits.maxBoundDescriptorSets > RGSS_JITTER_SET;
}

void R_InitRGSS (void)
{
	const VkDescriptorSetLayoutCreateInfo empty = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
	if (vkCreateDescriptorSetLayout (vulkan_globals.device, &empty, NULL, &empty_set_layout) != VK_SUCCESS)
		Sys_Error ("Couldn't create RGSS empty descriptor layout");
	const VkDescriptorSetLayoutBinding	  binding = {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, NULL};
	const VkDescriptorSetLayoutCreateInfo info = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .bindingCount = 1, .pBindings = &binding};
	jitter_set_layout.num_ubos = 1;
	if (vkCreateDescriptorSetLayout (vulkan_globals.device, &info, NULL, &jitter_set_layout.handle) != VK_SUCCESS)
		Sys_Error ("Couldn't create RGSS jitter descriptor layout");
	R_CreateBuffer (
		&jitter_buffer, &jitter_memory, 16, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0,
		&num_vulkan_misc_allocations, NULL, "RGSS jitter");
	jitter_set = R_AllocateDescriptorSet (&jitter_set_layout);
	const VkDescriptorBufferInfo buffer = {jitter_buffer, 0, 16};
	const VkWriteDescriptorSet	 write = {
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstSet = jitter_set,
		.dstBinding = 0,
		.descriptorCount = 1,
		.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		.pBufferInfo = &buffer};
	vkUpdateDescriptorSets (vulkan_globals.device, 1, &write, 0, NULL);

	const VkPushConstantRange		 range = {VK_SHADER_STAGE_FRAGMENT_BIT, 0, 12};
	const VkPipelineLayoutCreateInfo layout = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = 1,
		.pSetLayouts = &vulkan_globals.single_texture_set_layout.handle,
		.pushConstantRangeCount = 1,
		.pPushConstantRanges = &range};
	if (vkCreatePipelineLayout (vulkan_globals.device, &layout, NULL, &vulkan_globals.rgss_resolve_pipeline.layout.handle) != VK_SUCCESS)
		Sys_Error ("Couldn't create RGSS resolve pipeline layout");
	vulkan_globals.rgss_resolve_pipeline.layout.push_constant_range = range;
	vulkan_globals.rgss_resolve_pipeline.layout.mboit_input_attachment_set = -1;
}

VkResult R_CreateScenePipelineLayout (const VkPipelineLayoutCreateInfo *source, vulkan_pipeline_layout_t *layout)
{
	VkPipelineLayoutCreateInfo info = *source;
	VkPushConstantRange		   range = *info.pPushConstantRanges;
	VkDescriptorSetLayout	   sets[RGSS_JITTER_SET + 1];
	layout->rgss = R_RGSSSupported ();
	if (layout->rgss)
	{
		assert (info.setLayoutCount <= RGSS_JITTER_SET);
		for (uint32_t i = 0; i < RGSS_JITTER_SET; ++i)
			sets[i] = i < info.setLayoutCount ? info.pSetLayouts[i] : empty_set_layout;
		sets[RGSS_JITTER_SET] = jitter_set_layout.handle;
		info.setLayoutCount = countof (sets);
		info.pSetLayouts = sets;
		range.size = RGSS_CONSTANT_SIZE;
		info.pPushConstantRanges = &range;
	}
	layout->push_constant_range = range;
	return vkCreatePipelineLayout (vulkan_globals.device, &info, NULL, &layout->handle);
}

void R_BindRGSS (cb_context_t *cbx)
{
	if (!vulkan_globals.rgss_samples || !cbx->current_pipeline.layout.rgss)
		return;
	vkCmdBindDescriptorSets (cbx->cb, VK_PIPELINE_BIND_POINT_GRAPHICS, cbx->current_pipeline.layout.handle, RGSS_JITTER_SET, 1, &jitter_set, 0, NULL);
	R_PushConstants (cbx, VK_SHADER_STAGE_ALL_GRAPHICS, RGSS_VIEWPORT_OFFSET, sizeof (cbx->rgss_scale), cbx->rgss_scale);
}

void R_SetViewport (cb_context_t *cbx, const VkViewport *viewport)
{
	vkCmdSetViewport (cbx->cb, 0, 1, viewport);
	if (!vulkan_globals.rgss_samples)
		return;
	const qboolean ui = cbx->subpass_type == SUBPASS_UI || cbx->subpass_type == SUBPASS_POST_PROCESS;
	const float	   scale_x = ui ? 1.0f : (float)vid.render_width / vid.width;
	const float	   scale_y = ui ? 1.0f : (float)vid.render_height / vid.height;
	cbx->rgss_scale[0] = 2.0f * scale_x / viewport->width;
	cbx->rgss_scale[1] = 2.0f * scale_y / viewport->height;
	if (vulkan_globals.rgss_samples && cbx->current_pipeline.layout.rgss)
		R_PushConstants (cbx, VK_SHADER_STAGE_ALL_GRAPHICS, RGSS_VIEWPORT_OFFSET, sizeof (cbx->rgss_scale), cbx->rgss_scale);
}

void R_CreateRGSSImages (VkImage output_image)
{
	if (!vulkan_globals.rgss_samples)
		return;
	frame_image = output_image;
	const VkImageCreateInfo info = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.imageType = VK_IMAGE_TYPE_2D,
		.format = vulkan_globals.color_format,
		.extent = {vid.width, vid.height, 1},
		.mipLevels = 1,
		.arrayLayers = vulkan_globals.rgss_samples,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.tiling = VK_IMAGE_TILING_OPTIMAL,
		.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE};
	if (vkCreateImage (vulkan_globals.device, &info, NULL, &sample_images) != VK_SUCCESS)
		Sys_Error ("Couldn't create RGSS frame images");
	GL_SetObjectName ((uint64_t)sample_images, VK_OBJECT_TYPE_IMAGE, "RGSS complete frames");
	VkMemoryRequirements requirements;
	vkGetImageMemoryRequirements (vulkan_globals.device, sample_images, &requirements);
	VkMemoryAllocateInfo allocation = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		.allocationSize = requirements.size,
		.memoryTypeIndex = GL_MemoryTypeFromProperties (requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0)};
	R_AllocateVulkanMemory (&sample_images_memory, &allocation, VULKAN_MEMORY_TYPE_DEVICE, &num_vulkan_misc_allocations);
	if (vkBindImageMemory (vulkan_globals.device, sample_images, sample_images_memory.handle, 0) != VK_SUCCESS)
		Sys_Error ("Couldn't bind RGSS frame memory");
	const VkImageViewCreateInfo view = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.image = sample_images,
		.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY,
		.format = info.format,
		.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, info.arrayLayers}};
	if (vkCreateImageView (vulkan_globals.device, &view, NULL, &sample_images_view) != VK_SUCCESS)
		Sys_Error ("Couldn't create RGSS frame image view");
	resolve_set = R_AllocateDescriptorSet (&vulkan_globals.single_texture_set_layout);
	const VkDescriptorImageInfo image = {vulkan_globals.point_sampler, sample_images_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
	const VkWriteDescriptorSet	write = {
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstSet = resolve_set,
		.dstBinding = 0,
		.descriptorCount = 1,
		.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		.pImageInfo = &image};
	vkUpdateDescriptorSets (vulkan_globals.device, 1, &write, 0, NULL);
}

void R_DestroyRGSSImages (void)
{
	if (!sample_images)
		return;
	R_FreeDescriptorSet (resolve_set, &vulkan_globals.single_texture_set_layout);
	vkDestroyImageView (vulkan_globals.device, sample_images_view, NULL);
	vkDestroyImage (vulkan_globals.device, sample_images, NULL);
	R_FreeVulkanMemory (&sample_images_memory, &num_vulkan_misc_allocations);
	sample_images = VK_NULL_HANDLE;
	sample_images_view = VK_NULL_HANDLE;
	resolve_set = VK_NULL_HANDLE;
	frame_image = VK_NULL_HANDLE;
}

void R_RGSSBeginSample (cb_context_t *cbx, uint32_t sample)
{
	const VkCommandBuffer commands = cbx->cb;
	// Replaying the frame reuses depth, transparency, SSAO, and UI attachments.
	// Complete their previous uses before overwriting the sample's shared resources.
	const VkMemoryBarrier before = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
		.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
		.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT};
	vkCmdPipelineBarrier (commands, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &before, 0, NULL, 0, NULL);
	if (sample == 0)
	{
		const VkImageMemoryBarrier image = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
			.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.image = sample_images,
			.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, vulkan_globals.rgss_samples}};
		vkCmdPipelineBarrier (commands, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &image);
	}
	float jitter[4] = {0};
	RGSS_Jitter (vulkan_globals.rgss_samples, sample, &jitter[0], &jitter[1]);
	memcpy (cbx->rgss_jitter, jitter, sizeof (cbx->rgss_jitter));
	vkCmdUpdateBuffer (commands, jitter_buffer, 0, sizeof (jitter), jitter);
	const VkMemoryBarrier after = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT, .dstAccessMask = VK_ACCESS_UNIFORM_READ_BIT};
	vkCmdPipelineBarrier (commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, 0, 1, &after, 0, NULL, 0, NULL);
}

void R_RGSSEndSample (VkCommandBuffer commands, uint32_t sample)
{
	VkImageMemoryBarrier source = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
		.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
		.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.image = frame_image,
		.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
	vkCmdPipelineBarrier (commands, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &source);
	const VkImageCopy region = {
		.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
		.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, sample, 1},
		.extent = {vid.width, vid.height, 1}};
	vkCmdCopyImage (commands, frame_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, sample_images, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
	source.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
	source.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	source.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	source.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	vkCmdPipelineBarrier (commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, NULL, 0, NULL, 1, &source);
	if (sample + 1 == vulkan_globals.rgss_samples)
	{
		const VkImageMemoryBarrier images = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
			.dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
			.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.image = sample_images,
			.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, vulkan_globals.rgss_samples}};
		vkCmdPipelineBarrier (commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, NULL, 0, NULL, 1, &images);
	}
}

void R_DrawRGSSResolve (cb_context_t *cbx, float gamma, float contrast)
{
	const struct
	{
		float	 gamma, contrast;
		uint32_t samples;
	} constants = {gamma, contrast, vulkan_globals.rgss_samples};
	R_BindPipeline (cbx, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_globals.rgss_resolve_pipeline);
	vkCmdBindDescriptorSets (cbx->cb, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_globals.rgss_resolve_pipeline.layout.handle, 0, 1, &resolve_set, 0, NULL);
	R_PushConstants (cbx, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof (constants), &constants);
	vkCmdDraw (cbx->cb, 3, 1, 0, 0);
}
