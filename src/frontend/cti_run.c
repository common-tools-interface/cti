/******************************************************************************\
 * cti_run.c - A generic interface to launch and interact with applications.
 *	      This provides the tool developer with an easy to use interface to
 *	      start new instances of an application.
 *
 * Copyright 2011-2014 Cray Inc.  All Rights Reserved.
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

#include <sys/types.h>
#include <sys/stat.h>

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

#include "cti_error.h"
#include "cti_fe.h"
#include "cti_run.h"

static int
_cti_checkofd(int fd)
{
	int flags;
	
	// if fd is -1, then the fd arg is meant to be ignored
	if (fd == -1)
		return 0;
	
	errno = 0;
	flags = fcntl(fd, F_GETFL);
	if (errno != 0)
		return 1;
	flags &= O_ACCMODE;
	if ((flags & O_WRONLY) || (flags & O_RDWR))
		return 0;
		
	return 1;
}

// This does sanity checking on args in common for both launchApp and launchAppBarrier
static int
_cti_checkLaunchArgs(	int stdout_fd, int stderr_fd, const char *inputFile, 
						const char *chdirPath)
{
	struct stat		st;

	if (_cti_checkofd(stdout_fd))
	{
		_cti_set_error("Invalid stdout_fd argument.");
		return 1;
	}
		
	if (_cti_checkofd(stderr_fd))
	{
		_cti_set_error("Invalid stderr_fd argument.");
		return 1;
	}
		
	if (inputFile != NULL)
	{
		// stat the inputFile
		if (stat(inputFile, &st))
		{
			_cti_set_error("Invalid inputFile argument. File does not exist.");
			return 1;
		}
		
		// make sure it is a regular file
		if (!S_ISREG(st.st_mode))
		{
			_cti_set_error("Invalid inputFile argument. The file is not a regular file.");
			return 1;
		}
		
		// make sure we can access it
		if (access(inputFile, R_OK))
		{
			_cti_set_error("Invalid inputFile argument. Bad permissions.");
			return 1;
		}
	}
	
	if (chdirPath != NULL)
	{
		// stat the chdirPath
		if (stat(chdirPath, &st))
		{
			_cti_set_error("Invalid chdirPath argument. Directory does not exist.");
			return 1;
		}
		
		// make sure it is a directory
		if (!S_ISDIR(st.st_mode))
		{
			_cti_set_error("Invalid chdirPath argument. The file is not a directory.");
			return 1;
		}
		
		// make sure we can access it
		if (access(chdirPath, R_OK | W_OK | X_OK))
		{
			_cti_set_error("Invalid chdirPath argument. Bad permissions.");
			return 1;
		}
	}
	
	// args are ok
	return 0;
}

cti_app_id_t
cti_launchApp(	const char * const launcher_argv[], int stdout_fd, int stderr_fd,
				const char *inputFile, const char *chdirPath,
				const char * const env_list[]	)
{
	const cti_wlm_proto_t * curProto = _cti_current_wlm_proto();
	
	// check arguments
	if (_cti_checkLaunchArgs(stdout_fd, stderr_fd, inputFile, chdirPath))
	{
		// error already set
		return 0;
	}

	// call the appropriate wlm launch function based on the current wlm proto
	return curProto->wlm_launch(	launcher_argv, stdout_fd, stderr_fd, 
									inputFile, chdirPath, env_list);
}

cti_app_id_t
cti_launchAppBarrier(	const char * const launcher_argv[], int stdout_fd, int stderr_fd,
				const char *inputFile, const char *chdirPath,
				const char * const env_list[]	)
{
	const cti_wlm_proto_t * curProto = _cti_current_wlm_proto();

	// check arguments
	if (_cti_checkLaunchArgs(stdout_fd, stderr_fd, inputFile, chdirPath))
	{
		// error already set
		return 0;
	}

	// call the appropriate wlm launch function based on the current wlm proto
	return curProto->wlm_launchBarrier(		launcher_argv, stdout_fd, stderr_fd,
											inputFile, chdirPath, env_list);
}

int
cti_releaseAppBarrier(cti_app_id_t appId)
{
	appEntry_t *	app_ptr;
	
	// sanity check
	if (appId == 0)
	{
		_cti_set_error("Invalid appId %d.", (int)appId);
		return 1;
	}
	
	// try to find an entry in the _cti_my_apps list for the apid
	if ((app_ptr = _cti_findAppEntry(appId)) == NULL)
	{
		// couldn't find the entry associated with the apid
		// error string already set
		return 1;
	}
	
	// Call the appropriate barrier release function based on the wlm
	return app_ptr->wlmProto->wlm_releaseBarrier(app_ptr->_wlmObj);
}

int
cti_killApp(cti_app_id_t appId, int signum)
{
	appEntry_t *	app_ptr;
	int				rtn = 1;
	
	// sanity check
	if (appId == 0)
	{
		_cti_set_error("Invalid apid %d.", (int)appId);
		return 1;
	}
	
	// try to find an entry in the _cti_my_apps list for the apid
	if ((app_ptr = _cti_findAppEntry(appId)) == NULL)
	{
		// couldn't find the entry associated with the apid
		// error string already set
		return 1;
	}
	
	// Call the appropriate app kill function based on the wlm
	rtn = app_ptr->wlmProto->wlm_killApp(app_ptr->_wlmObj, signum);
	
	return rtn;
}

