/*********************************************************************************\
 * cti_fe.h - External C interface for the cti frontend.
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

#ifndef _CTI_FE_H
#define _CTI_FE_H

#include "cti_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

// Internal identifier used by callers to interface with the library. When they
// request functionality that operates on applications, they must pass this
// identifier in.
typedef uint64_t cti_app_id_t;

/* struct typedefs */

typedef struct
{
	char *				hostname;
	int					numPEs;
} cti_host_t;

typedef struct
{
	int					numHosts;
	cti_host_t *		hosts;
} cti_hostsList_t;

/* API function prototypes */
const char *			cti_version(void);
cti_wlm_type			cti_current_wlm(void);
const char *			cti_wlm_type_toString(cti_wlm_type);
char *					cti_getHostname(void);
int						cti_appIsValid(cti_app_id_t);
void					cti_deregisterApp(cti_app_id_t);
char *					cti_getLauncherHostName(cti_app_id_t);
int						cti_getNumAppPEs(cti_app_id_t);
int						cti_getNumAppNodes(cti_app_id_t);
char **	 				cti_getAppHostsList(cti_app_id_t);
cti_hostsList_t *		cti_getAppHostsPlacement(cti_app_id_t);
void					cti_destroyHostsList(cti_hostsList_t *);

/* WLM-specific functions */

typedef struct {
	uint64_t			apid;
	pid_t				aprunPid;
} cti_aprunProc_t;

typedef struct {
	uint32_t			jobid;
	uint32_t			stepid;
} cti_srunProc_t;

uint64_t cti_alps_getApid(pid_t aprunPid);
cti_app_id_t cti_alps_registerApid(uint64_t apid);
cti_aprunProc_t * cti_alps_getAprunInfo(cti_app_id_t app_id);
int cti_alps_getAlpsOverlapOrdinal(cti_app_id_t app_Id);
cti_srunProc_t * cti_cray_slurm_getJobInfo(pid_t srunPid);
cti_app_id_t cti_cray_slurm_registerJobStep( uint32_t job_id, uint32_t step_id);
cti_srunProc_t * cti_cray_slurm_getSrunInfo(cti_app_id_t appId);
cti_app_id_t cti_slurm_registerJobStep(pid_t launcher_pid);
cti_app_id_t cti_ssh_registerJob(pid_t launcher_pid);

#ifdef __cplusplus
}
#endif

#endif /* _CTI_FE_H */
