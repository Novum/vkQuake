#include <stdio.h>
#include <assert.h>
#include <stdint.h>
#include <string.h>

// include miniz stb-syle, directly in this compilation unit.
// (supported by miniz)
#include "../Quake/miniz.c"

int main (int argc, char **argv)
{
	FILE *fin, *fout = NULL;

	size_t	 original_length, compressed_length;
	uint8_t *compressed_bytes_buffer, *original_bytes_buffer = NULL;
	char	*c;
	int		 use_compression = 0;

	// defaults args:
	// bintoc [input file] [struct name] [output file]
	size_t fin_index = 1;
	size_t struct_name_index = 2;
	size_t fout_index = 3;

	// + compression arg:
	// bintoc -c [input file] [struct name] [output file]
	if (argc == 5)
	{
		if (strcmp (argv[1], "-c") == 0)
		{
			use_compression = 1;
			fin_index++;
			struct_name_index++;
			fout_index++;
		}
		else
		{
			return 1;
		}
	}
	else if (argc != 4)
	{
		return 1;
	}

	fin = fopen (argv[fin_index], "rb");
	fout = fopen (argv[fout_index], "w+");

	if ((fin == NULL) || (fout == NULL))
	{
		if (fin != NULL)
			fclose (fin);
		if (fout != NULL)
			fclose (fout);
		return 1;
	}

	// load all input in memory :
	fseek (fin, 0, SEEK_END);
	original_length = (size_t)ftell (fin);
	fseek (fin, 0, SEEK_SET);

	original_bytes_buffer = (uint8_t *)malloc (original_length);

	if (original_bytes_buffer == NULL)
		return 1;

	if (original_length != fread ((void *)original_bytes_buffer, 1, original_length, fin))
	{
		fclose (fin);
		free (original_bytes_buffer);
		return 1;
	}
	fclose (fin);

	//
	if (use_compression)
	{
		// best compression:
		compressed_bytes_buffer =
			(uint8_t *)tdefl_compress_mem_to_heap ((const void *)original_bytes_buffer, original_length, &compressed_length, TDEFL_MAX_PROBES_MASK);
	}
	else
	{
		// stupid flow control analysis erroneous check workaround
		compressed_bytes_buffer = NULL;
	}

	for (c = argv[struct_name_index]; *c != 0; ++c)
		if (*c == '.')
			*c = '_';

	// write output:
	{
		fprintf (fout, "// clang-format off\n");
		fprintf (fout, "const unsigned char %s[] = {\n", argv[struct_name_index]);

		uint8_t *output_bytes = (use_compression ? compressed_bytes_buffer : original_bytes_buffer);
		size_t	 output_buffer_size = (use_compression ? compressed_length : original_length);

		int n = 0;

		for (int i = 0; i < output_buffer_size; i++)
		{
			fprintf (fout, "0x%.2X, ", (int)output_bytes[i]);
			++n;
			if (n % 10 == 0)
				fprintf (fout, "\n");
		}

		fprintf (fout, "};\n");

		fprintf (fout, "const int %s_size = %ld;\n", argv[struct_name_index], (long)output_buffer_size);
		if (use_compression)
			fprintf (fout, "const int %s_decompressed_size = %ld;\n", argv[struct_name_index], (long)original_length);
	}

	fclose (fout);

	free (original_bytes_buffer);
	free (compressed_bytes_buffer);

	return 0;
}
