/******************************************************************************\
 * alps_backend.h - A header file for the alps_backend interface.
 *
 * © 2011-2012 Cray Inc.  All Rights Reserved.
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
 ******************************************************************************/
 
#ifndef _ALPS_BACKEND_H
#define _ALPS_BACKEND_H

#include <stdint.h>
#include "alps/alps.h"
#include "alps/alps_toolAssist.h"

#include "alps_defs.h"
#include "pmi_attribs_parser.h"

typedef struct
{
        int                     numPairs;
        nodeRankPidPair_t *     rankPidPairs;
} nodeAppPidList_t;

typedef struct
{
        int             nid;    // compute node id
        char *          cname;  // compute node hostname
} computeNode_t;

/* function prototypes */
nodeAppPidList_t *      findAppPids(void);
void                    destroy_nodeAppPidList(nodeAppPidList_t *);
char *                  getNodeCName(void);
char *                  getNodeNidName(void);
int                     getNodeNid(void);
int                     getFirstPE(void);
int                     getPesHere(void);

#endif /* _ALPS_BACKEND_H */

