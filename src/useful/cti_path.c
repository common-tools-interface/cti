/******************************************************************************\
 * cti_path.c - Functions relating to searching and setting path variables.
 *
 * Copyright 2011-2017 Cray Inc.  All Rights Reserved.
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
 ******************************************************************************/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <dirent.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <limits.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>

#include <sys/types.h>
#include <sys/stat.h>

#include "cti_path.h"

#define EXTRA_LIBRARY_PATH  "/lib64:/usr/lib64:/lib:/usr/lib"

/*
 * Try to locate 'file' using PATH.
 *
 * Return pointer to path/file that can be opened, or NULL on failure.
 *
 * It is the responsiblity of the caller to free the returned buffer when done.
 */
char *
_cti_pathFind(const char *file, const char *envPath) 
{
	struct stat stat_buf;
	char    buf[PATH_MAX];
	char    *path;
	char    *tmp;
	char    *p_entry;
	char    *savePtr = NULL;
	char    *retval;

	if (file == NULL) 
	{
		return NULL;
	}

	// Check for possible relative or absolute path
	if (file[0] == '.' || file[0] == '/') 
	{
		if (stat(file, &stat_buf) == 0) 
		{
			// stat resolves symbolic links
			if (!S_ISREG(stat_buf.st_mode))
			{
				// can't execute a directory, or special files
				return NULL;
			} else
			{
				return strdup(file);
			}
		} else 
		{
			// can't access file
			return NULL;
		}
	}

	if (envPath == NULL) 
	{
		// default to using PATH
		envPath = "PATH";
	}

	if ((tmp = getenv(envPath)) == NULL)
	{
		// nothing in path to search
		return NULL;
	}
	path = strdup(tmp);

	/*
	* Start searching the colon-delimited PATH, prepending each
	* directory and checking to see if stat succeeds
	*/
	// grab the first p_entry in the path
	p_entry = strtok_r(path, ":", &savePtr);
	while (p_entry != NULL) 
	{
		// create the full path string
		snprintf(buf, PATH_MAX+1, "%s/%s", p_entry, file);
		// check to see if we can stat it
		if (stat(buf, &stat_buf) == 0) 
		{
			// we can stat it so make sure its a regular file.
			if ((stat_buf.st_mode & S_IFMT) == S_IFREG)
			{
				// This file matches
				retval = strdup(buf);
				free(path);
				return retval;
			}
		}
		// grab the next p_entry in the path
		p_entry = strtok_r(NULL, ":", &savePtr);
	}

	free(path);
	
	// not found
	return NULL;
}

/*
 * Try to locate 'library' in standard locations.
 *
 * Return pointer to path/library that can be opened, or NULL on failure.
 *
 * It is the responsiblity of the caller to free the returned buffer when done.
 *
 * This function makes use of a dangerous popen command. Do not pass user defined
 * strings to this function.
 */
char *
_cti_libFind(const char *file)
{
	struct stat		stat_buf;
	char			buf[PATH_MAX];
	char *			path;
	char *			tmp;
	char *			p_entry;
	char *			extraPath;
	char *			savePtr = NULL;
	char *			cmd;
	char *			res = NULL;
	size_t			res_len;
	int				len;
	FILE *			fp;
	char *			base;
	char *			retval;
	
	/* Check for possible relative or absolute path */
	if (file[0] == '.' || file[0] == '/') 
	{
		if (stat(file, &stat_buf) == 0) 
		{
			if (!S_ISREG(stat_buf.st_mode))
			{
				/* can't execute a directory, or special files */
				return NULL;
			} else
			{
				return strdup(file);
			}
		} else 
		{
			/* can't access file */
			return NULL;
		}
	}

	/*
	* Search LD_LIBRARY_PATH first
	*/	
	if ((tmp = getenv("LD_LIBRARY_PATH")) != NULL)
	{
		path = strdup(tmp);
	
		/*
		* Start searching the colon-delimited PATH, prepending each
		* directory and checking to see if stat succeeds
		*/
		// grab the first p_entry in the path
		p_entry = strtok_r(path, ":", &savePtr);
		while (p_entry != NULL) 
		{
			// create the full path string
			snprintf(buf, PATH_MAX+1, "%s/%s", p_entry, file);
			// check to see if we can stat it
			if (stat(buf, &stat_buf) == 0) 
			{
				// we can stat it so make sure its a regular file.
				if ((stat_buf.st_mode & S_IFMT) == S_IFREG)
				{
					retval = strdup(buf);
					free(path);
					return retval;
				}
			}
			// grab the next p_entry in the path
			p_entry = strtok_r(NULL, ":", &savePtr);
		}	
		
		free(path);
	}

	/*
	* Search the ldcache for the file
	*/

	// create the command string -- This is incredibly dangerous and should not
	// be used elsewhere outside of this library.
	if (asprintf(&cmd, "/sbin/ldconfig -p | grep \"%s\" | cut -d \" \" -f4", file) <= 0)
	{
		// failure occured
		return NULL;
	}

	if ((fp = popen(cmd, "r")) != NULL)
	{
		// we have output
		while ((len = getline(&res, &res_len, fp)) != -1)
		{
			// turn the newline into a null terminator
			if (res[len-1] == '\n')
			{
				res[len-1] = '\0';
			}
			
			// check to see if the basename of the result matches our file
		    if ((base = _cti_pathToName(res)) != NULL)
			{
				if ((strlen(base) == strlen(file)) && (strcmp(base, file) == 0))
				{
					// filenames match - ensure we can stat the file
					if (stat(res, &stat_buf) == 0)
					{
						// we can stat it so make sure its a regular file
						if ((stat_buf.st_mode & S_IFMT) == S_IFREG)
						{
							// found the library
							retval = strdup(res);
							free(res);
							free(base);
							pclose(fp);
							return retval;
						}
					}
				}
				// This is not the library you are looking for.
				free(base);
			}
			free(res);
			res = NULL;
		}
		pclose(fp);
	}
	free(cmd);

	/*
	* Search the additional path for the file
	*/
	extraPath = strdup(EXTRA_LIBRARY_PATH);
	p_entry = strtok_r(extraPath, ":", &savePtr);
	while (p_entry != NULL) 
	{
		sprintf(buf, "%s/%s", p_entry, file);
		if (stat(buf, &stat_buf) == 0) 
		{
			// we can stat it so make sure its a regular file or sym link.
			if (S_ISREG(stat_buf.st_mode)) 
			{
				retval = strdup(buf);
				free(extraPath);
				return retval;
			}
		} 
		p_entry = strtok_r(NULL, ":", &savePtr);
	}
	
	free(extraPath);
	
	// not found
	return NULL;
}

/*
 * Set the path directory to be PATH and LD_LIBRARY_PATH.
 *
 * Also, chdir to the path dir so that files created in "./"
 * have a writable home. This addresses the fact that /tmp 
 * can not be guaranteed to be writable.
 */
int
_cti_adjustPaths(const char *path, const char* libPath)
{
	struct stat statbuf;
	char *binpath = NULL;
	char *libpath = NULL;
	
	// sanity check
	if (path == NULL)
		return 1;
	
	// stat the directory to get its current perms
	if (stat(path, &statbuf) == -1)
		return 1;
	
	// Relax permissions to ensure we can write to this directory
	// use the existing perms for group and global settings
	if (chmod(path, statbuf.st_mode | S_IRWXU) != 0)
		return 1;
		
	// change the working directory to path
	if (chdir(path) != 0)
		return 1;
	
	if (asprintf(&binpath, "%s/bin", path) <= 0)
		return 1;
	
	// set path to the PATH variable
	if (setenv("PATH", binpath, 1) != 0)
	{
		free(binpath);
		return 1;
	}
	
	free(binpath);
	
	if (libPath == NULL) {
		if (asprintf(&libpath, "%s/lib", path) <= 0)
			return 1;
	}
	
	// set path to the LD_LIBRARY_PATH variable
	if (setenv("LD_LIBRARY_PATH", libpath, 1) != 0)
	{
		free(libpath);
		return 1;
	}
	
	free(libpath);
	
	return 0;
}

/* "a/b/c" => "c" */
char *
_cti_pathToName(const char *path)
{
	char *  end;
	
	// locate the last instance of '/' in the path
	end = strrchr(path, '/');
	
	// sanity check
	if (end == NULL)
		return NULL;
	
	// increment end to point one char past the final '/'
	// and strdup from that point to the null term
	return strdup(++end);
}

/* "a/b/c" => "a/b" */
char *
_cti_pathToDir(const char *path)
{
	char *  end;
	char * result = strdup(path);
	
	// locate the last instance of '/' in the path
	end = strrchr(path, '/');
	
	// sanity check
	if (end == NULL)
		return NULL;

	//End the string just before the final slash
	result[end-path] = '\0';
	
	return result;
}

// This will act as a rm -rf ...
int
_cti_removeDirectory(const char *path)
{
	DIR *			dir;
	struct dirent *	d;
	char *			name_path;
	struct stat		statbuf;

	// sanity check
	if (path == NULL)
	{
		//_cti_set_error("_cti_removeDirectory: invalid args.");
		return 1;
	}
	
	// open the directory
	if ((dir = opendir(path)) == NULL)
	{
		//_cti_set_error("_cti_removeDirectory: Could not opendir %s.", path);
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
			//_cti_set_error("_cti_removeDirectory: asprintf failed.");
			closedir(dir);
			return 1;
		}
		
		// stat the file
		if (stat(name_path, &statbuf) == -1)
		{
			//_cti_set_error("_cti_removeDirectory: Could not stat %s.", name_path);
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
				//_cti_set_error("_cti_removeDirectory: Could not remove %s.", name_path);
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
		//_cti_set_error("_cti_removeDirectory: Could not remove %s.", path);
		return 1;
	}
	
	return 0;
}
