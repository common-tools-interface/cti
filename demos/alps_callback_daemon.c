/*********************************************************************************\
 * alps_callback_daemon.c - The compute node daemon portion of the alps_callback_demo.
 *
 * © 2011 Cray Inc.  All Rights Reserved.
 *
 * Unpublished Proprietary Information.
 * This unpublished work is protected to trade secret, copyright and other laws.
 * Except as permitted by contract or express written permission of Cray Inc.,
 * no part of this work or its content may be used, reproduced or disclosed
 * in any form.
 *
 * $HeadURL$
 * $Date$
 * $Rev$
 * $Author$
 *
 *********************************************************************************/

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

#include "tool_backend.h"

#include "alps_callback_demo.h"

const struct option long_opts[] = {
                                {"hostname", required_argument, 0, 'h'},
                                {"help", no_argument, 0, 'x'},
                                {0, 0, 0, 0}
                                };

// service node information
char *  fe_hostname = (char *)NULL;
// this nodes information
char *  my_hostname = (char *)NULL;
int     firstPe = 0;
int     numPes = 0;
// pid information
nodeAppPidList_t *      appPids = (nodeAppPidList_t *)NULL;

int
callback_register()
{
        int rc;
        int svcNodefd;                    // file descriptor for the callback socket
        struct addrinfo hints;           // hints object for call to getaddrinfo
        struct addrinfo *svcNode;        // service node addrinfo object
        char msg[BUFSIZE];                     // message buffer
        
        memset(&hints, 0, sizeof(hints));       // ensure the hints struct is cleared
        hints.ai_family = AF_UNSPEC;            // use AF_INET6 to force IPv6
        hints.ai_socktype = SOCK_STREAM;
        
        // get the addrinfo of the service nodes cname
        if ((rc = getaddrinfo(fe_hostname, CALLBACK_PORT, &hints, &svcNode)) != 0)
        {
                fprintf(stderr, "Getaddrinfo failed: %s\n", gai_strerror(rc));
                return 1;
        }

        // create the socket
	if ((svcNodefd = socket(svcNode->ai_family, svcNode->ai_socktype, svcNode->ai_protocol)) < 0)
	{
	        fprintf(stderr, "Callback socket creation failed.\n");
	        return 1;
	}
	
	fprintf(stderr, "Connecting...\n");
	fprintf(stderr, "Host: %s\n", fe_hostname);
	fprintf(stderr, "Port: %d\n", atoi(CALLBACK_PORT));
	
	if (connect(svcNodefd, svcNode->ai_addr, svcNode->ai_addrlen) == -1)
	{
	        close(svcNodefd);
	        fprintf(stderr, "Callback socket connect failed.\n");
	        return 1;
	}
	
	snprintf(msg, BUFSIZE, "%d:%s:%d", firstPe, my_hostname, numPes);
	
	if (send(svcNodefd, msg, strlen(msg), 0) < 0)
	{
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
        FILE *log;
        
        while ((c = getopt_long(argc, argv, "h:", long_opts, &opt_ind)) != -1)
        {
                switch (c)
                {
                        case 'h':
                                if (optarg == (char *)NULL)
                                {
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
        if ((my_hostname = getNodeCName()) == (char *)NULL)
        {
                fprintf(stderr, "getNodeCName failed.\n");
                return 1;
        }
        fprintf(stderr, "My hostname: %s\n", my_hostname);
        
        // get the first PE that resides on this node
        if ((firstPe = getFirstPE()) == -1)
        {
                fprintf(stderr, "getFirstPE failed.\n");
                return 1;
        }
        fprintf(stderr, "My first PE: %d\n", firstPe);
        
        // get the number of PEs that reside on this node
        if ((numPes = getPesHere()) == -1)
        {
                fprintf(stderr, "getPesHere failed.\n");
                return 1;
        }
        fprintf(stderr, "PEs here: %d\n", numPes);
        
        // get the pids for the app
        if ((appPids = findAppPids()) == (nodeAppPidList_t *)NULL)
        {
                fprintf(stderr, "findAppPids failed.\n");
                return 1;
        }
        fprintf(stderr, "App pid_t's here: %d\n", appPids->numPairs);
        
        callback_register();
        
        return 0;
}
