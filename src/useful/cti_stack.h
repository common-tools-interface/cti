/******************************************************************************\
 * cti_stack.h - Header file for the stack interface.
 *
 * Copyright 2014 Cray Inc.  All Rights Reserved.
 *
 * Unpublished Proprietary Information.
 * This unpublished work is protected to trade secret, copyright and other laws.
 * Except as permitted by contract or express written permission of Cray Inc.,
 * no part of this work or its content may be used, reproduced or disclosed
 * in any form.
 *
 ******************************************************************************/

#ifndef _CTI_STACK_H
#define _CTI_STACK_H

#ifdef __cplusplus
extern "C" {
#endif

#define CTI_DEFAULT_STACK	128

/* struct typedefs */
typedef struct
{
	unsigned int	idx;
	unsigned int	num_elems;
	void **			elems;
} cti_stack_t;

/* function prototypes */
cti_stack_t *	_cti_newStack(void);
void			_cti_consumeStack(cti_stack_t *);
int				_cti_push(cti_stack_t *, void *);
void *			_cti_pop(cti_stack_t *);

#ifdef __cplusplus
}
#endif

#endif /* _CTI_STACK_H */
