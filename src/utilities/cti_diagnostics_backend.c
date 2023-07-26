/******************************************************************************\
 *
 * Copyright 2021 Hewlett Packard Enterprise Development LP.
 * SPDX-License-Identifier: Linux-OpenIB
 ******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <sys/stat.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>

#include "common_tools_be.h"

static int connect_address(char const* address, char const* port)
{
	int fd = -1;

	// Initialize socket connection hints
	struct addrinfo hints;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_NUMERICSERV;

	// Get address information
	struct addrinfo *node = NULL;
	int const getaddrinfo_rc = getaddrinfo(address, port, &hints, &node);
	if (getaddrinfo_rc) {
		fprintf(stderr, "getaddrinfo failed: %s\n", gai_strerror(getaddrinfo_rc));
		goto cleanup_connect_address;
	} else if (node == NULL) {
		fprintf(stderr, "getaddrinfo %s:%s returned no results\n", address, port);
		goto cleanup_connect_address;
	}

	// Create socket
	fd = socket(node->ai_family, node->ai_socktype, node->ai_protocol);
	if (fd < 0) {
		fprintf(stderr, "socket call failed for %s:%s\n", address, port);
		goto cleanup_connect_address;
	}

	// Connect to socket
	if (connect(fd, node->ai_addr, node->ai_addrlen)) {
		fprintf(stderr, "connect call failed for %s:%s\n", address, port);
		close(fd);
		fd = -1;
		goto cleanup_connect_address;
	}

cleanup_connect_address:
	if (node) {
		freeaddrinfo(node);
		node = NULL;
	}

	return fd;
}

int main(int argc, char** argv)
{
	int rc = -1;
	char const *result_message = "All backend tests passed";

	int result_socket = -1;
	char *app_id = NULL;
	cti_pidList_t *pid_list = NULL;
	char *root_dir = NULL;

	// Get frontend address and port
	if (argc != 3) {
		fprintf(stderr, "usage: %s address port\n", argv[0]);
		goto cleanup;
	}
	char const* address = argv[1];
	char const* port = argv[2];

	// Connect to frontend result socket
	result_socket = connect_address(address, port);
	if (result_socket < 0) {
		fprintf(stderr, "connect to %s:%d failed\n", address, port);
		goto cleanup;
	}

	// Check valid application ID
	app_id = cti_be_getAppId();
	if (app_id == NULL) {
		result_message = "cti_be_getAppId failed";
		goto send_result;
	}

	// Get PID list
	pid_list = cti_be_findAppPids();
	if (pid_list == NULL) {
		result_message = "cti_be_findAppPids failed";
		goto send_result;
	}

	// Get backend file directory
	root_dir = cti_be_getRootDir();
	if (root_dir == NULL) {
		result_message = "cti_be_getRootDir failed";
		goto send_result;
	}

	// Ensure backend file directory is accessible
	{ struct stat st;
		if (stat(root_dir, &st)) {
			result_message = "Backend root directory inaccessible";
			goto send_result;
		}
		if (!S_ISDIR(st.st_mode)) {
			result_message = "Backend root path is not a directory";
			goto send_result;
		}
		if (access(root_dir, R_OK | W_OK | X_OK)) {
			result_message = "Backend root directory is not readable / writable / executable";
			goto send_result;
		}
	}

send_result:

	// Send success or error message to frontend via socket
	if (send(result_socket, result_message, strlen(result_message) + 1, 0) < 0) {
		perror("send");
		goto cleanup;
	}

	rc = 0;

cleanup:
	if (root_dir) {
		free(root_dir);
		root_dir = NULL;
	}
	if (pid_list) {
		cti_be_destroyPidList(pid_list);
		pid_list = NULL;
	}
	if (app_id) {
		free(app_id);
		app_id = NULL;
	}
	if (result_socket >= 0) {
		close(result_socket);
		result_socket = -1;
	}

	return rc;
}

