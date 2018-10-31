/******************************************************************************\
 * cti_error.h - Global error handling interface. This should be used on the
 *               frontend only.
 *
 * Copyright 2013 Cray Inc.  All Rights Reserved.
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

#ifndef _CTI_ERROR_H
#define _CTI_ERROR_H

#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

void	_cti_set_error(const char *, ...);
bool	_cti_is_valid_environment();

#ifdef __cplusplus
}
#endif

#endif /* _CTI_ERROR_H */

