/*********************************************************************************\
 * alps_application.h - A header file for the alps_application interface.
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


#ifndef _ALPS_APPLICATION_H
#define _ALPS_APPLICATION_H

#include "alps/alps.h"
#include "alps/apInfo.h"

#include "alps_defs.h"

/* Private types */

// The following represents a datatype that is inside the appEntry_t structure
// that is managed exclusively by the transfer layer. The application layer
// will only check to see if it is null or not during cleanup and call the
// appropriate function.
typedef void * transfer_iface_obj;
// This is the cleanup function prototype to deal with the above object.
typedef void (*transfer_iface_destroy)(transfer_iface_obj);

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
	transfer_iface_obj		_transferObj;	// Managed by alps_transfer.c for this app entry
	transfer_iface_destroy	_destroyObj;	// Managed by the alps_transfer.c for this app entry
} appEntry_t;

struct appList
{
	appEntry_t *		thisEntry;
	struct appList *	nextEntry;
};
typedef struct appList appList_t;

/* Public types */

typedef struct
{
	char *	hostname;
	int		numPes;
} cti_host_t;

typedef struct
{
	int				numHosts;
	cti_host_t *	hosts;
} cti_hostsList_t;

/* function prototypes */

appEntry_t *		_cti_findApp(uint64_t);
appEntry_t *		_cti_newApp(uint64_t);
int					cti_registerApid(uint64_t);
void				cti_deregisterApid(uint64_t);
uint64_t			cti_getApid(pid_t);
char *				cti_getNodeCName(void);
int					cti_getNodeNid(void);
int					cti_getAppNid(uint64_t);
int					cti_getNumAppPEs(uint64_t);
int					cti_getNumAppNodes(uint64_t);
char **	 			cti_getAppHostsList(uint64_t);
cti_hostsList_t *	cti_getAppHostsPlacement(uint64_t);
void				cti_destroyHostsList(cti_hostsList_t *);

#endif /* _ALPS_APPLICATION_H */
