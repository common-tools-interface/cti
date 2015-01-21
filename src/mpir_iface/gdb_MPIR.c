/******************************************************************************\
 * gdb_MPIR.h - Routines that are shared between the iface library calls and 
 *              the starter process.
 *
 * Copyright 2014 Cray Inc.	All Rights Reserved.
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

#include "gdb_MPIR.h"

/* Types used here */
typedef struct
{
	cti_gdb_msgtype_t	msg_type;
	size_t				nmemb;
} cti_gdb_msgheader_t;

/* Global variables */
// We set this on error, the caller might ask for it.
char *	_cti_gdb_err_string = NULL;

static void
_cti_gdb_set_error(char *fmt, ...)
{
	va_list ap;

	if (fmt == NULL)
		return;
		
	if (_cti_gdb_err_string != NULL)
	{
		free(_cti_gdb_err_string);
		_cti_gdb_err_string = NULL;
	}

	va_start(ap, fmt);
	
	vasprintf(&_cti_gdb_err_string, fmt, ap);

	va_end(ap);
}

/*****************************************
** General functions used by both sides
*****************************************/

cti_pid_t *
_cti_gdb_newPid(size_t num_pids)
{
	cti_pid_t *	this;
	
	// create return type
	if ((this = malloc(sizeof(cti_pid_t))) == NULL)
	{
		// Malloc failed
		_cti_gdb_set_error("malloc failed.\n");
		return NULL;
	}
	
	// init
	this->num_pids = num_pids;
	
	// create inner pid array
	if ((this->pid = calloc(num_pids, sizeof(pid_t))) == NULL)
	{
		// calloc failed
		_cti_gdb_set_error("calloc failed.\n");
		free(this);
		return NULL;
	}
	
	return this;
}

void
_cti_gdb_freePid(cti_pid_t *this)
{
	// sanity
	if (this == NULL)
		return;
		
	if (this->pid != NULL)
		free(this->pid);
	
	free(this);
}

cti_gdb_msg_t *
_cti_gdb_createMsg(cti_gdb_msgtype_t type, ...)
{
	cti_gdb_msg_t *	rtn;
	va_list			args;

	// allocate the return structure
	if ((rtn = malloc(sizeof(cti_gdb_msg_t))) == NULL)
	{
		// Malloc failed
		_cti_gdb_set_error("malloc failed.\n");
		return NULL;
	}
	memset(rtn, 0, sizeof(cti_gdb_msg_t));
	
	// set the type
	rtn->msg_type = type;
	
	va_start(args, type);
	
	// set the payload based on the type
	switch (type)
	{
		case MSG_ERROR:
		case MSG_ID:
			// These have a string payload
			rtn->msg_payload.msg_string = va_arg(args, char *);
			
			break;
		
		case MSG_PID:
			// These have a cti_gdb_pid_t payload
			rtn->msg_payload.msg_pid = va_arg(args, cti_pid_t *);
			
			break;
		
		case MSG_INIT:
		case MSG_EXIT:
		case MSG_READY:
		case MSG_RELEASE:
			// These have no payload
			break;
	}
	
	va_end(args);
	
	return rtn;
}

void
_cti_gdb_consumeMsg(cti_gdb_msg_t *this)
{
	if (this == NULL)
		return;
		
	switch (this->msg_type)
	{
		case MSG_INIT:
		case MSG_EXIT:
		case MSG_READY:
		case MSG_RELEASE:
			// Do nothing
			break;
			
		case MSG_ERROR:
		case MSG_ID:
			// try to free the payload string if there is one
			if (this->msg_payload.msg_string != NULL)
			{
				free(this->msg_payload.msg_string);
			}
			
			break;
			
		case MSG_PID:
			// try to free the payload pid if there is one
			if (this->msg_payload.msg_pid != NULL)
			{
				_cti_gdb_freePid(this->msg_payload.msg_pid);
			}
			
			break;
	}
	
	free(this);
}

int
_cti_gdb_sendMsg(FILE *wfp, cti_gdb_msg_t *msg)
{
	cti_gdb_msgheader_t	msg_hdr;
	
	// sanity
	if (msg == NULL || wfp == NULL)
	{
		_cti_gdb_set_error("_cti_gdb_sendMsg: Invalid arguments.\n"); 
		return 1;
	}
	
	// Init the header from msg
	memset(&msg_hdr, 0, sizeof(msg_hdr));
	msg_hdr.msg_type = msg->msg_type;
	msg_hdr.nmemb = 0;
	
	// get the length of the payload if there is any
	switch (msg->msg_type)
	{
		// These have an optional string
		case MSG_ERROR:
		case MSG_ID:
			if (msg->msg_payload.msg_string != NULL)
			{
				// calculate the payload length for the header
				// Add one for the null terminator!
				msg_hdr.nmemb = strlen(msg->msg_payload.msg_string) + 1;
			}
			break;
			
		// These have a pid set
		case MSG_PID:
			if (msg->msg_payload.msg_pid != NULL)
			{
				// length num_pids
				msg_hdr.nmemb = msg->msg_payload.msg_pid->num_pids;
			}
			
		// These have no payload
		case MSG_INIT:
		case MSG_EXIT:
		case MSG_READY:
		case MSG_RELEASE:
			break;
	}
	
	// write the header
	if (fwrite(&msg_hdr, sizeof(cti_gdb_msgheader_t), 1, wfp) != 1)
	{
		_cti_gdb_set_error("_cti_gdb_sendMsg: Pipe fwrite failed.\n");
		return 1;
	}
	
	// optionally write the payload if there is any
	if (msg_hdr.nmemb > 0)
	{
		switch (msg->msg_type)
		{
			case MSG_ERROR:
			case MSG_ID:
				if (fwrite(msg->msg_payload.msg_string, sizeof(char), msg_hdr.nmemb, wfp) != msg_hdr.nmemb)
				{
					_cti_gdb_set_error("_cti_gdb_sendMsg: Pipe fwrite failed.\n");
					return 1;
				}
				break;
			
			case MSG_PID:
				if (fwrite(msg->msg_payload.msg_pid->pid, sizeof(pid_t), msg_hdr.nmemb, wfp) != msg_hdr.nmemb)
				{
					_cti_gdb_set_error("_cti_gdb_sendMsg: Pipe fwrite failed.\n");
					return 1;
				}
				break;
			
			// These have no payload
			case MSG_INIT:
			case MSG_EXIT:
			case MSG_READY:
			case MSG_RELEASE:
				_cti_gdb_set_error("_cti_gdb_sendMsg: payload data set on invalid msg_type.\n");
				return 1;
		}
	}
	
	// flush the stream
	fflush(wfp);
	
	return 0;
}

cti_gdb_msg_t *
_cti_gdb_recvMsg(FILE *rfp)
{
	cti_gdb_msg_t *		rtn;
	cti_gdb_msgheader_t	msg_head;
	
	// sanity
	if (rfp == NULL)
	{
		_cti_gdb_set_error("_cti_gdb_recvMsg: Invalid arguments.\n"); 
		return NULL;
	}
		
	// allocate the return structure
	if ((rtn = malloc(sizeof(cti_gdb_msg_t))) == NULL)
	{
		// Malloc failed
		_cti_gdb_set_error("malloc failed.");
		return NULL;
	}
	memset(rtn, 0, sizeof(cti_gdb_msg_t)); // clear it to NULL
		
	// fread the header
	if (fread(&msg_head, sizeof(cti_gdb_msgheader_t), 1, rfp) != 1)
	{
		_cti_gdb_set_error("_cti_gdb_recvMsg: Pipe read failed.\n");
		_cti_gdb_consumeMsg(rtn);
		return NULL;
	}
	
	// set the msg_type
	rtn->msg_type = msg_head.msg_type;
	
	// Receive the payload if needed
	switch (msg_head.msg_type)
	{
		case MSG_ERROR:
			// Ensure that there is payload
			if (msg_head.nmemb <= 0)
			{
				_cti_gdb_set_error("_cti_gdb_recvMsg: Failed to read MSG_ERROR string on pipe.\n");
				_cti_gdb_consumeMsg(rtn);
				return NULL;
			}
		
			// Something went wrong on their end, get the error string and cleanup
			if ((rtn->msg_payload.msg_string = malloc(msg_head.nmemb * sizeof(char))) == NULL)
			{
				_cti_gdb_set_error("malloc failed.");
				_cti_gdb_consumeMsg(rtn);
				return NULL;
			}
			
			// fread the string
			if (fread(rtn->msg_payload.msg_string, sizeof(char), msg_head.nmemb, rfp) != msg_head.nmemb)
			{
				_cti_gdb_set_error("_cti_gdb_recvMsg: Failed to read MSG_ERROR string on pipe.\n");
				_cti_gdb_consumeMsg(rtn);
				return NULL;
			}
		
			// ensure string is null terminated, otherwise overwrite the last bit
			// in the string with a null terminator
			if (*((rtn->msg_payload.msg_string) + msg_head.nmemb - 1) != '\0')
			{
				*((rtn->msg_payload.msg_string) + msg_head.nmemb - 1) = '\0';
			}
		
			// set the error string and cleanup
			_cti_gdb_set_error("%s\n", rtn->msg_payload.msg_string);
			_cti_gdb_consumeMsg(rtn);
			
			return NULL;
	
		case MSG_ID:
			// Ensure that there is payload
			if (msg_head.nmemb <= 0)
			{
				_cti_gdb_set_error("_cti_gdb_recvMsg: Failed to read MSG_ID string on pipe.\n");
				_cti_gdb_consumeMsg(rtn);
				return NULL;
			}
		
			if ((rtn->msg_payload.msg_string = malloc(msg_head.nmemb * sizeof(char))) == NULL)
			{
				_cti_gdb_set_error("malloc failed.");
				_cti_gdb_consumeMsg(rtn);
				return NULL;
			}
			
			// fread the string
			if (fread(rtn->msg_payload.msg_string, sizeof(char), msg_head.nmemb, rfp) != msg_head.nmemb)
			{
				_cti_gdb_set_error("_cti_gdb_recvMsg: Failed to read MSG_ID string on pipe.\n");
				_cti_gdb_consumeMsg(rtn);
				return NULL;
			}
		
			// ensure string is null terminated, otherwise overwrite the last bit
			// in the string with a null terminator
			if (*((rtn->msg_payload.msg_string) + msg_head.nmemb - 1) != '\0')
			{
				*((rtn->msg_payload.msg_string) + msg_head.nmemb - 1) = '\0';
			}
			
			break;
			
		case MSG_PID:
			// This has an optional payload
			if (msg_head.nmemb <= 0)
			{
				rtn->msg_payload.msg_pid = NULL;
				break;
			}
			
			if ((rtn->msg_payload.msg_pid = _cti_gdb_newPid(msg_head.nmemb)) == NULL)
			{
				// error already set
				_cti_gdb_consumeMsg(rtn);
				return NULL;
			}
			
			// fread the pids
			if (fread(rtn->msg_payload.msg_pid->pid, sizeof(pid_t), msg_head.nmemb, rfp) != msg_head.nmemb)
			{
				_cti_gdb_set_error("_cti_gdb_recvMsg: Failed to read MSG_PID pids on pipe.\n");
				_cti_gdb_consumeMsg(rtn);
				return NULL;
			}
			
			break;
		
		// There is no payload for everything else
		default:
			// Ensure that there is no payload, otherwise something went horribly wrong.
			if (msg_head.nmemb > 0)
			{
				_cti_gdb_set_error("_cti_gdb_recvMsg: Payload recv on non-payload msg!\n");
				_cti_gdb_consumeMsg(rtn);
				return NULL;
			}
			
			break;
	}
	
	// All done
	return rtn;
}

