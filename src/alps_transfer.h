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

typedef int MANIFEST_ID;
typedef int SESSION_ID;

typedef struct
{
	MANIFEST_ID		mid;			// manifest id
	SESSION_ID		sid;			// optional session id
	int				inst;			// instance number - used with session to prevent tarball name conflicts
	char *			stage_name;		// basename of the manifest directory
	stringList_t *	exec_names;		// list of manifest binary names
	stringList_t *	lib_names;		// list of manifest dso names
	stringList_t *	file_names;		// list of manifest regular file names
	stringList_t *	exec_loc;		// fullpath of manifest binaries
	stringList_t *	lib_loc;		// fullpath of manifest libaries
	stringList_t *	file_loc;		// fullpath of manifest files
} manifest_t;

typedef struct
{
	SESSION_ID		sid;			// session id
	int				instCnt;		// instance count - set in the manifest to prevent naming conflicts
	char *			stage_name;		// basename of the manifest directory
	char *			toolPath;		// toolPath of the app entry - DO NOT FREE THIS!!!
	stringList_t *	exec_names;		// list of manifest binary names
	stringList_t *	lib_names;		// list of manifest dso names
	stringList_t *	file_names;		// list of manifest regular file names
} session_t;

struct manifList
{
	manifest_t *		thisEntry;
	struct manifList *	nextEntry;
};
typedef struct manifList manifList_t;

struct sessList
{
	session_t *			thisEntry;
	struct sessList *	nextEntry;
};
typedef struct sessList sessList_t;

// This gets placed into the appEntry_t struct
typedef struct
{
	int		numSess;
	int *	session_ids;
	size_t	len;
} sessMgr_t;

// Used to define the increment size when alloc/realloc session_ids array
#define SESS_INC_SIZE		10

/* function prototypes */
MANIFEST_ID		createNewManifest(SESSION_ID);
void			destroyManifest(MANIFEST_ID);
int				addManifestBinary(MANIFEST_ID, char *);
int				addManifestLibrary(MANIFEST_ID, char *);
int				addManifestFile(MANIFEST_ID, char *);
SESSION_ID		sendManifest(uint64_t, MANIFEST_ID, int);
SESSION_ID		execToolDaemon(uint64_t, MANIFEST_ID, SESSION_ID, char *, char **, char **, int);
char **			getSessionLockFiles(SESSION_ID sid);
char *			getSessionRootDir(SESSION_ID);
char *			getSessionBinDir(SESSION_ID);
char *			getSessionLibDir(SESSION_ID);
char *			getSessionFileDir(SESSION_ID);
char *			getSessionTmpDir(SESSION_ID);

#endif /* _ALPS_TRANSFER_H */
