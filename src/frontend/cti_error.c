/******************************************************************************\
 * cti_error.c - Global error handling interface. This should be used on the
 *               frontend only.
 *
 * © 2013 Cray Inc.  All Rights Reserved.
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

#include <stdio.h>
#include <stdlib.h>

#include "cti_error.h"

#define DEFAULT_ERR_STR	"Unknown CTI error"

static char *	_cti_err_string = NULL;

void
_cti_set_error(char *fmt, ...)
{
	va_list ap;

	if (fmt == NULL)
		return;
		
	if (_cti_err_string != NULL)
	{
		free(_cti_err_string);
		_cti_err_string = NULL;
	}

	va_start(ap, fmt);
	
	vasprintf(&_cti_err_string, fmt, ap);

	va_end(ap);
}

// The internal library should not have access to this function, that is why
// it is not defined in the cti_error.h header.
const char *
cti_error_str(void)
{
	if (_cti_err_string == NULL)
		return DEFAULT_ERR_STR;
		
	return (const char *)_cti_err_string;
}

