/******************************************************************************\
 * cti_list.c - Functions relating to creating and maintaining a linked list
 *
 * © 2014-2015 Cray Inc.  All Rights Reserved.
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

#include "cti_list.h"

cti_list_t *
_cti_newList(void)
{
	cti_list_t *	l;
	
	// allocate the list datatype
	if ((l = malloc(sizeof(cti_list_t))) == NULL)
	{
		// malloc failed
		return NULL;
	}
	
	// set the list members
	l->nelems = 0;
	l->head = NULL;
	l->scan = NULL;
	l->tail = NULL;
	
	return l;
}

void
_cti_consumeList(cti_list_t *l, void (*free_func)(void *))
{
	// Don't use l->scan here since the free_func() could mess with it.
	cti_list_e_t *	ptr;

	// sanity
	if (l == NULL)
		return;
	
	// force nelems to 0 so any calls to remove will fail
	l->nelems = 0;
	
	ptr = l->head;
	
	while (ptr != NULL)
	{
		// save next in head
		l->head = ptr->next;
		
		// call caller provided free function
		if (free_func != NULL)
		{
			free_func(ptr->this);
		}
		
		// free this entry
		free(ptr);
		
		// point at the saved next entry
		ptr = l->head;
	}
	
	// free the list
	free(l);
}

int
_cti_list_add(cti_list_t *l, void *elem)
{
	// sanity
	if (l == NULL || elem == NULL)
		return 1;
	
	// allocate entry
	if ((l->scan = malloc(sizeof(cti_list_e_t))) == NULL)
	{
		// malloc failed
		return 1;
	}
	
	// set elem in entry
	l->scan->this = elem;
	l->scan->next = NULL;
	
	if (l->tail == NULL)
	{
		// This is the first element in the list
		l->scan->prev = NULL;
		l->head = l->scan;
		l->tail = l->scan;
	} else
	{
		l->scan->prev = l->tail;
		l->tail->next = l->scan;
		l->tail = l->scan;
	}
	
	// increment nelems
	l->nelems++;
	
	return 0;
}

void
_cti_list_remove(cti_list_t *l, void *elem)
{
	// sanity
	if (l == NULL || elem == NULL || l->nelems == 0)
		return;

	// iterate over list if scan isn't pointing at elem
	if (l->scan == NULL || l->scan->this != elem)
	{
		// iterate over the list
		l->scan = l->head;
		while (l->scan != NULL)
		{
			if (l->scan->this == elem)
				break;
			l->scan = l->scan->next;
		}
		if (l->scan == NULL)
		{
			// not found
			return;
		}
	}
	
	// decrement the elements count
	l->nelems--;
	
	// is this the head
	if (l->scan->prev == NULL)
	{
		l->head = l->scan->next;
		if (l->head != NULL)
		{
			l->head->prev = NULL;
		}
	// is this the tail
	} else if (	l->scan->next == NULL)
	{
		l->tail = l->scan->prev;
		l->tail->next = NULL;
	// it is in the middle
	} else
	{
		l->scan->prev->next = l->scan->next;
		l->scan->next->prev = l->scan->prev;
	}
	
	// sanity - clear out any free'ed pointers
	if (l->nelems == 0)
	{
		l->head = NULL;
		l->tail = NULL;
	}
	
	// free the memory
	free(l->scan);
	
	// reset scan - this avoids future access of free'ed memory
	l->scan = NULL;
}

void
_cti_list_reset(cti_list_t *l)
{
	// sanity
	if (l == NULL)
		return;
		
	l->scan = l->head;
}

void *
_cti_list_next(cti_list_t *l)
{
	void *	rtn;
	
	// sanity
	if (l == NULL || l->nelems == 0)
		return NULL;
		
	if (l->scan == NULL)
		l->scan = l->head;
	
	rtn = l->scan->this;
	l->scan = l->scan->next;
	
	return rtn;
}

void *
_cti_list_pop(cti_list_t *l)
{
	void *	rtn;

	// sanity
	if (l == NULL || l->nelems == 0)
		return NULL;
	
	// point scan at the head
	l->scan = l->head;
	
	// point at the return obj
	rtn = l->scan->this;
	
	// decrement the elements count
	l->nelems--;
	
	// reset head to next
	l->head = l->scan->next;
	if (l->head != NULL)
	{
		l->head->prev = NULL;
	}
	
	// sanity - clear out any free'ed pointers
	if (l->nelems == 0)
	{
		l->head = NULL;
		l->tail = NULL;
	}
	
	// free the memory
	free(l->scan);
	
	// reset scan - this avoids future access of free'ed memory
	l->scan = NULL;
	
	return rtn;
}

int
_cti_list_len(cti_list_t *l)
{
	// sanity
	if (l == NULL)
		return 0;
	
	return l->nelems;
}

