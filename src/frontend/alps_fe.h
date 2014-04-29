/*********************************************************************************\
 * alps_fe.h - A header file for the alps specific frontend interface.
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
 *********************************************************************************/

#ifndef _ALPS_FE_H
#define _ALPS_FE_H

#include <stdint.h>
#include <sys/types.h>

#include "cti_fe.h"

typedef struct
{
	uint64_t	apid;
	pid_t		aprunPid;
} cti_aprunProc_t;

/* function prototypes */
int 				_cti_alps_init(void);
void				_cti_alps_fini(void);
int					_cti_alps_compJobId(void *, void *);
cti_app_id_t		_cti_alps_launchBarrier(char **, int, int, int, int, char *, char *, char **);
int					_cti_alps_releaseBarrier(void *);
int					_cti_alps_killApp(void *, int);
int					_cti_alps_ship_package(void *, char *);
int					_cti_alps_start_daemon(void *, char *, int);
int					_cti_alps_getNumAppPEs(void *);
int					_cti_alps_getNumAppNodes(void *);
char **				_cti_alps_getAppHostsList(void *);
cti_hostsList_t *	_cti_alps_getAppHostsPlacement(void *);
char *				_cti_alps_getHostName(void);
char *				_cti_alps_getLauncherHostName(void *);

cti_app_id_t		cti_registerApid(uint64_t);
uint64_t			cti_getApid(pid_t);

#endif /* _ALPS_FE_H */
