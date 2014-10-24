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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <sys/stat.h>
#include <sys/types.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>

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

typedef struct
{
	char *				loc;			// location of file
	int					present;		// already shipped?
} fileEntry_t;

typedef struct
{
	cti_manifest_id_t	mid;			// manifest id
	cti_session_id_t	sid;			// optional session id
	int					inst;			// instance number - used with session to prevent tarball name conflicts
	char *				stage_name;		// basename of the manifest directory
	stringList_t *		exec_files;		// list of manifest binaries
	stringList_t *		lib_files;		// list of manifest libraries
	stringList_t *		libdir_files;	// list of manifest library directories
	stringList_t *		file_files;		// list of manifest files
	int					hasFiles;		// true if there are any files to ship in the file lists
} manifest_t;

typedef struct
{
	cti_session_id_t	sid;			// session id
	int					instCnt;		// instance count - set in the manifest to prevent naming conflicts
	char *				stage_name;		// basename of the manifest directory
	char *				toolPath;		// toolPath of the app entry - DO NOT FREE THIS!!!
	stringList_t *		exec_names;		// list of manifest binary names
	stringList_t *		lib_names;		// list of manifest dso names
	stringList_t *		libdir_names;	// list of manifest library directory names
	stringList_t *		file_names;		// list of manifest regular file names
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

/* Static prototypes */
static fileEntry_t *	_cti_newFileEntry(void);
static void				_cti_consumeFileEntry(void *);
static fileEntry_t *	_cti_copyFileEntry(fileEntry_t *);
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
static int				_cti_copyFileToPackage(const char *, const char *, const char *);
static int				_cti_removeDirectory(char *);
static int				_cti_copyDirectoryToPackage(const char *, const char *, const char *);
static int				_cti_packageManifestAndShip(appEntry_t *, manifest_t *);

/* global variables */
static sessList_t *			_cti_my_sess	= NULL;
static cti_session_id_t		_cti_next_sid	= 1;
static manifList_t *		_cti_my_manifs	= NULL;
static cti_manifest_id_t	_cti_next_mid	= 1;

/* static functions */

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
	if (entry->loc)
	{
		newEntry->loc = strdup(entry->loc);
	}
	newEntry->present = entry->present;
	
	return newEntry;
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
	
	// eat each of the string lists
	_cti_consumeStringList(sess->exec_names, &_cti_consumeFileEntry);
	_cti_consumeStringList(sess->lib_names, &_cti_consumeFileEntry);
	_cti_consumeStringList(sess->libdir_names, &_cti_consumeFileEntry);
	_cti_consumeStringList(sess->file_names, &_cti_consumeFileEntry);
	
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
	
	// create the stringList_t objects
	if ((this->exec_names = _cti_newStringList()) == NULL)
	{
		_cti_set_error("_cti_newStringList() failed.");
		_cti_consumeSession(this);
		return NULL;
	}
	if ((this->lib_names = _cti_newStringList()) == NULL)
	{
		_cti_set_error("_cti_newStringList() failed.");
		_cti_consumeSession(this);
		return NULL;
	}
	if ((this->libdir_names = _cti_newStringList()) == NULL)
	{
		_cti_set_error("_cti_newStringList() failed.");
		_cti_consumeSession(this);
		return NULL;
	}
	if ((this->file_names = _cti_newStringList()) == NULL)
	{
		_cti_set_error("_cti_newStringList() failed.");
		_cti_consumeSession(this);
		return NULL;
	}
	
	// copy the files in the manifest over to the session.
	
	if ((l_ptr = _cti_getEntries(m_ptr->exec_files)) != NULL)
	{
		// save for later
		o_ptr = l_ptr;
		while (l_ptr != NULL)
		{
			// copy the data entry for the new list
			if (l_ptr->data)
			{
				d_ptr = _cti_copyFileEntry(l_ptr->data);
				if (d_ptr == NULL)
				{
					// error occured, is already set
					_cti_consumeSession(this);
					_cti_cleanupEntries(o_ptr);
					return NULL;
				}
			}
			// add this string to the session list
			if (_cti_addString(this->exec_names, l_ptr->str, d_ptr))
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
	
	if ((l_ptr = _cti_getEntries(m_ptr->lib_files)) != NULL)
	{
		// save for later
		o_ptr = l_ptr;
		while (l_ptr != NULL)
		{
			// copy the data entry for the new list
			if (l_ptr->data)
			{
				d_ptr = _cti_copyFileEntry(l_ptr->data);
				if (d_ptr == NULL)
				{
					// error occured, is already set
					_cti_consumeSession(this);
					_cti_cleanupEntries(o_ptr);
					return NULL;
				}
			}
			// add this string to the session list
			if (_cti_addString(this->lib_names, l_ptr->str, d_ptr))
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
	
	if ((l_ptr = _cti_getEntries(m_ptr->libdir_files)) != NULL)
	{
		// save for later
		o_ptr = l_ptr;
		while (l_ptr != NULL)
		{
			// copy the data entry for the new list
			if (l_ptr->data)
			{
				d_ptr = _cti_copyFileEntry(l_ptr->data);
				if (d_ptr == NULL)
				{
					// error occured, is already set
					_cti_consumeSession(this);
					_cti_cleanupEntries(o_ptr);
					return NULL;
				}
			}
			// add this string to the session list
			if (_cti_addString(this->libdir_names, l_ptr->str, d_ptr))
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
	
	if ((l_ptr = _cti_getEntries(m_ptr->file_files)) != NULL)
	{
		// save for later
		o_ptr = l_ptr;
		while (l_ptr != NULL)
		{
			// copy the data entry for the new list
			if (l_ptr->data)
			{
				d_ptr = _cti_copyFileEntry(l_ptr->data);
				if (d_ptr == NULL)
				{
					// error occured, is already set
					_cti_consumeSession(this);
					_cti_cleanupEntries(o_ptr);
					return NULL;
				}
			}
			// add this string to the session list
			if (_cti_addString(this->file_names, l_ptr->str, d_ptr))
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
	
	// eat each of the string lists
	_cti_consumeStringList(entry->exec_files, &_cti_consumeFileEntry);
	_cti_consumeStringList(entry->lib_files, &_cti_consumeFileEntry);
	_cti_consumeStringList(entry->libdir_files, &_cti_consumeFileEntry);
	_cti_consumeStringList(entry->file_files, &_cti_consumeFileEntry);
	
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
	
	// create the stringList_t objects
	if ((this->exec_files = _cti_newStringList()) == NULL)
	{
		_cti_set_error("_cti_newStringList() failed.");
		_cti_consumeManifest(this);
		return NULL;
	}
	if ((this->lib_files = _cti_newStringList()) == NULL)
	{
		_cti_set_error("_cti_newStringList() failed.");
		_cti_consumeManifest(this);
		return NULL;
	}
	if ((this->libdir_files = _cti_newStringList()) == NULL)
	{
		_cti_set_error("_cti_newStringList() failed.");
		_cti_consumeManifest(this);
		return NULL;
	}
	if ((this->file_files = _cti_newStringList()) == NULL)
	{
		_cti_set_error("_cti_newStringList() failed.");
		_cti_consumeManifest(this);
		return NULL;
	}
	
	// Check to see if we need to add session info. Note that the names list
	// will guarantee uniqueness. We simply need to add the name of a library
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
		
		// copy all of the names to the manifest file lists
		
		if ((l_ptr = _cti_getEntries(s_ptr->exec_names)) != NULL)
		{
			// save for later
			o_ptr = l_ptr;
			while (l_ptr != NULL)
			{
				// copy the data entry for the new list
				if (l_ptr->data)
				{
					f_ptr = _cti_copyFileEntry(l_ptr->data);
					if (f_ptr == NULL)
					{
						// error already set
						_cti_consumeManifest(this);
						_cti_cleanupEntries(o_ptr);
						return NULL;
					}
				} else
				{
					// failed to find a data entry for this string
					_cti_set_error("_cti_newManifest: Missing data entry for string!");
					_cti_consumeManifest(this);
					_cti_cleanupEntries(o_ptr);
					return NULL;
				}
				
				// set present to 1 since this was part of the session, and we are
				// guaranteed to have shipped all files at this point
				f_ptr->present = 1;		// these files were already transfered and checked for validity
				
				// add this string to the manifest list
				if (_cti_addString(this->exec_files, l_ptr->str, f_ptr))
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
		
		if ((l_ptr = _cti_getEntries(s_ptr->lib_names)) != NULL)
		{
			// save for later
			o_ptr = l_ptr;
			while (l_ptr != NULL)
			{
				// copy the data entry for the new list
				if (l_ptr->data)
				{
					f_ptr = _cti_copyFileEntry(l_ptr->data);
					if (f_ptr == NULL)
					{
						// error already set
						_cti_consumeManifest(this);
						_cti_cleanupEntries(o_ptr);
						return NULL;
					}
				} else
				{
					// failed to find a data entry for this string
					_cti_set_error("_cti_newManifest: Missing data entry for string!");
					_cti_consumeManifest(this);
					_cti_cleanupEntries(o_ptr);
					return NULL;
				}
				
				// set present to 1 since this was part of the session, and we are
				// guaranteed to have shipped all files at this point
				f_ptr->present = 1;		// these files were already transfered and checked for validity
				
				// add this string to the manifest list
				if (_cti_addString(this->lib_files, l_ptr->str, f_ptr))
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
		
		if ((l_ptr = _cti_getEntries(s_ptr->libdir_names)) != NULL)
		{
			// save for later
			o_ptr = l_ptr;
			while (l_ptr != NULL)
			{
				// copy the data entry for the new list
				if (l_ptr->data)
				{
					f_ptr = _cti_copyFileEntry(l_ptr->data);
					if (f_ptr == NULL)
					{
						// error already set
						_cti_consumeManifest(this);
						_cti_cleanupEntries(o_ptr);
						return NULL;
					}
				} else
				{
					// failed to find a data entry for this string
					_cti_set_error("_cti_newManifest: Missing data entry for string!");
					_cti_consumeManifest(this);
					_cti_cleanupEntries(o_ptr);
					return NULL;
				}
				
				// set present to 1 since this was part of the session, and we are
				// guaranteed to have shipped all files at this point
				f_ptr->present = 1;		// these files were already transfered and checked for validity
				
				// add this string to the manifest list
				if (_cti_addString(this->libdir_files, l_ptr->str, f_ptr))
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
		
		if ((l_ptr = _cti_getEntries(s_ptr->file_names)) != NULL)
		{
			// save for later
			o_ptr = l_ptr;
			while (l_ptr != NULL)
			{
				// copy the data entry for the new list
				if (l_ptr->data)
				{
					f_ptr = _cti_copyFileEntry(l_ptr->data);
					if (f_ptr == NULL)
					{
						// error already set
						_cti_consumeManifest(this);
						_cti_cleanupEntries(o_ptr);
						return NULL;
					}
				} else
				{
					// failed to find a data entry for this string
					_cti_set_error("_cti_newManifest: Missing data entry for string!");
					_cti_consumeManifest(this);
					_cti_cleanupEntries(o_ptr);
					return NULL;
				}
				
				// set present to 1 since this was part of the session, and we are
				// guaranteed to have shipped all files at this point
				f_ptr->present = 1;		// these files were already transfered and checked for validity
				
				// add this string to the manifest list
				if (_cti_addString(this->file_files, l_ptr->str, f_ptr))
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
	stringEntry_t *	l_ptr;
	stringEntry_t *	o_ptr;
	fileEntry_t *	f_ptr;
	
	// sanity check
	if (m_ptr == NULL || s_ptr == NULL)
	{
		_cti_set_error("_cti_addManifestToSession: Invalid args.");
		return 1;
	}
	
	// copy all of the names to the session name lists
	
	if ((l_ptr = _cti_getEntries(m_ptr->exec_files)) != NULL)
	{
		// save for later
		o_ptr = l_ptr;
		while (l_ptr != NULL)
		{
			// check if this entry is in the list
			if (!_cti_searchStringList(s_ptr->exec_names, l_ptr->str))
			{
				// not in list, so add it
				
				// copy the data entry
				if (l_ptr->data)
				{
					f_ptr = _cti_copyFileEntry(l_ptr->data);
					if (f_ptr == NULL)
					{
						// error already set
						_cti_cleanupEntries(o_ptr);
						return 1;
					}
				} else
				{
					// failed to find a data entry for this string
					_cti_set_error("_cti_addManifestToSession: Missing data entry for string!");
					_cti_cleanupEntries(o_ptr);
					return 1;
				}
				
				f_ptr->present = 1;		// these files were already transfered and checked for validity
				
				if (_cti_addString(s_ptr->exec_names, l_ptr->str, f_ptr))
				{
					// failed to save name into the list
					_cti_set_error("_cti_addString() failed.");
					_cti_cleanupEntries(o_ptr);
					return 1;
				}
			}
			// increment pointers
			l_ptr = l_ptr->next;
		}
		// cleanup
		_cti_cleanupEntries(o_ptr);
	}
	
	if ((l_ptr = _cti_getEntries(m_ptr->lib_files)) != NULL)
	{
		// save for later
		o_ptr = l_ptr;
		while (l_ptr != NULL)
		{
			// check if this entry is in the list
			if (!_cti_searchStringList(s_ptr->lib_names, l_ptr->str))
			{
				// not in list, so add it
				
				// copy the data entry
				if (l_ptr->data)
				{
					f_ptr = _cti_copyFileEntry(l_ptr->data);
					if (f_ptr == NULL)
					{
						// error already set
						_cti_cleanupEntries(o_ptr);
						return 1;
					}
				} else
				{
					// failed to find a data entry for this string
					_cti_set_error("_cti_addManifestToSession: Missing data entry for string!");
					_cti_cleanupEntries(o_ptr);
					return 1;
				}
				
				f_ptr->present = 1;		// these files were already transfered and checked for validity
				
				if (_cti_addString(s_ptr->lib_names, l_ptr->str, f_ptr))
				{
					// failed to save name into the list
					_cti_set_error("_cti_addString() failed.");
					_cti_cleanupEntries(o_ptr);
					return 1;
				}
			}
			// increment pointers
			l_ptr = l_ptr->next;
		}
		// cleanup
		_cti_cleanupEntries(o_ptr);
	}
	
	if ((l_ptr = _cti_getEntries(m_ptr->libdir_files)) != NULL)
	{
		// save for later
		o_ptr = l_ptr;
		while (l_ptr != NULL)
		{
			// check if this entry is in the list
			if (!_cti_searchStringList(s_ptr->libdir_names, l_ptr->str))
			{
				// not in list, so add it
				
				// copy the data entry
				if (l_ptr->data)
				{
					f_ptr = _cti_copyFileEntry(l_ptr->data);
					if (f_ptr == NULL)
					{
						// error already set
						_cti_cleanupEntries(o_ptr);
						return 1;
					}
				} else
				{
					// failed to find a data entry for this string
					_cti_set_error("_cti_addManifestToSession: Missing data entry for string!");
					_cti_cleanupEntries(o_ptr);
					return 1;
				}
				
				f_ptr->present = 1;		// these files were already transfered and checked for validity
				
				if (_cti_addString(s_ptr->libdir_names, l_ptr->str, f_ptr))
				{
					// failed to save name into the list
					_cti_set_error("_cti_addString() failed.");
					_cti_cleanupEntries(o_ptr);
					return 1;
				}
			}
			// increment pointers
			l_ptr = l_ptr->next;
		}
		// cleanup
		_cti_cleanupEntries(o_ptr);
	}
	
	if ((l_ptr = _cti_getEntries(m_ptr->file_files)) != NULL)
	{
		// save for later
		o_ptr = l_ptr;
		while (l_ptr != NULL)
		{
			// check if this entry is in the list
			if (!_cti_searchStringList(s_ptr->file_names, l_ptr->str))
			{
				// not in list, so add it
				
				// copy the data entry
				if (l_ptr->data)
				{
					f_ptr = _cti_copyFileEntry(l_ptr->data);
					if (f_ptr == NULL)
					{
						// error already set
						_cti_cleanupEntries(o_ptr);
						return 1;
					}
				} else
				{
					// failed to find a data entry for this string
					_cti_set_error("_cti_addManifestToSession: Missing data entry for string!");
					_cti_cleanupEntries(o_ptr);
					return 1;
				}
				
				f_ptr->present = 1;		// these files were already transfered and checked for validity
				
				if (_cti_addString(s_ptr->file_names, l_ptr->str, f_ptr))
				{
					// failed to save name into the list
					_cti_set_error("_cti_addString() failed.");
					_cti_cleanupEntries(o_ptr);
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
	int				rtn;		// rtn value
	fileEntry_t *	f_ptr;		// pointer to file entry
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
	
	// search the exec_files string list for a duplicate filename
	rtn = _cti_searchStringList(m_ptr->exec_files, realname);
	if (rtn)
	{
		// filename has already been added - compare the entry against realpath to make
		// sure there isn't a naming conflict
		if ((f_ptr = (fileEntry_t *)_cti_lookupValue(m_ptr->exec_files, realname)) == NULL)
		{
			// no f_ptr, something is majorly wrong
			_cti_set_error("Internal: Null data for string entry!");
			// cleanup memory
			free(fullname);
			free(realname);
			return 1;
		}
		
		// sanity
		if (f_ptr->loc == NULL)
		{
			// loc is null, something is majorly wrong
			_cti_set_error("Internal: Null loc entry for f_ptr!");
			// cleanup memory
			free(fullname);
			free(realname);
			return 1;
		}
		
		// now make sure that the fullname matches the loc
		if (strncmp(f_ptr->loc, fullname, strlen(f_ptr->loc)))
		{
			// strings don't match
			_cti_set_error("A file named %s has already been added to the manifest.", realname);
			// cleanup memory
			free(fullname);
			free(realname);
			return 1;
		}
		
		// if we get here, the file locations match. No conflict, silently return.
		
		// cleanup memory
		free(fullname);
		free(realname);
	} else
	{
		// not found in list, so this is a unique file name

		// add realname to the names list
		if ((f_ptr = _cti_newFileEntry()) == NULL)
		{
			// error already set
			free(fullname);
			free(realname);
			return 1;
		}
		
		f_ptr->loc  = fullname;	// this will get free'ed later on
		f_ptr->present = 0;
		
		// add the string to the string list based on the realname of the file.
		// we want to avoid conflicts on the realname only.
		if (_cti_addString(m_ptr->exec_files, realname, f_ptr))
		{
			// failed to save name into the list
			_cti_set_error("_cti_addString() failed.");
			return 1;
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
		
		// set hasFiles to true
		m_ptr->hasFiles = 1;
		
		// cleanup - f_ptr is cleaned up later on
		free(realname);
	}
	
	return 0;
}

int
cti_addManifestLibrary(cti_manifest_id_t mid, const char *fstr)
{
	manifest_t *	m_ptr;		// pointer to the manifest_t object associated with the cti_manifest_id_t argument
	char *			fullname;	// full path name of the library to add to the manifest
	char *			realname;	// realname (lacking path info) of the library
	int				rtn;		// rtn value
	fileEntry_t *	f_ptr;		// pointer to file entry
	
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
	// this by pointing its LD_LIBRARY_PATH to a custom directory containing the conflicting lib.
	
	// search the string list for a duplicate filename
	rtn = _cti_searchStringList(m_ptr->lib_files, realname);
	if (rtn)
	{
		// filename has already been added - compare the entry against realpath to make
		// sure there isn't a naming conflict
		if ((f_ptr = (fileEntry_t *)_cti_lookupValue(m_ptr->lib_files, realname)) == NULL)
		{
			// no f_ptr, something is majorly wrong
			_cti_set_error("Internal: Null data for string entry!");
			// cleanup memory
			free(fullname);
			free(realname);
			return 1;
		}
		
		// sanity
		if (f_ptr->loc == NULL)
		{
			// loc is null, something is majorly wrong
			_cti_set_error("Internal: Null loc entry for f_ptr!");
			// cleanup memory
			free(fullname);
			free(realname);
			return 1;
		}
		
		// now make sure that the fullname matches the loc
		if (strncmp(f_ptr->loc, fullname, strlen(f_ptr->loc)))
		{
			// strings don't match
			_cti_set_error("A file named %s has already been added to the manifest.", realname);
			// cleanup memory
			free(fullname);
			free(realname);
			return 1;
		}
		
		// if we get here, the file locations match. No conflict, silently return.
		
		// cleanup memory
		free(fullname);
		free(realname);
	} else
	{
		// not found in list, so this is a unique file name

		// create auxilary f_ptr data structure
		if ((f_ptr = _cti_newFileEntry()) == NULL)
		{
			// error already set
			free(fullname);
			free(realname);
			return 1;
		}
		
		// setup auxillary data structure to put in the string list
		f_ptr->loc  = fullname;	// this will get free'ed later on
		f_ptr->present = 0;
		
		// add the string to the string list based on the realname of the file.
		// we want to avoid conflicts on the realname only.
		if (_cti_addString(m_ptr->lib_files, realname, f_ptr))
		{
			// failed to save name into the list
			_cti_set_error("_cti_addString() failed.");
			return 1;
		}
		
		// set hasFiles to true
		m_ptr->hasFiles = 1;
		
		// cleanup - f_ptr is cleaned up later on
		free(realname);
	}
	
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
	int				rtn;		// rtn value
	fileEntry_t *	f_ptr;		// pointer to file entry
	
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
	
	// search the string list for a duplicate filename
	rtn = _cti_searchStringList(m_ptr->libdir_files, realname);
	if (rtn)
	{
		// filename has already been added - compare the entry against realpath to make
		// sure there isn't a naming conflict
		if ((f_ptr = (fileEntry_t *)_cti_lookupValue(m_ptr->libdir_files, realname)) == NULL)
		{
			// no f_ptr, something is majorly wrong
			_cti_set_error("Internal: Null data for string entry!");
			// cleanup memory
			free(fullname);
			free(realname);
			return 1;
		}
		
		// sanity
		if (f_ptr->loc == NULL)
		{
			// loc is null, something is majorly wrong
			_cti_set_error("Internal: Null loc entry for f_ptr!");
			// cleanup memory
			free(fullname);
			free(realname);
			return 1;
		}
		
		// now make sure that the fullname matches the loc
		if (strncmp(f_ptr->loc, fullname, strlen(f_ptr->loc)))
		{
			// strings don't match
			_cti_set_error("A file named %s has already been added to the manifest.", realname);
			// cleanup memory
			free(fullname);
			free(realname);
			return 1;
		}
		
		// if we get here, the file locations match. No conflict, silently return.
		
		// cleanup memory
		free(fullname);
		free(realname);
	} else
	{
		// not found in list, so this is a unique file name

		// create auxilary f_ptr data structure
		if ((f_ptr = _cti_newFileEntry()) == NULL)
		{
			// error already set
			free(fullname);
			free(realname);
			return 1;
		}
		
		// setup auxillary data structure to put in the string list
		f_ptr->loc  = fullname;	// this will get free'ed later on
		f_ptr->present = 0;
		
		// add the string to the string list based on the realname of the file.
		// we want to avoid conflicts on the realname only.
		if (_cti_addString(m_ptr->libdir_files, realname, f_ptr))
		{
			// failed to save name into the list
			_cti_set_error("_cti_addString() failed.");
			return 1;
		}
		
		// set hasFiles to true
		m_ptr->hasFiles = 1;
		
		// cleanup - f_ptr is cleaned up later on
		free(realname);
	}
	
	return 0;
}

int
cti_addManifestFile(cti_manifest_id_t mid, const char *fstr)
{
	manifest_t *	m_ptr;		// pointer to the manifest_t object associated with the cti_manifest_id_t argument
	char *			fullname;	// full path name of the file to add to the manifest
	char *			realname;	// realname (lacking path info) of the file
	int				rtn;		// rtn value
	fileEntry_t *	f_ptr;		// pointer to file entry
	
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
	
	// search the string list for a duplicate filename
	rtn = _cti_searchStringList(m_ptr->file_files, realname);
	if (rtn)
	{
		// filename has already been added - compare the entry against realpath to make
		// sure there isn't a naming conflict
		if ((f_ptr = (fileEntry_t *)_cti_lookupValue(m_ptr->file_files, realname)) == NULL)
		{
			// no f_ptr, something is majorly wrong
			_cti_set_error("Internal: Null data for string entry!");
			// cleanup memory
			free(fullname);
			free(realname);
			return 1;
		}
		
		// sanity
		if (f_ptr->loc == NULL)
		{
			// loc is null, something is majorly wrong
			_cti_set_error("Internal: Null loc entry for f_ptr!");
			// cleanup memory
			free(fullname);
			free(realname);
			return 1;
		}
		
		// now make sure that the fullname matches the loc
		if (strncmp(f_ptr->loc, fullname, strlen(f_ptr->loc)))
		{
			// strings don't match
			_cti_set_error("A file named %s has already been added to the manifest.", realname);
			// cleanup memory
			free(fullname);
			free(realname);
			return 1;
		}
		
		// if we get here, the file locations match. No conflict, silently return.
		
		// cleanup memory
		free(fullname);
		free(realname);
	} else
	{
		// not found in list, so this is a unique file name

		// create auxilary f_ptr data structure
		if ((f_ptr = _cti_newFileEntry()) == NULL)
		{
			// error already set
			free(fullname);
			free(realname);
			return 1;
		}
		
		// setup auxillary data structure to put in the string list
		f_ptr->loc  = fullname;	// this will get free'ed later on
		f_ptr->present = 0;
		
		// add the string to the string list based on the realname of the file.
		// we want to avoid conflicts on the realname only.
		if (_cti_addString(m_ptr->file_files, realname, f_ptr))
		{
			// failed to save name into the list
			_cti_set_error("_cti_addString() failed.");
			return 1;
		}
		
		// set hasFiles to true
		m_ptr->hasFiles = 1;
		
		// cleanup - f_ptr is cleaned up later on
		free(realname);
	}
	
	return 0;
}

static int
_cti_copyFileToPackage(const char *loc, const char *name, const char *path)
{
	int				nr, nw;
	FILE *			f1;
	FILE *			f2;
	char *			name_path;
	char			buffer[BUFSIZ];
	struct stat		statbuf;

	// sanity check
	if (loc == NULL || name == NULL || path == NULL)
	{
		_cti_set_error("_cti_copyFileToPackage: invalid args.");
		return 1;
	}

	if ((f1 = fopen(loc, "r")) == NULL)
	{
		_cti_set_error("_cti_copyFileToPackage: fopen failed.");
		return 1;
	}
	
	// create the new name path
	if (asprintf(&name_path, "%s/%s", path, name) <= 0)
	{
		_cti_set_error("_cti_copyFileToPackage: asprintf failed.");
		fclose(f1);
		return 1;
	}
	
	if ((f2 = fopen(name_path, "w")) == NULL)
	{
		_cti_set_error("_cti_copyFileToPackage: fopen failed.");
		fclose(f1);
		free(name_path);
		return 1;
	}
		
	// read/write everything from f1/to f2
	while ((nr = fread(buffer, sizeof(char), BUFSIZ, f1)) > 0)
	{
		if ((nw = fwrite(buffer, sizeof(char), nr, f2)) != nr)
		{
			_cti_set_error("_cti_copyFileToPackage: fwrite failed.");
			fclose(f1);
			fclose(f2);
			free(name_path);
			return 1;
		}
	}
	
	// close the files
	fclose(f1);
	fclose(f2);
		
	// set the permissions of the new file to that of the old file
	if (stat(loc, &statbuf) == -1)
	{
		_cti_set_error("_cti_copyFileToPackage: Could not stat %s.", loc);
		free(name_path);
		return 1;
	}
	if (chmod(name_path, statbuf.st_mode) != 0)
	{
		_cti_set_error("_cti_copyFileToPackage: Could not chmod %s.", name_path);
		free(name_path);
		return 1;
	}
		
	// cleanup
	free(name_path);
	
	return 0;
}

// This will act as a rm -rf ...
static int
_cti_removeDirectory(char *path)
{
	DIR *			dir;
	struct dirent *	d;
	char *			name_path;
	struct stat		statbuf;

	// sanity check
	if (path == NULL)
	{
		_cti_set_error("_cti_removeDirectory: invalid args.");
		return 1;
	}
	
	// open the directory
	if ((dir = opendir(path)) == NULL)
	{
		_cti_set_error("_cti_removeDirectory: Could not opendir %s.", path);
		return 1;
	}
	
	// Recurse over every file in the directory
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
	
		// create the full path name
		if (asprintf(&name_path, "%s/%s", path, d->d_name) <= 0)
		{
			_cti_set_error("_cti_removeDirectory: asprintf failed.");
			closedir(dir);
			return 1;
		}
		
		// stat the file
		if (stat(name_path, &statbuf) == -1)
		{
			_cti_set_error("_cti_removeDirectory: Could not stat %s.", name_path);
			closedir(dir);
			free(name_path);
			return 1;
		}
		
		// if this is a directory we need to recursively call this function
		if (S_ISDIR(statbuf.st_mode))
		{
			if (_cti_removeDirectory(name_path))
			{
				// error already set
				closedir(dir);
				free(name_path);
				return 1;
			}
		} else
		{
			// remove the file
			if (remove(name_path))
			{
				_cti_set_error("_cti_removeDirectory: Could not remove %s.", name_path);
				closedir(dir);
				free(name_path);
				return 1;
			}
		}
		// done with this file
		free(name_path);
	}
	
	// done with the directory
	closedir(dir);
	
	// remove the directory
	if (remove(path))
	{
		_cti_set_error("_cti_removeDirectory: Could not remove %s.", path);
		return 1;
	}
	
	return 0;
}

// TODO: This could be made smarter by handling symlinks...
static int
_cti_copyDirectoryToPackage(const char *loc, const char *name, const char *path)
{
	DIR *			dir;
	struct dirent *	d;
	int				nr, nw;
	FILE *			f1;
	FILE *			f2;
	char *			target_path;
	char *			name_path;
	char *			target_name_path;
	char			buffer[BUFSIZ];
	struct stat		statbuf;

	// sanity check
	if (loc == NULL || name == NULL || path == NULL)
	{
		_cti_set_error("_cti_copyDirectoryToPackage: invalid args.");
		return 1;
	}
	
	// stat the reference directory
	if (stat(loc, &statbuf) == -1)
	{
		_cti_set_error("_cti_copyDirectoryToPackage: Could not stat %s.", loc);
		return 1;
	}
	
	// Open the reference directory
	if ((dir = opendir(loc)) == NULL)
	{
		_cti_set_error("_cti_copyDirectoryToPackage: Could not opendir %s.", loc);
		return 1;
	}
	
	// create the new target path
	if (asprintf(&target_path, "%s/%s", path, name) <= 0)
	{
		_cti_set_error("_cti_copyDirectoryToPackage: asprintf failed.");
		closedir(dir);
		return 1;
	}
	
	// create the new target directory
	if (mkdir(target_path, statbuf.st_mode))
	{
		_cti_set_error("_cti_copyDirectoryToPackage: mkdir failed.");
		closedir(dir);
		free(target_path);
		return 1;
	}
	
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
		
		// Create the name path to this file
		if (asprintf(&name_path, "%s/%s", loc, d->d_name) <= 0)
		{
			_cti_set_error("_cti_copyDirectoryToPackage: asprintf failed.");
			closedir(dir);
			_cti_removeDirectory(target_path);
			free(target_path);
			return 1;
		}
		
		// stat this file
		if (stat(name_path, &statbuf) == -1)
		{
			_cti_set_error("_cti_copyDirectoryToPackage: Could not stat %s.", name_path);
			closedir(dir);
			_cti_removeDirectory(target_path);
			free(target_path);
			free(name_path);
			return 1;
		}
		
		// If this is a directory, we need to recursively call this function to copy its contents
		if (S_ISDIR(statbuf.st_mode))
		{
			if (_cti_copyDirectoryToPackage(name_path, d->d_name, target_path))
			{
				// error already set
				closedir(dir);
				_cti_removeDirectory(target_path);
				free(target_path);
				free(name_path);
				return 1;
			}
		} else
		{
			// Otherwise we try to copy as usual
			
			// open the reference file
			if ((f1 = fopen(name_path, "r")) == NULL)
			{
				_cti_set_error("_cti_copyDirectoryToPackage: fopen failed.");
				closedir(dir);
				_cti_removeDirectory(target_path);
				free(target_path);
				free(name_path);
				return 1;
			}
			
			// create the new name path
			if (asprintf(&target_name_path, "%s/%s", target_path, d->d_name) <= 0)
			{
				_cti_set_error("_cti_copyDirectoryToPackage: asprintf failed.");
				closedir(dir);
				_cti_removeDirectory(target_path);
				free(target_path);
				free(name_path);
				return 1;
			}
			
			// open the target file
			if ((f2 = fopen(target_name_path, "w")) == NULL)
			{
				_cti_set_error("_cti_copyDirectoryToPackage: fopen failed.");
				closedir(dir);
				_cti_removeDirectory(target_path);
				free(target_path);
				free(name_path);
				fclose(f1);
				free(target_name_path);
				return 1;
			}
			
			// read/write everything from f1/to f2
			while ((nr = fread(buffer, sizeof(char), BUFSIZ, f1)) > 0)
			{
				if ((nw = fwrite(buffer, sizeof(char), nr, f2)) != nr)
				{
					_cti_set_error("_cti_copyDirectoryToPackage: fwrite failed.");
					closedir(dir);
					fclose(f1);
					fclose(f2);
					_cti_removeDirectory(target_path);
					free(target_path);
					free(name_path);
					free(target_name_path);
					return 1;
				}
			}
			
			// close the files
			fclose(f1);
			fclose(f2);
			
			// set the permissions of the new file to that of the old file
			if (chmod(target_name_path, statbuf.st_mode) != 0)
			{
				_cti_set_error("_cti_copyDirectoryToPackage: Could not chmod %s.", name_path);
				closedir(dir);
				_cti_removeDirectory(target_path);
				free(target_path);
				free(name_path);
				free(target_name_path);
				return 1;
			}
			
			// cleanup
			free(target_name_path);
		}
		
		// cleanup
		free(name_path);
	}
	
	// done
	closedir(dir);
	free(target_path);
	
	return 0;
}

static int
_cti_packageManifestAndShip(appEntry_t *app_ptr, manifest_t *m_ptr)
{
	const char *			cfg_dir = NULL;		// tmp directory
	char *					stage_dir = NULL;	// staging directory name
	char *					stage_path = NULL;	// staging path
	char *					bin_path = NULL;
	char *					lib_path = NULL;
	char *					tmp_path = NULL;
	const char * const *	wlm_files;
	stringEntry_t *			l_ptr;
	stringEntry_t *			o_ptr;
	fileEntry_t *			f_ptr;
	char *					tar_name = NULL;
	char *					tmp_tar_name;
	struct archive *		a = NULL;
	struct archive *		disk = NULL;
	struct archive_entry *	entry;
	ssize_t					len;
	int						r, fd;
	char 					buff[16384];
	char *					t = NULL;
	char *					orig_path;
	
	// sanity check
	if (app_ptr == NULL || m_ptr == NULL)
	{
		_cti_set_error("_cti_packageManifestAndShip: invalid args.");
		return 1;
	}
	
	// sanity check
	if (m_ptr->hasFiles == 0)
	{
		_cti_set_error("_cti_packageManifestAndShip: Nothing to ship!");
		return 1;
	}
	
	// Get the configuration directory
	if ((cfg_dir = _cti_getCfgDir()) == NULL)
	{
		// error already set
		return 1;
	}
	
	// Check the manifest to see if it already has a stage_name set, if so this is part of an existing
	// session and we should use the same directory name
	if (m_ptr->stage_name == NULL)
	{
		// check to see if the caller set a staging directory name, otherwise create a unique one for them
		if ((stage_dir = getenv(DAEMON_STAGE_VAR)) == NULL)
		{
			// take the default action
			if (asprintf(&stage_path, "%s/%s", cfg_dir, DEFAULT_STAGE_DIR) <= 0)
			{
				_cti_set_error("_cti_packageManifestAndShip: asprintf failed.");
				goto packageManifestAndShip_error;
			}
		
			// create the temporary directory for the manifest package
			if (mkdtemp(stage_path) == NULL)
			{
				_cti_set_error("_cti_packageManifestAndShip: mkdtemp failed.");
				goto packageManifestAndShip_error;
			}
		} else
		{
			// use the user defined directory
			if (asprintf(&stage_path, "%s/%s", cfg_dir, stage_dir) <= 0)
			{
				_cti_set_error("_cti_packageManifestAndShip: asprintf failed.");
				goto packageManifestAndShip_error;
			}
		
			if (mkdir(stage_path, S_IRWXU))
			{
				_cti_set_error("_cti_packageManifestAndShip: mkdir failed.");
				goto packageManifestAndShip_error;
			}
		}
	
		// get the stage name since we want to rebase things in the tarball
		// save this in the m_ptr for later
		m_ptr->stage_name = _cti_pathToName(stage_path);
	} else
	{
		// use existing name
		if (asprintf(&stage_path, "%s/%s", cfg_dir, m_ptr->stage_name) <= 0)
		{
			_cti_set_error("_cti_packageManifestAndShip: asprintf failed.");
			goto packageManifestAndShip_error;
		}
		
		if (mkdir(stage_path, S_IRWXU))
		{
			_cti_set_error("_cti_packageManifestAndShip: mkdir failed.");
			goto packageManifestAndShip_error;
		}
	}
	
	// now create the required subdirectories
	if (asprintf(&bin_path, "%s/bin", stage_path) <= 0)
	{
		_cti_set_error("_cti_packageManifestAndShip: asprintf failed.");
		goto packageManifestAndShip_error;
	}
	if (asprintf(&lib_path, "%s/lib", stage_path) <= 0)
	{
		_cti_set_error("_cti_packageManifestAndShip: asprintf failed.");
		goto packageManifestAndShip_error;
	}
	if (asprintf(&tmp_path, "%s/tmp", stage_path) <= 0)
	{
		_cti_set_error("_cti_packageManifestAndShip: asprintf failed.");
		goto packageManifestAndShip_error;
	}
	if (mkdir(bin_path, S_IRWXU))
	{
		_cti_set_error("_cti_packageManifestAndShip: mkdir failed.");
		goto packageManifestAndShip_error;
	}
	if (mkdir(lib_path,  S_IRWXU))
	{
		_cti_set_error("_cti_packageManifestAndShip: mkdir failed.");
		goto packageManifestAndShip_error;
	}
	if (mkdir(tmp_path, S_IRWXU))
	{
		_cti_set_error("_cti_packageManifestAndShip: mkdir failed.");
		goto packageManifestAndShip_error;
	}
	
	// Process file lists based on the WLM now that it is known
	
	// grab any extra wlm binaries if this is the first instance
	if (m_ptr->inst == 1)
	{
		if ((wlm_files = app_ptr->wlmProto->wlm_extraBinaries()) != NULL)
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
	}
	
	if ((l_ptr = _cti_getEntries(m_ptr->exec_files)) != NULL)
	{
		// save for later
		o_ptr = l_ptr;
		while (l_ptr != NULL)
		{
			// Check this binary for validity
			f_ptr = l_ptr->data;
			
			if (f_ptr == NULL)
			{
				_cti_set_error("_cti_packageManifestAndShip: Null data for string entry!");
				_cti_cleanupEntries(o_ptr);
				goto packageManifestAndShip_error;
			}
			
			// Only check file if it needs to be shipped
			if (!f_ptr->present)
			{
				if (app_ptr->wlmProto->wlm_verifyBinary(l_ptr->str))
				{
					// this file is not valid
					
					// increment pointers
					l_ptr = l_ptr->next;
					
					continue;
				}
				
				// copy this file to the package
				if (_cti_copyFileToPackage(f_ptr->loc, l_ptr->str, bin_path))
				{
					// error string already set
					_cti_cleanupEntries(o_ptr);
					goto packageManifestAndShip_error;
				}
			}
			
			// increment pointers
			l_ptr = l_ptr->next;
		}
		// cleanup
		_cti_cleanupEntries(o_ptr);
	}

	// grab any extra libraries if this is the first instance
	if (m_ptr->inst == 1)
	{
		if ((wlm_files = app_ptr->wlmProto->wlm_extraLibraries()) != NULL)
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
	}

	if ((l_ptr = _cti_getEntries(m_ptr->lib_files)) != NULL)
	{
		// save for later
		o_ptr = l_ptr;
		while (l_ptr != NULL)
		{
			// Check this binary for validity
			f_ptr = l_ptr->data;
			
			if (f_ptr == NULL)
			{
				_cti_set_error("_cti_packageManifestAndShip: Null data for string entry!");
				_cti_cleanupEntries(o_ptr);
				goto packageManifestAndShip_error;
			}
			
			// Only check file if it needs to be shipped
			if (!f_ptr->present)
			{
				if (app_ptr->wlmProto->wlm_verifyBinary(l_ptr->str))
				{
					// this file is not valid
					
					// increment pointers
					l_ptr = l_ptr->next;
					
					continue;
				}
				
				// copy this file to the package
				if (_cti_copyFileToPackage(f_ptr->loc, l_ptr->str, lib_path))
				{
					// error string already set
					_cti_cleanupEntries(o_ptr);
					goto packageManifestAndShip_error;
				}
			}
			
			// increment pointers
			l_ptr = l_ptr->next;
		}
		// cleanup
		_cti_cleanupEntries(o_ptr);
	}

	// grab any extra library directories if this is the first instance
	if (m_ptr->inst == 1)
	{
		if ((wlm_files = app_ptr->wlmProto->wlm_extraLibDirs()) != NULL)
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
	}

	if ((l_ptr = _cti_getEntries(m_ptr->libdir_files)) != NULL)
	{
		// save for later
		o_ptr = l_ptr;
		while (l_ptr != NULL)
		{
			// Check this binary for validity
			f_ptr = l_ptr->data;
			
			if (f_ptr == NULL)
			{
				_cti_set_error("_cti_packageManifestAndShip: Null data for string entry!");
				_cti_cleanupEntries(o_ptr);
				goto packageManifestAndShip_error;
			}
			
			// Only check file if it needs to be shipped
			if (!f_ptr->present)
			{
				if (app_ptr->wlmProto->wlm_verifyBinary(l_ptr->str))
				{
					// this file is not valid
					
					// increment pointers
					l_ptr = l_ptr->next;
					
					continue;
				}
				
				// copy this directory to the package
				if (_cti_copyDirectoryToPackage(f_ptr->loc, l_ptr->str, lib_path))
				{
					// error string already set
					_cti_cleanupEntries(o_ptr);
					goto packageManifestAndShip_error;
				}
			}
			
			// increment pointers
			l_ptr = l_ptr->next;
		}
		// cleanup
		_cti_cleanupEntries(o_ptr);
	}
	
	// grab any extra files if this is the first instance
	if (m_ptr->inst == 1)
	{
		if ((wlm_files = app_ptr->wlmProto->wlm_extraFiles()) != NULL)
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
	
	if ((l_ptr = _cti_getEntries(m_ptr->file_files)) != NULL)
	{
		// save for later
		o_ptr = l_ptr;
		while (l_ptr != NULL)
		{
			// Check this binary for validity
			f_ptr = l_ptr->data;
			
			if (f_ptr == NULL)
			{
				_cti_set_error("_cti_packageManifestAndShip: Null data for string entry!");
				_cti_cleanupEntries(o_ptr);
				goto packageManifestAndShip_error;
			}
			
			// Only check file if it needs to be shipped
			if (!f_ptr->present)
			{
				if (app_ptr->wlmProto->wlm_verifyBinary(l_ptr->str))
				{
					// this file is not valid
					
					// increment pointers
					l_ptr = l_ptr->next;
					
					continue;
				}
				
				// copy this file to the package
				if (_cti_copyFileToPackage(f_ptr->loc, l_ptr->str, stage_path))
				{
					// error string already set
					_cti_cleanupEntries(o_ptr);
					goto packageManifestAndShip_error;
				}
			}
			
			// increment pointers
			l_ptr = l_ptr->next;
		}
		// cleanup
		_cti_cleanupEntries(o_ptr);
	}
	
	// create the tarball name
	if (asprintf(&tar_name, "%s.tar", stage_path) <= 0)
	{
		_cti_set_error("_cti_packageManifestAndShip: asprintf failed.");
		goto packageManifestAndShip_error;
	}
	
	// Create the tarball
	a = archive_write_new();
	archive_write_add_filter_none(a);
	
	// Fix for BUG 811393 - use gnutar instead of ustar.
	archive_write_set_format_gnutar(a);
	
	archive_write_open_filename(a, tar_name);
	
	disk = archive_read_disk_new();
	
	if (archive_read_disk_open(disk, stage_path) != ARCHIVE_OK)
	{
		_cti_set_error("%s", archive_error_string(disk));
		goto packageManifestAndShip_error;
	}
		
	for (;;)
	{
		entry = archive_entry_new();
		r = archive_read_next_header2(disk, entry);
		if (r == ARCHIVE_EOF)
		{
			archive_entry_free(entry);
			break;
		}
		if (r != ARCHIVE_OK)
		{
			_cti_set_error("%s", archive_error_string(disk));
			archive_entry_free(entry);
			goto packageManifestAndShip_error;
		}
		archive_read_disk_descend(disk);
		
		// rebase the pathname in the header, we need to first grab the existing
		// pathanme to pass to open.
		orig_path = strdup(archive_entry_pathname(entry));
		// point at the start of the rebased path
		if ((t = strstr(orig_path, m_ptr->stage_name)) == NULL)
		{
			_cti_set_error("_cti_packageManifestAndShip: Could not find base name for tar.");
			free(orig_path);
			archive_entry_free(entry);
			goto packageManifestAndShip_error;
		}
		// Set the pathanme in the entry
		archive_entry_set_pathname(entry, t);
		
		r = archive_write_header(a, entry);
		if (r < ARCHIVE_OK)
		{
			_cti_set_error("%s", archive_error_string(a));
			free(orig_path);
			archive_entry_free(entry);
			goto packageManifestAndShip_error;
		}
		if (r == ARCHIVE_FATAL)
		{
			_cti_set_error("_cti_packageManifestAndShip: ARCHIVE_FATAL error occured.");
			free(orig_path);
			archive_entry_free(entry);
			goto packageManifestAndShip_error;
		}
		if (r > ARCHIVE_FAILED)
		{
			fd = open(orig_path, O_RDONLY);
			len = read(fd, buff, sizeof(buff));
			while (len > 0)
			{
				if (archive_write_data(a, buff, len) == -1)
				{
					_cti_set_error("_cti_packageManifestAndShip: archive_write_data() error occured.");
					close(fd);
					free(orig_path);
					archive_entry_free(entry);
					goto packageManifestAndShip_error;
				}
				len = read(fd, buff, sizeof(buff));
			}
			close(fd);
		}
		
		// cleanup
		free(orig_path);
		archive_entry_free(entry);
	}
	
	archive_read_close(disk);
	archive_read_free(disk);
	disk = NULL;
	
	archive_write_close(a);
	archive_write_free(a);
	a = NULL;
	
	// rename the existing tarball based on its instance to prevent a race 
	// condition where the dlaunch on the compute node has not yet extracted the 
	// previously shipped tarball and we overwrite it with this new
	// one.
	
	// create the temp tarball name
	if (asprintf(&tmp_tar_name, "%s%d.tar", stage_path, m_ptr->inst) <= 0)
	{
		_cti_set_error("_cti_packageManifestAndShip: asprintf failed.");
		goto packageManifestAndShip_error;
	}
	
	// move the tarball
	if (rename(tar_name, tmp_tar_name))
	{
		_cti_set_error("_cti_packageManifestAndShip: Failed to rename tarball to %s.", tmp_tar_name);
		free(tmp_tar_name);
		goto packageManifestAndShip_error;
	}
	
	// set the tar_name to the tmp_tar_name
	free(tar_name);
	tar_name = tmp_tar_name;
	
	// Call the appropriate transfer function based on the wlm
	if (app_ptr->wlmProto->wlm_shipPackage(app_ptr->_wlmObj, tar_name))
	{
		// we failed to ship the file to the compute nodes for some reason - catastrophic failure
		// Error message already set
		goto packageManifestAndShip_error;
	}
	
	// clean things up
	free(bin_path);
	free(lib_path);
	free(tmp_path);
	if (_cti_removeDirectory(stage_path))
	{
		// Normally we don't want to print to stderr, but in this case we should at least try
		// to do something since we don't return with a warning status.
		fprintf(stderr, "Failed to remove files from %s, please remove manually.\n", stage_path);
	}
	free(stage_path);
	remove(tar_name);
	free(tar_name);
	
	return 0;
	
	// Error handling code starts below
	// Note that the error string has already been set
	
packageManifestAndShip_error:

	// Attempt to cleanup the archive stuff just in case it has been created already
	if (disk != NULL)
	{
		archive_read_close(disk);
		archive_read_free(disk);
	}
	if (a != NULL)
	{
		archive_write_close(a);
		archive_write_free(a);
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
	// Try to remove any files already copied
	if (stage_path != NULL)
	{
		if (_cti_removeDirectory(stage_path))
		{
			// Normally we don't want to print to stderr, but in this case we should at least try
			// to do something since we don't return with a warning status.
			fprintf(stderr, "Failed to remove files from %s, please remove manually.\n", stage_path);
		}
		free(stage_path);
	}
	if (tar_name != NULL)
	{
		remove(tar_name);
		free(tar_name);
	}
	return 1;
}

cti_session_id_t
cti_sendManifest(cti_app_id_t appId, cti_manifest_id_t mid, int dbg)
{
	appEntry_t *		app_ptr;			// pointer to the appEntry_t object associated with the provided aprun pid
	manifest_t *		m_ptr;				// pointer to the manifest_t object associated with the cti_manifest_id_t argument
	char *				jid_str;			// job identifier string - wlm specific
	cti_args_t *		d_args;				// args to pass to the daemon launcher
	session_t *			s_ptr = NULL;		// points at the session to return
	cti_session_id_t	rtn;
	int					trnsfr = 1;			// should we transfer the dlaunch?

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
	
	// if _transfer_init is set in the app entry object, there is no need to send dlaunch
	// a second time
	if (app_ptr->_transfer_init)
	{
		trnsfr = 0;
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
	
	// Ensure that there are files to ship, otherwise there is no need to ship a
	// tarball, everything we need already has been transfered to the nodes
	if (m_ptr->hasFiles)
	{
		// ship the manifest tarball to the compute nodes
		if (_cti_packageManifestAndShip(app_ptr, m_ptr))
		{
			// Failed to ship the manifest - catastrophic failure
			// error string already set
			_cti_reapManifest(m_ptr->mid);
			return 0;
		}
	} else
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
	
	// Ensure toolPath is not null
	if (app_ptr->toolPath == NULL)
	{
		_cti_set_error("Tool daemon path information is missing!");
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
	
	if (_cti_addArg(d_args, "-a %s", jid_str))
	{
		_cti_set_error("_cti_addArg failed.");
		_cti_reapManifest(m_ptr->mid);
		free(jid_str);
		_cti_freeArgs(d_args);
		return 0;
	}
	free(jid_str);
	
	if (_cti_addArg(d_args, "-p %s", app_ptr->toolPath))
	{
		_cti_set_error("_cti_addArg failed.");
		_cti_reapManifest(m_ptr->mid);
		_cti_freeArgs(d_args);
		return 0;
	}
	
	if (_cti_addArg(d_args, "-w %d", app_ptr->wlmProto->wlm_type))
	{
		_cti_set_error("_cti_addArg failed.");
		_cti_reapManifest(m_ptr->mid);
		_cti_freeArgs(d_args);
		return 0;
	}
	
	if (_cti_addArg(d_args, "-m %s%d.tar", m_ptr->stage_name, m_ptr->inst))
	{
		_cti_set_error("_cti_addArg failed.");
		_cti_reapManifest(m_ptr->mid);
		_cti_freeArgs(d_args);
		return 0;
	}
	
	if (_cti_addArg(d_args, "-d %s", m_ptr->stage_name))
	{
		_cti_set_error("_cti_addArg failed.");
		_cti_reapManifest(m_ptr->mid);
		_cti_freeArgs(d_args);
		return 0;
	}
	
	if (_cti_addArg(d_args, "-i %d", m_ptr->inst))
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
	if (app_ptr->wlmProto->wlm_startDaemon(app_ptr->_wlmObj, trnsfr, app_ptr->toolPath, d_args))
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
	// set the tranfser_init in the app_ptr
	app_ptr->_transfer_init = 1;
	// point the toolPath of the session at the value in the app_ptr
	s_ptr->toolPath = app_ptr->toolPath;
	
	return s_ptr->sid;
}

cti_session_id_t
cti_execToolDaemon(cti_app_id_t appId, cti_manifest_id_t mid, cti_session_id_t sid, const char *daemon, const char * const args[], const char * const env[], int dbg)
{
	appEntry_t *	app_ptr;			// pointer to the appEntry_t object associated with the provided aprun pid
	manifest_t *	m_ptr;				// pointer to the manifest_t object associated with the cti_manifest_id_t argument
	int				useManif = 0;		// controls if a manifest was shipped or not
	char *			fullname;			// full path name of the executable to launch as a tool daemon
	char *			realname;			// realname (lacking path info) of the executable
	char *			jid_str;			// job id string to pass to the backend. This is wlm specific.
	cti_args_t *	d_args;				// args to pass to the daemon launcher
	session_t *		s_ptr = NULL;		// points at the session to return
	int				trnsfr = 1;			// should we transfer the dlaunch?
		
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
	
	// if _transfer_init is set in the app entry object, there is no need to send dlaunch
	// a second time
	if (app_ptr->_transfer_init)
	{
		trnsfr = 0;
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
	if (m_ptr->hasFiles)
	{
		// ship the manifest tarball to the compute nodes
		if (_cti_packageManifestAndShip(app_ptr, m_ptr))
		{
			// Failed to ship the manifest - catastrophic failure
			// error string already set
			_cti_reapManifest(m_ptr->mid);
			return 0;
		}
		++useManif;
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
	
	// Ensure toolPath is not null
	if (app_ptr->toolPath == NULL)
	{
		_cti_set_error("Tool daemon path information is missing!");
		_cti_reapManifest(m_ptr->mid);
		free(jid_str);
		free(realname);
		return 0;
	}
	
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
	
	if (_cti_addArg(d_args, "-a %s", jid_str))
	{
		_cti_set_error("_cti_addArg failed.");
		_cti_reapManifest(m_ptr->mid);
		free(jid_str);
		free(realname);
		_cti_freeArgs(d_args);
		return 0;
	}
	free(jid_str);
	
	if (_cti_addArg(d_args, "-p %s", app_ptr->toolPath))
	{
		_cti_set_error("_cti_addArg failed.");
		_cti_reapManifest(m_ptr->mid);
		free(realname);
		_cti_freeArgs(d_args);
		return 0;
	}
	
	if (_cti_addArg(d_args, "-w %d", app_ptr->wlmProto->wlm_type))
	{
		_cti_set_error("_cti_addArg failed.");
		_cti_reapManifest(m_ptr->mid);
		free(realname);
		_cti_freeArgs(d_args);
		return 0;
	}
	
	if (_cti_addArg(d_args, "-b %s", realname))
	{
		_cti_set_error("_cti_addArg failed.");
		_cti_reapManifest(m_ptr->mid);
		free(realname);
		_cti_freeArgs(d_args);
		return 0;
	}
	free(realname);
	
	if (_cti_addArg(d_args, "-d %s", m_ptr->stage_name))
	{
		_cti_set_error("_cti_addArg failed.");
		_cti_reapManifest(m_ptr->mid);
		_cti_freeArgs(d_args);
		return 0;
	}
	
	if (_cti_addArg(d_args, "-i %d", m_ptr->inst))
	{
		_cti_set_error("_cti_addArg failed.");
		_cti_reapManifest(m_ptr->mid);
		_cti_freeArgs(d_args);
		return 0;
	}
	
	// add -m argument if needed
	if (useManif)
	{
		if (_cti_addArg(d_args, "-m %s%d.tar", m_ptr->stage_name, m_ptr->inst))
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
			// add this env arg
			if (_cti_addArg(d_args, "-e %s", *env++))
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
	if (app_ptr->wlmProto->wlm_startDaemon(app_ptr->_wlmObj, trnsfr, app_ptr->toolPath, d_args))
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
	// set the tranfser_init in the app_ptr
	app_ptr->_transfer_init = 1;
	// point the toolPath of the session at the value in the app_ptr
	s_ptr->toolPath = app_ptr->toolPath;
	
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

