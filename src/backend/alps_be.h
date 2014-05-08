/*********************************************************************************\
 * alps_be.h - A header file for the alps specific backend interface.
 *
 * © 2014 Cray Inc.  All Rights Reserved.
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

#ifndef _ALPS_BE_H
#define _ALPS_BE_H

#include "cti_be.h"

/* function prototypes */
int 			_cti_alps_init(void);
void			_cti_alps_fini(void);
cti_pidList_t *	_cti_alps_findAppPids(void);
char *			_cti_alps_getNodeHostname(void);
int				_cti_alps_getNodeFirstPE(void);
int				_cti_alps_getNodePEs(void);

#endif /* _ALPS_BE_H */
