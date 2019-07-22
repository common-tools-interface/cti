/******************************************************************************\
 * cray_tools_shared.h - Shared type definitions shared between the FE and BE API
 *
 * Copyright 2011-2019 Cray Inc.  All Rights Reserved.
 *
 * Unpublished Proprietary Information.
 * This unpublished work is protected to trade secret, copyright and other laws.
 * Except as permitted by contract or express written permission of Cray Inc.,
 * no part of this work or its content may be used, reproduced or disclosed
 * in any form.
 *
 ******************************************************************************/

#ifndef _CRAY_TOOLS_SHARED_H
#define _CRAY_TOOLS_SHARED_H

#include <stdint.h>
#include <sys/types.h>

/*
 * The Cray tools interface can read environment variables about the system
 * configuration dynamically at run time. The environment variables that are
 * read are defined here.  Note that the value of these environment
 * variables are subject to change. Use the defines to guarantee portability.
 *
 * CTI_BASE_DIR_ENV_VAR (optional)
 *
 *      Used to define the absolute path to the CTI install directory. This
 *      can also be hardcoded at build time. Use for relocatable installs.
 *
 * CTI_LOG_DIR_ENV_VAR (optional)
 *
 *      Used to define a path to write log files to. This location must be
 *      cross mounted and accessible by the compute nodes in order to receive
 *      debug logs from tool daemons. If CTI_DBG_ENV_VAR is set, and this env
 *      variable is omitted, then the log files will be written to /tmp on
 *      the compute nodes. The CTI_LOG_DIR attribute overrides this environment
 *      variable.
 *
 * CTI_DBG_ENV_VAR (optional)
 *
 *      Used to turn on redirection of tool daemon stdout/stderr to a log
 *      file. This should be used in conjuntion with CTI_LOG_DIR_ENV_VAR.
 *
 * CTI_CFG_DIR_ENV_VAR (optional)
 *
 *      Used to define a location to create a directory used to write
 *      internal temporary files to on the frontend. This directory must
 *      have permissions set to 0700. If not set, the default is to check
 *      $TMPDIR, /tmp, and $HOME respectively.
 *
 * CTI_LAUNCHER_NAME_ENV_VAR (optional)
 *
 *      Used to define the name of the application launcher. Overrides the
 *      default job launcher for the workload manager in use on the system.
 *      For example, if running on a slurm system this environment variable
 *      can be set to "mpiexec" to override the use of srun.
 *
 * CTI_WLM_IMPL_ENV_VAR (optional)
 *
 *      Used to override internal detection logic of the workload manager in
 *      use on the system. This can be used to force CTI to instantiate a
 *      specific workload manager. Set this environment variable to the
 *      corresponding string for each of the cti_wlm_type_t defined below:
 *          CTI_WLM_CRAY_SLURM set to "slurm"
 *          CTI_WLM_SSH set to "generic"
 *
 */
#define CTI_BASE_DIR_ENV_VAR        "CTI_INSTALL_DIR"
#define CTI_LOG_DIR_ENV_VAR         "CTI_LOG_DIR"
#define CTI_DBG_ENV_VAR             "CTI_DEBUG"
#define CTI_CFG_DIR_ENV_VAR         "CTI_CFG_DIR"
#define CTI_LAUNCHER_NAME_ENV_VAR   "CTI_LAUNCHER_NAME"
#define CTI_WLM_IMPL_ENV_VAR        "CTI_WLM_IMPL"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    CTI_WLM_NONE,       // error/unitialized state
    CTI_WLM_CRAY_SLURM, // SLURM implementation
    CTI_WLM_SSH,        // Direct SSH implementation
    CTI_WLM_MOCK        // Used for unit testing
} cti_wlm_type_t;

#ifdef __cplusplus
}
#endif

#endif /* _CRAY_TOOLS_SHARED_H */
