#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char* argv[])
{
	// Expect testing to take no more than a few minutes
	sleep(300);

	// Test will kill application in cleanup
	fprintf(stderr, "CTI diagnostics test timed out!\n");

	return -1;
}

