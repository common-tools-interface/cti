/******************************************************************************\
 *
 * Copyright 2019 Cray Inc.    All Rights Reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * BSD license below:
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

#include <unistd.h>

#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>

#include "message_two/message.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char* argv[]) {

    if (argc != 3) {
        fprintf(stderr, "Invalid parameters\nExpected: SocketIP, SocketPort\n");
        return 1;
    }
    //avoid race conditions in the least elegant way...
    sleep(1);

    //Declare variables to store socket IP and port
    char* ip;
    int port;

    //Set variables to their respective values
    ip = argv[1];
    sscanf(argv[2], "%d", &port);

    //Create socket
    struct sockaddr_in s_address;
    int c_socket;
    int rc;
    struct addrinfo *node;
    struct addrinfo hints;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_NUMERICSERV;

    // get the addrinfo
    if ((rc = getaddrinfo(argv[1], argv[2], &hints, &node)) != 0) {
        fprintf(stderr, "Getaddrinfo failed: %s\n", gai_strerror(rc));
        return 1;
    }

    if ((c_socket = socket(node->ai_family, node->ai_socktype, node->ai_protocol)) < 0) {
        fprintf(stderr, "Failed to create local socket\n");
        return 1;
    }

    fprintf(stderr, "Connecting...\n");
    fprintf(stderr, "Host: %s\n", argv[1]);
    fprintf(stderr, "Port: %d\n", atoi(argv[2]));

    if (connect(c_socket, node->ai_addr, node->ai_addrlen) < 0) {
        fprintf(stderr, "Failed to connect\n");
        perror("ERROR:");
        return 1;
    }
    fprintf(stderr, "CONNECTED\n");
    //Send predictable data over socket
    send(c_socket, get_message(), 1, 0);
    close(c_socket);
    return 0;
}
