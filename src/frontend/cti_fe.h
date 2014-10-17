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

// This is the wlm proto object that all wlm implementations should define.
// The noneness functions can be used if a function is not definable by your wlm,
// but that should only be used if an API call is truly incompatible with the wlm.
typedef struct
{
	cti_wlm_type			wlm_type;										// wlm type
	int 					(*wlm_init)(void);								// wlm init function - return true on error
	void					(*wlm_fini)(void);								// wlm finish function
	int						(*wlm_cmpJobId)(void *, void *);				// compare wlm specific job ids - return -1 on error, 1 on match, 0 on mismatch
	char *					(*wlm_getJobId)(void *);						// return the string version of the job identifer
	cti_app_id_t			(*wlm_launchBarrier)(	char * const [],		// launch application - return 0 on error or else cti_app_id_t
												int, 
												int, 
												int, 
												int, 
												const char *, 
												const char *, 
												char * const []);
	int						(*wlm_releaseBarrier)(void *);					// release app from barrier - return true on error
	int						(*wlm_killApp)(void *, int);					// kill application - return true on error
	int						(*wlm_verifyBinary)(const char *);				// verify that binary file is valid for wlm - return true on invalid binary
	int						(*wlm_verifyLibrary)(const char *);				// verify that library file is valid for wlm - return true on invalid library
	int						(*wlm_verifyLibDir)(const char *);				// verify that library directory is valid for wlm - return true on invalid library directory
	int						(*wlm_verifyFile)(const char *);				// verify that normal file is valid for wlm - return true on invalid file
	const char * const *	(*wlm_extraBinaries)(void);						// extra wlm specific binaries required by backend library - return NULL if none
	const char * const *	(*wlm_extraLibraries)(void);					// extra wlm specific libraries required by backend library - return NULL if none
	const char * const *	(*wlm_extraLibDirs)(void);						// extra wlm specific library directories required by backend library - return NULL if none
	const char * const *	(*wlm_extraFiles)(void);						// extra wlm specific files required by backend library - return NULL if none
	int						(*wlm_shipPackage)(void *, const char *);		// ship package to backends - return true on error
	int						(*wlm_startDaemon)(void *, int, const char *, const char *);	// start backend tool daemon - return true on error
	int						(*wlm_getNumAppPEs)(void *);					// retrieve number of PEs in app - return 0 on error
	int						(*wlm_getNumAppNodes)(void *);					// retrieve number of compute nodes in app - return 0 on error
	char **					(*wlm_getAppHostsList)(void *);					// get hosts list for app - return NULL on error
	cti_hostsList_t *		(*wlm_getAppHostsPlacement)(void *);			// get PE rank/host placement for app - return NULL on error
	char *					(*wlm_getHostName)(void);						// get hostname of current node - return NULL on error
	char *					(*wlm_getLauncherHostName)(void *);				// get hostname where the job launcher was started - return NULL on error
} cti_wlm_proto_t;

// This is the function prototype used to destroy the passed in objects managed
// by outside interfaces.
typedef void (*obj_destroy)(void *);

typedef struct
{
	cti_app_id_t		appId;				// cti application ID
	cti_wlm_proto_t *	wlmProto;			// wlm proto obj of this app
	char *				toolPath;			// backend toolhelper path for temporary storage
	void *				_wlmObj;			// Managed by appropriate wlm fe for this app entry
	obj_destroy			_wlmDestroy;		// Used to destroy the wlm object
	int					_transfer_init;		// Managed by alps_transfer.c for this app entry
	void *				_transferObj;		// Managed by alps_transfer.c for this app entry
	obj_destroy			_transferDestroy;	// Used to destroy the transfer object
} appEntry_t;

/* current wlm proto */
extern cti_wlm_proto_t *	_cti_wlmProto;

/* function prototypes */
cti_app_id_t			_cti_newAppEntry(cti_wlm_proto_t *, const char *, void *, obj_destroy);
appEntry_t *			_cti_findAppEntry(cti_app_id_t);
appEntry_t *			_cti_findAppEntryByJobId(void *);
int						_cti_setTransferObj(appEntry_t *, void *, obj_destroy);

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
int						_cti_wlm_cmpJobId_none(void *, void *);
char *					_cti_wlm_getJobId_none(void *);
cti_app_id_t			_cti_wlm_launchBarrier_none(char * const [], int, int, int, int, const char *, const char *, char * const []);
int						_cti_wlm_releaseBarrier_none(void *);
int						_cti_wlm_killApp_none(void *, int);
int						_cti_wlm_verifyBinary_none(const char *);
int						_cti_wlm_verifyLibrary_none(const char *);
int						_cti_wlm_verifyLibDir_none(const char *);
int						_cti_wlm_verifyFile_none(const char *);
const char * const *	_cti_wlm_extraBinaries_none(void);
const char * const *	_cti_wlm_extraLibraries_none(void);
const char * const *	_cti_wlm_extraLibDirs_none(void);
const char * const *	_cti_wlm_extraFiles_none(void);
int						_cti_wlm_shipPackage_none(void *, const char *);
int						_cti_wlm_startDaemon_none(void *, int, const char *, const char *);
int						_cti_wlm_getNumAppPEs_none(void *);
int						_cti_wlm_getNumAppNodes_none(void *);
char **					_cti_wlm_getAppHostsList_none(void *);
cti_hostsList_t *		_cti_wlm_getAppHostsPlacement_none(void *);
char *					_cti_wlm_getHostName_none(void);
char *					_cti_wlm_getLauncherHostName_none(void *);

#endif /* _CTI_FE_H */
