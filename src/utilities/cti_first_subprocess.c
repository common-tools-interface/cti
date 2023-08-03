/******************************************************************************\
 *
 * Copyright 2021 Hewlett Packard Enterprise Development LP.
 * SPDX-License-Identifier: Linux-OpenIB
 ******************************************************************************/
#define _GNU_SOURCE 
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>
#include <string.h>

int main(int argc, char* argv[])
{

	// Print PID and executable path of first child process for every supplied PID

	for (int i = 1; i < argc; i++) {
		char* child_pid_path = NULL;
		FILE* child_pid_file = NULL;
		char* child_exe_link = NULL;
		char* child_exe_path = NULL;
		ssize_t child_exe_path_len = 0;

		// Allocate path to child's executable
		child_exe_path = (char*)malloc(PATH_MAX);
		if (child_exe_path == NULL) {
			perror("malloc");
			goto cleanup_iteration;
		}

		// Parse parent PID
		errno = 0;
		char *end = NULL;
		long pid = strtol(argv[i], &end, 10);
		if ((argv[i] == end) || (*end != '\0') || (errno == ERANGE)) {
			fprintf(stderr, "failed to parse: '%s'\n", argv[i]);
			goto cleanup_iteration;
		}

		// Create path to children PID file
		child_pid_path = NULL;
		if (asprintf(&child_pid_path, "/proc/%ld/task/%ld/children", pid, pid) < 0) {
			perror("asprintf");
			goto cleanup_iteration;
		}

		// Open and read first PID of children PID file
		child_pid_file = fopen(child_pid_path, "r");
		if (child_pid_file == NULL) {
			fprintf(stderr, "failed to open %s for reading (%s)\n", child_pid_path, strerror(errno));
			goto cleanup_iteration;
		}

		pid_t child_pid = 0;
		if (fscanf(child_pid_file, "%d", &child_pid) != 1) {
			fprintf(stderr, "failed to read first PID from %s\n", child_pid_path);
			goto cleanup_iteration;
		}

		// Create path to child's executable link
		if (asprintf(&child_exe_link, "/proc/%ld/exe", child_pid) < 0) {
			perror("asprintf");
			// Proceed on to print a blank line for child executable

		// Read path to child's executable
		} else {
			child_exe_path_len = readlink(child_exe_link, child_exe_path, PATH_MAX);
			if (child_exe_path_len < 0) {
				perror("readlink");
				// Proceed on to print a blank line for child executable
			}
		}

		// Output parent, child PIDs, and executable for consumption
		printf("%ld\n%d\n", pid, child_pid);
		if (child_exe_path != NULL) {
			printf("%.*s\n", (int)child_exe_path_len, child_exe_path);

		// Output empty value for child executable
		} else {
			printf("\n");
		}

cleanup_iteration:

		if (child_exe_path != NULL) {
			free(child_exe_path);
			child_exe_path = NULL;
		}

		if (child_exe_link != NULL) {
			free(child_exe_link);
			child_exe_link = NULL;
		}

		if (child_pid_file != NULL) {
			fclose(child_pid_file);
			child_pid_file = NULL;
		}

		if (child_pid_path != NULL) {
			free(child_pid_path);
			child_pid_path = NULL;
		}
	}

	// Output completed
	printf("\n");

	return 0;
}

