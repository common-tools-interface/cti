/******************************************************************************\
 *
 * Copyright 2019-2020 Hewlett Packard Enterprise Development LP.
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
#include <sys/stat.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char* argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Invalid parameters\nExpected: SocketIP, SocketPort, Filepath\n");
        return 1;
    }

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
    memset(&hints, 0, sizeof(struct addrinfo));
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
    fprintf(stderr, "Checking for %s...\n", argv[3]);

    struct stat buf;
    int r = stat(argv[3], &buf);

    if (r == 0) {
        // file found
        send(c_socket, "1", 1, 0);
    } else {
        // file not found
        send(c_socket, "0", 1, 0);
    }

    close(c_socket);

    return 0;
}
