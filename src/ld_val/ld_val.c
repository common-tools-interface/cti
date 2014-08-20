/*********************************************************************************\
 * ld_val.c - A library that takes advantage of the rtld audit interface library
 *	    to recieve the locations of the shared libraries that are required
 *	    by the runtime dynamic linker for a specified program. This is the
 *	    static portion of the code to link into a program wishing to use
 *	    this interface.
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

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/wait.h>
#include <sys/ioctl.h>

#include "ld_val_defs.h"
#include "ld_val.h"

/* Internal prototypes */
static int		_cti_save_str(char *);
static char **	_cti_make_rtn_array(void);
static char *	_cti_ld_get_lib(void);
static char *	_cti_ld_verify(char *);
static int		_cti_ld_load(char *, char *, char *);

/* list of valid linkers */
// We should check the 64 bit linker first since most
// apps are built using x86-64 nowadays.
// Check the lsb linker last. (do we even use lsb code?)
// lsb = linux standard base
static const char *_cti_linkers[] = {
	"/lib64/ld-linux-x86-64.so.2",
	"/lib/ld-linux.so.2",
	"/lib64/ld-lsb-x86-64.so.2",
	"/lib/ld-lsb.so.2",
	"/lib64/ld-lsb-x86-64.so.3",
	"/lib/ld-lsb.so.3",
	NULL
};

/* global variables */
static int		_cti_num_ptrs;
static int		_cti_num_alloc;
static char **	_cti_tmp_array = NULL;
static int		_cti_fds[2];

static int
_cti_save_str(char *str)
{
	if (str == NULL)
		return -1;
	
	if (_cti_num_ptrs >= _cti_num_alloc)
	{
		_cti_num_alloc += BLOCK_SIZE;
		if ((_cti_tmp_array = realloc(_cti_tmp_array, _cti_num_alloc * sizeof(char *))) == (void *)0)
		{
			perror("realloc");
			return -1;
		}
	}
	
	_cti_tmp_array[_cti_num_ptrs++] = str;
	
	return _cti_num_ptrs;
}

static char **
_cti_make_rtn_array()
{
	char **rtn;
	int i;
	
	if (_cti_tmp_array == NULL)
		return NULL;
	
	// create the return array
	if ((rtn = calloc(_cti_num_ptrs+1, sizeof(char *))) == (void *)0)
	{
		perror("calloc");
		return NULL;
	}
	
	// assign each element of the return array
	for (i=0; i<_cti_num_ptrs; i++)
	{
		rtn[i] = _cti_tmp_array[i];
	}
	
	// set the final element to null
	rtn[i] = NULL;
	
	// free the temp array
	free(_cti_tmp_array);
	
	// reset global variables
	_cti_tmp_array = NULL;
	_cti_num_alloc = 0;
	_cti_num_ptrs = 0;
	
	return rtn;
}

static char *
_cti_ld_verify(char *executable)
{
	const char *linker = NULL;
	int pid, status, fc, i=1;
	
	if (executable == NULL)
		return NULL;

	// Verify that the linker is able to perform relocations on our binary
	// This should be able to handle both 32 and 64 bit executables
	// We will simply choose the first one that works for us.
	for (linker = _cti_linkers[0]; linker != NULL; linker = _cti_linkers[i++])
	{
		pid = fork();
		
		// error case
		if (pid < 0)
		{
			perror("fork");
			return NULL;
		}
		
		// child case
		if (pid == 0)
		{
			// redirect our stdout/stderr to /dev/null
			fc = open("/dev/null", O_WRONLY);
			dup2(fc, STDERR_FILENO);
			dup2(fc, STDOUT_FILENO);
			
			// exec the linker to verify it is able to load our program
			execl(linker, linker, "--verify", executable, NULL);
			perror("execl");
		}
		
		// parent case
		// wait for child to return
		waitpid(pid, &status, 0);
		if (WIFEXITED(status))
		{
			// if we recieved an exit status of 0, the verify was successful
			if (WEXITSTATUS(status) == 0)
				break;
		}
	}
	
	return (char *)linker;
}

static int
_cti_ld_load(char *linker, char *executable, char *lib)
{
	int pid, fc;
	
	if (linker == NULL || executable == NULL)
		return -1;
	
	// create the pipe
	if (pipe(_cti_fds) < 0)
	{
		perror("pipe");
		return -1;
	}
	
	// invoke the rtld interface.
	pid = fork();
	
	// error case
	if (pid < 0)
	{
		perror("fork");
		return pid;
	}
	
	// child case
	if (pid == 0)
	{
		// close the read end of the pipe
		close(_cti_fds[0]);
		
		// dup2 stderr - Note that we want to use stderr since stdout will be
		// cluttered with other junk
		if (dup2(_cti_fds[1], STDERR_FILENO) < 0)
		{
			fprintf(stderr, "CTI error: Unable to redirect LD_AUDIT stderr.\n");
			exit(1);
		}
		
		// redirect stdout to /dev/null - we don't really care if this fails.
		fc = open("/dev/null", O_WRONLY);
		dup2(fc, STDOUT_FILENO);
	
		// set the LD_AUDIT environment variable for this process
		if (setenv(LD_AUDIT, lib, 1) < 0)
		{
			perror("setenv");
			fprintf(stderr, "CTI error: Failed to set LD_AUDIT environment variable.\n");
			exit(1);
		}
		
		// exec the linker with --list to get a list of our dso's
		execl(linker, linker, "--list", executable, NULL);
		perror("execl");
		exit(1);
	}
	
	// parent case
	// close write end of the pipe
	close(_cti_fds[1]);
	
	// return the child pid
	return pid;
}

static char *
_cti_ld_get_lib()
{
	char 	seq;
	char 	path_len_buf[BUFSIZ] = {0};
	int		path_len = 0;
	int		pos;
	int		ret;
	char *	libstr;
	
	// Read the first character - we expect this to be LIBAUDIT_SEP_CHAR
	if (read(_cti_fds[0], &seq, 1) <= 0)
	{
		// read failed, return NULL
		return NULL;
	}

	// Ensure this is the LIBAUDIT_SEP_CHAR
	if (seq != LIBAUDIT_SEP_CHAR)
	{
		fprintf(stderr, "CTI error: Invalid sequence start detected in _cti_ld_get_lib.\n");
		return NULL;
	}
	
	// Now read in the length of the path string
	pos = 0;
	do {
		ret = read(_cti_fds[0], &path_len_buf[pos], 1);
		if (ret < 0)
		{
			// error occured
			perror("read");
			return NULL;
		} else if (ret == 0)
		{
			// Something went very wrong here, we got an eof in the middle of 
			// a valid sequence
			fprintf(stderr, "CTI error: EOF detected in valid sequence in _cti_ld_get_lib.\n");
			return NULL;
		}
		
		// check to see if we read the LIBAUDIT_SEP_CHAR
		if (path_len_buf[pos] == LIBAUDIT_SEP_CHAR)
		{
			// we finished reading the length, ensure that pos is not zero
			// otherwise we literally just read %% and have no length.
			if (pos == 0)
			{
				fprintf(stderr, "CTI error: Missing path len value in _cti_ld_get_lib.\n");
				return NULL;
			}
			
			// set the char to null term - we have a valid length and are finished
			path_len_buf[pos] = '\0';
			path_len = atoi(path_len_buf);
		} else
		{
			// increment pos
			++pos;
		}
	} while (path_len == 0);
	
	// Now we know how long the string will be, so lets allocate a buffer and
	// read the rest
	if ((libstr = malloc(path_len+1)) == (void *)0)
	{
		perror("malloc");
		return NULL;
	}
	memset(libstr, 0, path_len+1);
	
	pos = 0;
	do {
		// read the library path string
		ret = read(_cti_fds[0], libstr+pos, path_len-pos);
		if (ret < 0)
		{
			// error occured
			perror("read");
			free(libstr);
			return NULL;
		} else if (ret == 0)
		{
			// Something went very wrong here, we got an eof in the middle of 
			// a valid sequence
			fprintf(stderr, "CTI error: EOF detected in valid sequence in _cti_ld_get_lib.\n");
			free(libstr);
			return NULL;
		}
		
		// update pos
		pos += ret;
	} while (pos != path_len);
	
	// return the string
	return libstr;
}

char **
_cti_ld_val(char *executable)
{
	char *			linker;
	int				pid;
	int				rec = 0;
	char *			tmp_audit;
	char *			audit_location;
	char *			libstr;
	char **			rtn;
	int				n;
	
	// sanity
	if (executable == NULL)
		return NULL;
	
	// reset global vars
	_cti_num_ptrs = 0;
	_cti_num_alloc = BLOCK_SIZE;
	
	// Ensure _cti_tmp_array is null
	if (_cti_tmp_array != NULL)
	{
		free(_cti_tmp_array);
	}
	// create space for the _cti_tmp_array
	if ((_cti_tmp_array = calloc(BLOCK_SIZE, sizeof(char *))) == (void *)0)
	{
		perror("calloc");
		return NULL;
	}
	
	// ensure that we found a valid linker that was verified
	if ((linker = _cti_ld_verify(executable)) == NULL)
	{
		fprintf(stderr, "CTI error: Failed to locate a working dynamic linker for the specified binary.\n");
		return NULL;
	}
	
	// get the location of the audit library
	if ((tmp_audit = getenv(LIBAUDIT_ENV_VAR)) != NULL)
	{
		audit_location = strdup(tmp_audit);
	} else
	{
		fprintf(stderr, "CTI error: Could not read CRAY_LD_VAL_LIBRARY to get location of libaudit.so.\n");
		return NULL;
	}
	
	// Now we load our program using the list command to get its dso's
	if ((pid = _cti_ld_load(linker, executable, audit_location)) <= 0)
	{
		fprintf(stderr, "CTI error: Failed to load the program using the linker.\n");
		return NULL;
	}
	
	// Try to read libraries while the child process is still alive
	do {
	
cti_data_avail:

		libstr = _cti_ld_get_lib();
		
		// if we recieved a null, we might be done.
		if (libstr == NULL)
			continue;
		
		// we want to ignore the first library we recieve
		// as it will always be the ld.so we are using to
		// get the shared libraries.
		if (++rec == 1)
		{
			if (libstr != NULL)
				free(libstr);
			continue;
		}
			
		if ((_cti_save_str(libstr)) <= 0)
		{
			fprintf(stderr, "CTI error: Unable to save temp string.\n");
			return NULL;
		}
	} while (!waitpid(pid, NULL, WNOHANG));
	
	// Final check to see if there is still data available for reading, if so
	// jump back up to the loop. Note that with pipes, even though the child
	// process is gone, we might still have data available to read. We want to
	// continue reading until we get an EOF on the pipe.
	if (ioctl(_cti_fds[0], FIONREAD, &n) < 0)
	{
		perror("ioctl");
	}
	if (n >= 1)
	{
		goto cti_data_avail;
	}
	
	rtn = _cti_make_rtn_array();
	
	// cleanup memory
	free(audit_location);
	// close read end of the pipe
	close(_cti_fds[0]);

	return rtn;
}

