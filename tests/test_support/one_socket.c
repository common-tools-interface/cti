#include <unistd.h>

#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "message_one/message.h"

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
