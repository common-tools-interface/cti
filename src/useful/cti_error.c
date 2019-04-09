/******************************************************************************\
 * cti_error.c - Global error handling interface. This should be used on the
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
 ******************************************************************************/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include "cti_error.h"
#include "cti_defs.h"

static char *	_cti_err_string = NULL;

void
_cti_set_error(const char *fmt, ...)
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

/*
 * _cti_is_valid_environment - Validate values of CTI environment variables
 *
 * Detail
 *      This function validates the values of set CTI environment variables.
 *		Currently this is set up to validate that CRAY_CTI_LAUNCHER_NAME
 *		is indeed an available executable. This can be extended further to
 *		validate new environment variables as they come up.
 *
 * Returns
 *      true if the set CTI environment variables contain valid values, false otherwise
 *
 */
bool _cti_is_valid_environment(){
	// Check that the specified launcher exists
	char* launcher_name_env;
	if ((launcher_name_env = getenv(CTI_LAUNCHER_NAME)) != NULL)
	{
		char* launcher_name_command;
		if(strcmp(launcher_name_env, "") == 0){
			_cti_set_error("Provided launcher path is empty.");
			return false;
		}

		asprintf(&launcher_name_command, "command -v %s > /dev/null 2>&1", launcher_name_env);
		int status = system(launcher_name_command);
		if ( (status == -1) || WEXITSTATUS(status) ) {
			// Command doesn't exist...
			_cti_set_error("Provided launcher '%s' cannot be found.\n", launcher_name_env);
			return false;
		}
	}

	return true;
}

// The internal library should not have access to this function, that is why
// it is not defined in the cti_error.h header.
const char *
cti_error_str(void)
{
	if (_cti_err_string == NULL)
		_cti_set_error("%s", DEFAULT_ERR_STR);

	return (const char *)_cti_err_string;
}

