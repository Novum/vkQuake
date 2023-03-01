#include <stdio.h>
#include <assert.h>

int main (int argc, char **argv)
{
	FILE		 *fin, *fout, *fin_compressed;
	char		  fin_deflate[1024];
	unsigned long decompressed_length, n;
	char		 *c;

	if (argc != 4)
		return 0;

	fin = fopen (argv[1], "rb");
	fout = fopen (argv[3], "w+");

	if ((fin == NULL) || (fout == NULL))
	{
		if (fin != NULL)
			fclose (fin);
		if (fout != NULL)
			fclose (fout);
		return 1;
	}

	snprintf (fin_deflate, sizeof (fin_deflate), "%s.deflate", argv[1]);
	decompressed_length = 0;
	fin_compressed = fopen (fin_deflate, "rb");
	if (fin_compressed != NULL)
	{
		fseek (fin, 0, SEEK_END);
		decompressed_length = (unsigned long)ftell (fin);
		fclose (fin);
		fin = fin_compressed;
	}

	for (c = argv[2]; *c != 0; ++c)
		if (*c == '.')
			*c = '_';

	fprintf (fout, "// clang-format off\n");
	fprintf (fout, "const unsigned char %s[] = {\n", argv[2]);

	n = 0;
	while (!feof (fin))
	{
		unsigned char ch;
		if (fread (&ch, 1, 1, fin) == 0)
			break;
		fprintf (fout, "0x%.2X, ", (int)ch);
		++n;
		if (n % 10 == 0)
			fprintf (fout, "\n");
	}

	fclose (fin);
	fprintf (fout, "};\n");

	fprintf (fout, "const int %s_size = %ld;\n", argv[2], n);
	if (decompressed_length)
		fprintf (fout, "const int %s_decompressed_size = %ld;\n", argv[2], decompressed_length);
	fclose (fout);
	return 0;
}
