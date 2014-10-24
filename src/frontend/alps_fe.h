/*********************************************************************************\
 * alps_fe.h - A header file for the alps specific frontend interface.
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

#ifndef _ALPS_FE_H
#define _ALPS_FE_H

#include <stdint.h>
#include <sys/types.h>

#include "cti_fe.h"

/* wlm proto object */
extern const cti_wlm_proto_t	_cti_alps_wlmProto;

typedef struct
{
	uint64_t	apid;
	pid_t		aprunPid;
} cti_aprunProc_t;

/* function prototypes */
cti_app_id_t		cti_alps_registerApid(uint64_t);
uint64_t			cti_alps_getApid(pid_t);
cti_aprunProc_t *	cti_alps_getAprunInfo(cti_app_id_t);
int					cti_alps_getAlpsOverlapOrdinal(cti_app_id_t);

#endif /* _ALPS_FE_H */
