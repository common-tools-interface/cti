#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>

int main(int argc, char* argv[])
{
	// Expect testing to take no more than a few minutes
	sleep(300);

	// Test will kill application in cleanup
	fprintf(stderr, "CTI diagnostics test timed out!\n");

	return -1;
}

