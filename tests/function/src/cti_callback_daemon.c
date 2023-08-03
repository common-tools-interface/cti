/*********************************************************************************\
 * cti_callback_daemon.c - The compute node daemon portion of the
 *                         cti_callback_test.
 *
 * Copyright 2011-2020 Hewlett Packard Enterprise Development LP.
 * SPDX-License-Identifier: Linux-OpenIB
 ******************************************************************************/

#include <fcntl.h>
#include <getopt.h>
#include <inttypes.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include "common_tools_be.h"

#include "cti_callback_test.h"

const struct option long_opts[] = {
            {"hostname", required_argument, 0, 'h'},
            {"help", no_argument, 0, 'x'},
            {0, 0, 0, 0}
            };

// service node information
char *  fe_hostname = NULL;
// this nodes information
char *  my_hostname = NULL;
int     firstPe = 0;
int     numPes = 0;
// pid information
cti_pidList_t * appPids = NULL;

int
callback_register()
{
    int     rc;
    int     svcNodefd;          // file descriptor for the callback socket
    struct addrinfo hints;      // hints object for call to getaddrinfo
    struct addrinfo *svcNode;   // service node addrinfo object
    char    msg[BUFSIZE];       // message buffer

    memset(&hints, 0, sizeof(hints));   // ensure the hints struct is cleared
    hints.ai_family = AF_UNSPEC;        // use AF_INET6 to force IPv6
    hints.ai_socktype = SOCK_STREAM;

    // get the addrinfo of the service nodes cname
    if ((rc = getaddrinfo(fe_hostname, CALLBACK_PORT, &hints, &svcNode)) != 0) {
        fprintf(stderr, "Getaddrinfo failed: %s\n", gai_strerror(rc));
        return 1;
    }

    // create the socket
    if ((svcNodefd = socket(svcNode->ai_family, svcNode->ai_socktype, svcNode->ai_protocol)) < 0) {
        fprintf(stderr, "Callback socket creation failed.\n");
        return 1;
    }

    fprintf(stderr, "Connecting...\n");
    fprintf(stderr, "Host: %s\n", fe_hostname);
    fprintf(stderr, "Port: %d\n", atoi(CALLBACK_PORT));

    if (connect(svcNodefd, svcNode->ai_addr, svcNode->ai_addrlen) == -1) {
        close(svcNodefd);
        fprintf(stderr, "Callback socket connect failed.\n");
        return 1;
    }

    snprintf(msg, BUFSIZE, "%d:%s:%d", firstPe, my_hostname, numPes);

    if (send(svcNodefd, msg, strlen(msg), 0) < 0) {
        close(svcNodefd);
        fprintf(stderr, "Callback socket send failed.\n");
        return 1;
    }

    close(svcNodefd);
    return 0;
}

int
main(int argc, char **argv)
{
    int opt_ind = 0;
    int c;

    while ((c = getopt_long(argc, argv, "h:", long_opts, &opt_ind)) != -1) {
        switch (c) {
            case 'h':
                if (optarg == NULL) {
                    //usage();
                    return 1;
                }

                fe_hostname = strdup(optarg);
                break;
            case 'x':
                //usage();
                return 1;
            default:
                //usage();
                return 1;
        }
    }

    // get my nodes cname
    if ((my_hostname = cti_be_getNodeHostname()) == NULL) {
        fprintf(stderr, "cti_be_getNodeCName failed.\n");
        my_hostname = "ERROR";
    }
    fprintf(stderr, "My hostname: %s\n", my_hostname);

    // get the first PE that resides on this node
    if ((firstPe = cti_be_getNodeFirstPE()) == -1) {
        fprintf(stderr, "cti_be_getNodeFirstPE failed.\n");
    }
    fprintf(stderr, "My first PE: %d\n", firstPe);

    // get the number of PEs that reside on this node
    if ((numPes = cti_be_getNodePEs()) == -1) {
        fprintf(stderr, "cti_be_getNodePEs failed.\n");
    }
    fprintf(stderr, "PEs here: %d\n", numPes);

    // get the pids for the app
    if ((appPids = cti_be_findAppPids()) == NULL) {
        fprintf(stderr, "findAppPids failed.\n");
    }
    else {
        fprintf(stderr, "App pid_t's here: %d\n", appPids->numPids);
    }

    callback_register();

    // sleep for either 1000 seconds or until the wlm kills us off.
    sleep(1000);

    return 0;
}
