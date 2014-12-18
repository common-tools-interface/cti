/******************************************************************************\
 * cti_stringList.c - Functions relating to creating and maintaining searchable
 *		              lists of strings.
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
 ******************************************************************************/
 
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */
 
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "cti_stack.h"
#include "cti_stringList.h"

#define NELEMS(x)		(sizeof(x) / sizeof(x[0]))
#define ISINVALID(c)	((c < ' ' || c > '~') ? 1:0)
#define OFFSET(c)		((c) - ' ')

/* Static prototypes */
static stringVal_t *	_cti_newStringVal(void);
static void				_cti_consumeStringVal(stringVal_t *, void (*)(void *));
static stringNode_t *	_cti_newStringNode(void);
static void 			_cti_consumeStringNode(stringNode_t *, void (*)(void *));
static stringEntry_t *	_cti_newStringEntry(void);

// support routines for vals

static stringVal_t *
_cti_newStringVal(void)
{
	stringVal_t *	val;
	
	// create the new val
	if ((val = malloc(sizeof(stringVal_t))) == NULL)
	{
		// malloc failed
		return NULL;
	}
	// set the val to NULL
	memset(val, 0, sizeof(stringVal_t));
	
	return val;
}

static void
_cti_consumeStringVal(stringVal_t *val, void (*free_func)(void *))
{
	if (val == NULL)
		return;
	
	if (val->key != NULL)
	{
		free(val->key);
	}	
	
	if (val->val != NULL && free_func != NULL)
	{
		free_func(val->val);
	}
	
	free(val);
}

// support routines for nodes

static stringNode_t *
_cti_newStringNode(void)
{
	stringNode_t *	node;
	
	// create the new node
	if ((node = malloc(sizeof(stringNode_t))) == NULL)
	{
		// malloc failed
		return NULL;
	}
	// set the node to NULL
	memset(node, 0, sizeof(stringNode_t));
	
	return node;
}

static void
_cti_consumeStringNode(stringNode_t *node, void (*free_func)(void *))
{
	int i;

	if (node == NULL)
		return;
	
	// maybe free the data
	if (node->data != NULL)
	{
		_cti_consumeStringVal(node->data, free_func);
	}
	
	// consume children
	for (i=0; i < NELEMS(node->next); ++i)
	{
		if (node->next[i] != NULL)
		{
			_cti_consumeStringNode(node->next[i], free_func);
		}
	}
	
	free(node);
}

static stringEntry_t *
_cti_newStringEntry(void)
{
	stringEntry_t *	e;
	
	// create the new entry
	if ((e = malloc(sizeof(stringEntry_t))) == NULL)
	{
		// malloc failed
		return NULL;
	}
	// set the entry to NULL
	memset(e, 0, sizeof(stringEntry_t));
	
	return e;
}

// The following functions are used to interact with a stringList_t

void
_cti_consumeStringList(stringList_t *lst, void (*free_func)(void *))
{
	// sanity check
	if (lst == NULL)
		return;
		
	// free the root node
	_cti_consumeStringNode(lst->root, free_func);
	
	// free the stringList_t object
	free(lst);
}

stringList_t *
_cti_newStringList(void)
{
	stringList_t *  this;
	
	// create the new stringList_t object
	if ((this = malloc(sizeof(stringList_t))) == NULL)
	{
		// malloc failed
		return NULL;
	}
	
	// set length to 0
	this->nstr = 0;
	
	// create the new root node
	if ((this->root = _cti_newStringNode()) == NULL)
	{
		// new node failed
		free(this);
		return NULL;
	}
	
	return this;
}

// returns number of strings in tree
int
_cti_lenStringList(stringList_t *lst)
{
	// sanity check
	if (lst == NULL)
		return 0;
		
	return lst->nstr;
}

void *
_cti_lookupValue(stringList_t *lst, const char *key)
{
	stringNode_t *	node;
	
	// sanity check
	// ensure lst is not null, it has a root node allocated, and there are entries.
	// ensure key is not null and it is not simply a null terminator.
	if (lst == NULL || lst->root == NULL || lst->nstr == 0 || key == NULL || *key == '\0')
		return NULL;
	
	// try to find this node
	// set initial node
	node = lst->root;
	
	// start walking the key
	while (*key != '\0')
	{
		// sanity - ensure the char is valid
		if (ISINVALID(*key))
		{
			return NULL;
		}
		
		// point at proper entry in the next array based on this char
		// we increment the key at this point after access
		node = node->next[(int)OFFSET(*key++)];
		
		// check exit condition
		if (node == NULL)
		{
			// the string is not present since there is no subtree for this char
			return NULL;
		}
	}
	
	// return the stored data
	return ((node->data == NULL) ? NULL : node->data->val);
}

int
_cti_addString(stringList_t *lst, const char *key, void *value)
{
	stringNode_t *	cur_node;
	stringNode_t **	first_create = NULL;
	const char *	p = key;

	// sanity check
	// ensure lst is not null and it has a root node allocated.
	// ensure key is not null and it is not simply a null terminator.
	// ensure da is not null
	if (lst == NULL || lst->root == NULL || p == NULL || *p == '\0' || value == NULL)
		return 1;
	
	// set initial node
	cur_node = lst->root;
	
	// start walking the string
	while (*p != '\0')
	{
		// sanity - ensure the key is valid
		if (ISINVALID(*p))
		{
			// Make sure we cleanup any nodes we created
			if (first_create != NULL)
			{
				_cti_consumeStringNode(*first_create, NULL);
				*first_create = NULL;
			}
			
			return 1;
		}
		
		// check if we need to allocate a new node for this char entry
		if (cur_node->next[(int)OFFSET(*p)] == NULL)
		{
			// allocate a new node
			if ((cur_node->next[(int)OFFSET(*p)] = _cti_newStringNode()) == NULL)
			{
				// error occured
				// Make sure we cleanup any nodes we created
				if (first_create != NULL)
				{
					_cti_consumeStringNode(*first_create, NULL);
					*first_create = NULL;
				}
				
				return 1;
			}
			
			// check if this is the first node we allocated
			if (first_create == NULL)
			{
				// it is the first, so set it to this new node
				first_create = &(cur_node->next[(int)OFFSET(*p)]);
			}
		}
		
		// point at proper entry in the next array based on this char
		// we increment the pointer at this point after access
		cur_node = cur_node->next[(int)OFFSET(*p++)];
	}
	
	// at this point *p is pointing at the null terminator, this is the end
	// of the string. We need to set the data value, or else return with error
	// since this value was already assigned.
	if (cur_node->data != NULL)
		return 1;
		
	// set the data value
	if ((cur_node->data = _cti_newStringVal()) == NULL)
		return 1;
		
	cur_node->data->key = strdup(key);
	cur_node->data->val = value;
	
	// increment length
	lst->nstr++;
	
	// all done, string has been inserted
	return 0;
}

stringEntry_t *
_cti_getEntries(stringList_t *lst)
{
	stringEntry_t *		rtn = NULL;
	stringEntry_t **	cur_entry = NULL;
	stringEntry_t *		e_ptr;
	cti_stack_t *		node_stack = NULL;
	stringNode_t *		cur_node;
	int					j;

	// sanity check
	// ensure lst is not null and it has a root node allocated and there are entries
	if (lst == NULL || lst->root == NULL || lst->nstr == 0)
		return NULL;
	
	// create a stack for the nodes
	if ((node_stack = _cti_newStack()) == NULL)
	{
		// stack create failed
		goto _cti_getStrings_error;
	}
	
	// set initial node
	cur_node = lst->root;
	
	// we want to do a preorder traversal to get the list of strings
	while (cur_node != NULL)
	{
		// visit this node
		if (cur_node->data != NULL)
		{
			// create an entry for this string
			if ((e_ptr = _cti_newStringEntry()) == NULL)
			{
				// something went wrong
				goto _cti_getStrings_error;
			}
			
			// set this entries value
			e_ptr->str = cur_node->data->key;
			e_ptr->data = cur_node->data->val;
			
			// put this entry into the return list
			if (rtn)
			{
				// put it at the current entry spot
				*cur_entry = e_ptr;
			} else
			{
				// this is the first entry in the list
				rtn = e_ptr;
			}
			
			// update cur_entry
			cur_entry = &e_ptr->next;
		}
		
		// push this nodes children in reverse order
		for (j = NELEMS(cur_node->next) - 1; j >= 0; j--)
		{
			if (cur_node->next[j] != NULL)
			{
				if (_cti_push(node_stack, cur_node->next[j]))
				{
					// push operation failed
					goto _cti_getStrings_error;
				}
			}
		}
		
		// pop a node off the stack
		cur_node = (stringNode_t *)_cti_pop(node_stack);
	}
	
	// all done, consume the stacks and return the string list
	_cti_consumeStack(node_stack);
	
	return rtn;
	
_cti_getStrings_error:

	if (rtn)
		_cti_cleanupEntries(rtn);
		
	if (node_stack)
		_cti_consumeStack(node_stack);
	
	return NULL;
}

void
_cti_cleanupEntries(stringEntry_t *e)
{
	stringEntry_t *	n;
	stringEntry_t * n2;
	
	if (e == NULL)
		return;
	
	// free all of the children entires
	n = e->next;
	while (n != NULL)
	{
		n2 = n->next;
		free(n);
		n = n2;
	}
	
	// free this entry
	free(e);
}

