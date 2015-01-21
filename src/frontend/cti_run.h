/*********************************************************************************\
 * cti_run.h - A header file for the cti_run interface.
 *
 * Copyright 2011-2014 Cray Inc.  All Rights Reserved.
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

#ifndef _CTI_RUN_H
#define _CTI_RUN_H

#include <stdint.h>
#include <sys/types.h>

#include "cti_fe.h"

/* function prototypes */
cti_app_id_t		cti_launchApp(const char * const [], int, int, const char *, const char *, const char * const []);
cti_app_id_t		cti_launchAppBarrier(const char * const [], int, int, const char *, const char *, const char * const []);
int					cti_releaseAppBarrier(cti_app_id_t);
int					cti_killApp(cti_app_id_t, int);

#endif /* _CTI_RUN_H */
