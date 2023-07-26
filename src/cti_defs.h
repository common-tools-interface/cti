/******************************************************************************\
 * cti_defs.h - A header file for common compile time defines.
 *
 * NOTE: These defines are used throughout the internal code base and are all
 *       placed inside this file to make modifications due to WLM changes
 *       easier.
 *
 * Copyright 2013-2023 Hewlett Packard Enterprise Development LP.
 * SPDX-License-Identifier: Linux-OpenIB
 ******************************************************************************/

#ifndef _CTI_DEFS_H
#define _CTI_DEFS_H

#include "common_tools_shared.h"

// We use macros defined by configure in this file. So we need to get access to
// config.h. Since that doesn't have good macro guards, and this file does, it
// is important to include cti_defs.h in every source file. This ensures configure
// macros get pulled in as well.
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <limits.h>
#include <stdint.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/*******************************************************************************
** Generic defines
*******************************************************************************/
#define CTI_BUF_SIZE            4096
#define CTI_ERR_STR_SIZE        1024
#define DEFAULT_ERR_STR         "Unknown CTI error"

/*******************************************************************************
** Frontend defines relating to the login node
*******************************************************************************/
#define WLM_DETECT_LIB_NAME "libwlm_detect.so" // wlm_detect library
#define LD_AUDIT_LIB_NAME       "libctiaudit.so"                       // ld audit library

/*******************************************************************************
** Backend defines relating to the compute node
*******************************************************************************/
// The following needs the 'X' for random char replacement.
#define STAGE_DIR_PREFIX                    "cti_daemon"            // default directory name for the fake root of the tool daemon
#define PMI_ATTRIBS_FILE_NAME               "pmi_attribs"           // Name of the pmi_attribs file to find pid info
#define PMI_ATTRIBS_DEFAULT_FOPEN_TIMEOUT   60ul                    // default timeout in seconds for trying to open pmi_attribs file
#define PID_FILE                            ".cti_pids"             // Name of the file containing the pids of the tool daemon processes

/*******************************************************************************
** Cray System information
*******************************************************************************/
#define CRAY_XT_NID_FILE         "/proc/cray_xt/nid"                // file where nid info is located on XT/XC systems
#define CRAY_SHASTA_NID_FILE     "/etc/cray/nid"                    // file where nid info is located on Shasta systems
#define CRAY_SHASTA_UAN_XNAME_FILE "/etc/cray/xname"                // file where NMN hostname is located on Shasta UANs

/*******************************************************************************
** SLURM specific information
*******************************************************************************/
// Used when reading/writing layout file - used on FE and BE
// File will begin with the following header
typedef struct
{
    int numNodes;
}   slurmLayoutFileHeader_t;
// Followed by numNodes of the following:
typedef struct
{
    char    host[HOST_NAME_MAX];    // hostname of this node
    int     PEsHere;    // Number of PEs placed on this node
    int     firstPE;    // first PE on this node
}   slurmLayoutFile_t;

// Used when reading/writing pid file - used on FE and BE
// File will begin with the following header
typedef struct
{
    int numPids;
}   slurmPidFileHeader_t;
// Followed by numPids of the following:
typedef struct
{
    pid_t   pid;        // pid_t of this PE
}   slurmPidFile_t;

#define SRUN                    "srun"                              // name of slurm job launcher binary
#define SATTACH                 "sattach"                           // name of slurm io redirect binary
#define SCANCEL                 "scancel"                           // name of slurm job signal binary
#define SBCAST                  "sbcast"                            // name of slurm transfer binary
#define SACCTMGR                "sacctmgr"                          // name of slurm configuration binary
#define SLURM_JOB_NAME          "SLURM_JOB_NAME"                    // Environment variable for Slurm job name
#define SLURM_APID(jobid, stepid)  ((stepid * 10000000000) + jobid) // formula for creating Cray apid from SLURM jobid.stepid
#define SLURM_TOOL_DIR          "/tmp"                              // SLURM staging path on compute node
#define SLURM_CRAY_DIR          "/var/opt/cray/alps/spool/%llu"     // Location of cray specific directory on compute node
#define SLURM_STAGE_DIR         "slurmXXXXXX"                       // directory name for staging slurm specific files to transfer
#define SLURM_LAYOUT_FILE       "slurm_layout"                      // name of file containing layout information
#define SLURM_PID_FILE          "slurm_pid"                         // name of file containing pid information
#define SLURM_DAEMON_GRES_ENV_VAR "CTI_SLURM_DAEMON_GRES"           // Set to specify `--gres` argument for tool daemon launches (or leave blank to disable)
#define SLURM_OVERRIDE_MC_ENV_VAR "CTI_SLURM_OVERRIDE_MC"           // Set to disable Slurm multi-cluster check
#define SLURM_NEVER_PARSE_SCANCEL                                              \
  "CTI_SLURM_NEVER_PARSE_SCANCEL" // Due to a slurm bug
                                  // (https://bugs.schedmd.com/show_bug.cgi?id=16551),
                                  // the return code of the scancel command is
                                  // not reliable enough to determine if a
                                  // cti_killApp was successful. A workaround is
                                  // implemented which parses the verbose output
                                  // of scancel for confirmation. Set this
                                  // environment variable to disable the
                                  // workaround and rely soley on the scancel
                                  // return code.

/*******************************************************************************
** SSH specific information
*******************************************************************************/
// Re-use types defined above
typedef slurmLayoutFileHeader_t cti_layoutFileHeader_t;
typedef slurmLayoutFile_t       cti_layoutFile_t;
typedef slurmPidFileHeader_t    cti_pidFileheader_t;
typedef slurmPidFile_t          cti_pidFile_t;

#define CLUSTER_FILE_TEST   "/etc/redhat-release"
#define SSH_STAGE_DIR       SLURM_STAGE_DIR
#define SSH_LAYOUT_FILE     SLURM_LAYOUT_FILE
#define SSH_PID_FILE        SLURM_PID_FILE
#define SSH_TOOL_DIR        SLURM_TOOL_DIR
#define SSH_DIR_ENV_VAR     "CTI_SSH_DIR"
#define SSH_KNOWNHOSTS_PATH_ENV_VAR     "CTI_SSH_KNOWNHOSTS_PATH"
#define SSH_PASSPHRASE_ENV_VAR      "CTI_SSH_PASSPHRASE"
#define SSH_PRIKEY_PATH_ENV_VAR     "CTI_SSH_PRIKEY_PATH"
#define SSH_PUBKEY_PATH_ENV_VAR     "CTI_SSH_PUBKEY_PATH"

#define LOCALHOST_PID_FILE  SLURM_PID_FILE
#define LOCALHOST_TOOL_DIR  SLURM_TOOL_DIR
#define LOCALHOST_STAGE_DIR SLURM_STAGE_DIR


/*******************************************************************************
** ALPS specific information
*******************************************************************************/

#define APRUN						"aprun"												// name of the ALPS job launcher binary
#define OLD_APRUN_LOCATION		"/usr/bin/aprun"									// default legacy location of the aprun binary
#define OBS_APRUN_LOCATION		"/opt/cray/alps/default/bin/aprun"				// default new location of the aprun binary
#define APKILL						"apkill"											// name of the ALPS job kill binary
#define ALPS_XT_NID				"/proc/cray_xt/nid"								// file where nid info is located
#define ALPS_XT_HOSTNAME_FMT		"nid%05d"											// hostname format string
#define ALPS_OBS_LOC				"/opt/cray/alps"									// used to test if the system is using the OBS format
#define ALPS_FE_LIB_NAME			"libalps.so"										// name of the alps library used on the frontend
#define ALPS_BE_LIB_NAME			"libalpsutil.so"									// name of the alps library used on the backend
#define OLD_TOOLHELPER_DIR		"/var/spool/alps/%llu/toolhelper%llu"			// old alps toolhelper path format on compute node
#define OBS_TOOLHELPER_DIR		"/var/opt/cray/alps/spool/%llu/toolhelper%llu"// new alps toolhelper path format on compute node
#define OLD_ATTRIBS_DIR			"/var/spool/alps/%llu"							// old pmi_attribs path format on compute node
#define OBS_ATTRIBS_DIR			"/var/opt/cray/alps/spool/%llu"					// new pmi_attribs path format on compute node
#define SHELL_ENV_VAR							"SHELL"									// The environment variable to set shell info
#define SHELL_PATH							"/bin/sh"								// The location of the shell to set SHELL to
#define USER_DEF_APRUN_LOC_ENV_VAR "CTI_APRUN_PATH"

/*
** PALS specific information
*/

#define PALS_BE_LIB_NAME "libpals.so" // name of the PALS library used on the backend
#define PALS_EXEC_HOST "CTI_PALS_EXEC_HOST" // To use PALS application ID for attaching outside of job's PBS allocation
// In PALS 1.2.3, there is a race condition between the tool launcher
// releasing a job from the startup barrier and the job actually getting to the
// startup barrier. This can result in the job receiving the startup barrier
// release signal before it actually arrives there, resulting in the job getting
// stuck in the barrier. As a workaround, this environment variable can be set
// to add a delay between job startup and barrier release. If set to a positve
// integer n, CTI will wait n seconds between starting a job and releasing it
// from the barrier on PALS. A delay as small as one second can work.
#define PALS_BARRIER_RELEASE_DELAY "CTI_PALS_BARRIER_RELEASE_DELAY"

/*******************************************************************************
** Flux specific information
*******************************************************************************/

#define FLUX_URI "FLUX_URI"
#define FLUX_INSTALL_DIR_ENV_VAR "FLUX_INSTALL_DIR"
#define LIBFLUX_PATH_ENV_VAR "LIBFLUX_PATH"
#define LIBFLUX_NAME "libflux-core.so.2"
#define CTI_FLUX_DEBUG_ENV_VAR "CTI_FLUX_DEBUG" // Bypass version mismatch errors

/*
** HPCM specific information
*/


/*******************************************************************************
** Environment variables that are set/maintained by this library
** Note that (read) is used to denote environment variables that the user will
** define and the library reads in, and (set) are environment variables that
** are set in the users environment by the library.
*******************************************************************************/

// Internal overrides for WLM workarounds
#define SRUN_OVERRIDE_ARGS_ENV_VAR   "CTI_SRUN_OVERRIDE"    // Frontend: replace variable SRUN arguments with these given arguments (read)
#define SRUN_APPEND_ARGS_ENV_VAR     "CTI_SRUN_APPEND"      // Frontend: append these arguments to the variable list of SRUN arguments (read)
#define CTI_HOST_ADDRESS_ENV_VAR     "CTI_HOST_ADDRESS"     // Frontend: override detection of host IP address
#define CTI_DEDUPLICATE_FILES_ENV_VAR "CTI_DEDUPLICATE_FILES" // Frontend: ship all files to backends, even if available

// Backend related env vars
#define BE_GUARD_ENV_VAR    "CTI_IAMBACKEND"        //Backend: Set by the daemon launcher to ensure proper setup
#define APID_ENV_VAR        "CTI_APID"              //Backend: Used to hold the string representation of the apid (set)
#define WLM_ENV_VAR         "CTI_WLM"               //Backend: Used to hold the enum representation of the wlm (set)
#define SCRATCH_ENV_VAR     "TMPDIR"                //Backend: Used to denote temporary storage space (set)
#define OLD_SCRATCH_ENV_VAR "CTI_OLD_TMPDIR"        //Backend: Used to denote the old setting of TMPDIR (set)
#define OLD_CWD_ENV_VAR     "CTI_OLD_CWD"           //Backend: Used to denote the old setting of CWD (set)
#define TOOL_DIR_VAR        "CTI_TOP_LEVEL"         //Backend: KEEP HIDDEN! Used to point at the top level toolhelper dir (set)
#define ROOT_DIR_VAR        "CTI_ROOT_DIR"          //Backend: Used to denote the fake root of the tool daemon (set)
#define BIN_DIR_VAR         "CTI_BIN_DIR"           //Backend: Used to denote where binaries are located (set)
#define LIB_DIR_VAR         "CTI_LIB_DIR"           //Backend: Used to denote where libraries are located (set)
#define FILE_DIR_VAR        "CTI_FILE_DIR"          //Backend: Used to denote where files are located (set)
#define PMI_ATTRIBS_DIR_VAR "CTI_PMI_ATTRIBS_DIR"   //Backend: Used to denote where the pmi_attribs file is located (set)
#define PMI_ATTRIBS_TIMEOUT_VAR "CTI_PMI_FOPEN_TIMEOUT" //Backend: Used to define a sleep timeout period for creation of pmi_attribs file (read)
#define PMI_EXTRA_SLEEP_VAR "CTI_PMI_EXTRA_SLEEP"   //Backend: Used to sleep a fixed period of time after the pmi_attribs file has been opened (read)

#ifdef __cplusplus
}

/*
** C++ only definitions used by frontend below.
*/
namespace cti {

// Default install directories on cray systems
static constexpr const char* const default_dir_locs[] = {
    "/opt/cray/pe/cti/" CTI_RELEASE_VERSION,
    "/opt/cray/cti/" CTI_RELEASE_VERSION,
    nullptr
};

} /* namespace cti */

#endif /* __cpluplus */

#endif /* _CTI_DEFS_H */
