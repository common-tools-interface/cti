/******************************************************************************\
 * cti_fe.c - cti frontend library functions.
 *
 * Copyright 2014-2016 Cray Inc.  All Rights Reserved.
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

#include <errno.h>
#include <dlfcn.h>
#include <pwd.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/stat.h>

#include "wlm_detect.h"

#include "cti_fe.h"
#include "cti_defs.h"
#include "cti_error.h"
#include "cti_transfer.h"
#include "cti_useful.h"

#include "alps_fe.h"
#include "cray_slurm_fe.h"
#include "slurm_fe.h"
#include "ssh_fe.h"

typedef struct
{
	void *			handle;
	char *			(*wlm_detect_get_active)(void);
	const char *	(*wlm_detect_get_default)(void);
} cti_wlm_detect_t;

/* Static prototypes */
static void			_cti_setup_base_dir(void);
static int			_cti_checkDirPerms(const char *);
static void			_cti_consumeAppEntry(void *);
bool				_cti_is_cluster_system();

/* static global vars */
static bool				_cti_fe_isInit		= false;	// Have we called init?
static bool				_cti_fe_isFini		= false;	// Have we called fini?
static cti_app_id_t		_cti_app_id 		= 1;		// start counting from 1
static cti_list_t *		_cti_my_apps		= NULL;		// global list pertaining to known application sessions
static cti_wlm_detect_t	_cti_wlm_detect 	= {0};		// wlm_detect functions for dlopen
static char *			_cti_cfg_dir		= NULL;		// config dir that we can use as temporary storage
static char *			_cti_ld_audit_lib	= NULL;		// ld audit library location
static char *			_cti_overwatch_bin	= NULL;		// overwatch binary location
static char *			_cti_gdb_bin		= NULL;		// GDB binary location
static char *			_cti_starter_bin	= NULL;		// MPIR starter binary location
static char *			_cti_attach_bin		= NULL;		// MPIR attach binary location
static char *			_cti_dlaunch_bin	= NULL;		// dlaunch binary location
static char *			_cti_slurm_util		= NULL;		// slurm utility binary location
static char * _cti_default_dir_locs[] = {DEFAULT_CTI_LOCS};

/* noneness wlm proto object */
static const cti_wlm_proto_t	_cti_nonenessProto =
{
	CTI_WLM_NONE,
	_cti_wlm_init_none,
	_cti_wlm_fini_none,
	_cti_wlm_destroy_none,
	_cti_wlm_getJobId_none,
	_cti_wlm_launch_none,
	_cti_wlm_launchBarrier_none,
	_cti_wlm_releaseBarrier_none,
	_cti_wlm_killApp_none,
	_cti_wlm_extraBinaries_none,
	_cti_wlm_extraLibraries_none,
	_cti_wlm_extraLibDirs_none,
	_cti_wlm_extraFiles_none,
	_cti_wlm_shipPackage_none,
	_cti_wlm_startDaemon_none,
	_cti_wlm_getNumAppPEs_none,
	_cti_wlm_getNumAppNodes_none,
	_cti_wlm_getAppHostsList_none,
	_cti_wlm_getAppHostsPlacement_none,
	_cti_wlm_getHostName_none,
	_cti_wlm_getLauncherHostName_none,
	_cti_wlm_getToolPath_none,
	_cti_wlm_getAttribsPath_none
};

/* global wlm proto object - this is initialized to noneness by default */
static const cti_wlm_proto_t *	_cti_wlmProto = &_cti_nonenessProto;

/*
 * This routine initializes CTI so it is set up for usage by the executable with which it is linked.
 * Part of this includes automatically determining the active Workload Manager. The
 * user can force SSH as the "WLM" by setting the environment variable CTI_LAUNCHER_NAME.
 * In the case of complete failure to determine the WLM, the default value of _cti_nonenessProto
 * is used.
*/
void __attribute__((constructor))
_cti_init(void)
{
	char *	active_wlm;
	char *	error;
	bool	use_default = false;
	bool	do_free = true;
	
	// only init once
	if (_cti_fe_isInit)
		return;
	
	// We do not want to call init if we are running on the backend inside of
	// a tool daemon! It is possible for BE libraries to link against both the
	// CTI fe and be libs (e.g. MRNet) and we do not want to call the FE init
	// in that case.
	if (getenv(BE_GUARD_ENV_VAR) != NULL)
		return;
	
	// allocate global data structures
	_cti_my_apps = _cti_newList();
	_cti_app_id = 1;
	
	// setup base directory info
	_cti_setup_base_dir();
	
	// init the transfer interface
	_cti_transfer_init();

	// Use the workload manager in the environment variable if it is set 
	char* wlm_name_env;
	if ((wlm_name_env = getenv(CTI_WLM)) != NULL)
	{
		if(strcasecmp(wlm_name_env, "alps") == 0)
		{
			_cti_wlmProto = &_cti_alps_wlmProto;
		}
		else if(strcasecmp(wlm_name_env, "slurm") == 0)
		{
			// Check to see if we are on a cluster. If so, use the cluster slurm prototype.
			struct stat sb;
			if (_cti_is_cluster_system())
			{
				_cti_wlmProto = &_cti_slurm_wlmProto;
			} 
			else
			{
				_cti_wlmProto = &_cti_cray_slurm_wlmProto;
			}
		}
		else if(strcasecmp(wlm_name_env, "generic") == 0)
		{
			_cti_wlmProto = &_cti_ssh_wlmProto;
		}
		else
		{
			fprintf(stderr, "%s\n", "Invalid workload manager option. Defaulting to generic.");			
			_cti_wlmProto = &_cti_ssh_wlmProto;
		}

		goto init_wlm;
	}

	if ((_cti_wlm_detect.handle = dlopen(WLM_DETECT_LIB_NAME, RTLD_LAZY)) == NULL)
	{
		// Check to see if we are on a cluster. If so, use the slurm proto
		if (_cti_is_cluster_system())
		{
			_cti_wlmProto = &_cti_slurm_wlmProto;
		} 
		else
		{
			use_default = true;
		}
		goto init_wlm;
	}
	
	// Clear any existing error
	dlerror();
	
	// load wlm_detect_get_active
	_cti_wlm_detect.wlm_detect_get_active = dlsym(_cti_wlm_detect.handle, "wlm_detect_get_active");
	if ((error = dlerror()) != NULL)
	{
		dlclose(_cti_wlm_detect.handle);
		use_default = true;
		goto init_wlm;
	}
	
	// try to get the active wlm
	active_wlm = (*_cti_wlm_detect.wlm_detect_get_active)();
	if (active_wlm == NULL)
	{
		// load wlm_detect_get_default
		_cti_wlm_detect.wlm_detect_get_default = dlsym(_cti_wlm_detect.handle, "wlm_detect_get_default");
		if ((error = dlerror()) != NULL)
		{
			dlclose(_cti_wlm_detect.handle);
			use_default = true;
			goto init_wlm;
		}
		// use the default wlm
		active_wlm = (char *)(*_cti_wlm_detect.wlm_detect_get_default)();
		do_free = false;
	}
	
	// parse the returned result
	if (strncmp("ALPS", active_wlm, strlen("ALPS")) == 0)
	{
		_cti_wlmProto = &_cti_alps_wlmProto;
	} else if (strncmp("SLURM", active_wlm, strlen("SLURM")) == 0)
	{
		_cti_wlmProto = &_cti_cray_slurm_wlmProto;
	} else
	{
		// fallback to use the default
		use_default = true;
	}
	
	// close the wlm_detect handle, we are done with it
	dlclose(_cti_wlm_detect.handle);
	
	// maybe cleanup the string
	if (do_free)
	{
		free(active_wlm);
	}
	
init_wlm:

	// check if wlm_detect failed, in which case we should use the default
	if (use_default)
	{
		_cti_wlmProto = &_cti_alps_wlmProto;
	}
	
	if (_cti_wlmProto->wlm_init())
	{
		// We failed to init, so reset wlm proto to noneness
		_cti_wlmProto = &_cti_nonenessProto;
		return;
	}
	
	// set the init guard to true
	_cti_fe_isInit = true;
}

// Destructor function
void __attribute__ ((destructor))
_cti_fini(void)
{
	// Ensure this is only called once
	if (_cti_fe_isFini)
		return;

	// cleanup global data structures
	_cti_consumeList(_cti_my_apps, _cti_consumeAppEntry);
	_cti_my_apps = NULL;
	
	// call the wlm proto fini function
	_cti_wlmProto->wlm_fini();
	
	// call the transfer fini function
	_cti_transfer_fini();
	
	// free the location strings
	if (_cti_cfg_dir != NULL)
		free(_cti_cfg_dir);
	_cti_cfg_dir = NULL;
	if (_cti_ld_audit_lib != NULL)
		free(_cti_ld_audit_lib);
	_cti_ld_audit_lib = NULL;
	if (_cti_overwatch_bin != NULL)
		free(_cti_overwatch_bin);
	_cti_overwatch_bin = NULL;
	if (_cti_gdb_bin != NULL)
		free(_cti_gdb_bin);
	_cti_gdb_bin = NULL;
	if (_cti_starter_bin != NULL)
		free(_cti_starter_bin);
	_cti_starter_bin = NULL;
	if (_cti_attach_bin != NULL)
		free(_cti_attach_bin);
	_cti_attach_bin = NULL;
	if (_cti_dlaunch_bin != NULL)
		free(_cti_dlaunch_bin);
	_cti_dlaunch_bin = NULL;
	if (_cti_slurm_util != NULL)
		free(_cti_slurm_util);
	_cti_slurm_util = NULL;
	
	// reset wlm proto to noneness
	_cti_wlmProto = &_cti_nonenessProto;
	
	_cti_fe_isFini = true;
	
	return;
}

/*********************
** internal functions 
*********************/

bool _cti_is_cluster_system(){
	struct stat sb;
	if (stat(CLUSTER_FILE_TEST, &sb) == 0)
	{
		return true;
	} 
	else
	{
		return false;
	}
}

int is_accessible_directory(char* path){
	// make sure this directory exists
	struct stat		st;
	if (stat(path, &st))
		return 0;
		
	// make sure it is a directory
	if (!S_ISDIR(st.st_mode))
		return 0;
		
	// check if we can access the directory
	if (access(path, R_OK | X_OK))
		return 0;
	
	return 1;
}

static void
_cti_setup_base_dir(void)
{
	char *			base_dir;

	base_dir = getenv(BASE_DIR_ENV_VAR);	

	if ( (base_dir == NULL) || !is_accessible_directory(base_dir) ){
	  int i;
	  for(i=0; _cti_default_dir_locs[i] != NULL; i++){
	    if(is_accessible_directory(_cti_default_dir_locs[i])){
	      base_dir = _cti_default_dir_locs[i];
	      break;
	    }
	  }
	}     
	
	// setup location paths
	
	if (asprintf(&_cti_ld_audit_lib, "%s/lib/%s", base_dir, LD_AUDIT_LIB_NAME) > 0)
	{
		if (access(_cti_ld_audit_lib, R_OK | X_OK))
		{
			free(_cti_ld_audit_lib);
			_cti_ld_audit_lib = NULL;
		}
	} else
	{
		_cti_ld_audit_lib = NULL;
	}
	
	if (asprintf(&_cti_overwatch_bin, "%s/libexec/%s", base_dir, CTI_OVERWATCH_BINARY) > 0)
	{
		if (access(_cti_overwatch_bin, R_OK | X_OK))
		{
			free(_cti_overwatch_bin);
			_cti_overwatch_bin = NULL;
		}
	} else
	{
		_cti_overwatch_bin = NULL;
	}
	
	if (asprintf(&_cti_gdb_bin, "%s/libexec/%s", base_dir, CTI_GDB_BINARY) > 0)
	{
		if (access(_cti_gdb_bin, R_OK | X_OK))
		{
			free(_cti_gdb_bin);
			_cti_gdb_bin = NULL;
		}
	} else
	{
		_cti_gdb_bin = NULL;
	}
	
	if (asprintf(&_cti_starter_bin, "%s/libexec/%s", base_dir, GDB_MPIR_STARTER) > 0)
	{
		if (access(_cti_starter_bin, R_OK | X_OK))
		{
			free(_cti_starter_bin);
			_cti_starter_bin = NULL;
		}
	} else
	{
		_cti_starter_bin = NULL;
	}
	
	if (asprintf(&_cti_attach_bin, "%s/libexec/%s", base_dir, GDB_MPIR_ATTACH) > 0)
	{
		if (access(_cti_attach_bin, R_OK | X_OK))
		{
			free(_cti_attach_bin);
			_cti_attach_bin = NULL;
		}
	} else
	{
		_cti_attach_bin = NULL;
	}
	
	if (asprintf(&_cti_dlaunch_bin, "%s/libexec/%s", base_dir, CTI_LAUNCHER) > 0)
	{
		if (access(_cti_dlaunch_bin, R_OK | X_OK))
		{
			free(_cti_dlaunch_bin);
			_cti_dlaunch_bin = NULL;
		}
	} else
	{
		_cti_dlaunch_bin = NULL;
	}
	
	if (asprintf(&_cti_slurm_util, "%s/libexec/%s", base_dir, SLURM_STEP_UTIL) > 0)
	{
		if (access(_cti_slurm_util, R_OK | X_OK))
		{
			free(_cti_slurm_util);
			_cti_slurm_util = NULL;
		}
	} else
	{
		_cti_slurm_util = NULL;
	}
}

// getter functions for paths

const char *
_cti_getLdAuditPath(void)
{
	return (const char *)_cti_ld_audit_lib;
}

const char *
_cti_getOverwatchPath(void)
{
	return (const char *)_cti_overwatch_bin;
}

const char *
_cti_getGdbPath(void)
{
	return (const char *)_cti_gdb_bin;
}

const char *
_cti_getStarterPath(void)
{
	return (const char *)_cti_starter_bin;
}

const char *
_cti_getAttachPath(void)
{
	return (const char *)_cti_attach_bin;
}

const char *
_cti_getDlaunchPath(void)
{
	return (const char *)_cti_dlaunch_bin;
}

const char *
_cti_getSlurmUtilPath(void)
{
	return (const char *)_cti_slurm_util;
}

static int
_cti_checkDirPerms(const char *dir)
{
	struct stat		st;
	
	if (dir == NULL)
		return 1;
	
	// Stat the tmp_dir
	if (stat(dir, &st))
	{
		// could not stat the directory
		return 1;
	}
	
	if (!S_ISDIR(st.st_mode))
	{
		// this is not a directory
		return 1;
	}
	
	// check if we can access the directory
	if (access(dir, R_OK | W_OK | X_OK))
	{
		// directory doesn't have proper permissions
		return 1;
	}
	
	return 0;
}

const char *
_cti_getCfgDir(void)
{
	char *			cfg_dir;
	char *			tmp_dir;
	struct passwd *	pw;
	struct stat		st;

	// return if we already have the value
	if (_cti_cfg_dir != NULL)
		return _cti_cfg_dir;
		
	// Get the pw info, this is used in the unique name part of cfg directories
	// and when doing the final ownership check
	if ((pw = getpwuid(getuid())) == NULL)
	{
		_cti_set_error("_cti_getCfgDir: getpwuid() %s", strerror(errno));
		return NULL;
	}
	
	// get the cfg dir settings
	if ((cfg_dir = getenv(CFG_DIR_VAR)) == NULL)
	{
		// Not found.
		// Ideally we want to use TMPDIR or /tmp, the directory name should be 
		// unique to the user.
		
		// Check to see if TMPDIR is set
		tmp_dir = getenv("TMPDIR");
		
		// check if can write to tmp_dir
		if (_cti_checkDirPerms(tmp_dir))
		{
			// We couldn't write to tmp_dir, so lets try using /tmp
			tmp_dir = "/tmp";
			if (_cti_checkDirPerms(tmp_dir))
			{
				// We couldn't write to /tmp, so lets use the home directory
				tmp_dir = getenv("HOME");
				
				// Check if we can write to HOME
				if (_cti_checkDirPerms(tmp_dir))
				{
					// We have no where to create a temporary directory...
					_cti_set_error("Cannot find suitable config directory. Try setting the %s env variable.", CFG_DIR_VAR);
					return NULL;
				}
			}
		}
		
		// Create the directory name string - we default this to have the name cray_cti-<username>
		if (asprintf(&cfg_dir, "%s/cray_cti-%s", tmp_dir, pw->pw_name) <= 0)
		{
			_cti_set_error("_cti_getCfgDir: asprintf failed.");
			return NULL;
		}
		
		// Create and setup the directory
		
		// try to stat the directory
		if (stat(cfg_dir, &st))
		{
			// the directory doesn't exist so we need to create it
			// use perms 700
			if (mkdir(cfg_dir, S_IRWXU))
			{
				_cti_set_error("_cti_getCfgDir: mkdir() %s", strerror(errno));
				return NULL;
			}
		} else
		{
			// directory already exists, so chmod it if has bad permissions.
			// We created this directory previously.
			if ((st.st_mode & (S_ISUID | S_ISGID | S_IRWXU | S_IRWXG | S_IRWXO)) & ~S_IRWXU)
			{
				if (chmod(cfg_dir, S_IRWXU))
				{
					_cti_set_error("_cti_getCfgDir: chmod() %s", strerror(errno));
					return NULL;
				}
			}
		}
		
		// make sure we have a good path string
		tmp_dir = cfg_dir;
		if ((cfg_dir = realpath(tmp_dir, NULL)) == NULL)
		{
			_cti_set_error("_cti_getCfgDir: realpath() %s", strerror(errno));
			free(tmp_dir);
			return NULL;
		}
		free(tmp_dir);
	} else
	{
		// The user set CFG_DIR_VAR, we *ALWAYS* want to use that
		
		// Check to see if we can write to this directory
		if (_cti_checkDirPerms(cfg_dir))
		{
			_cti_set_error("Bad directory specified by environment variable %s.", CFG_DIR_VAR);
			return NULL;
		}
		
		// verify that it has the permissions we expect
		if (stat(cfg_dir, &st))
		{
			// could not stat the directory
			_cti_set_error("_cti_getCfgDir: stat() %s", strerror(errno));
			return NULL;
		}
		if ((st.st_mode & (S_ISUID | S_ISGID | S_IRWXU | S_IRWXG | S_IRWXO)) & ~S_IRWXU)
		{
			// bits other than S_IRWXU are set
			_cti_set_error("Bad permissions for directory specified by environment variable %s. Only 0700 allowed.", CFG_DIR_VAR);
			return NULL;
		}
		
		// call realpath on cfg_dir
		tmp_dir = cfg_dir;
		if ((cfg_dir = realpath(tmp_dir, NULL)) == NULL)
		{
			_cti_set_error("_cti_getCfgDir: realpath() %s", strerror(errno));
			return NULL;
		}
	}
	
	// Ensure we have ownership of this directory, otherwise it is untrusted
	memset(&st, 0, sizeof(st));
	if (stat(cfg_dir, &st))
	{
		// could not stat the directory
		_cti_set_error("_cti_getCfgDir: stat() %s", strerror(errno));
		free(cfg_dir);
		return NULL;
	}
	// verify that we have ownership of this directory
	if (st.st_uid != pw->pw_uid)
	{
		_cti_set_error("_cti_getCfgDir: Directory %s already exists", cfg_dir);
		free(cfg_dir);
		return NULL;
	}
	
	// set the global variable
	_cti_cfg_dir = cfg_dir;
	
	return _cti_cfg_dir;
}

static void
_cti_consumeAppEntry(void *this)
{
	appEntry_t *	entry = (appEntry_t *)this;
	void *			s_ptr;
	
	// sanity check
	if (entry == NULL)
		return;
	
	// consume sessions associated with this app, they are no longer valid
	while ((s_ptr = _cti_list_pop(entry->sessions)) != NULL)
	{
		_cti_consumeSession(s_ptr);
	}
	_cti_consumeList(entry->sessions, NULL);
	
	// Check to see if there is a wlm obj
	if (entry->_wlmObj != NULL)
	{
		// Call the appropriate function based on the wlm
		entry->wlmProto->wlm_destroy(entry->_wlmObj);
	}
	entry->_wlmObj = NULL;
	
	// nom nom the final appEntry_t object
	free(entry);
}

appEntry_t *
_cti_newAppEntry(const cti_wlm_proto_t *wlmProto, cti_wlm_obj wlm_obj)
{
	appEntry_t *	this;
	
	// sanity
	if (wlmProto == NULL || wlm_obj == NULL)
	{
		_cti_set_error("_cti_newAppEntry: Bad args.");
		return NULL;
	}
	
	// create the new appEntry_t object
	if ((this = malloc(sizeof(appEntry_t))) == NULL)
	{
		_cti_set_error("malloc failed.");
		return NULL;
	}
	memset(this, 0, sizeof(appEntry_t));     // clear it to NULL
	
	// set the members
	this->appId = _cti_app_id++;	// assign this to the next id.
	this->sessions = _cti_newList();
	this->wlmProto = wlmProto;
	this->_wlmObj = wlm_obj;
	this->refCnt = 1;
	
	// save the new appEntry_t into the global list
	if(_cti_list_add(_cti_my_apps, this))
	{
		_cti_set_error("_cti_newAppEntry: _cti_list_add() failed.");
		_cti_consumeAppEntry(this);
		return NULL;
	}
	
	return this;
}

appEntry_t *
_cti_findAppEntry(cti_app_id_t appId)
{
	appEntry_t *	this;
	
	// iterate through the _cti_my_apps list
	_cti_list_reset(_cti_my_apps);
	while ((this = (appEntry_t *)_cti_list_next(_cti_my_apps)) != NULL)
	{
		// return if the appId's match
		if (this->appId == appId)
			return this;
	}
	
	// if we get here, an entry for appId doesn't exist
	_cti_set_error("The appId %d is not registered.", (int)appId);
	return NULL;
}

int
_cti_refAppEntry(cti_app_id_t appId)
{
	appEntry_t *	this;
	
	// iterate through the _cti_my_apps list
	_cti_list_reset(_cti_my_apps);
	while ((this = (appEntry_t *)_cti_list_next(_cti_my_apps)) != NULL)
	{
		// inc refCnt if the appId's match
		if (this->appId == appId)
		{
			this->refCnt++;
			return 0;
		}
	}
	
	// if we get here, an entry for appId doesn't exist
	_cti_set_error("The appId %d is not registered.", (int)appId);
	return 1;
}

const cti_wlm_proto_t *
_cti_current_wlm_proto(void)
{
	return _cti_wlmProto;
}

/************************
* API defined functions
************************/

const char *
cti_version(void)
{
	return CTI_FE_VERSION;
}

cti_wlm_type
cti_current_wlm(void)
{
	return _cti_wlmProto->wlm_type;
}

int 
cti_setAttribute(cti_attr_type attrib, const char *value)
{
    switch (attrib)
    {
        case CTI_ATTR_STAGE_DEPENDENCIES:

            // Sanity
            if (value == NULL) 
            {
                _cti_set_error("CTI_ATTR_STAGE_DEPENDENCIES: NULL pointer for 'value'."); 
                return 1;
            }

            if (value[0] == '0')
            {
                _cti_setStageDeps(false);
                return 0;
            }
            if (value[0] == '1')
            {
                _cti_setStageDeps(true);
                return 0;
            }
            _cti_set_error("CTI_ATTR_STAGE_DEPENDENCIES: Unsupported value '%c'", value[0]); 
            return 1;

        default:
            _cti_set_error("Invalid cti_attr_type '%d'.", (int)attrib); 
            return 1;
    }

	return 0;
}

const char *
cti_wlm_type_toString(cti_wlm_type wlm_type)
{
	switch (wlm_type)
	{
		case CTI_WLM_ALPS:
			return "Cray ALPS";
			
		case CTI_WLM_CRAY_SLURM:
			return "Cray based SLURM";
	
		case CTI_WLM_SLURM:
			return "SLURM";

		case CTI_WLM_SSH:
			return "Fallback (SSH based) workload manager";
			
		case CTI_WLM_NONE:
			return "No WLM detected";
	}
	
	// Shouldn't get here
	return "Invalid WLM.";
}

int
cti_appIsValid(cti_app_id_t appId)
{
	appEntry_t *	this;

	// sanity check
	if (appId == 0)
		return 0;
		
	// find the appEntry_t obj
	if ((this = _cti_findAppEntry(appId)) == NULL)
		return 0;
		
	return 1;
}

void
cti_deregisterApp(cti_app_id_t appId)
{
	appEntry_t *	this;
	
	// sanity check
	if (appId == 0)
		return;
		
	// find the appEntry_t obj
	if ((this = _cti_findAppEntry(appId)) == NULL)
		return;
		
	// dec refCnt and ensure it is 0, otherwise return
	if (--(this->refCnt) > 0)
		return;
	
	// remove it from the list
	_cti_list_remove(_cti_my_apps, this);
	
	// free the appEntry_t obj
	_cti_consumeAppEntry(this);
}

int
cti_getNumAppPEs(cti_app_id_t appId)
{
	appEntry_t *	app_ptr;
	
	// sanity check
	if (appId == 0)
	{
		_cti_set_error("Invalid appId %d.", (int)appId);
		return 0;
	}
		
	// try to find an entry in the _cti_my_apps list for the apid
	if ((app_ptr = _cti_findAppEntry(appId)) == NULL)
	{
		// couldn't find the entry associated with the apid
		// error string already set
		return 0;
	}
	
	// Call the appropriate function based on the wlm
	return app_ptr->wlmProto->wlm_getNumAppPEs(app_ptr->_wlmObj);
}

int
cti_getNumAppNodes(cti_app_id_t appId)
{
	appEntry_t *	app_ptr;
	
	// sanity check
	if (appId == 0)
	{
		_cti_set_error("Invalid appId %d.", (int)appId);
		return 0;
	}
	
	// try to find an entry in the _cti_my_apps list for the apid
	if ((app_ptr = _cti_findAppEntry(appId)) == NULL)
	{
		// couldn't find the entry associated with the apid
		// error string already set
		return 0;
	}
	
	// Call the appropriate function based on the wlm
	return app_ptr->wlmProto->wlm_getNumAppNodes(app_ptr->_wlmObj);
}

char **
cti_getAppHostsList(cti_app_id_t appId)
{
	appEntry_t *	app_ptr;
	
	// sanity check
	if (appId == 0)
	{
		_cti_set_error("Invalid appId %d.", (int)appId);
		return 0;
	}
	
	// try to find an entry in the _cti_my_apps list for the apid
	if ((app_ptr = _cti_findAppEntry(appId)) == NULL)
	{
		// couldn't find the entry associated with the apid
		// error string already set
		return 0;
	}
	
	// Call the appropriate function based on the wlm
	return app_ptr->wlmProto->wlm_getAppHostsList(app_ptr->_wlmObj);
}

cti_hostsList_t *
cti_getAppHostsPlacement(cti_app_id_t appId)
{
	appEntry_t *	app_ptr;
	
	// sanity check
	if (appId == 0)
	{
		_cti_set_error("Invalid appId %d.", (int)appId);
		return 0;
	}
	
	// try to find an entry in the _cti_my_apps list for the apid
	if ((app_ptr = _cti_findAppEntry(appId)) == NULL)
	{
		// couldn't find the entry associated with the apid
		// error string already set
		return 0;
	}
	
	// Call the appropriate function based on the wlm
	return app_ptr->wlmProto->wlm_getAppHostsPlacement(app_ptr->_wlmObj);
}

void
cti_destroyHostsList(cti_hostsList_t *placement_list)
{
	// sanity check
	if (placement_list == NULL)
		return;
		
	if (placement_list->hosts != NULL)
		free(placement_list->hosts);
		
	free(placement_list);
}

char *
cti_getHostname()
{
	// call wlm proto function
	return _cti_wlmProto->wlm_getHostName();
}

char *
cti_getLauncherHostName(cti_app_id_t appId)
{
	appEntry_t *	app_ptr;
	
	// sanity check
	if (appId == 0)
	{
		_cti_set_error("Invalid appId %d.", (int)appId);
		return 0;
	}
	
	// try to find an entry in the _cti_my_apps list for the apid
	if ((app_ptr = _cti_findAppEntry(appId)) == NULL)
	{
		// couldn't find the entry associated with the apid
		// error string already set
		return 0;
	}
	
	// Call the appropriate function based on the wlm
	return app_ptr->wlmProto->wlm_getLauncherHostName(app_ptr->_wlmObj);
}


/* Noneness functions for wlm proto */

int
_cti_wlm_init_none(void)
{
	_cti_set_error("wlm_init() not supported for %s", cti_wlm_type_toString(_cti_wlmProto->wlm_type));
	return 1;
}

void
_cti_wlm_fini_none(void)
{
	_cti_set_error("wlm_fini() not supported for %s", cti_wlm_type_toString(_cti_wlmProto->wlm_type));
	return;
}

void
_cti_wlm_destroy_none(cti_wlm_obj a1)
{
	_cti_set_error("wlm_fini() not supported for %s", cti_wlm_type_toString(_cti_wlmProto->wlm_type));
	return;
}

char *
_cti_wlm_getJobId_none(cti_wlm_obj a1)
{
	_cti_set_error("wlm_getJobId() not supported for %s", cti_wlm_type_toString(_cti_wlmProto->wlm_type));
	return NULL;
}

cti_app_id_t
_cti_wlm_launch_none(const char * const a1[], int a2, int a3, const char *a4, const char *a5, const char * const a6[])
{
	_cti_set_error("wlm_launch() not supported for %s", cti_wlm_type_toString(_cti_wlmProto->wlm_type));
	return 0;
}

cti_app_id_t
_cti_wlm_launchBarrier_none(const char * const a1[], int a2, int a3, const char *a4, const char *a5, const char * const a6[])
{
	_cti_set_error("wlm_launchBarrier() not supported for %s", cti_wlm_type_toString(_cti_wlmProto->wlm_type));
	return 0;
}

int
_cti_wlm_releaseBarrier_none(cti_wlm_obj a1)
{
	_cti_set_error("wlm_releaseBarrier() not supported for %s", cti_wlm_type_toString(_cti_wlmProto->wlm_type));
	return 1;
}

int
_cti_wlm_killApp_none(cti_wlm_obj a1, int a2)
{
	_cti_set_error("wlm_killApp() not supported for %s", cti_wlm_type_toString(_cti_wlmProto->wlm_type));
	return 1;
}

const char * const *
_cti_wlm_extraBinaries_none(cti_wlm_obj a1)
{
	_cti_set_error("wlm_extraBinaries() not supported for %s", cti_wlm_type_toString(_cti_wlmProto->wlm_type));
	return NULL;
}

const char * const *
_cti_wlm_extraLibraries_none(cti_wlm_obj a1)
{
	_cti_set_error("wlm_extraLibraries() not supported for %s", cti_wlm_type_toString(_cti_wlmProto->wlm_type));
	return NULL;
}

const char * const *
_cti_wlm_extraLibDirs_none(cti_wlm_obj a1)
{
	_cti_set_error("wlm_extraLibDirs() not supported for %s", cti_wlm_type_toString(_cti_wlmProto->wlm_type));
	return NULL;
}

const char * const *
_cti_wlm_extraFiles_none(cti_wlm_obj a1)
{
	_cti_set_error("wlm_extraFiles() not supported for %s", cti_wlm_type_toString(_cti_wlmProto->wlm_type));
	return NULL;
}

int
_cti_wlm_shipPackage_none(cti_wlm_obj a1, const char *a2)
{
	_cti_set_error("wlm_shipPackage() not supported for %s", cti_wlm_type_toString(_cti_wlmProto->wlm_type));
	return 1;
}

int
_cti_wlm_startDaemon_none(cti_wlm_obj a1, cti_args_t *a2)
{
	_cti_set_error("wlm_startDaemon() not supported for %s", cti_wlm_type_toString(_cti_wlmProto->wlm_type));
	return 1;
}

int
_cti_wlm_getNumAppPEs_none(cti_wlm_obj a1)
{
	_cti_set_error("wlm_getNumAppPEs() not supported for %s", cti_wlm_type_toString(_cti_wlmProto->wlm_type));
	return 0;
}

int
_cti_wlm_getNumAppNodes_none(cti_wlm_obj a1)
{
	_cti_set_error("wlm_getNumAppNodes() not supported for %s", cti_wlm_type_toString(_cti_wlmProto->wlm_type));
	return 0;
}

char **
_cti_wlm_getAppHostsList_none(cti_wlm_obj a1)
{
	_cti_set_error("wlm_getAppHostsList() not supported for %s", cti_wlm_type_toString(_cti_wlmProto->wlm_type));
	return NULL;
}

cti_hostsList_t *
_cti_wlm_getAppHostsPlacement_none(cti_wlm_obj a1)
{
	_cti_set_error("wlm_getAppHostsPlacement() not supported for %s", cti_wlm_type_toString(_cti_wlmProto->wlm_type));
	return NULL;
}

char *
_cti_wlm_getHostName_none(void)
{
	_cti_set_error("wlm_getHostName() not supported for %s", cti_wlm_type_toString(_cti_wlmProto->wlm_type));
	return NULL;
}

char *
_cti_wlm_getLauncherHostName_none(cti_wlm_obj a1)
{
	_cti_set_error("wlm_getLauncherHostName() not supported for %s", cti_wlm_type_toString(_cti_wlmProto->wlm_type));
	return NULL;
}

const char *
_cti_wlm_getToolPath_none(cti_wlm_obj a1)
{
	_cti_set_error("wlm_getToolPath() not supported for %s", cti_wlm_type_toString(_cti_wlmProto->wlm_type));
	return NULL;
}

const char *
_cti_wlm_getAttribsPath_none(cti_wlm_obj a1)
{
	_cti_set_error("wlm_getAttribsPath() not supported for %s", cti_wlm_type_toString(_cti_wlmProto->wlm_type));
	return NULL;
}

