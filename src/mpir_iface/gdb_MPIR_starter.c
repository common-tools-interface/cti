/******************************************************************************\
 * gdb_MPIR_starter.c - An interface to start launcher processes and hold at a
 *                    startup barrier. This code is the child that communicates
 *                    to the parent via pipes. This process controls the starter
 *                    process via gdb.
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

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stddef.h>

#include <sys/select.h>

#include "MI.h"

#include "cti_defs.h"
#include "gdb_MPIR.h"

#define	 GDB_MIN_VERS 			7.2

/* static global variables */

// Read/Write pipes
static FILE *			_cti_gdb_pipe_r		= NULL;
static FILE *			_cti_gdb_pipe_w		= NULL;
// GDB specific stuff
static MISession *		_cti_gdb_sess;
static int				_cti_gdb_ready;
static MIEvent *		_cti_gdb_event;

const struct option long_opts[] = {
			{"read",		required_argument,	0, 'r'},
			{"write",		required_argument,	0, 'w'},
			{"gdb",			required_argument,	0, 'g'},
			{"starter",		required_argument,	0, 's'},
			{"input",		required_argument,	0, 'i'},
			{"help",		no_argument,		0, 'h'},
			{0, 0, 0, 0}
			};

typedef struct {
  char * host_name;           /* Something we can pass to inet_addr */
  char * executable_name;     /* The name of the image */
  int    pid;		      /* The pid of the process */
} MPIR_PROCDESC;

static void
usage(char *name)
{
	fprintf(stdout, "Usage: %s [OPTIONS]...\n", name);
	fprintf(stdout, "Start a parallel application using a launcher via the MPIR interface.\n\n");
	
	fprintf(stdout, "\t-r, --read      fd of read control pipe         (required)\n");
	fprintf(stdout, "\t-w, --write     fd of write control pipe        (required)\n");
	fprintf(stdout, "\t-g, --gdb       Name of gdb binary              (required)\n");
	fprintf(stdout, "\t-s, --starter   Name of starter binary          (required)\n");
	fprintf(stdout, "\t-i, --input     redirect stdin to provided file (optional)\n");
	fprintf(stdout, "\t-h, --help      Display this text and exit\n\n");
	
	fprintf(stdout, "Additional starter arguments can be provided by using the special \"--\" argument\n");
	fprintf(stdout, "followed by the starter arguments.\n");
}

/* Static prototypes */
static int	_cti_gdb_SendMICommand(MISession *, MICommand *);
static void	_cti_gdb_cleanupMI(void);

static void	event_callback(MIEvent *);

/* GDBMI callback functions */
static void
event_callback(MIEvent *e)
{
	_cti_gdb_ready = 1;
	_cti_gdb_event = e;
}

static void
_cti_gdb_cleanupMI(void)
{
	if (_cti_gdb_sess != NULL)
	{
		MICommand *cmd = MIGDBExit();
		_cti_gdb_SendMICommand(_cti_gdb_sess, cmd);
		MICommandFree(cmd);
		
		MISessionFree(_cti_gdb_sess);
	}
}

/*
** Send the MI command
*/
static int
_cti_gdb_SendMICommand(MISession *sess, MICommand *cmd)
{
	char *buf;

	MISessionSendCommand(sess, cmd);
	do {
		if (MISessionProgress(sess) == -1)
		{
			if ((buf = MICommandResultErrorMessage(cmd)) != NULL)
			{
				// error is set
				free(buf);
				return 0;
			} else 
			{
				// caller handles error
				return 1;
			}
		}

		if (sess->out_fd == -1) 
		{
			// caller handles error
			return 1;
		}
	} while (!MICommandCompleted(cmd));
	
	return 0;
}

static void
_cti_gdb_sendError(char *err_str)
{
	cti_gdb_msg_t *	msg;
	
	// sanity
	if (err_str == NULL)
		return;
	
	// create the request message
	if ((msg = _cti_gdb_createMsg(MSG_ERROR, err_str)) == NULL)
	{
		free(err_str);
		return;
	}
	
	// send the message
	if (_cti_gdb_sendMsg(_cti_gdb_pipe_w, msg))
	{
		_cti_gdb_consumeMsg(msg);
		return;
	}
	
	// cleanup the message we just sent
	_cti_gdb_consumeMsg(msg);
}

static int
_cti_gdb_setup_gdbmi_environment(void)
{
	MICommand *		cmd;
	
	cmd = MIGDBSet("confirm", "off");
	if (_cti_gdb_SendMICommand(_cti_gdb_sess, cmd))
	{
		_cti_gdb_sendError(strdup("_cti_gdb_SendMICommand failed!"));
		MICommandFree(cmd);
		return 1;
	}
	if (!MICommandResultOK(cmd)) 
	{
		_cti_gdb_sendError(MICommandResultErrorMessage(cmd));
		MICommandFree(cmd);
		return 1;
	}
	MICommandFree(cmd);
	
	// demangle c++ names
	cmd = MIGDBSet("print demangle", "on");
	if (_cti_gdb_SendMICommand(_cti_gdb_sess, cmd))
	{
		_cti_gdb_sendError(strdup("_cti_gdb_SendMICommand failed!"));
		MICommandFree(cmd);
		return 1;
	}
	if (!MICommandResultOK(cmd)) 
	{
		_cti_gdb_sendError(MICommandResultErrorMessage(cmd));
		MICommandFree(cmd);
		return 1;
	}
	MICommandFree(cmd);
	
	cmd = MIGDBSet("print asm-demangle", "on");
	if (_cti_gdb_SendMICommand(_cti_gdb_sess, cmd))
	{
		_cti_gdb_sendError(strdup("_cti_gdb_SendMICommand failed!"));
		MICommandFree(cmd);
		return 1;
	}
	if (!MICommandResultOK(cmd)) 
	{
		_cti_gdb_sendError(MICommandResultErrorMessage(cmd));
		MICommandFree(cmd);
		return 1;
	}
	MICommandFree(cmd);
	
	// print derived types from the vtable
	cmd = MIGDBSet("print object", "on");
	if (_cti_gdb_SendMICommand(_cti_gdb_sess, cmd))
	{
		_cti_gdb_sendError(strdup("_cti_gdb_SendMICommand failed!"));
		MICommandFree(cmd);
		return 1;
	}
	if (!MICommandResultOK(cmd)) 
	{
		_cti_gdb_sendError(MICommandResultErrorMessage(cmd));
		MICommandFree(cmd);
		return 1;
	}
	MICommandFree(cmd);
	
	// limit backtrace size to 1000
	cmd = MIGDBSet("backtrace limit", "1000");
	if (_cti_gdb_SendMICommand(_cti_gdb_sess, cmd))
	{
		_cti_gdb_sendError(strdup("_cti_gdb_SendMICommand failed!"));
		MICommandFree(cmd);
		return 1;
	}
	if (!MICommandResultOK(cmd)) 
	{
		_cti_gdb_sendError(MICommandResultErrorMessage(cmd));
		MICommandFree(cmd);
		return 1;
	}
	MICommandFree(cmd);
	
	// unwind signals triggered by gdb
	cmd = MIGDBSet("unwindonsignal", "on");
	if (_cti_gdb_SendMICommand(_cti_gdb_sess, cmd))
	{
		_cti_gdb_sendError(strdup("_cti_gdb_SendMICommand failed!"));
		MICommandFree(cmd);
		return 1;
	}
	if (!MICommandResultOK(cmd)) 
	{
		_cti_gdb_sendError(MICommandResultErrorMessage(cmd));
		MICommandFree(cmd);
		return 1;
	}
	MICommandFree(cmd);
	
	// always continue on fatal errors
	cmd = MIGDBSet("continue-on-fatal-error", "on");
	if (_cti_gdb_SendMICommand(_cti_gdb_sess, cmd))
	{
		_cti_gdb_sendError(strdup("_cti_gdb_SendMICommand failed!"));
		MICommandFree(cmd);
		return 1;
	}
	// for some reason this command always reports errors, so don't error check it.
	MICommandFree(cmd);
	
	return 0;
}

int 
main(int argc, char *argv[])
{
	int				opt_ind = 0;
	int				c;
	long int		val;
	char *			end_p;
	char *			gdb = NULL;
	char *			starter	= NULL;
	char *			s_args[2] = {0};
	char *			input_file	= NULL;
	MICommand *		cmd;
	cti_gdb_msg_t *	msg;

	// we require at least 4 arguments beyond argv[0]
	if (argc < 5)
	{
		usage(argv[0]);
		return 1;
	}

	// parse the provide args
	while ((c = getopt_long(argc, argv, "r:w:g:s:i:h", long_opts, &opt_ind)) != -1)
	{
		switch (c)
		{
			case 0:
				// if this is a flag, do nothing
				break;
				
			case 'r':
				if (optarg == NULL)
				{
					usage(argv[0]);
					return 1;
				}
				
				// convert the string into the actual fd
				errno = 0;
				end_p = NULL;
				val = strtol(optarg, &end_p, 10);
	
				// check for errors
				if ((errno == ERANGE && (val == LONG_MAX || val == LONG_MIN)) || (errno != 0 && val == 0))
				{
					perror("strtol");
					return 1;
				}
				if (end_p == NULL || *end_p != '\0')
				{
					perror("strtol");
					return 1;
				}
				if (val > INT_MAX || val < INT_MIN)
				{
					fprintf(stderr, "Invalid read fd argument.\n");
					return 1;
				}
				
				_cti_gdb_pipe_r = fdopen((int)val, "r");
				
				if (_cti_gdb_pipe_r == NULL)
				{
					fprintf(stderr, "Invalid read fd argument.\n");
					return 1;
				}
				
				break;
				
			case 'w':
				if (optarg == NULL)
				{
					usage(argv[0]);
					return 1;
				}
				
				// convert the string into the actual fd
				errno = 0;
				end_p = NULL;
				val = strtol(optarg, &end_p, 10);
				
				// check for errors
				if ((errno == ERANGE && (val == LONG_MAX || val == LONG_MIN)) || (errno != 0 && val == 0))
				{
					perror("strtol");
					return 1;
				}
				if (end_p == NULL || *end_p != '\0')
				{
					perror("strtol");
					return 1;
				}
				if (val > INT_MAX || val < INT_MIN)
				{
					fprintf(stderr, "Invalid write fd argument.\n");
					return 1;
				}
				
				_cti_gdb_pipe_w = fdopen((int)val, "w");
				
				if (_cti_gdb_pipe_w == NULL)
				{
					fprintf(stderr, "Invalid write fd argument.\n");
					return 1;
				}
				
				break;
				
			case 'g':
				if (optarg == NULL)
				{
					usage(argv[0]);
					return 1;
				}
				
				if (gdb != NULL)
				{
					free(gdb);
				}
				
				// strip leading whitespace
				while (*optarg == ' ')
				{
					++optarg;
				}
				
				// get the gdb binary string
				gdb = strdup(optarg);
				
				// ensure the gdb exists
				if (access(gdb, X_OK))
				{
					fprintf(stderr, "Invalid gdb argument.\n");
					return 1;
				}
				
				break;
				
			case 's':
				if (optarg == NULL)
				{
					usage(argv[0]);
					return 1;
				}
				
				if (starter != NULL)
				{
					free(starter);
				}
				
				// strip leading whitespace
				while (*optarg == ' ')
				{
					++optarg;
				}
				
				// get the starter binary string
				starter = strdup(optarg);
				
				break;
				
			case 'i':
				if (optarg == NULL)
				{
					usage(argv[0]);
					return 1;
				}
				
				if (input_file != NULL)
				{
					free(input_file);
				}
				
				// strip leading whitespace
				while (*optarg == ' ')
				{
					++optarg;
				}
				
				// get the input file string
				input_file = strdup(optarg);
				
				break;
				
			case 'h':
				usage(argv[0]);
				return 0;
				
			default:
				usage(argv[0]);
				return 1;
		}
	}
	
	// post-process required args to make sure we have everything we need
	if (_cti_gdb_pipe_r == NULL || _cti_gdb_pipe_w == NULL || gdb == NULL || starter == NULL)
	{
		usage(argv[0]);
		return 1;
	}

	// process any additional non-opt arguments
	for ( ; optind < argc; ++optind)
	{
		if (s_args[0] == NULL)
		{
			if (asprintf(&s_args[0], "%s", argv[optind]) < 0)
			{
				perror("asprintf");
				return 1;
			}
		} else
		{
			char *s_ptr = s_args[0];
			s_args[0] = NULL;
			if (asprintf(&s_args[0], "%s %s", s_ptr, argv[optind]) < 0)
			{
				perror("asprintf");
				return 1;
			}
			free(s_ptr);
		}
	}
	
	// handle input file if need be
	if (input_file != NULL)
	{
		if (s_args[0] == NULL)
		{
			if (asprintf(&s_args[0], "< %s", input_file) < 0)
			{
				perror("asprintf");
				return 1;
			}
		} else
		{
			char *s_ptr = s_args[0];
			s_args[0] = NULL;
			if (asprintf(&s_args[0], "%s < %s", s_ptr, input_file) < 0)
			{
				perror("asprintf");
				return 1;
			}
			free(s_ptr);
		}
	}
	
	// It is safe to write on the _cti_gdb_pipe_w fd now. All future errors should
	// go there since we have good args now. 
	
	_cti_gdb_sess = MISessionNew();
	
	//MISessionSetDebug(1);
	
	MISessionRegisterEventCallback(_cti_gdb_sess, event_callback);
	//MISessionRegisterConsoleCallback(_cti_gdb_sess, console_callback);
	//MISessionRegisterLogCallback(_cti_gdb_sess, log_callback);
	//MISessionRegisterTargetCallback(_cti_gdb_sess, target_callback);
	
	// Set provided gdb binary path
	MISessionSetGDBPath(_cti_gdb_sess, gdb);
	
	if (MISessionStartLocal(_cti_gdb_sess, starter) < 0)
	{
		// Send an error message and exit
		_cti_gdb_sendError(strdup("Could not start debugger!"));
		_cti_gdb_cleanupMI();
		return 1;
	}
	
	if (_cti_gdb_setup_gdbmi_environment())
	{
		// error already sent
		_cti_gdb_cleanupMI();
		return 1;
	}
	
	// set arguments for launcher
	cmd = MIExecArguments(s_args);
	if (_cti_gdb_SendMICommand(_cti_gdb_sess, cmd))
	{
		_cti_gdb_sendError(strdup("_cti_gdb_SendMICommand failed!"));
		MICommandFree(cmd);
		_cti_gdb_cleanupMI();
		return 1;
	}
	if (!MICommandResultOK(cmd)) 
	{
		_cti_gdb_sendError(MICommandResultErrorMessage(cmd));
		MICommandFree(cmd);
		_cti_gdb_cleanupMI();
		return 1;
	}
	MICommandFree(cmd);
	
	// set the lang to c.
	cmd = MIGDBSet("lang", "c");
	if (_cti_gdb_SendMICommand(_cti_gdb_sess, cmd))
	{
		_cti_gdb_sendError(strdup("_cti_gdb_SendMICommand failed!"));
		MICommandFree(cmd);
		_cti_gdb_cleanupMI();
		return 1;
	}
	if (!MICommandResultOK(cmd)) 
	{
		_cti_gdb_sendError(MICommandResultErrorMessage(cmd));
		MICommandFree(cmd);
		_cti_gdb_cleanupMI();
		return 1;
	}
	MICommandFree(cmd);
	
	// Insert breakpoint at main
	cmd = MIBreakInsert(0, 0, NULL, 0, "main", 0);
	if (_cti_gdb_SendMICommand(_cti_gdb_sess, cmd))
	{
		_cti_gdb_sendError(strdup("_cti_gdb_SendMICommand failed!"));
		MICommandFree(cmd);
		_cti_gdb_cleanupMI();
		return 1;
	}
	if (!MICommandResultOK(cmd)) 
	{
		_cti_gdb_sendError(MICommandResultErrorMessage(cmd));
		MICommandFree(cmd);
		_cti_gdb_cleanupMI();
		return 1;
	}
	MICommandFree(cmd);
	
	// Insert breakpoint at MPIR_Breakpoint
	cmd = MIBreakInsert(0, 0, NULL, 0, "MPIR_Breakpoint", 0);
	if (_cti_gdb_SendMICommand(_cti_gdb_sess, cmd))
	{
		_cti_gdb_sendError(strdup("_cti_gdb_SendMICommand failed!"));
		MICommandFree(cmd);
		_cti_gdb_cleanupMI();
		return 1;
	}
	if (!MICommandResultOK(cmd)) 
	{
		_cti_gdb_sendError(MICommandResultErrorMessage(cmd));
		MICommandFree(cmd);
		_cti_gdb_cleanupMI();
		return 1;
	}
	MICommandFree(cmd);
	
	// issue a run command, this starts the launcher
	MICommand* exec_run_cmd = MIExecRun();
	if (_cti_gdb_SendMICommand(_cti_gdb_sess, exec_run_cmd))
	{
		_cti_gdb_sendError(strdup("_cti_gdb_SendMICommand failed!"));
		MICommandFree(exec_run_cmd);
		_cti_gdb_cleanupMI();
		return 1;
	}
	if (!MICommandResultOK(exec_run_cmd)) 
	{
		_cti_gdb_sendError(MICommandResultErrorMessage(exec_run_cmd));
		MICommandFree(exec_run_cmd);
		_cti_gdb_cleanupMI();
		return 1;
	}

	// wait for the async command to complete
	while (!_cti_gdb_ready)
	{
		if (MISessionProgress(_cti_gdb_sess) == -1)
		{
			_cti_gdb_sendError(strdup("MISessionProgress failed!"));
			_cti_gdb_cleanupMI();
			return 1;
		}
	}
	
	// ensure we got a breakpoint hit event
	if (_cti_gdb_event == NULL || _cti_gdb_event->type != MIEventTypeBreakpointHit)
	{
		_cti_gdb_sendError(strdup("Failed to run launcher to main!"));
		_cti_gdb_cleanupMI();
		return 1;
	}
	
	// reset the events
	_cti_gdb_ready = 0;
	MIEventFree(_cti_gdb_event);
	_cti_gdb_event = NULL;
	
	//Get the launcher pid from the result of exec-run
	MIResultRecord* exec_run_return_record = MICommandResult(exec_run_cmd);
	pid_t launcher_pid = -1;
	MIResult * result;
	for (MIListSet(exec_run_return_record->results); (result = (MIResult *)MIListGet(exec_run_return_record->results)) != NULL; ) {
		MIValue * value = result->value;
		char * str = NULL;
		if (value->type == MIValueTypeConst) {
			str = value->cstring;
		}
		if (strcmp(result->variable, "pid") == 0) {
			launcher_pid = (int)strtol(str, NULL, 10);
		}
	}
	MICommandFree(exec_run_cmd);

	// We are now sitting at main. Now set MPIR_being_debugged to 1.
	
	cmd = MIGDBSet("MPIR_being_debugged=1", NULL);
	if (_cti_gdb_SendMICommand(_cti_gdb_sess, cmd))
	{
		_cti_gdb_sendError(strdup("_cti_gdb_SendMICommand failed!"));
		MICommandFree(cmd);
		_cti_gdb_cleanupMI();
		return 1;
	}
	if (!MICommandResultOK(cmd)) 
	{
		_cti_gdb_sendError(MICommandResultErrorMessage(cmd));
		MICommandFree(cmd);
		_cti_gdb_cleanupMI();
		return 1;
	}
	MICommandFree(cmd);
	
	// Issue a continue command to hit the MPIR_breakpoint routine
	cmd = MIExecContinue();
	if (_cti_gdb_SendMICommand(_cti_gdb_sess, cmd))
	{
		_cti_gdb_sendError(strdup("_cti_gdb_SendMICommand failed!"));
		MICommandFree(cmd);
		_cti_gdb_cleanupMI();
		return 1;
	}
	if (!MICommandResultOK(cmd)) 
	{
		_cti_gdb_sendError(MICommandResultErrorMessage(cmd));
		MICommandFree(cmd);
		_cti_gdb_cleanupMI();
		return 1;
	}
	MICommandFree(cmd);
	
	// wait for the async command to complete
	while (!_cti_gdb_ready)
	{
		if (MISessionProgress(_cti_gdb_sess) == -1)
		{
			_cti_gdb_sendError(strdup("MISessionProgress failed!"));
			_cti_gdb_cleanupMI();
			return 1;
		}
	}
	
	// ensure we got a breakpoint hit event
	if (_cti_gdb_event == NULL || _cti_gdb_event->type != MIEventTypeBreakpointHit)
	{
		_cti_gdb_sendError(strdup("Failed to run launcher to main!"));
		_cti_gdb_cleanupMI();
		return 1;
	}
	
	
	
	
	// TODO: Ensure we hit the MPIR_Breakpoint routine
	

	
	// reset the events
	_cti_gdb_ready = 0;
	MIEventFree(_cti_gdb_event);
	_cti_gdb_event = NULL;
	
	// We are now finished with setup. We are sitting at the startup barrier.
	// Inform the parent that we are ready for further commands.
	
	// create the ready message
	if ((msg = _cti_gdb_createMsg(MSG_READY)) == NULL)
	{
		// send the error message
		if (_cti_gdb_err_string != NULL)
		{
			_cti_gdb_sendError(strdup(_cti_gdb_err_string));
		} else
		{
			_cti_gdb_sendError(strdup("Unknown gdb_MPIR error!\n"));
		}
		_cti_gdb_cleanupMI();
		return 1;
	}
	
	// send the message
	if (_cti_gdb_sendMsg(_cti_gdb_pipe_w, msg))
	{
		// send the error message
		if (_cti_gdb_err_string != NULL)
		{
			_cti_gdb_sendError(strdup(_cti_gdb_err_string));
		} else
		{
			_cti_gdb_sendError(strdup("Unknown gdb_MPIR error!\n"));
		}
		_cti_gdb_consumeMsg(msg);
		_cti_gdb_cleanupMI();
		return 1;
	}
	// cleanup the message we just sent
	_cti_gdb_consumeMsg(msg);
	
	// Main event loop
	while (1)
	{
		fd_set			fds;
		struct timeval	tv;
	
		// We want to select on our read fd for messages from the parent.
		FD_ZERO(&fds);
		FD_SET(fileno(_cti_gdb_pipe_r), &fds);
	
		// wait a max of twenty minutes for user to release, otherwise force exit.
		tv.tv_sec = 1200;
		tv.tv_usec = 0;
		
		switch (select(fileno(_cti_gdb_pipe_r)+1, &fds, NULL, NULL, &tv))
		{
			case -1:
				// check value of errno
				switch (errno)
				{
					// these are recoverable
					case EINTR:
						continue;
						
					// the rest are fatal
					default:
						_cti_gdb_sendError(strdup("select failed!"));
						_cti_gdb_cleanupMI();
						return 1;
				}
				break;
				
			case 0:
				// Timeout reached!
				_cti_gdb_sendError(strdup("Timeout period reached!"));
				_cti_gdb_cleanupMI();
				return 1;
				
			default:
				// parent sent us a command
				
				// recv response
				if ((msg = _cti_gdb_recvMsg(_cti_gdb_pipe_r)) == NULL)
				{
					if (_cti_gdb_err_string != NULL)
					{
						_cti_gdb_sendError(strdup(_cti_gdb_err_string));
					} else
					{
						_cti_gdb_sendError(strdup("Unknown gdb_MPIR error!\n"));
					}
					_cti_gdb_cleanupMI();
					return 1;
				}
	
				// process the response
				switch (msg->msg_type)
				{
					// Stuff that causes us to exit immidiately. Either we don't
					// know how to handle the requested command or the parent is
					// trying to force us to quit.
					case MSG_INIT:
					case MSG_ERROR:
					case MSG_READY:
					case MSG_EXIT:
						// Do not detach, just exit. Something went very wrong
						// on the parent side.
						_cti_gdb_sendError(strdup("Invalid msg_type!\n"));
						_cti_gdb_cleanupMI();
						_cti_gdb_consumeMsg(msg);
						
						return 1;

					case MSG_ID:
					{
						char *			res;
						char *			ptr;
						char *			end;
						cti_gdb_msg_t *	res_msg;
					
						// Ensure the payload string is not null
						if (msg->msg_payload.msg_string == NULL)
						{
							// bad message, this is not fatal
							_cti_gdb_sendError(strdup("Bad MSG_ID payload string."));
							
							break;
						}
						
						// evaluate the value of the provided symbol
						cmd = MIDataEvaluateExpression(msg->msg_payload.msg_string);
						if (_cti_gdb_SendMICommand(_cti_gdb_sess, cmd))
						{
							// this is a fatal error
							_cti_gdb_sendError(strdup("_cti_gdb_SendMICommand failed!"));
							MICommandFree(cmd);
							_cti_gdb_cleanupMI();
							_cti_gdb_consumeMsg(msg);
							
							return 1;
						}
						if (!MICommandResultOK(cmd)) 
						{
							// this is a non fatal error, we can recover
							_cti_gdb_sendError(MICommandResultErrorMessage(cmd));
							MICommandFree(cmd);
							
							break;
						}
						
						// get the string value of the result
						if ((res = MIGetDataEvaluateExpressionInfo(cmd)) == NULL)
						{
							// this is a fatal error
							_cti_gdb_sendError(strdup("MIGetDataEvaluateExpressionInfo failed!"));
							MICommandFree(cmd);
							_cti_gdb_cleanupMI();
							_cti_gdb_consumeMsg(msg);
							
							return 1;
						}
						
						// cleanup the command
						MICommandFree(cmd);
						
						// gdb prints string values at the end in quotes, so lets
						// find the first quote
						if ((ptr = strchr(res, '\"')) == NULL)
						{
							// this is a fatal error
							_cti_gdb_sendError(strdup("Bad data returned by gdb."));
							free(res);
							_cti_gdb_cleanupMI();
							_cti_gdb_consumeMsg(msg);
							
							return 1;
						}
						
						// point past the quote
						++ptr;
						
						// now find the last quote
						if ((end = strrchr(ptr, '\"')) == NULL)
						{
							// this is a fatal error
							_cti_gdb_sendError(strdup("Bad data returned by gdb."));
							free(res);
							_cti_gdb_cleanupMI();
							_cti_gdb_consumeMsg(msg);
							
							return 1;
						}
						
						// turn it into a null terminator
						*end = '\0';
						
						// create the ready message
						if ((res_msg = _cti_gdb_createMsg(MSG_ID, strdup(ptr))) == NULL)
						{
							// send the error message
							if (_cti_gdb_err_string != NULL)
							{
								_cti_gdb_sendError(strdup(_cti_gdb_err_string));
							} else
							{
								_cti_gdb_sendError(strdup("Unknown gdb_MPIR error!\n"));
							}
							free(res);
							_cti_gdb_cleanupMI();
							_cti_gdb_consumeMsg(msg);
							return 1;
						}
						
						// send the message
						if (_cti_gdb_sendMsg(_cti_gdb_pipe_w, res_msg))
						{
							// send the error message
							if (_cti_gdb_err_string != NULL)
							{
								_cti_gdb_sendError(strdup(_cti_gdb_err_string));
							} else
							{
								_cti_gdb_sendError(strdup("Unknown gdb_MPIR error!\n"));
							}
							_cti_gdb_consumeMsg(res_msg);
							free(res);
							_cti_gdb_cleanupMI();
							_cti_gdb_consumeMsg(msg);
							return 1;
						}
						
						// cleanup
						_cti_gdb_consumeMsg(res_msg);
						free(res);
						_cti_gdb_consumeMsg(msg);
						
						break;
					}
					
					case MSG_PID:
					{
						char *			res;
						int				p_size;
						cti_pid_t *		pids;
						char *			cmd_str;
						int				len, i;
						int				err = 0;
						
						// cleanup the message, we are done with it
						_cti_gdb_consumeMsg(msg);
						
						// evaluate the value of MPIR_proctable_size
						cmd = MIDataEvaluateExpression("MPIR_proctable_size");
						if (_cti_gdb_SendMICommand(_cti_gdb_sess, cmd))
						{
							// this is a fatal error
							_cti_gdb_sendError(strdup("_cti_gdb_SendMICommand failed!"));
							MICommandFree(cmd);
							_cti_gdb_cleanupMI();
							
							return 1;
						}
						if (!MICommandResultOK(cmd)) 
						{
							// this is a non fatal error, we can recover
							_cti_gdb_sendError(MICommandResultErrorMessage(cmd));
							MICommandFree(cmd);
							
							break;
						}
						
						// get the string value of the result
						if ((res = MIGetDataEvaluateExpressionInfo(cmd)) == NULL)
						{
							// this is a fatal error
							_cti_gdb_sendError(strdup("MIGetDataEvaluateExpressionInfo failed!"));
							MICommandFree(cmd);
							_cti_gdb_cleanupMI();
							
							return 1;
						}
						
						// cleanup the command
						MICommandFree(cmd);
						
						// convert the res to an int
						p_size = atoi(res);
						free(res);
						
						// ensure p_size is non-zero
						if (p_size <= 0)
						{
							// this is a non fatal error, we can recover
							_cti_gdb_sendError("Invalid MPIR_proctable_size value.");
							
							break;
						}
						
						// create a cti_pid_t obj
						if ((pids = _cti_gdb_newPid(p_size)) == NULL)
						{
							// this is a non fatal error, we can recover
							if (_cti_gdb_err_string != NULL)
							{
								_cti_gdb_sendError(strdup(_cti_gdb_err_string));
							} else
							{
								_cti_gdb_sendError(strdup("Unknown gdb_MPIR error!\n"));
							}
							
							break;
						}
						
						// create the command string based on p_size, this will ensure
						// that our array is big enough to hold any of the commands
						if (asprintf(&cmd_str, "*((int*)((void*)MPIR_proctable+%lu))", p_size*sizeof(MPIR_PROCDESC)+offsetof(MPIR_PROCDESC, pid)) < 0)
						{
							// this is a non fatal error, we can recover
							_cti_gdb_sendError("asprintf failed.");
							_cti_gdb_freePid(pids);
							
							break;
						}
						// save the length of this string
						len = strlen(cmd_str);
						
						// start grabbing the pids for each rank
						for (i=0; i < p_size; ++i)
						{
							// create the command string
							if (snprintf(cmd_str, len+1, "*((int*)((void*)MPIR_proctable+%lu))", i*sizeof(MPIR_PROCDESC)+offsetof(MPIR_PROCDESC, pid)) < 0)
							{
								// this is a non fatal error, we can recover
								_cti_gdb_sendError("snprintf failed.");
								++err;
								
								break;
							}
							
							// evaluate the value of pid
							cmd = MIDataEvaluateExpression(cmd_str);
							if (_cti_gdb_SendMICommand(_cti_gdb_sess, cmd))
							{
								// this is a fatal error
								_cti_gdb_sendError(strdup("_cti_gdb_SendMICommand failed!"));
								MICommandFree(cmd);
								_cti_gdb_cleanupMI();
								free(cmd_str);
								_cti_gdb_freePid(pids);
								
								return 1;
							}
							if (!MICommandResultOK(cmd))
							{
								// this is a non fatal error, we can recover
								_cti_gdb_sendError(MICommandResultErrorMessage(cmd));
								MICommandFree(cmd);
								++err;
								
								break;
							}
							
							// get the string value of the result
							if ((res = MIGetDataEvaluateExpressionInfo(cmd)) == NULL)
							{
								// this is a fatal error
								_cti_gdb_sendError(strdup("MIGetDataEvaluateExpressionInfo failed!"));
								MICommandFree(cmd);
								_cti_gdb_cleanupMI();
								free(cmd_str);
								_cti_gdb_freePid(pids);
								
								return 1;
							}
							
							// set the pid
							pids->pid[i] = atoi(res);
							
							// done with iteration
							free(res);
							MICommandFree(cmd);
						}
						
						// cleanup
						free(cmd_str);
						
						if (err)
						{
							// error occured - error msg already sent
							_cti_gdb_freePid(pids);
							
							break;
						}
						
						// create the pid message
						if ((msg = _cti_gdb_createMsg(MSG_PID, pids)) == NULL)
						{
							// send the error message
							if (_cti_gdb_err_string != NULL)
							{
								_cti_gdb_sendError(strdup(_cti_gdb_err_string));
							} else
							{
								_cti_gdb_sendError(strdup("Unknown gdb_MPIR error!\n"));
							}
							_cti_gdb_cleanupMI();
							
							return 1;
						}
						
						// send the message
						if (_cti_gdb_sendMsg(_cti_gdb_pipe_w, msg))
						{
							// send the error message
							if (_cti_gdb_err_string != NULL)
							{
								_cti_gdb_sendError(strdup(_cti_gdb_err_string));
							} else
							{
								_cti_gdb_sendError(strdup("Unknown gdb_MPIR error!\n"));
							}
							_cti_gdb_cleanupMI();
							_cti_gdb_consumeMsg(msg);
							
							return 1;
						}
						
						// cleanup
						_cti_gdb_consumeMsg(msg);
						
						break;
					}

					case MSG_PROCTABLE:
					{
						char *			res;
						int				p_size;
						cti_mpir_proctable_t *		proctable;
						char *			cmd_str;
						int				len, i;
						int				err = 0;
						
						// cleanup the message, we are done with it
						_cti_gdb_consumeMsg(msg);
						
						// evaluate the value of MPIR_proctable_size
						cmd = MIDataEvaluateExpression("MPIR_proctable_size");
						if (_cti_gdb_SendMICommand(_cti_gdb_sess, cmd))
						{
							// this is a fatal error
							_cti_gdb_sendError(strdup("_cti_gdb_SendMICommand failed!"));
							MICommandFree(cmd);
							_cti_gdb_cleanupMI();
							
							return 1;
						}
						if (!MICommandResultOK(cmd)) 
						{
							// this is a non fatal error, we can recover
							_cti_gdb_sendError(MICommandResultErrorMessage(cmd));
							MICommandFree(cmd);
							
							break;
						}
						
						// get the string value of the result
						if ((res = MIGetDataEvaluateExpressionInfo(cmd)) == NULL)
						{
							// this is a fatal error
							_cti_gdb_sendError(strdup("MIGetDataEvaluateExpressionInfo failed!"));
							MICommandFree(cmd);
							_cti_gdb_cleanupMI();
							
							return 1;
						}
						
						// cleanup the command
						MICommandFree(cmd);
						
						// convert the res to an int
						p_size = atoi(res);
						free(res);
						
						// ensure p_size is non-zero
						if (p_size <= 0)
						{
							// this is a non fatal error, we can recover
							_cti_gdb_sendError("Invalid MPIR_proctable_size value.");
							
							break;
						}
						
						// create a cti_proctable_t obj
						if ((proctable = _cti_gdb_newProctable(p_size)) == NULL)
						{
							// this is a non fatal error, we can recover
							if (_cti_gdb_err_string != NULL)
							{
								_cti_gdb_sendError(strdup(_cti_gdb_err_string));
							} else
							{
								_cti_gdb_sendError(strdup("Unknown gdb_MPIR error!\n"));
							}
							
							break;
						}
						
						// create the command string based on p_size, this will ensure
						// that our array is big enough to hold any of the commands
						if (asprintf(&cmd_str, "*((int*)((void*)MPIR_proctable+%lu))", p_size*sizeof(MPIR_PROCDESC)+offsetof(MPIR_PROCDESC, pid)) < 0)
						{
							// this is a non fatal error, we can recover
							_cti_gdb_sendError("asprintf failed.");
							_cti_gdb_freeProctable(proctable);
							
							break;
						}
						// save the length of this string
						len = strlen(cmd_str);

						proctable->num_pids = p_size;
						
						// start grabbing the pids for each rank
						for (i=0; i < p_size; ++i)
						{
							// create the command string
							if (snprintf(cmd_str, len+1, "*((int*)((void*)MPIR_proctable+%lu))", i*sizeof(MPIR_PROCDESC)+offsetof(MPIR_PROCDESC, pid)) < 0)
							{
								// this is a non fatal error, we can recover
								_cti_gdb_sendError("snprintf failed.");
								++err;
								
								break;
							}
							
							// evaluate the value of pid
							cmd = MIDataEvaluateExpression(cmd_str);
							if (_cti_gdb_SendMICommand(_cti_gdb_sess, cmd))
							{
								// this is a fatal error
								_cti_gdb_sendError(strdup("_cti_gdb_SendMICommand failed!"));
								MICommandFree(cmd);
								_cti_gdb_cleanupMI();
								free(cmd_str);
								_cti_gdb_freeProctable(proctable);
								
								return 1;
							}
							if (!MICommandResultOK(cmd))
							{
								// this is a non fatal error, we can recover
								_cti_gdb_sendError(MICommandResultErrorMessage(cmd));
								MICommandFree(cmd);
								++err;
								
								break;
							}
							
							// get the string value of the result
							if ((res = MIGetDataEvaluateExpressionInfo(cmd)) == NULL)
							{
								// this is a fatal error
								_cti_gdb_sendError(strdup("MIGetDataEvaluateExpressionInfo failed!"));
								MICommandFree(cmd);
								_cti_gdb_cleanupMI();
								free(cmd_str);
								_cti_gdb_freeProctable(proctable);
								
								return 1;
							}
							
							// set the pid
							proctable->pids[i] = atoi(res);

							//TODO: anything else to reset here?
							free(res);
							res = NULL;
							MICommandFree(cmd);

							char* hostname_cmd_str;

							// create the command string
							if (asprintf(&hostname_cmd_str, "*(*((char**)((void*)MPIR_proctable+%lu)))@strlen(*((char**)((void*)MPIR_proctable+%lu)))", i*sizeof(MPIR_PROCDESC)+offsetof(MPIR_PROCDESC, host_name), i*sizeof(MPIR_PROCDESC)+offsetof(MPIR_PROCDESC, host_name)) < 0)
							{
								// this is a non fatal error, we can recover
								_cti_gdb_sendError("snprintf failed.");
								++err;
								
								break;
							}

							
							// evaluate the value of pid
							cmd = MIDataEvaluateExpression(hostname_cmd_str);
							if (_cti_gdb_SendMICommand(_cti_gdb_sess, cmd))
							{
								// this is a fatal error
								_cti_gdb_sendError(strdup("_cti_gdb_SendMICommand failed!"));
								MICommandFree(cmd);
								_cti_gdb_cleanupMI();
								_cti_gdb_freeProctable(proctable);
								
								return 1;
							}
							if (!MICommandResultOK(cmd))
							{
								// this is a non fatal error, we can recover
								_cti_gdb_sendError(MICommandResultErrorMessage(cmd));
								MICommandFree(cmd);
								++err;
								
								break;
							}
							
							// get the string value of the result
							if ((res = MIGetDataEvaluateExpressionInfo(cmd)) == NULL)
							{
								// this is a fatal error
								_cti_gdb_sendError(strdup("MIGetDataEvaluateExpressionInfo failed!"));
								MICommandFree(cmd);
								_cti_gdb_cleanupMI();
								_cti_gdb_freeProctable(proctable);
								
								return 1;
							}

							//Remove the quotes around the string supplied by MI
							int result_length = strlen(res)+1;
							proctable->hostnames[i] = malloc(result_length-2);
							memcpy(proctable->hostnames[i], res+1, result_length-3);
							proctable->hostnames[i][result_length-3] = '\0';
							
							// done with iteration
							free(res);
							MICommandFree(cmd);
						}
						
						// cleanup
						free(cmd_str);
						
						if (err)
						{
							// error occured - error msg already sent
							_cti_gdb_freeProctable(proctable);
							
							break;
						}
						
						// create the pid message
						if ((msg = _cti_gdb_createMsg(MSG_PROCTABLE, proctable)) == NULL)
						{
							// send the error message
							if (_cti_gdb_err_string != NULL)
							{
								_cti_gdb_sendError(strdup(_cti_gdb_err_string));
							} else
							{
								_cti_gdb_sendError(strdup("Unknown gdb_MPIR error!\n"));
							}
							_cti_gdb_cleanupMI();
							
							return 1;
						}
						
						// send the message
						if (_cti_gdb_sendMsg(_cti_gdb_pipe_w, msg))
						{
							// send the error message
							if (_cti_gdb_err_string != NULL)
							{
								_cti_gdb_sendError(strdup(_cti_gdb_err_string));
							} else
							{
								_cti_gdb_sendError(strdup("Unknown gdb_MPIR error!\n"));
							}
							_cti_gdb_cleanupMI();
							_cti_gdb_consumeMsg(msg);
							
							return 1;
						}
						
						// cleanup
						_cti_gdb_consumeMsg(msg);
						
						break;
					}

					case MSG_LAUNCHER_PID:
					{
						// cleanup the message, we are done with it
						_cti_gdb_consumeMsg(msg);

						// create the pid message
						if ((msg = _cti_gdb_createMsg(MSG_LAUNCHER_PID, launcher_pid)) == NULL)
						{
							// send the error message
							if (_cti_gdb_err_string != NULL)
							{
								_cti_gdb_sendError(strdup(_cti_gdb_err_string));
							} else
							{
								_cti_gdb_sendError(strdup("Unknown gdb_MPIR error!\n"));
							}
							_cti_gdb_cleanupMI();
							
							return 1;
						}
						
						// send the message
						if (_cti_gdb_sendMsg(_cti_gdb_pipe_w, msg))
						{
							// send the error message
							if (_cti_gdb_err_string != NULL)
							{
								_cti_gdb_sendError(strdup(_cti_gdb_err_string));
							} else
							{
								_cti_gdb_sendError(strdup("Unknown gdb_MPIR error!\n"));
							}
							_cti_gdb_cleanupMI();
							_cti_gdb_consumeMsg(msg);
							
							return 1;
						}
						
						// cleanup
						_cti_gdb_consumeMsg(msg);
						
						break;
					}
					case MSG_RELEASE:
						// cleanup the message, we are done with it
						_cti_gdb_consumeMsg(msg);
						
						// detach from the srun process
						cmd = MITargetDetachAll();
						if (_cti_gdb_SendMICommand(_cti_gdb_sess, cmd))
						{
							// this is a fatal error
							_cti_gdb_sendError(strdup("_cti_gdb_SendMICommand failed!"));
							MICommandFree(cmd);
							_cti_gdb_cleanupMI();
							return 1;
						}
						if (!MICommandResultOK(cmd)) 
						{
							// this is a fatal error
							_cti_gdb_sendError(MICommandResultErrorMessage(cmd));
							MICommandFree(cmd);
							_cti_gdb_cleanupMI();
							return 1;
						}
						
						// create the exit message
						if ((msg = _cti_gdb_createMsg(MSG_EXIT)) == NULL)
						{
							// send the error message
							if (_cti_gdb_err_string != NULL)
							{
								_cti_gdb_sendError(strdup(_cti_gdb_err_string));
							} else
							{
								_cti_gdb_sendError(strdup("Unknown gdb_MPIR error!\n"));
							}
							_cti_gdb_cleanupMI();
							return 1;
						}
						
						// send the message
						if (_cti_gdb_sendMsg(_cti_gdb_pipe_w, msg))
						{
							// send the error message
							if (_cti_gdb_err_string != NULL)
							{
								_cti_gdb_sendError(strdup(_cti_gdb_err_string));
							} else
							{
								_cti_gdb_sendError(strdup("Unknown gdb_MPIR error!\n"));
							}
							_cti_gdb_consumeMsg(msg);
							_cti_gdb_cleanupMI();
							return 1;
						}
						
						// Clean things up, we are done now and can exit.
						_cti_gdb_consumeMsg(msg);
						_cti_gdb_cleanupMI();
						fclose(_cti_gdb_pipe_r);
						fclose(_cti_gdb_pipe_w);
						
						return 0;
				}
				
				break;
		}
	}
	
	// Shouldn't get here!!!
	return 1;
}

