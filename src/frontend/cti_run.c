/******************************************************************************\
 * cti_run.c - A generic interface to launch and interact with applications.
 *	      This provides the tool developer with an easy to use interface to
 *	      start new instances of an application.
 *
 * © 2011-2014 Cray Inc.  All Rights Reserved.
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
 
#include <stdlib.h>

#include "cti_error.h"
#include "cti_fe.h"
#include "cti_run.h"
#include "alps_fe.h"

cti_app_id_t
cti_launchAppBarrier(	char **launcher_argv, int redirectOutput, int redirectInput, 
						int stdout_fd, int stderr_fd, char *inputFile, char *chdirPath,
						char **env_list	)
{
	// get the current WLM and call the appropriate launch function
	switch (cti_current_wlm())
	{
		case CTI_WLM_ALPS:
			return _cti_alps_launchBarrier(launcher_argv, redirectOutput, redirectInput, stdout_fd, stderr_fd, inputFile, chdirPath, env_list);
			
		case CTI_WLM_CRAY_SLURM:
		case CTI_WLM_SLURM:
			_cti_set_error("Current WLM is not yet supported.");
			return 0;
			
		case CTI_WLM_MULTI:
			// TODO - add argument to allow caller to select preferred wlm if there is
			// ever multiple WLMs present on the system.
			_cti_set_error("Multiple workload managers present! This is not yet supported.");
			return 0;
			
		case CTI_WLM_NONE:
			_cti_set_error("No valid workload manager detected.");
			return 0;
	}
	
	// should not get here
	_cti_set_error("At impossible exit.");
	return 0;
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
	
	// sanity check
	if (app_ptr->_wlmObj == NULL)
	{
		_cti_set_error("Aprun barrier release operation failed.");
		return 1;
	}
	
	// Call the appropriate barrier release function based on the wlm
	switch (app_ptr->wlm)
	{
		case CTI_WLM_ALPS:
			return _cti_alps_releaseBarrier(app_ptr->_wlmObj);
			
		case CTI_WLM_CRAY_SLURM:
		case CTI_WLM_SLURM:
			_cti_set_error("Current WLM is not yet supported.");
			return 1;
			
		case CTI_WLM_MULTI:
			// TODO - add argument to allow caller to select preferred wlm if there is
			// ever multiple WLMs present on the system.
			_cti_set_error("Multiple workload managers present! This is not yet supported.");
			return 1;
			
		case CTI_WLM_NONE:
			_cti_set_error("No valid workload manager detected.");
			return 1;
	}
	
	// done
	return 0;
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
	
	// sanity check
	if (app_ptr->_wlmObj == NULL)
	{
		_cti_set_error("Aprun barrier release operation failed.");
		return 1;
	}
	
	// Call the appropriate app kill function based on the wlm
	switch (app_ptr->wlm)
	{
		case CTI_WLM_ALPS:
			rtn = _cti_alps_killApp(app_ptr->_wlmObj, signum);
			break;
			
		case CTI_WLM_CRAY_SLURM:
		case CTI_WLM_SLURM:
			_cti_set_error("Current WLM is not yet supported.");
			return 1;
			
		case CTI_WLM_MULTI:
			// TODO - add argument to allow caller to select preferred wlm if there is
			// ever multiple WLMs present on the system.
			_cti_set_error("Multiple workload managers present! This is not yet supported.");
			return 1;
			
		case CTI_WLM_NONE:
			_cti_set_error("No valid workload manager detected.");
			return 1;
	}
	
	// deregister this apid from the interface
	if (!rtn)
		cti_deregisterApp(appId);
	
	return rtn;
}

