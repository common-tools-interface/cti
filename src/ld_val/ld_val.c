/*********************************************************************************\
 * ld_val.c - A library that takes advantage of the rtld audit interface library
 *	    to recieve the locations of the shared libraries that are required
 *	    by the runtime dynamic linker for a specified program. This is the
 *	    static portion of the code to link into a program wishing to use
 *	    this interface.
 *
 * Copyright 2011-2014 Cray Inc.  All Rights Reserved.
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
static int			_cti_save_str(char *);
static char **		_cti_make_rtn_array(void);
static const char *	_cti_ld_verify(const char *);
static int			_cti_ld_load(const char *, const char *, const char *);

/* list of valid linkers */
// We should check the 64 bit linker first since most
// apps are built using x86-64 nowadays.
// Check the lsb linker last. (do we even use lsb code?)
// lsb = linux standard base
static const char *_cti_linkers[] = {
	"/lib64/ld-linux-x86-64.so.2",
	"/lib/ld-linux.so.2",
	"/lib64/ld-lsb-x86-64.so.2",
	"/lib64/ld-lsb-x86-64.so.3",
	"/lib64/ld-2.11.3.so",
	"/lib/ld-lsb.so.2",
	"/lib/ld-lsb.so.3",
	NULL
};

/* global variables */
static char		_cti_read_buf[READ_BUF_LEN];
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

static const char *
_cti_ld_verify(const char *executable)
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
			// exec shouldn't return
			exit(1);
		}
		
		// parent case
		// wait for child to return
		waitpid(pid, &status, 0);
		if (WIFEXITED(status))
		{
			// if we recieved an exit status of 0, the verify was successful
			if (WEXITSTATUS(status) == 0)
				return linker;
		}
	}
	
	// not found
	return NULL;
}

static int
_cti_ld_load(const char *linker, const char *executable, const char *lib)
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

char **
_cti_ld_val(const char *executable, const char *ld_audit_path)
{
	const char *	linker;
	int				pid, status;
	char **			rtn;
	int				num_read;
	int				pos;
	char *			libstr;
	int				found = 0;
	char *			start;
	char *			end;
	int				rem;
	
	// sanity
	if (executable == NULL || ld_audit_path == NULL)
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
		// If no valid linker was found, we assume that this was a static binary.
		return NULL;
	}
	
	// Now we load our program using the list command to get its dso's
	if ((pid = _cti_ld_load(linker, executable, ld_audit_path)) <= 0)
	{
		fprintf(stderr, "CTI error: Failed to load the program using the linker.\n");
		return NULL;
	}
	
	// reset pos
	pos = 0;
	
	// Try to read libraries while the pipe is open
	do {
		// read up to READ_BUF_LEN, offset based on pos since we might have
		// leftovers
		num_read = read(_cti_fds[0], &_cti_read_buf[pos], READ_BUF_LEN-pos);
		// check for error
		if (num_read < 0)
		{
			// error occured
			perror("read");
			// prevent zombie
			kill(pid, SIGKILL);
			waitpid(pid, &status, 0);
			return NULL;
		} else if (num_read == 0)
		{
			// if we get an EOF with pos set, we have a partial string without
			// the rest, this is an error
			if (pos != 0)
			{
				// Something went very wrong here, we got an eof in the middle of 
				// a valid sequence
				fprintf(stderr, "CTI error: EOF detected in valid sequence.\n");
				// prevent zombie
				kill(pid, SIGKILL);
				waitpid(pid, &status, 0);
				return NULL;
			}
			// we are done if we get an EOF
			continue;
		}
		
		// init start, end, and rem
		start = _cti_read_buf;
		end = start+1;
		rem = num_read;
		
		// walk the buffer to find the null terms and create libstr out of them.
		// we are done when end points at the last character read
		while (end < &_cti_read_buf[num_read-1])
		{
			// find the first null terminator in the string
			if ((end = memchr(start, '\0', rem)) == NULL)
			{
				// point end at the last entry and continue, we have a partial
				// string.
				end = &_cti_read_buf[num_read-1];
				continue;
			}
			
			// copy the current string
			libstr = strdup(start);
			
			// if found is false, we should check for the linker string, we don't
			// want to ship this. We will use the ld.so that is present on the
			// compute nodes.
			if (!found)
			{
				if (strncmp(linker, libstr, strlen(linker)))
				{
					// strings don't match, we want to save it.
					goto save_str;
				} else
				{
					// set found and free this string. Do not save it.
					++found;
					free(libstr);
				}
			} else
			{
save_str:
				if ((_cti_save_str(libstr)) <= 0)
				{
					fprintf(stderr, "CTI error: Unable to save temp string.\n");
					// prevent zombie
					kill(pid, SIGKILL);
					waitpid(pid, &status, 0);
					return NULL;
				}
			}
			
			// point end pass the null terminator and adjust the remainder, we
			// need to get rid of the entire string plus the null term.
			rem -= ++end - start;
			
			// point start at the end, and move on to another iteration
			start = end;
		}
			
		// if rem if non-zero, we have a partial string left over.
		if (rem != 0)
		{
			memmove(_cti_read_buf, start, rem);
			// memset the rest to zero, just to be safe
			if (rem < num_read)
			{
				memset(&_cti_read_buf[rem], 0, num_read-rem);
			}
			// update pos
			pos = rem;
		} else
		{
			// reset pos for next run since there was no remainder this time
			pos = 0;
		}
	} while (num_read != 0);
	
	// All done, make the return array
	rtn = _cti_make_rtn_array();
	
	// close read end of the pipe
	close(_cti_fds[0]);
	
	// prevent zombie
	waitpid(pid, &status, 0);

	return rtn;
}

