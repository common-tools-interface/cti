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

// app launch / lifecycle
typedef struct {
	char *	hostname;
	int   	numPEs;
} cti_host_t;

typedef struct {
	int         	numHosts;
	cti_host_t *	hosts;
} cti_hostsList_t;

// file transfers
typedef int cti_manifest_id_t;
typedef int cti_session_id_t;

// SLURM-specific
typedef struct {
	uint32_t	jobid;
	uint32_t	stepid;
} cti_srunProc_t;

/* API function prototypes */

// current frontend information query
const char *	cti_version(void);
cti_wlm_type	cti_current_wlm(void);
const char *	cti_wlm_type_toString(cti_wlm_type);
char *      	cti_getHostname(void);

// running app information query
char *           	cti_getLauncherHostName(cti_app_id_t);
int              	cti_getNumAppPEs(cti_app_id_t);
int              	cti_getNumAppNodes(cti_app_id_t);
char **          	cti_getAppHostsList(cti_app_id_t);
cti_hostsList_t *	cti_getAppHostsPlacement(cti_app_id_t);
void             	cti_destroyHostsList(cti_hostsList_t *);

// app lifecycle management
int         	cti_appIsValid(cti_app_id_t);
void        	cti_deregisterApp(cti_app_id_t);
cti_app_id_t	cti_launchApp(const char * const [], int, int, const char *, const char *, const char * const []);
cti_app_id_t	cti_launchAppBarrier(const char * const [], int, int, const char *, const char *, const char * const []);
int         	cti_releaseAppBarrier(cti_app_id_t);
int         	cti_killApp(cti_app_id_t, int);

// transfer global state management
void _cti_transfer_init(void);
void _cti_transfer_fini(void);
// global setStageDeps for backwards compatability
extern bool _cti_stage_deps; // located in cti_transfer/cti_transfer.cpp
void _cti_setStageDeps(bool stageDeps);

// transfer session management
cti_session_id_t	cti_createSession(cti_app_id_t appId);
int             	cti_sessionIsValid(cti_session_id_t sid);
int             	cti_destroySession(cti_session_id_t sid);
void _cti_consumeSession(void* sidPtr); // destroy session via appentry's session list

// transfer session directory listings
char **	cti_getSessionLockFiles(cti_session_id_t sid);
char * 	cti_getSessionRootDir(cti_session_id_t sid);
char * 	cti_getSessionBinDir(cti_session_id_t sid);
char * 	cti_getSessionLibDir(cti_session_id_t sid);
char * 	cti_getSessionFileDir(cti_session_id_t sid);
char * 	cti_getSessionTmpDir(cti_session_id_t sid);


// transfer manifest management
cti_manifest_id_t	cti_createManifest(cti_session_id_t sid);
int              	cti_manifestIsValid(cti_manifest_id_t mid);
int              	cti_addManifestBinary(cti_manifest_id_t mid, const char * rawName);
int              	cti_addManifestLibrary(cti_manifest_id_t mid, const char * rawName);
int              	cti_addManifestLibDir(cti_manifest_id_t mid, const char * rawName);
int              	cti_addManifestFile(cti_manifest_id_t mid, const char * rawName);
int              	cti_sendManifest(cti_manifest_id_t mid);

// tool daemon management
int cti_execToolDaemon(cti_manifest_id_t mid, const char *daemonPath,
const char * const daemonArgs[], const char * const envVars[]);

/* WLM-specific functions */

// Cray-SLURM
cti_srunProc_t * cti_cray_slurm_getJobInfo(pid_t srunPid);
cti_app_id_t cti_cray_slurm_registerJobStep(uint32_t job_id, uint32_t step_id);
cti_srunProc_t * cti_cray_slurm_getSrunInfo(cti_app_id_t appId);

// SSH
cti_app_id_t cti_ssh_registerJob(pid_t launcher_pid);

#ifdef __cplusplus
}
#endif

#endif /* _CTI_FE_H */
