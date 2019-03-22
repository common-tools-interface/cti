#include "print_two/print.h"

int main(int argc, char const* const argv[])
{
	if (argc != 2) {
		fprintf(stderr, "must provide output file path\n");
		return 1;
	}

	FILE *fp = fopen(argv[1], "w");
	print_message(fp);
	fclose(fp);
	return 0;
}
