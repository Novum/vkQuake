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
//image.c -- image loading

#include "quakedef.h"

static char loadfilename[MAX_OSPATH]; //file scope so that error messages can use it

typedef struct stdio_buffer_s {
	FILE *f;
	unsigned char buffer[1024];
	int size;
	int pos;
} stdio_buffer_t;

static stdio_buffer_t *Buf_Alloc(FILE *f)
{
	stdio_buffer_t *buf = (stdio_buffer_t *) calloc(1, sizeof(stdio_buffer_t));
	buf->f = f;
	return buf;
}

static void Buf_Free(stdio_buffer_t *buf)
{
	free(buf);
}

static inline int Buf_GetC(stdio_buffer_t *buf)
{
	if (buf->pos >= buf->size)
	{
		buf->size = fread(buf->buffer, 1, sizeof(buf->buffer), buf->f);
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

TODO: search order: tga png jpg pcx lmp
============
*/
byte *Image_LoadImage (const char *name, int *width, int *height)
{
	FILE	*f;

	q_snprintf (loadfilename, sizeof(loadfilename), "%s.tga", name);
	COM_FOpenFile (loadfilename, &f, NULL);
	if (f)
		return Image_LoadTGA (f, width, height);

	q_snprintf (loadfilename, sizeof(loadfilename), "%s.pcx", name);
	COM_FOpenFile (loadfilename, &f, NULL);
	if (f)
		return Image_LoadPCX (f, width, height);

	return NULL;
}

//==============================================================================
//
//  TGA
//
//==============================================================================

typedef struct targaheader_s {
	unsigned char 	id_length, colormap_type, image_type;
	unsigned short	colormap_index, colormap_length;
	unsigned char	colormap_size;
	unsigned short	x_origin, y_origin, width, height;
	unsigned char	pixel_size, attributes;
} targaheader_t;

#define TARGAHEADERSIZE 18 //size on disk

targaheader_t targa_header;

int fgetLittleShort (FILE *f)
{
	byte	b1, b2;

	b1 = fgetc(f);
	b2 = fgetc(f);

	return (short)(b1 + b2*256);
}

int fgetLittleLong (FILE *f)
{
	byte	b1, b2, b3, b4;

	b1 = fgetc(f);
	b2 = fgetc(f);
	b3 = fgetc(f);
	b4 = fgetc(f);

	return b1 + (b2<<8) + (b3<<16) + (b4<<24);
}

/*
============
Image_WriteTGA -- writes RGB or RGBA data to a TGA file

returns true if successful
============
*/
qboolean Image_WriteTGA (const char *name, byte *data, int width, int height, int bpp, qboolean upsidedown, qboolean bgra)
{
	int		handle, i, size, temp, bytes;
	char	pathname[MAX_OSPATH];
	byte	header[TARGAHEADERSIZE];

	Sys_mkdir (com_gamedir); //if we've switched to a nonexistant gamedir, create it now so we don't crash
	q_snprintf (pathname, sizeof(pathname), "%s/%s", com_gamedir, name);
	handle = Sys_FileOpenWrite (pathname);
	if (handle == -1)
		return false;

	Q_memset (&header, 0, TARGAHEADERSIZE);
	header[2] = 2; // uncompressed type
	header[12] = width&255;
	header[13] = width>>8;
	header[14] = height&255;
	header[15] = height>>8;
	header[16] = bpp; // pixel size
	if (upsidedown)
		header[17] = 0x20; //upside-down attribute

	// swap red and blue bytes
	bytes = bpp/8;
	size = width*height*bytes;
	if (!bgra)
	{
		for (i=0; i<size; i+=bytes)
		{
			temp = data[i];
			data[i] = data[i+2];
			data[i+2] = temp;
		}
	}

	Sys_FileWrite (handle, &header, TARGAHEADERSIZE);
	Sys_FileWrite (handle, data, size);
	Sys_FileClose (handle);

	return true;
}

/*
=============
Image_LoadTGA
=============
*/
byte *Image_LoadTGA (FILE *fin, int *width, int *height)
{
	int				columns, rows, numPixels;
	byte			*pixbuf;
	int				row, column;
	byte			*targa_rgba;
	int				realrow; //johnfitz -- fix for upside-down targas
	qboolean		upside_down; //johnfitz -- fix for upside-down targas
	stdio_buffer_t	*buf;

	targa_header.id_length = fgetc(fin);
	targa_header.colormap_type = fgetc(fin);
	targa_header.image_type = fgetc(fin);

	targa_header.colormap_index = fgetLittleShort(fin);
	targa_header.colormap_length = fgetLittleShort(fin);
	targa_header.colormap_size = fgetc(fin);
	targa_header.x_origin = fgetLittleShort(fin);
	targa_header.y_origin = fgetLittleShort(fin);
	targa_header.width = fgetLittleShort(fin);
	targa_header.height = fgetLittleShort(fin);
	targa_header.pixel_size = fgetc(fin);
	targa_header.attributes = fgetc(fin);

	if (targa_header.image_type!=2 && targa_header.image_type!=10)
		Sys_Error ("Image_LoadTGA: %s is not a type 2 or type 10 targa\n", loadfilename);

	if (targa_header.colormap_type !=0 || (targa_header.pixel_size!=32 && targa_header.pixel_size!=24))
		Sys_Error ("Image_LoadTGA: %s is not a 24bit or 32bit targa\n", loadfilename);

	columns = targa_header.width;
	rows = targa_header.height;
	numPixels = columns * rows;
	upside_down = !(targa_header.attributes & 0x20); //johnfitz -- fix for upside-down targas

	targa_rgba = (byte *) Hunk_Alloc (numPixels*4);

	if (targa_header.id_length != 0)
		fseek(fin, targa_header.id_length, SEEK_CUR);  // skip TARGA image comment

	buf = Buf_Alloc(fin);

	if (targa_header.image_type==2) // Uncompressed, RGB images
	{
		for(row=rows-1; row>=0; row--)
		{
			//johnfitz -- fix for upside-down targas
			realrow = upside_down ? row : rows - 1 - row;
			pixbuf = targa_rgba + realrow*columns*4;
			//johnfitz
			for(column=0; column<columns; column++)
			{
				unsigned char red,green,blue,alphabyte;
				switch (targa_header.pixel_size)
				{
				case 24:
					blue = Buf_GetC(buf);
					green = Buf_GetC(buf);
					red = Buf_GetC(buf);
					*pixbuf++ = red;
					*pixbuf++ = green;
					*pixbuf++ = blue;
					*pixbuf++ = 255;
					break;
				case 32:
					blue = Buf_GetC(buf);
					green = Buf_GetC(buf);
					red = Buf_GetC(buf);
					alphabyte = Buf_GetC(buf);
					*pixbuf++ = red;
					*pixbuf++ = green;
					*pixbuf++ = blue;
					*pixbuf++ = alphabyte;
					break;
				}
			}
		}
	}
	else if (targa_header.image_type==10) // Runlength encoded RGB images
	{
		unsigned char red,green,blue,alphabyte,packetHeader,packetSize,j;
		for(row=rows-1; row>=0; row--)
		{
			//johnfitz -- fix for upside-down targas
			realrow = upside_down ? row : rows - 1 - row;
			pixbuf = targa_rgba + realrow*columns*4;
			//johnfitz
			for(column=0; column<columns; )
			{
				packetHeader=Buf_GetC(buf);
				packetSize = 1 + (packetHeader & 0x7f);
				if (packetHeader & 0x80) // run-length packet
				{
					switch (targa_header.pixel_size)
					{
					case 24:
						blue = Buf_GetC(buf);
						green = Buf_GetC(buf);
						red = Buf_GetC(buf);
						alphabyte = 255;
						break;
					case 32:
						blue = Buf_GetC(buf);
						green = Buf_GetC(buf);
						red = Buf_GetC(buf);
						alphabyte = Buf_GetC(buf);
						break;
					default: /* avoid compiler warnings */
						blue = red = green = alphabyte = 0;
					}

					for(j=0;j<packetSize;j++)
					{
						*pixbuf++=red;
						*pixbuf++=green;
						*pixbuf++=blue;
						*pixbuf++=alphabyte;
						column++;
						if (column==columns) // run spans across rows
						{
							column=0;
							if (row>0)
								row--;
							else
								goto breakOut;
							//johnfitz -- fix for upside-down targas
							realrow = upside_down ? row : rows - 1 - row;
							pixbuf = targa_rgba + realrow*columns*4;
							//johnfitz
						}
					}
				}
				else // non run-length packet
				{
					for(j=0;j<packetSize;j++)
					{
						switch (targa_header.pixel_size)
						{
						case 24:
							blue = Buf_GetC(buf);
							green = Buf_GetC(buf);
							red = Buf_GetC(buf);
							*pixbuf++ = red;
							*pixbuf++ = green;
							*pixbuf++ = blue;
							*pixbuf++ = 255;
							break;
						case 32:
							blue = Buf_GetC(buf);
							green = Buf_GetC(buf);
							red = Buf_GetC(buf);
							alphabyte = Buf_GetC(buf);
							*pixbuf++ = red;
							*pixbuf++ = green;
							*pixbuf++ = blue;
							*pixbuf++ = alphabyte;
							break;
						default: /* avoid compiler warnings */
							blue = red = green = alphabyte = 0;
						}
						column++;
						if (column==columns) // pixel packet run spans across rows
						{
							column=0;
							if (row>0)
								row--;
							else
								goto breakOut;
							//johnfitz -- fix for upside-down targas
							realrow = upside_down ? row : rows - 1 - row;
							pixbuf = targa_rgba + realrow*columns*4;
							//johnfitz
						}
					}
				}
			}
			breakOut:;
		}
	}

	Buf_Free(buf);
	fclose(fin);

	*width = (int)(targa_header.width);
	*height = (int)(targa_header.height);
	return targa_rgba;
}

//==============================================================================
//
//  PCX
//
//==============================================================================

typedef struct
{
    char			signature;
    char			version;
    char			encoding;
    char			bits_per_pixel;
    unsigned short	xmin,ymin,xmax,ymax;
    unsigned short	hdpi,vdpi;
    byte			colortable[48];
    char			reserved;
    char			color_planes;
    unsigned short	bytes_per_line;
    unsigned short	palette_type;
    char			filler[58];
} pcxheader_t;

/*
============
Image_LoadPCX
============
*/
byte *Image_LoadPCX (FILE *f, int *width, int *height)
{
	pcxheader_t	pcx;
	int			x, y, w, h, readbyte, runlength, start;
	byte		*p, *data;
	byte		palette[768];
	stdio_buffer_t  *buf;

	start = ftell (f); //save start of file (since we might be inside a pak file, SEEK_SET might not be the start of the pcx)

	fread(&pcx, sizeof(pcx), 1, f);
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

	data = (byte *) Hunk_Alloc((w*h+1)*4); //+1 to allow reading padding byte on last line

	//load palette
	fseek (f, start + com_filesize - 768, SEEK_SET);
	fread (palette, 1, 768, f);

	//back to start of image data
	fseek (f, start + sizeof(pcx), SEEK_SET);

	buf = Buf_Alloc(f);

	for (y=0; y<h; y++)
	{
		p = data + y * w * 4;

		for (x=0; x<(pcx.bytes_per_line); ) //read the extra padding byte if necessary
		{
			readbyte = Buf_GetC(buf);

			if(readbyte >= 0xC0)
			{
				runlength = readbyte & 0x3F;
				readbyte = Buf_GetC(buf);
			}
			else
				runlength = 1;

			while(runlength--)
			{
				p[0] = palette[readbyte*3];
				p[1] = palette[readbyte*3+1];
				p[2] = palette[readbyte*3+2];
				p[3] = 255;
				p += 4;
				x++;
			}
		}
	}

	Buf_Free(buf);
	fclose(f);

	*width = w;
	*height = h;
	return data;
}
