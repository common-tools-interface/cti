/******************************************************************************\
 * cti_stack.c - Functions relating to creating and maintaining a dynamic stack
 *
 * Copyright 2014-2015 Cray Inc.  All Rights Reserved.
 *
 * Unpublished Proprietary Information.
 * This unpublished work is protected to trade secret, copyright and other laws.
 * Except as permitted by contract or express written permission of Cray Inc.,
 * no part of this work or its content may be used, reproduced or disclosed
 * in any form.
 *
 ******************************************************************************/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <stdlib.h>

#include "cti_stack.h"

/* Static prototypes */
static int	_cti_resizeStack(cti_stack_t *);

// stack implementation

static int
_cti_resizeStack(cti_stack_t *s)
{
	void **	new_elems;

	// sanity
	if (s == NULL || s->num_elems == 0 || s->elems == NULL)
		return 1;

	// double the size of the stack
	if ((new_elems = realloc(s->elems, 2 * s->num_elems * sizeof(void *))) == NULL)
	{
		// realloc failed
		return 1;
	}

	// set the new size
	s->num_elems *= 2;

	return 0;
}

cti_stack_t *
_cti_newStack(void)
{
	cti_stack_t *	s;

	// allocate the stack datatype
	if ((s = malloc(sizeof(cti_stack_t))) == NULL)
	{
		// malloc failed
		return NULL;
	}

	// set the stack control members
	s->idx = 0;
	s->num_elems = CTI_DEFAULT_STACK;

	// allocate the stack elements
	if ((s->elems = calloc(CTI_DEFAULT_STACK, sizeof(void *))) == NULL)
	{
		// calloc failed
		free(s);
		return NULL;
	}

	return s;
}

void
_cti_consumeStack(cti_stack_t *s)
{
	// sanity
	if (s == NULL)
		return;

	// free the stack elems
	if (s->elems)
		free(s->elems);

	// free the stack
	free(s);
}

int
_cti_push(cti_stack_t *s, void *elem)
{
	// sanity
	if (s == NULL || elem == NULL)
		return 0;

	// check if we need to grow the stack
	if (s->idx >= s->num_elems)
	{
		if (_cti_resizeStack(s))
		{
			// resize failed
			return 1;
		}
	}

	// push to the current idx, incrementing it afterwards
	s->elems[s->idx++] = elem;

	return 0;
}

void *
_cti_pop(cti_stack_t *s)
{
	// sanity
	if (s == NULL)
		return NULL;

	// ensure there are elements on the stack
	if (s->idx == 0)
		return NULL;

	// return the current stack element
	return s->elems[--(s->idx)];
}

