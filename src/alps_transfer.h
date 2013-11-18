/*********************************************************************************\
 * alps_transfer.h - A header file for the alps_transfer interface.
 *
 * © 2011-2013 Cray Inc.  All Rights Reserved.
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

#ifndef _ALPS_TRANSFER_H
#define _ALPS_TRANSFER_H

#include "useful/stringList.h"

#include "alps_defs.h"

typedef int CTI_MANIFEST_ID;
typedef int CTI_SESSION_ID;

/* function prototypes */
CTI_MANIFEST_ID	cti_createNewManifest(CTI_SESSION_ID);
void			cti_destroyManifest(CTI_MANIFEST_ID);
int				cti_addManifestBinary(CTI_MANIFEST_ID, char *);
int				cti_addManifestLibrary(CTI_MANIFEST_ID, char *);
int				cti_addManifestFile(CTI_MANIFEST_ID, char *);
CTI_SESSION_ID	cti_sendManifest(uint64_t, CTI_MANIFEST_ID, int);
CTI_SESSION_ID	cti_execToolDaemon(uint64_t, CTI_MANIFEST_ID, CTI_SESSION_ID, char *, char **, char **, int);
char **			cti_getSessionLockFiles(CTI_SESSION_ID sid);
char *			cti_getSessionRootDir(CTI_SESSION_ID);
char *			cti_getSessionBinDir(CTI_SESSION_ID);
char *			cti_getSessionLibDir(CTI_SESSION_ID);
char *			cti_getSessionFileDir(CTI_SESSION_ID);
char *			cti_getSessionTmpDir(CTI_SESSION_ID);

#endif /* _ALPS_TRANSFER_H */
