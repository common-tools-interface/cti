/******************************************************************************\
 * cti_be.h - A interface to interact with placement information on compute
 *		  nodes. This provides the tool developer with an easy to use interface
 *        to obtain application information for backend tool daemons running on
 *        the compute nodes.
 *
 * Copyright 2011-2019 Cray Inc.  All Rights Reserved.
 *
 * Unpublished Proprietary Information.
 * This unpublished work is protected to trade secret, copyright and other laws.
 * Except as permitted by contract or express written permission of Cray Inc.,
 * no part of this work or its content may be used, reproduced or disclosed
 * in any form.
 *
 ******************************************************************************/

#ifndef _CTI_BE_H
#define _CTI_BE_H

#include <sys/types.h>

#include "cti_defs.h"

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

// This is the wlm proto object that all wlm implementations should define.
// The noneness functions can be used if a function is not definable by your wlm,
// but that should only be used if an API call is truly incompatible with the wlm.
typedef struct
{
	cti_be_wlm_type		wlm_type;						// wlm type
	int 				(*wlm_init)(void);				// wlm init function - return true on error
	void				(*wlm_fini)(void);				// wlm finish function
	cti_pidList_t *		(*wlm_findAppPids)(void);		// get pids of application ranks - return NULL on error
	char *				(*wlm_getNodeHostname)(void);	// get hostname of current compute node - return NULL on error
	int					(*wlm_getNodeFirstPE)(void);	// get first numeric rank located on the current compute node - return -1 on error
	int					(*wlm_getNodePEs)(void);		// get number of ranks located on the current compute node - return -1 on error
} cti_be_wlm_proto_t;

/* internal function prototypes */
char *				_cti_be_getToolDir(void);
char *				_cti_be_getAttribsDir(void);

/* function prototypes */
const char *		cti_be_version(void);
cti_be_wlm_type		cti_be_current_wlm(void);
const char *		cti_be_wlm_type_toString(cti_be_wlm_type);
char *				cti_be_getAppId(void);
cti_pidList_t *		cti_be_findAppPids(void);
void				cti_be_destroyPidList(cti_pidList_t *);
char *				cti_be_getNodeHostname(void);
int					cti_be_getNodeFirstPE(void);
int					cti_be_getNodePEs(void);
char *				cti_be_getRootDir(void);
char *				cti_be_getBinDir(void);
char *				cti_be_getLibDir(void);
char *				cti_be_getFileDir(void);
char *				cti_be_getTmpDir(void);

/* Noneness functions for wlm proto - Use these if your wlm proto doesn't define the function */
int					_cti_be_wlm_init_none(void);
void				_cti_be_wlm_fini_none(void);
cti_pidList_t *		_cti_be_wlm_findAppPids_none(void);
char *				_cti_be_wlm_getNodeHostname_none(void);
int					_cti_be_wlm_getNodeFirstPE_none(void);
int					_cti_be_wlm_getNodePEs_none(void);

#endif /* _CTI_BE_H */
