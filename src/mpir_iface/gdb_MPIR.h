/******************************************************************************\
 * gdb_MPIR.h - A header file for routines and data structures shared between
 *              the iface library calls and the starter process.
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

#ifndef _GDB_MPIR_H
#define _GDB_MPIR_H

#include <stdarg.h>
#include <stdio.h>

/* datatype typedefs */

enum cti_gdb_msgtype
{
	MSG_INIT = 0,	// used for initial state
	MSG_ERROR,
	MSG_EXIT,
	MSG_ID,
	MSG_PID,
	MSG_READY,
	MSG_RELEASE
};
typedef enum cti_gdb_msgtype cti_gdb_msgtype_t;

// used for returning rank/pid pairs
// TODO: If rank reordering is ever supported, this will need to change. It
// assumes that node hostname information is not needed because the BE has the
// first PE/num PEs available. That assumption will be incorrect for rank 
// reordered jobs since they are not following SMP order.
typedef struct
{
	size_t		num_pids;
	pid_t *		pid;
} cti_pid_t;

// This is a union in order to be extendible into the future if need be.
// Simply add another type to the union if we expect a different type of value
// to be returned.
typedef union
{
	char *		msg_string;
	cti_pid_t *	msg_pid;
} cti_gdb_msgpayload_t;

typedef struct
{
	cti_gdb_msgtype_t		msg_type;
	cti_gdb_msgpayload_t	msg_payload;
} cti_gdb_msg_t;

// Error string set by this layer - certain calls will allow the caller to use
// the string on error to do whatever they need to do with error handling.
extern char *	_cti_gdb_err_string;

/* function prototypes */
cti_pid_t *		_cti_gdb_newPid(size_t);
void			_cti_gdb_freePid(cti_pid_t *);
cti_gdb_msg_t *	_cti_gdb_createMsg(cti_gdb_msgtype_t, ...);
void			_cti_gdb_consumeMsg(cti_gdb_msg_t *);
int				_cti_gdb_sendMsg(FILE *, cti_gdb_msg_t *);
cti_gdb_msg_t *	_cti_gdb_recvMsg(FILE *);

#endif /* _GDB_MPIR_H */

