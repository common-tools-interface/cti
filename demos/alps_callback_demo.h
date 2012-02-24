/*********************************************************************************\
 * alps_callback_demo.h - Header file for the alps_callback_demo.
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

#ifndef _ALPS_CALLBACK_DEMO_H
#define _ALPS_CALLBACK_DEMO_H

/* internal defines */
// Port declarations are arbitrary for now. This might cause problems
// in the future.
#define CALLBACK_PORT "13337"
#define BACKLOG 8192
#define BUFSIZE 32768

#define LAUNCHER  "callback_daemon"

/* struct typedefs */
typedef struct
{
        char *  cname;          // service node hostname
        
        pthread_t               listener;       // listener thread
        pthread_attr_t          attr;           // thread attributes
        pthread_mutexattr_t     lock_attr;      // mutex lock attr
        pthread_mutex_t         lock;           // mutex lock for threads
        pthread_condattr_t      cond_attr;      // condition variable attr
        pthread_cond_t          cond;           // condition variable
} FrontEndNode_t;

typedef struct
{
        char *  node_cname;          // compute node hostname
} BackEndNode_t;

typedef struct
{
        int     listenfd;       // fd for listener socket
        
        struct addrinfo        hints;           // hints object for call to getaddrinfo
        struct addrinfo        *listener;       // listener addrinfo object
        pthread_attr_t          attr;            // handler thread attributes
} listenThreadArgs_t;

typedef struct
{
        pthread_t               handlerTid;     // tid for the thread
        int                     cnodefd;        // fd for connected compute node
        struct sockaddr_in     cnode;          // sockaddr_in for the cnode socket
        socklen_t               len;            // length of the sockaddr_in object
} handlerThreadArgs_t;

/* function prototypes */
void *  callback_handler(void *);
void    handler_destroy(void *);
void *  callback_listener(void *);
int     callback_create(void);
void    callback_destroy(void *);

#endif /* _ALPS_CALLBACK_DEMO_H */
