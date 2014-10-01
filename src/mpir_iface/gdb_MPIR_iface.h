/******************************************************************************\
 * gdb_MPIR_iface.h - A header file for the gdb MPIR starter interface.
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

#ifndef _GDB_MPIR_IFACE_H
#define _GDB_MPIR_IFACE_H

// gdb identifier
typedef int cti_gdb_id_t;

/* function prototypes */
cti_gdb_id_t	_cti_gdb_newInstance(void);
void			_cti_gdb_cleanup(cti_gdb_id_t);
void			_cti_gdb_cleanupAll(void);
void			_cti_gdb_execStarter(cti_gdb_id_t, const char *, char * const[], const char *);
int				_cti_gdb_postFork(cti_gdb_id_t);
char *			_cti_gdb_getJobId(cti_gdb_id_t, const char *);
int				_cti_gdb_releaseBarrier(cti_gdb_id_t);

#endif /* _GDB_MPIR_IFACE_H */

