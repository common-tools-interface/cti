/******************************************************************************\
 * alps_backend.h - A header file for the alps_backend interface.
 *
 * © 2011-2013 Cray Inc.  All Rights Reserved.
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

// External visibility
typedef struct
{
	pid_t	pid;	// This entries pid
	int		rank;	// This entries rank
} cti_rankPidPair_t;

typedef struct
{
	int					numPids;
	cti_rankPidPair_t *	pids;
} cti_pidList_t;

/* function prototypes */
cti_pidList_t *		cti_findAppPids(void);
void				cti_destroy_pidList(cti_pidList_t *);
char *				cti_getNodeCName(void);
char *				cti_getNodeNidName(void);
int					cti_getNodeNid(void);
int					cti_getFirstPE(void);
int					cti_getPesHere(void);

#endif /* _ALPS_BACKEND_H */

