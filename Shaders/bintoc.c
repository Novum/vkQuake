#include <stdio.h>
#include <assert.h>

int main (int argc, char **argv)
{
	if (argc != 4)
		return 0;

	char *fnin = argv[1];
	FILE *fin = fopen (fnin, "rb");

	char *fnout = argv[3];
	FILE *fout = fopen (fnout, "w+");

	if ((fin == NULL) || (fout == NULL))
	{
		return 1;
	}

	char fin_deflate[1024];
	snprintf (fin_deflate, sizeof (fin_deflate), "%s.deflate", argv[1]);
	FILE *fin_compressed = fopen (fin_deflate, "rb");
	unsigned long decompressed_length = 0;
	if (fin_compressed != NULL)
	{
		fseek (fin, 0, SEEK_END);
		decompressed_length = (unsigned long) ftell (fin);
		fclose (fin);
		fin = fin_compressed;
	}

	for (char *c = argv[2]; *c != 0; ++c)
		if (*c == '.')
			*c = '_';
	fprintf (fout, "unsigned char %s[] = {\n", argv[2]);
	unsigned long n = 0;

	while (!feof (fin))
	{
		unsigned char c;
		if (fread (&c, 1, 1, fin) == 0)
			break;
		fprintf (fout, "0x%.2X, ", (int)c);
		++n;
		if (n % 10 == 0)
			fprintf (fout, "\n");
	}

	fclose (fin);
	fprintf (fout, "};\n");

	fprintf (fout, "int %s_size = %ld;\n", argv[2], n);
	if (decompressed_length)
		fprintf (fout, "int %s_decompressed_size = %ld;\n", argv[2], decompressed_length);
	fclose (fout);
	return 0;
}
