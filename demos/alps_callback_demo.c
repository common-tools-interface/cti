/*********************************************************************************\
 * alps_callback_demo.c - An example program which takes advantage of the CrayTool
 *			Interface which will launch an aprun session from the given
 *			argv, transfer and launch a simple tool daemon that will
 *			communicate with the frontend over a simple socket connection.
 *
 * © 2011-2014 Cray Inc.	All Rights Reserved.
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

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <unistd.h>
#include <netdb.h>
#include <pthread.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include "cray_tools_fe.h"

#include "alps_callback_demo.h"

/* global variables */
int					registered_nodes = 0;
int					app_nodes = 0;
int					num_pes = 0;
FrontEndNode_t		my_node;
BackEndNode_t **	peNodes;

void *
callback_handler(void *thread_arg)
{
	handlerThreadArgs_t *	args = thread_arg;
	char *					addr = inet_ntoa(args->cnode.sin_addr);
	int 					port = ntohs(args->cnode.sin_port);
	char 					recv_get[BUFSIZE];
	int 					start_pe, local_pes, node;
	char 					*cname, *lasts, *tok;

	pthread_cleanup_push(handler_destroy, thread_arg);	// setup thread cleanup function
	
	// grab the mutex lock
	pthread_mutex_lock(&my_node.lock);
	printf("Compute node connected.\n");
	printf("CNode_addr: %s\n", addr);
	printf("CNode_port: %d\n\n", port);
	// unlock the mutex
	pthread_mutex_unlock(&my_node.lock);
	
	// clear the receive buffer
	memset((void *)recv_get, 0, BUFSIZE);
	
	// recieve from the client
	if (recv(args->cnodefd, recv_get, sizeof(recv_get), 0) < 0)
	{
		fprintf(stderr, "Failed to recieve.\n");
		return NULL;
	}
	
	// Parse the response and insert into the global table
	if ((tok = strtok_r(recv_get, ":", &lasts)) == NULL)
	{
		fprintf(stderr, "Failed to parse recv buffer.\n");
		return NULL;
	}
	
	// first is the starting pe for that node
	// 
	start_pe = atoi(tok);
	
	// our concept of node number is based on the starting pe on the node,
	// divided by the total number of pes divided by the number of nodes.
	node = start_pe / (num_pes/app_nodes);
	
	if ((tok = strtok_r(NULL, ":", &lasts)) == NULL)
	{
		fprintf(stderr, "Failed to parse recv buffer.\n");
		return NULL;
	}
	cname = strdup(tok);	// second is the compute nodes cname
	
	if ((tok = strtok_r(NULL, ":", &lasts)) == NULL)
	{
		fprintf(stderr, "Failed to parse recv buffer.\n");
		return NULL;
	}
	local_pes = atoi(tok);	// last is the number of local pes on the node
	
	// grab the mutex lock
	pthread_mutex_lock(&my_node.lock);
	
	printf("Starting PE on node: %d\n", start_pe);
	printf("cnode hostname: %s\n", cname);
	printf("Local PEs on the node: %d\n\n", local_pes);
		
	++registered_nodes;
	peNodes[node]->node_cname = cname;
	
	// check if we are done
	pthread_cond_signal(&my_node.cond);
	// unlock the mutex
	pthread_mutex_unlock(&my_node.lock);
	
	// done
	pthread_cleanup_pop(1);
	pthread_exit(0);
}

void
handler_destroy(void *thread_arg)
{
	handlerThreadArgs_t *args = thread_arg;
	
	if (args->cnodefd)
		close(args->cnodefd);
		
	if (args != NULL)
		free(args);
}

void *
callback_listener(void *thread_arg)
{
	int 					rc;
	const int 				set = 1;
	handlerThreadArgs_t *	hargs;				// handler thread args
	listenThreadArgs_t *	args = thread_arg;	// cast the void ptr into a listenThreadArgs_t ptr
	
	pthread_cleanup_push(callback_destroy, thread_arg);	// setup thread cleanup function
	
	memset(&args->hints, 0, sizeof(args->hints));	// ensure the hints struct is cleared
	args->hints.ai_family = AF_UNSPEC;		// use AF_INET6 to force IPv6
	args->hints.ai_socktype = SOCK_STREAM;
	args->hints.ai_flags = AI_PASSIVE;
	
	// get the addrinfo on my cname device
	if ((rc = getaddrinfo(NULL, CALLBACK_PORT, &args->hints, &args->listener)) != 0)
	{
		fprintf(stderr, "Getaddrinfo failed: %s\n", gai_strerror(rc));
		return NULL;
	}
	
	// create the socket
	if ((args->listenfd = socket(args->listener->ai_family, args->listener->ai_socktype, args->listener->ai_protocol)) < 0)
	{
		fprintf(stderr, "Listener socket creation failed.\n");
		return NULL;
	}
	
	if (setsockopt(args->listenfd, SOL_SOCKET, SO_REUSEADDR, &set, sizeof(set))	!= 0)
	{
		fprintf(stderr, "Listener socket setsockopt failed.\n");
		return NULL;
	}
	
	// bind to the socket
	if (bind(args->listenfd, args->listener->ai_addr, args->listener->ai_addrlen) == -1)
	{
		fprintf(stderr, "Listener bind on socket failed.\n");
		return NULL;
	}
	
	// listen on the socket
	if (listen(args->listenfd, BACKLOG) < 0)
	{
		fprintf(stderr, "Listen failed.\n");
		return NULL;
	}
	
	// setup the handler thread attributes having a detached state
	pthread_attr_init(&args->attr);
	pthread_attr_setdetachstate(&args->attr, PTHREAD_CREATE_DETACHED);
	
	// accept until the main thread sends us a cancel
	while(1)
	{
		// I don't think accept has a thread cancelation point
		pthread_testcancel();
		
		// create a arg object for the new thread
		if ((hargs = malloc(sizeof(handlerThreadArgs_t))) == (void *)0)
		{
			fprintf(stderr, "Unable to malloc thread arguments.\n");
			return NULL;
		}
		
		hargs->len = sizeof(hargs->cnode);
		
		if ((hargs->cnodefd = accept(args->listenfd, (struct sockaddr *) &hargs->cnode, &hargs->len)) == -1)
		{
			fprintf(stderr, "Unable to accept incoming connection.\n");
			free(hargs);
			continue;
		}
		
		// setup the thread
		pthread_create(&hargs->handlerTid, &args->attr, callback_handler, (void *)hargs);
	}
	
	// never get here
	pthread_cleanup_pop(1);
	pthread_exit(0);
}

int
callback_create()
{
	listenThreadArgs_t *thread_arg;

	// setup the mutex lock
	pthread_mutexattr_init(&my_node.lock_attr);
	pthread_mutex_init(&my_node.lock, &my_node.lock_attr);
	
	// setup the condition variable
	pthread_condattr_init(&my_node.cond_attr);
	pthread_cond_init(&my_node.cond, &my_node.cond_attr);
	
	// have threads detach by default
	pthread_attr_init(&my_node.attr);
	pthread_attr_setdetachstate(&my_node.attr, PTHREAD_CREATE_DETACHED);
	
	// create the thread argument
	if ((thread_arg = malloc(sizeof(listenThreadArgs_t))) == (void *)0)
	{
		fprintf(stderr, "Unable to malloc thread arguments.\n");
		return 1;
	}
	
	// create the listener thread
	pthread_create(&my_node.listener, &my_node.attr, callback_listener, (void *)thread_arg);
	
	if(!my_node.listener)
	{
		// error
		return 1;
	}
	
	return 0;
}

void
callback_destroy(void *thread_arg)
{
	listenThreadArgs_t *args = thread_arg;

	// destroy the global mutex lock
	pthread_mutexattr_destroy(&my_node.lock_attr);
	pthread_mutex_destroy(&my_node.lock);
	
	// destroy the global condition variable
	pthread_condattr_destroy(&my_node.cond_attr);
	pthread_cond_destroy(&my_node.cond);
	
	// destroy the listener thread attr
	pthread_attr_destroy(&my_node.attr);
	
	// cleanup the thread argument object
	// free the addrinfo object
	if (args->listener != NULL)
		freeaddrinfo(args->listener);
	
	// close the socket
	if (args->listenfd)
		close(args->listenfd);
	
	// destroy the thread attributes
	pthread_attr_destroy(&args->attr);
	
	// free the thread argument
	if (args != NULL)
		free(args);
}

int
main(int argc, char **argv)
{
	cti_aprunProc_t *	myapp;
	char **				tool_argv;
	int					i;
	cti_session_id_t	mysid;
	
	// ensure the user called me correctly
	if (argc < 2)
	{
		//usage();
		return 1;
	}
	
	printf("Setting up callback handler and launching aprun...\n");
	
	// setup the callback handler
	if (callback_create())
	{
		fprintf(stderr, "Callback thread creation failed.\n");
		return 1;
	}
	
	// call aprun
	if ((myapp = cti_launchAprunBarrier(&argv[1],0,0,0,0,NULL,NULL,NULL)) <= 0)
	{
		fprintf(stderr, "Aprun failed.\n");
		return 1;
	}
	
	// get number of allocated nodes in app
	if ((app_nodes = cti_getNumAppNodes(myapp->apid)) == 0)
	{
		fprintf(stderr, "cti_getNumAppNodes failed.\n");
		cti_killAprun(myapp->apid, 9);
		return 1;
	}
	// get number of PEs in app
	if ((num_pes = cti_getNumAppPEs(myapp->apid)) == 0)
	{
		fprintf(stderr, "cti_getNumAppPEs failed.\n");
		cti_killAprun(myapp->apid, 9);
		return 1;
	}
	
	// create the global peNodes array
	if ((peNodes = calloc(app_nodes, sizeof(BackEndNode_t *))) == (void *)0)
	{
		fprintf(stderr, "could not calloc memory for the peNodes array.\n");
		cti_killAprun(myapp->apid, 9);
		return 1;
	}
	for (i=0; i<app_nodes; i++)
	{
		if ((peNodes[i] = malloc(sizeof(BackEndNode_t))) == (void *)0)
		{
			fprintf(stderr, "Could not malloc memory for peNodes entry.\n");
			cti_killAprun(myapp->apid, 9);
			return 1;
		}
	}
	
	// get the cname of this service node
	my_node.cname = cti_getNodeCName();
	
	// alloc memory for the argv array
	if ((tool_argv = calloc(3, sizeof(char *))) == (void *)0)
	{
		fprintf(stderr, "Could not calloc memory for the tool argv string.\n");
		cti_killAprun(myapp->apid, 9);
		return 1;
	}
	tool_argv[0] = "-h";
	tool_argv[1] = my_node.cname;
	tool_argv[2] = NULL;
	
	// Transfer and exec the callback_daemon application
	if ((mysid = cti_execToolDaemon(myapp->apid, 0, 0, LAUNCHER, tool_argv, NULL, 0)) == 0)
	{
		fprintf(stderr, "Could not launch callback daemon on compute nodes!\n");
		cti_killAprun(myapp->apid, 9);
		return 1;
	}
	
	// Wait for the callbacks to complete.
	pthread_mutex_lock(&my_node.lock);
	
	fprintf(stdout, "Waiting for callbacks...\n\n");
	
	while (registered_nodes < app_nodes)
	{
		pthread_cond_wait(&my_node.cond, &my_node.lock);
		fprintf(stdout, "Total registered callbacks: %d\n\n", registered_nodes);
	}
	
	pthread_mutex_unlock(&my_node.lock);
	
	// callbacks complete, kill the listener thread
	pthread_cancel(my_node.listener);
	
	printf("Hit return to release the application from the startup barrier...");
	
	// just read a single character from stdin then release the app/exit
	(void)getchar();
	
	// release barrier
	if (cti_releaseAprunBarrier(myapp->apid))
	{
		fprintf(stderr, "Could not release app from barrier.\n");
		cti_killAprun(myapp->apid, 9);
		return 1;
	}
	
	return 0;
}
