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
#include "useful.h"

/* Types used here */

typedef struct
{
	cti_manifest_id_t	mid;			// manifest id
	cti_session_id_t	sid;			// optional session id
	int					inst;			// instance number - used with session to prevent tarball name conflicts
	char *				stage_name;		// basename of the manifest directory
	stringList_t *		exec_names;		// list of manifest binary names
	stringList_t *		lib_names;		// list of manifest dso names
	stringList_t *		file_names;		// list of manifest regular file names
	stringList_t *		exec_loc;		// fullpath of manifest binaries
	stringList_t *		lib_loc;		// fullpath of manifest libaries
	stringList_t *		file_loc;		// fullpath of manifest files
} manifest_t;

typedef struct
{
	cti_session_id_t	sid;			// session id
	int					instCnt;		// instance count - set in the manifest to prevent naming conflicts
	char *				stage_name;		// basename of the manifest directory
	char *				toolPath;		// toolPath of the app entry - DO NOT FREE THIS!!!
	stringList_t *		exec_names;		// list of manifest binary names
	stringList_t *		lib_names;		// list of manifest dso names
	stringList_t *		file_names;		// list of manifest regular file names
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

/* 
** This list may need to be updated with each new release of CNL.
** FIXME: This needs to be WLM specific.
*/
static const char * _cti_ignored_libs[] = {
	"libdl.so.2",
	"libc.so.6",
	"libvolume_id.so.1",
	"libcidn.so.1",
	"libnsl.so.1",
	"librt.so.1",
	"libutil.so.1",
	"libpthread.so.0",
	"libudev.so.0",
	"libcrypt.so.1",
	"libz.so.1",
	"libm.so.6",
	"libnss_files.so.2",
	NULL
};

/* Static prototypes */
static sessList_t *		_cti_growSessList(void);
static void				_cti_reapSessList(void);
static void				_cti_reapSession(cti_session_id_t);
static void				_cti_consumeSession(session_t *);
static session_t *		_cti_findSession(cti_session_id_t);
static session_t *		_cti_newSession(cti_manifest_id_t);
static manifList_t *	_cti_growManifList(void);
static void				_cti_reapManifList(void);
static void				_cti_reapManifest(cti_manifest_id_t);
static void				_cti_consumeManifest(manifest_t *);
static manifest_t *		_cti_findManifest(cti_manifest_id_t);
static manifest_t *		_cti_newManifest(cti_session_id_t);
static int				_cti_addManifestToSession(cti_manifest_id_t, cti_session_id_t);
static void				_cti_addSessionToApp(appEntry_t *, cti_session_id_t);
static int				_cti_removeFilesFromDir(char *);
static int				_cti_copyFilesToPackage(stringList_t *, char *);
static int				_cti_packageManifestAndShip(uint64_t, cti_manifest_id_t);

/* global variables */
static sessList_t *			_cti_my_sess	= NULL;
static cti_session_id_t		_cti_next_sid	= 1;
static manifList_t *		_cti_my_manifs	= NULL;
static cti_manifest_id_t	_cti_next_mid	= 1;

static sessList_t *
_cti_growSessList(void)
{
	sessList_t *	newEntry;
	sessList_t *	lstPtr;
	
	// alloc space for the new list entry
	if ((newEntry = malloc(sizeof(sessList_t))) == (void *)0)
	{
		_cti_set_error("malloc failed.");
		return NULL;
	}
	memset(newEntry, 0, sizeof(sessList_t));     // clear it to NULL
	
	// if _cti_my_sess is null, this is the new head of the list
	if ((lstPtr = _cti_my_sess) == NULL)
	{
		_cti_my_sess = newEntry;
	} else
	{
		// we need to iterate through the list to find the open next entry
		while (lstPtr->nextEntry != NULL)
		{
			lstPtr = lstPtr->nextEntry;
		}
		lstPtr->nextEntry = newEntry;
	}
	
	// return the pointer to the new entry
	return newEntry;
}

// this function is used to undo the operation performed by _cti_growSessList
static void
_cti_reapSessList(void)
{
	sessList_t *	lstPtr;

	// sanity check
	if ((lstPtr = _cti_my_sess) == NULL)
		return;
	
	// if this was the first entry, lets remove it
	if (lstPtr->nextEntry == NULL)
	{
		free(lstPtr);
		_cti_my_sess = NULL;
		return;
	}
	
	// iterate through until we find the entry whos next entry has a null next entry ;)
	// i.e. magic - this works because _cti_growSessList always places the new session_t entry
	// at the end of the list
	while (lstPtr->nextEntry->nextEntry != NULL)
	{
		lstPtr = lstPtr->nextEntry;
	}
	// the next entry has a null next entry so we need to free the next entry
	free(lstPtr->nextEntry);
	// now we need to set this entries next entry to null
	lstPtr->nextEntry = NULL;
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
	while (lstPtr->thisEntry == NULL)
	{
		// if this is the only object in the list, then delete the entire list
		if ((lstPtr = lstPtr->nextEntry) == NULL)
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
	while (lstPtr->thisEntry->sid != sid)
	{
		prePtr = lstPtr;
		if ((lstPtr = lstPtr->nextEntry) == NULL)
		{
			// there are no more entries and we didn't find the sid
			return;
		}
	}
	
	// check to see if this was the first entry in the global _cti_my_sess list
	if (prePtr == lstPtr)
	{
		// point the global _cti_my_sess list to the next entry
		_cti_my_sess = lstPtr->nextEntry;
		// consume the session_t object for this entry in the list
		_cti_consumeSession(lstPtr->thisEntry);
		// free the list object
		free(lstPtr);
	} else
	{
		// we are at some point midway through the global _cti_my_sess list
		
		// point the previous entries next entry to the list pointers next entry
		// this bypasses the current list pointer
		prePtr->nextEntry = lstPtr->nextEntry;
		// consume the session_t object for this entry in the list
		_cti_consumeSession(lstPtr->thisEntry);
		// free the list object
		free(lstPtr);
	}
	
	// done
	return;
}

static void
_cti_consumeSession(session_t *entry)
{
	// sanity check
	if (entry == NULL)
		return;
		
	// free the basename of the manifest directory
	if (entry->stage_name != NULL)
		free(entry->stage_name);
	
	// eat each of the string lists
	_cti_consumeStringList(entry->exec_names);
	_cti_consumeStringList(entry->lib_names);
	_cti_consumeStringList(entry->file_names);
	
	// nom nom the final session_t object
	free(entry);
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
	while (lstPtr->thisEntry == NULL)
	{
		// if this is the only object in the list, then delete the entire list
		if ((lstPtr = lstPtr->nextEntry) == NULL)
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
	while (lstPtr->thisEntry->sid != sid)
	{
		if ((lstPtr = lstPtr->nextEntry) == NULL)
		{
			// there are no more entries and we didn't find the sid
			_cti_set_error("cti_session_id_t %d does not exist.", (int)sid);
			return NULL;
		}
	}
	
	return lstPtr->thisEntry;
}

static session_t *
_cti_newSession(cti_manifest_id_t mid)
{
	sessList_t *	lstPtr;
	session_t *		this;
	manifest_t *	m_ptr;
	int				i;
	char **			str_ptr;
	
	// ensure the mid exists
	if ((m_ptr = _cti_findManifest(mid)) == NULL)
	{
		// We failed to find the manifest for the mid
		// error string already set
		return NULL;
	}
	
	// grow the global _cti_my_manifs list and get its new manifest_t entry
	if ((lstPtr = _cti_growSessList()) == NULL)
	{
		// error string already set
		return NULL;
	}
	
	// create the new session_t object
	if ((this = malloc(sizeof(session_t))) == (void *)0)
	{
		_cti_set_error("malloc failed.");
		_cti_reapSessList();
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
		_cti_reapSessList();
		_cti_consumeSession(this);
		return NULL;
	}
	if ((this->lib_names = _cti_newStringList()) == NULL)
	{
		_cti_set_error("_cti_newStringList() failed.");
		_cti_reapSessList();
		_cti_consumeSession(this);
		return NULL;
	}
	if ((this->file_names = _cti_newStringList()) == NULL)
	{
		_cti_set_error("_cti_newStringList() failed.");
		_cti_reapSessList();
		_cti_consumeSession(this);
		return NULL;
	}
	
	// copy the names in each list of the manifest over to the session list
	i = m_ptr->exec_names->num;
	str_ptr = m_ptr->exec_names->list;
	while (0 < i--)
	{
		if (_cti_addString(this->exec_names, *str_ptr))
		{
			// failed to save str_ptr into the list
			_cti_set_error("_cti_addString() failed.");
			_cti_reapSessList();
			_cti_consumeSession(this);
			return NULL;
		}
		
		// increment str_ptr
		++str_ptr;
	}
	i = m_ptr->lib_names->num;
	str_ptr = m_ptr->lib_names->list;
	while (0 < i--)
	{
		if (_cti_addString(this->lib_names, *str_ptr))
		{
			// failed to save str_ptr into the list
			_cti_set_error("_cti_addString() failed.");
			_cti_reapSessList();
			_cti_consumeSession(this);
			return NULL;
		}
		
		// increment str_ptr
		++str_ptr;
	}
	i = m_ptr->file_names->num;
	str_ptr = m_ptr->file_names->list;
	while (0 < i--)
	{
		if (_cti_addString(this->file_names, *str_ptr))
		{
			// failed to save str_ptr into the list
			_cti_set_error("_cti_addString() failed.");
			_cti_reapSessList();
			_cti_consumeSession(this);
			return NULL;
		}
		
		// increment str_ptr
		++str_ptr;
	}
	
	// save the new session_t object into the returned sessList_t object that
	// the call to _cti_growSessList gave us.
	lstPtr->thisEntry = this;
	
	return this;
}

static manifList_t *
_cti_growManifList(void)
{
	manifList_t *	newEntry;
	manifList_t *	lstPtr;
	
	// alloc space for the new list entry
	if ((newEntry = malloc(sizeof(manifList_t))) == (void *)0)
	{
		_cti_set_error("malloc failed.");
		return NULL;
	}
	memset(newEntry, 0, sizeof(manifList_t));     // clear it to NULL
	
	// if _cti_my_manifs is null, this is the new head of the list
	if ((lstPtr = _cti_my_manifs) == NULL)
	{
		_cti_my_manifs = newEntry;
	} else
	{
		// we need to iterate through the list to find the open next entry
		while (lstPtr->nextEntry != NULL)
		{
			lstPtr = lstPtr->nextEntry;
		}
		lstPtr->nextEntry = newEntry;
	}
	
	// return the pointer to the new entry
	return newEntry;
}

// this function is used to undo the operation performed by _cti_growManifList
static void
_cti_reapManifList(void)
{
	manifList_t *	lstPtr;

	// sanity check
	if ((lstPtr = _cti_my_manifs) == NULL)
		return;
	
	// if this was the first entry, lets remove it
	if (lstPtr->nextEntry == NULL)
	{
		free(lstPtr);
		_cti_my_manifs = NULL;
		return;
	}
	
	// iterate through until we find the entry whos next entry has a null next entry ;)
	// i.e. magic - this works because _cti_growManifList always places the new manifest_t entry
	// at the end of the list
	while (lstPtr->nextEntry->nextEntry != NULL)
	{
		lstPtr = lstPtr->nextEntry;
	}
	// the next entry has a null next entry so we need to free the next entry
	free(lstPtr->nextEntry);
	// now we need to set this entries next entry to null
	lstPtr->nextEntry = NULL;
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
	while (lstPtr->thisEntry == NULL)
	{
		// if this is the only object in the list, then delete the entire list
		if ((lstPtr = lstPtr->nextEntry) == NULL)
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
	while (lstPtr->thisEntry->mid != mid)
	{
		prePtr = lstPtr;
		if ((lstPtr = lstPtr->nextEntry) == NULL)
		{
			// there are no more entries and we didn't find the mid
			return;
		}
	}
	
	// check to see if this was the first entry in the global _cti_my_manifs list
	if (prePtr == lstPtr)
	{
		// point the global _cti_my_manifs list to the next entry
		_cti_my_manifs = lstPtr->nextEntry;
		// consume the manifest_t object for this entry in the list
		_cti_consumeManifest(lstPtr->thisEntry);
		// free the list object
		free(lstPtr);
	} else
	{
		// we are at some point midway through the global _cti_my_manifs list
		
		// point the previous entries next entry to the list pointers next entry
		// this bypasses the current list pointer
		prePtr->nextEntry = lstPtr->nextEntry;
		// consume the manifest_t object for this entry in the list
		_cti_consumeManifest(lstPtr->thisEntry);
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
	_cti_consumeStringList(entry->exec_names);
	_cti_consumeStringList(entry->lib_names);
	_cti_consumeStringList(entry->file_names);
	_cti_consumeStringList(entry->exec_loc);
	_cti_consumeStringList(entry->lib_loc);
	_cti_consumeStringList(entry->file_loc);
	
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
	while (lstPtr->thisEntry == NULL)
	{
		// if this is the only object in the list, then delete the entire list
		if ((lstPtr = lstPtr->nextEntry) == NULL)
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
	while (lstPtr->thisEntry->mid != mid)
	{
		if ((lstPtr = lstPtr->nextEntry) == NULL)
		{
			// there are no more entries and we didn't find the mid
			_cti_set_error("cti_manifest_id_t %d does not exist.", (int)mid);
			return NULL;
		}
	}
	
	return lstPtr->thisEntry;
}

static manifest_t *
_cti_newManifest(cti_session_id_t sid)
{
	manifList_t *	lstPtr;
	manifest_t *	this;
	const char **	ignore_ptr;
	session_t *		s_ptr;
	int				i;
	char **			str_ptr;
	
	// grow the global _cti_my_manifs list and get its new manifest_t entry
	if ((lstPtr = _cti_growManifList()) == NULL)
	{
		// error string already set
		return NULL;
	}
	
	// create the new manifest_t object
	if ((this = malloc(sizeof(manifest_t))) == (void *)0)
	{
		_cti_set_error("malloc failed.");
		_cti_reapManifList();
		return NULL;
	}
	memset(this, 0, sizeof(manifest_t));     // clear it to NULL
	
	// set the mid member
	// TODO: This could be made smarter by using a hash table instead of a revolving int we see now
	this->mid = _cti_next_mid++;
	
	// create the stringList_t objects
	if ((this->exec_names = _cti_newStringList()) == NULL)
	{
		_cti_set_error("_cti_newStringList() failed.");
		_cti_reapManifList();
		_cti_consumeManifest(this);
		return NULL;
	}
	if ((this->lib_names = _cti_newStringList()) == NULL)
	{
		_cti_set_error("_cti_newStringList() failed.");
		_cti_reapManifList();
		_cti_consumeManifest(this);
		return NULL;
	}
	// Add the ignored library strings to the shipped_libs string list.
	for (ignore_ptr=_cti_ignored_libs; *ignore_ptr != NULL; ++ignore_ptr)
	{
		if (_cti_addString(this->lib_names, *ignore_ptr))
		{
			_cti_set_error("_cti_addString() failed.");
			_cti_reapManifList();
			_cti_consumeManifest(this);
			return NULL;
		}
	}
	if ((this->file_names = _cti_newStringList()) == NULL)
	{
		_cti_set_error("_cti_newStringList() failed.");
		_cti_reapManifList();
		_cti_consumeManifest(this);
		return NULL;
	}
	if ((this->exec_loc = _cti_newStringList()) == NULL)
	{
		_cti_set_error("_cti_newStringList() failed.");
		_cti_reapManifList();
		_cti_consumeManifest(this);
		return NULL;
	}
	if ((this->lib_loc = _cti_newStringList()) == NULL)
	{
		_cti_set_error("_cti_newStringList() failed.");
		_cti_reapManifList();
		_cti_consumeManifest(this);
		return NULL;
	}
	if ((this->file_loc = _cti_newStringList()) == NULL)
	{
		_cti_set_error("_cti_newStringList() failed.");
		_cti_reapManifList();
		_cti_consumeManifest(this);
		return NULL;
	}
	
	// set the sid in the manifest
	this->sid = sid;
	
	// Check to see if we need to add session info. Note that the names list
	// will guarantee uniqueness. We simply need to add the name of a library
	// to this list to prevent it from being shipped.
	if (sid != 0)
	{
		if ((s_ptr = _cti_findSession(sid)) == NULL)
		{
			// We failed to find the session for the sid
			// error string already set
			_cti_reapManifList();
			_cti_consumeManifest(this);
			return NULL;
		}
		
		// increment the instance count in the sid
		s_ptr->instCnt++;
		
		// set the instance number based on the instCnt in the sid
		this->inst = s_ptr->instCnt;
		
		// copy the information from the session to the manifest
		this->stage_name = strdup(s_ptr->stage_name);
		
		// copy all of the names to the manifest name lists
		i = s_ptr->exec_names->num;
		str_ptr = s_ptr->exec_names->list;
		while (0 < i--)
		{
			if (_cti_addString(this->exec_names, *str_ptr))
			{
				// failed to save str_ptr into the list
				_cti_set_error("_cti_addString() failed.");
				_cti_reapManifList();
				_cti_consumeManifest(this);
				return NULL;
			}
		
			// increment str_ptr
			++str_ptr;
		}
		i = s_ptr->lib_names->num;
		str_ptr = s_ptr->lib_names->list;
		while (0 < i--)
		{
			if (_cti_addString(this->lib_names, *str_ptr))
			{
				// failed to save str_ptr into the list
				_cti_set_error("_cti_addString() failed.");
				_cti_reapManifList();
				_cti_consumeManifest(this);
				return NULL;
			}
		
			// increment str_ptr
			++str_ptr;
		}
		i = s_ptr->file_names->num;
		str_ptr = s_ptr->file_names->list;
		while (0 < i--)
		{
			if (_cti_addString(this->file_names, *str_ptr))
			{
				// failed to save str_ptr into the list
				_cti_set_error("_cti_addString() failed.");
				_cti_reapManifList();
				_cti_consumeManifest(this);
				return NULL;
			}
		
			// increment str_ptr
			++str_ptr;
		}
	} else
	{
		// this is the first instance of the session that will be created upon
		// shipping this manifest
		this->inst = 1;
	}
	
	// save the new manifest_t object into the returned manifList_t object that
	// the call to _cti_growManifList gave us.
	lstPtr->thisEntry = this;
	
	return this;
}

static int
_cti_addManifestToSession(cti_manifest_id_t mid, cti_session_id_t sid)
{
	manifest_t *	m_ptr;
	session_t *		s_ptr;
	int				i;
	char **			str_ptr;
	
	// sanity check
	if (mid == 0)
	{
		_cti_set_error("Invalid cti_manifest_id_t %d.", (int)mid);
		return 1;
	}
	
	if (sid == 0)
	{
		_cti_set_error("Invalid cti_session_id_t %d.", (int)sid);
		return 1;
	}
	
	// Find the manifest entry in the global manifest list for the mid
	if ((m_ptr = _cti_findManifest(mid)) == NULL)
	{
		// We failed to find the manifest for the mid
		// error string already set
		return 1;
	}
	
	// Find the session entry in the global session list for the sid
	if ((s_ptr = _cti_findSession(sid)) == NULL)
	{
		// We failed to find the session for the sid
		// error string already set
		return 1;
	}
	
	// copy all of the names to the session name lists
	i = m_ptr->exec_names->num;
	str_ptr = m_ptr->exec_names->list;
	while (0 < i--)
	{
		// ensure this name is not already in the list
		if (!_cti_searchStringList(s_ptr->exec_names, *str_ptr))
		{
			// not in the list, so add it
			if (_cti_addString(s_ptr->exec_names, *str_ptr))
			{
				// failed to save str_ptr into the list
				_cti_set_error("_cti_addString() failed.");
				return 1;
			}
		}
		// increment str_ptr
		++str_ptr;
	}
	
	i = m_ptr->lib_names->num;
	str_ptr = m_ptr->lib_names->list;
	while (0 < i--)
	{
		// ensure this name is not already in the list
		if (!_cti_searchStringList(s_ptr->lib_names, *str_ptr))
		{
			// not in the list, so add it
			if (_cti_addString(s_ptr->lib_names, *str_ptr))
			{
				// failed to save str_ptr into the list
				_cti_set_error("_cti_addString() failed.");
				return 1;
			}
		}
		// increment str_ptr
		++str_ptr;
	}
	
	i = m_ptr->file_names->num;
	str_ptr = m_ptr->file_names->list;
	while (0 < i--)
	{
		// ensure this name is not already in the list
		if (!_cti_searchStringList(s_ptr->file_names, *str_ptr))
		{
			// not in the list, so add it
			if (_cti_addString(s_ptr->file_names, *str_ptr))
			{
				// failed to save str_ptr into the list
				_cti_set_error("_cti_addString() failed.");
				return 1;
			}
		}
		// increment str_ptr
		++str_ptr;
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
		app_ptr->_transferObj = (void *)sess_obj;
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
cti_addManifestBinary(cti_manifest_id_t mid, char *fstr)
{
	manifest_t *	m_ptr;		// pointer to the manifest_t object associated with the cti_manifest_id_t argument
	char *			fullname;	// full path name of the binary to add to the manifest
	char *			realname;	// realname (lacking path info) of the file
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
		return 1;
	}
	
	// search the exec_names list for a duplicate filename
	if (!_cti_searchStringList(m_ptr->exec_names, realname))
	{
		// not found in list, so this is a unique file name
		
		// add realname to the names list
		if (_cti_addString(m_ptr->exec_names, realname))
		{
			// failed to save realname into the list
			_cti_set_error("_cti_addString() failed.");
			return 1;
		}
		
		// add fullname to the loc list
		if (_cti_addString(m_ptr->exec_loc, fullname))
		{
			// failed to save fullname into the list
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
				if (cti_addManifestLibrary(m_ptr->mid, *tmp++))
				{
					// if we return with non-zero status, catastrophic failure occured
					// error string already set
					return 1;
				}
			}
			
			tmp = lib_array;
			while (*tmp != NULL)
			{
				free(*tmp++);
			}
			free(lib_array);
		}
	}
	
	// cleanup memory
	free(fullname);
	free(realname);
	
	// if filename has already been added - enforce uniqueness requirements and silently fail
	return 0;
}

int
cti_addManifestLibrary(cti_manifest_id_t mid, char *fstr)
{
	manifest_t *	m_ptr;		// pointer to the manifest_t object associated with the cti_manifest_id_t argument
	char *			fullname;	// full path name of the library to add to the manifest
	char *			realname;	// realname (lacking path info) of the library
	
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
	if ((fullname = _cti_libFind(fstr, NULL)) == NULL)
	{
		_cti_set_error("Could not locate the specified library in LD_LIBRARY_PATH or system location.");
		return 1;
	}
	
	// next just grab the real name (without path information) of the library
	if ((realname = _cti_pathToName(fullname)) == NULL)
	{
		_cti_set_error("Could not convert the fullname to realname.");
		return 1;
	}
	
	// TODO: We need to create a way to ship conflicting libraries. Since most libraries are sym links to
	// their proper version, name collisions are possible. In the future, the launcher should be able to handle
	// this by pointing its LD_LIBRARY_PATH to a custom directory containing the conflicting lib.
	
	// search the lib_names list for a duplicate filename
	if (!_cti_searchStringList(m_ptr->lib_names, realname))
	{
		// not found in list, so this is a unique file name
		
		// add realname to the names list
		if (_cti_addString(m_ptr->lib_names, realname))
		{
			// failed to save realname into the list
			_cti_set_error("_cti_addString() failed.");
			return 1;
		}
		
		// add fullname to the loc list
		if (_cti_addString(m_ptr->lib_loc, fullname))
		{
			// failed to save fullname into the list
			_cti_set_error("_cti_addString() failed.");
			return 1;
		}
	}
		
	// cleanup memory
	free(fullname);
	free(realname);
	
	// if library has already been added - enforce uniqueness requirements and silently fail
	return 0;
}

int
cti_addManifestFile(cti_manifest_id_t mid, char *fstr)
{
	manifest_t *	m_ptr;		// pointer to the manifest_t object associated with the cti_manifest_id_t argument
	char *			fullname;	// full path name of the file to add to the manifest
	char *			realname;	// realname (lacking path info) of the file
	
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
		return 1;
	}
	
	// search the file_names list for a duplicate filename
	if (!_cti_searchStringList(m_ptr->file_names, realname))
	{
		// not found in list, so this is a unique file name
		
		// add realname to the names list
		if (_cti_addString(m_ptr->file_names, realname))
		{
			// failed to save realname into the list
			_cti_set_error("_cti_addString() failed.");
			return 1;
		}
		
		// add fullname to the loc list
		if (_cti_addString(m_ptr->file_loc, fullname))
		{
			// failed to save fullname into the list
			_cti_set_error("_cti_addString() failed.");
			return 1;
		}
	}
		
	// cleanup memory
	free(fullname);
	free(realname);
	
	// if file has already been added - enforce uniqueness requirements and silently fail
	return 0;
}

static int
_cti_removeFilesFromDir(char *path)
{
	struct dirent *	d;
	DIR *			dir;
	char *			name_path = NULL;
	
	if ((dir = opendir(path)) == NULL)
	{
		_cti_set_error("opendir: %s", strerror(errno));
		return 1;
	}
	
	do {
		// reset errno
		errno = 0;
		
		// get the next dirent entry in the directory
		if ((d = readdir(dir)) != NULL)
		{
			// create the full path name
			if (asprintf(&name_path, "%s/%s", path, d->d_name) <= 0)
			{
				_cti_set_error("asprintf failed.");
				closedir(dir);
				return 1;
			}
			// remove the file
			remove(name_path);
			free(name_path);
		}
	} while (d != NULL);
	
	// check for error
	if (errno != 0)
	{
		_cti_set_error("readdir: %s", strerror(errno));
		closedir(dir);
		return 1;
	}
	
	closedir(dir);
	return 0;
}

static int
_cti_copyFilesToPackage(stringList_t *list, char *path)
{
	int				i, nr, nw;
	char **			str_ptr;
	FILE *			f1;
	FILE *			f2;
	char *			name;
	char *			name_path;
	char			buffer[BUFSIZ];
	struct stat		statbuf;

	if (list == NULL)
	{
		_cti_set_error("_cti_copyFileToPackage list is NULL.");
		return 1;
	}
	
	if (path == NULL)
	{
		_cti_set_error("_cti_copyFileToPackage path is NULL.");
		return 1;
	}

	i = list->num;
	str_ptr = list->list;
	
	while (0 < i--)
	{
		if ((f1 = fopen(*str_ptr, "r")) == NULL)
		{
			_cti_set_error("fopen failed.");
			return 1;
		}
		// get the short name
		name = _cti_pathToName(*str_ptr);
		if (asprintf(&name_path, "%s/%s", path, name) <= 0)
		{
			_cti_set_error("asprintf failed.");
			fclose(f1);
			free(name);
			return 1;
		}
		free(name);
		if ((f2 = fopen(name_path, "w")) == NULL)
		{
			_cti_set_error("fopen failed.");
			fclose(f1);
			free(name_path);
			return 1;
		}
		// read/write everything from f1/to f2
		while ((nr = fread(buffer, sizeof(char), BUFSIZ, f1)) > 0)
		{
			if ((nw = fwrite(buffer, sizeof(char), nr, f2)) != nr)
			{
				_cti_set_error("fwrite failed.");
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
		if (stat(*str_ptr, &statbuf) == -1)
		{
			_cti_set_error("Could not stat %s.", *str_ptr);
			free(name_path);
			return 1;
		}
		if (chmod(name_path, statbuf.st_mode) != 0)
		{
			_cti_set_error("Could not chmod %s.", name_path);
			free(name_path);
			return 1;
		}
		
		// cleanup
		free(name_path);
		
		// increment str_ptr
		++str_ptr;
	}
	
	return 0;
}

static int
_cti_packageManifestAndShip(cti_app_id_t appId, cti_manifest_id_t mid)
{
	appEntry_t *			app_ptr;			// pointer to the appEntry_t object associated with the provided aprun pid
	manifest_t *			m_ptr = NULL;		// pointer to the manifest_t object associated with the cti_manifest_id_t argument
	char *					cfg_dir = NULL;		// configuration directory from env var
	char *					stage_dir = NULL;	// staging directory name
	char *					stage_path = NULL;	// staging path
	char *					bin_path = NULL;
	char *					lib_path = NULL;
	char *					tmp_path = NULL;
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
		goto packageManifestAndShip_error;
	}
	
	// Find the manifest entry in the global manifest list for the mid
	if ((m_ptr = _cti_findManifest(mid)) == NULL)
	{
		// We failed to find the manifest for the mid
		// error string already set
		goto packageManifestAndShip_error;
	}
	
	// get configuration dir
	if ((cfg_dir = getenv(CFG_DIR_VAR)) == NULL)
	{
		_cti_set_error("Cannot getenv on %s. Ensure environment variables are set.", CFG_DIR_VAR);
		goto packageManifestAndShip_error;
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
				_cti_set_error("asprintf failed.");
				goto packageManifestAndShip_error;
			}
		
			// create the temporary directory for the manifest package
			if (mkdtemp(stage_path) == NULL)
			{
				_cti_set_error("mkdtemp failed.");
				goto packageManifestAndShip_error;
			}
		} else
		{
			// use the user defined directory
			if (asprintf(&stage_path, "%s/%s", cfg_dir, stage_dir) <= 0)
			{
				_cti_set_error("asprintf failed.");
				goto packageManifestAndShip_error;
			}
		
			if (mkdir(stage_path, S_IRWXU))
			{
				_cti_set_error("mkdir failed.");
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
			_cti_set_error("asprintf failed.");
			goto packageManifestAndShip_error;
		}
		
		if (mkdir(stage_path, S_IRWXU))
		{
			_cti_set_error("mkdir failed.");
			goto packageManifestAndShip_error;
		}
	}
	
	// now create the required subdirectories
	if (asprintf(&bin_path, "%s/bin", stage_path) <= 0)
	{
		_cti_set_error("asprintf failed.");
		goto packageManifestAndShip_error;
	}
	if (asprintf(&lib_path, "%s/lib", stage_path) <= 0)
	{
		_cti_set_error("asprintf failed.");
		goto packageManifestAndShip_error;
	}
	if (asprintf(&tmp_path, "%s/tmp", stage_path) <= 0)
	{
		_cti_set_error("asprintf failed.");
		goto packageManifestAndShip_error;
	}
	if (mkdir(bin_path, S_IRWXU))
	{
		_cti_set_error("mkdir failed.");
		goto packageManifestAndShip_error;
	}
	if (mkdir(lib_path,  S_IRWXU))
	{
		_cti_set_error("mkdir failed.");
		goto packageManifestAndShip_error;
	}
	if (mkdir(tmp_path, S_IRWXU))
	{
		_cti_set_error("mkdir failed.");
		goto packageManifestAndShip_error;
	}
	
	// copy all of the binaries
	if (_cti_copyFilesToPackage(m_ptr->exec_loc, bin_path))
	{
		// error string already set
		goto packageManifestAndShip_error;
	}
	
	// copy all of the libraries
	if (_cti_copyFilesToPackage(m_ptr->lib_loc, lib_path))
	{
		// error string already set
		goto packageManifestAndShip_error;
	}
	
	// copy all of the files
	if (_cti_copyFilesToPackage(m_ptr->file_loc, stage_path))
	{
		// error string already set
		goto packageManifestAndShip_error;
	}
	
	// create the tarball name
	if (asprintf(&tar_name, "%s.tar", stage_path) <= 0)
	{
		_cti_set_error("asprintf failed.");
		goto packageManifestAndShip_error;
	}
	
	// Create the tarball
	a = archive_write_new();
	archive_write_add_filter_none(a);
	
	archive_write_set_format_ustar(a);
	
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
			break;
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
			_cti_set_error("Could not find base name for tar.");
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
			_cti_set_error("ARCHIVE_FATAL error occured.");
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
					_cti_set_error("archive_write_data() error occured.");
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
		_cti_set_error("asprintf failed.");
		goto packageManifestAndShip_error;
	}
	
	// move the tarball
	if (rename(tar_name, tmp_tar_name))
	{
		_cti_set_error("Failed to rename tarball to %s.", tmp_tar_name);
		free(tmp_tar_name);
		goto packageManifestAndShip_error;
	}
	
	// set the tar_name to the tmp_tar_name
	free(tar_name);
	tar_name = tmp_tar_name;
	
	// Call the appropriate transfer function based on the wlm
	switch (app_ptr->wlm)
	{
		case CTI_WLM_ALPS:
			if (_cti_alps_ship_package(app_ptr->_wlmObj, tar_name))
			{
				// we failed to ship the file to the compute nodes for some reason - catastrophic failure
				// Error message already set
				goto packageManifestAndShip_error;
			}
			break;
			
		case CTI_WLM_CRAY_SLURM:
		case CTI_WLM_SLURM:
			_cti_set_error("Current WLM is not yet supported.");
			goto packageManifestAndShip_error;
			
		case CTI_WLM_MULTI:
			// TODO - add argument to allow caller to select preferred wlm if there is
			// ever multiple WLMs present on the system.
			_cti_set_error("Multiple workload managers present! This is not yet supported.");
			goto packageManifestAndShip_error;
			
		case CTI_WLM_NONE:
			_cti_set_error("No valid workload manager detected.");
			goto packageManifestAndShip_error;
	}
	
	// clean things up
	if (_cti_removeFilesFromDir(bin_path))
	{
		// Normally we don't want to print to stderr, but in this case we should at least try
		// to do something since we don't return with a warning status.
		fprintf(stderr, "Failed to remove files from %s, please remove manually.\n", bin_path);
	}
	remove(bin_path);
	free(bin_path);
	if (_cti_removeFilesFromDir(lib_path))
	{
		// Normally we don't want to print to stderr, but in this case we should at least try
		// to do something since we don't return with a warning status.
		fprintf(stderr, "Failed to remove files from %s, please remove manually.\n", lib_path);
	}
	remove(lib_path);
	free(lib_path);
	if (_cti_removeFilesFromDir(stage_path))
	{
		// Normally we don't want to print to stderr, but in this case we should at least try
		// to do something since we don't return with a warning status.
		fprintf(stderr, "Failed to remove files from %s, please remove manually.\n", stage_path);
	}
	remove(tmp_path);
	free(tmp_path);
	remove(stage_path);
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

	// Try to remove any files already copied
	if (bin_path != NULL)
	{
		if (_cti_removeFilesFromDir(bin_path))
		{
			// Normally we don't want to print to stderr, but in this case we should at least try
			// to do something since we don't return with a warning status.
			fprintf(stderr, "Failed to remove files from %s, please remove manually.\n", bin_path);
		}
		remove(bin_path);
		free(bin_path);
	}
	if (lib_path != NULL)
	{
		if (_cti_removeFilesFromDir(lib_path))
		{
			// Normally we don't want to print to stderr, but in this case we should at least try
			// to do something since we don't return with a warning status.
			fprintf(stderr, "Failed to remove files from %s, please remove manually.\n", lib_path);
		}
		remove(lib_path);
		free(lib_path);
	}
	if (tmp_path != NULL)
	{
		remove(tmp_path);
		free(tmp_path);
	}
	if (stage_path != NULL)
	{
		if (_cti_removeFilesFromDir(stage_path))
		{
			// Normally we don't want to print to stderr, but in this case we should at least try
			// to do something since we don't return with a warning status.
			fprintf(stderr, "Failed to remove files from %s, please remove manually.\n", stage_path);
		}
		remove(stage_path);
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
	char *				args_flat;			// flattened args array to pass to the toolhelper call
	char *				cpy;				// temporary cpy var used in creating args_flat
	char *				launcher;			// full path name of the daemon launcher application
	size_t				len;				// len vars used in creating the args_flat string
	session_t *			s_ptr = NULL;		// points at the session to return
	cti_session_id_t	rtn;
	int					trnsfr = 1;			// should we transfer the dlaunch?
	int					l, val;

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
	
	// Ensure that there are strings in the loc lists, otherwise there is no 
	// need to ship a tarball, everything we need already has been transfered
	// to the nodes
	if (m_ptr->exec_loc->num || m_ptr->lib_loc->num || m_ptr->file_loc->num)
	{
		// ship the manifest tarball to the compute nodes
		if (_cti_packageManifestAndShip(appId, m_ptr->mid))
		{
			// Failed to ship the manifest - catastrophic failure
			// error string already set
			return 0;
		}
	} else
	{
		// ensure that there is a session set in the m_ptr
		if (m_ptr->sid <= 0)
		{
			_cti_set_error("cti_manifest_id_t %d was empty!", m_ptr->mid);
			return 0;
		}
		
		// return the session id since everything was already shipped
		rtn = m_ptr->sid;
		
		// remove the manifest
		_cti_reapManifest(m_ptr->mid);
		
		return rtn;
	}
	
	// now we need to create the flattened argv string for the actual call to the wrapper
	// this is passed through the toolhelper
	// The options passed MUST correspond to the options defined in the daemon_launcher program.
	
	// Create the launcher path based on the trnsfr option. If this is false, we have already
	// transfered the launcher over to the compute node and want to use the existing one over
	// there, otherwise we need to find the location of the launcher on our end to have alps
	// transfer to the compute nodes
	if (trnsfr)
	{
		// Need to transfer launcher binary
		
		// Find the location of the daemon launcher program
		if ((launcher = _cti_pathFind(CTI_LAUNCHER, NULL)) == NULL)
		{
			_cti_set_error("Could not locate the launcher application in PATH.");
			return 0;
		}
	} else
	{
		// use existing launcher binary on compute node
		if (asprintf(&launcher, "%s/%s", app_ptr->toolPath, CTI_LAUNCHER) <= 0)
		{
			_cti_set_error("asprintf failed.");
			return 0;
		}
	}
	
	// determine the length of the argv[0]
	len = strlen(launcher);

	// find the length of the inst int as a string
	l = 1;
	val = m_ptr->inst;
	while(val>9) { l++; val/=10; }
	
	// determine the length of the -m (manifest) argument
	len += strlen(" -m ");
	len += strlen(m_ptr->stage_name);
	len += l;
	len += strlen(".tar");
	
	// determine the length of the -d (directory) argument
	len += strlen(" -d ");
	len += strlen(m_ptr->stage_name);
	
	// determine the length of the -i (instance) argument
	len += strlen(" -i ");
	len += l;
		
	// if debug is on, add the len of the debug switch
	if (dbg)
	{
		len += strlen(" --debug");
	}
		
	// add one for the null terminator
	++len;
	
	// malloc space for this string buffer
	if ((args_flat = malloc(len)) == (void *)0)
	{
		// malloc failed
		_cti_set_error("malloc failed.");
		free(launcher);
		return 0;
	}
		
	// start creating the flattened args string
	snprintf(args_flat, len, "%s -m %s%d.tar -d %s -i %d", launcher, m_ptr->stage_name, m_ptr->inst, m_ptr->stage_name, m_ptr->inst);
	
	// Cleanup
	free(launcher);
	
	// add the debug switch if debug is on
	if (dbg)
	{
		cpy = strdup(args_flat);
		snprintf(args_flat, len, "%s --debug", cpy);
		free(cpy);
	}
	
	// Done. We now have a flattened args string
	
	// Call the appropriate transfer function based on the wlm
	switch (app_ptr->wlm)
	{
		case CTI_WLM_ALPS:
			if (_cti_alps_start_daemon(app_ptr->_wlmObj, args_flat, trnsfr))
			{
				// we failed to ship the file to the compute nodes for some reason - catastrophic failure
				// Error message already set
				free(args_flat);
				_cti_reapManifest(m_ptr->mid);
				return 0;
			}
			break;
			
		case CTI_WLM_CRAY_SLURM:
		case CTI_WLM_SLURM:
			_cti_set_error("Current WLM is not yet supported.");
			free(args_flat);
			_cti_reapManifest(m_ptr->mid);
			return 0;
			
		case CTI_WLM_MULTI:
			// TODO - add argument to allow caller to select preferred wlm if there is
			// ever multiple WLMs present on the system.
			_cti_set_error("Multiple workload managers present! This is not yet supported.");
			free(args_flat);
			_cti_reapManifest(m_ptr->mid);
			return 0;
			
		case CTI_WLM_NONE:
			_cti_set_error("No valid workload manager detected.");
			free(args_flat);
			_cti_reapManifest(m_ptr->mid);
			return 0;
	}
		
	// cleanup our memory
	free(args_flat);
	
	// create a new session for this tool daemon instance if one doesn't already exist
	if (m_ptr->sid == 0)
	{
		if ((s_ptr = _cti_newSession(m_ptr->mid)) == NULL)
		{
			// we failed to create a new session_t - catastrophic failure
			// error string already set
			return 0;
		}
	} else
	{
		// Bugfix: Make sure to set s_ptr...
		// Find the session entry in the global session list for the sid
		if ((s_ptr = _cti_findSession(m_ptr->sid)) == NULL)
		{
			// We failed to find the session for the sid
			// error string already set
			return 0;
		}
		// Merge the manifest into the existing session
		if (_cti_addManifestToSession(m_ptr->mid, m_ptr->sid))
		{
			// we failed to merge the manifest into the session - catastrophic failure
			// error string already set
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
cti_execToolDaemon(cti_app_id_t appId, cti_manifest_id_t mid, cti_session_id_t sid, char *fstr, char **args, char **env, int dbg)
{
	appEntry_t *	app_ptr;			// pointer to the appEntry_t object associated with the provided aprun pid
	manifest_t *	m_ptr;				// pointer to the manifest_t object associated with the cti_manifest_id_t argument
	int				useManif = 0;		// controls if a manifest was shipped or not
	char *			fullname;			// full path name of the executable to launch as a tool daemon
	char *			realname;			// realname (lacking path info) of the executable
	char *			args_flat;			// flattened args array to pass to the toolhelper call
	char *			cpy;				// temporary cpy var used in creating args_flat
	char *			launcher;			// full path name of the daemon launcher application
	char **			tmp;				// temporary pointer used to iterate through lists of strings
	size_t			len, env_base_len;	// len vars used in creating the args_flat string
	session_t *		s_ptr = NULL;		// points at the session to return
	int				trnsfr = 1;			// should we transfer the dlaunch?
	int				l, val;
		
	// sanity check
	if (appId == 0)
	{
		_cti_set_error("Invalid appId %d.", (int)appId);
		return 1;
	}
	
	if (fstr == NULL)
	{
		_cti_set_error("cti_execToolDaemon had null fstr.");
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
	
	// add the fstr to the manifest
	if (cti_addManifestBinary(m_ptr->mid, fstr))
	{
		// Failed to add the binary to the manifest - catastrophic failure
		// error string already set
		return 0;
	}
	
	// Ensure that there are strings in the loc lists, otherwise there is no 
	// need to ship a tarball, everything we need already has been transfered
	// to the nodes
	if (m_ptr->exec_loc->num || m_ptr->lib_loc->num || m_ptr->file_loc->num)
	{
		// ship the manifest tarball to the compute nodes
		if (_cti_packageManifestAndShip(appId, m_ptr->mid))
		{
			// Failed to ship the manifest - catastrophic failure
			// error string already set
			return 0;
		}
		++useManif;
	}
	
	// now we need to create the flattened argv string for the actual call to the wrapper
	// this is passed through the toolhelper
	// The options passed MUST correspond to the options defined in the daemon_launcher program.
		
	// find the binary name for the args
	
	// convert to fullpath name
	if ((fullname = _cti_pathFind(fstr, NULL)) == NULL)
	{
		_cti_set_error("Could not locate the specified file in PATH.");
		return 0;
	}
	
	// next just grab the real name (without path information) of the binary
	if ((realname = _cti_pathToName(fullname)) == NULL)
	{
		_cti_set_error("Could not convert the fullname to realname.");
		return 0;
	}
	
	// done with fullname
	free(fullname);
	
	// Create the launcher path based on the trnsfr option. If this is false, we have already
	// transfered the launcher over to the compute node and want to use the existing one over
	// there, otherwise we need to find the location of the launcher on our end to have alps
	// transfer to the compute nodes
	if (trnsfr)
	{
		// Need to transfer launcher binary
		
		// Find the location of the daemon launcher program
		if ((launcher = _cti_pathFind(CTI_LAUNCHER, NULL)) == NULL)
		{
			_cti_set_error("Could not locate the launcher application in PATH.");
			return 0;
		}
	} else
	{
		// use existing launcher binary on compute node
		if (asprintf(&launcher, "%s/%s", app_ptr->toolPath, CTI_LAUNCHER) <= 0)
		{
			_cti_set_error("asprintf failed.");
			return 0;
		}
	}
	
	// determine the length of the argv[0] and -b (binary) argument
	len = strlen(launcher) + strlen(" -b ") + strlen(realname);
	
	// find the length of the inst int as a string
	l = 1;
	val = m_ptr->inst;
	while(val>9) { l++; val/=10; }
	
	// We use a -m if useManif is true.
	if (useManif)
	{
		// determine the length of the -m (manifest) argument
		len += strlen(" -m ");
		len += strlen(m_ptr->stage_name);
		len += l;
		len += strlen(".tar");
	}
	
	// determine the length of the -d (directory) argument
	len += strlen(" -d ");
	len += strlen(m_ptr->stage_name);
	
	// determine the length of the -i (instance) argument
	len += strlen(" -i ");
	len += l;
	
	// iterate through the env array and determine its total length
	env_base_len = strlen(" -e "); 
	tmp = env;
	// ensure the are actual entries before dereferencing tmp
	if (tmp != NULL)
	{
		while (*tmp != NULL)
		{
			len += env_base_len + strlen(*tmp++);
		}
	}
		
	// if debug is on, add the len of the debug switch
	if (dbg)
	{
		len += strlen(" --debug");
	}
		
	// add the length of the "--" terminator to end the opt parsing
	len += strlen(" --");
		
	// iterate through the args array and determine its length
	tmp = args;
	// ensure the are actual entries before dereferencing tmp
	if (tmp != NULL)
	{
		while (*tmp != NULL)
		{
			len += strlen(" ") + strlen(*tmp++) + strlen(" ");
		}
	}
		
	// add one for the null terminator
	++len;
	
	// malloc space for this string buffer
	if ((args_flat = malloc(len)) == (void *)0)
	{
		// malloc failed
		_cti_set_error("malloc failed.");
		return 0;
	}
		
	// start creating the flattened args string
	snprintf(args_flat, len, "%s -b %s -d %s -i %d", launcher, realname, m_ptr->stage_name, m_ptr->inst);
	
	// cleanup
	free(launcher);
	
	// add -m argument if needed
	if (useManif)
	{
		cpy = strdup(args_flat);
		snprintf(args_flat, len, "%s -m %s%d.tar", cpy, m_ptr->stage_name, m_ptr->inst);
		free(cpy);
	}
	
	// cleanup mem
	free(realname);
		
	// add each of the env arguments
	tmp = env;
	// ensure the are actual entries before dereferencing tmp
	if (tmp != NULL)
	{
		while (*tmp != NULL)
		{
			// we need a temporary copy of the args_flat string so far
			cpy = strdup(args_flat);
			snprintf(args_flat, len, "%s -e %s", cpy, *tmp++);
			free(cpy);
		}
	}
		
	// add the debug switch if debug is on
	if (dbg)
	{
		cpy = strdup(args_flat);
		snprintf(args_flat, len, "%s --debug", cpy);
		free(cpy);
	}
	
	// add the "options" terminator
	cpy = strdup(args_flat);
	snprintf(args_flat, len, "%s --", cpy);
	free(cpy);
	
	// add each of the args from the args array
	tmp = args;
	// ensure the are actual entries before dereferencing tmp
	if (tmp != NULL)
	{
		while (*tmp != NULL)
		{
			cpy = strdup(args_flat);
			snprintf(args_flat, len, "%s %s ", cpy, *tmp++);
			free(cpy);
		}
	}
		
	// Done. We now have a flattened args string
	
	// Call the appropriate transfer function based on the wlm
	switch (app_ptr->wlm)
	{
		case CTI_WLM_ALPS:
			if (_cti_alps_start_daemon(app_ptr->_wlmObj, args_flat, trnsfr))
			{
				// we failed to ship the file to the compute nodes for some reason - catastrophic failure
				// Error message already set
				free(args_flat);
				_cti_reapManifest(m_ptr->mid);
				return 0;
			}
			break;
			
		case CTI_WLM_CRAY_SLURM:
		case CTI_WLM_SLURM:
			_cti_set_error("Current WLM is not yet supported.");
			free(args_flat);
			_cti_reapManifest(m_ptr->mid);
			return 0;
			
		case CTI_WLM_MULTI:
			// TODO - add argument to allow caller to select preferred wlm if there is
			// ever multiple WLMs present on the system.
			_cti_set_error("Multiple workload managers present! This is not yet supported.");
			free(args_flat);
			_cti_reapManifest(m_ptr->mid);
			return 0;
			
		case CTI_WLM_NONE:
			_cti_set_error("No valid workload manager detected.");
			free(args_flat);
			_cti_reapManifest(m_ptr->mid);
			return 0;
	}
	
	// cleanup our memory
	free(args_flat);
	
	// create a new session for this tool daemon instance if one doesn't already exist
	if (s_ptr == NULL)
	{
		if ((s_ptr = _cti_newSession(m_ptr->mid)) == NULL)
		{
			// we failed to create a new session_t - catastrophic failure
			// error string already set
			return 0;
		}
	} else
	{
		// Merge the manifest into the existing session only if we needed to
		// transfer any files
		if (useManif)
		{
			if (_cti_addManifestToSession(m_ptr->mid, s_ptr->sid))
			{
				// we failed to merge the manifest into the session - catastrophic failure
				// error string already set
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

