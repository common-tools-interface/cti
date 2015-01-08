/******************************************************************************\
 * cti_list.h - Header file for the list interface.
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

#ifndef _CTI_LIST_H
#define _CTI_LIST_H

struct cti_list_e
{
	void *					this;
	struct cti_list_e *		prev;
	struct cti_list_e *		next;
};
typedef struct cti_list_e cti_list_e_t;

typedef struct
{
	unsigned int	nelems;
	cti_list_e_t *	head;
	cti_list_e_t *	scan;
	cti_list_e_t *	tail;
} cti_list_t;

cti_list_t *	_cti_newList(void);
void			_cti_consumeList(cti_list_t *, void (*)(void *));
int				_cti_list_add(cti_list_t *, void *);
void			_cti_list_remove(cti_list_t *, void *);
void			_cti_list_reset(cti_list_t *);
void *			_cti_list_next(cti_list_t *);
void *			_cti_list_pop(cti_list_t *);
int				_cti_list_len(cti_list_t *);

#endif /* _CTI_LIST_H */
