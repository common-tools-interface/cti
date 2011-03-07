
/*
 * (c) 2009 Cray Inc.  All Rights Reserved.  Unpublished Proprietary
 * Information.  This unpublished work is protected to trade secret,
 * copyright and other laws.  Except as permitted by contract or
 * express written permission of Cray Inc., no part of this work or
 * its content may be used, reproduced or disclosed in any form.
 */

#ifndef __ALPS_H__
#define __ALPS_H__

#ident "$Id: alps.h 6052 2009-06-17 21:42:55Z kohnke $"

/*
 * Port numbers, paths and file names related to ALPS components
 * are defined here.
 */

#define ALPS_APCONFIG_PORT_ENV  "ALPS_APCONFIG_PORT"
#define ALPS_APCONFIG_PORT	608
#define ALPS_APINIT_PORT_ENV	"ALPS_APINIT_PORT"
#define ALPS_APINIT_PORT	607
#define ALPS_APSCHED_PORT_ENV   "ALPS_APSCHED_PORT"
#define ALPS_APSCHED_PORT	607
#define ALPS_APSYS_PORT_ENV	"ALPS_APSYS_PORT"
#define ALPS_APSYS_PORT		606

#define ALPS_DB_HOST_ENV  "ALPS_DB_HOST"
#define ALPS_DB_NAME_ENV  "ALPS_DB_NAME"
#define ALPS_DB_USER_ENV  "ALPS_DB_USER"
#define ALPS_DB_IDBY_ENV  "ALPS_DB_IDBY"
 
#define ALPS_LOG_PATH		"/var/log/alps"
#define ALPS_RUN_PREFIX		"/var/run"
#define APSCHED_RUN_NAME	"apsched"
#define APINIT_RUN_NAME		"apinit"
#define APSYS_RUN_NAME		"apsys"
#define APBRIDGE_RUN_NAME	"apbridge"
#define APSCHED_LOG_PREFIX	APSCHED_RUN_NAME
#define APINIT_LOG_PREFIX	APINIT_RUN_NAME
#define APSYS_LOG_PREFIX	APSYS_RUN_NAME
#define APBRIDGE_LOG_PREFIX	APBRIDGE_RUN_NAME

/*
 * Compute node temporary files belonging to each application
 * have this path and file names.
 */
#define ALPS_CNODE_PATH		"/var/spool/alps"
#define ALPS_CNODE_PATH_FMT	ALPS_CNODE_PATH "/%llu"

/*
 * Compute node cpuset directory belonging to each application
 * on XT have this path name.
 */
#define ALPS_CNODE_CPUSET_PATH	 	"/dev/cpuset"
#define ALPS_CNODE_CPUSET_PATH_FMT	ALPS_CNODE_CPUSET_PATH "/%llu"

/*
 * For the XT node IDs are stored in /proc
 */
#define ALPS_XT_NID		"/proc/cray_xt/nid"

/*
 * UDP socket information is stored in /proc
 */
#define ALPS_NET_UDP		"/proc/net/udp"

/*
 * UDP response timeout in seconds
 */
#define ALPS_UDP_TIMEOUT	10
#define ALPS_UDP_TIMEOUT_APINIT 30

#define ALPS_UDP_RETRIES	 9
#define ALPS_UDP_RETRIES_APINIT  6
/*
 * TCP connect timeout in seconds
 */
#define ALPS_TCP_TIMEOUT	6

/*
 * TCP message response timeout
 */
#define ALPS_TCP_MSG_TIMEOUT	60

/*
 * CMS timeout in seconds
 */
#define ALPS_CMS_TIMEOUT	5

/*
 * Name mapping for TCP socket connection states
 */
#ifdef TCP_STATE
const char *tcp_state[] =
{
    "",
    "ESTABLISHED",
    "SYN_SENT",
    "SYN_RECV",
    "FIN_WAIT1",
    "FIN_WAIT2",
    "TIME_WAIT",
    "CLOSE",
    "CLOSE_WAIT",
    "LAST_ACK",
    "LISTEN",
    "CLOSING"
};
#else
extern const char *tcp_state[];
#endif

/*
 * The XT application API uses pipes to communicate between
 * apinit and the application. The file descriptors for those
 * pipes are defined here. The FDs are those of the application,
 * apinit uses the corresponding FD + 1 for its end of the pipe.
 *
 * During initialization integers identifying the total number of PEs
 * in the application and the PE number of each instance of the application
 * will be written by apinit on XTAPI_FD_IDENTITY. The application must
 * return its nidpid value on XTAPI_FD_MYNIDPID. When all instances of
 * the application have reported their nidpids, apinit will send the
 * collected nidpid entries on XTAPI_FD_ALLNIDPID.
 *
 * The pipes will not be closed by apinit.
 */
#define XTAPI_FD_IDENTITY	100	/* read integer count and pe number */
#define XTAPI_FD_MYNIDPID	102	/* write nidpid to apinit */
#define XTAPI_FD_ALLNIDPID	104	/* read all nidpid entries */

/*
 *-------------------------------------------------------
 */

/*
 * Max memory request from aprun in megabytes
 */
#define ALPS_MAX_MEMSIZE 1048576	/* 0x100000 */

/*
 * Current maximum number of segments (i.e. NumaNodes) supported by ALPS
 */
#define ALPS_MAX_SEGMENTS 2

/*
 * Current maximum number of cores per socket
 */
#define ALPS_CORES_PER_HD_SOCKET 16

#if XT_GNI
/*
 * ALPS Baker GNI (Gemini and Aries) limits:
 * Note that apsched only handles one NIC for now.
 */
#define ALPS_GNI_MAX_NICS	1	/* Max # Gemini/Aries NICs/node */

#define ALPS_GNI_NTT_SIZE	8192	/* # Entries in Gemini NTT */

#define ALPS_GNI_NTT_GRAN_MIN	1	/* Min NTT granularity */
#define ALPS_GNI_NTT_LOG2GRAN_MIN 0	/* log2(Min NTT granularity) */
#define ALPS_GNI_NTT_GRAN_MAX	32	/* Max NTT granularity */
#define ALPS_GNI_NTT_LOG2GRAN_MAX 5	/* log2(Max NTT granularity) */

#define ALPS_GNI_PTAG_MAX	255	/* Ptags are 8 bits in Gemini */
#define ALPS_GNI_PTAG_GLOBALS	10	/* default # of system-unique PTags */
#define ALPS_GNI_PTAG_GLOBAL_NODES 5000	/* if app is bigger than 5000 nodes, */
                                        /* use global PTag instead of NTT */
#endif

/*
 * Handy macros: STR(X) will string-ize its argument,
 * CVT(X) will string-ize the value of its argument; e.g.:
 * #define TWO 2
 * STR(TWO) -> "TWO"
 * CVT(TWO) -> "2"
 */
#define STR(X)	#X
#define CVT(X)	STR(X)

/*
 * Handy macro: creates a bitmask of NUMBITS lowest-order bits
 */
#define BITMASK(NUMBITS) ((1LL << (NUMBITS)) - 1)

/*
 * Size of 'type' in bits
 */
#define BITSIZE(TYPE)	(sizeof(TYPE) * 8)

/*
 * Standard max/min macros: prepend with ALPS to avoid re-definition
 */
#define ALPSMAX(a, b)  ({ typeof(a) _a = (a); typeof(b) _b = (b); \
                           _a > _b ? _a : _b; })
#define ALPSMIN(a, b)  ({ typeof(a) _a = (a); typeof(b) _b = (b); \
                           _a < _b ? _a : _b; })

/*
 * Handy macro: count the set bits in the 32-bit object 'map'
 * The nice non-BW algorithm is from Brian Kernighan (found at
 * http://graphics.stanford.edu/~seander/bithacks.html#CountBitsSetKernighan);
 * it only does as many iterations as there are set bits.
 */
#if __craybw
#include <asm/intrinsics.h>	/* needed for _pop32() */
#define numBits(map) ({					\
    int __ret; unsigned __m = (map);			\
    __ret = _pop32(__m);				\
    __ret;						\
})
#else
#define numBits(map) ({					\
    int __ret; unsigned __m = (map);			\
    for (__ret = 0; __m; __ret++) { __m &= __m - 1; }	\
    __ret;						\
})
#endif

/* Node states */
typedef enum { state_unknown=0, state_down, state_avail } alps_nodeState_t;

#endif /* __ALPS_H__ */
