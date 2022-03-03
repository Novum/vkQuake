#include <stdio.h>
#include <assert.h>

int main(int argc, char** argv) {
	if(argc != 4)
	return 0;

	char* fnin = argv[1];
	FILE* fin = fopen(fnin, "rb");

	char* fnout = argv[3];
	FILE* fout = fopen(fnout, "w+");

	for (char* c = argv[2]; *c != 0; ++c)
		if (*c == '.') *c = '_';
	fprintf(fout, "unsigned char %s[] = {\n", argv[2]);
	unsigned long n = 0;

	while(!feof(fin)) {
		unsigned char c;
		if(fread(&c, 1, 1, fin) == 0) break;
		fprintf(fout, "0x%.2X, ", (int)c);
		++n;
		if(n % 10 == 0) fprintf(fout, "\n");
	}

	fclose(fin);
	fprintf(fout, "};\n");

	fprintf(fout, "int %s_size = %ld;\n", argv[2], n);
	fclose(fout);
	return 0;
}
