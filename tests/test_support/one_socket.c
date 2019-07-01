#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char* argv[]) {

    if (argc != 3) {
        fprintf(stderr, "Invalid parameters\nExpected: SocketIP, SocketPort\n");
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
    if ((c_socket = socket(AF_INET, SOCK_STREAM, 0)) < 0) { //TODO: Don't assume socket parameters
        fprintf(stderr, "Failed to create local socket\n");
        return 1;
    }

    s_address.sin_family = AF_INET;
    s_address.sin_port = htons(port);

    if (inet_pton(AF_INET, ip, &s_address.sin_addr)<=0) {
        fprintf(stderr, "Failed to convert IP address %s\n", ip);
        return 1;
    }

    if (connect(c_socket, (struct sockaddr*) &s_address, sizeof(s_address)) < 0) {
        fprintf(stderr, "Failed to connect\n");
    }

    //Send predictable data over socket
    send(c_socket, "1", 1, 0);

    //The 'server' will destroy the socket once its done
    return 0;       
}
