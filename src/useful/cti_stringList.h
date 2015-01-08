/******************************************************************************\
 * cti_stringList.h - Header file for the stringList interface. This implements
 *                    an adaptive radix tree. This saves on both time and space.
 *
 *                    This has the benefit of:
 *
 *                    * O(k) operations. In many cases, this can be faster than
 *                      a hash table since the hash function is an O(k) 
 *                      operation, and hash tables have very poor cache 
 *                      locality.
 *                    * Prefix compression.
 *                    * Ordered iteration.
 *
 * Based on BSD based armon/libart found at:
 *     https://github.com/armon/libart
 *
 * For more info on adaptive radix trees, see: 
 *     http://www-db.in.tum.de/%7Eleis/papers/ART.pdf 
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
 
#ifndef _CTI_STRINGLIST_H
#define _CTI_STRINGLIST_H

#include <stdint.h>

#define MAX_LABEL_LEN		13	// Pay attention to struct alignment here.

#define NODE4		1
#define NODE16		2
#define NODE48		3
#define NODE256	4

/* typedefs */

/*
** Leaf node. This holds the actual key/value pair.
*/
typedef struct
{
	void *					val;				// value for this entry
	uint32_t				key_len;			// length of key
	unsigned char			key[];				// key for this entry
} stringLeaf_t;

/*
** Base node. Each of the sized nodes begin with this type, so we can treat any
** of them as a stringNode_t pointer, and then cast after querying the type.
*/
typedef struct
{
	unsigned char	label[MAX_LABEL_LEN];	// 13 bytes
	uint8_t			label_len;				// 1 byte
	uint8_t			type;					// 1 byte
	uint8_t			num_edges;				// 1 byte
} stringNode_t;

/*
** Node with 4 edges
*/
typedef struct
{
	stringNode_t 	node;
	unsigned char 	keys[4];
	stringNode_t *	edges[4];
} stringNode4_t;

/*
** Node with 16 edges
*/
typedef struct
{
	stringNode_t 	node;
	unsigned char 	keys[16];
	stringNode_t *	edges[16];
} stringNode16_t;

/*
** Node with 48 edges, and full 256 key vector for faster lookups
*/
typedef struct
{
	stringNode_t 	node;
	unsigned char 	keys[256];
	stringNode_t *	edges[48];
} stringNode48_t;

/*
** Full node with 256 edges
*/
typedef struct
{
	stringNode_t 	node;
	stringNode_t *	edges[256];
} stringNode256_t;

/*
** Root of tree.
*/
typedef struct
{
	stringNode_t *			root;	// root node of tree
	uint64_t				nstr;	// number of keys in list
} stringList_t;

/*
** Caller provided callback function for iterating over a tree
*/
typedef int (*cti_stringCallback)(void *opaque, const char *key, void *val);

/* function prototypes */
stringList_t *  _cti_newStringList(void);
void			_cti_consumeStringList(stringList_t *, void (*)(void *));
int				_cti_addString(stringList_t *, const char *, void *);
void			_cti_removeString(stringList_t *, const char *, void (*)(void *));
void *			_cti_lookupValue(stringList_t *, const char *);
int				_cti_lenStringList(stringList_t *);
int				_cti_forEachString(stringList_t *, cti_stringCallback, void *);

#endif /* _CTI_STRINGLIST_H */
