/******************************************************************************\
 *
 * Copyright 2021 Hewlett Packard Enterprise Development LP.
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 ******************************************************************************/

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

		// Parse parent PID
		errno = 0;
		char *end = NULL;
		long pid = strtol(argv[i], &end, 10);
		if ((argv[i] == end) || (errno == ERANGE)) {
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

		// Output parent and child PIDs for consumption
		printf("%ld\n%ld\n", pid, child_pid);

		// Create path to child's executable link
		child_exe_link = NULL;
		if (asprintf(&child_exe_link, "/proc/%ld/exe", child_pid) < 0) {
			perror("asprintf");
			printf("\n"); // Output empty value for child executable
			goto cleanup_iteration;
		}

		// Read path to child's executable
		char child_exe_path[PATH_MAX];
		ssize_t child_exe_path_len = readlink(child_exe_link, child_exe_path, sizeof(child_exe_path));
		if (child_exe_path_len < 0) {
			perror("readlink");
			printf("\n"); // Output empty value for child executable
			goto cleanup_iteration;
		}

		printf("%.*s\n", (int)child_exe_path_len, child_exe_path);

cleanup_iteration:
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

