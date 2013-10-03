/*********************************************************************************\
 * alps_transfer.h - A header file for the alps_application interface.
 *
 * © 2011-2013 Cray Inc.	All Rights Reserved.
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

#include "alps_defs.h"

// The following represents a datatype that is inside the appEntry_t structure
// that is managed exclusively by the alps_transfer.c layer. The alps_application.c
// layer will only check to see if it is null or not during cleanup and call the
// appropriate function.
typedef void * TRANSFER_IFACE_OBJ;
// This is the cleanup function prototype to deal with the above object.
typedef void (*TRANSFER_IFACE_DESTROY)(TRANSFER_IFACE_OBJ);

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
	uint64_t				apid;			// ALPS application ID
	alpsInfo_t				alpsInfo;		// Information pertaining to the applications ALPS status
	char *					toolPath;		// backend toolhelper path for temporary storage
	int						transfer_init;	// Transfer interface initialized?
	TRANSFER_IFACE_OBJ		_transferObj;	// Managed by alps_transfer.c for this app entry
	TRANSFER_IFACE_DESTROY	_destroyObj;	// Managed by the alps_transfer.c for this app entry
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
