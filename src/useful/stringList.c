/*********************************************************************************\
 * stringList.c - Functions relating to creating and maintaining searchable lists
 *                of strings.
 *
 * © 2011 Cray Inc.  All Rights Reserved.
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
 
#ifdef HAVE_CONFIG_H
#include        <config.h>
#endif /* HAVE_CONFIG_H */
 
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "stringList.h"

/* Static prototypes */
static int      growStringList(stringList_t *);

static int
growStringList(stringList_t *lst)
{
        int             i;
        char **         tmp_lst;
        
        // sanity check
        if (lst == (stringList_t *)NULL)
                return 1;
                
        // check if the actual list is null
        if (lst->list == (char **)NULL)
        {
                // allocate 10 new char ptrs for the list
                if ((lst->list = calloc(BLOCK_SIZE, sizeof(char *))) == 0)
                {
                        return 1;
                }
                memset(lst->list, 0, BLOCK_SIZE*sizeof(char *));     // clear it to NULL
                
                // reset the len and num values
                lst->num = 0;
                lst->len = BLOCK_SIZE;
                
                return 0;
        }
        
        // ensure there is enough entries in the list for a future addition
        if ((lst->num + 1) > lst->len)
        {
                if ((tmp_lst = calloc(lst->len + BLOCK_SIZE, sizeof(char *))) == 0)
                {
                        return 1;
                }
                memset(tmp_lst, 0, (lst->len + BLOCK_SIZE)*sizeof(char *));     // clear it to NULL
                
                // copy the old list to the new one
                for (i = 0; i < lst->num; i++)
                {
                        tmp_lst[i] = lst->list[i];
                }
                // free the old list
                free(lst->list);
                // set the new list
                lst->list = tmp_lst;
                // increment the len value
                lst->len += BLOCK_SIZE;
                
                return 0;
        }

        return 0;
}

// The following functions are used to interact with a stringList_t

stringList_t *
newStringList()
{
        stringList_t *  this;

        // create the new stringList_t object
        if ((this = malloc(sizeof(stringList_t))) == (void *)NULL)
        {
                // malloc failed
                return (stringList_t *)NULL;
        }
        memset(this, 0, sizeof(stringList_t));     // clear it to NULL

        // growStringList takes care of the initial creation of the list for us
        if (growStringList(this))
        {
                // growStringList failed
                free(this);
                return (stringList_t *)NULL;
        }
        
        return this;
}

int
consumeStringList(stringList_t *lst)
{
        char ** tmp;

        // sanity check
        if (lst == (stringList_t *)NULL)
                return 1;
        
        // free the actual list of strings       
        if (lst->list != (char **)NULL)
        {
                // free each entry
                tmp = lst->list;
                while (*tmp != (char *)NULL)
                {
                        free(*tmp++);
                }
                // free the list itself
                free(lst->list);
        }
        
        // free the stringList_t object
        free(lst);
        
        return 0;
}

int
searchStringList(stringList_t *lst, char *str)
{
        char **str_ptr;
        int i = lst->num;
        
        // sanity check
        if (lst == (stringList_t *)NULL || str == (char *)NULL)
                return 0;
                
        // shouldn't happen, but better safe then sorry
        if (lst->list == (char **)NULL)
                return 0;
                
        // set the str_ptr to the start of the list
        str_ptr = lst->list;
        
        // iterate through the list
        while (0 < i--)
        {
                if (!strcmp(*str_ptr, str))
                        return 1;
                ++str_ptr;
        }
        
        // not found
        return 0;
}

int
addString(stringList_t *lst, char *str)
{
        // sanity check
        if (lst == (stringList_t *)NULL || str == (char *)NULL)
                return 1;
                
        // ensure room exists in the list
        if (growStringList(lst))
                return 1;
                
        // add the str to the list at the index num
        // post increment num
        lst->list[lst->num++] = strdup(str);
        
        return 0;
}


