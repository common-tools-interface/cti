#include <unistd.h>

#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char* argv[]) {

    if (argc != 3) {
        fprintf(stderr, "Invalid parameters\nExpected: SocketIP, SocketPort\n");
        return 1;
    }
    sleep(1); //avoid race conditions in the least elegant way...
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
    
    //s_address.sin_family = AF_INET;
    //s_address.sin_port = htons(port);
    //sscanf(argv[1], "%d", &s_address.sin_addr.s_addr);
    //s_address.sin_addr = stoi(argv[1]);
    //if (inet_pton(AF_INET, ip, &s_address.sin_addr)<=0) {
    //    fprintf(stderr, "Failed to convert IP address %s\n", ip);
    //    return 1;
    //}
    
    fprintf(stderr, "Connecting...\n");
    fprintf(stderr, "Host: %s\n", argv[1]);
    fprintf(stderr, "Port: %d\n", atoi(argv[2]));
    
    //struct addrinfo *p = node;
    //char host[256];
    //for(p = node; p != NULL; p = p -> ai_next) {
    //    getnameinfo(p->ai_addr, p->ai_addrlen, host, sizeof(host), NULL, 0, NI_NUMERICHOST);
    //    fprintf(stderr, "%s\n", host);
    //}

    if (connect(c_socket, node->ai_addr, node->ai_addrlen) < 0) {
    //if (connect(c_socket, (struct sockaddr*) &s_address, sizeof(s_address)) < 0) {
        fprintf(stderr, "Failed to connect\n");
        perror("ERROR:");
        return 1;
    }
    fprintf(stderr, "CONNECTED\n");
    //Send predictable data over socket
    send(c_socket, "1", 1, 0);

    //The 'server' will destroy the socket once its done
    close(c_socket);
    return 0;       
}
