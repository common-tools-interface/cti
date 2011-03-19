/*********************************************************************************\
 * alps_backend.h - A header file for the alps_backend interface.
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
 
#ifndef _ALPS_BACKEND_H
#define _ALPS_BACKEND_H

#include <stdint.h>
#include "alps/alps.h"
#include "alps/alps_toolAssist.h"

#define ALPS_XT_CNAME   "/proc/cray_xt/cname"
#define ALPS_XT_HOSTNAME_FMT    "nid%05d"
#define ALPS_XT_HOSTNAME_LEN    9

/* struct typedefs */
typedef struct
{
        int             numPids;
        pid_t *         peAppPids;
} nodeAppPidList_t;

typedef struct
{
        int     nid;            // compute node id
        char *  cname;          // compute node hostname
} computeNode_t;

/* function prototypes */
nodeAppPidList_t *      findAppPids(uint64_t);
void                    destroy_nodeAppPidList(nodeAppPidList_t *);
char *                  getNodeCName(void);
char *                  getNodeNidName(void);
int                     getNodeNid(void);
int                     getFirstPE(uint64_t);
int                     getPesHere(uint64_t);

#endif /* _ALPS_BACKEND_H */

