/******************************************************************************\
 * cti_args.c - Functions relating to creating argv arrays.
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cti_args.h"

// Number of elements we should increment by for argv
#define ARGV_BLOCK_SIZE	16

// static prototypes
static int	_cti_resizeArgs(cti_args_t *);


cti_args_t *
_cti_newArgs(void)
{
	cti_args_t *	this;
	
	// allocate the args datatype
	if ((this = malloc(sizeof(cti_args_t))) == NULL)
	{
		// malloc failed
		return NULL;
	}
	
	// init the members
	this->argc = 0;
	if ((this->argv = malloc(ARGV_BLOCK_SIZE * sizeof(char *))) == NULL)
	{
		// malloc failed
		free(this);
		return NULL;
	}
	memset(this->argv, 0, ARGV_BLOCK_SIZE * sizeof(char *));
	this->_len = ARGV_BLOCK_SIZE;
	
	return this;
}

void
_cti_freeArgs(cti_args_t * this)
{
	unsigned int i;
	
	// sanity
	if (this == NULL)
		return;
		
	// free argv elems
	for (i=0; i < this->argc; ++i)
	{
		free(this->argv[i]);
	}
	
	// free argv
	free(this->argv);
	
	// free obj
	free(this);
}

static int
_cti_resizeArgs(cti_args_t *this)
{
	void *	new;

	// sanity
	if (this == NULL)
		return 1;
	
	if ((new = realloc(this->argv, (this->_len + ARGV_BLOCK_SIZE) * sizeof(char *))) == NULL)
	{
		// realloc failed
		return 1;
	}
	// update members
	this->argv = (char **)new;
	this->_len += ARGV_BLOCK_SIZE;
	// initialize new memory
	memset(&(this->argv[this->argc]), 0, (this->_len - this->argc) * sizeof(char *));
	
	return 0;
}

int
_cti_addArg(cti_args_t *this, const char *fmt, ...)
{
	va_list ap;
	char *	new_arg;
	
	// sanity
	if (this == NULL || fmt == NULL)
	{
		// failed to add arg
		return 1;
	}
	
	// setup the va_args
	va_start(ap, fmt);
	
	// create the argument string
	if (vasprintf(&new_arg, fmt, ap) <= 0)
	{
		// vasprintf failed
		return 1;
	}
	
	// finish the va_args
	va_end(ap);
	
	// Ensure that there is room for this argument
	if (this->argc == this->_len)
	{
		// need to resize
		if (_cti_resizeArgs(this))
		{
			// failed to resize
			return 1;
		}
	}
	
	// set the argument string
	this->argv[this->argc] = new_arg;
	this->argc += 1;
	
	return 0;
}

int
_cti_mergeArgs(cti_args_t *a1, cti_args_t *a2)
{
	int	s, i, j, len;

	// sanity
	if (a1 == NULL || a2 == NULL)
		return 1;
		
	// check if we are already done
	if (a2->argc == 0)
		return 0;
		
	// keep track of where we start adding args so that we can undo the
	// operation on error
	s = a1->argc;
	
	for (i=0; i < a2->argc; ++i)
	{
		if (_cti_addArg(a1, a2->argv[i]))
		{
			// undo the operation
			len = a1->argc;
			for (j=s; j < len; ++j)
			{
				free(a1->argv[j]);
				a1->argv[j] = NULL;
				a1->argc -= 1;
			}
			
			return 1;
		}
	}
	
	return 0;
}

char *
_cti_flattenArgs(cti_args_t *this)
{
	char *	rtn;
	int		len;
	int		i;
	char *	ptr;
	
	// sanity
	if (this == NULL || this->argc == 0)
		return NULL;
		
	// calculate the length of the result
	
	// start out with the first arg
	len = strlen(this->argv[0]);

	// now loop over each arg
	for (i=1; i < this->argc; ++i)
	{
		// add in 1 byte for the arg seperator ' '
		len += strlen(this->argv[i]) + 1;
	}
	
	// add in 1 byte for the null terminator
	len += 1;
	
	// allocate the rtn array
	if ((rtn = malloc(len * sizeof(char))) == NULL)
	{
		// malloc failed
		return NULL;
	}
	
	// copy first arg to the res
	ptr = rtn;
	memcpy(ptr, this->argv[0], strlen(this->argv[0]));
	ptr += strlen(this->argv[0]);
	
	// copy each arg to the res
	for (i=1; i < this->argc; ++i)
	{
		// set the arg seperator ' '
		*ptr++ = ' ';
		memcpy(ptr, this->argv[i], strlen(this->argv[i]));
		ptr += strlen(this->argv[i]);
	}
	
	// set the null terminator
	*ptr = '\0';
	
	return rtn;
}

