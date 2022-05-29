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

// gl_texmgr.c -- fitzquake's texture manager. manages texture images

#include "quakedef.h"
#include "gl_heap.h"

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#define STB_IMAGE_RESIZE_STATIC
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#include "stb_image_resize.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

static cvar_t gl_max_size = {"gl_max_size", "0", CVAR_NONE};
static cvar_t gl_picmip = {"gl_picmip", "0", CVAR_NONE};

extern cvar_t vid_filter;
extern cvar_t vid_anisotropic;

#define MAX_MIPS 16
static int          numgltextures;
static gltexture_t *active_gltextures, *free_gltextures;
gltexture_t        *notexture, *nulltexture, *whitetexture, *greytexture;

unsigned int d_8to24table[256];
unsigned int d_8to24table_fbright[256];
unsigned int d_8to24table_fbright_fence[256];
unsigned int d_8to24table_nobright[256];
unsigned int d_8to24table_nobright_fence[256];
unsigned int d_8to24table_conchars[256];
unsigned int d_8to24table_shirt[256];
unsigned int d_8to24table_pants[256];

// Heap
#define TEXTURE_HEAP_SIZE_MB 32

static glheap_t **texmgr_heaps;
static int        num_texmgr_heaps;

static byte *image_resize_buffer;
static int   image_resize_buffer_size;

/*
================================================================================

    COMMANDS

================================================================================
*/

/*
===============
TexMgr_SetFilterModes
===============
*/
static void TexMgr_SetFilterModes (gltexture_t *glt)
{
	VkDescriptorImageInfo image_info;
	memset (&image_info, 0, sizeof (image_info));
	image_info.imageView = glt->image_view;
	image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	VkSampler point_sampler = (vid_anisotropic.value == 1) ? vulkan_globals.point_aniso_sampler_lod_bias : vulkan_globals.point_sampler_lod_bias;
	VkSampler linear_sampler = (vid_anisotropic.value == 1) ? vulkan_globals.linear_aniso_sampler_lod_bias : vulkan_globals.linear_sampler_lod_bias;

	if (glt->flags & TEXPREF_NEAREST)
		image_info.sampler = point_sampler;
	else if (glt->flags & TEXPREF_LINEAR)
		image_info.sampler = linear_sampler;
	else
		image_info.sampler = (vid_filter.value == 1) ? point_sampler : linear_sampler;

	VkWriteDescriptorSet texture_write;
	memset (&texture_write, 0, sizeof (texture_write));
	texture_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	texture_write.dstSet = glt->descriptor_set;
	texture_write.dstBinding = 0;
	texture_write.dstArrayElement = 0;
	texture_write.descriptorCount = 1;
	texture_write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	texture_write.pImageInfo = &image_info;

	vkUpdateDescriptorSets (vulkan_globals.device, 1, &texture_write, 0, NULL);
}

/*
===============
TexMgr_UpdateTextureDescriptorSets
===============
*/
void TexMgr_UpdateTextureDescriptorSets (void)
{
	gltexture_t *glt;

	for (glt = active_gltextures; glt; glt = glt->next)
		TexMgr_SetFilterModes (glt);
}

/*
===============
TexMgr_Imagelist_f -- report loaded textures
===============
*/
static void TexMgr_Imagelist_f (void)
{
	float        mb;
	float        texels = 0;
	gltexture_t *glt;

	for (glt = active_gltextures; glt; glt = glt->next)
	{
		Con_SafePrintf ("   %4i x%4i %s\n", glt->width, glt->height, glt->name);
		if (glt->flags & TEXPREF_MIPMAP)
			texels += glt->width * glt->height * 4.0f / 3.0f;
		else
			texels += (glt->width * glt->height);
	}

	mb = (texels * 4) / 0x100000;
	Con_Printf ("%i textures %i pixels %1.1f megabytes\n", numgltextures, (int)texels, mb);
}

/*
================================================================================

    TEXTURE MANAGER

================================================================================
*/

/*
================
TexMgr_FindTexture
================
*/
gltexture_t *TexMgr_FindTexture (qmodel_t *owner, const char *name)
{
	gltexture_t *glt;

	if (name)
	{
		for (glt = active_gltextures; glt; glt = glt->next)
		{
			if (glt->owner == owner && !strcmp (glt->name, name))
				return glt;
		}
	}

	return NULL;
}

/*
================
TexMgr_NewTexture
================
*/
gltexture_t *TexMgr_NewTexture (void)
{
	gltexture_t *glt;

	glt = free_gltextures;
	free_gltextures = glt->next;
	glt->next = active_gltextures;
	active_gltextures = glt;

	numgltextures++;
	return glt;
}

static void GL_DeleteTexture (gltexture_t *texture);

/*
================
TexMgr_FreeTexture
================
*/
void TexMgr_FreeTexture (gltexture_t *kill)
{
	gltexture_t *glt;

	if (kill == NULL)
	{
		Con_Printf ("TexMgr_FreeTexture: NULL texture\n");
		return;
	}

	if (active_gltextures == kill)
	{
		active_gltextures = kill->next;
		kill->next = free_gltextures;
		free_gltextures = kill;

		GL_DeleteTexture (kill);
		numgltextures--;
		return;
	}

	for (glt = active_gltextures; glt; glt = glt->next)
	{
		if (glt->next == kill)
		{
			glt->next = kill->next;
			kill->next = free_gltextures;
			free_gltextures = kill;

			GL_DeleteTexture (kill);
			numgltextures--;
			return;
		}
	}

	Con_Printf ("TexMgr_FreeTexture: not found\n");
}

/*
================
TexMgr_FreeTextures

compares each bit in "flags" to the one in glt->flags only if that bit is active in "mask"
================
*/
void TexMgr_FreeTextures (unsigned int flags, unsigned int mask)
{
	gltexture_t *glt, *next;

	for (glt = active_gltextures; glt; glt = next)
	{
		next = glt->next;
		if ((glt->flags & mask) == (flags & mask))
			TexMgr_FreeTexture (glt);
	}
}

/*
================
TexMgr_FreeTexturesForOwner
================
*/
void TexMgr_FreeTexturesForOwner (qmodel_t *owner)
{
	gltexture_t *glt, *next;

	for (glt = active_gltextures; glt; glt = next)
	{
		next = glt->next;
		if (glt && glt->owner == owner)
			TexMgr_FreeTexture (glt);
	}
}

/*
================
TexMgr_DeleteTextureObjects
================
*/
void TexMgr_DeleteTextureObjects (void)
{
	gltexture_t *glt;

	for (glt = active_gltextures; glt; glt = glt->next)
	{
		GL_DeleteTexture (glt);
	}
}

/*
================================================================================

    INIT

================================================================================
*/

/*
=================
TexMgr_LoadPalette -- johnfitz -- was VID_SetPalette, moved here, renamed, rewritten
=================
*/
void TexMgr_LoadPalette (void)
{
	byte *pal, *src, *dst;
	int   i, mark;
	FILE *f;

	COM_FOpenFile ("gfx/palette.lmp", &f, NULL);
	if (!f)
		Sys_Error ("Couldn't load gfx/palette.lmp");

	mark = Hunk_LowMark ();
	pal = (byte *)Hunk_Alloc (768);
	if (fread (pal, 1, 768, f) != 768)
		Sys_Error ("Couldn't load gfx/palette.lmp");
	fclose (f);

	// standard palette, 255 is transparent
	dst = (byte *)d_8to24table;
	src = pal;
	for (i = 0; i < 256; i++)
	{
		*dst++ = *src++;
		*dst++ = *src++;
		*dst++ = *src++;
		*dst++ = 255;
	}
	((byte *)&d_8to24table[255])[3] = 0;

	// fullbright palette, 0-223 are black (for additive blending)
	src = pal + 224 * 3;
	dst = (byte *)&d_8to24table_fbright[224];
	for (i = 224; i < 256; i++)
	{
		*dst++ = *src++;
		*dst++ = *src++;
		*dst++ = *src++;
		*dst++ = 255;
	}
	for (i = 0; i < 224; i++)
	{
		dst = (byte *)&d_8to24table_fbright[i];
		dst[3] = 255;
		dst[2] = dst[1] = dst[0] = 0;
	}

	// nobright palette, 224-255 are black (for additive blending)
	dst = (byte *)d_8to24table_nobright;
	src = pal;
	for (i = 0; i < 256; i++)
	{
		*dst++ = *src++;
		*dst++ = *src++;
		*dst++ = *src++;
		*dst++ = 255;
	}
	for (i = 224; i < 256; i++)
	{
		dst = (byte *)&d_8to24table_nobright[i];
		dst[3] = 255;
		dst[2] = dst[1] = dst[0] = 0;
	}

	// fullbright palette, for fence textures
	memcpy (d_8to24table_fbright_fence, d_8to24table_fbright, 256 * 4);
	d_8to24table_fbright_fence[255] = 0; // Alpha of zero.

	// nobright palette, for fence textures
	memcpy (d_8to24table_nobright_fence, d_8to24table_nobright, 256 * 4);
	d_8to24table_nobright_fence[255] = 0; // Alpha of zero.

	// conchars palette, 0 and 255 are transparent
	memcpy (d_8to24table_conchars, d_8to24table, 256 * 4);
	((byte *)&d_8to24table_conchars[0])[3] = 0;

	Hunk_FreeToLowMark (mark);
}

/*
================
TexMgr_NewGame
================
*/
void TexMgr_NewGame (void)
{
	TexMgr_FreeTextures (0, TEXPREF_PERSIST); // deletes all textures where TEXPREF_PERSIST is unset
	TexMgr_LoadPalette ();
}

/*
================
TexMgr_Init

must be called before any texture loading
================
*/
void TexMgr_Init (void)
{
	int               i;
	static byte       notexture_data[16] = {159, 91, 83, 255, 0, 0, 0, 255, 0, 0, 0, 255, 159, 91, 83, 255};                    // black and pink checker
	static byte       nulltexture_data[16] = {127, 191, 255, 255, 0, 0, 0, 255, 0, 0, 0, 255, 127, 191, 255, 255};              // black and blue checker
	static byte       whitetexture_data[16] = {255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255}; // white
	static byte       greytexture_data[16] = {127, 127, 127, 255, 127, 127, 127, 255, 127, 127, 127, 255, 127, 127, 127, 255};  // 50% grey
	extern texture_t *r_notexture_mip, *r_notexture_mip2;

	// init texture list
	free_gltextures = (gltexture_t *)Hunk_AllocName (MAX_GLTEXTURES * sizeof (gltexture_t), "gltextures");
	active_gltextures = NULL;
	for (i = 0; i < MAX_GLTEXTURES - 1; i++)
		free_gltextures[i].next = &free_gltextures[i + 1];
	free_gltextures[i].next = NULL;
	numgltextures = 0;

	// palette
	TexMgr_LoadPalette ();

	Cvar_RegisterVariable (&gl_max_size);
	Cvar_RegisterVariable (&gl_picmip);
	Cmd_AddCommand ("imagelist", &TexMgr_Imagelist_f);

	// load notexture images
	notexture = TexMgr_LoadImage (
		NULL, "notexture", 2, 2, SRC_RGBA, notexture_data, "", (src_offset_t)notexture_data, TEXPREF_NEAREST | TEXPREF_PERSIST | TEXPREF_NOPICMIP);
	nulltexture = TexMgr_LoadImage (
		NULL, "nulltexture", 2, 2, SRC_RGBA, nulltexture_data, "", (src_offset_t)nulltexture_data, TEXPREF_NEAREST | TEXPREF_PERSIST | TEXPREF_NOPICMIP);
	whitetexture = TexMgr_LoadImage (
		NULL, "whitetexture", 2, 2, SRC_RGBA, whitetexture_data, "", (src_offset_t)whitetexture_data, TEXPREF_NEAREST | TEXPREF_PERSIST | TEXPREF_NOPICMIP);
	greytexture = TexMgr_LoadImage (
		NULL, "greytexture", 2, 2, SRC_RGBA, greytexture_data, "", (src_offset_t)greytexture_data, TEXPREF_NEAREST | TEXPREF_PERSIST | TEXPREF_NOPICMIP);

	// have to assign these here becuase Mod_Init is called before TexMgr_Init
	r_notexture_mip->gltexture = r_notexture_mip2->gltexture = notexture;
}

/*
================================================================================

    IMAGE LOADING

================================================================================
*/

/*
================
TexMgr_Downsample
================
*/
static unsigned *TexMgr_Downsample (unsigned *data, int in_width, int in_height, int out_width, int out_height)
{
	const int out_size_bytes = out_width * out_height * 4;

	assert ((out_width >= 1) && (out_width < in_width));
	assert ((out_height >= 1) && (out_height < in_height));

	if (out_size_bytes > image_resize_buffer_size)
		image_resize_buffer = realloc (image_resize_buffer, out_size_bytes);

	stbir_resize_uint8 ((byte *)data, in_width, in_height, 0, (byte *)image_resize_buffer, out_width, out_height, 0, 4);
	memcpy (data, image_resize_buffer, out_size_bytes);

	return data;
}

/*
===============
TexMgr_AlphaEdgeFix

eliminate pink edges on sprites, etc.
operates in place on 32bit data
===============
*/
static void TexMgr_AlphaEdgeFix (byte *data, int width, int height)
{
	int   i, j, n = 0, b, c[3] = {0, 0, 0}, lastrow, thisrow, nextrow, lastpix, thispix, nextpix;
	byte *dest = data;

	for (i = 0; i < height; i++)
	{
		lastrow = width * 4 * ((i == 0) ? height - 1 : i - 1);
		thisrow = width * 4 * i;
		nextrow = width * 4 * ((i == height - 1) ? 0 : i + 1);

		for (j = 0; j < width; j++, dest += 4)
		{
			if (dest[3]) // not transparent
				continue;

			lastpix = 4 * ((j == 0) ? width - 1 : j - 1);
			thispix = 4 * j;
			nextpix = 4 * ((j == width - 1) ? 0 : j + 1);

			b = lastrow + lastpix;
			if (data[b + 3])
			{
				c[0] += data[b];
				c[1] += data[b + 1];
				c[2] += data[b + 2];
				n++;
			}
			b = thisrow + lastpix;
			if (data[b + 3])
			{
				c[0] += data[b];
				c[1] += data[b + 1];
				c[2] += data[b + 2];
				n++;
			}
			b = nextrow + lastpix;
			if (data[b + 3])
			{
				c[0] += data[b];
				c[1] += data[b + 1];
				c[2] += data[b + 2];
				n++;
			}
			b = lastrow + thispix;
			if (data[b + 3])
			{
				c[0] += data[b];
				c[1] += data[b + 1];
				c[2] += data[b + 2];
				n++;
			}
			b = nextrow + thispix;
			if (data[b + 3])
			{
				c[0] += data[b];
				c[1] += data[b + 1];
				c[2] += data[b + 2];
				n++;
			}
			b = lastrow + nextpix;
			if (data[b + 3])
			{
				c[0] += data[b];
				c[1] += data[b + 1];
				c[2] += data[b + 2];
				n++;
			}
			b = thisrow + nextpix;
			if (data[b + 3])
			{
				c[0] += data[b];
				c[1] += data[b + 1];
				c[2] += data[b + 2];
				n++;
			}
			b = nextrow + nextpix;
			if (data[b + 3])
			{
				c[0] += data[b];
				c[1] += data[b + 1];
				c[2] += data[b + 2];
				n++;
			}

			// average all non-transparent neighbors
			if (n)
			{
				dest[0] = (byte)(c[0] / n);
				dest[1] = (byte)(c[1] / n);
				dest[2] = (byte)(c[2] / n);

				n = c[0] = c[1] = c[2] = 0;
			}
		}
	}
}

/*
================
TexMgr_8to32
================
*/
static unsigned *TexMgr_8to32 (byte *in, int pixels, unsigned int *usepal)
{
	int       i;
	unsigned *out, *data;

	out = data = (unsigned *)Hunk_Alloc (pixels * 4);

	for (i = 0; i < pixels; i++)
		*out++ = usepal[*in++];

	return data;
}

/*
================
TexMgr_DeriveNumMips
================
*/
static int TexMgr_DeriveNumMips (int width, int height)
{
	int num_mips = 0;
	while (width >= 1 && height >= 1)
	{
		width /= 2;
		height /= 2;
		num_mips += 1;
	}
	return num_mips;
}

/*
================
TexMgr_DeriveStagingSize
================
*/
static int TexMgr_DeriveStagingSize (int width, int height)
{
	int size = 0;
	while (width >= 1 && height >= 1)
	{
		size += width * height * 4;
		width /= 2;
		height /= 2;
	}
	return size;
}

static byte *TexMgr_PreMultiply32 (byte *in, size_t width, size_t height)
{
	size_t pixels = width * height;
	byte  *out = (byte *)Hunk_Alloc (pixels * 4);
	byte  *result = out;
	while (pixels-- > 0)
	{
		out[0] = (in[0] * in[3]) >> 8;
		out[1] = (in[1] * in[3]) >> 8;
		out[2] = (in[2] * in[3]) >> 8;
		out[3] = in[3];
		in += 4;
		out += 4;
	}
	return result;
}

/*
================
TexMgr_LoadImage32 -- handles 32bit source data
================
*/
static void TexMgr_LoadImage32 (gltexture_t *glt, unsigned *data)
{
	GL_DeleteTexture (glt);

	// do this before any rescaling
	if (glt->flags & TEXPREF_PREMULTIPLY)
		data = (unsigned *)TexMgr_PreMultiply32 ((byte *)data, glt->width, glt->height);

	// mipmap down
	int picmip = (glt->flags & TEXPREF_NOPICMIP) ? 0 : q_max ((int)gl_picmip.value, 0);
	int mipwidth = q_max (glt->width >> picmip, 1);
	int mipheight = q_max (glt->height >> picmip, 1);

	int maxsize = (int)vulkan_globals.device_properties.limits.maxImageDimension2D;
	if ((mipwidth > maxsize) || (mipheight > maxsize))
	{
		if (mipwidth >= mipheight)
		{
			mipheight = q_max ((mipheight * maxsize) / mipwidth, 1);
			mipwidth = maxsize;
		}
		else
		{
			mipwidth = q_max ((mipwidth * maxsize) / mipheight, 1);
			mipheight = maxsize;
		}
	}

	if ((int)glt->width != mipwidth || (int)glt->height != mipheight)
	{
		TexMgr_Downsample (data, glt->width, glt->height, mipwidth, mipheight);
		glt->width = mipwidth;
		glt->height = mipheight;
		if (glt->flags & TEXPREF_ALPHA)
			TexMgr_AlphaEdgeFix ((byte *)data, glt->width, glt->height);
	}
	int num_mips = (glt->flags & TEXPREF_MIPMAP) ? TexMgr_DeriveNumMips (glt->width, glt->height) : 1;

	const qboolean warp_image = (glt->flags & TEXPREF_WARPIMAGE);
	if (warp_image)
		num_mips = WARPIMAGEMIPS;

	// Check for sanity. This should never be reached.
	if (num_mips > MAX_MIPS)
		Sys_Error ("Texture has over %d mips", MAX_MIPS);

	const qboolean lightmap = glt->source_format == SRC_LIGHTMAP;
	const qboolean surface_indices = glt->source_format == SRC_SURF_INDICES;

	VkResult err;

	const VkFormat format = !surface_indices ? VK_FORMAT_R8G8B8A8_UNORM : VK_FORMAT_R32_UINT;

	VkImageCreateInfo image_create_info;
	memset (&image_create_info, 0, sizeof (image_create_info));
	image_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	image_create_info.imageType = VK_IMAGE_TYPE_2D;
	image_create_info.format = format;
	image_create_info.extent.width = glt->width;
	image_create_info.extent.height = glt->height;
	image_create_info.extent.depth = 1;
	image_create_info.mipLevels = num_mips;
	image_create_info.arrayLayers = 1;
	image_create_info.samples = VK_SAMPLE_COUNT_1_BIT;
	image_create_info.tiling = VK_IMAGE_TILING_OPTIMAL;
	if (warp_image)
		image_create_info.usage =
			(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
		     VK_IMAGE_USAGE_STORAGE_BIT);
	else if (lightmap)
		image_create_info.usage = (VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT);
	else
		image_create_info.usage = (VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);

	image_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	image_create_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

	err = vkCreateImage (vulkan_globals.device, &image_create_info, NULL, &glt->image);
	if (err != VK_SUCCESS)
		Sys_Error ("vkCreateImage failed");
	GL_SetObjectName ((uint64_t)glt->image, VK_OBJECT_TYPE_IMAGE, va ("%s image", glt->name));

	VkMemoryRequirements memory_requirements;
	vkGetImageMemoryRequirements (vulkan_globals.device, glt->image, &memory_requirements);

	uint32_t     memory_type_index = GL_MemoryTypeFromProperties (memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0);
	VkDeviceSize heap_size = TEXTURE_HEAP_SIZE_MB * (VkDeviceSize)1024 * (VkDeviceSize)1024;
	VkDeviceSize aligned_offset = GL_AllocateFromHeaps (
		&num_texmgr_heaps, &texmgr_heaps, heap_size, memory_type_index, VULKAN_MEMORY_TYPE_DEVICE, memory_requirements.size, memory_requirements.alignment,
		&glt->heap, &glt->heap_node, &num_vulkan_tex_allocations, "Textures Heap");
	err = vkBindImageMemory (vulkan_globals.device, glt->image, glt->heap->memory.handle, aligned_offset);
	if (err != VK_SUCCESS)
		Sys_Error ("vkBindImageMemory failed");

	VkImageViewCreateInfo image_view_create_info;
	memset (&image_view_create_info, 0, sizeof (image_view_create_info));
	image_view_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	image_view_create_info.image = glt->image;
	image_view_create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
	image_view_create_info.format = format;
	image_view_create_info.components.r = VK_COMPONENT_SWIZZLE_R;
	image_view_create_info.components.g = VK_COMPONENT_SWIZZLE_G;
	image_view_create_info.components.b = VK_COMPONENT_SWIZZLE_B;
	image_view_create_info.components.a = VK_COMPONENT_SWIZZLE_A;
	image_view_create_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	image_view_create_info.subresourceRange.baseMipLevel = 0;
	image_view_create_info.subresourceRange.levelCount = num_mips;
	image_view_create_info.subresourceRange.baseArrayLayer = 0;
	image_view_create_info.subresourceRange.layerCount = 1;

	err = vkCreateImageView (vulkan_globals.device, &image_view_create_info, NULL, &glt->image_view);
	if (err != VK_SUCCESS)
		Sys_Error ("vkCreateImageView failed");
	GL_SetObjectName ((uint64_t)glt->image_view, VK_OBJECT_TYPE_IMAGE_VIEW, va ("%s image view", glt->name));

	// Allocate and update descriptor for this texture
	glt->descriptor_set = R_AllocateDescriptorSet (&vulkan_globals.single_texture_set_layout);
	GL_SetObjectName ((uint64_t)glt->descriptor_set, VK_OBJECT_TYPE_DESCRIPTOR_SET, va ("%s desc set", glt->name));

	TexMgr_SetFilterModes (glt);

	if (warp_image || lightmap)
	{
		image_view_create_info.subresourceRange.levelCount = 1;
		err = vkCreateImageView (vulkan_globals.device, &image_view_create_info, NULL, &glt->target_image_view);
		if (err != VK_SUCCESS)
			Sys_Error ("vkCreateImageView failed");
		GL_SetObjectName ((uint64_t)glt->target_image_view, VK_OBJECT_TYPE_IMAGE_VIEW, va ("%s target image view", glt->name));
	}
	else
	{
		glt->target_image_view = VK_NULL_HANDLE;
	}

	// Don't upload data for warp image, will be updated by rendering
	if (warp_image)
	{
		VkFramebufferCreateInfo framebuffer_create_info;
		memset (&framebuffer_create_info, 0, sizeof (framebuffer_create_info));
		framebuffer_create_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		framebuffer_create_info.renderPass = vulkan_globals.warp_render_pass;
		framebuffer_create_info.attachmentCount = 1;
		framebuffer_create_info.pAttachments = &glt->target_image_view;
		framebuffer_create_info.width = glt->width;
		framebuffer_create_info.height = glt->height;
		framebuffer_create_info.layers = 1;
		err = vkCreateFramebuffer (vulkan_globals.device, &framebuffer_create_info, NULL, &glt->frame_buffer);
		if (err != VK_SUCCESS)
			Sys_Error ("vkCreateFramebuffer failed");
		GL_SetObjectName ((uint64_t)glt->frame_buffer, VK_OBJECT_TYPE_FRAMEBUFFER, va ("%s framebuffer", glt->name));
	}

	if (warp_image)
	{
		// Allocate and update descriptor for this texture
		glt->storage_descriptor_set = R_AllocateDescriptorSet (&vulkan_globals.single_texture_cs_write_set_layout);
		GL_SetObjectName ((uint64_t)glt->storage_descriptor_set, VK_OBJECT_TYPE_DESCRIPTOR_SET, va ("%s storage desc set", glt->name));

		VkDescriptorImageInfo output_image_info;
		memset (&output_image_info, 0, sizeof (output_image_info));
		output_image_info.imageView = glt->target_image_view;
		output_image_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

		VkWriteDescriptorSet storage_image_write;
		memset (&storage_image_write, 0, sizeof (storage_image_write));
		storage_image_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		storage_image_write.dstBinding = 0;
		storage_image_write.dstArrayElement = 0;
		storage_image_write.descriptorCount = 1;
		storage_image_write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		storage_image_write.dstSet = glt->storage_descriptor_set;
		storage_image_write.pImageInfo = &output_image_info;

		vkUpdateDescriptorSets (vulkan_globals.device, 1, &storage_image_write, 0, NULL);
	}
	else
	{
		glt->storage_descriptor_set = VK_NULL_HANDLE;
	}

	// Don't upload data for warp image, will be updated by rendering
	if (warp_image)
		return;

	glt->frame_buffer = VK_NULL_HANDLE;

	// Upload
	VkBufferImageCopy regions[MAX_MIPS];
	memset (&regions, 0, sizeof (regions));

	int staging_size = (glt->flags & TEXPREF_MIPMAP) ? TexMgr_DeriveStagingSize (mipwidth, mipheight) : (mipwidth * mipheight * 4);

	VkBuffer        staging_buffer;
	VkCommandBuffer command_buffer;
	int             staging_offset;
	unsigned char  *staging_memory = R_StagingAllocate (staging_size, 4, &command_buffer, &staging_buffer, &staging_offset);

	int num_regions = 0;
	int mip_offset = 0;

	if (glt->flags & TEXPREF_MIPMAP)
	{
		mipwidth = glt->width;
		mipheight = glt->height;

		while (mipwidth >= 1 && mipheight >= 1)
		{
			memcpy (staging_memory + mip_offset, data, mipwidth * mipheight * 4);
			regions[num_regions].bufferOffset = staging_offset + mip_offset;
			regions[num_regions].imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			regions[num_regions].imageSubresource.layerCount = 1;
			regions[num_regions].imageSubresource.mipLevel = num_regions;
			regions[num_regions].imageExtent.width = mipwidth;
			regions[num_regions].imageExtent.height = mipheight;
			regions[num_regions].imageExtent.depth = 1;

			mip_offset += mipwidth * mipheight * 4;
			num_regions += 1;

			if (mipwidth > 1 && mipheight > 1)
				TexMgr_Downsample (data, mipwidth, mipheight, mipwidth / 2, mipheight / 2);

			mipwidth /= 2;
			mipheight /= 2;
		}
	}
	else
	{
		memcpy (staging_memory + mip_offset, data, mipwidth * mipheight * 4);
		regions[0].bufferOffset = staging_offset + mip_offset;
		regions[0].imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		regions[0].imageSubresource.layerCount = 1;
		regions[0].imageSubresource.mipLevel = 0;
		regions[0].imageExtent.width = mipwidth;
		regions[0].imageExtent.height = mipheight;
		regions[0].imageExtent.depth = 1;
	}

	VkImageMemoryBarrier image_memory_barrier;
	memset (&image_memory_barrier, 0, sizeof (image_memory_barrier));
	image_memory_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	image_memory_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	image_memory_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	image_memory_barrier.image = glt->image;
	image_memory_barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	image_memory_barrier.subresourceRange.baseMipLevel = 0;
	image_memory_barrier.subresourceRange.levelCount = num_mips;
	image_memory_barrier.subresourceRange.baseArrayLayer = 0;
	image_memory_barrier.subresourceRange.layerCount = 1;

	image_memory_barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	image_memory_barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	image_memory_barrier.srcAccessMask = 0;
	image_memory_barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	vkCmdPipelineBarrier (command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &image_memory_barrier);

	vkCmdCopyBufferToImage (command_buffer, staging_buffer, glt->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, num_mips, regions);

	image_memory_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	image_memory_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	image_memory_barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	image_memory_barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	vkCmdPipelineBarrier (command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, NULL, 0, NULL, 1, &image_memory_barrier);
}

/*
================
TexMgr_LoadImage8 -- handles 8bit source data, then passes it to LoadImage32
================
*/
static void TexMgr_LoadImage8 (gltexture_t *glt, byte *data)
{
	GL_DeleteTexture (glt);

	extern cvar_t gl_fullbrights;
	unsigned int *usepal;
	int           i;

	// HACK HACK HACK -- taken from tomazquake
	if (strstr (glt->name, "shot1sid") && glt->width == 32 && glt->height == 32 && CRC_Block (data, 1024) == 65393)
	{
		// This texture in b_shell1.bsp has some of the first 32 pixels painted white.
		// They are invisible in software, but look really ugly in GL. So we just copy
		// 32 pixels from the bottom to make it look nice.
		memcpy (data, data + 32 * 31, 32);
	}

	// detect false alpha cases
	if (glt->flags & TEXPREF_ALPHA && !(glt->flags & TEXPREF_CONCHARS))
	{
		for (i = 0; i < (int)(glt->width * glt->height); i++)
			if (data[i] == 255) // transparent index
				break;
		if (i == (int)(glt->width * glt->height))
			glt->flags -= TEXPREF_ALPHA;
	}

	// choose palette and padbyte
	if (glt->flags & TEXPREF_FULLBRIGHT)
	{
		if (glt->flags & TEXPREF_ALPHA)
			usepal = d_8to24table_fbright_fence;
		else
			usepal = d_8to24table_fbright;
	}
	else if (glt->flags & TEXPREF_NOBRIGHT && gl_fullbrights.value)
	{
		if (glt->flags & TEXPREF_ALPHA)
			usepal = d_8to24table_nobright_fence;
		else
			usepal = d_8to24table_nobright;
	}
	else if (glt->flags & TEXPREF_CONCHARS)
	{
		usepal = d_8to24table_conchars;
	}
	else
	{
		usepal = d_8to24table;
	}

	// convert to 32bit
	data = (byte *)TexMgr_8to32 (data, glt->width * glt->height, usepal);

	// fix edges
	if (glt->flags & TEXPREF_ALPHA)
		TexMgr_AlphaEdgeFix (data, glt->width, glt->height);

	// upload it
	TexMgr_LoadImage32 (glt, (unsigned *)data);
}

/*
================
TexMgr_LoadLightmap -- handles lightmap data
================
*/
static void TexMgr_LoadLightmap (gltexture_t *glt, byte *data)
{
	TexMgr_LoadImage32 (glt, (unsigned *)data);
}

/*
================
TexMgr_LoadImage -- the one entry point for loading all textures
================
*/
gltexture_t *TexMgr_LoadImage (
	qmodel_t *owner, const char *name, int width, int height, enum srcformat format, byte *data, const char *source_file, src_offset_t source_offset,
	unsigned flags)
{
	unsigned short crc;
	gltexture_t   *glt;
	int            mark;

	if (isDedicated)
		return NULL;

	// cache check
	switch (format)
	{
	case SRC_INDEXED:
		crc = CRC_Block (data, width * height);
		break;
	case SRC_LIGHTMAP:
		crc = CRC_Block (data, width * height * LIGHTMAP_BYTES);
		break;
	case SRC_RGBA:
		crc = CRC_Block (data, width * height * 4);
		break;
	default: /* not reachable but avoids compiler warnings */
		crc = 0;
	}
	if ((flags & TEXPREF_OVERWRITE) && (glt = TexMgr_FindTexture (owner, name)))
	{
		if (glt->source_crc == crc)
			return glt;
	}
	else
		glt = TexMgr_NewTexture ();

	// copy data
	glt->owner = owner;
	q_strlcpy (glt->name, name, sizeof (glt->name));
	glt->width = width;
	glt->height = height;
	glt->flags = flags;
	glt->shirt = -1;
	glt->pants = -1;
	q_strlcpy (glt->source_file, source_file, sizeof (glt->source_file));
	glt->source_offset = source_offset;
	glt->source_format = format;
	glt->source_width = width;
	glt->source_height = height;
	glt->source_crc = crc;

	// upload it
	mark = Hunk_LowMark ();

	switch (glt->source_format)
	{
	case SRC_INDEXED:
		TexMgr_LoadImage8 (glt, data);
		break;
	case SRC_LIGHTMAP:
		TexMgr_LoadLightmap (glt, data);
		break;
	case SRC_RGBA:
	case SRC_SURF_INDICES:
		TexMgr_LoadImage32 (glt, (unsigned *)data);
		break;
	}

	Hunk_FreeToLowMark (mark);

	return glt;
}

/*
================================================================================

    COLORMAPPING AND TEXTURE RELOADING

================================================================================
*/

/*
================
TexMgr_ReloadImage -- reloads a texture, and colormaps it if needed
================
*/
void TexMgr_ReloadImage (gltexture_t *glt, int shirt, int pants)
{
	byte  translation[256];
	byte *src, *dst, *data = NULL, *translated;
	int   mark, size, i;
	//
	// get source data
	//
	mark = Hunk_LowMark ();

	if (glt->source_file[0] && glt->source_offset)
	{
		// lump inside file
		FILE *f;
		COM_FOpenFile (glt->source_file, &f, NULL);
		if (!f)
			goto invalid;
		fseek (f, glt->source_offset, SEEK_CUR);
		size = glt->source_width * glt->source_height;
		/* should be SRC_INDEXED, but no harm being paranoid:  */
		if (glt->source_format == SRC_RGBA)
		{
			size *= 4;
		}
		else if (glt->source_format == SRC_LIGHTMAP)
		{
			size *= LIGHTMAP_BYTES;
		}
		data = (byte *)Hunk_Alloc (size);
		if (fread (data, 1, size, f) != size)
			goto invalid;
		fclose (f);
	}
	else if (glt->source_file[0] && !glt->source_offset)
	{
		data = Image_LoadImage (glt->source_file, (int *)&glt->source_width, (int *)&glt->source_height); // simple file
	}
	else if (!glt->source_file[0] && glt->source_offset)
	{
		data = (byte *)glt->source_offset; // image in memory
	}
	if (!data)
	{
	invalid:
		Con_Printf ("TexMgr_ReloadImage: invalid source for %s\n", glt->name);
		Hunk_FreeToLowMark (mark);
		return;
	}

	glt->width = glt->source_width;
	glt->height = glt->source_height;
	//
	// apply shirt and pants colors
	//
	// if shirt and pants are -1,-1, use existing shirt and pants colors
	// if existing shirt and pants colors are -1,-1, don't bother colormapping
	if (shirt > -1 && pants > -1)
	{
		if (glt->source_format == SRC_INDEXED)
		{
			glt->shirt = shirt;
			glt->pants = pants;
		}
		else
			Con_Printf ("TexMgr_ReloadImage: can't colormap a non SRC_INDEXED texture: %s\n", glt->name);
	}
	if (glt->shirt > -1 && glt->pants > -1)
	{
		// create new translation table
		for (i = 0; i < 256; i++)
			translation[i] = i;

		shirt = glt->shirt * 16;
		if (shirt < 128)
		{
			for (i = 0; i < 16; i++)
				translation[TOP_RANGE + i] = shirt + i;
		}
		else
		{
			for (i = 0; i < 16; i++)
				translation[TOP_RANGE + i] = shirt + 15 - i;
		}

		pants = glt->pants * 16;
		if (pants < 128)
		{
			for (i = 0; i < 16; i++)
				translation[BOTTOM_RANGE + i] = pants + i;
		}
		else
		{
			for (i = 0; i < 16; i++)
				translation[BOTTOM_RANGE + i] = pants + 15 - i;
		}

		// translate texture
		size = glt->width * glt->height;
		dst = translated = (byte *)Hunk_Alloc (size);
		src = data;

		for (i = 0; i < size; i++)
			*dst++ = translation[*src++];

		data = translated;
	}
	//
	// upload it
	//
	switch (glt->source_format)
	{
	case SRC_INDEXED:
		TexMgr_LoadImage8 (glt, data);
		break;
	case SRC_LIGHTMAP:
		TexMgr_LoadLightmap (glt, data);
		break;
	case SRC_RGBA:
	case SRC_SURF_INDICES:
		TexMgr_LoadImage32 (glt, (unsigned *)data);
		break;
	}

	Hunk_FreeToLowMark (mark);
}

/*
================
TexMgr_ReloadNobrightImages -- reloads all texture that were loaded with the nobright palette.  called when gl_fullbrights changes
================
*/
void TexMgr_ReloadNobrightImages (void)
{
	gltexture_t *glt;

	for (glt = active_gltextures; glt; glt = glt->next)
		if (glt->flags & TEXPREF_NOBRIGHT)
			TexMgr_ReloadImage (glt, -1, -1);
}

/*
================================================================================

    TEXTURE BINDING / TEXTURE UNIT SWITCHING

================================================================================
*/

typedef struct
{
	VkImage         image;
	VkImageView     target_image_view;
	VkImageView     image_view;
	VkFramebuffer   frame_buffer;
	VkDescriptorSet descriptor_set;
	VkDescriptorSet storage_descriptor_set;
	glheap_t       *heap;
	glheapnode_t   *heap_node;
} texture_garbage_t;

static int               current_garbage_index;
static int               num_garbage_textures[2];
static texture_garbage_t texture_garbage[MAX_GLTEXTURES][2];

/*
================
TexMgr_CollectGarbage
================
*/
void TexMgr_CollectGarbage (void)
{
	int                num;
	int                i;
	texture_garbage_t *garbage;

	current_garbage_index = (current_garbage_index + 1) % 2;
	num = num_garbage_textures[current_garbage_index];
	for (i = 0; i < num; ++i)
	{
		garbage = &texture_garbage[i][current_garbage_index];
		if (garbage->frame_buffer != VK_NULL_HANDLE)
			vkDestroyFramebuffer (vulkan_globals.device, garbage->frame_buffer, NULL);
		if (garbage->target_image_view)
			vkDestroyImageView (vulkan_globals.device, garbage->target_image_view, NULL);
		vkDestroyImageView (vulkan_globals.device, garbage->image_view, NULL);
		vkDestroyImage (vulkan_globals.device, garbage->image, NULL);
		R_FreeDescriptorSet (garbage->descriptor_set, &vulkan_globals.single_texture_set_layout);
		if (garbage->storage_descriptor_set)
			R_FreeDescriptorSet (garbage->descriptor_set, &vulkan_globals.single_texture_cs_write_set_layout);

		GL_FreeFromHeaps (num_texmgr_heaps, texmgr_heaps, garbage->heap, garbage->heap_node, &num_vulkan_tex_allocations);
	}
	num_garbage_textures[current_garbage_index] = 0;
}

/*
================
GL_DeleteTexture
================
*/
static void GL_DeleteTexture (gltexture_t *texture)
{
	int                garbage_index;
	texture_garbage_t *garbage;

	if (texture->image_view == VK_NULL_HANDLE)
		return;

	if (in_update_screen)
	{
		garbage_index = num_garbage_textures[current_garbage_index]++;
		garbage = &texture_garbage[garbage_index][current_garbage_index];
		garbage->image = texture->image;
		garbage->target_image_view = texture->target_image_view;
		garbage->image_view = texture->image_view;
		garbage->frame_buffer = texture->frame_buffer;
		garbage->descriptor_set = texture->descriptor_set;
		garbage->storage_descriptor_set = texture->storage_descriptor_set;
		garbage->heap = texture->heap;
		garbage->heap_node = texture->heap_node;
	}
	else
	{
		GL_WaitForDeviceIdle ();

		if (texture->frame_buffer != VK_NULL_HANDLE)
			vkDestroyFramebuffer (vulkan_globals.device, texture->frame_buffer, NULL);
		if (texture->target_image_view)
			vkDestroyImageView (vulkan_globals.device, texture->target_image_view, NULL);
		vkDestroyImageView (vulkan_globals.device, texture->image_view, NULL);
		vkDestroyImage (vulkan_globals.device, texture->image, NULL);
		R_FreeDescriptorSet (texture->descriptor_set, &vulkan_globals.single_texture_set_layout);
		if (texture->storage_descriptor_set)
			R_FreeDescriptorSet (texture->storage_descriptor_set, &vulkan_globals.single_texture_cs_write_set_layout);

		GL_FreeFromHeaps (num_texmgr_heaps, texmgr_heaps, texture->heap, texture->heap_node, &num_vulkan_tex_allocations);
	}

	texture->frame_buffer = VK_NULL_HANDLE;
	texture->target_image_view = VK_NULL_HANDLE;
	texture->image_view = VK_NULL_HANDLE;
	texture->image = VK_NULL_HANDLE;
	texture->heap = NULL;
	texture->heap_node = NULL;
}
