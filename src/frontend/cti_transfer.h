/*********************************************************************************\
 * cti_transfer.h - Defines the legacy cti_transfer interface for managing session
 * and manifest information, as well as launching tool daemons with staged files.
 *
 * Copyright 2011-2015 Cray Inc.  All Rights Reserved.
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

#ifndef _CTI_TRANSFER_H
#define _CTI_TRANSFER_H

#include "cti_defs.h"

typedef int cti_manifest_id_t;
typedef int cti_session_id_t;

#ifdef __cplusplus
extern "C" {
#endif

	/* session prototypes */
	cti_session_id_t	cti_createSession(cti_app_id_t appId);
	int					cti_sessionIsValid(cti_session_id_t sid);
	int					cti_destroySession(cti_session_id_t sid);

	char **				cti_getSessionLockFiles(cti_session_id_t sid);
	char *				cti_getSessionRootDir(cti_session_id_t sid);
	char *				cti_getSessionBinDir(cti_session_id_t sid);
	char *				cti_getSessionLibDir(cti_session_id_t sid);
	char *				cti_getSessionFileDir(cti_session_id_t sid);
	char *				cti_getSessionTmpDir(cti_session_id_t sid);

	/* manifest prototypes */
	cti_manifest_id_t	cti_createManifest(cti_session_id_t sid);
	int					cti_manifestIsValid(cti_manifest_id_t mid);

	int					cti_addManifestBinary(cti_manifest_id_t mid, const char * rawName);
	int					cti_addManifestLibrary(cti_manifest_id_t mid, const char * rawName);
	int					cti_addManifestLibDir(cti_manifest_id_t mid, const char * rawName);
	int					cti_addManifestFile(cti_manifest_id_t mid, const char * rawName);

	int					cti_sendManifest(cti_manifest_id_t mid);

	/* tool daemon prototypes */
	int					cti_execToolDaemon(cti_manifest_id_t mid, const char *daemonPath,
	const char * const daemonArgs[], const char * const envVars[]);

#if 1
#define TRANSITION_DEFS
void _cti_setStageDeps(bool stageDeps);
void _cti_transfer_init(void);
void _cti_transfer_fini(void);
void _cti_consumeSession(void *);
#endif

#ifdef __cplusplus
}
#endif

#endif /* _CTI_TRANSFER_H */
