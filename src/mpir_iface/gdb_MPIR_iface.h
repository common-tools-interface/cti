/******************************************************************************\
 * gdb_MPIR_iface.h - A header file for the gdb MPIR starter interface.
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

#ifndef _GDB_MPIR_IFACE_H
#define _GDB_MPIR_IFACE_H

// gdb identifier
typedef int cti_gdb_id_t;

// used for returning rank/pid pairs - the array corresponds to the rank->pid
// pairing.
// XXX: This needs to remain consistent with gdb_MPIR.h
//
// TODO: If rank reordering is ever supported, this will need to change. It
// assumes that node hostname information is not needed because the BE has the
// first PE/num PEs available. That assumption will be incorrect for rank 
// reordered jobs since they are not following SMP order.
typedef struct
{
	size_t		num_pids;
	pid_t *		pid;
} cti_mpir_pid_t;

/* function prototypes */
cti_gdb_id_t		_cti_gdb_newInstance(void);
void				_cti_gdb_cleanup(cti_gdb_id_t);
void				_cti_gdb_cleanupAll(void);
void				_cti_gdb_execStarter(cti_gdb_id_t, const char *, const char *, const char *, const char * const[], const char *);
int					_cti_gdb_postFork(cti_gdb_id_t);
char *				_cti_gdb_getSymbolVal(cti_gdb_id_t, const char *);
cti_mpir_pid_t *	_cti_gdb_getAppPids(cti_gdb_id_t);
void				_cti_gdb_freeMpirPid(cti_mpir_pid_t *);
// After calling releaseBarrier, no further calls with this instance is possible. The child
// will have exited.
int				_cti_gdb_releaseBarrier(cti_gdb_id_t);

#endif /* _GDB_MPIR_IFACE_H */

