/******************************************************************************\
 * alps_backend.h - A header file for the alps_backend interface.
 *
 * © 2011-2014 Cray Inc.  All Rights Reserved.
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
 
#ifndef _CTI_BE_H
#define _CTI_BE_H

#include <sys/types.h>

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

enum cti_wlm_type
{
	CTI_WLM_NONE,	// error/unitialized state
	CTI_WLM_ALPS,
	CTI_WLM_CRAY_SLURM,
	CTI_WLM_SLURM
};
typedef enum cti_wlm_type	cti_wlm_type;

/* function prototypes */
cti_wlm_type		cti_current_wlm(void);
const char *		cti_wlm_type_toString(cti_wlm_type);
cti_pidList_t *		cti_findAppPids(void);
void				cti_destroyPidList(cti_pidList_t *);
char *				cti_getNodeHostname(void);
int					cti_getNodeFirstPE(void);
int					cti_getNodePEs(void);

#endif /* _CTI_BE_H */

