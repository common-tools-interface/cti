/******************************************************************************\
 * cti_stringList.c - Functions relating to creating and maintaining searchable
 *		              lists of strings.
 *
 * This is now based on an adaptive radix tree derived from BSD sources located
 * at https://github.com/armon/libart
 *
 * © 2011-2015 Cray Inc.  All Rights Reserved.
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
#include <assert.h>

#include "cti_stack.h"
#include "cti_stringList.h"

/*
** Some explanation is in order. This code makes use of tagged pointers to distinguish
** a node from a leaf. Since both nodes and leafs are pointers to a data structure, on
** 64 bit architectures the address returned by malloc is word aligned. This means the 
** lower 3 bits are always going to be pinned to 0. We can take advantage of this and 
** use them for a tag. This is not portable and probably a bad idea, but it will save 
** time and space. We only make use of the lowest bit.
*/
#define IS_LEAF(x)	((uintptr_t)x & 1)
#define TAG_LEAF(x)	((void *)((uintptr_t)x | 1))
#define UNTAG_LEAF(x)	((void *)((uintptr_t)x & ~1))

// Types used here

typedef union {
	stringNode4_t 	*p1;
	stringNode16_t	*p2;
	stringNode48_t	*p3;
	stringNode256_t	*p4;
} stringNodePtr_t;

/* Static prototypes */
static stringLeaf_t *	_cti_newStringLeaf(const unsigned char *, int, void *);
static int				_cti_compare_leaf(stringLeaf_t *, const unsigned char *, int);
static stringNode_t *	_cti_newStringNode(uint8_t);
static void				_cti_destroyStringNode(stringNode_t *, void (*)(void *));
static stringLeaf_t *	_cti_minimumLeaf(stringNode_t *);
static stringNode_t **	_cti_findEdge(stringNode_t *, unsigned char);
static int				_cti_labelDiffIndex(stringNode_t *, const unsigned char *, int, int);
static int				_cti_labelNumShared(stringNode_t *, const unsigned char *, int, int);
static void				_cti_copyStringNode(stringNode_t *, stringNode_t *);
static void				_cti_addEdge256(stringNode256_t *, unsigned char, stringNode_t *);
static void				_cti_addEdge48(stringNode48_t *, stringNode_t **, unsigned char, stringNode_t *);
static void				_cti_addEdge16(stringNode16_t *, stringNode_t **, unsigned char, stringNode_t *);
static void				_cti_addEdge4(stringNode4_t *, stringNode_t **, unsigned char, stringNode_t *);
static int				_cti_insert_key(stringNode_t *, stringNode_t **, const unsigned char *, int, void *, int);
static void				_cti_delEdge256(stringNode256_t *, stringNode_t **, unsigned char);
static void				_cti_delEdge48(stringNode48_t *, stringNode_t **, unsigned char);
static void				_cti_delEdge16(stringNode16_t *, stringNode_t **, stringNode_t **);
static void				_cti_delEdge4(stringNode4_t *, stringNode_t **, stringNode_t **);
static stringLeaf_t *	_cti_remove_key(stringNode_t *, stringNode_t **, const unsigned char *, int, int);
static int				_cti_forEach_key(stringNode_t *, cti_stringCallback, void *);

// macro like functions

static inline int
min(int a, int b)
{
	return (a < b) ? a : b;
}

/*****************************
** support routines for leafs
*****************************/

static stringLeaf_t *
_cti_newStringLeaf(const unsigned char *key, int key_len, void *val)
{
	stringLeaf_t *	leaf;
	
	// sanity
	if (key == NULL || val == NULL)
		return NULL;
	
	// create the new leaf - add one to force null terminator in key only.
	if ((leaf = malloc(sizeof(stringLeaf_t) + key_len + 1)) == NULL)
	{
		// malloc failed
		return NULL;
	}
	
	// set leaf members
	leaf->val = val;
	leaf->key_len = key_len;
	memcpy(leaf->key, key, key_len);
	
	// set null terminator. Note that we don't include this in key_len, and is
	// only useful when calling the caller provided callback when iterating over
	// the tree.
	leaf->key[key_len] = '\0';
	
	return leaf;
}

static int
_cti_compare_leaf(stringLeaf_t *leaf, const unsigned char *key, int key_len)
{
	// Fail if len mismatch
	if (leaf->key_len != (uint32_t)key_len)
	{
		return 1;
	}
	
	// compare the keys
	return memcmp(leaf->key, key, key_len);
}

/*****************************
** support routines for nodes
*****************************/

static stringNode_t *
_cti_newStringNode(uint8_t type)
{
	stringNode_t *	node = NULL;
	
	// The following works because each node subtype always begins with a stringNode_t.
	switch (type)
	{
		case NODE4:
			if ((node = malloc(sizeof(stringNode4_t))) == NULL)
			{
				// malloc failed
				break;
			}
			// init to NULL
			memset(node, 0, sizeof(stringNode4_t));
			break;
		
		case NODE16:
			if ((node = malloc(sizeof(stringNode16_t))) == NULL)
			{
				// malloc failed
				break;
			}
			// init to NULL
			memset(node, 0, sizeof(stringNode16_t));
			break;
			
		case NODE48:
			if ((node = malloc(sizeof(stringNode48_t))) == NULL)
			{
				// malloc failed
				break;
			}
			// init to NULL
			memset(node, 0, sizeof(stringNode48_t));
			break;
		
		case NODE256:
			if ((node = malloc(sizeof(stringNode256_t))) == NULL)
			{
				// malloc failed
				break;
			}
			// init to NULL
			memset(node, 0, sizeof(stringNode256_t));
			break;
			
		default:
			abort();
	}
	
	if (node != NULL)
	{
		// set type
		node->type = type;
	}
	return node;
}

// recursively destroys a node and all of its children
static void
_cti_destroyStringNode(stringNode_t *node, void (*free_func)(void *))
{
	stringNodePtr_t p;
	int 			i;

	// sanity
	if (node == NULL)
		return;
		
	// check if this is a leaf, if so we are done
	if (IS_LEAF(node))
	{
		stringLeaf_t *leaf = UNTAG_LEAF(node);
		
		if (free_func != NULL)
		{
			free_func(leaf->val);
		}
		free(leaf);
		return;
	}
	
	switch(node->type)
	{
		case NODE4:
			p.p1 = (stringNode4_t *)node;
			for (i=0; i < node->num_edges; ++i)
			{
				_cti_destroyStringNode(p.p1->edges[i], free_func);
			}
			break;
			
		case NODE16:
			p.p2 = (stringNode16_t *)node;
			for (i=0; i < node->num_edges; ++i)
			{
				_cti_destroyStringNode(p.p2->edges[i], free_func);
			}
			break;
			
		case NODE48:
			p.p3 = (stringNode48_t *)node;
			for (i=0; i < node->num_edges; ++i)
			{
				_cti_destroyStringNode(p.p3->edges[i], free_func);
			}
			break;
			
		case NODE256:
			p.p4 = (stringNode256_t *)node;
			for (i=0; i < 256; ++i)
			{
				if (p.p4->edges[i] != NULL)
				{
					_cti_destroyStringNode(p.p4->edges[i], free_func);
				}
			}
			break;
		
		default:
			abort();
	}
	
	// free this node since the children have been freed
	free(node);
}

static stringLeaf_t *
_cti_minimumLeaf(stringNode_t *node)
{
	int idx;

	if (node == NULL)	return NULL;
	if (IS_LEAF(node))	return UNTAG_LEAF(node);
	
	switch (node->type)
	{
		case NODE4:
			return _cti_minimumLeaf(((stringNode4_t *)node)->edges[0]);
		case NODE16:
			return _cti_minimumLeaf(((stringNode16_t *)node)->edges[0]);
		case NODE48:
			idx = 0;
			while(((stringNode48_t *)node)->keys[idx] == 0) ++idx;
			idx = ((stringNode48_t *)node)->keys[idx] - 1;
			return _cti_minimumLeaf(((stringNode48_t *)node)->edges[idx]);
		case NODE256:
			idx=0;
			while(((stringNode256_t *)node)->edges[idx] == 0)	++idx;
			return _cti_minimumLeaf(((stringNode256_t *)node)->edges[idx]);
		default:
			abort();
	}
}

static stringNode_t **
_cti_findEdge(stringNode_t *node, unsigned char c)
{
	int i;
	stringNodePtr_t	p;
	
	switch (node->type)
	{
		// simple sequential search
		case NODE4:
			p.p1 = (stringNode4_t *)node;
			for (i=0; i < node->num_edges; ++i)
			{
				if (p.p1->keys[i] == c)
					return &p.p1->edges[i];
			}
			break;
			
		// simple sequential search - could make better by ensuring this gets vectorized
		case NODE16:
			p.p2 = (stringNode16_t *)node;
			for (i=0; i < node->num_edges; ++i)
			{
				if (p.p2->keys[i] == c)
					return &p.p2->edges[i];
			}
			break;
			
		// constant lookup
		case NODE48:
			p.p3 = (stringNode48_t *)node;
			i = p.p3->keys[c];
			if (i != 0)
				return &p.p3->edges[i-1];
			break;
			
		// constant lookup
		case NODE256:
			p.p4 = (stringNode256_t *)node;
			if (p.p4->edges[c] != NULL)
				return &p.p4->edges[c];
			break;
			
		default:
			break;
	}
	
	// if we get here, not found
	return NULL;
}

static int
_cti_labelDiffIndex(stringNode_t *node, const unsigned char *key, int key_len, int idx)
{
	int max_cmp, i;
	
	// avoid following leafs by checking the label
	max_cmp = min(min(MAX_LABEL_LEN, node->label_len), key_len - idx);
	for (i=0; i < max_cmp; ++i)
	{
		if (node->label[i] != key[i+idx])
			return i;
	}
	
	// Check if label is longer than we checked
	if (node->label_len > MAX_LABEL_LEN)
	{
		// Find any leaf. This works because every leaf here contains the
		// missing label_len in its stored key.
		stringLeaf_t *leaf = _cti_minimumLeaf(node);
		max_cmp = min(leaf->key_len, key_len) - idx;
		for(; i < max_cmp; ++i)
		{
			if (leaf->key[i+idx] != key[i+idx])
				return i;
		}
	}
	
	return i;
}

// The difference between this function and _cti_labelDiffIndex is that this one doesn't follow
// into leaf nodes to find the actual index of divergence. It only looks at the label.
static int
_cti_labelNumShared(stringNode_t *node, const unsigned char *key, int key_len, int idx)
{
	int max_cmp, i;
	
	// calculate the max char to compare to
	max_cmp = min(min(node->label_len, MAX_LABEL_LEN), key_len - idx);
	for (i=0; i < max_cmp; ++i)
	{
		if (node->label[i] != key[i+idx])
			return i;
	}
	
	return i;
}

static void
_cti_copyStringNode(stringNode_t *dest, stringNode_t *src)
{
	dest->num_edges = src->num_edges;
	dest->label_len = src->label_len;
	memcpy(dest->label, src->label, min(MAX_LABEL_LEN, src->label_len));
}

/********************************************
** support routines for adding leafs/nodes
********************************************/

static void
_cti_addEdge256(stringNode256_t *node, unsigned char c, stringNode_t *edge)
{
	node->node.num_edges++;
	node->edges[c] = edge;
}

static void
_cti_addEdge48(stringNode48_t *node, stringNode_t **ref, unsigned char c, stringNode_t *edge)
{
	if (node->node.num_edges < 48)
	{
		int idx = 0;
		
		// find next unused edge
		while (node->edges[idx] != NULL)	++idx;
		
		node->node.num_edges++;
		node->keys[c] = idx + 1;	// offset by 1 to use 0 as the "null" check.
		node->edges[idx] = edge;
	} else
	{
		// grow the node to the full sized variant
		stringNode256_t *new = (stringNode256_t *)_cti_newStringNode(NODE256);
		if (new == NULL)
		{
			abort();
		}
		// copy only the edges since the full size doesn't use a key array
		for (int i = 0; i<256; ++i)
		{
			if (node->keys[i] != 0)
			{
				new->edges[i] = node->edges[node->keys[i] - 1];
			}
		}
		_cti_copyStringNode((stringNode_t *)new, (stringNode_t *)node);
		*ref = (stringNode_t *)new;
		free(node);
		_cti_addEdge256(new, c, edge);
	}
}

// This function is nearly identical to _cti_addEdge4. Could be made better by 
// ensuring this gets vectorized.
static void
_cti_addEdge16(stringNode16_t *node, stringNode_t **ref, unsigned char c, stringNode_t *edge)
{
	if (node->node.num_edges < 16)
	{
		int i;
		for (i=0; i < node->node.num_edges; ++i)
		{
			if (c < node->keys[i])
				break;
		}
		
		// shift to make room
		memmove(node->keys+i+1, node->keys+i, node->node.num_edges - i);
		memmove(node->edges+i+1, node->edges+i, (node->node.num_edges - i) * sizeof(void *));
		
		// insert new edge
		node->keys[i] = c;
		node->edges[i] = edge;
		node->node.num_edges++;
	} else
	{
		// Need to resize the node
		stringNode48_t *new = (stringNode48_t *)_cti_newStringNode(NODE48);
		if (new == NULL)
		{
			abort();
		}
		// copy the key vector and edges
		memcpy(new->keys, node->keys, node->node.num_edges * sizeof(unsigned char));
		memcpy(new->edges, node->edges, node->node.num_edges * sizeof(void *));
		// copy the node
		_cti_copyStringNode((stringNode_t *)new, (stringNode_t *)node);
		*ref = (stringNode_t *)new;
		free(node);
		_cti_addEdge48(new, ref, c, edge);
	}
}

static void
_cti_addEdge4(stringNode4_t *node, stringNode_t **ref, unsigned char c, stringNode_t *edge)
{
	if (node->node.num_edges < 4)
	{
		int i;
		for (i=0; i < node->node.num_edges; ++i)
		{
			if (c < node->keys[i])
				break;
		}
		
		// shift to make room - always going to have more than one edge
		memmove(node->keys+i+1, node->keys+i, node->node.num_edges - i);
		memmove(node->edges+i+1, node->edges+i, (node->node.num_edges - i) * sizeof(void *));
		
		// insert new edge
		node->keys[i] = c;
		node->edges[i] = edge;
		node->node.num_edges++;
	} else
	{
		// Need to resize the node
		stringNode16_t *new = (stringNode16_t *)_cti_newStringNode(NODE16);
		if (new == NULL)
		{
			abort();
		}
		// copy the key vector and edges
		memcpy(new->keys, node->keys, node->node.num_edges * sizeof(unsigned char));
		memcpy(new->edges, node->edges, node->node.num_edges * sizeof(void *));
		// copy the node
		_cti_copyStringNode((stringNode_t *)new, (stringNode_t *)node);
		*ref = (stringNode_t *)new;
		free(node);
		_cti_addEdge16(new, ref, c, edge);
	}
}

static int
_cti_insert_key(stringNode_t *node, stringNode_t **ref, const unsigned char *key, int key_len, void *val, int idx)
{
	stringLeaf_t *	leaf;
	stringLeaf_t *	l2;
	stringNode4_t *	new;
	stringNode_t **	edge;
	
	// sanity
	if (ref == NULL || key == NULL || val == NULL)
		return 1;

	// If node is null, create a new leaf
	if (node == NULL)
	{
		if ((leaf = _cti_newStringLeaf(key, key_len, val)) == NULL)
		{
			return 1;
		}
		
		*ref = (stringNode_t *)TAG_LEAF(leaf);
		
		return 0;
	}

	// If node is a leaf, we need to turn it into a node
	if (IS_LEAF(node))
	{
		int max_cmp, prefix_len;
	
		leaf = UNTAG_LEAF(node);
		
		// check if this key is already present, if so return error.
		if (_cti_compare_leaf(leaf, key, key_len) == 0)
		{
			return 1;
		}
		
		// New key, need to split the leaf into a new stringNode4_t
		if ((new = (stringNode4_t *)_cti_newStringNode(NODE4)) == NULL)
		{
			return 1;
		}
		
		// Create the new leaf
		if ((l2 = _cti_newStringLeaf(key, key_len, val)) == NULL)
		{
			free(new);
			return 1;
		}
		
		// Determine number of label chars that match
	
		// only want to compare to the smallest key length minus idx, otherwise we run off the end.
		max_cmp = min(leaf->key_len, l2->key_len) - idx;
		
		// iterate over each key value
		for (prefix_len=0; prefix_len < max_cmp; ++prefix_len)
		{
			if (leaf->key[prefix_len+idx] != l2->key[prefix_len+idx])
				break;
		}
		
		new->node.label_len = prefix_len;
		// only copy the max label len for now, we check this later if we need to update
		memcpy(new->node.label, key+idx, min(MAX_LABEL_LEN, prefix_len));
		
		// Add the leafs to the new stringNode4_t
		*ref = (stringNode_t *)new;
		_cti_addEdge4(new, ref, leaf->key[idx+prefix_len], TAG_LEAF(leaf));
		_cti_addEdge4(new, ref, l2->key[idx+prefix_len], TAG_LEAF(l2));
		
		return 0;
	}
	
	// check if node has a label
	if (node->label_len)
	{
		// determine the index where the labels diverge
		int label_idx = _cti_labelDiffIndex(node, key, key_len, idx);
		if ((uint32_t)label_idx >= node->label_len)
		{
			// labels match, so recurse the search
			idx += node->label_len;
			goto RECURSE_SEARCH;
		}
		
		// Create a new node
		if ((new = (stringNode4_t *)_cti_newStringNode(NODE4)) == NULL)
		{
			return 1;
		}
		new->node.label_len = label_idx;
		memcpy(new->node.label, node->label, min(MAX_LABEL_LEN, label_idx));
		
		*ref = (stringNode_t *)new;
		
		// adjust label of old node
		if (node->label_len <= MAX_LABEL_LEN)
		{
			_cti_addEdge4(new, ref, node->label[label_idx], node);
			node->label_len -= label_idx + 1;
			memmove(node->label, node->label + label_idx + 1, min(MAX_LABEL_LEN, node->label_len));
		} else
		{
			node->label_len -= label_idx + 1;
			leaf = _cti_minimumLeaf(node);
			_cti_addEdge4(new, ref, leaf->key[label_idx+idx], node);
			memcpy(node->label, leaf->key+label_idx+idx+1, min(MAX_LABEL_LEN, node->label_len));
		}
		
		// Insert the new leaf
		if ((leaf = _cti_newStringLeaf(key, key_len, val)) == NULL)
		{
			return 1;
		}
		_cti_addEdge4(new, ref, key[label_idx+idx], TAG_LEAF(leaf));
		
		return 0;
	}
	
RECURSE_SEARCH:

	// Find an edge to recurse to
	edge = _cti_findEdge(node, key[idx]);
	if (edge != NULL)
	{
		return _cti_insert_key(*edge, edge, key, key_len, val, idx+1);
	}
	
	// No edge, so we insert new leaf into this node
	if ((leaf = _cti_newStringLeaf(key, key_len, val)) == NULL)
	{
		return 1;
	}
	
	switch (node->type)
	{
		case NODE4:
			_cti_addEdge4((stringNode4_t *)node, ref, key[idx], TAG_LEAF(leaf));
			break;
		case NODE16:
			_cti_addEdge16((stringNode16_t *)node, ref, key[idx], TAG_LEAF(leaf));
			break;
		case NODE48:
			_cti_addEdge48((stringNode48_t *)node, ref, key[idx], TAG_LEAF(leaf));
			break;
		case NODE256:
			_cti_addEdge256((stringNode256_t *)node, key[idx], TAG_LEAF(leaf));
			break;
		default:
			abort();
	}
	
	return 0;
}

/**********************************************
** support routines for removing leafs/nodes
**********************************************/

static void
_cti_delEdge256(stringNode256_t *node, stringNode_t **ref, unsigned char c)
{
	node->edges[c] = NULL;
	node->node.num_edges--;
	
	// If we drop to 37, we need to turn this into a NODE48
	// This is choosen to prevent thrashing
	if (node->node.num_edges == 37)
	{
		stringNode48_t *new;
		unsigned char	i, e_i;
		
		new = (stringNode48_t *)_cti_newStringNode(NODE48);
		if (new == NULL)
		{
			abort();
		}
		
		// copy the node
		_cti_copyStringNode((stringNode_t *)new, (stringNode_t *)node);
		
		// copy the keys and edges
		e_i = 0;
		for (i=0; i < 256; ++i)
		{
			if (node->edges[i] != NULL)
			{
				new->edges[e_i] = node->edges[i];
				new->keys[i] = e_i + 1;
				++e_i;
			}
		}
		free(node);
		*ref = (stringNode_t *)new;	
	}
}

static void
_cti_delEdge48(stringNode48_t *node, stringNode_t **ref, unsigned char c)
{
	unsigned char idx = node->keys[c];
	
	node->keys[c] = 0;
	node->edges[idx] = NULL;
	node->node.num_edges--;
	
	// if we drop to 12, we need to turn this into a NODE16
	// This is choosen to prevent thrashing
	if (node->node.num_edges == 12)
	{
		stringNode16_t *new;
		unsigned char	i, e_i;
	
		new = (stringNode16_t *)_cti_newStringNode(NODE16);
		if (new == NULL)
		{
			abort();
		}
		// copy the node
		_cti_copyStringNode((stringNode_t *)new, (stringNode_t *)node);
	
		// copy the keys and edges
		e_i = 0;
		for (i=0; i < 256; ++i)
		{
			idx = node->keys[i];
			if (idx)
			{
				new->keys[e_i] = i;
				new->edges[e_i] = node->edges[idx - 1];
				++e_i;
			}
		}
		free(node);
		*ref = (stringNode_t *)new;	
	}
}

static void
_cti_delEdge16(stringNode16_t *node, stringNode_t **ref, stringNode_t **leaf)
{
	int offset;
	
	offset = leaf - node->edges;
	memmove(node->keys+offset, node->keys+offset+1, node->node.num_edges - 1 - offset);
	memmove(node->edges+offset, node->edges+offset+1, (node->node.num_edges - 1 - offset) * sizeof(void *));
	node->node.num_edges--;
	
	// if we drop below 4, we need to turn this into a NODE4
	if (node->node.num_edges == 3)
	{
		stringNode4_t *new = (stringNode4_t *)_cti_newStringNode(NODE4);
		if (new == NULL)
		{
			abort();
		}
		// copy the node
		_cti_copyStringNode((stringNode_t *)new, (stringNode_t *)node);
		// copy the key vector and edges
		memcpy(new->keys, node->keys, 4);
		memcpy(new->edges, node->edges, 4 * sizeof(void *));
		free(node);
		*ref = (stringNode_t *)new;
	}
}

static void
_cti_delEdge4(stringNode4_t *node, stringNode_t **ref, stringNode_t **leaf)
{
	int 			offset, label_len, sub_label;
	stringNode_t *	edge;

	// Calculate the offset to the start of the edge to remove. The leaf is always
	// going to be part of this node.
	offset = leaf - node->edges;
	memmove(node->keys+offset, node->keys+offset+1, node->node.num_edges - 1 - offset);
	memmove(node->edges+offset, node->edges+offset+1, (node->node.num_edges - 1 - offset) * sizeof(void *));
	node->node.num_edges--;
	
	// Remove this node if it only has a single edge left
	if (node->node.num_edges)
	{
		edge = node->edges[0];
		// Check if this edge is a leaf, if it isn't then we need to concatenate
		// the labels.
		if (!IS_LEAF(edge))
		{
			label_len = node->node.label_len;
			if (label_len < MAX_LABEL_LEN)
			{
				node->node.label[label_len++] = node->keys[0];
			}
			// keep copying
			if (label_len < MAX_LABEL_LEN)
			{
				sub_label = min(edge->label_len, MAX_LABEL_LEN - label_len);
				memcpy(node->node.label+label_len, edge->label, sub_label);
				label_len += sub_label;
			}
			
			// copy the label to the edge
			memcpy(edge->label, node->node.label, min(label_len, MAX_LABEL_LEN));
			edge->label_len += node->node.label_len + 1;
		}
		*ref = edge;
		free(node);
	}
}

static stringLeaf_t *
_cti_remove_key(stringNode_t *node, stringNode_t **ref, const unsigned char *key, int key_len, int idx)
{
	stringLeaf_t *	leaf;
	stringNode_t **	edge;
	int 			prefix_len;

	// terminator
	if (node == NULL)
		return NULL;
		
	// Check if this is a leaf node
	if (IS_LEAF(node))
	{
		leaf = UNTAG_LEAF(node);
		// check if the key matches this leaf, otherwise the key is not present
		if (_cti_compare_leaf(leaf, key, key_len))
		{
			return NULL;
		}
		// remove the leaf and return
		*ref = NULL;
		return leaf;
	}
	
	// Check if the label matches
	if (node->label_len != 0)
	{
		prefix_len = _cti_labelNumShared(node, key, key_len, idx);
		if (prefix_len != min(MAX_LABEL_LEN, node->label_len))
		{
			// key not present since needle differs from label
			return NULL;
		}
		// just assume the label matches. We check the final key in the leaf
		// anyways to ensure things match.
		idx += node->label_len;
	}
	
	// Find the proper edge
	edge = _cti_findEdge(node, key[idx]);
	if (edge == NULL)
	{
		// key not present
		return NULL;
	}
	
	// If the edge is a leaf, delete it from this node
	if (IS_LEAF(*edge))
	{
		leaf = UNTAG_LEAF(*edge);
	
		// ensure this leaf matches the key
		if (_cti_compare_leaf(leaf, key, key_len))
		{
			// key doesn't match leaf, so it is not present
			return NULL;
		}
		
		switch (node->type)
		{
			case NODE4:
				_cti_delEdge4((stringNode4_t *)node, ref, edge);
				break;
			case NODE16:
				_cti_delEdge16((stringNode16_t *)node, ref, edge);
				break;
			case NODE48:
				_cti_delEdge48((stringNode48_t *)node, ref, key[idx]);
				break;
			case NODE256:
				_cti_delEdge256((stringNode256_t *)node, ref, key[idx]);
				break;
			default:
				abort();
		}
		
		return leaf;
		
	} else
	{
		return _cti_remove_key(*edge, edge, key, key_len, idx+1);
	}
}

/***********************************************
** support routines for iterating over the tree
***********************************************/

static int
_cti_forEach_key(stringNode_t *node, cti_stringCallback cb, void *data)
{
	stringLeaf_t *	leaf;
	int				i,idx, rtn;

	// sanity
	if (node == NULL)
		return 0;
		
	// Check if this is a leaf node
	if (IS_LEAF(node))
	{
		leaf = UNTAG_LEAF(node);
		return cb(data, (const char *)leaf->key, leaf->val);
	}
	
	switch (node->type)
	{
		case NODE4:
			for (i=0; i < node->num_edges; ++i)
			{
				// User can force us to bail if callback returns non-zero
				rtn = _cti_forEach_key(((stringNode4_t *)node)->edges[i], cb, data);
				if (rtn) return rtn;
			}
			break;
		
		case NODE16:
			for (i=0; i < node->num_edges; ++i)
			{
				rtn = _cti_forEach_key(((stringNode16_t *)node)->edges[i], cb, data);
				if (rtn) return rtn;
			}
			break;
			
		case NODE48:
			for (i=0; i < 256; ++i)
			{
				idx = ((stringNode48_t *)node)->keys[i];
				if (idx == 0)	continue;
				
				rtn = _cti_forEach_key(((stringNode48_t *)node)->edges[idx-1], cb, data);
				if (rtn) return rtn;
			}
			break;
			
		case NODE256:
			for (i=0; i < 256; ++i)
			{
				if (((stringNode256_t *)node)->edges[i] == NULL)	continue;
				
				rtn = _cti_forEach_key(((stringNode256_t *)node)->edges[i], cb, data);
				if (rtn) return rtn;
			}
			break;
			
		default:
			abort();
	}
	
	return 0;
}

/*****************************
** API functions start below
*****************************/

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
	
	// set to null
	memset(this, 0, sizeof(stringList_t));
	
	return this;
}

void
_cti_consumeStringList(stringList_t *lst, void (*free_func)(void *))
{
	// sanity
	if (lst == NULL)
		return;
	
	// destroy the root, this recursively destroys the tree
	_cti_destroyStringNode(lst->root, free_func);
	
	// destroy the tree
	free(lst);
}

int
_cti_addString(stringList_t *lst, const char *key, void *value)
{
	int rtn;

	// sanity check
	// ensure lst is not null.
	// ensure key is not null and it is not simply a null terminator.
	// ensure value is not null
	if (lst == NULL || key == NULL || *key == '\0' || value == NULL)
		return 1;
	
	rtn = _cti_insert_key(lst->root, &lst->root, (const unsigned char *)key, strlen(key), value, 0);
	
	if (!rtn)
	{
		lst->nstr++;
	}
	
	return rtn;
}

void
_cti_removeString(stringList_t *lst, const char *key, void (*free_func)(void *))
{
	stringLeaf_t *	leaf;
	
	// sanity check
	// ensure lst is not null.
	// ensure key is not null and it is not simply a null terminator.
	if (lst == NULL || key == NULL || *key == '\0')
		return;
	
	leaf = _cti_remove_key(lst->root, &lst->root, (const unsigned char *)key, strlen(key), 0);
	
	if (leaf != NULL)
	{
		// call user provided free_func
		if (free_func != NULL)
		{
			free_func(leaf->val);
		}
	
		free(leaf);
		
		lst->nstr--;
	}
}

void *
_cti_lookupValue(stringList_t *lst, const char *key)
{
	stringNode_t *	node;
	stringNode_t **	edge;
	stringLeaf_t *	leaf;
	int				prefix_len, key_len, idx;
	
	// sanity check
	// ensure lst is not null and has a root node allocated.
	// ensure key is not null and it is not simply a null terminator.
	if (lst == NULL || lst->root == NULL || key == NULL || *key == '\0')
		return NULL;
	
	node = lst->root;
	idx = 0;
	key_len = strlen(key);
	
	while (node != NULL)
	{
		// Check if this is a leaf
		if (IS_LEAF(node))
		{
			leaf = UNTAG_LEAF(node);
			
			// check if key matches leaf, if not return NULL
			if (_cti_compare_leaf(leaf, (const unsigned char *)key, key_len))
			{
				return NULL;
			}
			
			return leaf->val;
		}
		
		// Ensure the key matches the label
		if (node->label_len != 0)
		{
			prefix_len = _cti_labelNumShared(node, (const unsigned char *)key, key_len, idx);
			if (prefix_len != min(MAX_LABEL_LEN, node->label_len))
			{
				// key prefix doesn't match label
				return NULL;
			}
			// assume it matches, we check in the child that the key matches anyways
			idx += node->label_len;
		}
		
		// Find an edge to recurse to
		edge = _cti_findEdge(node, key[idx]);
		node = (edge != 0) ? *edge : NULL;
	}
	
	// Key is not in tree
	return NULL;
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

int
_cti_forEachString(stringList_t *lst, cti_stringCallback cb, void *data)
{
	// sanity check
	// ensure lst is not null and has a root node allocated.
	// ensure that a callback was provided.
	if (lst == NULL || lst->root == NULL || cb == NULL)
		return 0;
		
	return _cti_forEach_key(lst->root, cb, data);
}

