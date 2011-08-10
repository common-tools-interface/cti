/*********************************************************************************\
 * path.c - Functions relating to searching and setting path variables.
 *
 * © 2011 Cray Inc.  All Rights Reserved.
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
#include        <config.h>
#endif /* HAVE_CONFIG_H */

#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <limits.h>
#include <string.h>
#include <strings.h>

#include <sys/types.h>
#include <sys/stat.h>

#include "path.h"

/*
 * Try to locate 'file' using PATH.
 *
 * Return pointer to path/file that can be opened, or NULL on failure.
 *
 * It is the responsiblity of the caller to free the returned buffer when done.
 */
char *
pathFind(const char *file, const char *envPath) 
{
        struct stat stat_buf;
        char    buf[PATH_MAX];
        char    *path;
        char    *tmp;
        char    *p_entry;
        char    *savePtr = NULL;
        char    *retval;

        if (file == (char *)NULL) 
        {
                return (char *)NULL;
        }

        /* Check for possible relative or absolute path */
        if (file[0] == '.' || file[0] == '/') 
        {
                if (stat(file, &stat_buf) == 0) 
                {
                        // stat resolves symbolic links
                        if (!S_ISREG(stat_buf.st_mode))
                        {
                                /* can't execute a directory, or special files */
                                return (char *)NULL;
                        } else
                        {
                                return strdup(file);
                        }
                } else 
                {
                        /* can't access file */
                        return (char *)NULL;
                }
        }

        if (envPath == NULL) 
        {
                // default to using PATH
                envPath = "PATH";
        }

        if ((tmp = getenv(envPath)) == (char *)NULL)
        {
                /* nothing in path to search */
                fprintf(stderr, "Could not getenv %s.\n", envPath);
                return (char *)NULL;
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
        return (char *)NULL;
}

/*
 * Try to locate 'library' using LD_LIBRARY_PATH.
 *
 * Return pointer to path/library that can be opened, or NULL on failure.
 *
 * It is the responsiblity of the caller to free the returned buffer when done.
 */
char *
libFind(const char *file, const char *envPath)
{
        struct stat stat_buf;
        char    buf[PATH_MAX];
        char    *path;
        char    *extraPath = NULL;
        char    *tmp;
        char    *p_entry;
        char    *savePtr = NULL;
        char    *retval;
        
        /* Check for possible relative or absolute path */
        if (file[0] == '.' || file[0] == '/') 
        {
                if (stat(file, &stat_buf) == 0) 
                {
                        if (!S_ISREG(stat_buf.st_mode))
                        {
                                /* can't execute a directory, or special files */
                                return (char *)NULL;
                        } else
                        {
                                return strdup(file);
                        }
                } else 
                {
                        /* can't access file */
                        return (char *)NULL;
                }
        }
        
        if (envPath == NULL) 
        {
                // default to using LD_LIBRARY_PATH
                envPath = "LD_LIBRARY_PATH";
                extraPath = strdup(EXTRA_LIBRARY_PATH);
        }
        
        if ((tmp = getenv(envPath)) == (char *)NULL)
        {
                /* nothing in path to search */
                fprintf(stderr, "Could not getenv %s.\n", envPath);
                return (char *)NULL;
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
                                retval = strdup(buf);
                                free(path);
                                return retval;
                        }
                }
                // grab the next p_entry in the path
                p_entry = strtok_r(NULL, ":", &savePtr);
        }
        
        free(path);
        
        // see if there is an extraPath defined, if not the file was not found in PATH
        if (extraPath == NULL) 
        {
	        return (char *)NULL;
        }
        
        /*
        * Search the additional path for the file
        */
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
        return (char *)NULL;
}

/*
 * Set the path directory to be PATH and LD_LIBRARY_PATH.
 *
 * Also, chdir to the path dir so that files created in "./"
 * have a writable home. This addresses the fact that /tmp 
 * can not be guaranteed to be writable.
 */
int
adjustPaths(char *path)
{
        struct stat statbuf;
        char *newpath;
        int len;
        
        // sanity check
        if (path == (char *)NULL)
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
        
        len = strlen(path) + 1;
        if ((newpath = malloc(len*sizeof(char))) == (char *)NULL)
        {
                return 1;
        }
        
        snprintf(newpath, len, "%s", path);
        
        // set path to the PATH variable
        if (setenv("PATH", newpath, 1) != 0)
        {
                free(newpath);
                return 1;
        }
        
        // set path to the LD_LIBRARY_PATH variable
        if (setenv("LD_LIBRARY_PATH", newpath, 1) != 0)
        {
                free(newpath);
                return 1;
        }
        
        free(newpath);
        
        return 0;
}

char *
pathToName(char *path)
{
        char *  end;
        
        // locate the last instance of '/' in the path
        end = strrchr(path, '/');
        
        // increment end to point one char past the final '/'
        // and strdup from that point to the null term
        return strdup(++end);
}
