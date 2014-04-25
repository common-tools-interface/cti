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
	char *	hostname;
	int		numPes;
} cti_host_t;

typedef struct
{
	int				numHosts;
	cti_host_t *	hosts;
} cti_hostsList_t;

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
int					_cti_alps_ship_package(void *, char *);
int					_cti_alps_start_daemon(void *, char *, int);

cti_app_id_t		cti_registerApid(uint64_t);
uint64_t			cti_getApid(pid_t);
char *				cti_getNodeCName(void);
int					cti_getNodeNid(void);
int					cti_getAppNid(cti_app_id_t);
int					cti_getNumAppPEs(cti_app_id_t);
int					cti_getNumAppNodes(cti_app_id_t);
char **	 			cti_getAppHostsList(cti_app_id_t);
cti_hostsList_t *	cti_getAppHostsPlacement(cti_app_id_t);
void				cti_destroyHostsList(cti_hostsList_t *);

#endif /* _ALPS_FE_H */
