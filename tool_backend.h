/******************************************************************************\
 * tool_backend.h - The public API definitions for the backend portion of the
 *                  tool_interface.
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
 ******************************************************************************/

#ifndef _TOOL_BACKEND_H
#define _TOOL_BACKEND_H

#include <stdint.h>
#include <sys/types.h>

/* struct typedefs */
typedef struct
{
        int             numPids;
        pid_t *         peAppPids;
} nodeAppPidList_t;

/*
 * alps_backend commands
 */
extern nodeAppPidList_t *       findAppPids(uint64_t);
extern void                     destroy_nodeAppPidList(nodeAppPidList_t *);
extern char *                   getNodeCName(void);
extern char *                   getNodeNidName(void);
extern int                      getNodeNid(void);
extern int                      getFirstPE(uint64_t);
extern int                      getPesHere(uint64_t);

#endif /* _TOOL_BACKEND_H */
