/******************************************************************************\
 * cti_args.h - Header file for the arg interface.
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

#ifndef _CTI_ARGS_H
#define _CTI_ARGS_H

#include	<stdarg.h>

/* struct typedefs */
typedef struct
{
	unsigned int	argc;
	char **			argv;
	unsigned int	_len;
} cti_args_t;

cti_args_t *	_cti_newArgs(void);
void			_cti_freeArgs(cti_args_t *);
int				_cti_addArg(cti_args_t *, const char *, ...);
int				_cti_mergeArgs(cti_args_t *, cti_args_t *);
char *			_cti_flattenArgs(cti_args_t *);

#endif /* _CTI_ARGS_H */
