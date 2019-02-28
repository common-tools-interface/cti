/*********************************************************************************\
 * cti_appentry.h - Interface for legacy CTI app reference-counting
 *
 * Copyright 2014-2015 Cray Inc.	All Rights Reserved.
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

#ifndef _CTI_APPENTR_H
#define _CTI_APPENTRY_H

#include "cti_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

#include "cti_fe_iface.h"

#include "useful/cti_args.h"
#include "useful/cti_list.h"

#ifdef __cplusplus
class Frontend;
#else
struct Frontend;
#endif

// wlm object managed by the actual impelmentation of cti_wlm_proto_t
typedef void *	cti_wlm_obj;

typedef struct
{
	cti_app_id_t			appId;				// cti application ID
	cti_list_t *			sessions;			// sessions associated with this app entry
	const Frontend *		frontend;			// wlm proto obj of this app
	cti_wlm_obj				_wlmObj;			// Managed by appropriate wlm implementation for this app entry
	unsigned int			refCnt;				// reference count - must be 0 before removing this entry
} appEntry_t;

/* internal function prototypes */
appEntry_t *			_cti_newAppEntry(const Frontend *, cti_wlm_obj);
appEntry_t *			_cti_findAppEntry(cti_app_id_t);
int						_cti_refAppEntry(cti_app_id_t);

#ifdef __cplusplus
}
#endif

#endif /* _CTI_APPENTRY_H */
