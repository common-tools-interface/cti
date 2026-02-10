/******************************************************************************\
 * common_tools_shared.h - Shared type definitions shared between the FE and
 *                         BE API.
 *
 * Copyright 2011-2020 Hewlett Packard Enterprise Development LP.
 * SPDX-License-Identifier: Linux-OpenIB
 ******************************************************************************/

#ifndef _COMMON_TOOLS_SHARED_H
#define _COMMON_TOOLS_SHARED_H

#include <stdint.h>
#include <sys/types.h>

/*
 * The common tools interface can read environment variables about the system
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
 *      specific workload manager.
 *          Supported WLM configurations:
 *          - Shasta / Slurm: "shasta/slurm"
 *          - Shasta / PALS:  "shasta/pals"
 *          - HPCM / Slurm:   "hpcm/slurm"
 *          - HPCM / PALS:    "hpcm/pals"
 *          - HPCM / Flux:    "hpcm/flux"
 *          - XC / Slurm:     "xc/slurm"
 *          - XC / ALPS:      "xc/alps"
 *          - CS / mpiexec:   "cs/mpiexec"
 *          - SSH with MPIR-compliant launcher: "linux/ssh"
 *
 * CTI_LAUNCHER_SCRIPT_ENV_VAR (optional)
 *
 *     If set, CTI will assume on Slurm systems that `srun` is
 *     overridden by a shell script at this path. This is commonly
 *     used with analysis tools such as Xalt. CTI will attempt to
 *     automatically detect and apply this case, but if it is not
 *     recognizing that `srun` is wrapped in a script, set this
 *     value to manually enable script launch mode.
 * 
 * CTI_LAUNCHER_WRAPPER_ENV_VAR (optional)
 * 
 *     If set, CTI app launches under slurm will be launched wrapped in the
 *     specified program. The wrapper must eventually make its own call to srun,
 *     forwarding its passed arguments to the actual launcher.
 *     
 *     Arguments can be passed to the wrapper by including them in the
 *     environment variable string. To pass an argument that includes spaces,
 *     surround the argument in quotes. To pass an argument that includes
 *     quotes, escape the quotes with \.
 * 
 *     .e.g CTI_LAUNCHER_WRAPPER='spindle --pull'
 *          cti_launchApp({"a.out"}, ...)
 *          -> 'spindle', '--pull', 'srun', 'a.out'
 *     
 *     .e.g CTI_LAUNCHER_WRAPPER='logger "\"quotes\" and spaces"'
 *          cti_launchApp({"a.out"}, ...)
 *         -> 'logger', '"quotes" and spaces', 'srun', 'a.out' (argc = 4)
 *
 * CTI_BACKEND_WRAPPER_ENV_VAR (optional)
 *
 *     Job launches may be running underneath a wrapper binary on the
 *     backend. For example, running each rank of a job inside a
 *     Singularity container. By setting the CTI_BACKEND_WRAPPER
 *     environment variable to the name of the wrapper binary
 *     (such as `singularity` for Singularity containers), CTI will
 *     treat the first child process of each wrapper instance as the
 *     true process for that rank of the job. Job ranks not running as
 *     the wrapper binary will not be changed.
 *     Note: currently supported only for the Slurm WLM.
 *
 * CTI_BACKEND_TMPDIR_ENV_VAR (optional)
 *
 *     Each workload manager has a default location to place temporary
 *     tool files such as tool daemons. This location can be overridden
 *     by setting this environment variable to a location that exists
 *     on the compute nodes. Note that the filesystem should be mounted
 *     such that binaries can execute from that location i.e. not noexec.
 *
 */
#define CTI_BASE_DIR_ENV_VAR         "CTI_INSTALL_DIR"
#define CTI_LOG_DIR_ENV_VAR          "CTI_LOG_DIR"
#define CTI_DBG_ENV_VAR              "CTI_DEBUG"
#define CTI_CFG_DIR_ENV_VAR          "CTI_CFG_DIR"
#define CTI_LAUNCHER_SCRIPT_ENV_VAR  "CTI_LAUNCHER_SCRIPT"
#define CTI_LAUNCHER_NAME_ENV_VAR    "CTI_LAUNCHER_NAME"
#define CTI_WLM_IMPL_ENV_VAR         "CTI_WLM_IMPL"
#define CTI_LAUNCHER_WRAPPER_ENV_VAR "CTI_LAUNCHER_WRAPPER"
#define CTI_BACKEND_WRAPPER_ENV_VAR  "CTI_BACKEND_WRAPPER"
#define CTI_BACKEND_TMPDIR_ENV_VAR   "CTI_BACKEND_TMPDIR"
#define CTI_CONTAINER_INSTANCE_ENV_VAR "CTI_CONTAINER_INSTANCE"
// CTI_WLM_TYPE_<type>_STR recognized by CTI_WLM_IMPL_ENV_VAR and corresponds
// to values in the cti_wlm_type_t enum.
// Note: users should not manualy set CTI_WLM_IMPL environment variable to
// "none" or "mock" as these workload manager types are for internal use only.
#define CTI_WLM_TYPE_SLURM_STR     	"slurm"
#define CTI_WLM_TYPE_ALPS_STR       "alps"
#define CTI_WLM_TYPE_SSH_STR   		"generic"
#define CTI_WLM_TYPE_PALS_STR       "pals"
#define CTI_WLM_TYPE_FLUX_STR       "flux"
#define CTI_WLM_TYPE_LOCALHOST_STR  "localhost"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * cti_wlm_type_t is used to denote the workload manager in use on the system.
 */
typedef enum
{
    CTI_WLM_NONE,   // error/unitialized state
    CTI_WLM_MOCK,   // Used for unit testing
    CTI_WLM_SLURM,  // SLURM implementation
    CTI_WLM_SSH,    // Direct SSH implementation
    CTI_WLM_ALPS,   // ALPS implementation
    CTI_WLM_PALS,   // PALS implementation
    CTI_WLM_FLUX,   // Flux implementation
    CTI_WLM_LOCALHOST  // Localhost implementation
} cti_wlm_type_t;

#ifdef __cplusplus
}
#endif

#endif /* _COMMON_TOOLS_SHARED_H */
