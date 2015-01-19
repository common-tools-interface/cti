/******************************************************************************\
 * cray_tools_fe.h - The public API definitions for the frontend portion of
 *                   the Cray tools interface. This interface should be used
 *                   only on Cray login nodes. It will not function on eslogin
 *                   or compute nodes. Frontend refers to the location where
 *                   applications are launched.
 *
 * Copyright 2011-2015 Cray Inc.  All Rights Reserved.
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

#ifndef _CRAY_TOOLS_FE_H
#define _CRAY_TOOLS_FE_H

#include <stdint.h>
#include <sys/types.h>

/*
 * The Cray tools interface needs to read environment variables about the system
 * configuration dynamically at run time. The environment variables that are
 * read are defined here.  Note that the value of these environment
 * variables are subject to change. Use the defines to guarantee portability.
 *
 * CTI_LIBAUDIT_ENV_VAR (required)
 *
 *         Used to define the absolute path to the audit library. This is
 *         required to be defined.
 *
 * CTI_DBG_LOG_DIR_ENV_VAR (optional)
 *
 *         Used to define a path to write log files to. This location must be 
 *         cross mounted and accessible by the compute nodes in order to receive
 *         debug logs from tool daemons.
 *
 * CTI_USER_DEF_APRUN_EXE_ENV_VAR (optional)
 *
 *         Used to define the absolute path to the aprun binary. This is used 
 *         when a site has renamed the real aprun binary to something else.
 *
 * CTI_GDB_LOC_ENV_VAR (optional)
 *
 *         Used to define the absolute path to the gdb binary for use with WLMs
 *         utilizing the MPIR iface. This is used to override the default gdb
 *         named cti_approved_gdb that is expected to be found in PATH. The gdb
 *         binary is only used when launching applications through this 
 *         interface that require the use of the MPIR proctable (e.g. SLURM).
 *
 * CTI_ATTRIBS_TIMEOUT_ENV_VAR (optional)
 *
 *         Used to define the amount of time the daemon will spend attempting to
 *         open the pmi_attribs file when gathering application pid information
 *         on the compute node. If this is not set, the default timeout period 
 *         is 60 seconds.
 *
 * CTI_EXTRA_SLEEP_ENV_VAR (optional)
 *
 *         Used to define an extra amount of time to sleep after reading the 
 *         pmi_attribs file if it was not immediately available. This is to 
 *         avoid a potential race condition. If this is not set, the default is
 *         to wait an order of magnitude less than the amount of time it took to
 *         open the pmi_attribs file.
 *
 * CTI_CFG_DIR_ENV_VAR (required)
 *
 *         Used to define a location to write internal temporary files and 
 *         directories to. The caller must have write permission inside this 
 *         directory.
 *
 * CTI_DAEMON_STAGE_DIR_ENV_VAR (optional - CAUTION!)
 *
 *         Used to define the directory root name that will be used for a
 *         sessions unique storage space. This can be used to force multiple
 *         sessions to use the same directory structure. The use of this is not
 *         recommended since it is not guarded against race conditions and 
 *         conflicting file names.
 * 
 */
#define CTI_LIBAUDIT_ENV_VAR            "CRAY_LD_VAL_LIBRARY"
#define CTI_DBG_LOG_DIR_ENV_VAR         "CRAY_DBG_LOG_DIR"
#define CTI_USER_DEF_APRUN_EXE_ENV_VAR  "CRAY_APRUN_PATH"
#define CTI_GDB_LOC_ENV_VAR             "CRAY_CTI_GDB_PATH"
#define CTI_ATTRIBS_TIMEOUT_ENV_VAR     "CRAY_CTI_PMI_FOPEN_TIMEOUT"
#define CTI_EXTRA_SLEEP_ENV_VAR         "CRAY_CTI_PMI_EXTRA_SLEEP"
#define CTI_CFG_DIR_ENV_VAR             "CRAY_CTI_CFG_DIR"
#define CTI_DAEMON_STAGE_DIR_ENV_VAR    "CRAY_CTI_STAGE_DIR"

#ifdef __cplusplus
extern "C" {
#endif

/* 
 * The following are types used as return values for some API calls.
 */
typedef struct
{
        char *  hostname;
        int     numPes;
} cti_host_t;

typedef struct
{
        int            numHosts;
        cti_host_t *   hosts;
} cti_hostsList_t;

enum cti_wlm_type
{
    CTI_WLM_NONE,    // error/unitialized state
    CTI_WLM_ALPS,
    CTI_WLM_CRAY_SLURM,
    CTI_WLM_SLURM
};
typedef enum cti_wlm_type  cti_wlm_type;

typedef uint64_t   cti_app_id_t;
typedef int        cti_session_id_t;
typedef int        cti_manifest_id_t;


/************************************************************
 * The Cray tools interface frontend calls are defined below.
 ***********************************************************/

/*
 * cti_error_str - Returns an error string associated with a command that
 *                 returned an error value.
 * 
 * Detail
 *      This function returns the internal error string associated with a failed
 *      command. This string can be used to print an informative message about
 *      why an API call failed. If no error is known, the string will contain
 *      "Unknown CTI error".
 *
 * Arguments
 *      None.
 *
 * Returns
 *      A string containing the error message, or else "Unknown CTI error".
 * 
 */
extern const char * cti_error_str(void);

/*
 * cti_current_wlm - Obtain the current workload manager (WLM) in use on the 
 *                   system.
 * 
 * Detail
 *      This call can be used to obtain the current WLM in use on the system.
 *      The result can be used by the caller to validate arguments to functions
 *      and learn which WLM specific calls can be made.
 *
 * Arguments
 *      None.
 *
 * Returns
 *      A cti_wlm_type that contains the current WLM in use on the system.
 *
 */
extern cti_wlm_type cti_current_wlm(void);

/*
 * cti_wlm_type_toString - Obtain the stringified representation of the 
 *                         cti_wlm_type.
 * 
 * Detail
 *      This call can be used to turn the cti_wlm_type returned by 
 *      cti_current_wlm into a human readable format.
 *
 * Arguments
 *      wlm_type - The cti_wlm_type to stringify
 *
 * Returns
 *      A string containing the human readable format.
 *
 */
extern const char * cti_wlm_type_toString(cti_wlm_type wlm_type);

/*
 * cti_getHostname - Returns the hostname of the current login node.
 * 
 * Detail
 *      This function determines the hostname of the current login node. This
 *      hostname can be used by tool daemons to create socket connections to
 *      the frontend.
 *
 * Arguments
 *      None.
 *
 * Returns
 *      A string containing the hostname, or else a null string on error.
 * 
 */
extern char * cti_getHostname();

/*
 * cti_deregisterApp - Assists in cleaning up internal allocated memory
 *                     associated with a previously registered application.
 * 
 * Detail
 *      For applications that use the tool interface that wish to operate over
 *      many different applications at once, this function can be used to free
 *      up and destroy any internal data structures that were created for use
 *      with the app_id of the registered application.
 *
 * Arguments
 *      app_id - The cti_app_id_t of the previously registered application.
 *
 * Returns
 *      Returns no value.
 *
 */
extern void cti_deregisterApp(cti_app_id_t app_id);

/*
 * cti_getLauncherHostName - Returns the hostname of the login node where the
 *                           application launcher process resides.
 * 
 * Detail
 *      This function determines the hostname of the login node where the 
 *      application launcher used to launch the registerd app_id resides. This
 *      hostname may be different from the result returned by cti_getHostname.
 *
 * Arguments
 *      app_id -  The cti_app_id_t of the registered application.
 *
 * Returns
 *      A string containing the launcher host, or else a null string on error.
 * 
 */
extern char * cti_getLauncherHostName(cti_app_id_t app_id);

/*
 * cti_getNumAppPEs - Returns the number of processing elements in the
 *                    application associated with the app_id.
 * 
 * Detail
 *      This function is used to determine the number of PEs (processing
 *      elements) for the application associated with the given app_id. A PE 
 *      typically represents a single rank.
 *
 * Arguments
 *      app_id -  The cti_app_id_t of the registered application.
 *
 * Returns
 *      Number of PEs in the application, or else 0 on error.
 * 
 */
extern int cti_getNumAppPEs(cti_app_id_t app_id);

/*
 * cti_getNumAppNodes - Returns the number of compute nodes allocated for the
 *                      application associated with the app_id.
 * 
 * Detail
 *      This function is used to determine the number of compute nodes that
 *      was allocated by the application launcher for the application associated
 *      with the given app_id.
 *
 * Arguments
 *      app_id -  The cti_app_id_t of the registered application.
 *
 * Returns
 *      Number of compute nodes allocated for the application,
 *      or else 0 on error.
 * 
 */
extern int cti_getNumAppNodes(cti_app_id_t app_id);

/*
 * cti_getAppHostsList - Returns a null terminated array of strings containing
 *                       the hostnames of the compute nodes allocated by the
 *                       application launcher for the application associated 
 *                       with the app_id.
 * 
 * Detail
 *      This function creates a list of compute node hostnames for each
 *      compute node assoicated with the given app_id. These hostnames
 *      can be used to communicate with the compute nodes over socket
 *      connections. The list is null terminated. It is the callers 
 *      responsibility to free the returned list of strings.
 *
 * Arguments
 *      app_id -  The cti_app_id_t of the registered application.
 *
 * Returns
 *      A null terminated list of pointers to strings, or else a null
 *      pointer on error.
 * 
 */
extern char ** cti_getAppHostsList(cti_app_id_t app_id);

/*
 * cti_getAppHostsPlacement - Returns a cti_hostsList_t containing cti_host_t
 *                            entries that contain the hostname of the compute
 *                            nodes allocated by the application launcher and 
 *                            the number of PEs assigned to that host for the 
 *                            application associated with the app_id.
 * 
 * Detail
 *      This function creates a cti_hostsList_t that contains the number of
 *      hosts associated with the application and cti_host_t entries that
 *      contain the hostname string along with the number of PEs assigned to
 *      this host. Note that there is a cti_host_t entry for each compute node
 *      hostname that is assoicated with the given app_id. These hostnames can
 *      be used to communicate with the compute nodes over socket connections.
 *
 * Arguments
 *      app_id -  The cti_app_id_t of the registered application.
 *
 * Returns
 *      An cti_hostsList_t that contains the number of hosts in the application
 *      and an array of cti_host_t for each host assigned to the application.
 * 
 */
extern cti_hostsList_t * cti_getAppHostsPlacement(cti_app_id_t app_id);

/*
 * cti_destroyHostsList - Used to destroy the memory allocated for a 
 *                        cti_hostsList_t struct.
 * 
 * Detail
 *      This function free's a cti_hostsList_t struct. This is used to
 *      safely destroy the data structure returned by a call to the 
 *      getAppHostsPlacement function when the caller is done with the data
 *      that was allocated during its creation.
 *
 * Arguments
 *      placement_list - A pointer to the cti_hostsList_t to free.
 *
 * Returns
 *      Void. This function behaves similarly to free().
 *
 */
extern void cti_destroyHostsList(cti_hostsList_t *placement_list);


/*******************************************************************************
 * cti_run functions - Functions related to starting and/or killing applications
 *                     using system application launchers like aprun, srun, or
 *                     mpirun.
 ******************************************************************************/

/*
 * cti_launchApp - Start an application using the application launcher.
 *
 * Detail
 *      This function will launch an application. The application launcher to
 *      use will be automatically determined based on the current workload 
 *      manager of the system. It is up to the caller to ensure that valid 
 *      launcher_argv arguments are provided that correspond to the application
 *      launcher. The application launcher will either be aprun or srun.
 *
 *      The stdout/stderr of the launcher can be redirected to an open file 
 *      descriptor. This is enabled by providing valid file descriptors that are
 *      opened for writing to the stdout_fd and/or stderr_fd arguments. If the
 *      stdout_fd and/or stderr_fd arguments are -1, then the stdout/stderr
 *      inherited from the caller will be used.
 *
 *      The stdin of the launcher can be redirected from a file. This is enabled
 *      by providing a string with the pathname of the file to the inputFile
 *      argument. If inputFile is NULL, then the stdin of the launcher will be
 *      redirected from /dev/null.
 *
 *      The current working directory for the launcher can be changed from its
 *      current location. This is enabled by providing a string with the
 *      pathname of the directory to cd to to the chdirPath argument. If 
 *      chdirPath is NULL, then no cd will take place.
 *
 *      The environment of the launcher can be modified. Any environment
 *      variable to set in the launcher process should be provided as a null
 *      terminated list of strings of the form "name=value" to the env_list
 *      argument. All environment variables set in the caller process will be
 *      inherited by the launcher process. If env_list is NULL, no changes to
 *      the environment will be made.
 *
 * Arguments
 *      launcher_argv -  A null terminated list of arguments to pass directly to
 *                       the launcher. This differs from a traditional argv in
 *                       the sense that launcher_argv[0] is the start of the
 *                       actual arguments passed to the launcher and not the
 *                       name of launcher itself.
 *      stdout_fd -      The file descriptor opened for writing to redirect
 *                       stdout to or -1 if no redirection should take place.
 *      stderr_fd -      The file descriptor opened for writing to redirect
 *                       stderr to or -1 if no redirection should take place.
 *      inputFile -      The pathname of a file to open and redirect stdin or
 *                       NULL if no redirection should take place. If NULL,
 *                       /dev/null will be used for stdin.
 *      chdirPath -      The path to change the current working directory to or 
 *                       NULL if no cd should take place.
 *      env_list -       A null terminated list of strings of the form 
 *                       "name=value". The name in the environment will be set
 *                       to value.
 *
 * Returns
 *      A cti_app_id_t that contains the id registered in this interface. This
 *      app_id should be used in subsequent calls. 0 is returned on error.
 * 
 */
extern cti_app_id_t cti_launchApp(   const char * const  launcher_argv[],
                                     int                 stdout_fd,
                                     int                 stderr_fd,
                                     const char *        inputFile,
                                     const char *        chdirPath, 
                                     const char * const  env_list[]);

/*
 * cti_launchAppBarrier - Start an application using the application launcher
 *                        and have the launcher hold the application at its
 *                        startup barrier for MPI/SHMEM/UPC/CAF applications.
 *
 * Detail
 *      This function will launch and hold an application at its startup barrier
 *      until cti_launchAppBarrier is called with the app_id returned by this
 *      call. If the application is not using a programming model like MPI/SHMEM
 *      /UPC/CAF, the application will not be held at the startup barrier and
 *      this function should not be used. Use cti_launchApp instead.
 *
 *      On Cray systems, the startup barrier is the point at which the 
 *      application processes have been started, but are being held in a CTOR
 *      before main() has been called. Holding an application at this point can
 *      guarantee that tool daemons can be started before the application code
 *      starts executing.
 *
 *      The arguments for this function are identical to cti_launchApp. See the
 *      cti_launchApp description for more information.
 *
 * Arguments
 *      See the cti_launchApp description for more information.
 *
 * Returns
 *      A cti_app_id_t that contains the id registered in this interface. This
 *      app_id should be used in subsequent calls. 0 is returned on error.
 * 
 */
extern cti_app_id_t cti_launchAppBarrier(   const char * const  launcher_argv[],
                                            int                 stdout_fd,
                                            int                 stderr_fd,
                                            const char *        inputFile,
                                            const char *        chdirPath, 
                                            const char * const  env_list[]);

/*
 * cti_releaseAppBarrier - Release the application launcher launched with the
 *                         cti_launchAppBarrier function from its startup
 *                         barrier.
 * 
 * Detail
 *      This function communicates to the application launcher process that it 
 *      should be released from the startup barrier it is currently being held
 *      at. Note that this function must be used in conjunction with a valid
 *      app_id that was created by the cti_launchAppBarrier function.
 *
 * Arguments
 *      app_id - The cti_app_id_t of the application started with a call to
 *               cti_launchAppBarrier.
 *
 * Returns
 *      0 on success, or else 1 on failure.
 * 
 */
extern int cti_releaseAppBarrier(cti_app_id_t app_id);

/*
 * cti_killApp - Send a signal using the appropriate launcher kill mechanism to 
 *               an application launcher.
 * 
 * Detail
 *      This function is used to send the provided signal to the app_id 
 *      associated with a valid application. The app_id must have been obtained
 *      by calling cti_launchAppBarrier or an appropriate register function.
 *
 * Arguments
 *      app_id -  The cti_app_id_t of the registered application.
 *      signum -  The signal number (defined in signal.h) to send to the 
 *                application.
 *
 * Returns
 *      0 on success, or else 1 on failure.
 * 
 */
extern int cti_killApp(cti_app_id_t app_id, int signum);


/*******************************************************************************
 * ALPS WLM functions - Functions valid with the ALPS WLM only.
 ******************************************************************************/

// alps specific type information
typedef struct
{
    uint64_t  apid;
    pid_t     aprunPid;
} cti_aprunProc_t;

/*
 * cti_alps_getApid - Get the apid associated with the pid of an existing aprun
 *                    process.
 *
 * Detail
 *      This function is used to obtain the alps apid associated with the pid
 *      of the aprun process. It is useful in order to call the 
 *      cti_alps_registerApid function when only the pid of the aprun process is
 *      known.
 *
 * Arguments
 *      aprunPid - The pid_t of the aprun process
 *
 * Returns
 *      A uint64_t that represents the alps apid of the aprun process. 0 is 
 *      returned on error.
 *
 */
extern uint64_t cti_alps_getApid(pid_t aprunPid);

/*
 * cti_alps_registerApid -  Assists in registering the apid of an already
 *                          running aprun application for use with the Cray tool 
 *                          interface.
 * 
 * Detail
 *      This function is used for registering a valid aprun application that was 
 *      previously launched through external means for use with the tool 
 *      interface. It is recommended to use the built-in functions to launch 
 *      applications, however sometimes this is impossible (such is the case for
 *      a debug attach scenario). In order to use any of the functions defined
 *      in this interface, the apid of the aprun application must be registered.
 *      This is done automatically when using the built-in functions to launch
 *      applications. The apid can be obtained from apstat.
 *
 * Arguments
 *      apid - The apid of the aprun application to register.
 *
 * Returns
 *      A cti_app_id_t that contains the id registered in this interface. This
 *      app_id should be used in subsequent calls. 0 is returned on error.
 * 
 */
extern cti_app_id_t cti_alps_registerApid(uint64_t apid);

/*
 * cti_alps_getAprunInfo - Obtain information about the aprun process
 *
 * Detail
 *      This function is used to obtain the apid of an aprun application and the
 *      pid_t of the aprun process based on the passed in app_id. It is the 
 *      callers responsibility to free the allocated storage with free() when it
 *      is no longer needed.
 *
 * Arguments
 *      app_id -  The cti_app_id_t of the registered application.
 *
 * Returns
 *      A cti_aprunProc_t pointer that contains the apid and pid_t of aprun.
 *      NULL is returned on error. The caller should free() the returned pointer
 *      when finished using it.
 *
 */
extern cti_aprunProc_t * cti_alps_getAprunInfo(cti_app_id_t app_id);

/*
 * cti_alps_getAlpsOverlapOrdinal - Return the applications "overlap ordinal"
 *
 * Detail
 *      This function is used to obtain the "overlap ordinal" for the 
 *      application. The overlap ordinal is a small integer unique to this app
 *      among the group of applications that partially or fully overlap the set 
 *      of nodes occupied by the specified application. This is only useful for
 *      checkpoint restart on Cray ALPS systems when trying to figure out the
 *      number of applications currently running beside the given application.
 *      Note that this function can only be called with a cti_app_id_t from a
 *      valid ALPS WLM application.
 *
 * Arguments
 *      app_id -  The cti_app_id_t of the registered application.
 *
 * Returns
 *      A non-negative integer representing the overlap ordinal. On error a
 *      negative value will be returned.
 */
extern int  cti_alps_getAlpsOverlapOrdinal(cti_app_id_t app_Id);


/*******************************************************************************
 * Cray SLURM WLM functions - Functions valid with the Cray native SLURM WLM
 *                            only.
 ******************************************************************************/

// slurm specific type information
typedef struct
{
    uint32_t  jobid;
    uint32_t  stepid;
} cti_srunProc_t;

/*
 * cti_cray_slurm_registerJobStep - Assists in registering the jobid and stepid
 *                                  of an already running srun application for
 *                                  use with the Cray tool interface.
 * 
 * Detail
 *      This function is used for registering a valid srun application that was
 *      previously launched through external means for use with the tool
 *      interface. It is recommended to use the built-in functions to launch
 *      applications, however sometimes this is impossible (such is the case for
 *      a debug attach scenario). In order to use any of the functions defined
 *      in this interface, the jobid and stepid of the srun application must be
 *      registered. This is done automatically when using the built-in functions
 *      to launch applications. The jobid/stepid can be obtained from qstat.
 *
 * Arguments
 *      job_id - The job id of the srun application to register.
 *      step_id - The step id of the srun application to register.
 *
 * Returns
 *      A cti_app_id_t that contains the id registered in this interface. This
 *      app_id should be used in subsequent calls. 0 is returned on error.
 * 
 */
extern cti_app_id_t cti_cray_slurm_registerJobStep( uint32_t job_id,
                                                    uint32_t step_id);
/*
 * cti_cray_slurm_getSrunInfo - Obtain information about the srun process
 *
 * Detail
 *      This function is used to obtain the jobid/stepid of an srun application
 *      based on the registered app_id. It is the callers responsibility to free
 *      the allocated storage with free() when it is no longer needed.
 *
 * Arguments
 *      app_id -  The cti_app_id_t of the registered application.
 *
 * Returns
 *      A cti_srunProc_t pointer that contains the jobid and stepid of srun.
 *      NULL is returned on error. The caller should free() the returned pointer
 *      when finished using it.
 *
 */
extern cti_srunProc_t * cti_cray_slurm_getSrunInfo(cti_app_id_t appId);


/*******************************************************************************
 * cti_transfer functions - Functions related to shipping files, shared
 *                          libraries, and binaries to compute nodes and
 *                          launching tool daemons.
 *
 * NOTE: The functions defined in this section will keep track of files that
 *       were previously shipped to compute nodes associated with a session and
 *       will not allow a naming conflict to occur between consecutive calls.
 *       This eliminates redundant shipping of dependencies between multiple
 *       calls associated with the same session.
 ******************************************************************************/

/*
 * cti_createSession - Create a new session abstraction that represents a unique
 *                     storage space on the compute nodes associated with a
 *                     registered application id.
 * Detail
 *      This function is used to create a new internal session object to be
 *      associated with the given app_id. The session represents a unique
 *      directory on the compute nodes that will not collide with other tools
 *      that make use of this interface. In order to use the other transfer
 *      functions, a session must first be created. 
 *
 *      The unique directory will have a random name by default to avoid
 *      collisions with other tool daemons using this interface. It will have
 *      sub-directories /bin for binaries, /lib for libraries, and /tmp for
 *      temporary storage. This directory is guaranteed to be cleaned up upon
 *      tool daemon exit. The directory will not be created on the compute nodes
 *      until a manifest is shipped or a tool daemon started.
 *
 *      The session will become invalid for future use upon calling the 
 *      cti_deregisterApp function with the app_id associated with the session.
 *
 * Arguments
 *      app_id - The cti_app_id_t of the registered application.
 *
 * Returns
 *      A non-zero cti_session_id_t on success, or else 0 on failure.
 *
 */
extern cti_session_id_t cti_createSession(cti_app_id_t app_id);

/*
 * cti_sessionIsValid - Test if a cti_session_id_t is still valid
 * Detail
 *      This function is used to test if a cti_session_id_t returned from the
 *      cti_createSession call is still valid. A session becomes invalid for
 *      future use upon calling the cti_deregisterApp function with the app_id
 *      associated with the session.
 *
 * Arguments
 *      sid - The cti_session_id_t of the session.
 *
 * Returns
 *      0 if the session is invalid, 1 if the session is still valid.
 *
 */
extern int cti_sessionIsValid(cti_session_id_t sid);

/*
 * cti_createManifest - Create a new manifest abstraction that represents a
 *                      list of binaries, libraries, library directories, and 
 *                      files to be sent to the session storage space.
 * Detail
 *      This function is used to create a new manifest list of binaries,
 *      libraries, library directories, and files that need to be shipped to the
 *      compute nodes for use by a tool daemon. Only uniquely named binaries,
 *      libraries, library directories, and files are added to the manifest that
 *      were not already added to this or any already shipped manifest. This
 *      avoids redundant shipment of files and inadvertent naming collisions.
 *
 *      Upon adding a file, if it has the same realname as an already added file
 *      of that type, and the locations match, no error will occur. If the
 *      locations differ, an error will occur because of the naming collision.
 *      The files in the manifest are only shipped upon calling cti_sendManifest
 *      or cti_execToolDaemon at which point the manifest becomes invalid for
 *      future use.
 *
 *      It is valid for multiple manifests associated with the same session id
 *      to exist at the same time. The manifest will become invalid for future
 *      use upon passing it to cti_sendManifest or cti_execToolDaemon calls or
 *      by calling the cti_deregisterApp function.
 *
 * Arguments
 *      sid - The cti_session_id_t of the session.
 *
 * Returns
 *      A non-zero cti_manifest_id_t on success, or else 0 on failure.
 *
 */
extern cti_manifest_id_t cti_createManifest(cti_session_id_t sid);

/*
 * cti_manifestIsValid - Test if a cti_manifest_id_t is still valid
 * Detail
 *      This function is used to test if a cti_manifest_id_t returned from the
 *      cti_createManifest call is still valid. A manifest becomes invalid for
 *      future use upon passing it to cti_sendManifest or cti_execToolDaemon
 *      calls or by calling the cti_deregisterApp function.
 *
 * Arguments
 *      mid - The cti_manifest_id_t of the manifest.
 *
 * Returns
 *      0 if the manifest is invalid, 1 if the manifest is still valid.
 *
 */
extern int cti_manifestIsValid(cti_manifest_id_t mid);

/*
 * cti_addManifestBinary - Add a program binary to a manifest.
 * 
 * Detail
 *      This function is used to add a program binary to a manifest based on the
 *      cti_manifest_id_t argument. The program binary along with any shared
 *      library dependencies will be added to the manifest. If the program uses
 *      dlopen to open libraries, those libraries will need to be manually added
 *      by calling cti_addManifestLibrary. Sending a program is used when a tool
 *      daemon needs to fork/exec another program. The binary can be an absolute
 *      path, a relative path, or file name upon which the PATH environment
 *      variable will be searched. Upon shipment, the binary can be found in
 *      PATH and shared library dependencies can be found in LD_LIBRARY_PATH of
 *      the tool daemon environment or by using the CTI backend API to determine
 *      the locations.
 *
 * Arguments
 *      mid -   The cti_manifest_id_t of the manifest.
 *      fstr -  The name of the binary to add to the manifest. This can either
 *              be a fullpath name to the file or else the file name if the 
 *              binary is found within PATH.
 *
 * Returns
 *      0 on success, or else 1 on failure.
 * 
 */
extern int cti_addManifestBinary(cti_manifest_id_t mid, const char *fstr);

/*
 * cti_addManifestLibrary - Add a shared library to a manifest.
 * 
 * Detail
 *      This function is used to add a shared library to a manifest based on the
 *      cti_manifest_id_t argument. Sending a shared library is used when a tool
 *      daemon or program dependency needs to dlopen a shared library at some
 *      point during its lifetime. Upon shipment, shared libraries can be found
 *      in LD_LIBRARY_PATH of the tool daemon environment or by using the CTI 
 *      backend API to determine the location.
 *
 * Arguments
 *      mid -     The cti_manifest_id_t of the manifest.
 *      fstr -    The name of the shared library to add to the manifest. This
 *                can either be a fullpath name to the library or else the 
 *                library name if the library is found within LD_LIBRARY_PATH or
 *                any of the default system locations where shared libraries are
 *                stored. The RPATH of the calling executable is not queried.
 *
 * Returns
 *      0 on success, or else 1 on failure.
 * 
 */
extern int cti_addManifestLibrary(cti_manifest_id_t mid, const char *fstr);

/*
 * cti_addManifestLibDir - Add a library directory to a manifest.
 * 
 * Detail
 *      This function is used to add a shared library directory to a manifest
 *      based on the cti_manifest_id_t argument. The contents of the directory
 *      will recursively added to the manifest. Sending a library directory is
 *      used when a tool daemon needs to dlopen a large number of shared
 *      libraries. For example, this can be the case for python programs. The
 *      library directory will not be added to the LD_LIBRARY_PATH of the tool
 *      daemon environment. It is up to the tool writer to use provided CTI
 *      backend library calls along with the directory name to locate the shared
 *      libraries as needed.
 *
 * Arguments
 *      mid -     The cti_manifest_id_t of the manifest.
 *      fstr -    The name of the shared library directory to add to the 
 *                manifest. This must be the fullpath of the directory.
 *
 * Returns
 *      0 on success, or else 1 on failure.
 * 
 */
extern int cti_addManifestLibDir(cti_manifest_id_t mid, const char *fstr);

/*
 * cti_addManifestFile - Add a regular file to a manifest.
 * 
 * Detail
 *      This function is used to add a regular file to a manifest based on the
 *      cti_manifest_id_t argument. Sending a regular file is used when a tool
 *      daemon needs to read from the file. For example, a required 
 *      configuration file. Upon shipment, the file can be found in PATH of the
 *      tool daemon environment or by using the CTI backend API to determine the
 *      location.
 *
 * Arguments
 *      mid -     The cti_manifest_id_t of the manifest.
 *      fstr -    The name of the file to add to the manifest. This can either 
 *                be a fullpath name to the file or else the file name if the 
 *                file is found within PATH.
 *
 * Returns
 *      0 on success, or else 1 on failure.
 * 
 */
extern int cti_addManifestFile(cti_manifest_id_t mid, const char *fstr);

/*
 * cti_sendManifest - Ship a manifest to a sessions unique storage space and
 *                    make the files available to a tool daemon.
 *
 * Detail
 *      This function is used to ship all files in the manifest to the unique
 *      storage space of the associated session. Typically this function should
 *      not be used and the manifest should be shipped with the
 *      cti_execToolDaemon call instead. This avoids multiple sends across the
 *      network. A manifest needs to be shipped if additional files are needed
 *      by a tool daemon after the tool daemon has launched. The provided 
 *      manifest will become invalid for future use upon calling this function.
 *
 *      If the debug option is non-zero, the environment variable defined by
 *      CTI_DBG_LOG_DIR_ENV_VAR will be read and log files will be created in 
 *      this location. If the environment variable is not defined, log files
 *      will be created in the /tmp directory on the compute nodes. This log
 *      will contain all output during shipment of the manifest and can be used
 *      to locate problems with file shipment.
 *
 * Arguments
 *      mid -   The cti_manifest_id_t of the manifest.
 *      debug - If non-zero, create a log file at the location provided by
 *              CTI_DBG_LOG_DIR_ENV_VAR.
 *
 * Returns
 *      0 on success, or else 1 on failure.
 * 
 */
extern int cti_sendManifest(cti_manifest_id_t mid, int debug);

/*
 * cti_execToolDaemon - Launch a tool daemon onto all the compute nodes
 *                      associated with the provided manifest session.
 * 
 * Detail
 *      This function is used to launch a program binary onto compute nodes.
 *      It will take care of starting up the binary and ensuring all of the
 *      files in the manifest are found in its environment as described above.
 *      One tool daemon will be started per compute node of the application.
 *
 *      Any files in the provided manifest argument will be shipped and made
 *      available to the tool daemon. If no other file dependencies are 
 *      required, an empty manifest must still be provided by first calling the
 *      cti_createManifest function with the session associated with the
 *      registered application. It is not necessary to add the tool daemon
 *      binary to the manifest before calling this function. The manifest will
 *      become invalid for future use upon calling this function.
 *
 *      The tool daemon will have any shipped binaries found in its PATH, any
 *      shared libraries found in its LD_LIBRARY_PATH, and have its TMPDIR 
 *      modified to point at a location that is guaranteed to have read/write 
 *      access. A null terminated list of environment variables can be provided
 *      to set in the environment of the tool daemon. Each entry in this list
 *      should have a "envVar=val" format. Any arguments that need to be passed
 *      to the tool daemon can be provided by a null termianted list of args.
 *      In this case, the args[0] is the beginning of actual tool daemon
 *      arguments and not the name of the tool daemon binary.
 *
 *      If the debug option is non-zero, the environment variable defined by
 *      CTI_DBG_LOG_DIR_ENV_VAR will be read and log files will be created in 
 *      this location. These log files will contain any output written to 
 *      stdout/stderr of the tool daemons. Otherwise tool daemon stdout/stderr 
 *      will be redirected to /dev/null.
 *
 * Arguments
 *      mid -     The cti_manifest_id_t of the manifest.
 *      fstr -    The name of the tool daemon binary to exec on the compute 
 *                nodes associated with the session. This can either be a
 *                fullpath name to the file or else the file name if the binary
 *                is found within PATH.
 *      args -    The null terminated list of arguments to pass to the tool
 *                daemon. This should not start with name of the tool daemon
 *                binary.
 *      env -     The null terminated list of environment variables to
 *                set in the environment of the fstr process. The strings
 *                in this list shall be formed in a "envVar=val" manner.
 *      debug -   If true, create log files at the location provided by
 *                CTI_DBG_LOG_DIR_ENV_VAR. Redirects stdout/stderr of the tool
 *                daemons to the log files.
 *
 * Returns
 *      0 on success, or else 1 on failure.
 * 
 */
extern int cti_execToolDaemon( cti_manifest_id_t   mid,
                               const char *        fstr, 
                               const char * const  args[],
                               const char * const  env[],
                               int                 debug);

/*
 * cti_getSessionLockFiles - Get the name(s) of instance dependency lock files.
 * 
 * Detail
 *      This function is used to return a null terminated list of lock file
 *      locations that must exist for dependency requirements of previously
 *      shipped manifests/tool daemons to be met. These files are not accessible
 *      from the login node. The strings can be used as arguments passed to the
 *      tool daemons.
 *
 * Arguments
 *      sid -     The cti_session_id_t of the session.
 *
 * Returns
 *      A null terminated array of strings, or else NULL on error.
 * 
 */
extern char ** cti_getSessionLockFiles(cti_session_id_t sid);

/*
 * cti_getSessionRootDir - Get root directory of session directory structure on 
 *                         compute node.
 * 
 * Detail
 *      This function is used to return the path of the root location of the
 *      session directory on the compute node. The path is not accessible from
 *      the login node. The returned string can be used to modify arguments to
 *      tool daemons to locate dependencies. It is the callers responsibility to
 *      free the allocated storage when it is no longer needed.
 *
 * Arguments
 *      sid -     The cti_session_id_t of the session.
 *
 * Returns
 *      A pointer to the path on success, or NULL on error.
 * 
 */
extern char * cti_getSessionRootDir(cti_session_id_t sid);

/*
 * cti_getSessionBinDir - Get bin directory of session binaries on compute node.
 * 
 * Detail
 *      This function is used to return the path of the binary location of the
 *      bin directory on the compute node. The path is not accessible from
 *      the login node. The returned string can be used to modify arguments to
 *      tool daemons to locate dependencies. It is the callers responsibility to
 *      free the allocated storage when it is no longer needed. All manifest and
 *      tool daemon binaries are placed within this directory.
 *
 * Arguments
 *      sid -     The cti_session_id_t of the session.
 *
 * Returns
 *      A pointer to the path on success, or NULL on error.
 * 
 */
extern char * cti_getSessionBinDir(cti_session_id_t sid);

/*
 * cti_getSessionLibDir - Get lib directory of session libraries on compute 
 *                        node.
 * 
 * Detail
 *      This function is used to return the path of the library location of the
 *      lib directory on the compute node. The path is not accessible from
 *      the login node. The returned string can be used to modify arguments to
 *      tool daemons to locate dependencies. It is the callers responsibility to
 *      free the allocated storage when it is no longer needed. All manifest and
 *      tool daemon libraries are placed within this directory.
 *
 * Arguments
 *      sid -     The cti_session_id_t of the session.
 *
 * Returns
 *      A pointer to the path on success, or NULL on error.
 * 
 */
extern char * cti_getSessionLibDir(cti_session_id_t sid);

/*
 * cti_getSessionFileDir - Get file directory of session files on compute node.
 * 
 * Detail
 *      This function is used to return the path of the file location of the
 *      file directory on the compute node. The path is not accessible from
 *      the login node. The returned string can be used to modify arguments to
 *      tool daemons to locate dependencies. It is the callers responsibility to
 *      free the allocated storage when it is no longer needed. All manifest
 *      files are placed within this directory.
 *
 * Arguments
 *      sid -     The cti_session_id_t of the session.
 *
 * Returns
 *      A pointer to the path on success, or NULL on error.
 * 
 */
extern char * cti_getSessionFileDir(cti_session_id_t sid);

/*
 * cti_getSessionTmpDir - Get tmp directory of session on compute node.
 * 
 * Detail
 *      This function is used to return the path of the tmp location on the 
 *      compute node. The path is not accessible from the login node. The 
 *      returned string can be used to modify arguments to tool daemons to 
 *      locate dependencies. It is the callers responsibility to free the 
 *      allocated storage when it is no longer needed. This tmp dir is not
 *      shared across sessions and populated by the tool daemon only.
 *
 * Arguments
 *      sid -     The cti_session_id_t of the session.
 *
 * Returns
 *      A pointer to the path on success, or NULL on error.
 * 
 */
extern char * cti_getSessionTmpDir(cti_session_id_t sid);

#ifdef __cplusplus
}
#endif

#endif /* _CRAY_TOOLS_FE_H */
