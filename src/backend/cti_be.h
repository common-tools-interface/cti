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
	cti_wlm_type		wlm_type;						// wlm type
	int 				(*wlm_init)(void);				// wlm init function - return true on error
	void				(*wlm_fini)(void);				// wlm finish function
	cti_pidList_t *		(*wlm_findAppPids)(void);		// get pids of application ranks - return NULL on error
	char *				(*wlm_getNodeHostname)(void);	// get hostname of current compute node - return NULL on error
	int					(*wlm_getNodeFirstPE)(void);	// get first numeric rank located on the current compute node - return -1 on error
	int					(*wlm_getNodePEs)(void);		// get number of ranks located on the current compute node - return -1 on error
} cti_wlm_proto_t;

/* current wlm proto */
extern cti_wlm_proto_t *	_cti_wlmProto;

/* internal function prototypes */
char *				_cti_getToolDir(void);

/* function prototypes */
cti_wlm_type		cti_current_wlm(void);
const char *		cti_wlm_type_toString(cti_wlm_type);
char *				cti_getAppId(void);
cti_pidList_t *		cti_findAppPids(void);
void				cti_destroyPidList(cti_pidList_t *);
char *				cti_getNodeHostname(void);
int					cti_getNodeFirstPE(void);
int					cti_getNodePEs(void);
char *				cti_getRootDir(void);
char *				cti_getBinDir(void);
char *				cti_getLibDir(void);
char *				cti_getFileDir(void);
char *				cti_getTmpDir(void);

/* Noneness functions for wlm proto - Use these if your wlm proto doesn't define the function */
int					_cti_wlm_init_none(void);
void				_cti_wlm_fini_none(void);
cti_pidList_t *		_cti_wlm_findAppPids_none(void);
char *				_cti_wlm_getNodeHostname_none(void);
int					_cti_wlm_getNodeFirstPE_none(void);
int					_cti_wlm_getNodePEs_none(void);

#endif /* _CTI_BE_H */

