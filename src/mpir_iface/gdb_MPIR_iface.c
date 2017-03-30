/******************************************************************************\
 * gdb_MPIR_iface.c - An interface to start launcher processes and hold at a
 *                    startup barrier
 *
 * Copyright 2014-2017 Cray Inc.  All Rights Reserved.
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

#include <sys/types.h>
#include <sys/stat.h>

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "cti_defs.h"
#include "cti_error.h"
#include "cti_useful.h"

#include "gdb_MPIR_iface.h"

// This will correspond to the number of allowed gdb instances to be started at
// any one period of time. It is prone to fragmentation when generating a valid
// key. But big deal. We don't expect callers to be launching hundreds of jobs
// at the same time...
#define CTI_GDB_TABLE_SIZE	32

/* Types used here */
typedef struct
{
	int			init;		// initialized?
	int			final;		// finalized?
	int 		pipeR[2];	// caller read pipe
	int 		pipeW[2];	// caller write pipe
	FILE *		pipe_r;		// my read stream
	FILE *		pipe_w;		// my write stream
} gdbCtlInst_t;


/* static global variables */
static unsigned int		_cti_gdb_inuse;
static unsigned int		_cti_gdb_nextid;
static gdbCtlInst_t *	_cti_gdb_hashtable[CTI_GDB_TABLE_SIZE];


/* static prototypes */
static void	_cti_gdb_consumeInst(cti_gdb_id_t);


static void
_cti_gdb_consumeInst(cti_gdb_id_t gdb_id)
{
	gdbCtlInst_t *	this;

	// ensure the caller passed a valid id.
	if (gdb_id < 0 || gdb_id >= CTI_GDB_TABLE_SIZE)
		return;
	
	// point at the instance
	this = _cti_gdb_hashtable[gdb_id];
	
	// validate the instance
	if (this == NULL)
	{
		return;
	}
	
	// conditional cleanup
	if (this->init)
	{
		// close my fds
		fclose(this->pipe_r);
		fclose(this->pipe_w);
	}
	
	// cleanup the instance
	_cti_gdb_hashtable[gdb_id] = NULL;
	--_cti_gdb_inuse;
	free(this);
}

// Used to force cleanup all remaining instances of gdb
void
_cti_gdb_cleanupAll(void)
{
	cti_gdb_msg_t *	msg;
	int				i;
	
	// ensure that we have outstanding gdb sessions
	if (_cti_gdb_inuse == 0)
		return;
	
	// create the exit request message
	if ((msg = _cti_gdb_createMsg(MSG_EXIT)) == NULL)
		return;
	
	for (i=0; i<CTI_GDB_TABLE_SIZE; ++i)
	{
		gdbCtlInst_t *this = _cti_gdb_hashtable[i];
		
		if (this == NULL)
			continue;
		
		// send the exit message if not finalized
		if (!this->final)
		{
			_cti_gdb_sendMsg(this->pipe_w, msg);
		}
		
		// we don't care about a response. The other side isn't going to
		// send one.
		
		// cleanup this instance
		_cti_gdb_consumeInst(i);
	}
	
	// cleanup the msg
	_cti_gdb_consumeMsg(msg);
}

// Used to force cleanup of a single instance
void
_cti_gdb_cleanup(cti_gdb_id_t gdb_id)
{
	gdbCtlInst_t *	this;
	
	// ensure the caller passed a valid id.
	if (gdb_id < 0 || gdb_id >= CTI_GDB_TABLE_SIZE)
	{
		return;
	}
	
	// point at the instance
	this = _cti_gdb_hashtable[gdb_id];
	
	// validate the instance
	if (this == NULL)
	{
		return;
	}
	
	// check if the instance has been initialized and hasn't yet been finalized
	if (this->init && !this->final)
	{
		// we need to send an exit message
		cti_gdb_msg_t *	msg;

		// create the exit request message
		if ((msg = _cti_gdb_createMsg(MSG_EXIT)) == NULL)
		{
			_cti_gdb_consumeInst(gdb_id);
			
			return;
		}
		
		// send the exit message
		_cti_gdb_sendMsg(this->pipe_w, msg);
		
		// we don't care about a response. The other side isn't going to
		// send one.
		
		// cleanup the msg
		_cti_gdb_consumeMsg(msg);
	}
	
	// cleanup this instance
	_cti_gdb_consumeInst(gdb_id);
}

// This function will create a gdbCtlInst_t and set it up so that the caller
// can make use of this interface. It returns a cti_gdb_id_t which corresponds
// to an entry in our hash table.
// returns -1 on error
cti_gdb_id_t
_cti_gdb_newInstance(void)
{
	cti_gdb_id_t	rtn;
	gdbCtlInst_t *	this;

	// ensure that there is open space in the hash table
	if (_cti_gdb_inuse >= CTI_GDB_TABLE_SIZE)
	{
		_cti_set_error("Too many pending applications waiting at barrier!\n");
		return -1;
	}
	
	// find the next open space in the hash table
	while (_cti_gdb_hashtable[_cti_gdb_nextid] != NULL)
	{
		_cti_gdb_nextid = (_cti_gdb_nextid + 1) % CTI_GDB_TABLE_SIZE;
	}
	
	// set the return id, post increment to reserve the current id since we are
	// going to use the bucket. Increment the inuse count.
	rtn = _cti_gdb_nextid++;
	++_cti_gdb_inuse;
	
	// create a new gdbCtlInst_t object
	if ((_cti_gdb_hashtable[rtn] = malloc(sizeof(gdbCtlInst_t))) == NULL)
	{
		// Malloc failed
		_cti_set_error("malloc failed.");
		_cti_gdb_hashtable[rtn] = NULL;
		--_cti_gdb_nextid;
		--_cti_gdb_inuse;
		return -1;
	}
	memset(_cti_gdb_hashtable[rtn], 0, sizeof(gdbCtlInst_t)); // clear it to NULL
	
	// point at the new instance, we can operate on this pointer from here on out
	// unless there is an error encountered.
	this = _cti_gdb_hashtable[rtn];
	
	// create the control pipes for this gdb interface instace
	if (pipe(this->pipeR) < 0)
	{
		_cti_set_error("_cti_gdb_newInstance: Pipe creation failure.");
		free(this);
		_cti_gdb_hashtable[rtn] = NULL;
		--_cti_gdb_nextid;
		--_cti_gdb_inuse;
		return -1;
	}
	if (pipe(this->pipeW) < 0)
	{
		_cti_set_error("_cti_gdb_newInstance: Pipe creation failure.");
		free(this);
		_cti_gdb_hashtable[rtn] = NULL;
		--_cti_gdb_nextid;
		--_cti_gdb_inuse;
		return -1;
	}
	
	// Done. The rest of the setup will occur post-fork.
	return rtn;
}

// This function is called by the child after the fork.
// It will setup the call to exec the gdb MPIR starter utility.
void
_cti_gdb_execStarter(	cti_gdb_id_t gdb_id, const char *starter, const char *gdb, 
						const char *launcher, const char * const launcher_args[], const char *input_file	)
{
	gdbCtlInst_t *	this;
	cti_args_t *	my_args;
	int				fd;
	
	// ensure the caller passed valid arguments
	if (starter == NULL || gdb == NULL || launcher == NULL)
	{
		// post fork - now way to report errors right now!
		fprintf(stderr, "CTI error: _cti_gdb_execStarter bad args.\n");
		return;
	}

	// ensure the caller passed a valid id.
	if (gdb_id < 0 || gdb_id >= CTI_GDB_TABLE_SIZE)
	{
		// post fork - no way to report errors right now!
		fprintf(stderr, "CTI error: _cti_gdb_execStarter bad args.\n");
		return;
	}
	
	// point at the instance
	this = _cti_gdb_hashtable[gdb_id];
	
	// validate the instance
	if (this == NULL)
	{
		// post fork - no way to report errors right now!
		fprintf(stderr, "CTI error: _cti_gdb_execStarter bad args.\n");
		return;
	}
	
	// ensure the instance hasn't already been init'ed
	if (this->init)
	{
		// post fork - no way to report errors right now!
		fprintf(stderr, "CTI error: _cti_gdb_execStarter already init!\n");
		return;
	}
	
	// close unused ends of the pipe
	close(this->pipeR[0]);
	close(this->pipeW[1]);
	
	//
	// create the required starter args
	//
	// For the starter process, it requires a -r <fd> and -w <fd> argument for 
	// the pipe fd numbers, a required -g <gdb> argument, a required -s <starter> 
	// argument, an optional -i <input> argument for redirect of stdin, 
	// followed by -- <launcher args>
	//
	
	// create a new args obj
	if ((my_args = _cti_newArgs()) == NULL)
	{
		// post fork - no way to report errors right now!
		fprintf(stderr, "CTI error: _cti_newArgs failed.");
		return;
	}
	
	if (_cti_addArg(my_args, "%s", starter))
	{
		fprintf(stderr, "CTI error: _cti_addArg failed.");
		_cti_freeArgs(my_args);
		return;
	}
	
	// use the read/write ends for the child in the instance
	// This is opposite of what we set in the parent.
	if (_cti_addArg(my_args, "-r"))
	{
		fprintf(stderr, "CTI error: _cti_addArg failed.");
		_cti_freeArgs(my_args);
		return;
	}
	if (_cti_addArg(my_args, "%d", this->pipeW[0]))
	{
		fprintf(stderr, "CTI error: _cti_addArg failed.");
		_cti_freeArgs(my_args);
		return;
	}
	
	if (_cti_addArg(my_args, "-w"))
	{
		fprintf(stderr, "CTI error: _cti_addArg failed.");
		_cti_freeArgs(my_args);
		return;
	}
	if (_cti_addArg(my_args, "%d", this->pipeR[1]))
	{
		fprintf(stderr, "CTI error: _cti_addArg failed.");
		_cti_freeArgs(my_args);
		return;
	}
	
	// create the gdb arg
	if (_cti_addArg(my_args, "-g"))
	{
		fprintf(stderr, "CTI error: _cti_addArg failed.");
		_cti_freeArgs(my_args);
		return;
	}
	if (_cti_addArg(my_args, "%s", gdb))
	{
		fprintf(stderr, "CTI error: _cti_addArg failed.");
		_cti_freeArgs(my_args);
		return;
	}
	
	// create the starter arg
	if (_cti_addArg(my_args, "-s"))
	{
		fprintf(stderr, "CTI error: _cti_addArg failed.");
		_cti_freeArgs(my_args);
		return;
	}
	if (_cti_addArg(my_args, "%s", launcher))
	{
		fprintf(stderr, "CTI error: _cti_addArg failed.");
		_cti_freeArgs(my_args);
		return;
	}
	
	// create input file arg if there is one
	if (input_file != NULL)
	{
		if (_cti_addArg(my_args, "-i"))
		{
			fprintf(stderr, "CTI error: _cti_addArg failed.");
			_cti_freeArgs(my_args);
			return;
		}
		if (_cti_addArg(my_args, "%s", input_file))
		{
			fprintf(stderr, "CTI error: _cti_addArg failed.");
			_cti_freeArgs(my_args);
			return;
		}
	}
	
	// Add launcher args if there are any
	if (launcher_args != NULL)
	{
		int i;
	
		if (_cti_addArg(my_args, "--"))
		{
			fprintf(stderr, "CTI error: _cti_addArg failed.");
			_cti_freeArgs(my_args);
			return;
		}
		for (i=0; launcher_args[i] != NULL; ++i)
		{
			if (_cti_addArg(my_args, "%s", launcher_args[i]))
			{
				fprintf(stderr, "CTI error: _cti_addArg failed.");
				_cti_freeArgs(my_args);
				return;
			}
		}
	}
	
	// we want to redirect stdin/stdout/stderr to /dev/null since it is not required
	if ((fd = open("/dev/null", O_RDONLY)) < 0)
	{
		perror("open");
		return;
	}
	
	// dup2 stdin
	if (dup2(fd, STDIN_FILENO) < 0)
	{
		perror("dup2");
		return;
	}
	
	// dup2 stdout
	if (dup2(fd, STDOUT_FILENO) < 0)
	{
		perror("dup2");
		return;
	}
	
	// dup2 stderr
	if (dup2(fd, STDERR_FILENO) < 0)
	{
		perror("dup2");
		return;
	}
	
	close(fd);
	
	// exec the starter utility
	execv(starter, my_args->argv);
	
	// if we get here, an error happened
	return;
}

// This function is called by the child after the fork.
// It will setup the call to exec the gdb MPIR attach utility.
void
_cti_gdb_execAttach(	cti_gdb_id_t gdb_id, const char *attach, const char *gdb, 
						pid_t starter_pid	)
{
	gdbCtlInst_t *	this;
	cti_args_t *	my_args;
	int				fd;
	
	// ensure the caller passed valid arguments
	if (attach == NULL || gdb == NULL || starter_pid <= 0)
	{
		// post fork - now way to report errors right now!
		fprintf(stderr, "CTI error: _cti_gdb_execAttach bad args.\n");
		return;
	}

	// ensure the caller passed a valid id.
	if (gdb_id < 0 || gdb_id >= CTI_GDB_TABLE_SIZE)
	{
		// post fork - no way to report errors right now!
		fprintf(stderr, "CTI error: _cti_gdb_execAttach bad args.\n");
		return;
	}
	
	// point at the instance
	this = _cti_gdb_hashtable[gdb_id];
	
	// validate the instance
	if (this == NULL)
	{
		// post fork - no way to report errors right now!
		fprintf(stderr, "CTI error: _cti_gdb_execAttach bad args.\n");
		return;
	}
	
	// ensure the instance hasn't already been init'ed
	if (this->init)
	{
		// post fork - no way to report errors right now!
		fprintf(stderr, "CTI error: _cti_gdb_execAttach already init!\n");
		return;
	}
	
	// close unused ends of the pipe
	close(this->pipeR[0]);
	close(this->pipeW[1]);
	
	//
	// create the required attach args
	//
	// For the attach process, it requires a -r <fd> and -w <fd> argument for 
	// the pipe fd numbers, a required -g <gdb> argument, a required -p <pid> 
	// argument.
	//
	
	// create a new args obj
	if ((my_args = _cti_newArgs()) == NULL)
	{
		// post fork - no way to report errors right now!
		fprintf(stderr, "CTI error: _cti_newArgs failed.");
		return;
	}
	
	if (_cti_addArg(my_args, "%s", attach))
	{
		fprintf(stderr, "CTI error: _cti_addArg failed.");
		_cti_freeArgs(my_args);
		return;
	}
	
	// use the read/write ends for the child in the instance
	// This is opposite of what we set in the parent.
	if (_cti_addArg(my_args, "-r"))
	{
		fprintf(stderr, "CTI error: _cti_addArg failed.");
		_cti_freeArgs(my_args);
		return;
	}
	if (_cti_addArg(my_args, "%d", this->pipeW[0]))
	{
		fprintf(stderr, "CTI error: _cti_addArg failed.");
		_cti_freeArgs(my_args);
		return;
	}
	
	if (_cti_addArg(my_args, "-w"))
	{
		fprintf(stderr, "CTI error: _cti_addArg failed.");
		_cti_freeArgs(my_args);
		return;
	}
	if (_cti_addArg(my_args, "%d", this->pipeR[1]))
	{
		fprintf(stderr, "CTI error: _cti_addArg failed.");
		_cti_freeArgs(my_args);
		return;
	}
	
	// create the gdb arg
	if (_cti_addArg(my_args, "-g"))
	{
		fprintf(stderr, "CTI error: _cti_addArg failed.");
		_cti_freeArgs(my_args);
		return;
	}
	if (_cti_addArg(my_args, "%s", gdb))
	{
		fprintf(stderr, "CTI error: _cti_addArg failed.");
		_cti_freeArgs(my_args);
		return;
	}
	
	// create the pid arg
	if (_cti_addArg(my_args, "-p"))
	{
		fprintf(stderr, "CTI error: _cti_addArg failed.");
		_cti_freeArgs(my_args);
		return;
	}
	if (_cti_addArg(my_args, "%d", (int)starter_pid))
	{
		fprintf(stderr, "CTI error: _cti_addArg failed.");
		_cti_freeArgs(my_args);
		return;
	}
	
	// we want to redirect stdin/stdout/stderr to /dev/null since it is not required
	if ((fd = open("/dev/null", O_RDONLY)) < 0)
	{
		perror("open");
		return;
	}
	
	// dup2 stdin
	if (dup2(fd, STDIN_FILENO) < 0)
	{
		perror("dup2");
		return;
	}
	
	// dup2 stdout
	if (dup2(fd, STDOUT_FILENO) < 0)
	{
		perror("dup2");
		return;
	}
	
	// dup2 stderr
	if (dup2(fd, STDERR_FILENO) < 0)
	{
		perror("dup2");
		return;
	}
	
	close(fd);
	
	// exec the attach utility
	execv(attach, my_args->argv);
	
	// if we get here, an error happened
	return;
}

// This function is called by the parent after they fork off the child process.
// It will finalize the setup and wait for the child to reach the startup
// barrier.
int
_cti_gdb_postFork(cti_gdb_id_t gdb_id)
{
	gdbCtlInst_t *	this;
	cti_gdb_msg_t *	msg;

	// ensure the caller passed a valid id.
	if (gdb_id < 0 || gdb_id >= CTI_GDB_TABLE_SIZE)
	{
		_cti_set_error("_cti_gdb_postFork: Invalid cti_gdb_id_t.\n");
		return 1;
	}
	
	// point at the instance
	this = _cti_gdb_hashtable[gdb_id];
	
	// validate the instance
	if (this == NULL)
	{
		_cti_set_error("_cti_gdb_postFork: Invalid cti_gdb_id_t.\n");
		return 1;
	}
	
	// ensure the instance hasn't already been init'ed
	if (this->init)
	{
		_cti_set_error("_cti_gdb_postFork: Instance already initialized!\n");
		return 1;
	}
	
	// initialize the instance
	
	// close unused ends of the pipe
	close(this->pipeR[1]);
	close(this->pipeW[0]);
	
	// set the read/write ends in the instance
	this->pipe_r = fdopen(this->pipeR[0], "r");
	if (this->pipe_r == NULL)
	{
		_cti_set_error("_cti_gdb_postFork: fdopen failed!\n");
		return 1;
	}
	this->pipe_w = fdopen(this->pipeW[1], "w");
	if (this->pipe_w == NULL)
	{
		_cti_set_error("_cti_gdb_postFork: fdopen failed!\n");
		return 1;
	}
	
	// set init to true
	this->init = 1;
	
	// We now expect to receive a ready message
	if ((msg = _cti_gdb_recvMsg(this->pipe_r)) == NULL)
	{
		// this is a fatal error
		if (_cti_gdb_err_string != NULL)
		{
			_cti_set_error("%s", _cti_gdb_err_string);
		} else
		{
			_cti_set_error("_cti_gdb_postFork: Unknown gdb_MPIR error!\n");
		}
		return 1;
	}
	
	// process the message
	switch (msg->msg_type)
	{
		// This is what we expect
		case MSG_READY:
			// Cleanup msg
			_cti_gdb_consumeMsg(msg);
			break;
			
		// anything else is an error
		default:
			// We don't have error recovery right now, so if an unexpected message
			// comes in we are screwed.
			_cti_set_error("_cti_gdb_postFork: Unexpected message received!\n");
			_cti_gdb_consumeMsg(msg);
			return 1;
	}
	
	// When we get here, the application launcher is sitting at the startup barrier
	// ready to continue.
	
	return 0;
}

// This function will return a string value for a symbol. Note that the caller
// needs to pass in the symbol name to query. We suspect this to change between
// different WLM implementations. The symbol must refer to a string value!
char *
_cti_gdb_getSymbolVal(cti_gdb_id_t gdb_id, const char *sym)
{
	gdbCtlInst_t *	this;
	cti_gdb_msg_t *	msg;
	char *			rtn;
	
	// check the args
	if (sym == NULL)
	{
		_cti_set_error("_cti_gdb_getSymbolVal: Invalid args.\n");
		return NULL;
	}

	// ensure the caller passed a valid id.
	if (gdb_id < 0 || gdb_id >= CTI_GDB_TABLE_SIZE)
	{
		_cti_set_error("_cti_gdb_getSymbolVal: Invalid cti_gdb_id_t.\n");
		return NULL;
	}
	
	// point at the instance
	this = _cti_gdb_hashtable[gdb_id];
	
	// validate the instance
	if (this == NULL)
	{
		_cti_set_error("_cti_gdb_getSymbolVal: Invalid cti_gdb_id_t.\n");
		return NULL;
	}
	
	// ensure the instance hasn't already been finalized
	if (this->final)
	{
		_cti_set_error("_cti_gdb_getSymbolVal: cti_gdb_id_t is finalized.\n");
		return NULL;
	}
	
	// create the request message
	if ((msg = _cti_gdb_createMsg(MSG_ID, strdup(sym))) == NULL)
	{
		// set the error message if there is one
		if (_cti_gdb_err_string != NULL)
		{
			_cti_set_error("%s", _cti_gdb_err_string);
		} else
		{
			_cti_set_error("_cti_gdb_getSymbolVal: Unknown gdb_MPIR error!\n");
		}
		return NULL;
	}
	
	// send the message
	if (_cti_gdb_sendMsg(this->pipe_w, msg))
	{
		// this is a fatal error
		if (_cti_gdb_err_string != NULL)
		{
			_cti_set_error("%s", _cti_gdb_err_string);
		} else
		{
			_cti_set_error("_cti_gdb_getSymbolVal: Unknown gdb_MPIR error!\n");
		}
		_cti_gdb_consumeMsg(msg);
		return NULL;
	}
	// cleanup the message we just sent
	_cti_gdb_consumeMsg(msg);
	
	// recv response
	if ((msg = _cti_gdb_recvMsg(this->pipe_r)) == NULL)
	{
		// this is a fatal error
		if (_cti_gdb_err_string != NULL)
		{
			_cti_set_error("%s", _cti_gdb_err_string);
		} else
		{
			_cti_set_error("_cti_gdb_getSymbolVal: Unknown gdb_MPIR error!\n");
		}
		return NULL;
	}
	
	// process the response
	switch (msg->msg_type)
	{
		// This is what we expect
		case MSG_ID:
			// save the return type
			rtn = strdup(msg->msg_payload.msg_string);
			break;
			
		// anything else is an error
		default:
			// We don't have error recovery right now, so if an unexpected message
			// comes in we are screwed.
			_cti_set_error("_cti_gdb_getSymbolVal: Unexpected message received!\n");
			_cti_gdb_consumeMsg(msg);
			return NULL;
	}
	
	// Cleanup msg
	_cti_gdb_consumeMsg(msg);
	
	return rtn;
}

// used to free the return type from _cti_gdb_getAppPids()
void
_cti_gdb_freeMpirPid(cti_mpir_pid_t *this)
{
	// sanity
	if (this == NULL)
		return;
		
	if (this->pid != NULL)
		free(this->pid);
	
	free(this);
}

// This function will return a cti_pid_t ptr that contains the rank->pid pairing.
// This is harvested from the MPIR_proctable.
cti_mpir_pid_t *
_cti_gdb_getAppPids(cti_gdb_id_t gdb_id)
{
	gdbCtlInst_t *		this;
	cti_gdb_msg_t *		msg;
	cti_mpir_pid_t *	rtn;
	int					i;

	// ensure the caller passed a valid id.
	if (gdb_id < 0 || gdb_id >= CTI_GDB_TABLE_SIZE)
	{
		_cti_set_error("_cti_gdb_getAppPids: Invalid cti_gdb_id_t.\n");
		return NULL;
	}
	
	// point at the instance
	this = _cti_gdb_hashtable[gdb_id];
	
	// validate the instance
	if (this == NULL)
	{
		_cti_set_error("_cti_gdb_getAppPids: Invalid cti_gdb_id_t.\n");
		return NULL;
	}
	
	// ensure the instance hasn't already been finalized
	if (this->final)
	{
		_cti_set_error("_cti_gdb_getAppPids: cti_gdb_id_t is finalized.\n");
		return NULL;
	}
	
	// create the request message
	if ((msg = _cti_gdb_createMsg(MSG_PID, (cti_pid_t *)NULL)) == NULL)
	{
		// set the error message if there is one
		if (_cti_gdb_err_string != NULL)
		{
			_cti_set_error("%s", _cti_gdb_err_string);
		} else
		{
			_cti_set_error("_cti_gdb_getAppPids: Unknown gdb_MPIR error!\n");
		}
		return NULL;
	}
	
	// send the message
	if (_cti_gdb_sendMsg(this->pipe_w, msg))
	{
		// this is a fatal error
		if (_cti_gdb_err_string != NULL)
		{
			_cti_set_error("%s", _cti_gdb_err_string);
		} else
		{
			_cti_set_error("_cti_gdb_getAppPids: Unknown gdb_MPIR error!\n");
		}
		_cti_gdb_consumeMsg(msg);
		return NULL;
	}
	// cleanup the message we just sent
	_cti_gdb_consumeMsg(msg);
	
	// recv response
	if ((msg = _cti_gdb_recvMsg(this->pipe_r)) == NULL)
	{
		// this is a fatal error
		if (_cti_gdb_err_string != NULL)
		{
			_cti_set_error("%s", _cti_gdb_err_string);
		} else
		{
			_cti_set_error("_cti_gdb_getSymbolVal: Unknown gdb_MPIR error!\n");
		}
		return NULL;
	}
	
	// process the response
	switch (msg->msg_type)
	{
		// This is what we expect
		case MSG_PID:
			if (msg->msg_payload.msg_pid == NULL)
			{
				_cti_set_error("_cti_gdb_getAppPids: Missing pid information in response.\n");
				_cti_gdb_consumeMsg(msg);
				return NULL;
			}
			
			// create the return type
			if ((rtn = malloc(sizeof(cti_mpir_pid_t))) == NULL)
			{
				_cti_set_error("malloc failed.\n");
				_cti_gdb_consumeMsg(msg);
				return NULL;
			}
			if ((rtn->pid = calloc(msg->msg_payload.msg_pid->num_pids, sizeof(pid_t))) == NULL)
			{
				_cti_set_error("calloc failed.\n");
				_cti_gdb_consumeMsg(msg);
				free(rtn);
				return NULL;
			}
			
			rtn->num_pids = msg->msg_payload.msg_pid->num_pids;
			
			// copy the return pid_t array
			for (i=0; i < rtn->num_pids; ++i)
			{
				rtn->pid[i] = msg->msg_payload.msg_pid->pid[i];
			}
			
			break;
			
		// anything else is an error
		default:
			// We don't have error recovery right now, so if an unexpected message
			// comes in we are screwed.
			_cti_set_error("_cti_gdb_getAppPids: Unexpected message received!\n");
			_cti_gdb_consumeMsg(msg);
			return NULL;
	}
	
	// Cleanup msg
	_cti_gdb_consumeMsg(msg);
	
	return rtn;
}

cti_mpir_proctable_t *
_cti_gdb_getProctable(cti_gdb_id_t gdb_id){
	gdbCtlInst_t *		this;
	cti_gdb_msg_t *		msg;
	cti_mpir_proctable_t *	rtn;

	// ensure the caller passed a valid id.
	if (gdb_id < 0 || gdb_id >= CTI_GDB_TABLE_SIZE)
	{
		_cti_set_error("_cti_gdb_getAppPids: Invalid cti_gdb_id_t.\n");
		return NULL;
	}
	
	// point at the instance
	this = _cti_gdb_hashtable[gdb_id];
	
	// validate the instance
	if (this == NULL)
	{
		_cti_set_error("_cti_gdb_getAppPids: Invalid cti_gdb_id_t.\n");
		return NULL;
	}
	
	// ensure the instance hasn't already been finalized
	if (this->final)
	{
		_cti_set_error("_cti_gdb_getAppPids: cti_gdb_id_t is finalized.\n");
		return NULL;
	}
	
	// create the request message
	if ((msg = _cti_gdb_createMsg(MSG_PROCTABLE, (cti_pid_t *)NULL)) == NULL)
	{
		// set the error message if there is one
		if (_cti_gdb_err_string != NULL)
		{
			_cti_set_error("%s", _cti_gdb_err_string);
		} else
		{
			_cti_set_error("_cti_gdb_getAppPids: Unknown gdb_MPIR error!\n");
		}
		return NULL;
	}
	
	// send the message
	if (_cti_gdb_sendMsg(this->pipe_w, msg))
	{
		// this is a fatal error
		if (_cti_gdb_err_string != NULL)
		{
			_cti_set_error("%s", _cti_gdb_err_string);
		} else
		{
			_cti_set_error("_cti_gdb_getAppPids: Unknown gdb_MPIR error!\n");
		}
		_cti_gdb_consumeMsg(msg);
		return NULL;
	}
	// cleanup the message we just sent
	_cti_gdb_consumeMsg(msg);
	
	// recv response
	if ((msg = _cti_gdb_recvMsg(this->pipe_r)) == NULL)
	{
		// this is a fatal error
		if (_cti_gdb_err_string != NULL)
		{
			_cti_set_error("%s", _cti_gdb_err_string);
		} else
		{
			_cti_set_error("_cti_gdb_getSymbolVal: Unknown gdb_MPIR error!\n");
		}
		return NULL;
	}
	
	// process the response
	switch (msg->msg_type)
	{
		// This is what we expect
		case MSG_PROCTABLE:
			if (msg->msg_payload.msg_proctable == NULL)
			{
				_cti_set_error("_cti_gdb_getProctable: Missing proctable information in response.\n");
				_cti_gdb_consumeMsg(msg);
				return NULL;
			}
			
			rtn = _cti_gdb_newProctable(msg->msg_payload.msg_proctable->num_pids);
			int current_pid;
			for(current_pid = 0; current_pid<msg->msg_payload.msg_proctable->num_pids; current_pid++){
				rtn->hostnames[current_pid] = strdup(msg->msg_payload.msg_proctable->hostnames[current_pid]);
				rtn->pids[current_pid] = msg->msg_payload.msg_proctable->pids[current_pid];
			}
			
			break;
			
		// anything else is an error
		default:
			// We don't have error recovery right now, so if an unexpected message
			// comes in we are screwed.
			_cti_set_error("_cti_gdb_getProctable: Unexpected message received!\n");
			_cti_gdb_consumeMsg(msg);
			return NULL;
	}
	
	// Cleanup msg
	_cti_gdb_consumeMsg(msg);
	
	return rtn;
}

pid_t 				
_cti_gdb_getLauncherPid(cti_gdb_id_t gdb_id)
{
	gdbCtlInst_t *		this;
	cti_gdb_msg_t *		msg;
	pid_t				rtn;

	// ensure the caller passed a valid id.
	if (gdb_id < 0 || gdb_id >= CTI_GDB_TABLE_SIZE)
	{
		_cti_set_error("_cti_gdb_getAppPids: Invalid cti_gdb_id_t.\n");
		return -1;
	}
	
	// point at the instance
	this = _cti_gdb_hashtable[gdb_id];
	
	// validate the instance
	if (this == NULL)
	{
		_cti_set_error("_cti_gdb_getAppPids: Invalid cti_gdb_id_t.\n");
		return -1;
	}
	
	// ensure the instance hasn't already been finalized
	if (this->final)
	{
		_cti_set_error("_cti_gdb_getAppPids: cti_gdb_id_t is finalized.\n");
		return -1;
	}
	
	// create the request message
	if ((msg = _cti_gdb_createMsg(MSG_LAUNCHER_PID, -1)) == NULL)
	{
		// set the error message if there is one
		if (_cti_gdb_err_string != NULL)
		{
			_cti_set_error("%s", _cti_gdb_err_string);
		} else
		{
			_cti_set_error("_cti_gdb_getAppPids: Unknown gdb_MPIR error!\n");
		}
		return -1;
	}
	
	// send the message
	if (_cti_gdb_sendMsg(this->pipe_w, msg))
	{
		// this is a fatal error
		if (_cti_gdb_err_string != NULL)
		{
			_cti_set_error("%s", _cti_gdb_err_string);
		} else
		{
			_cti_set_error("_cti_gdb_getAppPids: Unknown gdb_MPIR error!\n");
		}
		_cti_gdb_consumeMsg(msg);
		return -1;
	}
	// cleanup the message we just sent
	_cti_gdb_consumeMsg(msg);
	
	// recv response
	if ((msg = _cti_gdb_recvMsg(this->pipe_r)) == NULL)
	{
		// this is a fatal error
		if (_cti_gdb_err_string != NULL)
		{
			_cti_set_error("%s", _cti_gdb_err_string);
		} else
		{
			_cti_set_error("_cti_gdb_getSymbolVal: Unknown gdb_MPIR error!\n");
		}
		return -1;
	}
	
	// process the response
	switch (msg->msg_type)
	{
		// This is what we expect
		case MSG_LAUNCHER_PID:
			rtn = msg->msg_payload.launcher_pid;
			break;
			
		// anything else is an error
		default:
			// We don't have error recovery right now, so if an unexpected message
			// comes in we are screwed.
			_cti_set_error("_cti_gdb_getProctable: Unexpected message received!\n");
			_cti_gdb_consumeMsg(msg);
			return -1;
	}
	
	// Cleanup msg
	_cti_gdb_consumeMsg(msg);
	
	return rtn;
}

// This function is used to release the application from the control of gdb. It
// causes gdb to exit and clean things up. After calling this, no further use of
// the gdb interface is possible.
int
_cti_gdb_release(cti_gdb_id_t gdb_id)
{
	gdbCtlInst_t *	this;
	cti_gdb_msg_t *	msg;

	// ensure the caller passed a valid id.
	if (gdb_id < 0 || gdb_id >= CTI_GDB_TABLE_SIZE)
	{
		_cti_set_error("_cti_gdb_release: Invalid cti_gdb_id_t.\n");
		return 1;
	}
	
	// point at the instance
	this = _cti_gdb_hashtable[gdb_id];
	
	// validate the instance
	if (this == NULL)
	{
		_cti_set_error("_cti_gdb_release: Invalid cti_gdb_id_t.\n");
		return 1;
	}
	
	// ensure the instance hasn't already been finalized
	if (this->final)
	{
		_cti_set_error("_cti_gdb_release: cti_gdb_id_t is finalized.\n");
		return 1;
	}
	
	// create the request message
	if ((msg = _cti_gdb_createMsg(MSG_RELEASE)) == NULL)
	{
		// set the error message if there is one
		if (_cti_gdb_err_string != NULL)
		{
			_cti_set_error("%s", _cti_gdb_err_string);
		} else
		{
			_cti_set_error("_cti_gdb_release: Unknown gdb_MPIR error!\n");
		}
		return 1;
	}
	
	// send the message
	if (_cti_gdb_sendMsg(this->pipe_w, msg))
	{
		// this is a fatal error
		if (_cti_gdb_err_string != NULL)
		{
			_cti_set_error("%s", _cti_gdb_err_string);
		} else
		{
			_cti_set_error("_cti_gdb_release: Unknown gdb_MPIR error!\n");
		}
		_cti_gdb_consumeMsg(msg);
		return 1;
	}
	// cleanup the message we just sent
	_cti_gdb_consumeMsg(msg);
	
	// recv response
	if ((msg = _cti_gdb_recvMsg(this->pipe_r)) == NULL)
	{
		// this is a fatal error
		if (_cti_gdb_err_string != NULL)
		{
			_cti_set_error("%s", _cti_gdb_err_string);
		} else
		{
			_cti_set_error("_cti_gdb_release: Unknown gdb_MPIR error!\n");
		}
		return 1;
	}
	
	// process the response
	switch (msg->msg_type)
	{
		// This is what we expect
		case MSG_EXIT:
			// Do nothing
			break;
			
		// anything else is an error
		default:
			// We don't have error recovery right now, so if an unexpected message
			// comes in we are screwed.
			_cti_set_error("_cti_gdb_release: Unexpected message received!\n");
			_cti_gdb_consumeMsg(msg);
			return 1;
	}
	
	// cleanup the message
	_cti_gdb_consumeMsg(msg);
	
	// set finalized to true
	this->final = 1;
	
	return 0;
}

