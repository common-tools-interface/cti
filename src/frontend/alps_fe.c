/******************************************************************************\
 * alps_fe.c - alps specific frontend library functions.
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
 ******************************************************************************/

#include <dlfcn.h>
#include <stdlib.h>
#include <string.h>

#include "alps_fe.h"
#include "cti_error.h"

typedef struct
{
	void *			handle;
	uint64_t    	(*alps_get_apid)(int, pid_t);
	int				(*alps_get_appinfo)(uint64_t, appInfo_t *, cmdDetail_t **, placeList_t **);
	const char *	(*alps_launch_tool_helper)(uint64_t, int, int, int, int, char **);
} cti_alps_funcs_t;

/* global variables */
static cti_alps_funcs_t *	_cti_alps_ptr = NULL;

int
_cti_alps_init(void)
{
	char *error;

	// Only init once.
	if (_cti_alps_ptr != NULL)
		return 0;
		
	// Create a new cti_alps_funcs_t
	if ((_cti_alps_ptr = malloc(sizeof(cti_alps_funcs_t))) == NULL)
	{
		_cti_set_error("malloc failed.");
		return 1;
	}
	memset(_cti_alps_ptr, 0, sizeof(cti_alps_funcs_t));     // clear it to NULL
	
	if ((_cti_alps_ptr->handle = dlopen("libalps.so", RTLD_LAZY)) == NULL)
	{
		_cti_set_error("dlopen: %s", dlerror());
		free(_cti_alps_ptr);
		_cti_alps_ptr = NULL;
		return 1;
	}
	
	// Clear any existing error
	dlerror();
	
	// load alps_get_apid
	_cti_alps_ptr->alps_get_apid = dlsym(_cti_alps_ptr->handle, "alps_get_apid");
	if ((error = dlerror()) != NULL)
	{
		_cti_set_error("dlsym: %s", error);
		dlclose(_cti_alps_ptr->handle);
		free(_cti_alps_ptr);
		_cti_alps_ptr = NULL;
		return 1;
	}
	
	// load alps_get_appinfo
	_cti_alps_ptr->alps_get_appinfo = dlsym(_cti_alps_ptr->handle, "alps_get_appinfo");
	if ((error = dlerror()) != NULL)
	{
		_cti_set_error("dlsym: %s", error);
		dlclose(_cti_alps_ptr->handle);
		free(_cti_alps_ptr);
		_cti_alps_ptr = NULL;
		return 1;
	}
	
	// load alps_launch_tool_helper
	_cti_alps_ptr->alps_launch_tool_helper = dlsym(_cti_alps_ptr->handle, "alps_launch_tool_helper");
	if ((error = dlerror()) != NULL)
	{
		_cti_set_error("dlsym: %s", error);
		dlclose(_cti_alps_ptr->handle);
		free(_cti_alps_ptr);
		_cti_alps_ptr = NULL;
		return 1;
	}
	
	// done
	return 0;
}

void
_cti_alps_fini(void)
{
	// sanity check
	if (_cti_alps_ptr == NULL)
		return;
		
	// cleanup
	dlclose(_cti_alps_ptr->handle);
	free(_cti_alps_ptr);
	_cti_alps_ptr = NULL;
	
	return;
}

// This returns true if init finished okay, otherwise it returns false. We assume in that
// case the the cti_error was already set.
int
_cti_alps_ready(void)
{
	return (_cti_alps_ptr != NULL);
}

uint64_t
_cti_alps_get_apid(int arg1, pid_t arg2)
{
	// sanity check
	if (_cti_alps_ptr == NULL)
		return 0;
		
	return (*_cti_alps_ptr->alps_get_apid)(arg1, arg2);
}

int
_cti_alps_get_appinfo(uint64_t arg1, appInfo_t *arg2, cmdDetail_t **arg3, placeList_t **arg4)
{
	// sanity check
	if (_cti_alps_ptr == NULL)
		return -1;
		
	return (*_cti_alps_ptr->alps_get_appinfo)(arg1, arg2, arg3, arg4);
}

const char *
_cti_alps_launch_tool_helper(uint64_t arg1, int arg2, int arg3, int arg4, int arg5, char **arg6)
{
	// sanity check
	if (_cti_alps_ptr == NULL)
		return "CTI error: _cti_alps_ptr is NULL!";
		
	return (*_cti_alps_ptr->alps_launch_tool_helper)(arg1, arg2, arg3, arg4, arg5, arg6);
}

