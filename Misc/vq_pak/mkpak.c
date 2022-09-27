/*
Copyright (c) 2022, Axel Gneiting

Permission to use, copy, modify, and/or distribute this software for any
purpose with or without fee is hereby granted, provided that the above
copyright notice and this permission notice appear in all copies.

THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/

#include <stdio.h>
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

static FILE *out;

typedef struct
{
	char name[56];
	int  filepos, filelen;
} dpackfile_t;

void write_byte (uint8_t value)
{
	fwrite (&value, 1, 1, out);
}

void write_int32 (uint32_t value)
{
	fwrite (&value, 4, 1, out);
}

void write_zero_padding (int count)
{
	for (int i = 0; i < count; ++i)
		write_byte (0);
}

void write_header (int32_t directory_offset, int32_t directory_size)
{
	write_byte ('P');
	write_byte ('A');
	write_byte ('C');
	write_byte ('K');
	write_int32 (directory_offset);
	write_int32 (directory_size);
}

int main (int argc, char *argv[])
{
	if (argc < 3)
	{
		fprintf (stderr, "Usage: mkpak [output.pak] [files...]\n");
		return 1;
	}

	out = fopen (argv[1], "wb+");
	if (out == NULL)
	{
		fprintf (stderr, "Could not open output file '%s'", argv[1]);
		return 1;
	}

	int32_t directory_offset = 12;
	int32_t num_in_files = argc - 2;
	int32_t directory_size = num_in_files * sizeof (dpackfile_t);
	int32_t file_offset = directory_offset + directory_size;

	write_header (directory_offset, directory_size);

	uint8_t *in_buffer = NULL;
	for (int i = 2; i < argc; ++i)
	{
		int   file_index = i - 2;
		FILE *in = fopen (argv[i], "rb");
		if (in == NULL)
		{
			fprintf (stderr, "Could not open input file '%s'", argv[i]);
			return 1;
		}

		fseek (in, 0, SEEK_END);
		size_t in_size = (size_t)ftell (in);
		fseek (in, 0, SEEK_SET);
		in_buffer = realloc (in_buffer, in_size);
		fread (in_buffer, in_size, 1, in);

		dpackfile_t pack_entry;
		memset (&pack_entry, 0, sizeof (pack_entry));
		strncpy (pack_entry.name, argv[i], sizeof (pack_entry.name) - 1);
		pack_entry.filelen = in_size;
		pack_entry.filepos = file_offset;

		fseek (out, directory_offset + (file_index * sizeof (dpackfile_t)), SEEK_SET);
		fwrite (&pack_entry, sizeof (pack_entry), 1, out);

		fseek (out, file_offset, SEEK_SET);
		fwrite (in_buffer, in_size, 1, out);

		file_offset += in_size;
		fclose (in);
	}
	free (in_buffer);

	return 0;
}
