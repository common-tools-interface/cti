/*********************************************************************************\
 * stringList.h - Header file for the stringList interface.
 *
 * © 2011-2013 Cray Inc.  All Rights Reserved.
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
 *********************************************************************************/
 
#ifndef _STRINGLIST_H
#define _STRINGLIST_H

#define BLOCK_SIZE              10

/* struct typedefs */
typedef struct
{
        int             num;            // number of strings currently in the list
        char **         list;           // pointer to a list of strings
        size_t          len;            // total alloc'ed size of the list
} stringList_t;

/* function prototypes */
stringList_t *  _cti_newStringList(void);
int             _cti_consumeStringList(stringList_t *);
int             _cti_searchStringList(stringList_t *, const char *);
int             _cti_addString(stringList_t *, const char *);

#endif /* _STRINGLIST_H */
