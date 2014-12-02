/******************************************************************************\
 * cti_signal.c - Functions relating to signal handling.
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

#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>

#include "cti_signal.h"

// static prototypes
static int	_cti_child_sig_common(void);

cti_signals_t *
_cti_critical_section(void (*handler)(int))
{
	cti_signals_t *		this;
	sigset_t			mask;
	struct sigaction 	sig_action;
	
	// sanity
	if (handler == NULL)
	{
		return NULL;
	}
	
	// create a new signals struct
	if ((this = malloc(sizeof(cti_signals_t))) == NULL)
	{
		// malloc failed
		return NULL;
	}
	memset(this, 0, sizeof(cti_signals_t));
	
	// If true, we should restore signals upon ending critical section. If the
	// handler already restored things, we don't want to restore twice.
	this->o_restore = true;
	
	// init the sig_action
	memset(&sig_action, 0, sizeof(sig_action));
	sig_action.sa_handler = handler;
	sig_action.sa_flags = 0;
	if (sigfillset(&sig_action.sa_mask))
	{
		// sigfillset failed
		free(this);
		return NULL;
	}
	
	// setup each sigaction
	if (sigaction(SIGQUIT,	&sig_action, SIGQUIT_SA(this)))
	{
		// sigaction failed
		free(this);
		return NULL;
	}
	if (sigaction(SIGILL,	&sig_action, SIGILL_SA(this)))
	{
		// sigaction failed
		free(this);
		return NULL;
	}
	if (sigaction(SIGABRT,	&sig_action, SIGABRT_SA(this)))
	{
		// sigaction failed
		free(this);
		return NULL;
	}
	if (sigaction(SIGFPE,	&sig_action, SIGFPE_SA(this)))
	{
		// sigaction failed
		free(this);
		return NULL;
	}
	if (sigaction(SIGSEGV,	&sig_action, SIGSEGV_SA(this)))
	{
		// sigaction failed
		free(this);
		return NULL;
	}
	if (sigaction(SIGTERM,	&sig_action, SIGTERM_SA(this)))
	{
		// sigaction failed
		free(this);
		return NULL;
	}
	
	// block all signals except for termination/error signals we want to handle
	if (sigfillset(&mask))
	{
		// sigfillset failed
		free(this);
		return NULL;
	}
	if (sigdelset(&mask, SIGQUIT))
	{
		// sigdelset failed
		free(this);
		return NULL;
	}
	if (sigdelset(&mask, SIGILL))
	{
		// sigdelset failed
		free(this);
		return NULL;
	}
	if (sigdelset(&mask, SIGABRT))
	{
		// sigdelset failed
		free(this);
		return NULL;
	}
	if (sigdelset(&mask, SIGFPE))
	{
		// sigdelset failed
		free(this);
		return NULL;
	}
	if (sigdelset(&mask, SIGSEGV))
	{
		// sigdelset failed
		free(this);
		return NULL;
	}
	if (sigdelset(&mask, SIGTERM))
	{
		// sigdelset failed
		free(this);
		return NULL;
	}
	
	// set the new procmask
	if (sigprocmask(SIG_SETMASK, &mask, SIGMASK(this)))
	{
		// sigprocmask failed
		free(this);
		return NULL;
	}
	
	return this;
}

// Call this to restore the default state of signals inside the handler
// function
void
_cti_restore_handler(cti_signals_t *this)
{
	// Do not restore if we get to the cleanup phase
	this->o_restore = false;

	// reset old signal handlers
	sigaction(SIGQUIT,	SIGQUIT_SA(this),	NULL);
	sigaction(SIGILL,	SIGILL_SA(this),	NULL);
	sigaction(SIGABRT,	SIGABRT_SA(this),	NULL);
	sigaction(SIGFPE,	SIGFPE_SA(this),	NULL);
	sigaction(SIGSEGV,	SIGSEGV_SA(this),	NULL);
	sigaction(SIGTERM,	SIGTERM_SA(this),	NULL);
	
	// reset old mask - this might cause another signal to be handled
	sigprocmask(SIG_SETMASK, SIGMASK(this), NULL);
}

void
_cti_end_critical_section(cti_signals_t *this)
{
	// sanity
	if (this == NULL)
	{
		return;
	}
	
	if (this->o_restore)
	{
		// reset old signal handlers
		sigaction(SIGQUIT,	SIGQUIT_SA(this),	NULL);
		sigaction(SIGILL,	SIGILL_SA(this),	NULL);
		sigaction(SIGABRT,	SIGABRT_SA(this),	NULL);
		sigaction(SIGFPE,	SIGFPE_SA(this),	NULL);
		sigaction(SIGSEGV,	SIGSEGV_SA(this),	NULL);
		sigaction(SIGTERM,	SIGTERM_SA(this),	NULL);
	
		// reset old mask
		sigprocmask(SIG_SETMASK, SIGMASK(this), NULL);
	}
	
	// cleanup
	free(this);
}

sigset_t *
_cti_block_signals(void)
{
	sigset_t *	this;
	sigset_t	mask;
	
	// We don't want our children to handle signals from the parent. We usually
	// will place the child into it's own process group to prevent that from
	// happening. But there is a race between the fork and seting the signal
	// handler in the child. So we need to block everything to avoid the problem.
	
	if ((this = malloc(sizeof(sigset_t))) == NULL)
	{
		// malloc failed
		return NULL;
	}
	memset(this, 0, sizeof(sigset_t));
	
	if (sigfillset(&mask))
	{
		// sigfillset failed
		free(this);
		return NULL;
	}
	
	if (sigprocmask(SIG_SETMASK, &mask, this))
	{
		// sigprocmask failed
		free(this);
		return NULL;
	}
	
	return this;
}

// Call either this function, or _cti_setpgid_restore, not both!
int
_cti_restore_signals(sigset_t *old)
{
	// sanity
	if (old == NULL)
		return 1;
		
	// restore the signal handler
	if (sigprocmask(SIG_SETMASK, old, NULL))
	{
		// sigprocmask failed
		free(old);
		return 1;
	}
	
	free(old);
	
	return 0;
}

int
_cti_setpgid_restore(pid_t child, sigset_t *old)
{
	// sanity
	if (old == NULL)
		return 1;
	
	// put the child in its own process group
	if (setpgid(child, child))
	{
		// setpgid failed
		free(old);
		return 1;
	}
	
	// restore the signal handler
	if (sigprocmask(SIG_SETMASK, old, NULL))
	{
		// sigprocmask failed
		free(old);
		return 1;
	}
	
	free(old);
	
	return 0;
}

static int
_cti_child_sig_common(void)
{
	struct sigaction 	sig_action;
	int 				i;
	
	// init the sig_action
	memset(&sig_action, 0, sizeof(sig_action));
	sig_action.sa_handler = SIG_DFL;
	sig_action.sa_flags = 0;
	sigemptyset(&sig_action.sa_mask);
	
	// Clear out all signal handlers from the parent so nothing weird can happen
	// in the child when it unblocks
	for (i=1; i < NSIG; ++i)
	{
		sigaction(i, &sig_action, NULL);
	}
	
	// Place this process in its own group to prevent signals being passed
	// to it from the controlling terminal. This is necessary in case the child 
	// code execs before the parent can put us into our own group.
	if (setpgid(0, 0))
	{
		// setpgid failed
		return 1;
	}
	
	return 0;
}

int
_cti_child_setpgid_restore(sigset_t *old)
{
	// sanity
	if (old == NULL)
		return 1;
		
	// call the common function
	if (_cti_child_sig_common())
	{
		// failure
		free(old);
		return 1;
	}
	
	// unmask the signals back to the old mask and free it
	if (sigprocmask(SIG_SETMASK, old, NULL))
	{
		// sigprocmask failed
		free(old);
		return 1;
	}
	
	// cleanup
	free(old);
	
	return 0;
}

int
_cti_child_setpgid_unblock_all(sigset_t *old)
{
	sigset_t	mask;
	
	// just free the old mask, we don't need it
	if (old != NULL)
	{
		free(old);
	}
	
	if (_cti_child_sig_common())
	{
		// failure
		return 1;
	}
	
	// fill the mask
	if (sigfillset(&mask))
	{
		// failure
		return 1;
	}
	
	// unmask all signals
	if (sigprocmask(SIG_UNBLOCK, &mask, NULL))
	{
		// failure
		return 1;
	}
	
	return 0;
}

