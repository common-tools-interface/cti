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

#include "cti_defs.h"
#include "cti_args.h"

/* struct typedefs */

// Internal identifier used by callers to interface with the library. When they
// request functionality that operates on applications, they must pass this
// identifier in.
typedef uint64_t cti_app_id_t;

typedef struct
{
	char *				hostname;
	int					numPes;
} cti_host_t;

typedef struct
{
	int					numHosts;
	cti_host_t *		hosts;
} cti_hostsList_t;

// wlm object managed by the actual impelmentation of cti_wlm_proto_t
typedef void *	cti_wlm_obj;

// pointer to a wlm defined application identifier, we need to use this when
// a specific implementation has a unique register function and needs to check
// if the passed in identifier has already been registered.
typedef void * cti_wlm_apid;

// This is the wlm proto object that all wlm implementations should define.
// The noneness functions can be used if a function is not definable by your wlm,
// but that should only be used if an API call is truly incompatible with the wlm.
typedef struct
{
	cti_wlm_type			wlm_type;											// wlm type
	int 					(*wlm_init)(void);									// wlm init function - return true on error
	void					(*wlm_fini)(void);									// wlm finish function
	void					(*wlm_destroy)(cti_wlm_obj);						// Used to destroy the cti_wlm_obj defined by this impelementation
	int						(*wlm_cmpJobId)(cti_wlm_obj, cti_wlm_apid);			// compare wlm specific job ids - return -1 on error, 1 on match, 0 on mismatch
	char *					(*wlm_getJobId)(cti_wlm_obj);						// return the string version of the job identifer
	cti_app_id_t			(*wlm_launchBarrier)(	const char * const [],		// launch application - return 0 on error or else cti_app_id_t
													int, 
													int, 
													int, 
													int, 
													const char *, 
													const char *, 
													const char * const []);
	int						(*wlm_releaseBarrier)(cti_wlm_obj);					// release app from barrier - return true on error
	int						(*wlm_killApp)(cti_wlm_obj, int);					// kill application - return true on error
	int						(*wlm_verifyBinary)(cti_wlm_obj, const char *);		// verify that binary file is valid for wlm - return true on invalid binary
	int						(*wlm_verifyLibrary)(cti_wlm_obj, const char *);	// verify that library file is valid for wlm - return true on invalid library
	int						(*wlm_verifyLibDir)(cti_wlm_obj, const char *);		// verify that library directory is valid for wlm - return true on invalid library directory
	int						(*wlm_verifyFile)(cti_wlm_obj, const char *);		// verify that normal file is valid for wlm - return true on invalid file
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
} cti_wlm_proto_t;

// This is the function prototype used to destroy an implementation defined obj
typedef void (*obj_destroy)(void *);

typedef struct
{
	cti_app_id_t			appId;				// cti application ID
	const cti_wlm_proto_t *	wlmProto;			// wlm proto obj of this app
	cti_wlm_obj				_wlmObj;			// Managed by appropriate wlm implementation for this app entry
	void *					_transferObj;		// Managed by alps_transfer.c for this app entry
	obj_destroy				_transferDestroy;	// Used to destroy the transfer object
} appEntry_t;

/* internal function prototypes */
cti_app_id_t			_cti_newAppEntry(const cti_wlm_proto_t *, cti_wlm_obj);
appEntry_t *			_cti_findAppEntry(cti_app_id_t);
appEntry_t *			_cti_findAppEntryByJobId(cti_wlm_apid);
int						_cti_setTransferObj(appEntry_t *, void *, obj_destroy);
const cti_wlm_proto_t *	_cti_current_wlm_proto(void);
const char *			_cti_getCfgDir(void);
int						_cti_removeDirectory(const char *);

/* API function prototypes */
cti_wlm_type			cti_current_wlm(void);
const char *			cti_wlm_type_toString(cti_wlm_type);
void					cti_deregisterApp(cti_app_id_t);
char *					cti_getHostname(void);
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
int						_cti_wlm_cmpJobId_none(cti_wlm_obj, cti_wlm_apid);
char *					_cti_wlm_getJobId_none(cti_wlm_obj);
cti_app_id_t			_cti_wlm_launchBarrier_none(const char * const [], int, int, int, int, const char *, const char *, const char * const []);
int						_cti_wlm_releaseBarrier_none(cti_wlm_obj);
int						_cti_wlm_killApp_none(cti_wlm_obj, int);
int						_cti_wlm_verifyBinary_none(cti_wlm_obj, const char *);
int						_cti_wlm_verifyLibrary_none(cti_wlm_obj, const char *);
int						_cti_wlm_verifyLibDir_none(cti_wlm_obj, const char *);
int						_cti_wlm_verifyFile_none(cti_wlm_obj, const char *);
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

#endif /* _CTI_FE_H */
