/******************************************************************************\
 * cray_slurm_fe.h - A header file for the Cray slurm specific frontend 
 *                   interface.
 *
 * Copyright 2014 Cray Inc.	All Rights Reserved.
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

#ifndef _CRAY_SLURM_FE_H
#define _CRAY_SLURM_FE_H

#include <stdint.h>
#include <sys/types.h>

#include "cti_fe.h"

/* wlm proto object */
extern const cti_wlm_proto_t	_cti_cray_slurm_wlmProto;

typedef struct
{
	uint32_t	jobid;
	uint32_t	stepid;
} cti_srunProc_t;

/* function prototypes */
cti_app_id_t		cti_cray_slurm_registerJobStep(uint32_t, uint32_t);
cti_srunProc_t *	cti_cray_slurm_getSrunInfo(cti_app_id_t appId);

// TODO: cti_cray_slurm_getJobInfo(pid_t)

#endif /* _ALPS_FE_H */
