/******************************************************************************\
 * gdb_MPIR_iface.c - An interface to start launcher processes and hold at a
 *                    startup barrier
 *
 * © 2014 Cray Inc.  All Rights Reserved.
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "cti_defs.h"
#include "cti_error.h"

#include "gdb_MPIR_iface.h"
#include "gdb_MPIR.h"

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
	int			pipe_r;		// my read end
	int			pipe_w;		// my write end
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
		close(this->pipe_r);
		close(this->pipe_w);
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
_cti_gdb_execStarter(cti_gdb_id_t gdb_id, const char *launcher, char * const launcher_args[], const char *input_file)
{
	gdbCtlInst_t *	this;
	char * const *	p;
	char *			pr_arg;
	char *			pw_arg;
	char *			l_arg;
	char *			i_arg = NULL;
	int				s_argc = 0;
	char **			s_argv;
	int				i;
	
	// ensure the caller passed valid arguments
	if (launcher == NULL)
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
	// count the number of starter args
	//
	
	// count the number of args in the launcher_args argument
	if (launcher_args != NULL)
	{
		// add one for the "--" argument
		++s_argc;
		p = launcher_args;
		while (*p++ != NULL)
		{
			++s_argc;
		}
	}
	
	if (input_file != NULL)
		++s_argc;
	
	//
	// create the required starter args
	//
	
	// use the read/write ends for the child in the instance
	// This is opposite of what we set in the parent.
	if (asprintf(&pr_arg, "-r %d", this->pipeW[0]) < 0)
	{
		// post fork - no way to report errors right now!
		perror("asprintf");
		return;
	}
	if (asprintf(&pw_arg, "-w %d", this->pipeR[1]) < 0)
	{
		// post fork - no way to report errors right now!
		perror("asprintf");
		return;
	}
	
	// create the starter arg
	if (asprintf(&l_arg, "-s %s", launcher) < 0)
	{
		// post fork - no way to report errors right now!
		perror("asprintf");
		return;
	}
	
	// create input file arg if there is one
	if (input_file != NULL)
	{
		if (asprintf(&i_arg, "-i %s", input_file) < 0)
		{
			// post fork - no way to report errors right now!
			perror("asprintf");
			return;
		}
	}
	
	//
	// create the argv array
	//
	
	// For the starter process, it requires a -r <fd> and -w <fd> argument for 
	// the pipe fd numbers, a required -s <starter> argument, an optional 
	// -i <input> argument for redirect of stdin, followed by -- <launcher args>
	
	// add required args to the s_argc
	s_argc += 5;
	
	// alloc the argc array
	if ((s_argv = calloc(s_argc, sizeof(char *))) == NULL)
	{
		// calloc failed
		// post fork - no way to report errors right now!
		perror("calloc");
		return;
	}
	
	s_argv[0] = GDB_MPIR_STARTER;
	s_argv[1] = pr_arg;
	s_argv[2] = pw_arg;
	s_argv[3] = l_arg;
	i = 4;
	if (i_arg != NULL)
	{
		s_argv[i++] = i_arg;
	}
	if (launcher_args != NULL)
	{
		s_argv[i++] = "--";
		p = launcher_args;
		while (*p != NULL)
		{
			s_argv[i++] = *p++;
		}
	}
	// set null terminator
	s_argv[i] = NULL;
	
	// exec the starter utility
	execvp(GDB_MPIR_STARTER, s_argv);
	
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
	this->pipe_r = this->pipeR[0];
	this->pipe_w = this->pipeW[1];
	
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

// This function is used to release the application from its startup barrier
// It also causes gdb to exit and clean things up. After calling this, no further
// use of the gdb interface is possible.
int
_cti_gdb_releaseBarrier(cti_gdb_id_t gdb_id)
{
	gdbCtlInst_t *	this;
	cti_gdb_msg_t *	msg;

	// ensure the caller passed a valid id.
	if (gdb_id < 0 || gdb_id >= CTI_GDB_TABLE_SIZE)
	{
		_cti_set_error("_cti_gdb_releaseBarrier: Invalid cti_gdb_id_t.\n");
		return 1;
	}
	
	// point at the instance
	this = _cti_gdb_hashtable[gdb_id];
	
	// validate the instance
	if (this == NULL)
	{
		_cti_set_error("_cti_gdb_releaseBarrier: Invalid cti_gdb_id_t.\n");
		return 1;
	}
	
	// ensure the instance hasn't already been finalized
	if (this->final)
	{
		_cti_set_error("_cti_gdb_releaseBarrier: cti_gdb_id_t is finalized.\n");
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
			_cti_set_error("_cti_gdb_releaseBarrier: Unknown gdb_MPIR error!\n");
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
			_cti_set_error("_cti_gdb_releaseBarrier: Unknown gdb_MPIR error!\n");
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
			_cti_set_error("_cti_gdb_releaseBarrier: Unknown gdb_MPIR error!\n");
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
			_cti_set_error("_cti_gdb_releaseBarrier: Unexpected message received!\n");
			_cti_gdb_consumeMsg(msg);
			return 1;
	}
	
	// cleanup the message
	_cti_gdb_consumeMsg(msg);
	
	// set finalized to true
	this->final = 1;
	
	return 0;
}

