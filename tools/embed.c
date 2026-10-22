#include <stdio.h>

int
main(int argc, char **argv)
{
	FILE *f;
	int c, n = 0;

	if (argc != 3 || !(f = fopen(argv[2], "rb"))) {
		fprintf(stderr, "usage: embed name file\n");
		return 1;
	}
	printf("static const unsigned char %s[] = {", argv[1]);
	while ((c = getc(f)) != EOF)
		printf("%s0x%02x,", n++ % 16 ? "" : "\n\t", c);
	printf("\n};\n");
	fclose(f);
	return 0;
}
