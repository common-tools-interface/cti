/*********************************************************************************\
 * cti_fe.h - A header file for the cti frontend interface.
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

#include "useful/cti_args.h"
#include "useful/cti_list.h"

// Internal identifier used by callers to interface with the library. When they
// request functionality that operates on applications, they must pass this
// identifier in.
typedef uint64_t cti_app_id_t;

/* struct typedefs */

typedef struct
{
	uint32_t	jobid;
	uint32_t	stepid;
} cti_srunProc_t;

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

// wlm object managed by the actual impelmentation of cti_wlm_proto_t
typedef void *	cti_wlm_obj;

// This is the wlm proto object that all wlm implementations should define.
// The noneness functions can be used if a function is not definable by your wlm,
// but that should only be used if an API call is truly incompatible with the wlm.
typedef struct
{
	cti_wlm_type			wlm_type;											// wlm type
	int 					(*wlm_init)(void);									// wlm init function - return true on error
	void					(*wlm_fini)(void);									// wlm finish function
	void					(*wlm_destroy)(cti_wlm_obj);						// Used to destroy the cti_wlm_obj defined by this impelementation
	char *					(*wlm_getJobId)(cti_wlm_obj);						// return the string version of the job identifer
	cti_app_id_t			(*wlm_launch)(			const char * const [],		// launch application without barrier - return 0 on error or else cti_app_id_t
													int, 
													int, 
													const char *, 
													const char *, 
													const char * const []);
	cti_app_id_t			(*wlm_launchBarrier)(	const char * const [],		// launch application with barrier - return 0 on error or else cti_app_id_t
													int, 
													int, 
													const char *, 
													const char *, 
													const char * const []);
	int						(*wlm_releaseBarrier)(cti_wlm_obj);					// release app from barrier - return true on error
	int						(*wlm_killApp)(cti_wlm_obj, int);					// kill application - return true on error
	const char * const *	(*wlm_extraBinaries)(cti_wlm_obj);					// extra wlm specific binaries required by backend library - return NULL if none
	const char * const *	(*wlm_extraLibraries)(cti_wlm_obj);					// extra wlm specific libraries required by backend library - return NULL if none
	const char * const *	(*wlm_extraLibDirs)(cti_wlm_obj);					// extra wlm specific library directories required by backend library - return NULL if none
	const char * const *	(*wlm_extraFiles)(cti_wlm_obj);						// extra wlm specific files required by backend library - return NULL if none
	int						(*wlm_shipPackage)(cti_wlm_obj, const char *);		// ship package to backends - return true on error
	int						(*wlm_startDaemon)(cti_wlm_obj, cti_args_t *);		// start backend tool daemon - return true on error
	int						(*wlm_getNumAppPEs)(cti_wlm_obj);					// retrieve number of PEs in app - return 0 on error
	int						(*wlm_getNumAppNodes)(cti_wlm_obj);					// retrieve number of compute nodes in app - return 0 on error
	char **					(*wlm_getAppHostsList)(cti_wlm_obj);				// get hosts list for app - return NULL on error
	cti_hostsList_t *		(*wlm_getAppHostsPlacement)(cti_wlm_obj);			// get PE rank/host placement for app - return NULL on error
	char *					(*wlm_getHostName)(void);							// get hostname of current node - return NULL on error
	char *					(*wlm_getLauncherHostName)(cti_wlm_obj);			// get hostname where the job launcher was started - return NULL on error
	const char *			(*wlm_getToolPath)(cti_wlm_obj);					// get backend base directory used for staging - return NULL on error
	const char *			(*wlm_getAttribsPath)(cti_wlm_obj);					// get backend directory where the pmi_attribs file can be found
} cti_wlm_proto_t;

typedef struct
{
	cti_app_id_t			appId;				// cti application ID
	cti_list_t *			sessions;			// sessions associated with this app entry
	const cti_wlm_proto_t *	wlmProto;			// wlm proto obj of this app
	cti_wlm_obj				_wlmObj;			// Managed by appropriate wlm implementation for this app entry
	unsigned int			refCnt;				// reference count - must be 0 before removing this entry
} appEntry_t;

/* internal function prototypes */
const char *			_cti_getLdAuditPath(void);
const char *			_cti_getOverwatchPath(void);
const char *			_cti_getDlaunchPath(void);
const char *			_cti_getSlurmUtilPath(void);
const char *			_cti_getCfgDir(void);
appEntry_t *			_cti_newAppEntry(const cti_wlm_proto_t *, cti_wlm_obj);
appEntry_t *			_cti_findAppEntry(cti_app_id_t);
int						_cti_refAppEntry(cti_app_id_t);
const cti_wlm_proto_t *	_cti_current_wlm_proto(void);

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

/* Noneness functions for wlm proto - Use these if your wlm proto doesn't define the function */
int						_cti_wlm_init_none(void);
void					_cti_wlm_fini_none(void);
void					_cti_wlm_destroy_none(cti_wlm_obj);
char *					_cti_wlm_getJobId_none(cti_wlm_obj);
cti_app_id_t			_cti_wlm_launch_none(const char * const [], int, int, const char *, const char *, const char * const []);
cti_app_id_t			_cti_wlm_launchBarrier_none(const char * const [], int, int, const char *, const char *, const char * const []);
int						_cti_wlm_releaseBarrier_none(cti_wlm_obj);
int						_cti_wlm_killApp_none(cti_wlm_obj, int);
const char * const *	_cti_wlm_extraBinaries_none(cti_wlm_obj);
const char * const *	_cti_wlm_extraLibraries_none(cti_wlm_obj);
const char * const *	_cti_wlm_extraLibDirs_none(cti_wlm_obj);
const char * const *	_cti_wlm_extraFiles_none(cti_wlm_obj);
int						_cti_wlm_shipPackage_none(cti_wlm_obj, const char *);
int						_cti_wlm_startDaemon_none(cti_wlm_obj, cti_args_t *);
int						_cti_wlm_getNumAppPEs_none(cti_wlm_obj);
int						_cti_wlm_getNumAppNodes_none(cti_wlm_obj);
char **					_cti_wlm_getAppHostsList_none(cti_wlm_obj);
cti_hostsList_t *		_cti_wlm_getAppHostsPlacement_none(cti_wlm_obj);
char *					_cti_wlm_getHostName_none(void);
char *					_cti_wlm_getLauncherHostName_none(cti_wlm_obj);
const char *			_cti_wlm_getToolPath_none(cti_wlm_obj);
const char *			_cti_wlm_getAttribsPath_none(cti_wlm_obj);

/* 
 *  This enum enumerates the various attributes that 
 *  can be set by cti_setAttribute.
 */
enum cti_attr_type
{
    CTI_ATTR_STAGE_DEPENDENCIES     // Define whether binary and library 
                                    // dependencies should be automatically 
                                    // staged by cti_addManifestBinary and 
                                    // cti_addManifestLIbrary: 0 or 1
                                    // Defaults to 1.
};
typedef enum cti_attr_type  cti_attr_type;

#ifdef __cplusplus
}
#endif

#endif /* _CTI_FE_H */
