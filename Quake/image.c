/*
Copyright (C) 1996-2001 Id Software, Inc.
Copyright (C) 2002-2009 John Fitzgibbons and others
Copyright (C) 2010-2014 QuakeSpasm developers

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
// image.c -- image loading

#include "quakedef.h"

static byte *Image_LoadPCX (FILE *f, int *width, int *height);
static byte *Image_LoadLMP (FILE *f, int *width, int *height);

// STB_IMAGE config:
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#define STBI_NO_BMP
#define STBI_NO_PSD
#define STBI_NO_GIF
#define STBI_NO_HDR
#define STBI_NO_PIC
#define STBI_NO_PNM
#define STBI_NO_LINEAR
// plug our Mem_Alloc in stb_image:
#define STBI_MALLOC(sz)		   Mem_Alloc (sz)
#define STBI_REALLOC(p, newsz) Mem_Realloc (p, newsz)
#define STBI_FREE(p)		   Mem_Free (p)

#ifdef _MSC_VER
// Disable warning C4505: Unused functions
#pragma warning(push)
#pragma warning(disable : 4505)
#endif

#ifdef __GNUC__
// Suppress unused function warnings on GCC/clang
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#if __GNUC__ >= 14
#pragma GCC diagnostic ignored "-Wstringop-overflow"
#endif
#endif

#include "stb_image.h"

#ifdef __GNUC__
// Restore unused function warnings on GCC/clang
#pragma GCC diagnostic pop
#endif

#ifdef _MSC_VER
#pragma warning(pop)
#endif

// STB_IMAGE_WRITE config:
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
// plug our Mem_Alloc in stb_image_write:
#define STBIW_MALLOC(sz)		Mem_Alloc (sz)
#define STBIW_REALLOC(p, newsz) Mem_Realloc (p, newsz)
#define STBIW_FREE(p)			Mem_Free (p)
#include "stb_image_write.h"

// LODEPNG config:
#define LODEPNG_NO_COMPILE_ALLOCATORS
#define LODEPNG_NO_COMPILE_DECODER
#define LODEPNG_NO_COMPILE_CPP
#define LODEPNG_NO_COMPILE_ANCILLARY_CHUNKS
#define LODEPNG_NO_COMPILE_ERROR_TEXT
#include "lodepng.h"
#include "lodepng.c"

void *lodepng_malloc (size_t size)
{
	return Mem_Alloc (size);
}

void *lodepng_realloc (void *ptr, size_t new_size)
{
	return Mem_Realloc (ptr, new_size);
}

void lodepng_free (void *ptr)
{
	Mem_Free (ptr);
}

static THREAD_LOCAL char loadfilename[MAX_OSPATH]; // file scope so that error messages can use it

typedef struct stdio_buffer_s
{
	FILE		 *f;
	unsigned char buffer[1024];
	int			  size;
	int			  pos;
} stdio_buffer_t;

static stdio_buffer_t *Buf_Alloc (FILE *f)
{
	stdio_buffer_t *buf = (stdio_buffer_t *)Mem_Alloc (sizeof (stdio_buffer_t));
	buf->f = f;
	return buf;
}

static void Buf_Free (stdio_buffer_t *buf)
{
	Mem_Free (buf);
}

static inline int Buf_GetC (stdio_buffer_t *buf)
{
	if (buf->pos >= buf->size)
	{
		buf->size = fread (buf->buffer, 1, sizeof (buf->buffer), buf->f);
		buf->pos = 0;

		if (buf->size == 0)
			return EOF;
	}

	return buf->buffer[buf->pos++];
}

/*
============
Image_LoadImage

returns a pointer to hunk allocated RGBA data

Search order:  png tga jpg pcx lmp
============
*/
byte *Image_LoadImage (const char *name, int *width, int *height, enum srcformat *fmt)
{
	static const char *const stbi_formats[] = {"png", "tga", "jpg", NULL};

	FILE *f;
	int	  i;

	for (i = 0; stbi_formats[i]; i++)
	{
		q_snprintf (loadfilename, sizeof (loadfilename), "%s.%s", name, stbi_formats[i]);
		COM_FOpenFile (loadfilename, &f, NULL);
		if (f)
		{
			// data is managed by our Mem_Alloc routines, nothing more to do.
			byte *data = stbi_load_from_file (f, width, height, NULL, 4);

			if (data)
			{
				*fmt = SRC_RGBA;
			}
			else
				Con_Warning ("couldn't load %s (%s)\n", loadfilename, stbi_failure_reason ());
			return data;
		}
	}

	q_snprintf (loadfilename, sizeof (loadfilename), "%s.pcx", name);
	COM_FOpenFile (loadfilename, &f, NULL);
	if (f)
	{
		*fmt = SRC_RGBA;
		return Image_LoadPCX (f, width, height);
	}

	q_snprintf (loadfilename, sizeof (loadfilename), "%s%s.lmp", "", name);
	COM_FOpenFile (loadfilename, &f, NULL);
	if (f)
	{
		*fmt = SRC_INDEXED;
		return Image_LoadLMP (f, width, height);
	}

	return NULL;
}

//==============================================================================
//
//  TGA
//
//==============================================================================

#define TARGAHEADERSIZE 18 /* size on disk */

/*
============
Image_WriteTGA -- writes RGB or RGBA data to a TGA file

returns true if successful
============
*/
qboolean Image_WriteTGA (const char *name, byte *data, int width, int height, int bpp, qboolean upsidedown)
{
	int	 handle, i, size, temp, bytes;
	char pathname[MAX_OSPATH];
	byte header[TARGAHEADERSIZE];

	Sys_mkdir (com_gamedir); // if we've switched to a nonexistant gamedir, create it now so we don't crash
	q_snprintf (pathname, sizeof (pathname), "%s/%s", com_gamedir, name);
	handle = Sys_FileOpenWrite (pathname);
	if (handle == -1)
		return false;

	memset (header, 0, TARGAHEADERSIZE);
	header[2] = 2; // uncompressed type
	header[12] = width & 255;
	header[13] = width >> 8;
	header[14] = height & 255;
	header[15] = height >> 8;
	header[16] = bpp; // pixel size
	if (upsidedown)
		header[17] = 0x20; // upside-down attribute

	// swap red and blue bytes
	bytes = bpp / 8;
	size = width * height * bytes;
	for (i = 0; i < size; i += bytes)
	{
		temp = data[i];
		data[i] = data[i + 2];
		data[i + 2] = temp;
	}

	Sys_FileWrite (handle, header, TARGAHEADERSIZE);
	Sys_FileWrite (handle, data, size);
	Sys_FileClose (handle);

	return true;
}

//==============================================================================
//
//  PCX
//
//==============================================================================

typedef struct
{
	char		   signature;
	char		   version;
	char		   encoding;
	char		   bits_per_pixel;
	unsigned short xmin, ymin, xmax, ymax;
	unsigned short hdpi, vdpi;
	byte		   colortable[48];
	char		   reserved;
	char		   color_planes;
	unsigned short bytes_per_line;
	unsigned short palette_type;
	char		   filler[58];
} pcxheader_t;

/*
============
Image_LoadPCX
============
*/
static byte *Image_LoadPCX (FILE *f, int *width, int *height)
{
	pcxheader_t		pcx;
	int				x, y, w, h, readbyte, runlength, start;
	byte		   *p, *data;
	byte			palette[768];
	stdio_buffer_t *buf;

	start = ftell (f); // save start of file (since we might be inside a pak file, SEEK_SET might not be the start of the pcx)

	if (fread (&pcx, sizeof (pcx), 1, f) != 1)
		Sys_Error ("'%s' is not a valid PCX file", loadfilename);

	pcx.xmin = (unsigned short)LittleShort (pcx.xmin);
	pcx.ymin = (unsigned short)LittleShort (pcx.ymin);
	pcx.xmax = (unsigned short)LittleShort (pcx.xmax);
	pcx.ymax = (unsigned short)LittleShort (pcx.ymax);
	pcx.bytes_per_line = (unsigned short)LittleShort (pcx.bytes_per_line);

	if (pcx.signature != 0x0A)
		Sys_Error ("'%s' is not a valid PCX file", loadfilename);

	if (pcx.version != 5)
		Sys_Error ("'%s' is version %i, should be 5", loadfilename, pcx.version);

	if (pcx.encoding != 1 || pcx.bits_per_pixel != 8 || pcx.color_planes != 1)
		Sys_Error ("'%s' has wrong encoding or bit depth", loadfilename);

	w = pcx.xmax - pcx.xmin + 1;
	h = pcx.ymax - pcx.ymin + 1;

	data = (byte *)Mem_Alloc ((w * h + 1) * 4); //+1 to allow reading padding byte on last line

	// load palette
	fseek (f, start + com_filesize - 768, SEEK_SET);
	if (fread (palette, 1, 768, f) != 768)
		Sys_Error ("'%s' is not a valid PCX file", loadfilename);

	// back to start of image data
	fseek (f, start + sizeof (pcx), SEEK_SET);

	buf = Buf_Alloc (f);

	for (y = 0; y < h; y++)
	{
		p = data + y * w * 4;

		for (x = 0; x < (pcx.bytes_per_line);) // read the extra padding byte if necessary
		{
			readbyte = Buf_GetC (buf);

			if (readbyte >= 0xC0)
			{
				runlength = readbyte & 0x3F;
				readbyte = Buf_GetC (buf);
			}
			else
				runlength = 1;

			while (runlength--)
			{
				p[0] = palette[readbyte * 3];
				p[1] = palette[readbyte * 3 + 1];
				p[2] = palette[readbyte * 3 + 2];
				p[3] = 255;
				p += 4;
				x++;
			}
		}
	}

	Buf_Free (buf);
	fclose (f);

	*width = w;
	*height = h;
	return data;
}

//==============================================================================
//
//  QPIC (aka '.lmp')
//
//==============================================================================

typedef struct
{
	unsigned int width, height;
} lmpheader_t;

/*
============
Image_LoadLMP
============
*/
static byte *Image_LoadLMP (FILE *f, int *width, int *height)
{
	lmpheader_t qpic;
	size_t		pix;
	void	   *data;

	if (fread (&qpic, 1, sizeof (qpic), f) != sizeof (qpic))
		Sys_Error ("'%s' is not a valid LMP file", loadfilename);

	qpic.width = LittleLong (qpic.width);
	qpic.height = LittleLong (qpic.height);

	pix = qpic.width * qpic.height;

	if (com_filesize != 8 + pix)
	{
		fclose (f);
		return NULL;
	}

	data = (byte *)Mem_Alloc (pix); //+1 to allow reading padding byte on last line
	if (fread (data, 1, pix, f) != pix)
		Sys_Error ("'%s' is not a valid LMP file", loadfilename);
	fclose (f);

	*width = qpic.width;
	*height = qpic.height;
	return data;
}

//==============================================================================
//
//  STB_IMAGE_WRITE
//
//==============================================================================

static byte *CopyFlipped (const byte *data, int width, int height, int bpp)
{
	int	  y, rowsize;
	byte *flipped;

	rowsize = width * (bpp / 8);
	flipped = (byte *)Mem_Alloc (height * rowsize);
	if (!flipped)
		return NULL;

	for (y = 0; y < height; y++)
	{
		memcpy (&flipped[y * rowsize], &data[(height - 1 - y) * rowsize], rowsize);
	}
	return flipped;
}

/*
============
Image_WriteJPG -- writes using stb_image_write

returns true if successful
============
*/
qboolean Image_WriteJPG (const char *name, byte *data, int width, int height, int bpp, int quality, qboolean upsidedown)
{
	unsigned error;
	char	 pathname[MAX_OSPATH];
	byte	*flipped;
	int		 bytes_per_pixel;

	if (!(bpp == 32 || bpp == 24))
		Sys_Error ("bpp not 24 or 32");

	bytes_per_pixel = bpp / 8;

	Sys_mkdir (com_gamedir); // if we've switched to a nonexistant gamedir, create it now so we don't crash
	q_snprintf (pathname, sizeof (pathname), "%s/%s", com_gamedir, name);

	if (!upsidedown)
	{
		flipped = CopyFlipped (data, width, height, bpp);
		if (!flipped)
			return false;
	}
	else
		flipped = data;

	error = stbi_write_jpg (pathname, width, height, bytes_per_pixel, flipped, quality);
	if (!upsidedown)
		Mem_Free (flipped);

	return (error != 0);
}

qboolean Image_WritePNG (const char *name, byte *data, int width, int height, int bpp, qboolean upsidedown)
{
	unsigned	   error;
	char		   pathname[MAX_OSPATH];
	byte		  *flipped;
	unsigned char *filters;
	unsigned char *png;
	size_t		   pngsize;
	LodePNGState   state;

	if (!(bpp == 32 || bpp == 24))
		Sys_Error ("bpp not 24 or 32");

	Sys_mkdir (com_gamedir); // if we've switched to a nonexistant gamedir, create it now so we don't crash
	q_snprintf (pathname, sizeof (pathname), "%s/%s", com_gamedir, name);

	flipped = (!upsidedown) ? CopyFlipped (data, width, height, bpp) : data;
	filters = (unsigned char *)Mem_Alloc (height);
	if (!filters || !flipped)
	{
		if (!upsidedown)
			Mem_Free (flipped);
		Mem_Free (filters);
		return false;
	}

	// set some options for faster compression
	lodepng_state_init (&state);
	state.encoder.zlibsettings.use_lz77 = 0;
	state.encoder.auto_convert = 0;
	state.encoder.filter_strategy = LFS_PREDEFINED;
	memset (filters, 1, height); // use filter 1; see https://www.w3.org/TR/PNG-Filters.html
	state.encoder.predefined_filters = filters;

	if (bpp == 24)
	{
		state.info_raw.colortype = LCT_RGB;
		state.info_png.color.colortype = LCT_RGB;
	}
	else
	{
		state.info_raw.colortype = LCT_RGBA;
		state.info_png.color.colortype = LCT_RGBA;
	}

	error = lodepng_encode (&png, &pngsize, flipped, width, height, &state);
	if (error == 0)
		lodepng_save_file (png, pngsize, pathname);
#ifdef LODEPNG_COMPILE_ERROR_TEXT
	else
		Con_Printf ("WritePNG: %s\n", lodepng_error_text (error));
#endif

	lodepng_state_cleanup (&state);
	lodepng_free (png); /* png was allocated by lodepng */
	Mem_Free (filters);
	if (!upsidedown)
		Mem_Free (flipped);

	return (error == 0);
}
