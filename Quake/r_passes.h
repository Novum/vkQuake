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

#ifndef R_PASSES_H
#define R_PASSES_H

#include <stdbool.h>
#include <vulkan/vulkan_core.h>

// Attachment/shader interface of a subpass, independent of draw-stage identity.
typedef enum
{
	SUBPASS_MAIN,
	SUBPASS_UI,
	SUBPASS_WBOIT,
	SUBPASS_MBOIT_MOMENTS,
	SUBPASS_MBOIT_COMPOSITE,
	SUBPASS_OIT_RESOLVE,
	SUBPASS_POST_PROCESS,
	SUBPASS_COUNT,
} subpass_type_t;

typedef enum
{
	MAIN_RENDER_PASS_STANDARD,
	MAIN_RENDER_PASS_OIT,
	MAIN_RENDER_PASS_MBOIT,
	MAIN_RENDER_PASS_VARIANT_COUNT,
} main_render_pass_variant_t;

typedef enum
{
	MAIN_RENDER_PASS_STENCIL_CLEAR,
	MAIN_RENDER_PASS_NO_STENCIL,
	MAIN_RENDER_PASS_STENCIL_COUNT,
} main_render_pass_stencil_t;

typedef struct
{
	VkRenderPass render_pass[MAIN_RENDER_PASS_STENCIL_COUNT];
	uint32_t	 subpass;
	uint32_t	 segment;
} render_pass_binding_t;

typedef struct end_rendering_parms_s
{
	uint32_t	 vid_width	   : 20;
	bool		 swapchain	   : 1;
	bool		 use_oit	   : 1;
	bool		 use_mboit	   : 1;
	bool		 render_warp   : 1;
	bool		 vid_palettize : 1;
	bool		 polyblend	   : 1;
	bool		 menu		   : 1;
	bool		 ray_debug	   : 1;
	uint32_t	 render_scale  : 4;
	uint32_t	 vid_height	   : 20;
	float		 time;
	VkClearValue color_clear_value;
	uint8_t		 v_blend[4];
	float		 origin[3];
	float		 forward[3];
	float		 right[3];
	float		 down[3];
} end_rendering_parms_t;

typedef struct
{
	uint32_t		   width, height;
	VkImageView		   color[2];
	VkImageView		   depth, msaa_color;
	VkImageView		   oit_accum, oit_reveal;
	VkImageView		   mboit_b0, mboit_moments, mboit_color;
	uint32_t		   swapchain_count;
	const VkImageView *swapchain;
} render_framebuffer_images_t;

void	 R_CreateFrameBuffers (const render_framebuffer_images_t *images);
void	 R_DestroyFrameBuffers (void);
uint32_t R_RecordFrame (
	end_rendering_parms_t *parms, uint32_t swapchain_index, VkCommandBuffer *submit_buffers, uint32_t submit_capacity, void (*record_readback) (void *),
	void *readback_data);

bool R_SetupRenderPasses (void);
void R_CreateRenderPasses (void);
void R_DestroyRenderPasses (void);

struct cb_context_s;
void R_ConfigureRenderContext (struct cb_context_s *cbx, int context, main_render_pass_variant_t variant, main_render_pass_stencil_t stencil);
const render_pass_binding_t *R_RenderPassBinding (subpass_type_t stage, main_render_pass_variant_t variant);

const render_pass_binding_t *R_RenderPassAlternative (VkRenderPass pass, uint32_t subpass, uint32_t index);

#endif /* R_PASSES_H */
