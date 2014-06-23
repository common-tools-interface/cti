/*********************************************************************************\
 * cti_transfer.h - A header file for the cti_transfer interface.
 *
 * © 2011-2014 Cray Inc.  All Rights Reserved.
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

#include <stdint.h>

#include "cti_fe.h"

typedef int cti_manifest_id_t;
typedef int cti_session_id_t;

/* function prototypes */
void				_cti_destroyAppSess(void *);

cti_manifest_id_t	cti_createNewManifest(cti_session_id_t);
void				cti_destroyManifest(cti_manifest_id_t);
int					cti_addManifestBinary(cti_manifest_id_t, const char *);
int					cti_addManifestLibrary(cti_manifest_id_t, const char *);
int					cti_addManifestLibDir(cti_manifest_id_t, const char *);
int					cti_addManifestFile(cti_manifest_id_t, const char *);
cti_session_id_t	cti_sendManifest(cti_app_id_t, cti_manifest_id_t, int);
cti_session_id_t	cti_execToolDaemon(cti_app_id_t, cti_manifest_id_t, cti_session_id_t, char *, char **, char **, int);
char **				cti_getSessionLockFiles(cti_session_id_t);
char *				cti_getSessionRootDir(cti_session_id_t);
char *				cti_getSessionBinDir(cti_session_id_t);
char *				cti_getSessionLibDir(cti_session_id_t);
char *				cti_getSessionFileDir(cti_session_id_t);
char *				cti_getSessionTmpDir(cti_session_id_t);

#endif /* _CTI_TRANSFER_H */
