/******************************************************************************\
 * cti_stringList.h - Header file for the stringList interface. This implements
 *                    a trie tree.
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
 
#ifndef _CTI_STRINGLIST_H
#define _CTI_STRINGLIST_H

#include <limits.h>

/* struct typedefs */
struct stringNode
{
	int						contains;		// Is this path a string in the list
	char *					str;			// string value for this node
	void *					data;			// data value for this entry
	struct stringNode *		next[CHAR_MAX];	// Child nodes
};
typedef struct stringNode	stringNode_t;

typedef struct
{
	struct stringNode *		root;			// root node of the tree
} stringList_t;

struct stringEntry
{
	const char *			str;			// string for this entry
	void *					data;			// data for this entry
	struct stringEntry *	next;			// next entry	
};
typedef struct stringEntry	stringEntry_t;

/* function prototypes */
stringList_t *  _cti_newStringList(void);
void			_cti_consumeStringList(stringList_t *, void (*)(void *));
int				_cti_searchStringList(stringList_t *, const char *);
void *			_cti_lookupValue(stringList_t *, const char *);
int				_cti_addString(stringList_t *, const char *, void *);
stringEntry_t *	_cti_getEntries(stringList_t *);
void			_cti_cleanupEntries(stringEntry_t *);

#endif /* _CTI_STRINGLIST_H */
