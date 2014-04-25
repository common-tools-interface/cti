/*********************************************************************************\
 * cti_fe.h - A header file for the cti frontend interface.
 *
 * © 2014 Cray Inc.	All Rights Reserved.
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

#ifndef _CTI_FE_H
#define _CTI_FE_H

#include <stdint.h>
#include <sys/types.h>

/* struct typedefs */

// Internal identifier used by callers to interface with the library. When they
// request functionality that operates on applications, they must pass this
// identifier in.
typedef uint64_t cti_app_id_t;

// This is the function prototype used to destroy the passed in objects managed
// by outside interfaces.
typedef void (*obj_destroy)(void *);

enum cti_wlm_type
{
	CTI_WLM_NONE,	// error/unitialized state
	CTI_WLM_MULTI,	// not used yet - placeholder for future design
	CTI_WLM_ALPS,
	CTI_WLM_CRAY_SLURM,
	CTI_WLM_SLURM
};
typedef enum cti_wlm_type	cti_wlm_type;

typedef struct
{
	cti_app_id_t			appId;				// cti application ID
	cti_wlm_type			wlm;				// WLM type of this app
	char *					toolPath;			// backend toolhelper path for temporary storage
	void *					_wlmObj;			// Managed by appropriate wlm fe for this app entry
	obj_destroy				_wlmDestroy;		// Used to destroy the wlm object
	int						_transfer_init;		// Managed by alps_transfer.c for this app entry
	void *					_transferObj;		// Managed by alps_transfer.c for this app entry
	obj_destroy				_transferDestroy;	// Used to destroy the transfer object
} appEntry_t;

/* function prototypes */

cti_app_id_t	_cti_newAppEntry(cti_wlm_type, const char *, void *, obj_destroy);
appEntry_t *	_cti_findAppEntry(cti_app_id_t);
appEntry_t *	_cti_findAppEntryByJobId(void *);
int				_cti_setTransferObj(cti_app_id_t, void *, obj_destroy);

cti_wlm_type	cti_current_wlm(void);
void			cti_deregisterApp(cti_app_id_t);

#endif /* _CTI_FE_H */
