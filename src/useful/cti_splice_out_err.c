/******************************************************************************\
 * splice_out_err.c - output one file to stdout and the other to stderr
 *
 * Copyright 2019 Cray Inc.  All Rights Reserved.
 *
 * Unpublished Proprietary Information.
 * This unpublished work is protected to trade secret, copyright and other laws.
 * Except as permitted by contract or express written permission of Cray Inc.,
 * no part of this work or its content may be used, reproduced or disclosed
 * in any form.
 *
 ******************************************************************************/

// This pulls in config.h
#include "cti_defs.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/ioctl.h>

#define BUF_SIZE 4096

typedef struct
{
	char const* path;
	int fd;
} path_fd_pair_t;

void*
read_write_fd(path_fd_pair_t const* in_path_out_fd)
{
	int64_t retval = 0;

	// opening a fifo will block until input is available, so it shouldn't be done in the main thread
	int const in_fd  = open(in_path_out_fd->path, O_RDONLY | O_RSYNC);
	if (in_fd < 0) {
		fprintf(stderr, "failed to open %s: %s\n", in_path_out_fd->path, strerror(errno));
		exit(1);
	}
	int const out_fd = in_path_out_fd->fd;

	// create splice pipes
	int splice_pipe[2];
	if (pipe(splice_pipe) < 0) {
		perror("pipe");
		retval = 1;
		goto cleanup;
	}

	// set up select call
	fd_set read_fds, err_fds;
	FD_ZERO(&read_fds);
	FD_ZERO(&err_fds);

	// continuously splice input to output
	while (1) {

		// add FD to watch / error set
		FD_SET(in_fd, &read_fds);
		FD_SET(in_fd, &err_fds);

		// wait in select
		if (select(in_fd + 1, &read_fds, NULL, &err_fds, NULL) < 0) {
			perror("select");
			retval = 1;
			goto cleanup;
		}

		// check for errors
		if (FD_ISSET(in_fd, &err_fds)) {
			fprintf(stderr, "select error on %d\n", in_fd);
			retval = 1;
			goto cleanup;
		}

		// check to see if input pipe has data
		if (FD_ISSET(in_fd, &read_fds)) {

			// splice all available bytes from input to output
			for (ssize_t bytes_read = -1; bytes_read != 0;) {

				// get number of bytes available
				int read_size = 0;
				if (ioctl(in_fd, FIONREAD, &read_size) < 0) {
					perror ("ioctl");
					retval = 1;
					goto cleanup;
				}

				// if zero, exit splice loop and wait for more data
				if (read_size == 0) {
					break;
				}

				// resize if larger than desired buffer size
				if (read_size > BUF_SIZE) {
					read_size = BUF_SIZE;
				}

				// read from input fd
				bytes_read = splice(in_fd, NULL, splice_pipe[1], NULL, read_size, SPLICE_F_MORE | SPLICE_F_MOVE);
				if (bytes_read < 0) {
					perror("splice");
					retval = 1;
					goto cleanup;
				}

				// write to output fd
				if (splice(splice_pipe[0], NULL, out_fd, NULL, read_size, SPLICE_F_MORE | SPLICE_F_MOVE) < 0) {
					perror("splice");
					retval = 1;
					goto cleanup;
				}
			}
		}
	}

cleanup:
	close(in_fd);
	close(out_fd);
	close(splice_pipe[0]);
	close(splice_pipe[1]);
	pthread_exit((void*)retval);
}

int
main(int const argc, char const* const argv[])
{
	// parse arguments
	if (argc != 3) {
		fprintf(stderr, "usage: %s <stdout file> <stderr file>\n", argv[0]);
		exit(1);
	}
	char const* stdout_file = argv[1];
	char const* stderr_file = argv[2];

	// set up thread arguments
	path_fd_pair_t const stdout_fd_pair = {
		.path  = stdout_file,
		.fd    = open("/dev/stdout", O_WRONLY)
	};
	path_fd_pair_t const stderr_fd_pair = {
		.path  = stderr_file,
		.fd    = open("/dev/stderr", O_WRONLY)
	};
	if ((stdout_fd_pair.fd < 0) || (stderr_fd_pair.fd < 0)) {
		perror("open");
		exit(1);
	}

	// start threads
	pthread_t stdout_thread, stderr_thread;
	pthread_create(&stdout_thread, NULL, (void * (*)(void *))read_write_fd, (void*)&stdout_fd_pair);
	pthread_create(&stderr_thread, NULL, (void * (*)(void *))read_write_fd, (void*)&stderr_fd_pair);

	// wait for threads
	int stdout_status, stderr_status;
	pthread_join(stdout_thread, (void**)&stdout_status);
	pthread_join(stderr_thread, (void**)&stderr_status);

	return (stdout_status || stderr_status);
}
