/*********************************************************************************\
 * cti_transfer.c - A generic interface to the transfer files and start daemons.
 *		   This provides a tool developer with an easy to use interface to
 *		   transfer binaries, shared libraries, and files to the compute nodes
 *		   associated with an app. This can also be used to launch tool daemons
 *		   on the compute nodes in an automated way.
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <sys/stat.h>
#include <sys/types.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <archive.h>
#include <archive_entry.h>

#include "alps_fe.h"
#include "cti_fe.h"
#include "cti_defs.h"
#include "cti_error.h"
#include "cti_transfer.h"
#include "ld_val.h"
#include "cti_useful.h"

/* Types used here */

typedef enum
{
	EXEC_ENTRY,
	LIB_ENTRY,
	LIBDIR_ENTRY,
	FILE_ENTRY
} entry_type;

struct fileEntry
{
	entry_type			type;			// type of file - so we can share trie data structure with other entries
	char *				loc;			// location of file
	int					present;		// already shipped?
	struct fileEntry *	next;			// linked list of fileEntry
};
typedef struct fileEntry	fileEntry_t;

typedef struct
{
	cti_manifest_id_t	mid;			// manifest id
	cti_session_id_t	sid;			// optional session id
	int					inst;			// instance number - used with session to prevent tarball name conflicts
	char *				stage_name;		// basename of the manifest directory
	stringList_t *		files;			// list of manifest files
} manifest_t;

typedef struct
{
	cti_session_id_t	sid;			// session id
	int					instCnt;		// instance count - set in the manifest to prevent naming conflicts
	char *				stage_name;		// basename of the manifest directory
	char *				toolPath;		// toolPath of the app entry - DO NOT FREE THIS!!!
	stringList_t *		files;			// list of session files
} session_t;

struct manifList
{
	manifest_t *		this;
	struct manifList *	next;
};
typedef struct manifList manifList_t;

struct sessList
{
	session_t *			this;
	struct sessList *	next;
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

// Used to define block size for file->file copies
#define CTI_BLOCK_SIZE	65536

/* Static prototypes */
static const char *		_cti_entryTypeToString(entry_type);
static fileEntry_t *	_cti_newFileEntry(void);
static void				_cti_consumeFileEntry(void *);
static fileEntry_t *	_cti_copyFileEntry(fileEntry_t *);
static fileEntry_t **	_cti_findFileEntryLoc(fileEntry_t *, entry_type);
static int				_cti_mergeFileEntry(fileEntry_t *, fileEntry_t *);
static int				_cti_chainFileEntry(fileEntry_t *, const char *, entry_type, char *, int);
static int				_cti_addSession(session_t *);
static void				_cti_reapSession(cti_session_id_t);
static void				_cti_consumeSession(session_t *);
static session_t *		_cti_findSession(cti_session_id_t);
static session_t *		_cti_newSession(manifest_t *);
static int				_cti_addManifest(manifest_t *);
static void				_cti_reapManifest(cti_manifest_id_t);
static void				_cti_consumeManifest(manifest_t *);
static manifest_t *		_cti_findManifest(cti_manifest_id_t);
static manifest_t *		_cti_newManifest(cti_session_id_t);
static int				_cti_addManifestToSession(manifest_t *, session_t *);
static void				_cti_addSessionToApp(appEntry_t *, cti_session_id_t);
static int				_cti_addDirToArchive(struct archive *, struct archive_entry *, const char *);
static int				_cti_copyFileToArchive(struct archive *, struct archive_entry *, const char *, const char *, char *);
static void				_cti_transfer_doCleanup(void);
static int				_cti_packageManifestAndShip(appEntry_t *, manifest_t *);

/* global variables */
static bool						_cti_doCleanup		= true;		// should we try to cleanup?
static sessList_t *				_cti_my_sess		= NULL;
static cti_session_id_t			_cti_next_sid		= 1;
static manifList_t *			_cti_my_manifs		= NULL;
static cti_manifest_id_t		_cti_next_mid		= 1;
static bool						_cti_r_init			= false;	// is initstate()?
static char *					_cti_r_state[256];				// state for random()

// stuff used in the signal handler, hence volatile
static volatile sig_atomic_t	_cti_signaled		= 0;	// True if we were signaled
static volatile sig_atomic_t	_cti_error			= 0;	// True if we were unable to cleanup in handler
static volatile sig_atomic_t	_cti_setup			= 0;	// True if _cti_signals is valid
static volatile cti_signals_t *	_cti_signals		= NULL;	// Old signal handlers
static volatile const char *	_cti_tar_file		= NULL;	// file to cleanup on signal
static volatile const char *	_cti_hidden_file	= NULL;	// hidden file to cleanup on signal

// valid chars array used in seed generation
static const char const		_cti_valid_char[]		= {
	'0','1','2','3','4','5','6','7','8','9',
	'A','B','C','D','E','F','G','H','I','J','K','L','M',
	'N','O','P','Q','R','S','T','U','V','W','X','Y','Z',
	'a','b','c','d','e','f','g','h','i','j','k','l','m',
	'n','o','p','q','r','s','t','u','v','w','x','y','z' };

/* signal handler to enforce cleanup of tmpfile */
static void
_cti_transfer_handler(int sig)
{
	// if already signaled, simply return. Since we restore the default signals
	// in this handler, and we block all other signals while in the handler, it
	// should be impossible to encounter this scenario.
	if (_cti_signaled)
		return;
	
	// set signaled to true
	_cti_signaled = 1;
	
	// Ensure that the shared objects have been setup
	if (!_cti_setup)
	{
		_cti_error = 1;
		return;
	}
	
	// It is safe to read from the global static objects
	
	// The following cast is safe since we block all other signals while inside
	// the handler. Only unlink the hidden file if the tarball unlink worked.
	if (unlink((char *)_cti_tar_file))
	{
		// failed to unlink the tar file
		_cti_error = 1;
	} else if (unlink((char *)_cti_hidden_file))
	{
		// failed to unlink the hidden file
		_cti_error = 1;
	}
	
	// Restore old signal handlers - again this cast is safe.
	_cti_restore_handler((cti_signals_t *)_cti_signals);
	
	// re-raise the signal
	raise(sig);
}

/* static functions */

static const char *
_cti_entryTypeToString(entry_type type)
{
	switch (type)
	{
		case EXEC_ENTRY:
			return "binary";
			
		case LIB_ENTRY:
			return "library";
			
		case LIBDIR_ENTRY:
			return "library directory";
		
		case FILE_ENTRY:
			return "file";
	}
	
	// shouldn't get here
	return "unknown";
}

static fileEntry_t *
_cti_newFileEntry(void)
{
	fileEntry_t *	newEntry;
	
	// alloc space for the new file entry
	if ((newEntry = malloc(sizeof(fileEntry_t))) == (void *)0)
	{
		_cti_set_error("malloc failed.");
		return NULL;
	}
	memset(newEntry, 0, sizeof(fileEntry_t));     // clear it to NULL
	
	return newEntry;
}

static void
_cti_consumeFileEntry(void *arg)
{
	fileEntry_t *entry = (fileEntry_t *)arg;

	// sanity check
	if (entry == NULL)
		return;
		
	if (entry->loc != NULL)
		free(entry->loc);
		
	if (entry->next != NULL)
		_cti_consumeFileEntry(entry->next);
		
	free(entry);
}

static fileEntry_t *
_cti_copyFileEntry(fileEntry_t *entry)
{
	fileEntry_t *	newEntry;
	
	// sanity check
	if (entry == NULL)
		return NULL;
		
	// create a new entry
	if ((newEntry = _cti_newFileEntry()) == NULL)
	{
		// error already set
		return NULL;
	}
	
	// copy the contents
	newEntry->type = entry->type;
	if (entry->loc)
	{
		newEntry->loc = strdup(entry->loc);
	}
	newEntry->present = entry->present;
	if (entry->next != NULL)
	{
		newEntry->next = _cti_copyFileEntry(entry->next);
	}
	
	return newEntry;
}

static fileEntry_t **
_cti_findFileEntryLoc(fileEntry_t *entry, entry_type type)
{
	fileEntry_t **	ptr;
	
	// sanity check
	if (entry == NULL)
	{
		_cti_set_error("_cti_findFileEntryLoc: Bad args.");
		return NULL;
	}
	
	ptr = &entry;
	
	// try to find the type in this fileEntry chain
	while (*ptr != NULL)
	{
		// check if this entry type matches
		if ((*ptr)->type == type)
		{
			// found it
			break;
		}
		// point at the next entry
		ptr = &((*ptr)->next);
	}
	
	return ptr;
}

// Merges all unique e2 types to e1. Any types already in e1 are not added.
// The reason this function exists is if there is a naming collision between
// bin/lib/libdir/file in the string list which require chaining in the all or
// parts of the linked list.
static int
_cti_mergeFileEntry(fileEntry_t *e1, fileEntry_t *e2)
{
	fileEntry_t **	ptr;

	// sanity check
	if (e1 == NULL || e2 == NULL)
	{
		_cti_set_error("_cti_mergeFileEntry: Bad args.");
		return 1;
	}
	
	while (e2 != NULL)
	{
		// check to see if type e2 is already in e1
		ptr = _cti_findFileEntryLoc(e1, e2->type);
		
		// check if ptr is null, if so entry not found
		if (*ptr == NULL)
		{
			// unique entry, so add just this entry, we don't want to copy the
			// next chain in.
			*ptr = _cti_newFileEntry();
			if (*ptr == NULL)
			{
				// error already set
				return 1;
			}
			// copy the contents
			(*ptr)->type = e2->type;
			if (e2->loc)
			{
				(*ptr)->loc = strdup(e2->loc);
			}
			(*ptr)->present = e2->present;
			(*ptr)->next = NULL;	// do not copy next
		}
		
		// increment e2
		e2 = e2->next;
	}
	
	return 0;
}

// Try to add a new file entry to an existing fileEntry chain. Returns -1 on error,
// 0 on success, and 1 if the file is already present.
static int
_cti_chainFileEntry(fileEntry_t *entry, const char *name, entry_type type, char *loc, int present)
{
	fileEntry_t **	ptr;
	
	// sanity
	if (entry == NULL || name == NULL || loc == NULL)
	{
		_cti_set_error("_cti_chainFileEntry: Bad args.");
		return -1;
	}
	
	// check to see if type is already in the chain
	ptr = _cti_findFileEntryLoc(entry, type);
	
	// If NULL we can add the binary to this chain
	if (*ptr == NULL)
	{
		// create new entry
		*ptr = _cti_newFileEntry();
		if (*ptr == NULL)
		{
			// error already set
			return -1;
		}
		
		// set data entries
		(*ptr)->type = type;
		(*ptr)->loc  = loc;
		(*ptr)->present = present;
		(*ptr)->next = NULL;
		
	} else
	{
		// An entry of this type already exists, check to see if we should error
		// out or silently fail since the caller asked to add a file already in
		// the manifest
		if (strncmp((*ptr)->loc, loc, strlen((*ptr)->loc)))
		{
			// location strings do not match, so error
			_cti_set_error("A %s named %s has already been added to the manifest.", _cti_entryTypeToString(type), name);
			return -1;
		}
		
		// file locations match. No conflict, return success since the file is already
		// in the manifest
		return 1;
	}
	
	// added file to the chain
	return 0;
}

static int
_cti_addSession(session_t *sess)
{
	sessList_t *	newEntry;
	sessList_t *	lstPtr;
	
	// sanity check
	if (sess == NULL)
	{
		_cti_set_error("_cti_addSessList failed.");
		return 1;
	}
	
	// alloc space for the new list entry
	if ((newEntry = malloc(sizeof(sessList_t))) == (void *)0)
	{
		_cti_set_error("malloc failed.");
		return 1;
	}
	memset(newEntry, 0, sizeof(sessList_t));     // clear it to NULL
	
	// set the sess in the list entry
	newEntry->this = sess;
	
	// if _cti_my_sess is null, this is the new head of the list
	if ((lstPtr = _cti_my_sess) == NULL)
	{
		_cti_my_sess = newEntry;
	} else
	{
		// we need to iterate through the list to find the open next entry
		while (lstPtr->next != NULL)
		{
			lstPtr = lstPtr->next;
		}
		lstPtr->next = newEntry;
	}
	
	// Done
	return 0;
}

static void
_cti_reapSession(cti_session_id_t sid)
{
	sessList_t *	lstPtr;
	sessList_t *	prePtr;
	
	// sanity check
	if (((lstPtr = _cti_my_sess) == NULL) || (sid <= 0))
		return;
	
	prePtr = _cti_my_sess;
	
	// this shouldn't happen, but doing so will prevent a segfault if the list gets corrupted
	while (lstPtr->this == NULL)
	{
		// if this is the only object in the list, then delete the entire list
		if ((lstPtr = lstPtr->next) == NULL)
		{
			_cti_my_sess = NULL;
			free(lstPtr);
			return;
		}
		// otherwise point _cti_my_sess to the lstPtr and free the corrupt entry
		_cti_my_sess = lstPtr;
		free(prePtr);
		prePtr = _cti_my_sess;
	}
	
	// we need to locate the position of the sessList_t object that we need to remove
	while (lstPtr->this->sid != sid)
	{
		prePtr = lstPtr;
		if ((lstPtr = lstPtr->next) == NULL)
		{
			// there are no more entries and we didn't find the sid
			return;
		}
	}
	
	// check to see if this was the first entry in the global _cti_my_sess list
	if (prePtr == lstPtr)
	{
		// point the global _cti_my_sess list to the next entry
		_cti_my_sess = lstPtr->next;
		// consume the session_t object for this entry in the list
		_cti_consumeSession(lstPtr->this);
		// free the list object
		free(lstPtr);
	} else
	{
		// we are at some point midway through the global _cti_my_sess list
		
		// point the previous entries next entry to the list pointers next entry
		// this bypasses the current list pointer
		prePtr->next = lstPtr->next;
		// consume the session_t object for this entry in the list
		_cti_consumeSession(lstPtr->this);
		// free the list object
		free(lstPtr);
	}
	
	// done
	return;
}

static void
_cti_consumeSession(session_t *sess)
{
	// sanity check
	if (sess == NULL)
		return;
		
	// free the basename of the manifest directory
	if (sess->stage_name != NULL)
		free(sess->stage_name);
		
	// free the toolpath
	if (sess->toolPath != NULL)
		free(sess->toolPath);
	
	// Eat the files list
	_cti_consumeStringList(sess->files, &_cti_consumeFileEntry);
	
	// nom nom the final session_t object
	free(sess);
}

static session_t *
_cti_findSession(cti_session_id_t sid)
{
	sessList_t *	lstPtr;
	
	// sanity check
	if (sid <= 0)
	{
		_cti_set_error("Invalid cti_session_id_t %d.", (int)sid);
	}
	
	if ((lstPtr = _cti_my_sess) == NULL)
	{
		_cti_set_error("cti_session_id_t %d does not exist.", (int)sid);
		return NULL;
	}
	
	// this shouldn't happen, but doing so will prevent a segfault if the list gets corrupted
	while (lstPtr->this == NULL)
	{
		// if this is the only object in the list, then delete the entire list
		if ((lstPtr = lstPtr->next) == NULL)
		{
			_cti_set_error("cti_session_id_t %d does not exist.", (int)sid);
			_cti_my_sess = NULL;
			free(lstPtr);
			return NULL;
		}
		// otherwise point _cti_my_sess to the lstPtr and free the corrupt entry
		_cti_my_sess = lstPtr;
	}
	
	// we need to locate the position of the sessList_t object that we are looking for
	while (lstPtr->this->sid != sid)
	{
		if ((lstPtr = lstPtr->next) == NULL)
		{
			// there are no more entries and we didn't find the sid
			_cti_set_error("cti_session_id_t %d does not exist.", (int)sid);
			return NULL;
		}
	}
	
	return lstPtr->this;
}

static session_t *
_cti_newSession(manifest_t *m_ptr)
{
	session_t *		this;
	stringEntry_t *	l_ptr = NULL;
	stringEntry_t *	o_ptr;
	fileEntry_t *	d_ptr;
	
	// sanity check
	if (m_ptr == NULL)
	{
		_cti_set_error("_cti_newSession: Invalid args.");
		return NULL;
	}
	
	// create the new session_t object
	if ((this = malloc(sizeof(session_t))) == (void *)0)
	{
		_cti_set_error("malloc failed.");
		return NULL;
	}
	memset(this, 0, sizeof(session_t));     // clear it to NULL
	
	// set the sid member
	// TODO: This could be made smarter by using a hash table instead of a revolving int we see now
	this->sid = _cti_next_sid++;
	
	// set the instance count
	// This gets used in the _cti_newManifest function to keep track of the number of
	// instances referencing this session. This is to prevent naming conflicts of 
	// the shipped tarball name.
	this->instCnt = 1;
	
	// set the stage name
	this->stage_name = strdup(m_ptr->stage_name);
	
	// create the stringList_t object
	if ((this->files = _cti_newStringList()) == NULL)
	{
		_cti_set_error("_cti_newStringList() failed.");
		_cti_consumeSession(this);
		return NULL;
	}
	
	// copy the files in the manifest over to the session.
	if ((l_ptr = _cti_getEntries(m_ptr->files)) != NULL)
	{
		// save for later
		o_ptr = l_ptr;
		while (l_ptr != NULL)
		{
			// copy the data entry for the new files list
			d_ptr = _cti_copyFileEntry(l_ptr->data);
			if (d_ptr == NULL)
			{
				// error occured, is already set
				_cti_consumeSession(this);
				_cti_cleanupEntries(o_ptr);
				return NULL;
			}
			// add this string to the files list
			if (_cti_addString(this->files, l_ptr->str, d_ptr))
			{
				// failed to save name into the list
				_cti_set_error("_cti_addString() failed.");
				_cti_consumeSession(this);
				return NULL;
			}
			// increment pointers
			d_ptr = NULL;
			l_ptr = l_ptr->next;
		}
		// cleanup
		_cti_cleanupEntries(o_ptr);
	}
	
	// save the new session_t object into the global session list
	if (_cti_addSession(this))
	{
		// error string already set
		_cti_consumeSession(this);
		return NULL;
	}
	
	return this;
}

static int
_cti_addManifest(manifest_t *manif)
{
	manifList_t *	newEntry;
	manifList_t *	lstPtr;
	
	// sanity check
	if (manif == NULL)
	{
		_cti_set_error("_cti_addManifest failed.");
		return 1;
	}
	
	// alloc space for the new list entry
	if ((newEntry = malloc(sizeof(manifList_t))) == (void *)0)
	{
		_cti_set_error("malloc failed.");
		return 1;
	}
	memset(newEntry, 0, sizeof(manifList_t));     // clear it to NULL
	
	// set the manif in the list entry
	newEntry->this = manif;
	
	// if _cti_my_manifs is null, this is the new head of the list
	if ((lstPtr = _cti_my_manifs) == NULL)
	{
		_cti_my_manifs = newEntry;
	} else
	{
		// we need to iterate through the list to find the open next entry
		while (lstPtr->next != NULL)
		{
			lstPtr = lstPtr->next;
		}
		lstPtr->next = newEntry;
	}
	
	return 0;
}

static void
_cti_reapManifest(cti_manifest_id_t mid)
{
	manifList_t *	lstPtr;
	manifList_t *	prePtr;
	
	// sanity check
	if (((lstPtr = _cti_my_manifs) == NULL) || (mid <= 0))
		return;
	
	prePtr = _cti_my_manifs;
	
	// this shouldn't happen, but doing so will prevent a segfault if the list gets corrupted
	while (lstPtr->this == NULL)
	{
		// if this is the only object in the list, then delete the entire list
		if ((lstPtr = lstPtr->next) == NULL)
		{
			_cti_my_manifs = NULL;
			free(lstPtr);
			return;
		}
		// otherwise point _cti_my_manifs to the lstPtr and free the corrupt entry
		_cti_my_manifs = lstPtr;
		free(prePtr);
		prePtr = _cti_my_manifs;
	}
	
	// we need to locate the position of the manifList_t object that we need to remove
	while (lstPtr->this->mid != mid)
	{
		prePtr = lstPtr;
		if ((lstPtr = lstPtr->next) == NULL)
		{
			// there are no more entries and we didn't find the mid
			return;
		}
	}
	
	// check to see if this was the first entry in the global _cti_my_manifs list
	if (prePtr == lstPtr)
	{
		// point the global _cti_my_manifs list to the next entry
		_cti_my_manifs = lstPtr->next;
		// consume the manifest_t object for this entry in the list
		_cti_consumeManifest(lstPtr->this);
		// free the list object
		free(lstPtr);
	} else
	{
		// we are at some point midway through the global _cti_my_manifs list
		
		// point the previous entries next entry to the list pointers next entry
		// this bypasses the current list pointer
		prePtr->next = lstPtr->next;
		// consume the manifest_t object for this entry in the list
		_cti_consumeManifest(lstPtr->this);
		// free the list object
		free(lstPtr);
	}
	
	// done
	return;
}

static void
_cti_consumeManifest(manifest_t *entry)
{
	// sanity check
	if (entry == NULL)
		return;
		
	// free the basename of the manifest directory
	if (entry->stage_name != NULL)
		free(entry->stage_name);
	
	// eat the string list
	_cti_consumeStringList(entry->files, &_cti_consumeFileEntry);
	
	// nom nom the final manifest_t object
	free(entry);
}

static manifest_t *
_cti_findManifest(cti_manifest_id_t mid)
{
	manifList_t *	lstPtr;
	
	// sanity check
	if (mid <= 0)
	{
		_cti_set_error("Invalid cti_manifest_id_t %d.", (int)mid);
		return NULL;
	}
	
	if ((lstPtr = _cti_my_manifs) == NULL)
	{
		_cti_set_error("cti_manifest_id_t %d does not exist.", (int)mid);
		return NULL;
	}
	
	// this shouldn't happen, but doing so will prevent a segfault if the list gets corrupted
	while (lstPtr->this == NULL)
	{
		// if this is the only object in the list, then delete the entire list
		if ((lstPtr = lstPtr->next) == NULL)
		{
			_cti_set_error("cti_manifest_id_t %d does not exist.", (int)mid);
			_cti_my_manifs = NULL;
			free(lstPtr);
			return NULL;
		}
		// otherwise point _cti_my_manifs to the lstPtr and free the corrupt entry
		_cti_my_manifs = lstPtr;
	}
	
	// we need to locate the position of the manifList_t object that we are looking for
	while (lstPtr->this->mid != mid)
	{
		if ((lstPtr = lstPtr->next) == NULL)
		{
			// there are no more entries and we didn't find the mid
			_cti_set_error("cti_manifest_id_t %d does not exist.", (int)mid);
			return NULL;
		}
	}
	
	return lstPtr->this;
}

static manifest_t *
_cti_newManifest(cti_session_id_t sid)
{
	manifest_t *	this;
	session_t *		s_ptr;
	stringEntry_t *	l_ptr;
	stringEntry_t *	o_ptr;
	fileEntry_t *	f_ptr;
	
	// create the new manifest_t object
	if ((this = malloc(sizeof(manifest_t))) == (void *)0)
	{
		_cti_set_error("malloc failed.");
		return NULL;
	}
	memset(this, 0, sizeof(manifest_t));     // clear it to NULL
	
	// set the mid member
	// TODO: This could be made smarter by using a hash table instead of a revolving int we see now
	this->mid = _cti_next_mid++;
	
	// set the provided sid in the manifest
	this->sid = sid;
	
	// create the stringList_t object
	if ((this->files = _cti_newStringList()) == NULL)
	{
		_cti_set_error("_cti_newStringList() failed.");
		_cti_consumeManifest(this);
		return NULL;
	}
	
	// Check to see if we need to add session info. Note that the files list
	// will guarantee uniqueness. We simply need to add the file entry
	// to this list to prevent it from being shipped.
	if (sid != 0)
	{
		if ((s_ptr = _cti_findSession(sid)) == NULL)
		{
			// We failed to find the session for the sid
			// error string already set
			_cti_consumeManifest(this);
			return NULL;
		}
		
		// increment the instance count in the sid
		s_ptr->instCnt++;
		
		// set the instance number based on the instCnt in the sid
		this->inst = s_ptr->instCnt;
		
		// copy the information from the session to the manifest
		this->stage_name = strdup(s_ptr->stage_name);
		
		// copy all of the fileEntry to the manifest files list
		if ((l_ptr = _cti_getEntries(s_ptr->files)) != NULL)
		{
			// save for later
			o_ptr = l_ptr;
			while (l_ptr != NULL)
			{
				// copy the data entry for the new list
				f_ptr = _cti_copyFileEntry(l_ptr->data);
				if (f_ptr == NULL)
				{
					// error already set
					_cti_consumeManifest(this);
					_cti_cleanupEntries(o_ptr);
					return NULL;
				}
				
				// set present to 1 since this was part of the session, and we are
				// guaranteed to have shipped all files at this point
				f_ptr->present = 1;		// these files were already transfered and checked for validity
				
				// add this string to the manifest files list
				if (_cti_addString(this->files, l_ptr->str, f_ptr))
				{
					// failed to save name into the list
					_cti_set_error("_cti_addString() failed.");
					_cti_consumeManifest(this);
					_cti_cleanupEntries(o_ptr);
					return NULL;
				}
				// increment pointers
				f_ptr = NULL;
				l_ptr = l_ptr->next;
			}
			// cleanup
			_cti_cleanupEntries(o_ptr);
		}
	} else
	{
		// this is the first instance of the session that will be created upon
		// shipping this manifest
		this->inst = 1;
	}
	
	// save the new manifest_t object into the global manifest list
	if (_cti_addManifest(this))
	{
		// error string already set
		_cti_consumeManifest(this);
		return NULL;
	}
	
	return this;
}

static int
_cti_addManifestToSession(manifest_t *m_ptr, session_t *s_ptr)
{
	stringEntry_t *	l_ptr;	// list of entries
	stringEntry_t *	o_ptr;	// used to save the original pointer to free it later
	fileEntry_t *	f_ptr;
	
	// sanity check
	if (m_ptr == NULL || s_ptr == NULL)
	{
		_cti_set_error("_cti_addManifestToSession: Invalid args.");
		return 1;
	}
	
	// copy all of the fileEntry to the session files list
	if ((l_ptr = _cti_getEntries(m_ptr->files)) != NULL)
	{
		// save for later
		o_ptr = l_ptr;
		while (l_ptr != NULL)
		{
			// check to see if this entry is already in the list
			f_ptr = _cti_lookupValue(s_ptr->files, l_ptr->str);
			if (f_ptr)
			{
				// string already in the list, we need to merge
				if (_cti_mergeFileEntry(f_ptr, l_ptr->data))
				{
					// error already set
					_cti_cleanupEntries(o_ptr);
					return 1;
				}
			} else
			{
				// not in list, so add the chain
				
				// copy the data entry
				f_ptr = _cti_copyFileEntry(l_ptr->data);
				if (f_ptr == NULL)
				{
					// error already set
					_cti_cleanupEntries(o_ptr);
					return 1;
				}
				
				if (_cti_addString(s_ptr->files, l_ptr->str, f_ptr))
				{
					// failed to save name into the list
					_cti_set_error("_cti_addString() failed.");
					_cti_cleanupEntries(o_ptr);
					_cti_consumeFileEntry(f_ptr);
					return 1;
				}
			}
			// increment pointers
			l_ptr = l_ptr->next;
		}
		// cleanup
		_cti_cleanupEntries(o_ptr);
	}
	
	return 0;
}

static void
_cti_addSessionToApp(appEntry_t *app_ptr, cti_session_id_t sid)
{
	sessMgr_t *	sess_obj;
	int *		new_ids;

	// sanity check
	if (app_ptr == NULL || sid == 0)
		return;
		
	if (app_ptr->_transferObj == NULL)
	{
		// create a new sessMgr_t for this app entry
		if ((sess_obj = malloc(sizeof(sessMgr_t))) == (void *)0)
		{
			// malloc failed
			return;
		}
		memset(sess_obj, 0, sizeof(sessMgr_t));
		
		// setup the sess_obj
		if ((sess_obj->session_ids = malloc(SESS_INC_SIZE * sizeof(int))) == (void *)0)
		{
			// malloc failed
			free(sess_obj);
			return;
		}
		memset(sess_obj->session_ids, 0, SESS_INC_SIZE * sizeof(int));
		
		sess_obj->len = SESS_INC_SIZE;
		
		// set the object in the app_ptr
		_cti_setTransferObj(app_ptr, (void *)sess_obj, _cti_destroyAppSess);
	} else
	{
		sess_obj = (sessMgr_t *)app_ptr->_transferObj;
		
		// ensure there is space, otherwise realloc
		if (sess_obj->numSess == sess_obj->len)
		{
			// need to realloc
			if ((new_ids = realloc(sess_obj->session_ids, (sess_obj->len + SESS_INC_SIZE) * sizeof(int))) == (void *)0)
			{
				// malloc failed
				return;
			}
			memset(&new_ids[sess_obj->len], 0, SESS_INC_SIZE * sizeof(int));
			
			sess_obj->session_ids = new_ids;
			sess_obj->len += SESS_INC_SIZE;
		}
	}
	
	// Put the sid into the session_ids array
	sess_obj->session_ids[sess_obj->numSess++] = sid;
}

/* API defined functions start here */

void
_cti_destroyAppSess(void *obj)
{
	sessMgr_t *	sess_obj = (sessMgr_t *)obj;
	int i;

	// sanity check
	if (sess_obj == NULL)
		return;
		
	for (i=0; i < sess_obj->numSess; ++i)
	{
		_cti_reapSession(sess_obj->session_ids[i]);
	}
	
	free(sess_obj->session_ids);
	free(sess_obj);
}

cti_manifest_id_t
cti_createNewManifest(cti_session_id_t sid)
{
	manifest_t *	m_ptr = NULL;
	
	if ((m_ptr = _cti_newManifest(sid)) == NULL)
	{
		// error string already set
		return 0;
	}
		
	return m_ptr->mid;
}

void
cti_destroyManifest(cti_manifest_id_t mid)
{
	_cti_reapManifest(mid);
}

int
cti_addManifestBinary(cti_manifest_id_t mid, const char *fstr)
{
	manifest_t *	m_ptr;		// pointer to the manifest_t object associated with the cti_manifest_id_t argument
	char *			fullname;	// full path name of the binary to add to the manifest
	char *			realname;	// realname (lacking path info) of the file
	fileEntry_t *	f_ptr;		// pointer to file entry
	int				rtn;
	char **			lib_array;	// the returned list of strings containing the required libraries by the executable
	char **			tmp;		// temporary pointer used to iterate through lists of strings
	
	// sanity check
	if (mid <= 0)
	{
		_cti_set_error("Invalid cti_manifest_id_t %d.", (int)mid);
		return 1;
	}
	
	if (fstr == NULL)
	{
		_cti_set_error("cti_addManifestBinary had null fstr.");
		return 1;
	}
		
	// Find the manifest entry in the global manifest list for the mid
	if ((m_ptr = _cti_findManifest(mid)) == NULL)
	{
		// We failed to find the manifest for the mid
		// error string already set
		return 1;
	}
	
	// first we should ensure that the binary exists and convert it to its fullpath name
	if ((fullname = _cti_pathFind(fstr, NULL)) == NULL)
	{
		_cti_set_error("Could not locate the specified file in PATH.");
		return 1;
	}
	
	// next just grab the real name (without path information) of the executable
	if ((realname = _cti_pathToName(fullname)) == NULL)
	{
		_cti_set_error("Could not convert the fullname to realname.");
		free(fullname);
		return 1;
	}
	
	// search the string list for this filename
	f_ptr = _cti_lookupValue(m_ptr->files, realname);
	if (f_ptr)
	{
		// realname is in string list, so try to chain this entry
		rtn = _cti_chainFileEntry(f_ptr, realname, EXEC_ENTRY, fullname, 0);
		if (rtn < 0)
		{
			// error already set
			free(fullname);
			free(realname);
			return 1;
		}
		if (rtn == 1)
		{
			// file locations match. No conflict, return success since the file is already
			// in the manifest
			free(fullname);
			free(realname);
			return 0;
		}
		
		// if we get here, then we need to add the libraries which is done below.
	} else
	{
		// not found in list, so this is a unique file name

		// create new entry
		if ((f_ptr = _cti_newFileEntry()) == NULL)
		{
			// error already set
			free(fullname);
			free(realname);
			return 1;
		}
		
		// set data entries
		f_ptr->type = EXEC_ENTRY;
		f_ptr->loc  = fullname;	// this will get free'ed later on
		f_ptr->present = 0;
		f_ptr->next = NULL;
		
		// add the string to the string list based on the realname of the file.
		// we want to avoid conflicts on the realname only.
		if (_cti_addString(m_ptr->files, realname, f_ptr))
		{
			// failed to save name into the list
			_cti_set_error("_cti_addString() failed.");
			free(realname);
			_cti_consumeFileEntry(f_ptr);
			return 1;
		}
		
		// done, adding of libraries is done below
	}
	
	// call the ld_val interface to determine if this executable has any dso requirements
	lib_array = _cti_ld_val(fullname);
	
	// If this executable has dso requirements. We need to add them to the manifest
	tmp = lib_array;
	// ensure the are actual entries before dereferencing tmp
	if (tmp != NULL)
	{
		while (*tmp != NULL)
		{
			if (cti_addManifestLibrary(m_ptr->mid, *tmp))
			{
				// if we return with non-zero status, catastrophic failure occured
				// error string already set
				return 1;
			}
			// free this tmp value, we are done with it
			free(*tmp++);
		}
		// free the final lib_array
		free(lib_array);
	}
	
	// cleanup
	free(realname);
	
	return 0;
}

int
cti_addManifestLibrary(cti_manifest_id_t mid, const char *fstr)
{
	manifest_t *	m_ptr;		// pointer to the manifest_t object associated with the cti_manifest_id_t argument
	char *			fullname;	// full path name of the library to add to the manifest
	char *			realname;	// realname (lacking path info) of the library
	fileEntry_t *	f_ptr;		// pointer to file entry
	int				rtn;
	
	// sanity check
	if (mid <= 0)
	{
		_cti_set_error("Invalid cti_manifest_id_t %d.", (int)mid);
		return 1;
	}
	
	if (fstr == NULL)
	{
		_cti_set_error("cti_addManifestLibrary had null fstr.");
		return 1;
	}
	
	// Find the manifest entry in the global manifest list for the mid
	if ((m_ptr = _cti_findManifest(mid)) == NULL)
	{
		// We failed to find the manifest for the mid
		// error string already set
		return 1;
	}
	
	// first we should ensure that the library exists and convert it to its fullpath name
	if ((fullname = _cti_libFind(fstr)) == NULL)
	{
		_cti_set_error("Could not locate %s in LD_LIBRARY_PATH or system location.", fstr);
		return 1;
	}
	
	// next just grab the real name (without path information) of the library
	if ((realname = _cti_pathToName(fullname)) == NULL)
	{
		_cti_set_error("Could not convert the fullname to realname.");
		free(fullname);
		return 1;
	}
	
	// TODO: We need to create a way to ship conflicting libraries. Since most libraries are sym links to
	// their proper version, name collisions are possible. In the future, the launcher should be able to handle
	// this by pointing its LD_LIBRARY_PATH to a custom directory containing the conflicting lib. Don't actually
	// implement this until the need arises.
	
	// search the string list for this filename
	f_ptr = _cti_lookupValue(m_ptr->files, realname);
	if (f_ptr)
	{
		// realname is in string list, so try to chain this entry
		rtn = _cti_chainFileEntry(f_ptr, realname, LIB_ENTRY, fullname, 0);
		if (rtn < 0)
		{
			// error already set
			free(fullname);
			free(realname);
			return 1;
		}
		if (rtn == 1)
		{
			// file locations match. No conflict, return success since the file is already
			// in the manifest
			free(fullname);
			free(realname);
			return 0;
		}
		
	} else
	{
		// not found in list, so this is a unique file name
		
		// create new entry
		if ((f_ptr = _cti_newFileEntry()) == NULL)
		{
			// error already set
			free(fullname);
			free(realname);
			return 1;
		}
		
		// set data entries
		f_ptr->type = LIB_ENTRY;
		f_ptr->loc  = fullname;	// this will get free'ed later on
		f_ptr->present = 0;
		f_ptr->next = NULL;
		
		// add the string to the string list based on the realname of the file.
		// we want to avoid conflicts on the realname only.
		if (_cti_addString(m_ptr->files, realname, f_ptr))
		{
			// failed to save name into the list
			_cti_set_error("_cti_addString() failed.");
			free(realname);
			_cti_consumeFileEntry(f_ptr);
			return 1;
		}
	}
	
	// cleanup
	free(realname);
	
	return 0;
}

// TODO: This should be able to merge two directories with the same name but different
// contents. Right now this doesn't happen. The directory can only be added once. This
// is probably not desired.
int
cti_addManifestLibDir(cti_manifest_id_t mid, const char *fstr)
{
	manifest_t *	m_ptr;		// pointer to the manifest_t object associated with the cti_manifest_id_t argument
	struct stat 	statbuf;
	char *			fullname;	// full path name of the library directory to add to the manifest
	char *			realname;	// realname (lacking path info) of the library directory
	fileEntry_t *	f_ptr;		// pointer to file entry
	int				rtn;		// rtn value
	
	// sanity check
	if (mid <= 0)
	{
		_cti_set_error("cti_addManifestLibDir: Invalid cti_manifest_id_t %d.", (int)mid);
		return 1;
	}
	
	if (fstr == NULL)
	{
		_cti_set_error("cti_addManifestLibDir: Invalid args.");
		return 1;
	}
	
	// Find the manifest entry in the global manifest list for the mid
	if ((m_ptr = _cti_findManifest(mid)) == NULL)
	{
		// We failed to find the manifest for the mid
		// error string already set
		return 1;
	}
	
	// Ensure the provided directory exists
	if (stat(fstr, &statbuf)) 
	{
		/* can't access file */
		_cti_set_error("cti_addManifestLibDir: Provided path %s does not exist.", fstr);
		return 1;
	}

	// ensure the file is a directory
	if (!S_ISDIR(statbuf.st_mode))
	{
		/* file is not a directory */
		_cti_set_error("cti_addManifestLibDir: Provided path %s is not a directory.", fstr);
		return 1;
	}
	
	// convert the path to its real fullname (i.e. resolve symlinks and get rid of special chars)
	if ((fullname = realpath(fstr, NULL)) == NULL)
	{
		_cti_set_error("cti_addManifestLibDir: realpath failed.");
		return 1;
	}

	// next just grab the real name (without path information) of the library directory
	if ((realname = _cti_pathToName(fullname)) == NULL)
	{
		_cti_set_error("cti_addManifestLibDir: Could not convert the fullname to realname.");
		free(fullname);
		return 1;
	}
	
	// search the string list for this filename
	f_ptr = _cti_lookupValue(m_ptr->files, realname);
	if (f_ptr)
	{
		// realname is in string list, so try to chain this entry
		rtn = _cti_chainFileEntry(f_ptr, realname, LIBDIR_ENTRY, fullname, 0);
		if (rtn < 0)
		{
			// error already set
			free(fullname);
			free(realname);
			return 1;
		}
		if (rtn == 1)
		{
			// file locations match. No conflict, return success since the file is already
			// in the manifest
			free(fullname);
			free(realname);
			return 0;
		}
		
	} else
	{
		// not found in list, so this is a unique file name
		
		// create new entry
		if ((f_ptr = _cti_newFileEntry()) == NULL)
		{
			// error already set
			free(fullname);
			free(realname);
			return 1;
		}
		
		// set data entries
		f_ptr->type = LIBDIR_ENTRY;
		f_ptr->loc  = fullname;	// this will get free'ed later on
		f_ptr->present = 0;
		f_ptr->next = NULL;
		
		// add the string to the string list based on the realname of the file.
		// we want to avoid conflicts on the realname only.
		if (_cti_addString(m_ptr->files, realname, f_ptr))
		{
			// failed to save name into the list
			_cti_set_error("_cti_addString() failed.");
			free(realname);
			_cti_consumeFileEntry(f_ptr);
			return 1;
		}
	}
	
	// cleanup
	free(realname);
	
	return 0;
}

int
cti_addManifestFile(cti_manifest_id_t mid, const char *fstr)
{
	manifest_t *	m_ptr;		// pointer to the manifest_t object associated with the cti_manifest_id_t argument
	char *			fullname;	// full path name of the file to add to the manifest
	char *			realname;	// realname (lacking path info) of the file
	fileEntry_t *	f_ptr;		// pointer to file entry
	int				rtn;		// rtn value
	
	// sanity check
	if (mid <= 0)
	{
		_cti_set_error("Invalid cti_manifest_id_t %d.", (int)mid);
		return 1;
	}
	
	if (fstr == NULL)
	{
		_cti_set_error("cti_addManifestFile had null fstr.");
		return 1;
	}
		
	// Find the manifest entry in the global manifest list for the mid
	if ((m_ptr = _cti_findManifest(mid)) == NULL)
	{
		// We failed to find the manifest for the mid
		// error string already set
		return 1;
	}
	
	// first we should ensure that the file exists and convert it to its fullpath name
	if ((fullname = _cti_pathFind(fstr, NULL)) == NULL)
	{
		_cti_set_error("Could not locate the specified file in PATH.");
		return 1;
	}
	
	// next just grab the real name (without path information) of the library
	if ((realname = _cti_pathToName(fullname)) == NULL)
	{
		_cti_set_error("Could not convert the fullname to realname.");
		free(fullname);
		return 1;
	}
	
	// search the string list for this filename
	f_ptr = _cti_lookupValue(m_ptr->files, realname);
	if (f_ptr)
	{
		// realname is in string list, so try to chain this entry
		rtn = _cti_chainFileEntry(f_ptr, realname, FILE_ENTRY, fullname, 0);
		if (rtn < 0)
		{
			// error already set
			free(fullname);
			free(realname);
			return 1;
		}
		if (rtn == 1)
		{
			// file locations match. No conflict, return success since the file is already
			// in the manifest
			free(fullname);
			free(realname);
			return 0;
		}
		
	} else
	{
		// not found in list, so this is a unique file name
		
		// create new entry
		if ((f_ptr = _cti_newFileEntry()) == NULL)
		{
			// error already set
			free(fullname);
			free(realname);
			return 1;
		}
		
		// set data entries
		f_ptr->type = FILE_ENTRY;
		f_ptr->loc  = fullname;	// this will get free'ed later on
		f_ptr->present = 0;
		f_ptr->next = NULL;
		
		// add the string to the string list based on the realname of the file.
		// we want to avoid conflicts on the realname only.
		if (_cti_addString(m_ptr->files, realname, f_ptr))
		{
			// failed to save name into the list
			_cti_set_error("_cti_addString() failed.");
			free(realname);
			_cti_consumeFileEntry(f_ptr);
			return 1;
		}
	}
	
	// cleanup
	free(realname);
	
	return 0;
}

static int
_cti_addDirToArchive(struct archive *a, struct archive_entry *entry, const char *d_loc)
{
	struct timespec		tv;
	int 				r;
	
	// sanity
	if (a == NULL || entry == NULL || d_loc == NULL)
	{
		_cti_set_error("_cti_addDirToArchive: Bad args.");
		return 1;
	}
	
	// get the current time
	clock_gettime(CLOCK_REALTIME, &tv);
	
	// clear archive header for this use
	archive_entry_clear(entry);
	
	// setup the archive header
	archive_entry_set_pathname(entry, d_loc);
	archive_entry_set_filetype(entry, AE_IFDIR);
	archive_entry_set_size(entry, 0);
	archive_entry_set_perm(entry, S_IRWXU);
	archive_entry_set_atime(entry, tv.tv_sec, tv.tv_nsec);
	archive_entry_set_birthtime(entry, tv.tv_sec, tv.tv_nsec);
	archive_entry_set_ctime(entry, tv.tv_sec, tv.tv_nsec);
	archive_entry_set_mtime(entry, tv.tv_sec, tv.tv_nsec);
	while ((r = archive_write_header(a, entry)) == ARCHIVE_RETRY);
	if (r == ARCHIVE_FATAL)
	{
		_cti_set_error("_cti_addDirToArchive: %s", archive_error_string(a));
		return 1;
	}
	
	return 0;
}

static int
_cti_copyFileToArchive(struct archive *a, struct archive_entry *entry, const char *f_loc, const char *a_loc, char *read_buf)
{
	struct stat		st;
	int				fd,r;
	ssize_t			len, a_len;
	
	// sanity
	if (a == NULL || entry == NULL || f_loc == NULL || a_loc == NULL || read_buf == NULL)
	{
		_cti_set_error("_cti_copyFileToArchive: Bad args.");
		return 1;
	}
	
	// clear archive header for this use
	archive_entry_clear(entry);
	
	// stat the file
	if (stat(f_loc, &st))
	{
		_cti_set_error("_cti_copyFileToArchive: stat() %s", strerror(errno));
		return 1;
	}
	
	// Ensure this file is of a supported type
	if (S_ISDIR(st.st_mode))
	{
		// This is an entire directory that needs to be added, we need to recurse
		// through each file in the directory and add it.
		DIR *				dir;
		struct dirent *		d;
		char 				sub_f_loc[PATH_MAX];
		char 				sub_a_loc[PATH_MAX];
		
		// Open the directory
		if ((dir = opendir(f_loc)) == NULL)
		{
			_cti_set_error("_cti_copyFileToArchive: opendir() %s", strerror(errno));
			return 1;
		}
		
		// setup the archive header for the top level directory
		archive_entry_copy_stat(entry, &st);
		archive_entry_set_pathname(entry, a_loc);
		while ((r = archive_write_header(a, entry)) == ARCHIVE_RETRY);
		if (r == ARCHIVE_FATAL)
		{
			_cti_set_error("_cti_copyFileToArchive: %s", archive_error_string(a));
			closedir(dir);
			return 1;
		}
		
		// Recurse through each file in the reference directory
		errno = 0;
		while ((d = readdir(dir)) != NULL)
		{
			// ensure this isn't the . or .. file
			switch (strlen(d->d_name))
			{
				case 1:
					if (d->d_name[0] == '.')
					{
						// continue to the outer while loop
						continue;
					}
					break;
				
				case 2:
					if (strcmp(d->d_name, "..") == 0)
					{
						// continue to the outer while loop
						continue;
					}
					break;
			
				default:
					break;
			}
		
			// Create the path strings to this file
			if (snprintf(sub_f_loc, PATH_MAX, "%s/%s", f_loc, d->d_name) <= 0)
			{
				_cti_set_error("_cti_copyFileToArchive: snprintf failed.");
				closedir(dir);
				return 1;
			}
			if (snprintf(sub_a_loc, PATH_MAX, "%s/%s", a_loc, d->d_name) <= 0)
			{
				_cti_set_error("_cti_copyFileToArchive: snprintf failed.");
				closedir(dir);
				return 1;
			}
			
			// recursively call copy to archive function
			if (_cti_copyFileToArchive(a, entry, sub_f_loc, sub_a_loc, read_buf))
			{
				// error already set
				closedir(dir);
				return 1;
			}
			
			// cleanup
			errno = 0;
		}
		
		// check for failure on readdir()
		if (errno != 0)
		{
			// readdir failed
			_cti_set_error("_cti_copyFileToArchive: readdir() %s", strerror(errno));
			closedir(dir);
			return 1;
		}
		
		// cleanup
		closedir(dir);
		// At this point we are finished, the directory has been copied to the archive.
		return 0;
		
	} else if (!S_ISREG(st.st_mode))
	{
		// This is an unsuported file and should not be added to the manifest
		_cti_set_error("_cti_copyFileToArchive: Invalid file type.");
		return 1;
	}
	
	// open the file
	if ((fd = open(f_loc, O_RDONLY)) == -1)
	{
		_cti_set_error("_cti_copyFileToArchive: open() %s", strerror(errno));
		return 1;
	}
		
	// setup the archive header
	archive_entry_copy_stat(entry, &st);
	archive_entry_set_pathname(entry, a_loc);
	while ((r = archive_write_header(a, entry)) == ARCHIVE_RETRY);
	if (r == ARCHIVE_FATAL)
	{
		_cti_set_error("_cti_copyFileToArchive: %s", archive_error_string(a));
		close(fd);
		return 1;
	}
	
	// write the data
	len = read(fd, read_buf, CTI_BLOCK_SIZE);
	if (len < 0)
	{
		_cti_set_error("_cti_copyFileToArchive: read() %s", strerror(errno));
		close(fd);
		return 1;
	}
	while (len > 0)
	{
		a_len = archive_write_data(a, read_buf, len);
		if (a_len < 0)
		{
			_cti_set_error("_cti_copyFileToArchive: %s", archive_error_string(a));
			close(fd);
			return 1;
		}
		if (a_len != len)
		{
			_cti_set_error("_cti_copyFileToArchive: archive_write_data len mismatch!");
			close(fd);
			return 1;
		}
		len = read(fd, read_buf, CTI_BLOCK_SIZE);
		if (len < 0)
		{
			_cti_set_error("_cti_copyFileToArchive: read() %s", strerror(errno));
			close(fd);
			return 1;
		}
	}
	
	// cleanup
	close(fd);
	return 0;
}

static void
_cti_transfer_doCleanup(void)
{
	const char *		cfg_dir;
	DIR *				dir;
	struct dirent *		d;
	char *				stage_name;
	char *				p;
	
	if (!_cti_doCleanup)
		return;
	
	// Get the configuration directory
	if ((cfg_dir = _cti_getCfgDir()) == NULL)
	{
		return;
	}
	
	// Open the directory
	if ((dir = opendir(cfg_dir)) == NULL)
	{
		return;
	}
	
	// create the pattern to look for - note that the '.' is added since the
	// file name will be hidden.
	if (asprintf(&stage_name, ".%s", DEFAULT_STAGE_DIR) <= 0)
	{
		closedir(dir);
		return;
	}
	// point at the first 'X' character at the end of the string
	if ((p = strrchr(stage_name, 'X')) == NULL)
	{
		closedir(dir);
		free(stage_name);
		return;
	}
	while ((p - stage_name) > 0 && *(p-1) == 'X')
	{
		--p;
	}
	// Set this to null terminator
	*p = '\0';
	
	// Recurse through each file in the reference directory
	while ((d = readdir(dir)) != NULL)
	{
		// ensure this isn't the . or .. file
		switch (strlen(d->d_name))
		{
			case 1:
				if (d->d_name[0] == '.')
				{
					// continue to the outer while loop
					continue;
				}
				break;
				
			case 2:
				if (strcmp(d->d_name, "..") == 0)
				{
					// continue to the outer while loop
					continue;
				}
				break;
			
			default:
				break;
		}
		
		// check this name against the stage_name string
		if (strncmp(d->d_name, stage_name, strlen(stage_name)) == 0)
		{
			FILE *	hfp;
			pid_t	pid;
			char *	h_path;
			char *	n_path;
			
			// create path to hidden file
			if (asprintf(&h_path, "%s/%s", cfg_dir, d->d_name) <= 0)
			{
				continue;
			}
		
			// pattern matches, we need to try removing this file
			if ((hfp = fopen(h_path, "r")) == NULL)
			{
				free(h_path);
				continue;
			}
			
			// read the pid from the file
			if (fread(&pid, sizeof(pid), 1, hfp) != 1)
			{
				free(h_path);
				fclose(hfp);
				continue;
			}
			
			fclose(hfp);
			
			// ping the process
			if (kill(pid, 0) == 0)
			{
				// process is still alive
				free(h_path);
				continue;
			}
			
			// process is dead, we need to remove the tarball
			if (asprintf(&n_path, "%s/%s", cfg_dir, d->d_name+1) <= 0)
			{
				free(h_path);
				continue;
			}
			
			unlink(n_path);
			free(n_path);
			
			// remove the hidden file
			unlink(h_path);
			free(h_path);
		}
	}
	
	// cleanup
	closedir(dir);
	free(stage_name);
	_cti_doCleanup = false;
}
	
static int
_cti_packageManifestAndShip(appEntry_t *app_ptr, manifest_t *m_ptr)
{
	const char *			cfg_dir = NULL;		// tmp directory
	char *					stage_name = NULL;	// staging path
	char *					stage_name_env;
	char *					bin_path = NULL;
	char *					lib_path = NULL;
	char *					tmp_path = NULL;
	const char * const *	wlm_files;
	stringEntry_t *			l_ptr;
	stringEntry_t *			o_ptr = NULL;
	fileEntry_t *			f_ptr;
	char *					tar_name = NULL;
	char *					hidden_tar_name = NULL;
	FILE *					hfp = NULL;
	pid_t					pid;
	struct archive *		a = NULL;
	struct archive_entry *	entry = NULL;
	char *					read_buf = NULL;
	char					path_buf[PATH_MAX];
	cti_signals_t *			signals = NULL;		// BUG 819725
	bool					has_files = false;	// tracks if any files need to be shipped
	int						rtn = 0;			// return value
	
	// sanity check
	if (app_ptr == NULL || m_ptr == NULL)
	{
		_cti_set_error("_cti_packageManifestAndShip: invalid args.");
		return 1;
	}
	
	// ensure there are entries in the string list, otherwise return the empty manifest error
	if (_cti_lenStringList(m_ptr->files) == 0)
	{
		return 2;
	}
	
	// Get the configuration directory
	if ((cfg_dir = _cti_getCfgDir()) == NULL)
	{
		// error already set
		goto packageManifestAndShip_error;
	}
	
	// try to cleanup
	_cti_transfer_doCleanup();
	
	// Check the manifest to see if it already has a stage_name set, if so this 
	// is part of an existing session and we should use the same name
	if (m_ptr->stage_name == NULL)
	{
		// check to see if the caller set a staging directory name, otherwise create a unique one for them
		if ((stage_name_env = getenv(DAEMON_STAGE_VAR)) == NULL)
		{
			char *	rand_char;
			
			// take the default action
			if (asprintf(&stage_name, "%s", DEFAULT_STAGE_DIR) <= 0)
			{
				_cti_set_error("_cti_packageManifestAndShip: asprintf failed.");
				goto packageManifestAndShip_error;
			}
			
			// Point at the last 'X' in the string
			if ((rand_char = strrchr(stage_name, 'X')) == NULL)
			{
				_cti_set_error("_cti_packageManifestAndShip: Bad stage_name string!");
				goto packageManifestAndShip_error;
			}
			
			// set state or init the PRNG if we haven't already done so.
			if (_cti_r_init)
			{
				// set the PRNG state
				if (setstate((char *)_cti_r_state) == NULL)
				{
					_cti_set_error("_cti_packageManifestAndShip: setstate failed.");
					goto packageManifestAndShip_error;
				}
			} else
			{
				// We need to generate a good seed to avoid collisions. Since this
				// library can be used by automated tests, it is vital to have a
				// good seed.
				struct timespec		tv;
				unsigned int		pval;
				unsigned int		seed;
				
				// get the current time from epoch with nanoseconds
				if (clock_gettime(CLOCK_REALTIME, &tv))
				{
					_cti_set_error("_cti_packageManifestAndShip: clock_gettime failed.");
					goto packageManifestAndShip_error;
				}
				
				// generate an appropriate value from the pid, we shift this to
				// the upper 16 bits of the int. This should avoid problems with
				// collisions due to slight variations in nano time and adding in
				// pid offsets.
				pval = (unsigned int)getpid() << ((sizeof(unsigned int) * CHAR_BIT) - 16);
				
				// Generate the seed. This is not crypto safe, but should have enough
				// entropy to avoid the case where two procs are started at the same
				// time that use this interface.
				seed = (tv.tv_sec ^ tv.tv_nsec) + pval;
				
				// init the state
				initstate(seed, (char *)_cti_r_state, sizeof(_cti_r_state));
				
				// set init to true
				_cti_r_init = true;
			}
			
			// now start replacing the 'X' characters in the stage_name string with
			// randomness
			do {
				unsigned int oset;
				// Generate a random offset into the array. This is random() modded 
				// with the number of elements in the array.
				oset = random() % (sizeof(_cti_valid_char)/sizeof(_cti_valid_char[0]));
				// assing this char
				*rand_char = _cti_valid_char[oset];
			} while (*(--rand_char) == 'X');
			
		} else
		{
			// use the user defined directory
			if (asprintf(&stage_name, "%s", stage_name_env) <= 0)
			{
				_cti_set_error("_cti_packageManifestAndShip: asprintf failed.");
				goto packageManifestAndShip_error;
			}
		}
		
		// set the stage name into the manifest obj - This gets cleaned up
		// later on
		m_ptr->stage_name = stage_name;
		stage_name = NULL;
	}
	
	// now create the required subdirectory strings
	if (asprintf(&bin_path, "%s/bin", m_ptr->stage_name) <= 0)
	{
		_cti_set_error("_cti_packageManifestAndShip: asprintf failed.");
		goto packageManifestAndShip_error;
	}
	if (asprintf(&lib_path, "%s/lib", m_ptr->stage_name) <= 0)
	{
		_cti_set_error("_cti_packageManifestAndShip: asprintf failed.");
		goto packageManifestAndShip_error;
	}
	if (asprintf(&tmp_path, "%s/tmp", m_ptr->stage_name) <= 0)
	{
		_cti_set_error("_cti_packageManifestAndShip: asprintf failed.");
		goto packageManifestAndShip_error;
	}
	
	// Add extra files needed by the WLM now that it is known. We only do this
	// if it is the first instance, otherwise the extra files will have already
	// been shipped.
	if (m_ptr->inst == 1)
	{
		// grab extra binaries
		if ((wlm_files = app_ptr->wlmProto->wlm_extraBinaries(app_ptr->_wlmObj)) != NULL)
		{
			while (*wlm_files != NULL)
			{
				if (cti_addManifestBinary(m_ptr->mid, *wlm_files))
				{
					// Failed to add the binary to the manifest - catastrophic failure
					goto packageManifestAndShip_error;
				}
				++wlm_files;
			}
		}
		
		// grab extra libraries
		if ((wlm_files = app_ptr->wlmProto->wlm_extraLibraries(app_ptr->_wlmObj)) != NULL)
		{
			while (*wlm_files != NULL)
			{
				if (cti_addManifestLibrary(m_ptr->mid, *wlm_files))
				{
					// Failed to add the binary to the manifest - catastrophic failure
					goto packageManifestAndShip_error;
				}
				++wlm_files;
			}
		}
		
		// grab extra library directories
		if ((wlm_files = app_ptr->wlmProto->wlm_extraLibDirs(app_ptr->_wlmObj)) != NULL)
		{
			while (*wlm_files != NULL)
			{
				if (cti_addManifestLibDir(m_ptr->mid, *wlm_files))
				{
					// Failed to add the binary to the manifest - catastrophic failure
					goto packageManifestAndShip_error;
				}
				++wlm_files;
			}
		}
		
		// grab extra files
		if ((wlm_files = app_ptr->wlmProto->wlm_extraFiles(app_ptr->_wlmObj)) != NULL)
		{
			while (*wlm_files != NULL)
			{
				if (cti_addManifestFile(m_ptr->mid, *wlm_files))
				{
					// Failed to add the binary to the manifest - catastrophic failure
					goto packageManifestAndShip_error;
				}
				++wlm_files;
			}
		}
	}
	
	// create the tarball name based on its instance to prevent a race 
	// condition where the dlaunch on the compute node has not yet extracted the 
	// previously shipped tarball, and we overwrite it with this new one. This
	// does not impact the name of the directory in the tarball.
	if (asprintf(&tar_name, "%s/%s%d.tar", cfg_dir, m_ptr->stage_name, m_ptr->inst) <= 0)
	{
		_cti_set_error("_cti_packageManifestAndShip: asprintf failed.");
		goto packageManifestAndShip_error;
	}
	
	// create the hidden name for the cleanup file. This will be checked by future
	// runs to try assisting in cleanup if we get killed unexpectedly. This is cludge
	// in an attempt to cleanup. The ideal situation is to be able to tell the kernel
	// to remove the tarball if the process exits, but no mechanism exists today that
	// I know about.
	if (asprintf(&hidden_tar_name, "%s/.%s%d.tar", cfg_dir, m_ptr->stage_name, m_ptr->inst) <= 0)
	{
		_cti_set_error("_cti_packageManifestAndShip: asprintf failed.");
		goto packageManifestAndShip_error;
	}
	
	// open the hidden file and write our pid into it
	if ((hfp = fopen(hidden_tar_name, "w")) == NULL)
	{
		_cti_set_error("_cti_packageManifestAndShip: fopen() %s", strerror(errno));
		goto packageManifestAndShip_error;
	}
	
	pid = getpid();
	
	if (fwrite(&pid, sizeof(pid), 1, hfp) != 1)
	{
		_cti_set_error("_cti_packageManifestAndShip: fwrite() %s", strerror(errno));
		goto packageManifestAndShip_error;
	}
	
	// cleanup - note that the hidden file is cleaned up later on. The only time
	// it doesn't get cleaned up is if we are killed and unable to clean up.
	fclose(hfp);
	hfp = NULL;
	
	// Create the archive write object
	if ((a = archive_write_new()) == NULL)
	{
		_cti_set_error("_cti_packageManifestAndShip: archive_write_new failed.");
		goto packageManifestAndShip_error;
	}
	
	// Fix for BUG 811393 - use gnutar instead of ustar.
	if (archive_write_set_format_gnutar(a) != ARCHIVE_OK)
	{
		_cti_set_error("_cti_packageManifestAndShip: %s", archive_error_string(a));
		goto packageManifestAndShip_error;
	}
	
	// BUG 819725: Enter the critical section
	_cti_signaled = 0;
	_cti_error = 0;
	_cti_setup = 0;
	_cti_signals = NULL;
	_cti_tar_file = tar_name;
	_cti_hidden_file = hidden_tar_name;
	if ((signals = _cti_critical_section(_cti_transfer_handler)) == NULL)
	{
		_cti_set_error("_cti_packageManifestAndShip: Failed to enter critical section.");
		goto packageManifestAndShip_error;
	}
	
	// Handle race with signal handler setup
	_cti_signals = signals;
	_cti_setup = 1;
	if (_cti_signaled)
	{
		_cti_set_error("_cti_packageManifestAndShip: Termination signal received.");
		goto packageManifestAndShip_error;
	}
	
	// Open the actual tarball on disk
	if (archive_write_open_filename(a, tar_name) != ARCHIVE_OK)
	{
		_cti_set_error("_cti_packageManifestAndShip: %s", archive_error_string(a));
		goto packageManifestAndShip_error;
	}
	
	// Handle race with file creation
	if (_cti_signaled)
	{
		// Try to unlink if signal handler was unable to. This can happen if we
		// were signaled in the write open before file creation, and then the
		// signal handler returned.
		if (_cti_error)
		{
			unlink(tar_name);
			unlink(hidden_tar_name);
		}
		_cti_set_error("_cti_packageManifestAndShip: Termination signal received.");
		goto packageManifestAndShip_error;
	}
	
	// Create the archive entry object. This is reused throughout.
	if ((entry = archive_entry_new()) == NULL)
	{
		_cti_set_error("_cti_packageManifestAndShip: archive_entry_new failed.");
		goto packageManifestAndShip_error;
	}
	
	// Create top level directory
	if (_cti_addDirToArchive(a, entry, m_ptr->stage_name))
	{
		// error already set
		goto packageManifestAndShip_error;
	}
	
	// create bin directory
	if (_cti_addDirToArchive(a, entry, bin_path))
	{
		// error already set
		goto packageManifestAndShip_error;
	}
	
	// create lib directory
	if (_cti_addDirToArchive(a, entry, lib_path))
	{
		// error already set
		goto packageManifestAndShip_error;
	}
	
	// create tmp directory
	if (_cti_addDirToArchive(a, entry, tmp_path))
	{
		// error already set
		goto packageManifestAndShip_error;
	}
	
	// allocate memory used when copying files to the tarball
	if ((read_buf = malloc(CTI_BLOCK_SIZE)) == NULL)
	{
		_cti_set_error("_cti_packageManifestAndShip: malloc failed.");
		goto packageManifestAndShip_error;
	}
	
	// process the files list in the manifest
	if ((l_ptr = _cti_getEntries(m_ptr->files)) != NULL)
	{
		// save for cleanup later
		o_ptr = l_ptr;
		
		// loop over each entry in the string list
		while (l_ptr != NULL)
		{
			// process each fileEntry in the chain
			f_ptr = l_ptr->data;
			while (f_ptr != NULL)
			{
				// only process this file if it needs to be shipped
				if (!f_ptr->present)
				{
					// switch on file type
					switch (f_ptr->type)
					{
						case EXEC_ENTRY:
							// verify that this binary should ship
							if (app_ptr->wlmProto->wlm_verifyBinary(app_ptr->_wlmObj, l_ptr->str))
							{
								// this file is not valid and should not be shipped
								break;
							}
							
							// create relative pathname
							if (snprintf(path_buf, PATH_MAX, "%s/%s", bin_path, l_ptr->str) <= 0)
							{
								_cti_set_error("_cti_packageManifestAndShip: snprintf failed.");
								goto packageManifestAndShip_error;
							}
							
							// copy this file to the archive
							if (_cti_copyFileToArchive(a, entry, f_ptr->loc, path_buf, read_buf))
							{
								// error already set
								goto packageManifestAndShip_error;
							}
							
							has_files = true;
							
							break;
							
						case LIB_ENTRY:
							// verify that this library should ship
							if (app_ptr->wlmProto->wlm_verifyLibrary(app_ptr->_wlmObj, l_ptr->str))
							{
								// this file is not valid and should not be shipped
								break;
							}
							
							// create relative pathname
							if (snprintf(path_buf, PATH_MAX, "%s/%s", lib_path, l_ptr->str) <= 0)
							{
								_cti_set_error("_cti_packageManifestAndShip: snprintf failed.");
								goto packageManifestAndShip_error;
							}
							
							// copy this file to the archive
							if (_cti_copyFileToArchive(a, entry, f_ptr->loc, path_buf, read_buf))
							{
								// error already set
								goto packageManifestAndShip_error;
							}
							
							has_files = true;
							
							break;
							
						case LIBDIR_ENTRY:
							// verify that this library directory should ship
							if (app_ptr->wlmProto->wlm_verifyLibDir(app_ptr->_wlmObj, l_ptr->str))
							{
								// this file is not valid and should not be shipped
								break;
							}
							
							// create relative pathname
							if (snprintf(path_buf, PATH_MAX, "%s/%s", lib_path, l_ptr->str) <= 0)
							{
								_cti_set_error("_cti_packageManifestAndShip: snprintf failed.");
								goto packageManifestAndShip_error;
							}
							
							// copy this directory to the archive
							if (_cti_copyFileToArchive(a, entry, f_ptr->loc, path_buf, read_buf))
							{
								// error already set
								goto packageManifestAndShip_error;
							}
							
							has_files = true;
							
							break;
							
						case FILE_ENTRY:
							// verify that this file should ship
							if (app_ptr->wlmProto->wlm_verifyFile(app_ptr->_wlmObj, l_ptr->str))
							{
								// this file is not valid and should not be shipped
								break;
							}
							
							// create relative pathname
							if (snprintf(path_buf, PATH_MAX, "%s/%s", m_ptr->stage_name, l_ptr->str) <= 0)
							{
								_cti_set_error("_cti_packageManifestAndShip: snprintf failed.");
								goto packageManifestAndShip_error;
							}
							
							// copy this file to the archive
							if (_cti_copyFileToArchive(a, entry, f_ptr->loc, path_buf, read_buf))
							{
								// error already set
								goto packageManifestAndShip_error;
							}
							
							has_files = true;
							
							break;
					}
					
					// set present since we have handled this file and added it to the package
					f_ptr->present = 1;
				}
				
				// point at the next fileEntry in the chain
				f_ptr = f_ptr->next;
			}
			
			// increment list entry pointer
			l_ptr = l_ptr->next;
		}
		
		// cleanup
		_cti_cleanupEntries(o_ptr);
		o_ptr = NULL;
	}
	
	// cleanup
	free(read_buf);
	read_buf = NULL;
	archive_entry_free(entry);
	entry = NULL;
	archive_write_free(a);
	a = NULL;
	
	// ensure files were added to the manifest, otherwise return the no files status
	if (!has_files)
	{
		rtn = 2;
		goto skipManifestTransfer;
	}
	
	// Call the appropriate transfer function based on the wlm
	if (app_ptr->wlmProto->wlm_shipPackage(app_ptr->_wlmObj, tar_name))
	{
		// we failed to ship the file to the compute nodes for some reason - catastrophic failure
		// Error message already set
		goto packageManifestAndShip_error;
	}
	
skipManifestTransfer:
	
	// clean things up
	free(bin_path);
	bin_path = NULL;
	free(lib_path);
	lib_path = NULL;
	free(tmp_path);
	tmp_path = NULL;
	unlink(tar_name);
	_cti_tar_file = NULL;
	free(tar_name);
	tar_name = NULL;
	unlink(hidden_tar_name);
	_cti_hidden_file = NULL;
	free(hidden_tar_name);
	hidden_tar_name = NULL;
	
	// BUG 819725: Unblock signals. We are done with the critical section
	_cti_setup = 0;
	_cti_end_critical_section(signals);
	signals = NULL;
	
	return rtn;
	
	// Error handling code starts below
	// Note that the error string has already been set
	
packageManifestAndShip_error:

	if (stage_name != NULL)
	{
		free(stage_name);
	}
	
	if (bin_path != NULL)
	{
		free(bin_path);
	}
	
	if (lib_path != NULL)
	{
		free(lib_path);
	}
	
	if (tmp_path != NULL)
	{
		free(tmp_path);
	}
	
	if (tar_name != NULL)
	{
		unlink(tar_name);
		_cti_tar_file = NULL;
		free(tar_name);
	}
	
	if (hidden_tar_name != NULL)
	{
		unlink(hidden_tar_name);
		_cti_hidden_file = NULL;
		free(hidden_tar_name);
	}
	
	if (hfp != NULL)
	{
		fclose(hfp);
	}
	
	// Attempt to cleanup the archive stuff just in case it has been created already
	if (entry != NULL)
	{
		archive_entry_free(entry);
	}
	
	if (a != NULL)
	{
		archive_write_free(a);
	}
	
	if (read_buf != NULL)
	{
		free(read_buf);
	}
	
	if (o_ptr != NULL)
	{
		_cti_cleanupEntries(o_ptr);
	}
	
	// BUG 819725: Unblock signals. We are done with the critical section
	if (signals != NULL)
	{
		_cti_end_critical_section(signals);
	}

	return 1;
}

cti_session_id_t
cti_sendManifest(cti_app_id_t appId, cti_manifest_id_t mid, int dbg)
{
	appEntry_t *		app_ptr;			// pointer to the appEntry_t object associated with the provided aprun pid
	const char *		toolPath;			// tool path associated with this appEntry_t based on the wlm
	manifest_t *		m_ptr;				// pointer to the manifest_t object associated with the cti_manifest_id_t argument
	char *				jid_str;			// job identifier string - wlm specific
	cti_args_t *		d_args;				// args to pass to the daemon launcher
	session_t *			s_ptr = NULL;		// points at the session to return
	int					r;
	cti_session_id_t	rtn;

	// sanity check
	if (appId == 0)
	{
		_cti_set_error("Invalid appId %d.", (int)appId);
		return 1;
	}
	
	if (mid <= 0)
	{
		_cti_set_error("Invalid cti_manifest_id_t %d.", (int)mid);
		return 1;
	}
	
	// try to find an entry in the my_apps array for the appId
	if ((app_ptr = _cti_findAppEntry(appId)) == NULL)
	{
		// appId not found in the global my_apps array - unknown appId failure
		// error string already set
		return 0;
	}
	
	// find the manifest_t for the mid
	if ((m_ptr = _cti_findManifest(mid)) == NULL)
	{
		// We failed to find the manifest for the mid
		// error string already set
		return 0;
	}
	
	// If there is a sid in the manifest, ensure it is still valid
	if (m_ptr->sid != 0)
	{
		// Bugfix: Make sure to set s_ptr...
		// Find the session entry in the global session list for the sid
		if ((s_ptr = _cti_findSession(m_ptr->sid)) == NULL)
		{
			// We failed to find the session for the sid
			// error string already set
			_cti_reapManifest(m_ptr->mid);
			return 0;
		}
	}
	
	// ship the manifest tarball to the compute nodes
	r = _cti_packageManifestAndShip(app_ptr, m_ptr);
	if (r == 1)
	{
		// Failed to ship the manifest - catastrophic failure
		// error string already set
		_cti_reapManifest(m_ptr->mid);
		return 0;
	}
	// if there was nothing to ship, ensure there was a session, otherwise error
	if (r == 2)
	{
		// ensure that there is a session set in the m_ptr
		if (m_ptr->sid <= 0)
		{
			_cti_set_error("cti_manifest_id_t %d was empty!", m_ptr->mid);
			_cti_reapManifest(m_ptr->mid);
			return 0;
		}
		
		// return the session id since everything was already shipped
		rtn = m_ptr->sid;
		
		// remove the manifest
		_cti_reapManifest(m_ptr->mid);
		
		return rtn;
	}
	
	// now we need to create the argv for the actual call to the WLM wrapper call
	//
	// The options passed MUST correspond to the options defined in the daemon_launcher program.
	//
	// The actual daemon launcher path string is determined by the wlm_startDaemon call
	// since that is wlm specific
	
	if ((jid_str = app_ptr->wlmProto->wlm_getJobId(app_ptr->_wlmObj)) == NULL)
	{
		// error already set
		_cti_reapManifest(m_ptr->mid);
		return 0;
	}
	
	// Get the tool path for this wlm
	if ((toolPath = app_ptr->wlmProto->wlm_getToolPath(app_ptr->_wlmObj)) == NULL)
	{
		// error already set
		_cti_reapManifest(m_ptr->mid);
		free(jid_str);
		return 0;
	}
	
	// create a new args obj
	if ((d_args = _cti_newArgs()) == NULL)
	{
		_cti_set_error("_cti_newArgs failed.");
		_cti_reapManifest(m_ptr->mid);
		free(jid_str);
		return 0;
	} 
	
	// begin adding the args
	
	if (_cti_addArg(d_args, "-a"))
	{
		_cti_set_error("_cti_addArg failed.");
		_cti_reapManifest(m_ptr->mid);
		free(jid_str);
		_cti_freeArgs(d_args);
		return 0;
	}
	if (_cti_addArg(d_args, "%s", jid_str))
	{
		_cti_set_error("_cti_addArg failed.");
		_cti_reapManifest(m_ptr->mid);
		free(jid_str);
		_cti_freeArgs(d_args);
		return 0;
	}
	free(jid_str);
	
	if (_cti_addArg(d_args, "-p"))
	{
		_cti_set_error("_cti_addArg failed.");
		_cti_reapManifest(m_ptr->mid);
		_cti_freeArgs(d_args);
		return 0;
	}
	if (_cti_addArg(d_args, "%s", toolPath))
	{
		_cti_set_error("_cti_addArg failed.");
		_cti_reapManifest(m_ptr->mid);
		_cti_freeArgs(d_args);
		return 0;
	}
	
	if (_cti_addArg(d_args, "-w"))
	{
		_cti_set_error("_cti_addArg failed.");
		_cti_reapManifest(m_ptr->mid);
		_cti_freeArgs(d_args);
		return 0;
	}
	if (_cti_addArg(d_args, "%d", app_ptr->wlmProto->wlm_type))
	{
		_cti_set_error("_cti_addArg failed.");
		_cti_reapManifest(m_ptr->mid);
		_cti_freeArgs(d_args);
		return 0;
	}
	
	if (_cti_addArg(d_args, "-m"))
	{
		_cti_set_error("_cti_addArg failed.");
		_cti_reapManifest(m_ptr->mid);
		_cti_freeArgs(d_args);
		return 0;
	}
	if (_cti_addArg(d_args, "%s%d.tar", m_ptr->stage_name, m_ptr->inst))
	{
		_cti_set_error("_cti_addArg failed.");
		_cti_reapManifest(m_ptr->mid);
		_cti_freeArgs(d_args);
		return 0;
	}
	
	if (_cti_addArg(d_args, "-d"))
	{
		_cti_set_error("_cti_addArg failed.");
		_cti_reapManifest(m_ptr->mid);
		_cti_freeArgs(d_args);
		return 0;
	}
	if (_cti_addArg(d_args, "%s", m_ptr->stage_name))
	{
		_cti_set_error("_cti_addArg failed.");
		_cti_reapManifest(m_ptr->mid);
		_cti_freeArgs(d_args);
		return 0;
	}
	
	if (_cti_addArg(d_args, "-i"))
	{
		_cti_set_error("_cti_addArg failed.");
		_cti_reapManifest(m_ptr->mid);
		_cti_freeArgs(d_args);
		return 0;
	}
	if (_cti_addArg(d_args, "%d", m_ptr->inst))
	{
		_cti_set_error("_cti_addArg failed.");
		_cti_reapManifest(m_ptr->mid);
		_cti_freeArgs(d_args);
		return 0;
	}
	
	// add the debug switch if debug is on
	if (dbg)
	{
		if (_cti_addArg(d_args, "--debug"))
		{
			_cti_set_error("_cti_addArg failed.");
			_cti_reapManifest(m_ptr->mid);
			_cti_freeArgs(d_args);
			return 0;
		}
	}
	
	// Done. We now have an argv array to pass
	
	// Call the appropriate transfer function based on the wlm
	if (app_ptr->wlmProto->wlm_startDaemon(app_ptr->_wlmObj, d_args))
	{
		// we failed to ship the file to the compute nodes for some reason - catastrophic failure
		// Error message already set
		_cti_freeArgs(d_args);
		_cti_reapManifest(m_ptr->mid);
		return 0;
	}
		
	// cleanup our memory
	_cti_freeArgs(d_args);
	
	// create a new session for this tool daemon instance if one doesn't already exist
	if (m_ptr->sid == 0)
	{
		if ((s_ptr = _cti_newSession(m_ptr)) == NULL)
		{
			// we failed to create a new session_t - catastrophic failure
			// error string already set
			_cti_reapManifest(m_ptr->mid);
			return 0;
		}
	} else
	{
		// Merge the manifest into the existing session
		if (_cti_addManifestToSession(m_ptr, s_ptr))
		{
			// we failed to merge the manifest into the session - catastrophic failure
			// error string already set
			_cti_reapManifest(m_ptr->mid);
			return 0;
		}
	}
	
	// remove the manifest
	_cti_reapManifest(m_ptr->mid);
	
	// Associate this session with the app_ptr
	_cti_addSessionToApp(app_ptr, s_ptr->sid);
	
	// copy toolPath into the session
	s_ptr->toolPath = strdup(toolPath);
	
	return s_ptr->sid;
}

cti_session_id_t
cti_execToolDaemon(cti_app_id_t appId, cti_manifest_id_t mid, cti_session_id_t sid, const char *daemon, const char * const args[], const char * const env[], int dbg)
{
	appEntry_t *	app_ptr;			// pointer to the appEntry_t object associated with the provided aprun pid
	manifest_t *	m_ptr;				// pointer to the manifest_t object associated with the cti_manifest_id_t argument
	int				useManif = 1;		// controls if a manifest was shipped or not
	int				r;					// return value from send manif call
	char *			fullname;			// full path name of the executable to launch as a tool daemon
	char *			realname;			// realname (lacking path info) of the executable
	char *			jid_str;			// job id string to pass to the backend. This is wlm specific.
	const char *	toolPath;			// tool path of backend staging directory. This is wlm specific.
	const char *	attribsPath;		// path to pmi_attribs file based on the wlm
	cti_args_t *	d_args;				// args to pass to the daemon launcher
	session_t *		s_ptr = NULL;		// points at the session to return
		
	// sanity check
	if (appId == 0)
	{
		_cti_set_error("Invalid appId %d.", (int)appId);
		return 1;
	}
	
	if (daemon == NULL)
	{
		_cti_set_error("Required tool daemon argument is missing.");
		return 1;
	}
	
	// try to find an entry in the my_apps array for the appId
	if ((app_ptr = _cti_findAppEntry(appId)) == NULL)
	{
		// appId not found in the global my_apps array - unknown appId failure
		// error string already set
		return 0;
	}
	
	// if the user provided a session, ensure it exists
	if (sid != 0)
	{
		if ((s_ptr = _cti_findSession(sid)) == NULL)
		{
			// We failed to find the session for the sid
			// error string already set
			return 0;
		}
	}
	
	// Check to see if the user provided a mid argument and process it
	if (mid == 0)
	{
		// lets create a new manifest_t object
		if ((m_ptr = _cti_newManifest(sid)) == NULL)
		{
			// we failed to create a new manifest_t - catastrophic failure
			// error string already set
			return 0;
		}
	} else
	{
		// try to find the manifest_t for the mid
		if ((m_ptr = _cti_findManifest(mid)) == NULL)
		{
			// We failed to find the manifest for the mid
			// error string already set
			return 0;
		}
	}
	
	// ensure that the session matches in the manifest
	if (s_ptr != NULL)
	{
		if (m_ptr->sid != s_ptr->sid)
		{
			// mismatch
			_cti_set_error("cti_manifest_id_t %d was not created with cti_session_id_t %d.", (int)mid, (int)sid);
			return 0;
		}
	}
	
	// add the daemon to the manifest
	if (cti_addManifestBinary(m_ptr->mid, daemon))
	{
		// Failed to add the binary to the manifest - catastrophic failure
		// error string already set
		_cti_reapManifest(m_ptr->mid);
		return 0;
	}
	
	// Ensure that there are files to ship, otherwise there is no need to ship a
	// tarball, everything we need already has been transfered to the nodes
	// Try to ship the tarball, if this returns with 2 everything we need already
	// has been transfered to the nodes and there is no need to ship it again.
	r = _cti_packageManifestAndShip(app_ptr, m_ptr);
	if (r == 1)
	{
		// Failed to ship the manifest - catastrophic failure
		// error string already set
		_cti_reapManifest(m_ptr->mid);
		return 0;
	}
	if (r == 2)
	{
		// manifest was not shipped.
		useManif = 0;
	}
	
	// now we need to create the argv for the actual call to the WLM wrapper call
	//
	// The options passed MUST correspond to the options defined in the daemon_launcher program.
	//
	// The actual daemon launcher path string is determined by the wlm_startDaemon call
	// since that is wlm specific
	
	// find the binary name for the args
	
	// convert daemon to fullpath name
	if ((fullname = _cti_pathFind(daemon, NULL)) == NULL)
	{
		_cti_set_error("Could not locate the specified tool daemon binary in PATH.");
		_cti_reapManifest(m_ptr->mid);
		return 0;
	}
	
	// next just grab the real name (without path information) of the binary
	if ((realname = _cti_pathToName(fullname)) == NULL)
	{
		_cti_set_error("Could not convert the tool daemon binary fullname to realname.");
		_cti_reapManifest(m_ptr->mid);
		free(fullname);
		return 0;
	}
	
	// done with fullname
	free(fullname);
	
	if ((jid_str = app_ptr->wlmProto->wlm_getJobId(app_ptr->_wlmObj)) == NULL)
	{
		// error already set
		_cti_reapManifest(m_ptr->mid);
		free(realname);
		return 0;
	}
	
	// Get the tool path for this wlm
	if ((toolPath = app_ptr->wlmProto->wlm_getToolPath(app_ptr->_wlmObj)) == NULL)
	{
		// error already set
		_cti_reapManifest(m_ptr->mid);
		free(jid_str);
		free(realname);
		return 0;
	}
	
	// Get the attribs path for this wlm - this is optional and can come back null
	attribsPath = app_ptr->wlmProto->wlm_getAttribsPath(app_ptr->_wlmObj);
	
	// create a new args obj
	if ((d_args = _cti_newArgs()) == NULL)
	{
		_cti_set_error("_cti_newArgs failed.");
		_cti_reapManifest(m_ptr->mid);
		free(jid_str);
		free(realname);
		return 0;
	} 
	
	// begin adding the args
	
	if (_cti_addArg(d_args, "-a"))
	{
		_cti_set_error("_cti_addArg failed.");
		_cti_reapManifest(m_ptr->mid);
		free(jid_str);
		free(realname);
		_cti_freeArgs(d_args);
		return 0;
	}
	if (_cti_addArg(d_args, "%s", jid_str))
	{
		_cti_set_error("_cti_addArg failed.");
		_cti_reapManifest(m_ptr->mid);
		free(jid_str);
		free(realname);
		_cti_freeArgs(d_args);
		return 0;
	}
	free(jid_str);
	
	if (_cti_addArg(d_args, "-p"))
	{
		_cti_set_error("_cti_addArg failed.");
		_cti_reapManifest(m_ptr->mid);
		free(realname);
		_cti_freeArgs(d_args);
		return 0;
	}
	if (_cti_addArg(d_args, "%s", toolPath))
	{
		_cti_set_error("_cti_addArg failed.");
		_cti_reapManifest(m_ptr->mid);
		free(realname);
		_cti_freeArgs(d_args);
		return 0;
	}
	
	if (attribsPath != NULL)
	{
		if (_cti_addArg(d_args, "-t"))
		{
			_cti_set_error("_cti_addArg failed.");
			_cti_reapManifest(m_ptr->mid);
			free(realname);
			_cti_freeArgs(d_args);
			return 0;
		}
		if (_cti_addArg(d_args, "%s", attribsPath))
		{
			_cti_set_error("_cti_addArg failed.");
			_cti_reapManifest(m_ptr->mid);
			free(realname);
			_cti_freeArgs(d_args);
			return 0;
		}
	}
	
	if (_cti_addArg(d_args, "-w"))
	{
		_cti_set_error("_cti_addArg failed.");
		_cti_reapManifest(m_ptr->mid);
		free(realname);
		_cti_freeArgs(d_args);
		return 0;
	}
	if (_cti_addArg(d_args, "%d", app_ptr->wlmProto->wlm_type))
	{
		_cti_set_error("_cti_addArg failed.");
		_cti_reapManifest(m_ptr->mid);
		free(realname);
		_cti_freeArgs(d_args);
		return 0;
	}
	
	if (_cti_addArg(d_args, "-b"))
	{
		_cti_set_error("_cti_addArg failed.");
		_cti_reapManifest(m_ptr->mid);
		free(realname);
		_cti_freeArgs(d_args);
		return 0;
	}
	if (_cti_addArg(d_args, "%s", realname))
	{
		_cti_set_error("_cti_addArg failed.");
		_cti_reapManifest(m_ptr->mid);
		free(realname);
		_cti_freeArgs(d_args);
		return 0;
	}
	free(realname);
	
	if (_cti_addArg(d_args, "-d"))
	{
		_cti_set_error("_cti_addArg failed.");
		_cti_reapManifest(m_ptr->mid);
		_cti_freeArgs(d_args);
		return 0;
	}
	if (_cti_addArg(d_args, "%s", m_ptr->stage_name))
	{
		_cti_set_error("_cti_addArg failed.");
		_cti_reapManifest(m_ptr->mid);
		_cti_freeArgs(d_args);
		return 0;
	}
	
	if (_cti_addArg(d_args, "-i"))
	{
		_cti_set_error("_cti_addArg failed.");
		_cti_reapManifest(m_ptr->mid);
		_cti_freeArgs(d_args);
		return 0;
	}
	if (_cti_addArg(d_args, "%d", m_ptr->inst))
	{
		_cti_set_error("_cti_addArg failed.");
		_cti_reapManifest(m_ptr->mid);
		_cti_freeArgs(d_args);
		return 0;
	}
	
	// add -m argument if needed
	if (useManif)
	{
		if (_cti_addArg(d_args, "-m"))
		{
			_cti_set_error("_cti_addArg failed.");
			_cti_reapManifest(m_ptr->mid);
			_cti_freeArgs(d_args);
			return 0;
		}
		if (_cti_addArg(d_args, "%s%d.tar", m_ptr->stage_name, m_ptr->inst))
		{
			_cti_set_error("_cti_addArg failed.");
			_cti_reapManifest(m_ptr->mid);
			_cti_freeArgs(d_args);
			return 0;
		}
	}
	
	// add each of the env vars from the env array
	// ensure the are actual entries before dereferencing
	if (env != NULL)
	{
		while (*env != NULL)
		{
			// ensure this env string is non-zero
			if (strlen(*env) <= 0)
			{
				continue;
			}
			// add this env arg
			if (_cti_addArg(d_args, "-e"))
			{
				_cti_set_error("_cti_addArg failed.");
				_cti_reapManifest(m_ptr->mid);
				_cti_freeArgs(d_args);
				return 0;
			}
			if (_cti_addArg(d_args, "%s", *env++))
			{
				_cti_set_error("_cti_addArg failed.");
				_cti_reapManifest(m_ptr->mid);
				_cti_freeArgs(d_args);
				return 0;
			}
		}
	}
		
	// add the debug switch if debug is on
	if (dbg)
	{
		if (_cti_addArg(d_args, "--debug"))
		{
			_cti_set_error("_cti_addArg failed.");
			_cti_reapManifest(m_ptr->mid);
			_cti_freeArgs(d_args);
			return 0;
		}
	}
	
	// add each of the args from the args array
	// ensure the are actual entries before dereferencing
	if (args != NULL)
	{
		// add the options terminator
		if (_cti_addArg(d_args, "--"))
		{
			_cti_set_error("_cti_addArg failed.");
			_cti_reapManifest(m_ptr->mid);
			_cti_freeArgs(d_args);
			return 0;
		}
		while (*args != NULL)
		{
			// ensure this arg string is non-zero
			if (strlen(*args) <= 0)
			{
				continue;
			}
			if (_cti_addArg(d_args, "%s", *args++))
			{
				_cti_set_error("_cti_addArg failed.");
				_cti_reapManifest(m_ptr->mid);
				_cti_freeArgs(d_args);
				return 0;
			}
		}
	}
		
	// Done. We now have an argv array to pass
	
	// Call the appropriate transfer function based on the wlm
	if (app_ptr->wlmProto->wlm_startDaemon(app_ptr->_wlmObj, d_args))
	{
		// we failed to ship the file to the compute nodes for some reason - catastrophic failure
		// Error message already set
		_cti_reapManifest(m_ptr->mid);
		_cti_freeArgs(d_args);
		return 0;
	}
	
	// cleanup our memory
	_cti_freeArgs(d_args);
	
	// create a new session for this tool daemon instance if one doesn't already exist
	if (s_ptr == NULL)
	{
		if ((s_ptr = _cti_newSession(m_ptr)) == NULL)
		{
			// we failed to create a new session_t - catastrophic failure
			// error string already set
			_cti_reapManifest(m_ptr->mid);
			return 0;
		}
	} else
	{
		// Merge the manifest into the existing session only if we needed to
		// transfer any files
		if (useManif)
		{
			if (_cti_addManifestToSession(m_ptr, s_ptr))
			{
				// we failed to merge the manifest into the session - catastrophic failure
				// error string already set
				_cti_reapManifest(m_ptr->mid);
				return 0;
			}
		}
	}
	
	// remove the manifest
	_cti_reapManifest(m_ptr->mid);
	
	// Associate this session with the app_ptr
	_cti_addSessionToApp(app_ptr, s_ptr->sid);
	
	// copy toolPath into the session
	s_ptr->toolPath = strdup(toolPath);
	
	return s_ptr->sid;
}

char **
cti_getSessionLockFiles(cti_session_id_t sid)
{
	session_t *		s_ptr = NULL;		// points at the session
	char **			rtn = NULL;
	char **			ptr;
	int				i;
	
	// Ensure the provided session exists
	if ((s_ptr = _cti_findSession(sid)) == NULL)
	{
		// We failed to find the session for the sid
		// error string already set
		return NULL;
	}
	
	// calloc the return array - need 1 extra for null term
	if ((rtn = calloc(s_ptr->instCnt+1, sizeof(char *))) == (void *)0)
	{
		_cti_set_error("calloc failed.");
		return NULL;
	}
	
	// create the strings
	ptr = rtn;
	for (i=1; i <= s_ptr->instCnt; ++i)
	{
		if (asprintf(ptr, "%s/.lock_%s_%d", s_ptr->toolPath, s_ptr->stage_name, i) <= 0)
		{
			_cti_set_error("asprintf failed.");
			free(rtn);
			return NULL;
		}
		// increment ptr
		++ptr;
	}
	// force the final entry to be null
	*ptr = NULL;
	
	return rtn;
}

char *
cti_getSessionRootDir(cti_session_id_t sid)
{
	session_t *		s_ptr = NULL;		// points at the session
	char *			rtn = NULL;
	
	// Ensure the provided session exists
	if ((s_ptr = _cti_findSession(sid)) == NULL)
	{
		// We failed to find the session for the sid
		// error string already set
		return NULL;
	}
	
	// create the return string
	if (asprintf(&rtn, "%s/%s", s_ptr->toolPath, s_ptr->stage_name) <= 0)
	{
		_cti_set_error("asprintf failed.");
		return NULL;
	}
	
	return rtn;
}

char *
cti_getSessionBinDir(cti_session_id_t sid)
{
	session_t *		s_ptr = NULL;		// points at the session
	char *			rtn = NULL;
	
	// Ensure the provided session exists
	if ((s_ptr = _cti_findSession(sid)) == NULL)
	{
		// We failed to find the session for the sid
		// error string already set
		return NULL;
	}
	
	// create the return string
	if (asprintf(&rtn, "%s/%s/bin", s_ptr->toolPath, s_ptr->stage_name) <= 0)
	{
		_cti_set_error("asprintf failed.");
		return NULL;
	}
	
	return rtn;
}

char *
cti_getSessionLibDir(cti_session_id_t sid)
{
	session_t *		s_ptr = NULL;		// points at the session
	char *			rtn = NULL;
	
	// Ensure the provided session exists
	if ((s_ptr = _cti_findSession(sid)) == NULL)
	{
		// We failed to find the session for the sid
		// error string already set
		return NULL;
	}
	
	// create the return string
	if (asprintf(&rtn, "%s/%s/lib", s_ptr->toolPath, s_ptr->stage_name) <= 0)
	{
		_cti_set_error("asprintf failed.");
		return NULL;
	}
	
	return rtn;
}

char *
cti_getSessionFileDir(cti_session_id_t sid)
{
	session_t *		s_ptr = NULL;		// points at the session
	char *			rtn = NULL;
	
	// Ensure the provided session exists
	if ((s_ptr = _cti_findSession(sid)) == NULL)
	{
		// We failed to find the session for the sid
		// error string already set
		return NULL;
	}
	
	// create the return string
	if (asprintf(&rtn, "%s/%s", s_ptr->toolPath, s_ptr->stage_name) <= 0)
	{
		_cti_set_error("asprintf failed.");
		return NULL;
	}
	
	return rtn;
}

char *
cti_getSessionTmpDir(cti_session_id_t sid)
{
	session_t *		s_ptr = NULL;		// points at the session
	char *			rtn = NULL;
	
	// Ensure the provided session exists
	if ((s_ptr = _cti_findSession(sid)) == NULL)
	{
		// We failed to find the session for the sid
		// error string already set
		return NULL;
	}
	
	// create the return string
	if (asprintf(&rtn, "%s/%s/tmp", s_ptr->toolPath, s_ptr->stage_name) <= 0)
	{
		_cti_set_error("asprintf failed.");
		return NULL;
	}
	
	return rtn;
}

