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

sigset_t *
_cti_block_signals(void)
{
	sigset_t *	rtn;
	sigset_t	mask;
	
	// We don't want our children to handle signals from the parent. We usually
	// will place the child into it's own process group to prevent that from
	// happening. But there is a race between the fork and seting the signal
	// handler in the child. So we need to block everything to avoid the problem.
	
	if ((rtn = malloc(sizeof(sigset_t))) == NULL)
	{
		// malloc failed
		return NULL;
	}
	memset(rtn, 0, sizeof(sigset_t));
	
	if (sigfillset(&mask))
	{
		// sigfillset failed
		free(rtn);
		return NULL;
	}
	
	if (sigprocmask(SIG_BLOCK, &mask, rtn))
	{
		// sigprocmask failed
		free(rtn);
		return NULL;
	}
	
	return rtn;
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
	for (i=0; i < NSIG; ++i)
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

