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

#define ALPS_LAUNCHER       "dlaunch"
#define CFG_DIR_VAR         "CRAY_CTI_CFG_DIR"
#define DAEMON_STAGE_DIR    "CRAY_CTI_STAGE_DIR"
#define DEFAULT_STAGE_DIR	"cti_daemonXXXXXX"

typedef int MANIFEST_ID;

typedef struct
{
	MANIFEST_ID		mid;			// manifest id
	char *			tarball_name;	// basename of manifest tarball sent to compute node
	stringList_t *	exec_names;		// list of manifest binary names
	stringList_t *	lib_names;		// list of manifest dso names
	stringList_t *	file_names;		// list of manifest regular file names
	stringList_t *	exec_loc;		// fullpath of manifest binaries
	stringList_t *	lib_loc;		// fullpath of manifest libaries
	stringList_t *	file_loc;		// fullpath of manifest files
} manifest_t;

struct manifList
{
	manifest_t *		thisEntry;
	struct manifList *	nextEntry;
};
typedef struct manifList manifList_t;

/* function prototypes */
MANIFEST_ID		createNewManifest(void);
void			destroyManifest(MANIFEST_ID);
int				addManifestBinary(MANIFEST_ID, char *);
int				addManifestLibrary(MANIFEST_ID, char *);
int				addManifestFile(MANIFEST_ID, char *);
int             execToolDaemon(uint64_t, MANIFEST_ID, char *, char **, char **, int);

#endif /* _ALPS_TRANSFER_H */
