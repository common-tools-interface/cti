/*********************************************************************************\
 * alps_transfer.h - A header file for the alps_application interface.
 *
 * © 2011-2012 Cray Inc.	All Rights Reserved.
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


#ifndef _ALPS_APPLICATION_H
#define _ALPS_APPLICATION_H

#include "alps/alps.h"
#include "alps/apInfo.h"

#include "useful/stringList.h"

#define ALPS_XT_CNAME				"/proc/cray_xt/cname"
#define ALPS_XT_HOSTNAME_FMT		"nid%05d"
#define ALPS_XT_HOSTNAME_LEN		9

/* struct typedefs */
typedef struct
{
	int		nid;		// service node id
	char *	cname;		// service node hostname
} serviceNode_t;

typedef struct
{
	int				pe0Node;		// ALPS PE0 node id
	appInfo_t		appinfo;		// ALPS application information
	cmdDetail_t *	cmdDetail;		// ALPS application command information (width, depth, memory, command name)
	placeList_t *	places;	 		// ALPS application placement information (nid, processors, PE threads)
} alpsInfo_t;

typedef struct
{
	uint64_t		apid;			// ALPS application ID
	alpsInfo_t		alpsInfo;		// Information pertaining to the applications ALPS status
	stringList_t *	shipped_execs;	// list of previously exec'ed binaries
	stringList_t *	shipped_libs;	// list of previously shipped dso's
	stringList_t *	shipped_files;	// list of previously shipped regular files
} appEntry_t;

struct appList
{
	appEntry_t *		thisEntry;
	struct appList *	nextEntry;
};
typedef struct appList appList_t;

typedef struct
{
	char *	hostname;
	int		numPes;
} nodeHostPlacement_t;

typedef struct
{
	int						numHosts;
	nodeHostPlacement_t *	hosts;
} appHostPlacementList_t;

/* function prototypes */
appEntry_t *				findApp(uint64_t);
appEntry_t *				newApp(uint64_t);
int							registerApid(uint64_t);
void						deregisterApid(uint64_t);
uint64_t					getApid(pid_t);
char *						getNodeCName(void);
int							getNodeNid(void);
int							getAppNid(uint64_t);
int							getNumAppPEs(uint64_t);
int							getNumAppNodes(uint64_t);
char **	 					getAppHostsList(uint64_t);
appHostPlacementList_t *	getAppHostsPlacement(uint64_t);

#endif /* _ALPS_APPLICATION_H */
