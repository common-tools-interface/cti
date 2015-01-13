/*********************************************************************************\
 * cti_transfer.c - A generic interface to the transfer files and start daemons.
 *		   This provides a tool developer with an easy to use interface to
 *		   transfer binaries, shared libraries, and files to the compute nodes
 *		   associated with an app. This can also be used to launch tool daemons
 *		   on the compute nodes in an automated way.
 *
 * © 2011-2015 Cray Inc.  All Rights Reserved.
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

// opaque callback type used when calling packageManifestAndShip
typedef struct
{
	char *					bin_path;
	char *					lib_path;
	char *					file_path;
	struct archive *		a;
	struct archive_entry *	entry;
	char *					read_buf;
	char					path_buf[PATH_MAX];
} package_data;

// opaque callback type used when calling resolveManifestConflicts
typedef struct
{
	stringList_t *	sess_files;
	cti_stack_t *	conflicts;
} conflict_data;

// Types of files in a file chain
typedef enum
{
	EXEC_ENTRY,
	LIB_ENTRY,
	LIBDIR_ENTRY,
	FILE_ENTRY
} entry_type;

// Actual file chain - we allow conflicting names as long as the file is in a
// different directory
struct fileEntry
{
	entry_type			type;			// type of file - so we can share trie data structure with other entries
	char *				loc;			// location of file
	struct fileEntry *	next;			// linked list of fileEntry
};
typedef struct fileEntry	fileEntry_t;

// Session is owned by an appEntry_t
typedef struct
{
	cti_session_id_t	sid;			// session id
	appEntry_t *		app_ptr;		// pointer to app_entry associated with session
	cti_list_t *		manifs;			// list of manifest objects associated with this session
	int					instCnt;		// manif instance count - prevents naming conflicts for manifest tarballs
	char *				stage_name;		// basename of the staging directory
	char *				toolPath;		// toolPath of the app entry
	stringList_t *		files;			// list of session files
} session_t;
#define SESS_APP(x)		(x)->app_ptr

// Manifest is owned by a session_t
typedef struct
{
	cti_manifest_id_t	mid;			// manifest id
	session_t *			sess;			// pointer to session obj
	stringList_t *		files;			// list of manifest files
	int					inst;			// instance of manifest - set on ship
} manifest_t;
#define MANIF_SESS(x)		(x)->sess
#define MANIF_APP(x)		(x)->sess->app_ptr

// Used to define block size for file->file copies
#define CTI_BLOCK_SIZE	65536

/* Static prototypes */
static void				_cti_transfer_handler(int);
static void				_cti_transfer_doCleanup(void);
static const char *		_cti_entryTypeToString(entry_type);
static fileEntry_t *	_cti_newFileEntry(void);
static void				_cti_consumeFileEntry(void *);
static fileEntry_t *	_cti_copyFileEntry(fileEntry_t *);
static fileEntry_t **	_cti_findFileEntryLoc(fileEntry_t *, entry_type);
static int				_cti_mergeFileEntry(fileEntry_t *, fileEntry_t *);
static int				_cti_chainFileEntry(fileEntry_t *, const char *, entry_type, char *);
static session_t *		_cti_newSession(appEntry_t *);
static session_t *		_cti_findSession(cti_session_id_t);
static int				_cti_checkSessionForConflict(session_t *, entry_type, const char *, const char *);
static int				_cti_addDirToArchive(struct archive *, struct archive_entry *, const char *);
static int				_cti_copyFileToArchive(struct archive *, struct archive_entry *, const char *, const char *, char *);
static manifest_t *		_cti_newManifest(session_t *);
static void				_cti_consumeManifest(void *);
static manifest_t *		_cti_findManifest(cti_manifest_id_t);
static int				_cti_addManifestToSession_callback(void *, const char *, void *);
static int				_cti_addManifestToSession(manifest_t *);
static int				_cti_resolveManifestConflicts_callback(void *, const char *, void *);
static int				_cti_resolveManifestConflicts(manifest_t *);
static int				_cti_addBinary(manifest_t *, const char *);
static int				_cti_addLibrary(manifest_t *, const char *);
static int				_cti_addLibDir(manifest_t *, const char *);
static int				_cti_addFile(manifest_t *, const char *);
static int				_cti_packageManifestAndShip_callback(void *, const char *, void *);
static int				_cti_packageManifestAndShip(manifest_t *);

/* global variables */
static cti_session_id_t			_cti_next_sid		= 1;
static cti_list_t *				_cti_my_sess		= NULL;
static cti_manifest_id_t		_cti_next_mid		= 1;
static cti_list_t *				_cti_my_manifs		= NULL;
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

/******************************************
** Constructor/destructor functions
******************************************/

// constructor
void
_cti_transfer_init(void)
{
	// Try to force cleanup of any old files. This happens in case the previous
	// instance of CTI FE was killed and left files in temp space.
	_cti_transfer_doCleanup();
	
	// create the global lists
	_cti_my_sess = _cti_newList();
	_cti_my_manifs = _cti_newList();
}

// destructor
void
_cti_transfer_fini(void)
{
	// destroy the lists - these should have already been cleared out
	_cti_consumeList(_cti_my_sess, _cti_consumeSession);
	_cti_consumeList(_cti_my_manifs, _cti_consumeManifest);
	
	// try to cleanup again
	_cti_transfer_doCleanup();
}

/******************************************
** support routines to try to cleanup
******************************************/

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

static void
_cti_transfer_doCleanup(void)
{
	const char *		cfg_dir;
	DIR *				dir;
	struct dirent *		d;
	char *				stage_name;
	char *				p;
	
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
}

/******************************************
** support routines for file entry chains
******************************************/

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
_cti_chainFileEntry(fileEntry_t *entry, const char *name, entry_type type, char *loc)
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

/********************************
** support routines for sessions
********************************/

static session_t *
_cti_newSession(appEntry_t *app_ptr)
{
	session_t *			s_ptr = NULL;		// points at the new session obj
	const char *		toolPath;			// tool path associated with this appEntry_t based on the wlm
	char *				stage_name_env;
	
	// sanity
	if (app_ptr == NULL)
	{
		_cti_set_error("_cti_newSession: Bad args.");
		return NULL;
	}
	
	// create the new session_t object
	if ((s_ptr = malloc(sizeof(session_t))) == NULL)
	{
		_cti_set_error("malloc failed.");
		goto createSession_error;
	}
	memset(s_ptr, 0, sizeof(session_t));     // clear it to NULL
	
	// set the sid member
	// TODO: This could be made smarter by using a hash table instead of the
	// revolving int/linked list we see now.
	s_ptr->sid = _cti_next_sid++;
	
	// set the app_ptr
	s_ptr->app_ptr = app_ptr;
	
	// create the manif list
	if ((s_ptr->manifs = _cti_newList()) == NULL)
	{
		_cti_set_error("_cti_newList() failed.");
		goto createSession_error;
	}
	
	// Set instance count
	s_ptr->instCnt = 0;
	
	// Get the tool path for this wlm
	if ((toolPath = app_ptr->wlmProto->wlm_getToolPath(app_ptr->_wlmObj)) == NULL)
	{
		// error already set
		goto createSession_error;
	}
	s_ptr->toolPath = strdup(toolPath);
	
	// Get the stage_name
	// check to see if the caller set a staging directory name, otherwise create a unique one for them
	if ((stage_name_env = getenv(DAEMON_STAGE_VAR)) == NULL)
	{
		char *	rand_char;
		
		// take the default action
		if (asprintf(&s_ptr->stage_name, "%s", DEFAULT_STAGE_DIR) <= 0)
		{
			_cti_set_error("asprintf failed.");
			goto createSession_error;
		}
		
		// Point at the last 'X' in the string
		if ((rand_char = strrchr(s_ptr->stage_name, 'X')) == NULL)
		{
			_cti_set_error("Bad stage_name string!");
			goto createSession_error;
		}
			
		// set state or init the PRNG if we haven't already done so.
		if (_cti_r_init)
		{
			// set the PRNG state
			if (setstate((char *)_cti_r_state) == NULL)
			{
				_cti_set_error("setstate failed.");
				goto createSession_error;
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
				_cti_set_error("clock_gettime failed.");
				goto createSession_error;
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
		if (asprintf(&s_ptr->stage_name, "%s", stage_name_env) <= 0)
		{
			_cti_set_error("asprintf failed.");
			goto createSession_error;
		}
	}
	
	// create the stringList_t object
	if ((s_ptr->files = _cti_newStringList()) == NULL)
	{
		_cti_set_error("_cti_newStringList() failed.");
		goto createSession_error;
	}
	
	// save the new session object into the app entry
	if (_cti_list_add(app_ptr->sessions, s_ptr))
	{
		_cti_set_error("_cti_list_add() failed.");
		goto createSession_error;
	}
	
	// save the new session object into the global session list
	if (_cti_list_add(_cti_my_sess, s_ptr))
	{
		_cti_set_error("_cti_list_add() failed.");
		goto createSession_error;
	}
	
	return s_ptr;
	
createSession_error:

	_cti_consumeSession(s_ptr);
	return NULL;
}

// Note that this is called by the cti_fe layer when cleaning up an appEntry_t
// so we don't mark it as static.
void
_cti_consumeSession(void *arg)
{
	session_t *		sess = arg;
	manifest_t *	m_ptr;

	// sanity check
	if (sess == NULL)
		return;
	
	// remove session from app entry
	_cti_list_remove(SESS_APP(sess)->sessions, sess);
	
	// remove session from global list
	_cti_list_remove(_cti_my_sess, sess);
	
	// consume manifests associated with this session, they are no longer valid
	while ((m_ptr = _cti_list_pop(sess->manifs)) != NULL)
	{
		_cti_consumeManifest(m_ptr);
	}
	_cti_consumeList(sess->manifs, NULL);
	
	// free the stage_name
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
	session_t *	this;
	
	// sanity check
	if (sid <= 0)
	{
		_cti_set_error("Invalid cti_session_id_t %d.", (int)sid);
	}
	
	// search the global session list for this sid
	_cti_list_reset(_cti_my_sess);
	while ((this = (session_t *)_cti_list_next(_cti_my_sess)) != NULL)
	{
		// return if the sid match
		if (this->sid == sid)
			return this;
	}
	
	// if we get here, we didn't find the sid
	_cti_set_error("cti_session_id_t %d does not exist.", (int)sid);
	return NULL;
}

/*
** Search a session file list for a naming conflict.
** Returns -1 if realname is not in the files list, returns 0 if file is already
** present in the session, returns 1 and error is set if naming conflict exists.
*/
static int
_cti_checkSessionForConflict(session_t *sess, entry_type type, const char *realname, const char *fullname)
{
	fileEntry_t *	f_ptr;		// pointer to file entry
	fileEntry_t **	entry;
	
	// sanity
	if (sess == NULL || realname == NULL || fullname == NULL)
	{
		_cti_set_error("_cti_checkSessionForConflict: Bad args.");
		return 1;
	}
	
	// search the session string list for this filename
	f_ptr = _cti_lookupValue(sess->files, realname);
	if (f_ptr != NULL)
	{
		// check if this realname is already in the session list for type
		if ((entry = _cti_findFileEntryLoc(f_ptr, type)) != NULL)
		{
			// check to see if the locations match
			if (strncmp((*entry)->loc, fullname, strlen((*entry)->loc)) == 0)
			{
				// file locations match. No conflict, return success since the file is already
				// in the session
				return 0;
			} else
			{
				_cti_set_error("A %s named %s is already present in the session.", _cti_entryTypeToString(type), realname);
				return 1;
			}
		}
	}

	// if we get here, the realname is not in the list
	return -1;
}

/********************************
** support routines for archives
** Used when shipping manifests.
********************************/

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

/********************************
** support routines for manifests
********************************/

static manifest_t *
_cti_newManifest(session_t * s_ptr)
{
	manifest_t *	this;	
	
	// sanity
	if (s_ptr == NULL)
	{
		_cti_set_error("_cti_newManifest: Bad args.");
		return NULL;
	}
	
	// create the new manifest_t object
	if ((this = malloc(sizeof(manifest_t))) == (void *)0)
	{
		_cti_set_error("malloc failed.");
		return NULL;
	}
	memset(this, 0, sizeof(manifest_t));     // clear it to NULL
	
	// set the mid member
	// TODO: This could be made smarter by using a hash table instead of the
	// revolving int/linked list we see now.
	this->mid = _cti_next_mid++;
	
	// set the sess pointer
	this->sess = s_ptr;
	
	// create the stringList_t object
	if ((this->files = _cti_newStringList()) == NULL)
	{
		_cti_set_error("_cti_newStringList() failed.");
		_cti_consumeManifest(this);
		return NULL;
	}
	
	this->inst = 0;
	
	// save the new session object into the session obj
	if (_cti_list_add(s_ptr->manifs, this))
	{
		_cti_set_error("_cti_list_add() failed.");
		_cti_consumeManifest(this);
		return NULL;
	}
	
	// save the new manifest object into the global manifest list
	if (_cti_list_add(_cti_my_manifs, this))
	{
		_cti_set_error("_cti_list_add() failed.");
		_cti_consumeManifest(this);
		return NULL;
	}
	
	return this;
}

static void
_cti_consumeManifest(void *arg)
{
	manifest_t *m_ptr = arg;

	// sanity check
	if (m_ptr == NULL)
		return;
		
	// remove manifest from session
	_cti_list_remove(MANIF_SESS(m_ptr)->manifs, m_ptr);
	
	// remove manifest from global list
	_cti_list_remove(_cti_my_manifs, m_ptr);
	
	// eat the string list
	_cti_consumeStringList(m_ptr->files, &_cti_consumeFileEntry);
	
	// nom nom the final manifest_t object
	free(m_ptr);
}

static manifest_t *
_cti_findManifest(cti_manifest_id_t mid)
{
	manifest_t *	this;
	
	// sanity check
	if (mid <= 0)
	{
		_cti_set_error("Invalid cti_manifest_id_t %d.", (int)mid);
		return NULL;
	}
	
	// search the global manifest list for this mid
	_cti_list_reset(_cti_my_manifs);
	while ((this = (manifest_t *)_cti_list_next(_cti_my_manifs)) != NULL)
	{
		// return if the mid match
		if (this->mid == mid)
			return this;
	}
	
	// if we get here, we didn't find the sid
	_cti_set_error("cti_manifest_id_t %d does not exist.", (int)mid);
	return NULL;
}

static int
_cti_addManifestToSession_callback(void *opaque, const char *key, void *val)
{
	stringList_t *		sess_files = opaque;
	fileEntry_t *		f_ptr = val;
	fileEntry_t *		s_ptr;

	// sanity
	if (sess_files == NULL || key == NULL || f_ptr == NULL)
	{
		_cti_set_error("_cti_addManifestToSession_callback: Bad args.");
		return 1;
	}
	
	// check if this key is present in the session files
	s_ptr = _cti_lookupValue(sess_files, key);
	if (s_ptr != NULL)
	{
		// file already in the session, we need to merge
		if (_cti_mergeFileEntry(s_ptr, f_ptr))
		{
			// error already set
			return 1;
		}
	} else
	{
		// not in session, so add the key
		
		// copy the data entry
		f_ptr = _cti_copyFileEntry(f_ptr);
		if (f_ptr == NULL)
		{
			// error already set
			return 1;
		}
		
		// add the file to the session
		if (_cti_addString(sess_files, key, f_ptr))
		{
			// failed to save name into the list
			_cti_set_error("_cti_addManifestToSession_callback: _cti_addString() failed.");
			_cti_consumeFileEntry(f_ptr);
			return 1;
		}
	}
	
	return 0;
}

// Ensure that the session and manifest conflicts have been resolved before
// calling this function
static int
_cti_addManifestToSession(manifest_t *m_ptr)
{
	// sanity check
	if (m_ptr == NULL)
	{
		_cti_set_error("_cti_addManifestToSession: Invalid args.");
		return 1;
	}
	
	return _cti_forEachString(m_ptr->files, _cti_addManifestToSession_callback, MANIF_SESS(m_ptr)->files);
}

static int
_cti_resolveManifestConflicts_callback(void *opaque, const char *key, void *val)
{
	conflict_data *	cb_data = opaque;
	fileEntry_t *	m_ptr;
	fileEntry_t *	s_ptr;
	fileEntry_t *	last;
	fileEntry_t *	tmp;
	fileEntry_t **	entry;

	// sanity
	if (cb_data == NULL || cb_data->sess_files == NULL || key == NULL || val == NULL)
	{
		_cti_set_error("_cti_resolveManifestConflicts_callback: Bad args.");
		return 1;
	}
	
	// check if this key is present in the session files
	s_ptr = _cti_lookupValue(cb_data->sess_files, key);
	if (s_ptr != NULL)
	{
		// The filename is present. Loop over each realname in the manifest list
		m_ptr = val;
		last = NULL;
		while (m_ptr != NULL)
		{
			// check if this realname is already in the session files for type
			if ((entry = _cti_findFileEntryLoc(s_ptr, m_ptr->type)) != NULL)
			{
				// filename is already present in session. Ensure the locations match.
				if (strncmp((*entry)->loc, m_ptr->loc, strlen((*entry)->loc)) == 0)
				{
					// names match, no error just remove from manifest and continue
					if (last == NULL && m_ptr->next == NULL)
					{
						// If this is the only entry we remove the entire key 
						// from the manifest upon return
						if (_cti_push(cb_data->conflicts, (void *)key))
						{
							_cti_set_error("_cti_resolveManifestConflicts_callback: _cti_push() failed.");
							return 1;
						}
						m_ptr = NULL;
					} else if (m_ptr->next == NULL)
					{
						// this handles the case where this is the final entry 
						// in a non-empty chain
						last->next = NULL;
						free(m_ptr->loc);
						free(m_ptr);
						m_ptr = NULL;
					} else
					{
						// this entry is in the middle of the chain
						m_ptr->type = m_ptr->next->type;
						free(m_ptr->loc);
						m_ptr->loc = m_ptr->next->loc;
						tmp = m_ptr->next->next;
						free(m_ptr->next);
						m_ptr->next = tmp;
						// no need to update m_ptr
					}
				} else
				{
					// names do not match, throw error
					_cti_set_error("A %s named %s is already present in the session.", _cti_entryTypeToString(m_ptr->type), m_ptr->loc);
					return 1;
				}
			} else
			{
				// this is a valid file since it isn't in the session. 
				// Update and continue iterating.
				last = m_ptr;
				m_ptr = m_ptr->next;
			}
		}
	}
	
	return 0;
}

static int
_cti_resolveManifestConflicts(manifest_t *m_ptr)
{
	conflict_data *	cb_data;
	int				rtn;
	const char *	str;
	
	// sanity
	if (m_ptr == NULL)
	{
		_cti_set_error("_cti_resolveManifestConflicts: Bad args.");
		return 1;
	}
	
	if ((cb_data = malloc(CTI_BLOCK_SIZE)) == NULL)
	{
		_cti_set_error("_cti_resolveManifestConflicts: malloc failed.");
		return 1;
	}
	
	// setup callback data
	cb_data->sess_files = MANIF_SESS(m_ptr)->files;
	cb_data->conflicts = _cti_newStack();
	if (cb_data->conflicts == NULL)
	{
		_cti_set_error("_cti_resolveManifestConflicts: _cti_newStack failed.");
		free(cb_data);
		return 1;
	}
	
	// iterate over each file in the manifest
	rtn = _cti_forEachString(m_ptr->files, _cti_resolveManifestConflicts_callback, cb_data);
	
	// remove any conflicting file from the manifest
	while ((str = _cti_pop(cb_data->conflicts)) != NULL)
	{
		_cti_removeString(m_ptr->files, str, _cti_consumeFileEntry);
	}
	
	_cti_consumeStack(cb_data->conflicts);
	free(cb_data);
	
	return rtn;
}

static int
_cti_addBinary(manifest_t *m_ptr, const char *fstr)
{
	char *			fullname;	// full path name of the binary to add to the manifest
	char *			realname;	// realname (lacking path info) of the file
	fileEntry_t *	f_ptr;		// pointer to file entry
	int				rtn;
	char **			lib_array;	// the returned list of strings containing the required libraries by the executable
	char **			tmp;		// temporary pointer used to iterate through lists of strings
	
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
	
	// Check if this file is already in the session
	rtn = _cti_checkSessionForConflict(MANIF_SESS(m_ptr), EXEC_ENTRY, realname, fullname);
	if (rtn == 0)
	{
		// file locations match. No conflict, return success since the file is already
		// in the session
		free(fullname);
		free(realname);
		return 0;
	} else if (rtn == 1)
	{
		// realname matches, but fullname conflicts. 
		// error already set
		free(fullname);
		free(realname);
		return 1;
	}
	
	// realname is not in the session string list
	
	// search the manifest string list for this filename
	f_ptr = _cti_lookupValue(m_ptr->files, realname);
	if (f_ptr != NULL)
	{
		// realname is in string list, so try to chain this entry
		rtn = _cti_chainFileEntry(f_ptr, realname, EXEC_ENTRY, fullname);
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
			if (_cti_addLibrary(m_ptr, *tmp))
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

static int
_cti_addLibrary(manifest_t *m_ptr, const char *fstr)
{
	char *			fullname;	// full path name of the library to add to the manifest
	char *			realname;	// realname (lacking path info) of the library
	fileEntry_t *	f_ptr;		// pointer to file entry
	int				rtn;
	
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
	
	// Check if this file is already in the session
	rtn = _cti_checkSessionForConflict(MANIF_SESS(m_ptr), LIB_ENTRY, realname, fullname);
	if (rtn == 0)
	{
		// file locations match. No conflict, return success since the file is already
		// in the session
		free(fullname);
		free(realname);
		return 0;
	} else if (rtn == 1)
	{
		// realname matches, but fullname conflicts. 
		// error already set
		free(fullname);
		free(realname);
		return 1;
	}
	
	// realname is not in the session string list
	
	// search the string list for this filename
	f_ptr = _cti_lookupValue(m_ptr->files, realname);
	if (f_ptr)
	{
		// realname is in string list, so try to chain this entry
		rtn = _cti_chainFileEntry(f_ptr, realname, LIB_ENTRY, fullname);
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
static int
_cti_addLibDir(manifest_t *m_ptr, const char *fstr)
{
	struct stat 	statbuf;
	char *			fullname;	// full path name of the library directory to add to the manifest
	char *			realname;	// realname (lacking path info) of the library directory
	fileEntry_t *	f_ptr;		// pointer to file entry
	int				rtn;		// rtn value
	
	// Ensure the provided directory exists
	if (stat(fstr, &statbuf)) 
	{
		/* can't access file */
		_cti_set_error("Provided path %s does not exist.", fstr);
		return 1;
	}

	// ensure the file is a directory
	if (!S_ISDIR(statbuf.st_mode))
	{
		/* file is not a directory */
		_cti_set_error("Provided path %s is not a directory.", fstr);
		return 1;
	}
	
	// convert the path to its real fullname (i.e. resolve symlinks and get rid of special chars)
	if ((fullname = realpath(fstr, NULL)) == NULL)
	{
		_cti_set_error("Realpath failed.");
		return 1;
	}

	// next just grab the real name (without path information) of the library directory
	if ((realname = _cti_pathToName(fullname)) == NULL)
	{
		_cti_set_error("Could not convert the fullname to realname.");
		free(fullname);
		return 1;
	}
	
	// Check if this file is already in the session
	rtn = _cti_checkSessionForConflict(MANIF_SESS(m_ptr), LIBDIR_ENTRY, realname, fullname);
	if (rtn == 0)
	{
		// file locations match. No conflict, return success since the file is already
		// in the session
		free(fullname);
		free(realname);
		return 0;
	} else if (rtn == 1)
	{
		// realname matches, but fullname conflicts. 
		// error already set
		free(fullname);
		free(realname);
		return 1;
	}
	
	// realname is not in the session string list
	
	// search the string list for this filename
	f_ptr = _cti_lookupValue(m_ptr->files, realname);
	if (f_ptr)
	{
		// realname is in string list, so try to chain this entry
		rtn = _cti_chainFileEntry(f_ptr, realname, LIBDIR_ENTRY, fullname);
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
_cti_addFile(manifest_t *m_ptr, const char *fstr)
{
	char *			fullname;	// full path name of the file to add to the manifest
	char *			realname;	// realname (lacking path info) of the file
	fileEntry_t *	f_ptr;		// pointer to file entry
	int				rtn;		// rtn value
	
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
	
	// Check if this file is already in the session
	rtn = _cti_checkSessionForConflict(MANIF_SESS(m_ptr), FILE_ENTRY, realname, fullname);
	if (rtn == 0)
	{
		// file locations match. No conflict, return success since the file is already
		// in the session
		free(fullname);
		free(realname);
		return 0;
	} else if (rtn == 1)
	{
		// realname matches, but fullname conflicts. 
		// error already set
		free(fullname);
		free(realname);
		return 1;
	}
	
	// realname is not in the session string list
	
	// search the string list for this filename
	f_ptr = _cti_lookupValue(m_ptr->files, realname);
	if (f_ptr)
	{
		// realname is in string list, so try to chain this entry
		rtn = _cti_chainFileEntry(f_ptr, realname, FILE_ENTRY, fullname);
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
_cti_packageManifestAndShip_callback(void *opaque, const char *key, void *val)
{
	package_data *	cb_data = opaque;
	fileEntry_t *	f_ptr = val;

	// sanity
	if (cb_data == NULL || key == NULL || f_ptr == NULL)
	{
		_cti_set_error("_cti_packageManifestAndShip_callback: Bad args.");
		return 1;
	}
	
	// loop over each file in the chain
	while (f_ptr != NULL)
	{
		// switch on file type
		switch (f_ptr->type)
		{
			case EXEC_ENTRY:
				// create relative pathname
				if (snprintf(cb_data->path_buf, PATH_MAX, "%s/%s", cb_data->bin_path, key) <= 0)
				{
					_cti_set_error("_cti_packageManifestAndShip_callback: snprintf failed.");
					return 1;
				}
							
				// copy this file to the archive
				if (_cti_copyFileToArchive(cb_data->a, cb_data->entry, f_ptr->loc, cb_data->path_buf, cb_data->read_buf))
				{
					// error already set
					return 1;
				}
							
				break;
					
			case LIB_ENTRY:
				// create relative pathname
				if (snprintf(cb_data->path_buf, PATH_MAX, "%s/%s", cb_data->lib_path, key) <= 0)
				{
					_cti_set_error("_cti_packageManifestAndShip_callback: snprintf failed.");
					return 1;
				}
							
				// copy this file to the archive
				if (_cti_copyFileToArchive(cb_data->a, cb_data->entry, f_ptr->loc, cb_data->path_buf, cb_data->read_buf))
				{
					// error already set
					return 1;
				}
				
				break;
				
			case LIBDIR_ENTRY:
				// create relative pathname
				if (snprintf(cb_data->path_buf, PATH_MAX, "%s/%s", cb_data->lib_path, key) <= 0)
				{
					_cti_set_error("_cti_packageManifestAndShip_callback: snprintf failed.");
					return 1;
				}
							
				// copy this file to the archive
				if (_cti_copyFileToArchive(cb_data->a, cb_data->entry, f_ptr->loc, cb_data->path_buf, cb_data->read_buf))
				{
					// error already set
					return 1;
				}
				
				break;
				
			case FILE_ENTRY:
				// create relative pathname.
				if (snprintf(cb_data->path_buf, PATH_MAX, "%s/%s", cb_data->file_path, key) <= 0)
				{
					_cti_set_error("_cti_packageManifestAndShip_callback: snprintf failed.");
					return 1;
				}
				
				// copy this file to the archive
				if (_cti_copyFileToArchive(cb_data->a, cb_data->entry, f_ptr->loc, cb_data->path_buf, cb_data->read_buf))
				{
					// error already set
					return 1;
				}
				
				break;
		}
	
		// point at the next fileEntry in the chain
		f_ptr = f_ptr->next;
	}
	
	// done
	return 0;
}

static int
_cti_packageManifestAndShip(manifest_t *m_ptr)
{
	const char *			cfg_dir = NULL;		// tmp directory
	char *					bin_path = NULL;
	char *					lib_path = NULL;
	char *					tmp_path = NULL;
	const char * const *	wlm_files;
	char *					tar_name = NULL;
	char *					hidden_tar_name = NULL;
	FILE *					hfp = NULL;
	pid_t					pid;
	struct archive *		a = NULL;
	struct archive_entry *	entry = NULL;
	char *					read_buf = NULL;
	package_data *			cb_data = NULL;
	cti_signals_t *			signals = NULL;		// BUG 819725
	int						rtn = 0;			// return value
	
	// sanity check
	if (m_ptr == NULL)
	{
		_cti_set_error("_cti_packageManifestAndShip: invalid args.");
		return 1;
	}
	
	// set inst id in manifest, we increment this in the session only upon success.
	m_ptr->inst = MANIF_SESS(m_ptr)->instCnt + 1;
	
	// Add extra files needed by the WLM. We only do this if it is the first 
	// instance, otherwise the extra files will have already been shipped.
	if (MANIF_SESS(m_ptr)->instCnt == 0)
	{
		// grab extra binaries
		if ((wlm_files = MANIF_APP(m_ptr)->wlmProto->wlm_extraBinaries(MANIF_APP(m_ptr)->_wlmObj)) != NULL)
		{
			while (*wlm_files != NULL)
			{
				if (_cti_addBinary(m_ptr, *wlm_files))
				{
					// Failed to add the binary to the manifest - catastrophic failure
					return 1;
				}
				++wlm_files;
			}
		}
		
		// grab extra libraries
		if ((wlm_files = MANIF_APP(m_ptr)->wlmProto->wlm_extraLibraries(MANIF_APP(m_ptr)->_wlmObj)) != NULL)
		{
			while (*wlm_files != NULL)
			{
				if (_cti_addLibrary(m_ptr, *wlm_files))
				{
					// Failed to add the binary to the manifest - catastrophic failure
					return 1;
				}
				++wlm_files;
			}
		}
		
		// grab extra library directories
		if ((wlm_files = MANIF_APP(m_ptr)->wlmProto->wlm_extraLibDirs(MANIF_APP(m_ptr)->_wlmObj)) != NULL)
		{
			while (*wlm_files != NULL)
			{
				if (_cti_addLibDir(m_ptr, *wlm_files))
				{
					// Failed to add the binary to the manifest - catastrophic failure
					return 1;
				}
				++wlm_files;
			}
		}
		
		// grab extra files
		if ((wlm_files = MANIF_APP(m_ptr)->wlmProto->wlm_extraFiles(MANIF_APP(m_ptr)->_wlmObj)) != NULL)
		{
			while (*wlm_files != NULL)
			{
				if (_cti_addFile(m_ptr, *wlm_files))
				{
					// Failed to add the binary to the manifest - catastrophic failure
					return 1;
				}
				++wlm_files;
			}
		}
	}
	
	// We need to reconcile the manifest files with the session files a second time.
	// This avoids the conflict where two manifests contain the same files. A caller
	// can create two manifests, and add the same file to both. No conflict will be
	// found since manifest state is not checked between two manifests. When the
	// first manifest is shipped, the conflicting file will be saved into the
	// session string list, but would be two late for the second manifest. By
	// checking the session string list at ship time, we avoid this problem.
	if (_cti_resolveManifestConflicts(m_ptr))
	{
		// error already set
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
	
	// create the required subdirectory strings
	if (asprintf(&bin_path, "%s/bin", MANIF_SESS(m_ptr)->stage_name) <= 0)
	{
		_cti_set_error("_cti_packageManifestAndShip: asprintf failed.");
		goto packageManifestAndShip_error;
	}
	if (asprintf(&lib_path, "%s/lib", MANIF_SESS(m_ptr)->stage_name) <= 0)
	{
		_cti_set_error("_cti_packageManifestAndShip: asprintf failed.");
		goto packageManifestAndShip_error;
	}
	if (asprintf(&tmp_path, "%s/tmp", MANIF_SESS(m_ptr)->stage_name) <= 0)
	{
		_cti_set_error("_cti_packageManifestAndShip: asprintf failed.");
		goto packageManifestAndShip_error;
	}
	
	// create the tarball name based on its instance to prevent a race 
	// condition where the dlaunch on the compute node has not yet extracted the 
	// previously shipped tarball, and we overwrite it with this new one. This
	// does not impact the name of the directory in the tarball.
	if (asprintf(&tar_name, "%s/%s%d.tar", cfg_dir, MANIF_SESS(m_ptr)->stage_name, m_ptr->inst) <= 0)
	{
		_cti_set_error("_cti_packageManifestAndShip: asprintf failed.");
		goto packageManifestAndShip_error;
	}
	
	// create the hidden name for the cleanup file. This will be checked by future
	// runs to try assisting in cleanup if we get killed unexpectedly. This is cludge
	// in an attempt to cleanup. The ideal situation is to be able to tell the kernel
	// to remove the tarball if the process exits, but no mechanism exists today that
	// I know about.
	if (asprintf(&hidden_tar_name, "%s/.%s%d.tar", cfg_dir, MANIF_SESS(m_ptr)->stage_name, m_ptr->inst) <= 0)
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
	if (_cti_addDirToArchive(a, entry, MANIF_SESS(m_ptr)->stage_name))
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

	// malloc callback data type
	if ((cb_data = malloc(sizeof(package_data))) == NULL)
	{
		_cti_set_error("_cti_packageManifestAndShip: malloc failed.");
		goto packageManifestAndShip_error;
	}
	
	// setup the callback data type
	cb_data->bin_path = bin_path;
	cb_data->lib_path = lib_path;
	// file path points at the top level directory for now
	cb_data->file_path = MANIF_SESS(m_ptr)->stage_name;
	cb_data->a = a;
	cb_data->entry = entry;
	cb_data->read_buf = read_buf;
	
	// Add the manifest files to the tarball
	if (_cti_forEachString(m_ptr->files, _cti_packageManifestAndShip_callback, cb_data))
	{
		// error already set
		goto packageManifestAndShip_error;
	}
	
	// Call the appropriate transfer function based on the wlm
	if (MANIF_APP(m_ptr)->wlmProto->wlm_shipPackage(MANIF_APP(m_ptr)->_wlmObj, tar_name))
	{
		// we failed to ship the file to the compute nodes for some reason - catastrophic failure
		// Error message already set
		goto packageManifestAndShip_error;
	}
	
	// increment instCnt in session obj since we shipped successfully
	MANIF_SESS(m_ptr)->instCnt++;
	
	// clean things up
	free(cb_data);
	cb_data = NULL;
	free(read_buf);
	read_buf = NULL;
	archive_entry_free(entry);
	entry = NULL;
	archive_write_free(a);
	a = NULL;
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
	
	if (cb_data != NULL)
	{
		free(cb_data);
	}
	
	// BUG 819725: Unblock signals. We are done with the critical section
	if (signals != NULL)
	{
		_cti_end_critical_section(signals);
	}

	return 1;
}

/***********************************
** API defined functions start here
***********************************/

cti_session_id_t
cti_createSession(cti_app_id_t appId)
{
	appEntry_t *	app_ptr;	// points at the app entry to associate with this session
	session_t *		s_ptr;
	
	// sanity check
	if (appId == 0)
	{
		_cti_set_error("Invalid cti_app_id_t %d.", (int)appId);
		return 0;
	}
	
	// try to find an entry in the my_apps array for the appId
	if ((app_ptr = _cti_findAppEntry(appId)) == NULL)
	{
		// appId not found in the global my_apps array - unknown appId failure
		// error string already set
		return 0;
	}
	
	if ((s_ptr = _cti_newSession(app_ptr)) == NULL)
	{
		// error string already set
		return 0;
	}
	
	return s_ptr->sid;
}

int
cti_sessionIsValid(cti_session_id_t sid)
{
	// sanity check
	if (sid == 0)
	{
		_cti_set_error("Invalid cti_session_id_t %d.", (int)sid);
		return 0;
	}
	
	// Find the session for this sid
	if (_cti_findSession(sid) == NULL)
	{
		// sid not found
		// error string already set
		return 0;
	}
	
	return 1;
}

cti_manifest_id_t
cti_createManifest(cti_session_id_t sid)
{
	session_t *		s_ptr;
	manifest_t *	m_ptr = NULL;
	
	// sanity check
	if (sid == 0)
	{
		_cti_set_error("Invalid cti_session_id_t %d.", (int)sid);
		return 0;
	}
	
	// Find the session for this sid
	if ((s_ptr = _cti_findSession(sid)) == NULL)
	{
		// sid not found
		// error string already set
		return 0;
	}
	
	if ((m_ptr = _cti_newManifest(s_ptr)) == NULL)
	{
		// error string already set
		return 0;
	}
	
	return m_ptr->mid;
}

int
cti_manifestIsValid(cti_manifest_id_t mid)
{
	// sanity check
	if (mid == 0)
	{
		_cti_set_error("Invalid cti_manifest_id_t %d.", (int)mid);
		return 0;
	}
	
	// Find the manifest entry in the global manifest list for the mid
	if (_cti_findManifest(mid) == NULL)
	{
		// We failed to find the manifest for the mid
		// error string already set
		return 0;
	}
	
	return 1;
}

int
cti_addManifestBinary(cti_manifest_id_t mid, const char *fstr)
{
	manifest_t *	m_ptr;		// pointer to the manifest_t object associated with the cti_manifest_id_t argument
	
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
	
	return _cti_addBinary(m_ptr, fstr);
}

int
cti_addManifestLibrary(cti_manifest_id_t mid, const char *fstr)
{
	manifest_t *	m_ptr;		// pointer to the manifest_t object associated with the cti_manifest_id_t argument
	
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
	
	return _cti_addLibrary(m_ptr, fstr);
}

int
cti_addManifestLibDir(cti_manifest_id_t mid, const char *fstr)
{
	manifest_t *	m_ptr;		// pointer to the manifest_t object associated with the cti_manifest_id_t argument
	
	// sanity check
	if (mid <= 0)
	{
		_cti_set_error("Invalid cti_manifest_id_t %d.", (int)mid);
		return 1;
	}
	
	if (fstr == NULL)
	{
		_cti_set_error("cti_addManifestLibDir had null fstr.");
		return 1;
	}
	
	// Find the manifest entry in the global manifest list for the mid
	if ((m_ptr = _cti_findManifest(mid)) == NULL)
	{
		// We failed to find the manifest for the mid
		// error string already set
		return 1;
	}
	
	return _cti_addLibDir(m_ptr, fstr);
}

int
cti_addManifestFile(cti_manifest_id_t mid, const char *fstr)
{
	manifest_t *	m_ptr;		// pointer to the manifest_t object associated with the cti_manifest_id_t argument
	
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
	
	return _cti_addFile(m_ptr, fstr);
}

int
cti_sendManifest(cti_manifest_id_t mid, int dbg)
{
	manifest_t *		m_ptr = NULL;	// pointer to the manifest_t object associated with the cti_manifest_id_t argument
	char *				jid_str = NULL;	// job identifier string - wlm specific
	cti_args_t *		d_args = NULL;	// args to pass to the daemon launcher
	int					r;

	// sanity check
	if (mid <= 0)
	{
		_cti_set_error("Invalid cti_manifest_id_t %d.", (int)mid);
	}
	
	// find the manifest_t for the mid
	if ((m_ptr = _cti_findManifest(mid)) == NULL)
	{
		// We failed to find the manifest for the mid
		// error string already set
		goto sendManifest_error;
	}
	
	// ship the manifest tarball to the compute nodes
	r = _cti_packageManifestAndShip(m_ptr);
	if (r == 1)
	{
		// Failed to ship the manifest - catastrophic failure
		// error string already set
		goto sendManifest_error;
	}
	// if there was nothing to ship, ensure there was a session, otherwise error
	if (r == 2)
	{
		// remove the manifest and return without error
		_cti_consumeManifest(m_ptr);
		return 0;
	}
	
	// now we need to create the argv for the actual call to the WLM wrapper call
	//
	// The options passed MUST correspond to the options defined in the daemon_launcher program.
	//
	// The actual daemon launcher path string is determined by the wlm_startDaemon call
	// since that is wlm specific
	
	if ((jid_str = MANIF_APP(m_ptr)->wlmProto->wlm_getJobId(MANIF_APP(m_ptr)->_wlmObj)) == NULL)
	{
		// error already set
		goto sendManifest_error;
	}
	
	// create a new args obj
	if ((d_args = _cti_newArgs()) == NULL)
	{
		_cti_set_error("_cti_newArgs failed.");
		goto sendManifest_error;
	}
	
	// begin adding the args
	
	if (_cti_addArg(d_args, "-a"))
	{
		_cti_set_error("_cti_addArg failed.");
		goto sendManifest_error;
	}
	if (_cti_addArg(d_args, "%s", jid_str))
	{
		_cti_set_error("_cti_addArg failed.");
		goto sendManifest_error;
	}
	
	if (_cti_addArg(d_args, "-p"))
	{
		_cti_set_error("_cti_addArg failed.");
		goto sendManifest_error;
	}
	if (_cti_addArg(d_args, "%s", MANIF_SESS(m_ptr)->toolPath))
	{
		_cti_set_error("_cti_addArg failed.");
		goto sendManifest_error;
	}
	
	if (_cti_addArg(d_args, "-w"))
	{
		_cti_set_error("_cti_addArg failed.");
		goto sendManifest_error;
	}
	if (_cti_addArg(d_args, "%d", MANIF_APP(m_ptr)->wlmProto->wlm_type))
	{
		_cti_set_error("_cti_addArg failed.");
		goto sendManifest_error;
	}
	
	if (_cti_addArg(d_args, "-m"))
	{
		_cti_set_error("_cti_addArg failed.");
		goto sendManifest_error;
	}
	if (_cti_addArg(d_args, "%s%d.tar", MANIF_SESS(m_ptr)->stage_name, m_ptr->inst))
	{
		_cti_set_error("_cti_addArg failed.");
		goto sendManifest_error;
	}
	
	if (_cti_addArg(d_args, "-d"))
	{
		_cti_set_error("_cti_addArg failed.");
		goto sendManifest_error;
	}
	if (_cti_addArg(d_args, "%s", MANIF_SESS(m_ptr)->stage_name))
	{
		_cti_set_error("_cti_addArg failed.");
		goto sendManifest_error;
	}
	
	if (_cti_addArg(d_args, "-i"))
	{
		_cti_set_error("_cti_addArg failed.");
		goto sendManifest_error;
	}
	if (_cti_addArg(d_args, "%d", m_ptr->inst))
	{
		_cti_set_error("_cti_addArg failed.");
		goto sendManifest_error;
	}
	
	// add the debug switch if debug is on
	if (dbg)
	{
		if (_cti_addArg(d_args, "--debug"))
		{
			_cti_set_error("_cti_addArg failed.");
			goto sendManifest_error;
		}
	}
	
	// Done. We now have an argv array to pass
	
	// Call the appropriate transfer function based on the wlm
	if (MANIF_APP(m_ptr)->wlmProto->wlm_startDaemon(MANIF_APP(m_ptr)->_wlmObj, d_args))
	{
		// we failed to ship the file to the compute nodes for some reason - catastrophic failure
		// Error message already set
		goto sendManifest_error;
	}
	
	// Merge the manifest into the existing session
	if (_cti_addManifestToSession(m_ptr))
	{
		// we failed to merge the manifest into the session - catastrophic failure
		// error string already set
		goto sendManifest_error;
	}
	
	// cleanup
	_cti_consumeManifest(m_ptr);
	free(jid_str);
	_cti_freeArgs(d_args);
	
	return 0;
	
sendManifest_error:

	if (m_ptr != NULL)
	{
		_cti_consumeManifest(m_ptr);
	}
	
	if (jid_str != NULL)
	{
		free(jid_str);
	}
	
	if (d_args != NULL)
	{
		_cti_freeArgs(d_args);
	}

	return 1;
}

int
cti_execToolDaemon(cti_manifest_id_t mid, const char *daemon, const char * const args[], const char * const env[], int dbg)
{
	manifest_t *	m_ptr = NULL;		// pointer to the manifest_t object associated with the cti_manifest_id_t argument
	bool			useManif = true;	// controls if a manifest was shipped or not
	int				r;					// return value from send manif call
	char *			fullname = NULL;	// full path name of the executable to launch as a tool daemon
	char *			realname = NULL;	// realname (lacking path info) of the executable
	char *			jid_str = NULL;		// job id string to pass to the backend. This is wlm specific.
	const char *	attribsPath;		// path to pmi_attribs file based on the wlm
	cti_args_t *	d_args = NULL;		// args to pass to the daemon launcher
	
	// sanity check
	if (daemon == NULL)
	{
		_cti_set_error("Required tool daemon argument is missing.");
		goto execToolDaemon_error;
	}
	
	// sanity check
	if (mid <= 0)
	{
		_cti_set_error("Invalid cti_manifest_id_t %d.", (int)mid);
	}
	
	// try to find the manifest_t for the mid
	if ((m_ptr = _cti_findManifest(mid)) == NULL)
	{
		// We failed to find the manifest for the mid
		// error string already set
		goto execToolDaemon_error;
	}
	
	// add the daemon to the manifest
	if (_cti_addBinary(m_ptr, daemon))
	{
		// Failed to add the binary to the manifest - catastrophic failure
		// error string already set
		goto execToolDaemon_error;
	}
	
	// Ensure that there are files to ship, otherwise there is no need to ship a
	// tarball, everything we need already has been transfered to the nodes
	// Try to ship the tarball, if this returns with 2 everything we need already
	// has been transfered to the nodes and there is no need to ship it again.
	r = _cti_packageManifestAndShip(m_ptr);
	if (r == 1)
	{
		// Failed to ship the manifest - catastrophic failure
		// error string already set
		goto execToolDaemon_error;
	}
	if (r == 2)
	{
		// manifest was not shipped.
		useManif = false;
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
		goto execToolDaemon_error;
	}
	
	// next just grab the real name (without path information) of the binary
	if ((realname = _cti_pathToName(fullname)) == NULL)
	{
		_cti_set_error("Could not convert the tool daemon binary fullname to realname.");
		goto execToolDaemon_error;
	}
	
	if ((jid_str = MANIF_APP(m_ptr)->wlmProto->wlm_getJobId(MANIF_APP(m_ptr)->_wlmObj)) == NULL)
	{
		// error already set
		goto execToolDaemon_error;
	}
	
	// Get the attribs path for this wlm - this is optional and can come back null
	attribsPath = MANIF_APP(m_ptr)->wlmProto->wlm_getAttribsPath(MANIF_APP(m_ptr)->_wlmObj);
	
	// create a new args obj
	if ((d_args = _cti_newArgs()) == NULL)
	{
		_cti_set_error("_cti_newArgs failed.");
		goto execToolDaemon_error;
	} 
	
	// begin adding the args
	
	if (_cti_addArg(d_args, "-a"))
	{
		_cti_set_error("_cti_addArg failed.");
		goto execToolDaemon_error;
	}
	if (_cti_addArg(d_args, "%s", jid_str))
	{
		_cti_set_error("_cti_addArg failed.");
		goto execToolDaemon_error;
	}
	
	if (_cti_addArg(d_args, "-p"))
	{
		_cti_set_error("_cti_addArg failed.");
		goto execToolDaemon_error;
	}
	if (_cti_addArg(d_args, "%s", MANIF_SESS(m_ptr)->toolPath))
	{
		_cti_set_error("_cti_addArg failed.");
		goto execToolDaemon_error;
	}
	
	if (attribsPath != NULL)
	{
		if (_cti_addArg(d_args, "-t"))
		{
			_cti_set_error("_cti_addArg failed.");
			goto execToolDaemon_error;
		}
		if (_cti_addArg(d_args, "%s", attribsPath))
		{
			_cti_set_error("_cti_addArg failed.");
			goto execToolDaemon_error;
		}
	}
	
	if (_cti_addArg(d_args, "-w"))
	{
		_cti_set_error("_cti_addArg failed.");
		goto execToolDaemon_error;
	}
	if (_cti_addArg(d_args, "%d", MANIF_APP(m_ptr)->wlmProto->wlm_type))
	{
		_cti_set_error("_cti_addArg failed.");
		goto execToolDaemon_error;
	}
	
	if (_cti_addArg(d_args, "-b"))
	{
		_cti_set_error("_cti_addArg failed.");
		goto execToolDaemon_error;
	}
	if (_cti_addArg(d_args, "%s", realname))
	{
		_cti_set_error("_cti_addArg failed.");
		goto execToolDaemon_error;
	}
	
	if (_cti_addArg(d_args, "-d"))
	{
		_cti_set_error("_cti_addArg failed.");
		goto execToolDaemon_error;
	}
	if (_cti_addArg(d_args, "%s", MANIF_SESS(m_ptr)->stage_name))
	{
		_cti_set_error("_cti_addArg failed.");
		goto execToolDaemon_error;
	}
	
	if (_cti_addArg(d_args, "-i"))
	{
		_cti_set_error("_cti_addArg failed.");
		goto execToolDaemon_error;
	}
	if (_cti_addArg(d_args, "%d", m_ptr->inst))
	{
		_cti_set_error("_cti_addArg failed.");
		goto execToolDaemon_error;
	}
	
	// add -m argument if needed
	if (useManif)
	{
		if (_cti_addArg(d_args, "-m"))
		{
			_cti_set_error("_cti_addArg failed.");
			goto execToolDaemon_error;
		}
		if (_cti_addArg(d_args, "%s%d.tar", MANIF_SESS(m_ptr)->stage_name, m_ptr->inst))
		{
			_cti_set_error("_cti_addArg failed.");
			goto execToolDaemon_error;
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
				goto execToolDaemon_error;
			}
			if (_cti_addArg(d_args, "%s", *env++))
			{
				_cti_set_error("_cti_addArg failed.");
				goto execToolDaemon_error;
			}
		}
	}
		
	// add the debug switch if debug is on
	if (dbg)
	{
		if (_cti_addArg(d_args, "--debug"))
		{
			_cti_set_error("_cti_addArg failed.");
			goto execToolDaemon_error;
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
			goto execToolDaemon_error;
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
				goto execToolDaemon_error;
			}
		}
	}
		
	// Done. We now have an argv array to pass
	
	// Call the appropriate transfer function based on the wlm
	if (MANIF_APP(m_ptr)->wlmProto->wlm_startDaemon(MANIF_APP(m_ptr)->_wlmObj, d_args))
	{
		// we failed to ship the file to the compute nodes for some reason - catastrophic failure
		// Error message already set
		goto execToolDaemon_error;
	}
	
	// Merge the manifest into the existing session only if we needed to
	// transfer any files
	if (useManif)
	{
		if (_cti_addManifestToSession(m_ptr))
		{
			// we failed to merge the manifest into the session - catastrophic failure
			// error string already set
			goto execToolDaemon_error;
		}
	}
	
	// cleanup
	_cti_consumeManifest(m_ptr);
	free(fullname);
	free(realname);
	free(jid_str);
	_cti_freeArgs(d_args);
	
	return 0;
	
execToolDaemon_error:
	
	if (m_ptr != NULL)
	{
		_cti_consumeManifest(m_ptr);
	}
	
	if (fullname != NULL)
	{
		free(fullname);
	}
	
	if (realname != NULL)
	{
		free(realname);
	}
	
	if (jid_str != NULL)
	{
		free(jid_str);
	}
	
	if (d_args != NULL)
	{
		_cti_freeArgs(d_args);
	}
	
	return 1;	
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
	
	// ensure that there are instances in the session
	if (s_ptr->instCnt == 0)
	{
		_cti_set_error("Backed not yet initialized for cti_session_id_t %d.", (int)sid);
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

