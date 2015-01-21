/******************************************************************************\
 * cti_signal.h - Header file for the signal interface.
 *
 * Copyright 2014 Cray Inc.  All Rights Reserved.
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
#include <stdbool.h>

#include <sys/types.h>

typedef struct
{
	bool				o_restore;
	struct sigaction	o_sa[6];
	sigset_t			o_mask;
#define SIGQUIT_SA(s)		&(s)->o_sa[0]
#define SIGILL_SA(s)		&(s)->o_sa[1]
#define SIGABRT_SA(s)		&(s)->o_sa[2]
#define SIGFPE_SA(s)		&(s)->o_sa[3]
#define SIGSEGV_SA(s)		&(s)->o_sa[4]
#define SIGTERM_SA(s)		&(s)->o_sa[5]
#define SIGMASK(s)		&(s)->o_mask
} cti_signals_t;

cti_signals_t *	_cti_critical_section(void (*)(int));
void			_cti_restore_handler(cti_signals_t *);
void			_cti_end_critical_section(cti_signals_t *);
sigset_t *		_cti_block_signals(void);
int				_cti_restore_signals(sigset_t *);
int				_cti_setpgid_restore(pid_t, sigset_t *);
int				_cti_child_setpgid_restore(sigset_t *);
int				_cti_child_setpgid_unblock_all(sigset_t *);

#endif /* _CTI_SIGNAL_H */
