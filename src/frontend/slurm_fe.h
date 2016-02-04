/******************************************************************************\
 * slurm_fe.h - A header file for the cluster slurm specific frontend 
 *              interface.
 *
 * Copyright 2016 Cray Inc.	All Rights Reserved.
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

#ifndef _SLURM_FE_H
#define _SLURM_FE_H

#include <stdint.h>
#include <sys/types.h>

#include "cti_fe.h"

/* wlm proto object */
extern const cti_wlm_proto_t	_cti_slurm_wlmProto;

/* function prototypes */
cti_app_id_t		cti_slurm_registerJobStep(uint32_t, uint32_t);
cti_srunProc_t *	cti_slurm_getSrunInfo(cti_app_id_t appId);

// TODO: cti_slurm_getJobInfo(pid_t)

#endif /* _SLURM_FE_H */
