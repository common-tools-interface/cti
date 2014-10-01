/******************************************************************************\
 * stringList.c - Functions relating to creating and maintaining searchable lists
 *		of strings.
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

#include "stack.h"
#include "stringList.h"

/* Static prototypes */
static stringNode_t *	_cti_newStringNode(void);
static void 			_cti_consumeStringNode(stringNode_t *, void (*)(void *));
static stringEntry_t *	_cti_newStringEntry(void);
static stringNode_t *	_cti_lookupNode(stringList_t *, const char *);


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
	
	// free the string
	if (node->str)
	{
		free(node->str);
	}
	
	// maybe free the data
	if (node->data && free_func != NULL)
	{
		free_func(node->data);
	}
	
	// consume children
	for (i=0; i < CHAR_MAX; ++i)
	{
		if (node->next[i])
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
	
	// create the new root node
	if ((this->root = _cti_newStringNode()) == NULL)
	{
		// new node failed
		free(this);
		return NULL;
	}
	
	return this;
}

static stringNode_t *
_cti_lookupNode(stringList_t *lst, const char *str)
{
	stringNode_t	*cur_node;
	
	// sanity check
	// ensure lst is not null and it has a root node allocated.
	// ensure str is not null and it is not simply a null terminator.
	if (lst == NULL || lst->root == NULL || str == NULL || *str == '\0')
		return NULL;
		
	// set initial node
	cur_node = lst->root;
	
	// start walking the string
	while (*str != '\0')
	{
		// sanity - if the char value is negative there is an error with the input.
		// this trie tree only works with valid ascii characters
		if (*str < 0)
		{
			return NULL;
		}
		
		// point at proper entry in the next array based on this char
		// we increment the str pointer at this point after access
		cur_node = cur_node->next[(int)*str++];
		
		// check exit condition
		if (cur_node == NULL)
		{
			// the string is not present since there is no subtree for this char
			return NULL;
		}
	}
	
	// we are now pointing at the null terminator in the string, so return 
	// this node
	return cur_node;
}

int
_cti_searchStringList(stringList_t *lst, const char *str)
{
	stringNode_t *	node;
	
	// sanity check
	// ensure lst is not null and it has a root node allocated.
	// ensure str is not null and it is not simply a null terminator.
	if (lst == NULL || lst->root == NULL || str == NULL || *str == '\0')
		return 0;
	
	// try to find this node
	if ((node = _cti_lookupNode(lst, str)) == NULL)
	{
		// string not present
		return 0;
	}
	
	// return if this is a valid string
	return node->contains ? 1:0;
}

void *
_cti_lookupValue(stringList_t *lst, const char *str)
{
	stringNode_t *	node;
	
	// sanity check
	// ensure lst is not null and it has a root node allocated.
	// ensure str is not null and it is not simply a null terminator.
	if (lst == NULL || lst->root == NULL || str == NULL || *str == '\0')
		return NULL;
	
	// try to find this node
	if ((node = _cti_lookupNode(lst, str)) == NULL)
	{
		// string not present
		return NULL;
	}
	
	// return the stored data
	return node->data;
}

int
_cti_addString(stringList_t *lst, const char *str, void *data)
{
	stringNode_t *	cur_node;
	stringNode_t **	first_create = NULL;
	const char *	p = str;

	// sanity check
	// ensure lst is not null and it has a root node allocated.
	// ensure str is not null and it is not simply a null terminator.
	if (lst == NULL || lst->root == NULL || p == NULL || *p == '\0')
		return 1;
	
	// set initial node
	cur_node = lst->root;
	
	// start walking the string
	while (*p != '\0')
	{
		// sanity - if the char value is negative there is an error with the input.
		// this trie tree only works with valid ascii characters
		if (*p < 0)
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
		if (cur_node->next[(int)*p] == NULL)
		{
			// allocate a new node
			if ((cur_node->next[(int)*p] = _cti_newStringNode()) == NULL)
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
				first_create = &(cur_node->next[(int)*p]);
			}
		}
		
		// point at proper entry in the next array based on this char
		// we increment the pointer at this point after access
		cur_node = cur_node->next[(int)*p++];
	}
	
	// at this point *p is pointing at the null terminator, this is the end
	// of the string. We need to set the contains if we haven't already done so.
	if (!cur_node->contains)
	{
		// set the control var
		cur_node->contains = 1;
		// set the string value - this is not space efficient, but makes our job
		// easier later on when we retrieve the strings
		cur_node->str = strdup(str);
		// set the string data value if there is one
		cur_node->data = data;
	}
	
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
	// ensure lst is not null and it has a root node allocated
	if (lst == NULL || lst->root == NULL)
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
		if (cur_node->contains)
		{
			// ensure there is a str
			if (cur_node->str == NULL)
			{
				// no str, we have a bad list data structure
				goto _cti_getStrings_error;
			}
			
			// create an entry for this string
			if ((e_ptr = _cti_newStringEntry()) == NULL)
			{
				// something went wrong
				goto _cti_getStrings_error;
			}
			
			// set this entries value
			e_ptr->str = cur_node->str;
			e_ptr->data = cur_node->data;
			
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
		for (j=CHAR_MAX-1; j >= 0; j--)
		{
			if (cur_node->next[j])
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
	
	// all done, consume the stack and return the string list
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
	if (e == NULL)
		return;
			
	// free the child
	_cti_cleanupEntries(e->next);
	
	// free this entry
	free(e);
}

