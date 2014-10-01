/******************************************************************************\
 * gdb_MPIR.h - Routines that are shared between the iface library calls and 
 *              the starter process.
 *
 * © 2014 Cray Inc.	All Rights Reserved.
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
	int					payload_len;
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
	memset(rtn, 0, sizeof(cti_gdb_msg_t)); // clear it to NULL
	
	// set the type
	rtn->msg_type = type;
	
	va_start(args, type);
	
	// set the payload based on the type
	switch (type)
	{
		case MSG_ERROR:
		case MSG_ID:
		{
			// These have a string payload
			rtn->msg_payload.msg_string = va_arg(args, char *);
			break;
		}
		
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
	}
	
	free(this);
}

int
_cti_gdb_sendMsg(int wfd, cti_gdb_msg_t *msg)
{
	cti_gdb_msgheader_t		msg_hdr;
	int						n;
	void *					pptr = NULL;

	// sanity
	if (msg == NULL)
	{
		_cti_gdb_set_error("_cti_gdb_sendMsg: Invalid arguments.\n"); 
		return 1;
	}
	
	// Init the header from msg
	msg_hdr.msg_type = msg->msg_type;
	
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
				msg_hdr.payload_len = strlen(msg->msg_payload.msg_string) + 1;
				pptr = msg->msg_payload.msg_string;
			}
			break;
			
		// These have no payload
		case MSG_INIT:
		case MSG_EXIT:
		case MSG_READY:
		case MSG_RELEASE:
			msg_hdr.payload_len = 0;
			break;
	}
		
	// write the header to the pipe
	n = write(wfd, &msg_hdr, sizeof(cti_gdb_msgheader_t));
	if (n < sizeof(cti_gdb_msgheader_t))
	{
		_cti_gdb_set_error("_cti_gdb_sendMsg: Pipe write failed.\n");
		return 1;
	}
	
	// optionally write the payload if there is any
	if (msg_hdr.payload_len > 0 && pptr != NULL)
	{
		n = write(wfd, pptr, msg_hdr.payload_len);
		if (n < msg_hdr.payload_len)
		{
			_cti_gdb_set_error("_cti_gdb_sendMsg: Pipe write failed.\n");
			return 1;
		}
	}
	
	return 0;
}

cti_gdb_msg_t *
_cti_gdb_recvMsg(int rfd)
{
	cti_gdb_msg_t *			rtn;
	cti_gdb_msgheader_t		msg_head;
	int						n,r,nr;
		
	// allocate the return structure
	if ((rtn = malloc(sizeof(cti_gdb_msg_t))) == NULL)
	{
		// Malloc failed
		_cti_gdb_set_error("malloc failed.");
		return NULL;
	}
	memset(rtn, 0, sizeof(cti_gdb_msg_t)); // clear it to NULL
		
	// Block on read until we read the entire header
	n = read(rfd, &msg_head, sizeof(cti_gdb_msgheader_t));
	if (n <= 0)
	{
		_cti_gdb_set_error("_cti_gdb_recvMsg: Pipe read failed.\n");
		_cti_gdb_consumeMsg(rtn);
		return NULL;
	}
	// Since this is a pipe, we need to ensure we read the whole thing.
	while (n < sizeof(cti_gdb_msgheader_t))
	{
		r = sizeof(cti_gdb_msgheader_t) - n;
		nr = read(rfd, (&msg_head)+n, r);
		if (nr <= 0)
		{
			_cti_gdb_set_error("_cti_gdb_recvMsg: Pipe read failed.\n");
			_cti_gdb_consumeMsg(rtn);
			return NULL;
		}
		n += nr;
	}
	
	// Receive the payload if needed
	switch (msg_head.msg_type)
	{
		case MSG_ERROR:
			// Ensure that there is payload
			if (msg_head.payload_len <= 0)
			{
				_cti_gdb_set_error("_cti_gdb_recvMsg: Failed to read MSG_ERROR string on pipe.\n");
				_cti_gdb_consumeMsg(rtn);
				return NULL;
			}
		
			// Something went wrong on their end, get the error string and cleanup
			if ((rtn->msg_payload.msg_string = malloc(msg_head.payload_len)) == NULL)
			{
				_cti_gdb_set_error("malloc failed.");
				_cti_gdb_consumeMsg(rtn);
				return NULL;
			}
		
			// Now block on read until we read the payload
			n = read(rfd, rtn->msg_payload.msg_string, msg_head.payload_len);
			if (n <= 0)
			{
				_cti_gdb_set_error("_cti_gdb_recvMsg: Failed to read MSG_ERROR string on pipe.\n");
				_cti_gdb_consumeMsg(rtn);
				return NULL;
			}
			// Since this is a pipe, we need to ensure we read the whole thing.
			while (n < msg_head.payload_len)
			{
				r = msg_head.payload_len - n;
				nr = read(rfd, (rtn->msg_payload.msg_string)+n, r);
				if (nr <= 0)
				{
					// Null terminate the string and set error
					*((rtn->msg_payload.msg_string) + n) = '\0';
					_cti_gdb_set_error("_cti_gdb_recvMsg: Pipe read failed. Partial MSG_ERROR string: %s\n", rtn->msg_payload.msg_string);
					_cti_gdb_consumeMsg(rtn);
					return NULL;
				}
				n += nr;
			}
		
			// ensure string is null terminated, otherwise overwrite the last bit
			// in the string with a null terminator
			if (*((rtn->msg_payload.msg_string) + msg_head.payload_len-1) != '\0')
			{
				*((rtn->msg_payload.msg_string) + msg_head.payload_len-1) = '\0';
			}
		
			// set the error string and cleanup
			_cti_gdb_set_error("%s\n", rtn->msg_payload.msg_string);
			_cti_gdb_consumeMsg(rtn);
			return NULL;
	
		case MSG_ID:
			// Ensure that there is payload
			if (msg_head.payload_len <= 0)
			{
				_cti_gdb_set_error("_cti_gdb_recvMsg: Failed to read MSG_ID string on pipe.\n");
				_cti_gdb_consumeMsg(rtn);
				return NULL;
			}
		
			if ((rtn->msg_payload.msg_string = malloc(msg_head.payload_len)) == NULL)
			{
				_cti_gdb_set_error("malloc failed.");
				_cti_gdb_consumeMsg(rtn);
				return NULL;
			}
		
			// Now block on read until we read the payload
			n = read(rfd, rtn->msg_payload.msg_string, msg_head.payload_len);
			if (n <= 0)
			{
				_cti_gdb_set_error("_cti_gdb_recvMsg: Failed to read MSG_ID string on pipe.\n");
				_cti_gdb_consumeMsg(rtn);
				return NULL;
			}
			// Since this is a pipe, we need to ensure we read the whole thing.
			while (n < msg_head.payload_len)
			{
				r = msg_head.payload_len - n;
				nr = read(rfd, (rtn->msg_payload.msg_string)+n, r);
				if (nr <= 0)
				{
					_cti_gdb_set_error("_cti_gdb_recvMsg: Failed to read MSG_ID string on pipe.\n");
					_cti_gdb_consumeMsg(rtn);
					return NULL;
				}
				n += nr;
			}
		
			// ensure string is null terminated, otherwise overwrite the last bit
			// in the string with a null terminator
			if (*((rtn->msg_payload.msg_string) + msg_head.payload_len-1) != '\0')
			{
				*((rtn->msg_payload.msg_string) + msg_head.payload_len-1) = '\0';
			}
			break;
		
		// There is no payload for everything else
		default:
			// Ensure that there is no payload, otherwise something went horribly wrong.
			if (msg_head.payload_len > 0)
			{
				_cti_gdb_set_error("_cti_gdb_recvMsg: Payload recv on non-payload msg!\n");
				_cti_gdb_consumeMsg(rtn);
				return NULL;
			}
			break;
	}
	
	// set the msg_type
	rtn->msg_type = msg_head.msg_type;
	
	// All done
	return rtn;
}

