/*********************************************************************************\
 * alps_fe.h - A header file for the alps specific frontend interface.
 *
 * © 2014 Cray Inc.	All Rights Reserved.
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

#ifndef _ALPS_FE_H
#define _ALPS_FE_H

#include "alps/alps.h"
#include "alps/apInfo.h"

/* function prototypes */

int 			_cti_alps_init(void);
void			_cti_alps_fini(void);
int				_cti_alps_ready(void);
uint64_t		_cti_alps_get_apid(int, pid_t);
int				_cti_alps_get_appinfo(uint64_t, appInfo_t *, cmdDetail_t **, placeList_t **);
const char *	_cti_alps_launch_tool_helper(uint64_t, int, int, int, int, char **);

#endif /* _ALPS_FE_H */
