/*********************************************************************************\
 * alps_run.h - A header file for the alps_run interface.
 *
 * © 2011-2014 Cray Inc.  All Rights Reserved.
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
 *********************************************************************************/

#ifndef _ALPS_RUN_H
#define _ALPS_RUN_H

#include <stdint.h>

#include "alps_defs.h"

/* public types */

typedef struct
{
	uint64_t	apid;
	pid_t		aprunPid;
} cti_aprunProc_t;

/* function prototypes */
void				_cti_reapAprunInv(uint64_t);
cti_aprunProc_t	*	cti_launchAprunBarrier(char **, int, int, int, int, char *, char *, char **);
int					cti_releaseAprunBarrier(uint64_t);
int					cti_killAprun(uint64_t, int);

#endif /* _ALPS_RUN_H */
