/*
Copyright (C) 2026 Axel Gneiting

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.
*/

#include "quakedef.h"

#define MAX_FRAME_STEPS		 64
#define MAX_PASS_ATTACHMENTS 8
#define MAX_FRAME_PASSES	 32
#define MAX_PASS_SUBPASSES	 16

typedef enum
{
	FRAME_BEGIN_GRAPHICS,
	FRAME_NEXT_SUBPASS,
	FRAME_GRAPHICS_WORK,
	FRAME_RECORD_WORK,
	FRAME_END_GRAPHICS,
	FRAME_PREPARED_COMMANDS,
} frame_step_type_t;

typedef enum
{
	FRAME_TARGET_SCENE,
	FRAME_TARGET_UI,
} frame_target_t;

typedef struct
{
	cb_context_t		  *cbx;
	end_rendering_parms_t *parms;
	bool				   screen_effects;
	void (*record_readback) (void *);
	void *readback_data;
} frame_record_context_t;

typedef void (*frame_recorder_t) (const frame_record_context_t *context);

static void R_RecordScreenEffects (const frame_record_context_t *context);
static void R_RecordReadback (const frame_record_context_t *context);

// Draw-stage identity is independent of attachment use and graphics grouping.
typedef enum
{
	DRAW_WORLD,
	DRAW_ENTITIES,
	DRAW_POST_ENTITIES,
	DRAW_TRANSPARENCY,
	DRAW_TRANSPARENCY_COMPOSITE,
	DRAW_OIT_RESOLVE,
	DRAW_BLENDED_PARTICLES,
	DRAW_UI,
	DRAW_POST_PROCESS,
} draw_stage_t;

typedef struct
{
	frame_step_type_t type;
	frame_recorder_t  recorder;
	uint32_t		  pass;
	uint32_t		  subpass;
	draw_stage_t	  draw_stage;
	int				  first_context;
	int				  last_context;
} frame_step_t;

typedef struct
{
	frame_target_t target;
	uint32_t	   subpass_count;
	subpass_type_t subpasses[MAX_PASS_SUBPASSES];
} graphics_pass_desc_t;

typedef struct
{
	uint32_t			 step_count;
	uint32_t			 pass_count;
	frame_step_t		 steps[MAX_FRAME_STEPS];
	graphics_pass_desc_t passes[MAX_FRAME_PASSES];
} frame_desc_t;

typedef struct
{
	VkFormat			  color_format;
	VkFormat			  depth_format;
	VkFormat			  swapchain_format;
	VkSampleCountFlagBits samples;
	frame_desc_t		  variants[MAIN_RENDER_PASS_VARIANT_COUNT];
} frame_layout_t;

typedef struct
{
	frame_desc_t *desc;
	bool		  graphics_active;
} frame_builder_t;

typedef struct
{
	VkRenderPass		  handles[MAIN_RENDER_PASS_STENCIL_COUNT];
	uint32_t			  attachment_count;
	VkFramebuffer		 *framebuffers;
	uint32_t			  framebuffer_count;
	render_pass_binding_t bindings[MAX_PASS_SUBPASSES];
} physical_pass_t;

static frame_layout_t  pending_layout;
static frame_layout_t  current_layout;
static physical_pass_t physical_passes[MAIN_RENDER_PASS_VARIANT_COUNT][MAX_FRAME_PASSES];

extern cvar_t					  r_usesops;
extern VkAccelerationStructureKHR bmodel_tlas;

static frame_step_t *R_AddFrameStep (frame_builder_t *builder, frame_step_type_t type)
{
	frame_desc_t *desc = builder->desc;
	if (desc->step_count == countof (desc->steps))
		Sys_Error ("Too many frame steps");
	frame_step_t *step = &desc->steps[desc->step_count++];
	step->type = type;
	step->pass = desc->pass_count ? desc->pass_count - 1 : 0;
	step->subpass = desc->pass_count && desc->passes[step->pass].subpass_count ? desc->passes[step->pass].subpass_count - 1 : 0;
	return step;
}

static void R_AddSubpass (frame_builder_t *builder, subpass_type_t type)
{
	graphics_pass_desc_t *pass = &builder->desc->passes[builder->desc->pass_count - 1];
	if (pass->subpass_count == countof (pass->subpasses))
		Sys_Error ("Too many subpasses");
	pass->subpasses[pass->subpass_count++] = type;
}

static void R_BeginGraphicsPass (frame_builder_t *builder, frame_target_t target, subpass_type_t type)
{
	assert (!builder->graphics_active);
	frame_desc_t *desc = builder->desc;
	if (desc->pass_count == countof (desc->passes))
		Sys_Error ("Too many graphics passes");
	desc->passes[desc->pass_count++].target = target;
	R_AddSubpass (builder, type);
	R_AddFrameStep (builder, FRAME_BEGIN_GRAPHICS);
	builder->graphics_active = true;
}

static void R_NextSubpass (frame_builder_t *builder, subpass_type_t type)
{
	assert (builder->graphics_active);
	R_AddSubpass (builder, type);
	R_AddFrameStep (builder, FRAME_NEXT_SUBPASS);
}

static void R_AddGraphicsWork (frame_builder_t *builder, draw_stage_t stage, int first, int last)
{
	assert (builder->graphics_active);
	for (uint32_t i = 0; i < builder->desc->step_count; ++i)
		if (builder->desc->steps[i].type == FRAME_GRAPHICS_WORK && builder->desc->steps[i].draw_stage == stage)
			Sys_Error ("Draw stage %d declared twice", stage);
	frame_step_t *step = R_AddFrameStep (builder, FRAME_GRAPHICS_WORK);
	step->draw_stage = stage;
	step->first_context = first;
	step->last_context = last;
}

static void R_EndGraphicsPass (frame_builder_t *builder)
{
	assert (builder->graphics_active);
	R_AddFrameStep (builder, FRAME_END_GRAPHICS);
	builder->graphics_active = false;
}

static void R_AddRecordWork (frame_builder_t *builder, frame_recorder_t recorder)
{
	assert (!builder->graphics_active);
	assert (recorder);
	R_AddFrameStep (builder, FRAME_RECORD_WORK)->recorder = recorder;
}

static void R_AddPreparedCommands (frame_builder_t *builder, int context)
{
	assert (!builder->graphics_active);
	R_AddFrameStep (builder, FRAME_PREPARED_COMMANDS)->first_context = context;
}

// This is the sole declaration of frame order and graphics grouping.
// Resource creation and command execution both consume its result.
static void R_DescribeFrame (frame_desc_t *desc, main_render_pass_variant_t variant)
{
	frame_builder_t builder = {.desc = desc};
	const bool		use_oit = variant != MAIN_RENDER_PASS_STANDARD;

	R_AddPreparedCommands (&builder, PCBX_BUILD_ACCELERATION_STRUCTURES);
	R_AddPreparedCommands (&builder, PCBX_UPDATE_LIGHTMAPS);
	R_AddPreparedCommands (&builder, PCBX_UPDATE_WARP);

	R_BeginGraphicsPass (&builder, FRAME_TARGET_SCENE, SUBPASS_MAIN);
	R_AddGraphicsWork (&builder, DRAW_WORLD, SCBX_WORLD, SCBX_WORLD);
	R_AddGraphicsWork (&builder, DRAW_ENTITIES, SCBX_ENTITIES, SCBX_ENTITIES);
	R_AddGraphicsWork (&builder, DRAW_POST_ENTITIES, SCBX_SKY, SCBX_VIEW_MODEL);
	if (!use_oit)
		R_AddGraphicsWork (&builder, DRAW_TRANSPARENCY, SCBX_FTE_PARTICLES_BLEND, SCBX_MAIN_PASS_LAST);

	if (variant == MAIN_RENDER_PASS_OIT)
	{
		R_NextSubpass (&builder, SUBPASS_WBOIT);
		R_AddGraphicsWork (&builder, DRAW_TRANSPARENCY, SCBX_ALPHA_ENTITIES_ACROSS_WATER, SCBX_MAIN_PASS_LAST);
		R_NextSubpass (&builder, SUBPASS_OIT_RESOLVE);
		R_AddGraphicsWork (&builder, DRAW_OIT_RESOLVE, SCBX_OIT_RESOLVE, SCBX_OIT_RESOLVE);
		R_AddGraphicsWork (&builder, DRAW_BLENDED_PARTICLES, SCBX_FTE_PARTICLES_BLEND, SCBX_FTE_PARTICLES_BLEND);
	}
	else if (variant == MAIN_RENDER_PASS_MBOIT)
	{
		R_NextSubpass (&builder, SUBPASS_MBOIT_MOMENTS);
		R_AddGraphicsWork (&builder, DRAW_TRANSPARENCY, SCBX_ALPHA_ENTITIES_ACROSS_WATER, SCBX_MAIN_PASS_LAST);
		R_NextSubpass (&builder, SUBPASS_MBOIT_COMPOSITE);
		R_AddGraphicsWork (&builder, DRAW_TRANSPARENCY_COMPOSITE, SCBX_MBOIT_COMPOSITE_PASS_FIRST, SCBX_MBOIT_COMPOSITE_PASS_LAST);
		R_NextSubpass (&builder, SUBPASS_OIT_RESOLVE);
		R_AddGraphicsWork (&builder, DRAW_OIT_RESOLVE, SCBX_OIT_RESOLVE, SCBX_OIT_RESOLVE);
		R_AddGraphicsWork (&builder, DRAW_BLENDED_PARTICLES, SCBX_FTE_PARTICLES_BLEND, SCBX_FTE_PARTICLES_BLEND);
	}
	R_EndGraphicsPass (&builder);

	// When disabled, this records the scene-to-GUI memory barrier instead.
	// Runtime effect parameters do not change graphics pipeline compatibility.
	R_AddRecordWork (&builder, R_RecordScreenEffects);

	R_BeginGraphicsPass (&builder, FRAME_TARGET_UI, SUBPASS_UI);
	R_AddGraphicsWork (&builder, DRAW_UI, SCBX_GUI, SCBX_GUI);
	R_NextSubpass (&builder, SUBPASS_POST_PROCESS);
	R_AddGraphicsWork (&builder, DRAW_POST_PROCESS, SCBX_POST_PROCESS, SCBX_POST_PROCESS);
	R_EndGraphicsPass (&builder);
	R_AddRecordWork (&builder, R_RecordReadback);
}

bool R_SetupRenderPasses (void)
{
	memset (&pending_layout, 0, sizeof (pending_layout));
	pending_layout.color_format = vulkan_globals.color_format;
	pending_layout.depth_format = vulkan_globals.depth_format;
	pending_layout.swapchain_format = vulkan_globals.swap_chain_format;
	pending_layout.samples = vulkan_globals.sample_count;
	for (int variant = 0; variant < MAIN_RENDER_PASS_VARIANT_COUNT; ++variant)
		R_DescribeFrame (&pending_layout.variants[variant], variant);

	// Do not publish this while the previous frame may still be recording.
	// R_CreateRenderPasses commits it after the restart has synchronized.
	return memcmp (&pending_layout, &current_layout, sizeof (pending_layout)) != 0;
}

static void R_MarkAttachments (const VkSubpassDescription *subpass, bool *used)
{
	for (uint32_t i = 0; i < subpass->colorAttachmentCount; ++i)
	{
		if (subpass->pColorAttachments[i].attachment != VK_ATTACHMENT_UNUSED)
			used[subpass->pColorAttachments[i].attachment] = true;
		if (subpass->pResolveAttachments && subpass->pResolveAttachments[i].attachment != VK_ATTACHMENT_UNUSED)
			used[subpass->pResolveAttachments[i].attachment] = true;
	}
	for (uint32_t i = 0; i < subpass->inputAttachmentCount; ++i)
		if (subpass->pInputAttachments[i].attachment != VK_ATTACHMENT_UNUSED)
			used[subpass->pInputAttachments[i].attachment] = true;
	if (subpass->pDepthStencilAttachment && subpass->pDepthStencilAttachment->attachment != VK_ATTACHMENT_UNUSED)
		used[subpass->pDepthStencilAttachment->attachment] = true;
}

// Subpass types describe attachment use, not draw-stage identity or grouping.
static void R_CreateGraphicsPasses (
	main_render_pass_variant_t variant, frame_target_t target, const VkAttachmentDescription *attachments, uint32_t attachment_count,
	const VkSubpassDescription *stage_definitions)
{
	const frame_desc_t *frame = &current_layout.variants[variant];
	bool				used_before[MAX_PASS_ATTACHMENTS] = {0};
	assert (attachment_count <= MAX_PASS_ATTACHMENTS);

	for (uint32_t pass_index = 0; pass_index < frame->pass_count; ++pass_index)
	{
		const graphics_pass_desc_t *desc = &frame->passes[pass_index];
		if (desc->target != target)
			continue;
		physical_pass_t *physical = &physical_passes[variant][pass_index];
		physical->attachment_count = attachment_count;

		VkAttachmentDescription pass_attachments[MAX_PASS_ATTACHMENTS];
		memcpy (pass_attachments, attachments, attachment_count * sizeof (*attachments));
		VkSubpassDescription subpasses[MAX_PASS_SUBPASSES];
		bool				 used_here[MAX_PASS_ATTACHMENTS] = {0};
		bool				 continues = false;
		for (uint32_t later = pass_index + 1; later < frame->pass_count; ++later)
			continues |= frame->passes[later].target == target;

		for (uint32_t i = 0; i < desc->subpass_count; ++i)
		{
			subpasses[i] = stage_definitions[desc->subpasses[i]];
			R_MarkAttachments (&subpasses[i], used_here);
		}
		for (uint32_t i = 0; i < attachment_count; ++i)
		{
			if (used_before[i])
			{
				pass_attachments[i].initialLayout = attachments[i].finalLayout;
				pass_attachments[i].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
				pass_attachments[i].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
			}
			if (continues)
			{
				pass_attachments[i].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
				pass_attachments[i].stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
			}
		}

		// External dependencies cover preceding graphics/compute/transfer work.
		// Internal dependencies cover every earlier subpass, including writers
		// separated from their consumers by preserve-only subpasses.
		VkSubpassDependency		   dependencies[MAX_PASS_SUBPASSES * (MAX_PASS_SUBPASSES + 1) / 2];
		uint32_t				   dependency_count = 0;
		const VkPipelineStageFlags graphics_stages = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
													 VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		const VkAccessFlags graphics_access = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
											  VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
											  VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;
		for (uint32_t dst = 0; dst < desc->subpass_count; ++dst)
		{
			dependencies[dependency_count++] = (VkSubpassDependency){
				.srcSubpass = VK_SUBPASS_EXTERNAL,
				.dstSubpass = dst,
				.srcStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
				.dstStageMask = graphics_stages,
				.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT,
				.dstAccessMask = graphics_access,
			};
			for (uint32_t src = 0; src < dst; ++src)
			{
				dependencies[dependency_count++] = (VkSubpassDependency){
					.srcSubpass = src,
					.dstSubpass = dst,
					.srcStageMask = graphics_stages,
					.dstStageMask = graphics_stages,
					.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
					.dstAccessMask = graphics_access,
					.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT,
				};
			}
		}
		for (int stencil = 0; stencil < MAIN_RENDER_PASS_STENCIL_COUNT; ++stencil)
		{
			if (target == FRAME_TARGET_SCENE && !used_before[1] && stencil == MAIN_RENDER_PASS_NO_STENCIL)
				pass_attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			const VkRenderPassCreateInfo info = {
				.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
				.attachmentCount = attachment_count,
				.pAttachments = pass_attachments,
				.subpassCount = desc->subpass_count,
				.pSubpasses = subpasses,
				.dependencyCount = dependency_count,
				.pDependencies = dependencies,
			};
			const VkResult result = vkCreateRenderPass (vulkan_globals.device, &info, NULL, &physical->handles[stencil]);
			if (result != VK_SUCCESS)
				Sys_Error ("Couldn't create render pass: %d", result);
			GL_SetObjectName ((uint64_t)physical->handles[stencil], VK_OBJECT_TYPE_RENDER_PASS, target == FRAME_TARGET_UI ? "ui" : "scene");
			for (uint32_t subpass = 0; subpass < desc->subpass_count; ++subpass)
			{
				render_pass_binding_t *binding = &physical->bindings[subpass];
				binding->render_pass[stencil] = physical->handles[stencil];
				binding->subpass = subpass;
				binding->segment = pass_index;
			}
		}
		for (uint32_t i = 0; i < attachment_count; ++i)
			used_before[i] |= used_here[i];
	}
}

void R_DestroyFrameBuffers (void)
{
	for (int variant = 0; variant < MAIN_RENDER_PASS_VARIANT_COUNT; ++variant)
		for (uint32_t pass = 0; pass < MAX_FRAME_PASSES; ++pass)
		{
			physical_pass_t *physical = &physical_passes[variant][pass];
			for (uint32_t i = 0; i < physical->framebuffer_count; ++i)
				vkDestroyFramebuffer (vulkan_globals.device, physical->framebuffers[i], NULL);
			free (physical->framebuffers);
			physical->framebuffers = NULL;
			physical->framebuffer_count = 0;
		}
}

void R_DestroyRenderPasses (void)
{
	for (int variant = 0; variant < MAIN_RENDER_PASS_VARIANT_COUNT; ++variant)
		for (uint32_t pass = 0; pass < MAX_FRAME_PASSES; ++pass)
			for (int stencil = 0; stencil < MAIN_RENDER_PASS_STENCIL_COUNT; ++stencil)
			{
				VkRenderPass *handle = &physical_passes[variant][pass].handles[stencil];
				if (*handle != VK_NULL_HANDLE)
					vkDestroyRenderPass (vulkan_globals.device, *handle, NULL);
				*handle = VK_NULL_HANDLE;
			}
	for (int context = 0; context < SCBX_NUM; ++context)
		for (int i = 0; i < SECONDARY_CB_MULTIPLICITY[context]; ++i)
			vulkan_globals.secondary_cb_contexts[context][i].render_pass = VK_NULL_HANDLE;
}

void R_CreateFrameBuffers (const render_framebuffer_images_t *images)
{
	const main_render_pass_variant_t variant = R_UseMBOIT () ? MAIN_RENDER_PASS_MBOIT : R_UseWBOIT () ? MAIN_RENDER_PASS_OIT : MAIN_RENDER_PASS_STANDARD;
	const frame_desc_t				*frame = &current_layout.variants[variant];
	const bool						 msaa = current_layout.samples != VK_SAMPLE_COUNT_1_BIT;
	for (uint32_t pass = 0; pass < frame->pass_count; ++pass)
	{
		const bool		 ui = frame->passes[pass].target == FRAME_TARGET_UI;
		physical_pass_t *physical = &physical_passes[variant][pass];
		assert (!physical->framebuffers);
		physical->framebuffer_count = ui ? images->swapchain_count : NUM_COLOR_BUFFERS;
		physical->framebuffers = calloc (physical->framebuffer_count, sizeof (*physical->framebuffers));
		if (!physical->framebuffers)
			Sys_Error ("Couldn't allocate framebuffers");
		for (uint32_t i = 0; i < physical->framebuffer_count; ++i)
		{
			VkImageView attachments[MAX_PASS_ATTACHMENTS] = {0};
			if (ui)
			{
				attachments[0] = images->color[0];
				attachments[1] = images->swapchain[i];
			}
			else
			{
				attachments[0] = images->color[i];
				attachments[1] = images->depth;
				uint32_t next = 2;
				if (msaa)
					attachments[next++] = images->msaa_color;
				if (variant == MAIN_RENDER_PASS_OIT)
				{
					attachments[next++] = images->oit_accum;
					attachments[next++] = images->oit_reveal;
				}
				else if (variant == MAIN_RENDER_PASS_MBOIT)
				{
					attachments[next++] = images->mboit_b0;
					attachments[next++] = images->mboit_moments;
					attachments[next++] = images->mboit_color;
				}
			}
			const VkFramebufferCreateInfo info = {
				.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
				.renderPass = physical->handles[MAIN_RENDER_PASS_STENCIL_CLEAR],
				.attachmentCount = physical->attachment_count,
				.pAttachments = attachments,
				.width = images->width,
				.height = images->height,
				.layers = 1,
			};
			const VkResult result = vkCreateFramebuffer (vulkan_globals.device, &info, NULL, &physical->framebuffers[i]);
			if (result != VK_SUCCESS)
				Sys_Error ("Couldn't create framebuffer: %d", result);
		}
	}
}

typedef struct screen_effect_constants_s
{
	uint32_t clamp_size_x;
	uint32_t clamp_size_y;
	float	 screen_size_rcp_x;
	float	 screen_size_rcp_y;
	float	 aspect_ratio;
	float	 time;
	uint32_t flags;
	float	 poly_blend_r;
	float	 poly_blend_g;
	float	 poly_blend_b;
	float	 poly_blend_a;
} screen_effect_constants_t;

typedef struct ray_debug_constants_s
{
	float screen_size_rcp_x;
	float screen_size_rcp_y;
	float aspect_ratio;
	float origin_x;
	float origin_y;
	float origin_z;
	float forward_x;
	float forward_y;
	float forward_z;
	float right_x;
	float right_y;
	float right_z;
	float down_x;
	float down_y;
	float down_z;
} ray_debug_constants_t;

#define SCREEN_EFFECT_FLAG_SCALE_MASK 0x3
#define SCREEN_EFFECT_FLAG_SCALE_2X	  0x1
#define SCREEN_EFFECT_FLAG_SCALE_4X	  0x2
#define SCREEN_EFFECT_FLAG_SCALE_8X	  0x3
#define SCREEN_EFFECT_FLAG_WATER_WARP 0x4
#define SCREEN_EFFECT_FLAG_PALETTIZE  0x8
#define SCREEN_EFFECT_FLAG_MENU		  0x10

/*
===============
R_ScreenEffects
===============
*/
static void R_ScreenEffects (cb_context_t *cbx, qboolean enabled, end_rendering_parms_t *parms)
{
	if (enabled)
	{
		R_BeginDebugUtilsLabel (cbx, "Screen Effects");

		VkImageMemoryBarrier image_barriers[2];
		image_barriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		image_barriers[0].pNext = NULL;
		image_barriers[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		image_barriers[0].dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		image_barriers[0].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		image_barriers[0].newLayout = VK_IMAGE_LAYOUT_GENERAL;
		image_barriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		image_barriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		image_barriers[0].image = vulkan_globals.color_buffers[0];
		image_barriers[0].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		image_barriers[0].subresourceRange.baseMipLevel = 0;
		image_barriers[0].subresourceRange.levelCount = 1;
		image_barriers[0].subresourceRange.baseArrayLayer = 0;
		image_barriers[0].subresourceRange.layerCount = 1;

		image_barriers[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		image_barriers[1].pNext = NULL;
		image_barriers[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		image_barriers[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		image_barriers[1].oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		image_barriers[1].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		image_barriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		image_barriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		image_barriers[1].image = vulkan_globals.color_buffers[1];
		image_barriers[1].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		image_barriers[1].subresourceRange.baseMipLevel = 0;
		image_barriers[1].subresourceRange.levelCount = 1;
		image_barriers[1].subresourceRange.baseArrayLayer = 0;
		image_barriers[1].subresourceRange.layerCount = 1;

		vkCmdPipelineBarrier (
			cbx->cb, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, NULL, 0, NULL, 2, image_barriers);

		GL_SetCanvas (cbx, CANVAS_NONE); // Invalidate canvas so push constants get set later

		vulkan_pipeline_t *pipeline = NULL;
#if defined(_DEBUG)
		if (parms->ray_debug)
		{
			pipeline = &vulkan_globals.ray_debug_pipeline;
		}
		else
#endif
			if (parms->render_scale >= 2)
		{
			if (vulkan_globals.screen_effects_sops && r_usesops.value)
				pipeline = &vulkan_globals.screen_effects_scale_sops_pipeline;
			else
				pipeline = &vulkan_globals.screen_effects_scale_pipeline;
		}
		else
			pipeline = &vulkan_globals.screen_effects_pipeline;

		R_BindPipeline (cbx, VK_PIPELINE_BIND_POINT_COMPUTE, *pipeline);

#if defined(_DEBUG)
		if (!parms->ray_debug || !bmodel_tlas)
#endif
		{
			vkCmdBindDescriptorSets (cbx->cb, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->layout.handle, 0, 1, &vulkan_globals.screen_effects_desc_set, 0, NULL);

			uint32_t screen_effect_flags = 0;
			if (parms->render_warp)
				screen_effect_flags |= SCREEN_EFFECT_FLAG_WATER_WARP;
			if (parms->render_scale >= 8)
				screen_effect_flags |= SCREEN_EFFECT_FLAG_SCALE_8X;
			else if (parms->render_scale >= 4)
				screen_effect_flags |= SCREEN_EFFECT_FLAG_SCALE_4X;
			else if (parms->render_scale >= 2)
				screen_effect_flags |= SCREEN_EFFECT_FLAG_SCALE_2X;
			if (parms->vid_palettize)
				screen_effect_flags |= SCREEN_EFFECT_FLAG_PALETTIZE;
			if (parms->menu)
				screen_effect_flags |= SCREEN_EFFECT_FLAG_MENU;

			const screen_effect_constants_t push_constants = {
				parms->vid_width - 1,
				parms->vid_height - 1,
				1.0f / (float)parms->vid_width,
				1.0f / (float)parms->vid_height,
				(float)parms->vid_width / (float)parms->vid_height,
				parms->time,
				screen_effect_flags,
				(float)parms->v_blend[0] / 255.0f,
				(float)parms->v_blend[1] / 255.0f,
				(float)parms->v_blend[2] / 255.0f,
				(float)parms->v_blend[3] / 255.0f,
			};
			R_PushConstants (cbx, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof (push_constants), &push_constants);
		}
#if defined(_DEBUG)
		else
		{
			vkCmdBindDescriptorSets (cbx->cb, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->layout.handle, 0, 1, &vulkan_globals.ray_debug_desc_set, 0, NULL);

			ZEROED_STRUCT (VkWriteDescriptorSetAccelerationStructureKHR, tlas_info);
			tlas_info.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
			tlas_info.accelerationStructureCount = 1;
			tlas_info.pAccelerationStructures = &bmodel_tlas;

			ZEROED_STRUCT (VkWriteDescriptorSet, tlas_write);
			tlas_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			tlas_write.pNext = &tlas_info;
			tlas_write.dstBinding = 0;
			tlas_write.descriptorCount = 1;
			tlas_write.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;

			vulkan_globals.vk_cmd_push_descriptor_set (cbx->cb, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->layout.handle, 1, 1, &tlas_write);

			const ray_debug_constants_t push_constants = {
				1.0f / (float)parms->vid_width,
				1.0f / (float)parms->vid_height,
				(float)parms->vid_width / (float)parms->vid_height,
				parms->origin[0],
				parms->origin[1],
				parms->origin[2],
				parms->forward[0],
				parms->forward[1],
				parms->forward[2],
				parms->right[0],
				parms->right[1],
				parms->right[2],
				parms->down[0],
				parms->down[1],
				parms->down[2],
			};
			R_PushConstants (cbx, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof (push_constants), &push_constants);
		}
#endif

		vkCmdDispatch (cbx->cb, (parms->vid_width + 7) / 8, (parms->vid_height + 7) / 8, 1);

		image_barriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		image_barriers[0].pNext = NULL;
		image_barriers[0].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		image_barriers[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		image_barriers[0].oldLayout = VK_IMAGE_LAYOUT_GENERAL;
		image_barriers[0].newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		image_barriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		image_barriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		image_barriers[0].image = vulkan_globals.color_buffers[0];
		image_barriers[0].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		image_barriers[0].subresourceRange.baseMipLevel = 0;
		image_barriers[0].subresourceRange.levelCount = 1;
		image_barriers[0].subresourceRange.baseArrayLayer = 0;
		image_barriers[0].subresourceRange.layerCount = 1;

		vkCmdPipelineBarrier (
			cbx->cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, NULL, 0, NULL, 1, image_barriers);

		R_EndDebugUtilsLabel (cbx);
	}
	else
	{
		VkMemoryBarrier memory_barrier;
		memory_barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
		memory_barrier.pNext = NULL;
		memory_barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		memory_barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

		vkCmdPipelineBarrier (
			cbx->cb, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 1, &memory_barrier, 0, NULL, 0, NULL);
	}
}

static void R_RecordScreenEffects (const frame_record_context_t *context)
{
	R_ScreenEffects (context->cbx, context->screen_effects, context->parms);
}

static void R_RecordReadback (const frame_record_context_t *context)
{
	if (context->record_readback)
		context->record_readback (context->readback_data);
}

static void R_SubmitContexts (VkCommandBuffer command_buffer, int first_context, int last_context)
{
	for (int scbx_index = first_context; scbx_index <= last_context; ++scbx_index)
	{
		for (int i = 0; i < SECONDARY_CB_MULTIPLICITY[scbx_index]; ++i)
			vkCmdExecuteCommands (command_buffer, 1, &vulkan_globals.secondary_cb_contexts[scbx_index][i].cb);
	}
}

typedef struct
{
	subpass_type_t				 type;
	const render_pass_binding_t *pass;
} context_binding_t;

static context_binding_t context_bindings[MAIN_RENDER_PASS_VARIANT_COUNT][SCBX_NUM];

void R_ConfigureRenderContext (cb_context_t *cbx, int context, main_render_pass_variant_t variant, main_render_pass_stencil_t stencil)
{
	assert (context >= 0 && context < SCBX_NUM);
	assert (variant >= 0 && variant < MAIN_RENDER_PASS_VARIANT_COUNT);
	const context_binding_t *binding = &context_bindings[variant][context];
	cbx->subpass_type = binding->type;
	cbx->pipeline_variant = variant;
	cbx->render_pass = binding->pass->render_pass[stencil];
	cbx->subpass = binding->pass->subpass;
}

static void R_BindFrameContexts (main_render_pass_variant_t variant)
{
	const frame_desc_t *frame = &current_layout.variants[variant];
	// Inactive contexts are still recorded, but never submitted.
	for (int context = 0; context < SCBX_NUM; ++context)
		context_bindings[variant][context] = (context_binding_t){SUBPASS_MAIN, R_RenderPassBinding (SUBPASS_MAIN, variant)};
	for (uint32_t i = 0; i < frame->step_count; ++i)
	{
		const frame_step_t *step = &frame->steps[i];
		if (step->type != FRAME_GRAPHICS_WORK)
			continue;
		for (int context = step->first_context; context <= step->last_context; ++context)
			context_bindings[variant][context] = (context_binding_t){
				frame->passes[step->pass].subpasses[step->subpass],
				&physical_passes[variant][step->pass].bindings[step->subpass],
			};
	}
}

// Execute only the already-compiled description. Late frame values such as
// clear colors, swapchain image indices and compute constants are not topology.
uint32_t R_RecordFrame (
	end_rendering_parms_t *parms, uint32_t swapchain_index, VkCommandBuffer *submit_buffers, uint32_t submit_capacity, void (*record_readback) (void *),
	void *readback_data)
{
	const main_render_pass_variant_t variant = parms->use_mboit ? MAIN_RENDER_PASS_MBOIT : parms->use_oit ? MAIN_RENDER_PASS_OIT : MAIN_RENDER_PASS_STANDARD;
	const frame_desc_t				*frame = &current_layout.variants[variant];
	VkCommandBuffer					 command_buffer = vulkan_globals.primary_cb_contexts[PCBX_RENDER_PASSES].cb;
	const bool						 screen_effects =
		parms->render_warp || parms->render_scale >= 2 || parms->vid_palettize || (parms->polyblend && parms->v_blend[3]) || parms->menu || parms->ray_debug;
	const bool	 msaa = current_layout.samples != VK_SAMPLE_COUNT_1_BIT;
	VkClearValue clear_values[MAX_PASS_ATTACHMENTS] = {0};
	clear_values[0] = parms->color_clear_value;
	if (msaa)
		clear_values[2] = parms->color_clear_value;
	if (variant == MAIN_RENDER_PASS_OIT)
		for (int i = 0; i < 4; ++i)
			clear_values[msaa ? 4 : 3].color.float32[i] = 1.0f;

	uint32_t					 submit_count = 0;
	const frame_record_context_t record_context = {
		.cbx = &vulkan_globals.primary_cb_contexts[PCBX_RENDER_PASSES],
		.parms = parms,
		.screen_effects = screen_effects,
		.record_readback = record_readback,
		.readback_data = readback_data,
	};
	bool recording_started = false;
	for (uint32_t i = 0; i < frame->step_count; ++i)
	{
		const frame_step_t *step = &frame->steps[i];
		if (step->type == FRAME_PREPARED_COMMANDS || !recording_started)
		{
			if (submit_count == submit_capacity)
				Sys_Error ("Too many frame command buffers");
			submit_buffers[submit_count++] =
				step->type == FRAME_PREPARED_COMMANDS ? vulkan_globals.primary_cb_contexts[step->first_context].cb : command_buffer;
			if (step->type != FRAME_PREPARED_COMMANDS)
				recording_started = true;
		}
		switch (step->type)
		{
		case FRAME_PREPARED_COMMANDS:
			assert (!recording_started);
			break;
		case FRAME_BEGIN_GRAPHICS:
		{
			const bool			   ui = frame->passes[step->pass].target == FRAME_TARGET_UI;
			const physical_pass_t *physical = &physical_passes[variant][step->pass];
			const uint32_t		   image = ui ? swapchain_index : screen_effects ? 1 : 0;
			assert (image < physical->framebuffer_count);
			const VkRenderPassBeginInfo begin = {
				.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
				.renderPass = physical->handles[ui || Sky_NeedStencil () ? MAIN_RENDER_PASS_STENCIL_CLEAR : MAIN_RENDER_PASS_NO_STENCIL],
				.framebuffer = physical->framebuffers[image],
				.renderArea = {{0, 0}, {parms->vid_width, parms->vid_height}},
				.clearValueCount = ui ? 0 : physical->attachment_count,
				.pClearValues = clear_values,
			};
			vkCmdBeginRenderPass (command_buffer, &begin, VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS);
			break;
		}
		case FRAME_NEXT_SUBPASS:
			vkCmdNextSubpass (command_buffer, VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS);
			break;
		case FRAME_GRAPHICS_WORK:
			R_SubmitContexts (command_buffer, step->first_context, step->last_context);
			break;
		case FRAME_RECORD_WORK:
			step->recorder (&record_context);
			break;
		case FRAME_END_GRAPHICS:
			vkCmdEndRenderPass (command_buffer);
			break;
		}
	}
	return submit_count;
}

static void R_CreateScenePasses (main_render_pass_variant_t variant)
{
	const bool resolve = current_layout.samples != VK_SAMPLE_COUNT_1_BIT;
	ZEROED_STRUCT_ARRAY (VkAttachmentDescription, attachment_descriptions, 8);
	const qboolean use_wboit = (variant == MAIN_RENDER_PASS_OIT);
	const qboolean use_mboit = (variant == MAIN_RENDER_PASS_MBOIT);
	const qboolean use_oit = use_wboit || use_mboit;
	const uint32_t scene_attachment_index = resolve ? 2 : 0;
	const uint32_t accum_attachment_index = resolve ? 3 : 2;
	const uint32_t reveal_attachment_index = resolve ? 4 : 3;
	const uint32_t mboit_b0_attachment_index = resolve ? 3 : 2;
	const uint32_t mboit_moments0_attachment_index = resolve ? 4 : 3;
	const uint32_t mboit_color_attachment_index = resolve ? 5 : 4;

	attachment_descriptions[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	attachment_descriptions[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	attachment_descriptions[0].samples = VK_SAMPLE_COUNT_1_BIT;
	attachment_descriptions[0].format = vulkan_globals.color_format;
	attachment_descriptions[0].loadOp = resolve ? VK_ATTACHMENT_LOAD_OP_DONT_CARE : VK_ATTACHMENT_LOAD_OP_CLEAR;
	attachment_descriptions[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;

	attachment_descriptions[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	attachment_descriptions[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	attachment_descriptions[1].samples = vulkan_globals.sample_count;
	attachment_descriptions[1].format = vulkan_globals.depth_format;
	attachment_descriptions[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	attachment_descriptions[1].storeOp = use_oit ? VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachment_descriptions[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	attachment_descriptions[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;

	if (resolve)
	{
		attachment_descriptions[scene_attachment_index].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		attachment_descriptions[scene_attachment_index].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		attachment_descriptions[scene_attachment_index].samples = vulkan_globals.sample_count;
		attachment_descriptions[scene_attachment_index].format = vulkan_globals.color_format;
		attachment_descriptions[scene_attachment_index].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		attachment_descriptions[scene_attachment_index].storeOp = use_oit ? VK_ATTACHMENT_STORE_OP_DONT_CARE : VK_ATTACHMENT_STORE_OP_STORE;
	}

	if (use_wboit)
	{
		attachment_descriptions[accum_attachment_index].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		attachment_descriptions[accum_attachment_index].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		attachment_descriptions[accum_attachment_index].samples = vulkan_globals.sample_count;
		attachment_descriptions[accum_attachment_index].format = VK_FORMAT_R16G16B16A16_SFLOAT;
		attachment_descriptions[accum_attachment_index].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		attachment_descriptions[accum_attachment_index].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;

		attachment_descriptions[reveal_attachment_index].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		attachment_descriptions[reveal_attachment_index].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		attachment_descriptions[reveal_attachment_index].samples = vulkan_globals.sample_count;
		attachment_descriptions[reveal_attachment_index].format = VK_FORMAT_R8_UNORM;
		attachment_descriptions[reveal_attachment_index].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		attachment_descriptions[reveal_attachment_index].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	}
	else if (use_mboit)
	{
		attachment_descriptions[mboit_b0_attachment_index].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		attachment_descriptions[mboit_b0_attachment_index].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		attachment_descriptions[mboit_b0_attachment_index].samples = vulkan_globals.sample_count;
		// power moments need single precision, fp16 loses them to underflow and cancellation
		attachment_descriptions[mboit_b0_attachment_index].format = VK_FORMAT_R32_SFLOAT;
		attachment_descriptions[mboit_b0_attachment_index].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		attachment_descriptions[mboit_b0_attachment_index].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;

		attachment_descriptions[mboit_moments0_attachment_index] = attachment_descriptions[mboit_b0_attachment_index];
		attachment_descriptions[mboit_moments0_attachment_index].format = VK_FORMAT_R32G32B32A32_SFLOAT;

		attachment_descriptions[mboit_color_attachment_index] = attachment_descriptions[mboit_moments0_attachment_index];
		attachment_descriptions[mboit_color_attachment_index].format = VK_FORMAT_R16G16B16A16_SFLOAT;
	}

	VkAttachmentReference scene_color_attachment_reference = {
		.attachment = scene_attachment_index,
		.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
	};
	VkAttachmentReference accum_attachment_reference = {
		.attachment = accum_attachment_index,
		.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
	};
	VkAttachmentReference reveal_attachment_reference = {
		.attachment = reveal_attachment_index,
		.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
	};
	VkAttachmentReference depth_attachment_reference = {
		.attachment = 1,
		.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
	};
	VkAttachmentReference resolve_attachment_reference = {
		.attachment = 0,
		.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
	};
	VkAttachmentReference oit_input_attachment_references[2] = {
		{.attachment = accum_attachment_index, .layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
		{.attachment = reveal_attachment_index, .layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
	};
	VkAttachmentReference oit_accum_color_attachment_references[2] = {
		accum_attachment_reference,
		reveal_attachment_reference,
	};
	VkAttachmentReference mboit_b0_attachment_reference = {
		.attachment = mboit_b0_attachment_index,
		.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
	};
	VkAttachmentReference mboit_moments0_attachment_reference = {
		.attachment = mboit_moments0_attachment_index,
		.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
	};
	VkAttachmentReference mboit_color_attachment_reference = {
		.attachment = mboit_color_attachment_index,
		.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
	};
	VkAttachmentReference mboit_moment_color_attachment_references[2] = {
		mboit_b0_attachment_reference,
		mboit_moments0_attachment_reference,
	};
	VkAttachmentReference mboit_composite_input_attachment_references[2] = {
		{.attachment = mboit_b0_attachment_index, .layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
		{.attachment = mboit_moments0_attachment_index, .layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
	};
	VkAttachmentReference mboit_resolve_input_attachment_references[2] = {
		{.attachment = mboit_b0_attachment_index, .layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
		{.attachment = mboit_color_attachment_index, .layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
	};
	uint32_t oit_subpass_preserve_attachments[1] = {
		scene_attachment_index,
	};

	ZEROED_STRUCT_ARRAY (VkSubpassDescription, subpass_descriptions, SUBPASS_COUNT);
	subpass_descriptions[SUBPASS_MAIN].colorAttachmentCount = 1;
	subpass_descriptions[SUBPASS_MAIN].pColorAttachments = &scene_color_attachment_reference;
	subpass_descriptions[SUBPASS_MAIN].pDepthStencilAttachment = &depth_attachment_reference;
	subpass_descriptions[SUBPASS_MAIN].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	if (resolve && !use_oit)
		subpass_descriptions[SUBPASS_MAIN].pResolveAttachments = &resolve_attachment_reference;

	if (use_wboit)
	{
		subpass_descriptions[use_wboit ? SUBPASS_WBOIT : SUBPASS_MBOIT_MOMENTS].colorAttachmentCount = countof (oit_accum_color_attachment_references);
		subpass_descriptions[use_wboit ? SUBPASS_WBOIT : SUBPASS_MBOIT_MOMENTS].pColorAttachments = oit_accum_color_attachment_references;
		subpass_descriptions[use_wboit ? SUBPASS_WBOIT : SUBPASS_MBOIT_MOMENTS].pDepthStencilAttachment = &depth_attachment_reference;
		subpass_descriptions[use_wboit ? SUBPASS_WBOIT : SUBPASS_MBOIT_MOMENTS].preserveAttachmentCount = countof (oit_subpass_preserve_attachments);
		subpass_descriptions[use_wboit ? SUBPASS_WBOIT : SUBPASS_MBOIT_MOMENTS].pPreserveAttachments = oit_subpass_preserve_attachments;
		subpass_descriptions[use_wboit ? SUBPASS_WBOIT : SUBPASS_MBOIT_MOMENTS].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;

		subpass_descriptions[use_wboit ? SUBPASS_OIT_RESOLVE : SUBPASS_MBOIT_COMPOSITE].colorAttachmentCount = 1;
		subpass_descriptions[use_wboit ? SUBPASS_OIT_RESOLVE : SUBPASS_MBOIT_COMPOSITE].pColorAttachments = &scene_color_attachment_reference;
		subpass_descriptions[use_wboit ? SUBPASS_OIT_RESOLVE : SUBPASS_MBOIT_COMPOSITE].pDepthStencilAttachment = &depth_attachment_reference;
		subpass_descriptions[use_wboit ? SUBPASS_OIT_RESOLVE : SUBPASS_MBOIT_COMPOSITE].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpass_descriptions[use_wboit ? SUBPASS_OIT_RESOLVE : SUBPASS_MBOIT_COMPOSITE].inputAttachmentCount = countof (oit_input_attachment_references);
		subpass_descriptions[use_wboit ? SUBPASS_OIT_RESOLVE : SUBPASS_MBOIT_COMPOSITE].pInputAttachments = oit_input_attachment_references;
		if (resolve)
			subpass_descriptions[use_wboit ? SUBPASS_OIT_RESOLVE : SUBPASS_MBOIT_COMPOSITE].pResolveAttachments = &resolve_attachment_reference;
	}
	else if (use_mboit)
	{
		subpass_descriptions[use_wboit ? SUBPASS_WBOIT : SUBPASS_MBOIT_MOMENTS].colorAttachmentCount = countof (mboit_moment_color_attachment_references);
		subpass_descriptions[use_wboit ? SUBPASS_WBOIT : SUBPASS_MBOIT_MOMENTS].pColorAttachments = mboit_moment_color_attachment_references;
		subpass_descriptions[use_wboit ? SUBPASS_WBOIT : SUBPASS_MBOIT_MOMENTS].pDepthStencilAttachment = &depth_attachment_reference;
		subpass_descriptions[use_wboit ? SUBPASS_WBOIT : SUBPASS_MBOIT_MOMENTS].preserveAttachmentCount = countof (oit_subpass_preserve_attachments);
		subpass_descriptions[use_wboit ? SUBPASS_WBOIT : SUBPASS_MBOIT_MOMENTS].pPreserveAttachments = oit_subpass_preserve_attachments;
		subpass_descriptions[use_wboit ? SUBPASS_WBOIT : SUBPASS_MBOIT_MOMENTS].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;

		subpass_descriptions[use_wboit ? SUBPASS_OIT_RESOLVE : SUBPASS_MBOIT_COMPOSITE].colorAttachmentCount = 1;
		subpass_descriptions[use_wboit ? SUBPASS_OIT_RESOLVE : SUBPASS_MBOIT_COMPOSITE].pColorAttachments = &mboit_color_attachment_reference;
		subpass_descriptions[use_wboit ? SUBPASS_OIT_RESOLVE : SUBPASS_MBOIT_COMPOSITE].pDepthStencilAttachment = &depth_attachment_reference;
		subpass_descriptions[use_wboit ? SUBPASS_OIT_RESOLVE : SUBPASS_MBOIT_COMPOSITE].preserveAttachmentCount = countof (oit_subpass_preserve_attachments);
		subpass_descriptions[use_wboit ? SUBPASS_OIT_RESOLVE : SUBPASS_MBOIT_COMPOSITE].pPreserveAttachments = oit_subpass_preserve_attachments;
		subpass_descriptions[use_wboit ? SUBPASS_OIT_RESOLVE : SUBPASS_MBOIT_COMPOSITE].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpass_descriptions[use_wboit ? SUBPASS_OIT_RESOLVE : SUBPASS_MBOIT_COMPOSITE].inputAttachmentCount =
			countof (mboit_composite_input_attachment_references);
		subpass_descriptions[use_wboit ? SUBPASS_OIT_RESOLVE : SUBPASS_MBOIT_COMPOSITE].pInputAttachments = mboit_composite_input_attachment_references;

		subpass_descriptions[SUBPASS_OIT_RESOLVE].colorAttachmentCount = 1;
		subpass_descriptions[SUBPASS_OIT_RESOLVE].pColorAttachments = &scene_color_attachment_reference;
		subpass_descriptions[SUBPASS_OIT_RESOLVE].pDepthStencilAttachment = &depth_attachment_reference;
		subpass_descriptions[SUBPASS_OIT_RESOLVE].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpass_descriptions[SUBPASS_OIT_RESOLVE].inputAttachmentCount = countof (mboit_resolve_input_attachment_references);
		subpass_descriptions[SUBPASS_OIT_RESOLVE].pInputAttachments = mboit_resolve_input_attachment_references;
		if (resolve)
			subpass_descriptions[SUBPASS_OIT_RESOLVE].pResolveAttachments = &resolve_attachment_reference;
	}

	R_CreateGraphicsPasses (
		variant, FRAME_TARGET_SCENE, attachment_descriptions,
		use_mboit	? (resolve ? 6 : 5)
		: use_wboit ? (resolve ? 5 : 4)
					: (resolve ? 3 : 2),
		subpass_descriptions);
}

static void R_CreateUIPasses (main_render_pass_variant_t variant)
{
	// UI Render Pass
	ZEROED_STRUCT_ARRAY (VkAttachmentDescription, attachment_descriptions, 2);

	attachment_descriptions[0].initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	attachment_descriptions[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	attachment_descriptions[0].samples = VK_SAMPLE_COUNT_1_BIT;
	attachment_descriptions[0].format = vulkan_globals.color_format;
	attachment_descriptions[0].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
	attachment_descriptions[0].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;

	attachment_descriptions[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	attachment_descriptions[1].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
	attachment_descriptions[1].samples = VK_SAMPLE_COUNT_1_BIT;
	attachment_descriptions[1].format = vulkan_globals.swap_chain_format;
	attachment_descriptions[1].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	attachment_descriptions[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;

	VkAttachmentReference color_input_attachment_reference;
	color_input_attachment_reference.attachment = 0;
	color_input_attachment_reference.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	VkAttachmentReference ui_color_attachment_reference;
	ui_color_attachment_reference.attachment = 0;
	ui_color_attachment_reference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	VkAttachmentReference swap_chain_attachment_reference;
	swap_chain_attachment_reference.attachment = 1;
	swap_chain_attachment_reference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	ZEROED_STRUCT_ARRAY (VkSubpassDescription, subpass_descriptions, SUBPASS_COUNT);
	subpass_descriptions[SUBPASS_UI].colorAttachmentCount = 1;
	subpass_descriptions[SUBPASS_UI].pColorAttachments = &ui_color_attachment_reference;
	subpass_descriptions[SUBPASS_UI].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;

	subpass_descriptions[SUBPASS_POST_PROCESS].colorAttachmentCount = 1;
	subpass_descriptions[SUBPASS_POST_PROCESS].pColorAttachments = &swap_chain_attachment_reference;
	subpass_descriptions[SUBPASS_POST_PROCESS].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass_descriptions[SUBPASS_POST_PROCESS].inputAttachmentCount = 1;
	subpass_descriptions[SUBPASS_POST_PROCESS].pInputAttachments = &color_input_attachment_reference;

	R_CreateGraphicsPasses (variant, FRAME_TARGET_UI, attachment_descriptions, 2, subpass_descriptions);
}

void R_CreateRenderPasses (void)
{
	R_SetupRenderPasses ();
	current_layout = pending_layout;
	for (int variant = 0; variant < MAIN_RENDER_PASS_VARIANT_COUNT; ++variant)
	{
		R_CreateScenePasses (variant);
		R_CreateUIPasses (variant);
		R_BindFrameContexts (variant);
	}
	VkResult err;
	if (vulkan_globals.warp_render_pass == VK_NULL_HANDLE)
	{
		ZEROED_STRUCT (VkAttachmentDescription, attachment_description);

		// Warp rendering
		attachment_description.format = VK_FORMAT_R8G8B8A8_UNORM;
		attachment_description.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachment_description.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		attachment_description.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		attachment_description.finalLayout = VK_IMAGE_LAYOUT_GENERAL;
		attachment_description.samples = VK_SAMPLE_COUNT_1_BIT;

		VkAttachmentReference scene_color_attachment_reference;
		scene_color_attachment_reference.attachment = 0;
		scene_color_attachment_reference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		ZEROED_STRUCT (VkSubpassDescription, subpass_description);
		subpass_description.colorAttachmentCount = 1;
		subpass_description.pColorAttachments = &scene_color_attachment_reference;
		subpass_description.pDepthStencilAttachment = NULL;
		subpass_description.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;

		VkSubpassDependency subpass_dependencies[2];
		subpass_dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
		subpass_dependencies[0].dstSubpass = 0;
		subpass_dependencies[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		subpass_dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		subpass_dependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
		subpass_dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		subpass_dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
		subpass_dependencies[1].srcSubpass = 0;
		subpass_dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
		subpass_dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		subpass_dependencies[1].dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
		subpass_dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		subpass_dependencies[1].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		subpass_dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

		ZEROED_STRUCT (VkRenderPassCreateInfo, render_pass_create_info);
		render_pass_create_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		render_pass_create_info.pAttachments = &attachment_description;
		render_pass_create_info.subpassCount = 1;
		render_pass_create_info.pSubpasses = &subpass_description;
		render_pass_create_info.attachmentCount = 1;
		render_pass_create_info.dependencyCount = 0;
		render_pass_create_info.pDependencies = NULL;
		render_pass_create_info.dependencyCount = 2;
		render_pass_create_info.pDependencies = subpass_dependencies;

		err = vkCreateRenderPass (vulkan_globals.device, &render_pass_create_info, NULL, &vulkan_globals.warp_render_pass);
		if (err != VK_SUCCESS)
			Sys_Error ("Couldn't create Vulkan render pass with code %i", (int)err);

		GL_SetObjectName ((uint64_t)vulkan_globals.warp_render_pass, VK_OBJECT_TYPE_RENDER_PASS, "warp");
	}
}

static const render_pass_binding_t *R_RenderPassBindingAt (subpass_type_t type, main_render_pass_variant_t variant, uint32_t index)
{
	const frame_desc_t *frame = &current_layout.variants[variant];
	for (uint32_t pass = 0; pass < frame->pass_count; ++pass)
		for (uint32_t subpass = 0; subpass < frame->passes[pass].subpass_count; ++subpass)
			if (frame->passes[pass].subpasses[subpass] == type && index-- == 0)
				return &physical_passes[variant][pass].bindings[subpass];
	return NULL;
}

const render_pass_binding_t *R_RenderPassBinding (subpass_type_t type, main_render_pass_variant_t variant)
{
	assert (type >= 0 && type < SUBPASS_COUNT);
	assert (variant >= 0 && variant < MAIN_RENDER_PASS_VARIANT_COUNT);
	const render_pass_binding_t *binding = R_RenderPassBindingAt (type, variant, 0);
	if (!binding)
		Sys_Error ("Missing subpass type %d in variant %d", type, variant);
	return binding;
}

// Enumerate other occurrences of the same subpass type, not other draw stages.
// A pipeline is created once for a shared subpass, and again only for a new binding.
const render_pass_binding_t *R_RenderPassAlternative (VkRenderPass pass, uint32_t subpass, uint32_t index)
{
	for (int variant = 0; variant < MAIN_RENDER_PASS_VARIANT_COUNT; ++variant)
		for (int type = 0; type < SUBPASS_COUNT; ++type)
		{
			const render_pass_binding_t *first = R_RenderPassBindingAt (type, variant, 0);
			if (first && first->render_pass[MAIN_RENDER_PASS_STENCIL_CLEAR] == pass && first->subpass == subpass)
				return R_RenderPassBindingAt (type, variant, index);
		}
	return NULL; // A standalone pass, such as texture warp.
}
