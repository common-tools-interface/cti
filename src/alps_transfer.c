/*********************************************************************************\
 * alps_transfer.c - A interface to the alps toolhelper functions. This provides
 *		   a tool developer with an easy to use interface to transfer
 *		   binaries, shared libraries, and files to the compute nodes
 *		   associated with an aprun pid. This can also be used to launch
 *		   tool daemons on the compute nodes in an automated way.
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

#include "alps/libalps.h"

#include "alps_transfer.h"
#include "alps_application.h"
#include "ld_val.h"

#include "useful/useful.h"

/* 
** This list may need to be updated with each new release of CNL.
*/
static const char * __ignored_libs[] = {
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
static manifList_t *	growManifList(void);
static void				reapManifList(void);
static void				reapManifest(MANIFEST_ID);
static void				consumeManifest(manifest_t *);
static manifest_t *		findManifest(MANIFEST_ID);
static manifest_t *		newManifest(void);
static int				removeFilesFromDir(char *);
static int				copyFilesToPackage(stringList_t *, char *);
static int				packageManifestAndShip(uint64_t, MANIFEST_ID);

/* global variables */
static manifList_t *	my_manifs;
static MANIFEST_ID		next_mid = 1;

static manifList_t *
growManifList(void)
{
	manifList_t *	newEntry;
	manifList_t *	lstPtr;
	
	// alloc space for the new list entry
	if ((newEntry = malloc(sizeof(manifList_t))) == (void *)0)
	{
		return NULL;
	}
	memset(newEntry, 0, sizeof(manifList_t));     // clear it to NULL
	
	// if my_manifs is null, this is the new head of the list
	if ((lstPtr = my_manifs) == NULL)
	{
		my_manifs = newEntry;
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

// this function is used to undo the operation performed by growManifList
static void
reapManifList(void)
{
	manifList_t *	lstPtr;

	// sanity check
	if ((lstPtr = my_manifs) == NULL)
		return;
	
	// if this was the first entry, lets remove it
	if (lstPtr->nextEntry == NULL)
	{
		free(lstPtr);
		my_manifs = NULL;
		return;
	}
	
	// iterate through until we find the entry whos next entry has a null next entry ;)
	// i.e. magic - this works because growManifList always places the new manifest_t entry
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
reapManifest(MANIFEST_ID mid)
{
	manifList_t *	lstPtr;
	manifList_t *	prePtr;
	
	// sanity check
	if (((lstPtr = my_manifs) == NULL) || (mid <= 0))
		return;
	
	prePtr = my_manifs;
	
	// this shouldn't happen, but doing so will prevent a segfault if the list gets corrupted
	while (lstPtr->thisEntry == NULL)
	{
		// if this is the only object in the list, then delete the entire list
		if ((lstPtr = lstPtr->nextEntry) == NULL)
		{
			my_manifs = NULL;
			free(lstPtr);
			return;
		}
		// otherwise point my_manifs to the lstPtr and free the corrupt entry
		my_manifs = lstPtr;
		free(prePtr);
		prePtr = my_manifs;
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
	
	// check to see if this was the first entry in the global my_manifs list
	if (prePtr == lstPtr)
	{
		// point the global my_manifs list to the next entry
		my_manifs = lstPtr->nextEntry;
		// consume the manifest_t object for this entry in the list
		consumeManifest(lstPtr->thisEntry);
		// free the list object
		free(lstPtr);
	} else
	{
		// we are at some point midway through the global my_manifs list
		
		// point the previous entries next entry to the list pointers next entry
		// this bypasses the current list pointer
		prePtr->nextEntry = lstPtr->nextEntry;
		// consume the manifest_t object for this entry in the list
		consumeManifest(lstPtr->thisEntry);
		// free the list object
		free(lstPtr);
	}
	
	// done
	return;
}

static void
consumeManifest(manifest_t *entry)
{
	// sanity check
	if (entry == NULL)
		return;
		
	// free the root location
	if (entry->tarball_name != NULL)
		free(entry->tarball_name);
	
	// eat each of the string lists
	consumeStringList(entry->exec_names);
	consumeStringList(entry->lib_names);
	consumeStringList(entry->file_names);
	consumeStringList(entry->exec_loc);
	consumeStringList(entry->lib_loc);
	consumeStringList(entry->file_loc);
	
	// nom nom the final manifest_t object
	free(entry);
}

static manifest_t *
findManifest(MANIFEST_ID mid)
{
	manifList_t *	lstPtr;
	
	// sanity check
	if (((lstPtr = my_manifs) == NULL) || (mid <= 0))
		return NULL;
	
	// this shouldn't happen, but doing so will prevent a segfault if the list gets corrupted
	while (lstPtr->thisEntry == NULL)
	{
		// if this is the only object in the list, then delete the entire list
		if ((lstPtr = lstPtr->nextEntry) == NULL)
		{
			my_manifs = NULL;
			free(lstPtr);
			return NULL;
		}
		// otherwise point my_manifs to the lstPtr and free the corrupt entry
		my_manifs = lstPtr;
	}
	
	// we need to locate the position of the manifList_t object that we are looking for
	while (lstPtr->thisEntry->mid != mid)
	{
		if ((lstPtr = lstPtr->nextEntry) == NULL)
		{
			// there are no more entries and we didn't find the mid
			return NULL;
		}
	}
	
	return lstPtr->thisEntry;
}

static manifest_t *
newManifest(void)
{
	manifList_t *	lstPtr;
	manifest_t *	this;
	const char **	ignore_ptr;
	
	// grow the global my_manifs list and get its new manifest_t entry
	if ((lstPtr = growManifList()) == NULL)
	{
		return NULL;
	}
	
	// create the new manifest_t object
	if ((this = malloc(sizeof(manifest_t))) == (void *)0)
	{
		reapManifList();
		return NULL;
	}
	memset(this, 0, sizeof(manifest_t));     // clear it to NULL
	
	// set the mid member
	// TODO: This could be made smarter by using a hash table instead of a revolving int we see now
	this->mid = next_mid++;
	
	// create the stringList_t objects
	if ((this->exec_names = newStringList()) == NULL)
	{
		reapManifList();
		consumeManifest(this);
		return NULL;
	}
	if ((this->lib_names = newStringList()) == NULL)
	{
		reapManifList();
		consumeManifest(this);
		return NULL;
	}
	// Add the ignored library strings to the shipped_libs string list.
	for (ignore_ptr=__ignored_libs; *ignore_ptr != NULL; ++ignore_ptr)
	{
		if (addString(this->lib_names, *ignore_ptr))
		{
			reapManifList();
			consumeManifest(this);
			return NULL;
		}
	}
	if ((this->file_names = newStringList()) == NULL)
	{
		reapManifList();
		consumeManifest(this);
		return NULL;
	}
	if ((this->exec_loc = newStringList()) == NULL)
	{
		reapManifList();
		consumeManifest(this);
		return NULL;
	}
	if ((this->lib_loc = newStringList()) == NULL)
	{
		reapManifList();
		consumeManifest(this);
		return NULL;
	}
	if ((this->file_loc = newStringList()) == NULL)
	{
		reapManifList();
		consumeManifest(this);
		return NULL;
	}
	
	// save the new manifest_t object into the returned manifList_t object that
	// the call to growManifList gave us.
	lstPtr->thisEntry = this;
	
	return this;
}

MANIFEST_ID
createNewManifest(void)
{
	manifest_t *	m_ptr = NULL;
	
	if ((m_ptr = newManifest()) == NULL)
		return 0;
		
	return m_ptr->mid;
}

void
destroyManifest(MANIFEST_ID mid)
{
	reapManifest(mid);
}

int
addManifestBinary(MANIFEST_ID mid, char *fstr)
{
	manifest_t *	m_ptr;		// pointer to the manifest_t object associated with the MANIFEST_ID argument
	char *			fullname;	// full path name of the binary to add to the manifest
	char *			realname;	// realname (lacking path info) of the file
	char **			lib_array;	// the returned list of strings containing the required libraries by the executable
	char **			tmp;		// temporary pointer used to iterate through lists of strings
	
	// sanity check
	if (mid <= 0 || fstr == NULL)
		return 1;
		
	// Find the manifest entry in the global manifest list for the mid
	if ((m_ptr = findManifest(mid)) == NULL)
	{
		// We failed to find the manifest for the mid
		fprintf(stderr, "MANIFEST_ID %d does not exist.\n", mid);
		return 1;
	}
	
	// first we should ensure that the binary exists and convert it to its fullpath name
	if ((fullname = pathFind(fstr, NULL)) == (char *)NULL)
	{
		fprintf(stderr, "Could not locate the specified file in PATH.\n");
		return 1;
	}
	
	// next just grab the real name (without path information) of the executable
	if ((realname = pathToName(fullname)) == (char *)NULL)
	{
		fprintf(stderr, "Could not convert the fullname to realname.\n");
		return 1;
	}
	
	// search the exec_names list for a duplicate filename
	if (!searchStringList(m_ptr->exec_names, realname))
	{
		// not found in list, so this is a unique file name
		
		// add realname to the names list
		if (addString(m_ptr->exec_names, realname))
		{
			// failed to save realname into the list
			return 1;
		}
		
		// add fullname to the loc list
		if (addString(m_ptr->exec_loc, fullname))
		{
			// failed to save fullname into the list
			return 1;
		}
		
		// call the ld_val interface to determine if this executable has any dso requirements
		lib_array = ld_val(fullname);
		
		// If this executable has dso requirements. We need to add them to the manifest
		tmp = lib_array;
		// ensure the are actual entries before dereferencing tmp
		if (tmp != (char **)NULL)
		{
			while (*tmp != (char *)NULL)
			{
				if (addManifestLibrary(m_ptr->mid, *tmp++))
				{
					// if we return with non-zero status, catastrophic failure occured
					return 1;
				}
			}
			
			tmp = lib_array;
			while (*tmp != (char *)NULL)
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
addManifestLibrary(MANIFEST_ID mid, char *fstr)
{
	manifest_t *	m_ptr;		// pointer to the manifest_t object associated with the MANIFEST_ID argument
	char *			fullname;	// full path name of the library to add to the manifest
	char *			realname;	// realname (lacking path info) of the library
	
	// sanity check
	if (mid <= 0 || fstr == NULL)
		return 1;
		
	// Find the manifest entry in the global manifest list for the mid
	if ((m_ptr = findManifest(mid)) == NULL)
	{
		// We failed to find the manifest for the mid
		fprintf(stderr, "MANIFEST_ID %d does not exist.\n", mid);
		return 1;
	}
	
	// first we should ensure that the library exists and convert it to its fullpath name
	if ((fullname = libFind(fstr, NULL)) == (char *)NULL)
	{
		fprintf(stderr, "Could not locate the specified library in LD_LIBRARY_PATH or system location.\n");
		return 1;
	}
	
	// next just grab the real name (without path information) of the library
	if ((realname = pathToName(fullname)) == (char *)NULL)
	{
		fprintf(stderr, "Could not convert the fullname to realname.\n");
		return 1;
	}
	
	// TODO: We need to create a way to ship conflicting libraries. Since most libraries are sym links to
	// their proper version, name collisions are possible. In the future, the launcher should be able to handle
	// this by pointing its LD_LIBRARY_PATH to a custom directory containing the conflicting lib.
	
	// search the lib_names list for a duplicate filename
	if (!searchStringList(m_ptr->lib_names, realname))
	{
		// not found in list, so this is a unique file name
		
		// add realname to the names list
		if (addString(m_ptr->lib_names, realname))
		{
			// failed to save realname into the list
			return 1;
		}
		
		// add fullname to the loc list
		if (addString(m_ptr->lib_loc, fullname))
		{
			// failed to save fullname into the list
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
addManifestFile(MANIFEST_ID mid, char *fstr)
{
	manifest_t *	m_ptr;		// pointer to the manifest_t object associated with the MANIFEST_ID argument
	char *			fullname;	// full path name of the file to add to the manifest
	char *			realname;	// realname (lacking path info) of the file
	
	// sanity check
	if (mid <= 0 || fstr == NULL)
		return 1;
		
	// Find the manifest entry in the global manifest list for the mid
	if ((m_ptr = findManifest(mid)) == NULL)
	{
		// We failed to find the manifest for the mid
		fprintf(stderr, "MANIFEST_ID %d does not exist.\n", mid);
		return 1;
	}
	
	// first we should ensure that the file exists and convert it to its fullpath name
	if ((fullname = pathFind(fstr, NULL)) == (char *)NULL)
	{
		fprintf(stderr, "Could not locate the specified file in PATH.\n");
		return 1;
	}
	
	// next just grab the real name (without path information) of the library
	if ((realname = pathToName(fullname)) == (char *)NULL)
	{
		fprintf(stderr, "Could not convert the fullname to realname.\n");
		return 1;
	}
	
	// search the file_names list for a duplicate filename
	if (!searchStringList(m_ptr->file_names, realname))
	{
		// not found in list, so this is a unique file name
		
		// add realname to the names list
		if (addString(m_ptr->file_names, realname))
		{
			// failed to save realname into the list
			return 1;
		}
		
		// add fullname to the loc list
		if (addString(m_ptr->file_loc, fullname))
		{
			// failed to save fullname into the list
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
removeFilesFromDir(char *path)
{
	struct dirent *	d;
	DIR *			dir;
	char *			name_path = NULL;
	
	if ((dir = opendir(path)) == NULL)
	{
		perror("opendir");
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
				fprintf(stderr, "asprintf failed.\n");
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
		perror("readdir");
		closedir(dir);
		return 1;
	}
	
	closedir(dir);
	return 0;
}

static int
copyFilesToPackage(stringList_t *list, char *path)
{
	int				i, nr, nw;
	char **			str_ptr;
	FILE *			f1;
	FILE *			f2;
	char *			name;
	char *			name_path;
	char			buffer[BUFSIZ];
	struct stat		statbuf;

	if (list == NULL || path == NULL)
		return 1;

	i = list->num;
	str_ptr = list->list;
	
	while (0 < i--)
	{
		if ((f1 = fopen(*str_ptr, "r")) == NULL)
		{
			return 1;
		}
		// get the short name
		name = pathToName(*str_ptr);
		if (asprintf(&name_path, "%s/%s", path, name) <= 0)
		{
			fclose(f1);
			free(name);
			return 1;
		}
		free(name);
		if ((f2 = fopen(name_path, "w")) == NULL)
		{
			fclose(f1);
			free(name_path);
			return 1;
		}
		// read/write everything from f1/to f2
		while ((nr = fread(buffer, sizeof(char), BUFSIZ, f1)) > 0)
		{
			if ((nw = fwrite(buffer, sizeof(char), nr, f2)) != nr)
			{
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
			fprintf(stderr, "Could not stat %s\n", *str_ptr);
			free(name_path);
			return 1;
		}
		if (chmod(name_path, statbuf.st_mode) != 0)
		{
			fprintf(stderr, "Could not chmod %s\n", name_path);
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
packageManifestAndShip(uint64_t apid, MANIFEST_ID mid)
{
	appEntry_t *			app_ptr;			// pointer to the appEntry_t object associated with the provided aprun pid
	manifest_t *			m_ptr = NULL;		// pointer to the manifest_t object associated with the MANIFEST_ID argument
	char *					cfg_dir = NULL;		// configuration directory from env var
	char *					stage_dir = NULL;	// staging directory name
	char *					stage_path = NULL;	// staging path
	char *					stage_name = NULL;	// staging directory name
	char *					bin_path = NULL;
	char *					lib_path = NULL;
	char *					tmp_path = NULL;
	char *					tar_name = NULL;
	const char *			errmsg;				// errmsg that is possibly returned by call to alps_launch_tool_helper
	struct archive *		a = NULL;
	struct archive *		disk = NULL;
	struct archive_entry *	entry;
	ssize_t					len;
	int						r, fd;
	char 					buff[16384];
	char *					t = NULL;
	char *					orig_path;
	
	// sanity check
	if (apid <= 0 || mid <= 0)
		return 1;
	
	// try to find an entry in the my_apps array for the apid
	if ((app_ptr = findApp(apid)) == NULL)
	{
		// apid not found in the global my_apps array
		// so lets create a new appEntry_t object for it
		if ((app_ptr = newApp(apid)) == NULL)
		{
			// we failed to create a new appEntry_t entry - catastrophic failure
			goto packageManifestAndShip_error;
		}
	}
	
	// Find the manifest entry in the global manifest list for the mid
	if ((m_ptr = findManifest(mid)) == NULL)
	{
		// We failed to find the manifest for the mid
		fprintf(stderr, "MANIFEST_ID %d does not exist.\n", mid);
		goto packageManifestAndShip_error;
	}
	
	// get configuration dir
	if ((cfg_dir = getenv(CFG_DIR_VAR)) == NULL)
	{
		fprintf(stderr, "Cannot find %s. Please set environment variables!\n", CFG_DIR_VAR);
		goto packageManifestAndShip_error;
	}
	
	// check to see if the caller set a staging directory name, otherwise create a unique one for them
	if ((stage_dir = getenv(DAEMON_STAGE_DIR)) == NULL)
	{
		// take the default action
		if (asprintf(&stage_path, "%s/%s", cfg_dir, DEFAULT_STAGE_DIR) <= 0)
		{
			goto packageManifestAndShip_error;
		}
		
		// create the temporary directory for the manifest package
		if (mkdtemp(stage_path) == NULL)
		{
			goto packageManifestAndShip_error;
		}
	} else
	{
		// use the user defined directory
		if (asprintf(&stage_path, "%s/%s", cfg_dir, stage_dir) <= 0)
		{
			goto packageManifestAndShip_error;
		}
		
		if (mkdir(stage_path, S_IRWXU))
		{
			goto packageManifestAndShip_error;
		}
	}
	
	// get the stage name since we want to rebase things in the tarball
	stage_name = pathToName(stage_path);
	
	// now create the required subdirectories
	if (asprintf(&bin_path, "%s/bin", stage_path) <= 0)
	{
		goto packageManifestAndShip_error;
	}
	if (asprintf(&lib_path, "%s/lib", stage_path) <= 0)
	{
		goto packageManifestAndShip_error;
	}
	if (asprintf(&tmp_path, "%s/tmp", stage_path) <= 0)
	{
		goto packageManifestAndShip_error;
	}
	if (mkdir(bin_path, S_IRWXU))
	{
		goto packageManifestAndShip_error;
	}
	if (mkdir(lib_path,  S_IRWXU))
	{
		goto packageManifestAndShip_error;
	}
	if (mkdir(tmp_path, S_IRWXU))
	{
		goto packageManifestAndShip_error;
	}
	
	// copy all of the binaries
	if (copyFilesToPackage(m_ptr->exec_loc, bin_path))
	{
		goto packageManifestAndShip_error;
	}
	
	// copy all of the libraries
	if (copyFilesToPackage(m_ptr->lib_loc, lib_path))
	{
		goto packageManifestAndShip_error;
	}
	
	// copy all of the files
	if (copyFilesToPackage(m_ptr->file_loc, stage_path))
	{
		goto packageManifestAndShip_error;
	}
	
	// create the tarball name
	if (asprintf(&tar_name, "%s.tar", stage_path) <= 0)
	{
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
		fprintf(stderr, "%s\n", archive_error_string(disk));
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
			fprintf(stderr, "%s\n", archive_error_string(disk));
			archive_entry_free(entry);
			goto packageManifestAndShip_error;
		}
		archive_read_disk_descend(disk);
		
		// rebase the pathname in the header, we need to first grab the existing
		// pathanme to pass to open.
		orig_path = strdup(archive_entry_pathname(entry));
		// point at the start of the rebased path
		if ((t = strstr(orig_path, stage_name)) == NULL)
		{
			fprintf(stderr, "Could not find base name for tar!\n");
			free(orig_path);
			archive_entry_free(entry);
			goto packageManifestAndShip_error;
		}
		// Set the pathanme in the entry
		archive_entry_set_pathname(entry, t);
		
		r = archive_write_header(a, entry);
		if (r < ARCHIVE_OK)
		{
			fprintf(stderr, ": %s\n", archive_error_string(a));
			free(orig_path);
			archive_entry_free(entry);
			goto packageManifestAndShip_error;
		}
		if (r == ARCHIVE_FATAL)
		{
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
				archive_write_data(a, buff, len);
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
	
	archive_write_close(a);
	archive_write_free(a);
	
	// now ship the tarball to the compute node
	if ((errmsg = alps_launch_tool_helper(app_ptr->apid, app_ptr->alpsInfo.pe0Node, 1, 0, 1, &tar_name)) != NULL)
	{
		// we failed to ship the file to the compute nodes for some reason - catastrophic failure
		fprintf(stderr, "%s\n", errmsg);
		goto packageManifestAndShip_error;
	}
	
	// set the tarball name for args later
	m_ptr->tarball_name = pathToName(tar_name);
	
	// clean things up
	if (removeFilesFromDir(bin_path))
	{
		fprintf(stderr, "Failed to remove files from %s, please remove manually.\n", bin_path);
	}
	remove(bin_path);
	free(bin_path);
	if (removeFilesFromDir(lib_path))
	{
		fprintf(stderr, "Failed to remove files from %s, please remove manually.\n", lib_path);
	}
	remove(lib_path);
	free(lib_path);
	if (removeFilesFromDir(stage_path))
	{
		fprintf(stderr, "Failed to remove files from %s, please remove manually.\n", stage_path);
	}
	remove(tmp_path);
	free(tmp_path);
	remove(stage_path);
	free(stage_path);
	free(stage_name);
	remove(tar_name);
	free(tar_name);
	
	return 0;
	
	// Error handling code starts below
	
packageManifestAndShip_error:
	// Attempt to cleanup the archive stuff just in case it has been created already
	archive_read_close(disk);
	archive_read_free(disk);
	archive_write_close(a);
	archive_write_free(a);

	// Error occurred - try to remove any files already copied
	if (bin_path != NULL)
	{
		if (removeFilesFromDir(bin_path))
		{
			fprintf(stderr, "Failed to remove files from %s, please remove manually.\n", bin_path);
		}
		remove(bin_path);
		free(bin_path);
	}
	if (lib_path != NULL)
	{
		if (removeFilesFromDir(lib_path))
		{
			fprintf(stderr, "Failed to remove files from %s, please remove manually.\n", lib_path);
		}
		remove(lib_path);
		free(lib_path);
	}
	if (stage_path != NULL)
	{
		if (removeFilesFromDir(stage_path))
		{
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
	m_ptr->tarball_name = NULL;
	return 1;
}

int
execToolDaemon(uint64_t apid, MANIFEST_ID mid, char *fstr, char **args, char **env, int dbg)
{
	appEntry_t *	app_ptr;	// pointer to the appEntry_t object associated with the provided aprun pid
	manifest_t *	m_ptr;		// pointer to the manifest_t object associated with the MANIFEST_ID argument
	const char *	errmsg;		// errmsg that is possibly returned by call to alps_launch_tool_helper
	char *			fullname;	// full path name of the executable to launch as a tool daemon
	char *			realname;	// realname (lacking path info) of the executable
	char *			args_flat;	// flattened args array to pass to the toolhelper call
	char *			cpy;		// temporary cpy var used in creating args_flat
	char *			launcher;	// full path name of the daemon launcher application
	char **			tmp;		// temporary pointer used to iterate through lists of strings
	size_t			len, env_base_len;	// len vars used in creating the args_flat string

	// sanity check
	if (apid <= 0 || fstr == (char *)NULL)
		return 1;
	
	// try to find an entry in the my_apps array for the apid
	if ((app_ptr = findApp(apid)) == (appEntry_t *)NULL)
	{
		// apid not found in the global my_apps array
		// so lets create a new appEntry_t object for it
		if ((app_ptr = newApp(apid)) == (appEntry_t *)NULL)
		{
			// we failed to create a new appEntry_t entry - catastrophic failure
			return 1;
		}
	}
	
	// try to find the manifest_t for the mid
	if ((m_ptr = findManifest(mid)) == NULL)
	{
		if (mid == 0)
		{
			// mid not found in the global my_manifs array
			// so lets create a new manifest_t object for it
			if ((m_ptr = newManifest()) == NULL)
			{
				// we failed to create a new manifest_t - catastrophic failure
				return 1;
			}
		} else
		{
			// We failed to find the manifest for the mid
			fprintf(stderr, "MANIFEST_ID %d does not exist.\n", mid);
			return 1;
		}
	}
	
	// add the fstr to the manifest
	if (addManifestBinary(m_ptr->mid, fstr))
	{
		// Failed to add the binary to the manifest - catastrophic failure
		return 1;
	}
		
	// ship the manifest tarball to the compute nodes
	if (packageManifestAndShip(apid, m_ptr->mid))
	{
		// Failed to ship the manifest - catastrophic failure
		return 1;
	}
	
	// now we need to create the flattened argv string for the actual call to the wrapper
	// this is passed through the toolhelper
	// The options passed MUST correspond to the options defined in the daemon_launcher program.
		
	// find the binary name for the args
	
	// convert to fullpath name
	if ((fullname = pathFind(fstr, NULL)) == NULL)
	{
		fprintf(stderr, "Could not locate the specified file in PATH.\n");
		return 1;
	}
	
	// next just grab the real name (without path information) of the binary
	if ((realname = pathToName(fullname)) == NULL)
	{
		fprintf(stderr, "Could not convert the fullname to realname.\n");
		return 1;
	}
	
	// done with fullname
	free(fullname);
	
	// Find the location of the daemon launcher program
	if ((launcher = pathFind(ALPS_LAUNCHER, NULL)) == (char *)NULL)
	{
		fprintf(stderr, "Could not locate the launcher application in PATH.\n");
		return 1;
	}
	
	// determine the length of the argv[0] and -b (binary) argument
	len = strlen(launcher) + strlen(" -b ") + strlen(realname);
	
	// determine the length of the -m (manifest) argument
	len += strlen(" -m ");
	len += strlen(m_ptr->tarball_name);
	
	// iterate through the env array and determine its total length
	env_base_len = strlen(" -e "); 
	tmp = env;
	// ensure the are actual entries before dereferencing tmp
	if (tmp != (char **)NULL)
	{
		while (*tmp != (char *)NULL)
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
	if (tmp != (char **)NULL)
	{
		while (*tmp != (char *)NULL)
		{
			len += strlen(" ") + strlen(*tmp++) + strlen(" ");
		}
	}
		
	// add one for the null terminator
	++len;
	
	// malloc space for this string buffer
	if ((args_flat = malloc(len)) == (void *)NULL)
	{
		// malloc failed
		return 1;
	}
		
	// start creating the flattened args string
	snprintf(args_flat, len, "%s -b %s -m %s", launcher, realname, m_ptr->tarball_name);
	
	// cleanup mem
	free(realname);
		
	// add each of the env arguments
	tmp = env;
	// ensure the are actual entries before dereferencing tmp
	if (tmp != (char **)NULL)
	{
		while (*tmp != (char *)NULL)
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
	if (tmp != (char **)NULL)
	{
		while (*tmp != (char *)NULL)
		{
			cpy = strdup(args_flat);
			snprintf(args_flat, len, "%s %s ", cpy, *tmp++);
			free(cpy);
		}
	}
		
	// Done. We now have a flattened args string
	// We can launch the tool daemon onto the compute nodes now.
	if ((errmsg = alps_launch_tool_helper(app_ptr->apid, app_ptr->alpsInfo.pe0Node, 1, 1, 1, &args_flat)) != NULL)
	{
		// we failed to launch the launcher on the compute nodes for some reason - catastrophic failure
		fprintf(stderr, "%s\n", errmsg);
		free(args_flat);
		reapManifest(m_ptr->mid);
		return 1;
	}
		
	// cleanup our memory
	free(args_flat);
	reapManifest(m_ptr->mid);
	
	return 0;
}

