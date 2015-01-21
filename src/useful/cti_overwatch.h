/******************************************************************************\
 * cti_overwatch.h - Header file for the overwatch interface.
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

#ifndef _CTI_OVERWATCH_H
#define _CTI_OVERWATCH_H

/* struct typedefs */
typedef struct
{
	pid_t	o_pid;		// overwatch process pid	
	FILE *	pipe_r;		// my read stream
	FILE *	pipe_w;		// my write stream
} cti_overwatch_t;

cti_overwatch_t *	_cti_create_overwatch(const char *);
int					_cti_assign_overwatch(cti_overwatch_t *, pid_t);
void				_cti_exit_overwatch(cti_overwatch_t *);

#endif /* _CTI_OVERWATCH_H */

