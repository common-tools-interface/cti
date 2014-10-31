/******************************************************************************\
 * cti_signal.h - Header file for the signal interface.
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

#ifndef _CTI_SIGNAL_H
#define _CTI_SIGNAL_H

#include <signal.h>

#include <sys/types.h>

sigset_t *	_cti_block_signals(void);
int			_cti_setpgid_restore(pid_t, sigset_t *);
int			_cti_child_setpgid_restore(sigset_t *);
int			_cti_child_setpgid_unblock_all(sigset_t *);

#endif /* _CTI_SIGNAL_H */
