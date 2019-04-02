/******************************************************************************\
 * cti_defs.h - A header file for common defines.
 *
 * NOTE: These defines are used throughout the internal code base and are all
 *       placed inside this file to make modifications due to WLM changes
 *       easier. The environment variables here should match those found in the
 *       public cray_tools_be.h and cray_tools_fe.h headers.
 *
 * Copyright 2013-2019 Cray Inc.    All Rights Reserved.
 *
 * Unpublished Proprietary Information.
 * This unpublished work is protected to trade secret, copyright and other laws.
 * Except as permitted by contract or express written permission of Cray Inc.,
 * no part of this work or its content may be used, reproduced or disclosed
 * in any form.
 *
 ******************************************************************************/

#ifndef _CTI_DEFS_H
#define _CTI_DEFS_H

#include <limits.h>
#include <stdint.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 *  This enum enumerates the various attributes that
 *  can be set by cti_setAttribute.
 */
enum cti_attr_type
{
    CTI_ATTR_STAGE_DEPENDENCIES     // Define whether binary and library
                                    // dependencies should be automatically
                                    // staged by cti_addManifestBinary and
                                    // cti_addManifestLIbrary: 0 or 1
                                    // Defaults to 1.
};
typedef enum cti_attr_type  cti_attr_type;

// WLM identifier. This is system specific. Right now only one WLM at a time
// is supported.
enum cti_wlm_type
{
    CTI_WLM_NONE,   // error/unitialized state
    CTI_WLM_CRAY_SLURM,
    CTI_WLM_SSH,
    CTI_WLM_MOCK // for unit testing only
};
typedef enum cti_wlm_type   cti_wlm_type;

enum cti_be_wlm_type
{
    CTI_BE_WLM_NONE,    // error/unitialized state
    CTI_BE_WLM_CRAY_SLURM,
    CTI_BE_WLM_SSH
};
typedef enum cti_be_wlm_type    cti_be_wlm_type;

/*******************************************************************************
** Generic defines
*******************************************************************************/
#define CTI_BUF_SIZE            4096
#define DEFAULT_ERR_STR         "Unknown CTI error"

/*******************************************************************************
** Frontend defines relating to the login node
*******************************************************************************/
#define CTI_OVERWATCH_BINARY    "cti_overwatch"                     // name of the overwatch binary
#define DEFAULT_SIG             9                                   // default signal value to use
#define LD_AUDIT_LIB_NAME       "libaudit.so"                       // ld audit library

/*******************************************************************************
** Backend defines relating to the compute node
*******************************************************************************/
// The following needs the 'X' for random char replacement.
#define DEFAULT_STAGE_DIR                   "cti_daemonXXXXXX"      // default directory name for the fake root of the tool daemon
#define SHELL_ENV_VAR                       "SHELL"                 // The environment variable to set shell info
#define SHELL_PATH                          "/bin/sh"               // The location of the shell to set SHELL to
#define PMI_ATTRIBS_FILE_NAME               "pmi_attribs"           // Name of the pmi_attribs file to find pid info
#define PMI_ATTRIBS_DEFAULT_FOPEN_TIMEOUT   60                      // default timeout in seconds for trying to open pmi_attribs file
#define PID_FILE                            ".cti_pids"             // Name of the file containing the pids of the tool daemon processes

/*******************************************************************************
** System information
*******************************************************************************/
#define CRAY_NID_FILE           "/proc/cray_xt/nid"                 // file where nid info is located
#define CRAY_HOSTNAME_FMT       "nid%05d"                           // NID based hostname format string

/*******************************************************************************
** Cray SLURM specific information
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

typedef slurmLayoutFileHeader_t cti_layoutFileHeader_t;
typedef slurmLayoutFile_t       cti_layoutFile_t;
typedef slurmPidFileHeader_t    cti_pidFileheader_t;
typedef slurmPidFile_t          cti_pidFile_t;

#define SRUN                    "srun"                                      // name of slurm job launcher binary
#define SATTACH                 "sattach"                                   // name of slurm io redirect binary
#define SCANCEL                 "scancel"                                   // name of slurm job signal binary
#define SBCAST                  "sbcast"                                    // name of slurm transfer binary
#define SLURM_STEP_UTIL         "cti_slurm_util"                            // name of cti slurm job step info utility
#define CRAY_SLURM_APID(jobid, stepid)  ((stepid * 10000000000) + jobid)    // formula for creating Cray apid from SLURM jobid.stepid
#define CRAY_SLURM_TOOL_DIR     "/tmp"                                      // Cray SLURM staging path on compute node
#define CRAY_SLURM_CRAY_DIR     "/var/opt/cray/alps/spool/%llu"             // Location of cray specific directory on compute node - pmi_attribs is here
#define SLURM_STAGE_DIR         "slurmXXXXXX"                               // directory name for staging slurm specific files to transfer
#define SLURM_LAYOUT_FILE       "slurm_layout"                              // name of file containing layout information
#define SLURM_PID_FILE          "slurm_pid"                                 // name of file containing pid information

/*******************************************************************************
** SSH specific information
*******************************************************************************/
#define CLUSTER_FILE_TEST   "/etc/redhat-release"
#define SSH_STAGE_DIR       SLURM_STAGE_DIR
#define SSH_LAYOUT_FILE     SLURM_LAYOUT_FILE
#define SSH_PID_FILE        SLURM_PID_FILE
#define SSH_TOOL_DIR        CRAY_SLURM_TOOL_DIR

/*******************************************************************************
** Environment variables that are set/maintained by this library
** Note that (read) is used to denote environment variables that the user will
** define and the library reads in, and (set) are environment variables that
** are set in the users environment by the library.
*******************************************************************************/
#define BASE_DIR_ENV_VAR    "CRAY_CTI_DIR"              //Frontend: Used to define the base install location (read)
#define CFG_DIR_VAR         "CRAY_CTI_CFG_DIR"          //Frontend: Used to define a writable location to create the manifest tarball (read)
#define DAEMON_STAGE_VAR    "CRAY_CTI_STAGE_DIR"        //Frontend: Used to define a directory name for the fake root of the tool daemon (read)
#define DBG_LOG_ENV_VAR     "CRAY_DBG_LOG_DIR"          //Frontend: Used to define a directory to write debug logs to (read)
#define DBG_ENV_VAR         "CRAY_CTI_DBG"              //Frontend: Used to turn on debug logging to files (read)
#define CTI_LAUNCHER_NAME   "CRAY_CTI_LAUNCHER_NAME"    //Frontend: Used to explicitly tell CTI the path to the launcher binary to use
#define CTI_WLM             "CRAY_CTI_WLM"              //Frontend: Used to explicitly tell CTI which workload manager to use. Accepts "slurm", and "generic"


#define BE_GUARD_ENV_VAR    "CRAYTOOL_IAMBACKEND"       //Backend: Set by the daemon launcher to ensure proper setup
#define APID_ENV_VAR        "CRAYTOOL_APID"             //Backend: Used to hold the string representation of the apid (set)
#define WLM_ENV_VAR         "CRAYTOOL_WLM"              //Backend: Used to hold the enum representation of the wlm (set)
#define SCRATCH_ENV_VAR     "TMPDIR"                    //Backend: Used to denote temporary storage space (set)
#define OLD_SCRATCH_ENV_VAR "CRAYTOOL_OLD_TMPDIR"       //Backend: Used to denote the old setting of TMPDIR (set)
#define OLD_CWD_ENV_VAR     "CRAYTOOL_OLD_CWD"          //Backend: Used to denote the old setting of CWD (set)
#define TOOL_DIR_VAR        "CRAYTOOL_TOP_LEVEL"        //Backend: KEEP HIDDEN! Used to point at the top level toolhelper dir (set)
#define ROOT_DIR_VAR        "CRAYTOOL_ROOT_DIR"         //Backend: Used to denote the fake root of the tool daemon (set)
#define BIN_DIR_VAR         "CRAYTOOL_BIN_DIR"          //Backend: Used to denote where binaries are located (set)
#define LIB_DIR_VAR         "CRAYTOOL_LIB_DIR"          //Backend: Used to denote where libraries are located (set)
#define FILE_DIR_VAR        "CRAYTOOL_FILE_DIR"         //Backend: Used to denote where files are located (set)
#define PMI_ATTRIBS_DIR_VAR "CRAYTOOL_PMI_ATTRIBS_DIR"  //Backend: Used to denote where the pmi_attribs file is located (set)
#define PMI_ATTRIBS_TIMEOUT_ENV_VAR "CRAY_CTI_PMI_FOPEN_TIMEOUT"    //Backend: Used to define a sleep timeout period for creation of pmi_attribs file (read)
#define PMI_EXTRA_SLEEP_ENV_VAR     "CRAY_CTI_PMI_EXTRA_SLEEP"      //Backend: Used to sleep a fixed period of time after the pmi_attribs file has been opened (read)

#ifdef __cplusplus
}
#endif

/*
** C++ only definitions below.
*/
#ifdef __cplusplus

#include "useful/cti_argv.hpp"

struct DaemonArgv : public cti_argv::Argv {
    using Option    = cti_argv::Argv::Option;
    using Parameter = cti_argv::Argv::Parameter;

    static constexpr Option Clean { "clean", 'c' };
    static constexpr Option Help  { "help",  'h' };
    static constexpr Option Debug { "debug",  1 };

    static constexpr Parameter ApID           { "apid",      'a' };
    static constexpr Parameter Binary         { "binary",    'b' };
    static constexpr Parameter Directory      { "directory", 'd' };
    static constexpr Parameter EnvVariable    { "env",       'e' };
    static constexpr Parameter InstSeqNum     { "inst",      'i' };
    static constexpr Parameter ManifestName   { "manifest",  'm' };
    static constexpr Parameter ToolPath       { "path",      'p' };
    static constexpr Parameter PMIAttribsPath { "apath",     't' };
    static constexpr Parameter LdLibraryPath  { "ldlibpath", 'l' };
    static constexpr Parameter WLMEnum        { "wlm",       'w' };

    static constexpr GNUOption long_options[] = {
        Clean, Help, Debug,
        ApID, Binary, Directory, EnvVariable, InstSeqNum, ManifestName, ToolPath,
            PMIAttribsPath, LdLibraryPath, WLMEnum,
    long_options_done };
};

struct SattachArgv : public cti_argv::Argv {
    using Option    = cti_argv::Argv::Option;
    using Parameter = cti_argv::Argv::Parameter;

    static constexpr Option PrependWithTaskLabel { "label",    1 };
    static constexpr Option DisplayLayout        { "layout",   2 };
    static constexpr Option RunInPty             { "pty",      3 };
    static constexpr Option QuietOutput          { "quiet",    4 };
    static constexpr Option VerboseOutput        { "verbose",  5 };

    static constexpr Parameter InputFilter  { "input-filter",  6 };
    static constexpr Parameter OutputFilter { "output-filter", 7 };
    static constexpr Parameter ErrorFilter  { "error-filter",  8 };


    static constexpr GNUOption long_options[] = {
        InputFilter, OutputFilter, ErrorFilter,
        PrependWithTaskLabel, DisplayLayout, RunInPty, QuietOutput, VerboseOutput,
    long_options_done };
};

// XXX: flaw in C++11 relating to static constexpr. can be removed in C++17
#ifdef INSIDE_WORKAROUND_OBJ

constexpr cti_argv::Argv::GNUOption cti_argv::Argv::long_options_done;

using Option    = cti_argv::Argv::Option;
using Parameter = cti_argv::Argv::Parameter;
using SA        = SattachArgv;
using DA        = DaemonArgv;

constexpr Option            SA::PrependWithTaskLabel, SA::DisplayLayout, SA::RunInPty,
                            SA::QuietOutput, SA::VerboseOutput;
constexpr Parameter         SA::InputFilter, SA::OutputFilter, SA::ErrorFilter;
constexpr cti_argv::Argv::GNUOption SA::long_options[];

constexpr Option            DA::Clean, DA::Help, DA::Debug;
constexpr Parameter         DA::ApID, DA::Binary, DA::Directory, DA::EnvVariable,
                            DA::InstSeqNum, DA::ManifestName, DA::ToolPath,
                            DA::PMIAttribsPath, DA::LdLibraryPath, DA::WLMEnum;
constexpr cti_argv::Argv::GNUOption DA::long_options[];

#endif /* INSIDE_WORKAROUND_OBJ */

#endif /* __cpluplus */

#endif /* _CTI_DEFS_H */
