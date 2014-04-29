/******************************************************************************\
 * cray_tools_fe.h - The public API definitions for the frontend portion of
 *                   the Cray tools interface. This interface should be used
 *                   only on Cray login nodes. It will not function on eslogin
 *                   or compute nodes. Frontend refers to the location where
 *                   applications are launched.
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
 * CTI_LIBAUDIT_KEYFILE_ENV_VAR (optional)
 *
 *         Used to define a path to a file used to create sys-v keys in the 
 *         audit library. If the file doesn't exist, it will be created at the 
 *         provided location. If it is not provided, a default choice will be 
 *         made.
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
 * CTI_DAEMON_STAGE_DIR_ENV_VAR	(optional - CAUTION!)
 *
 *         Used to define the directory root name that will be used to stage 
 *         binaries, libraries, and files to on the compute node. This can be 
 *         used to force multiple sessions to use the same directory structure.
 *         The use of this is not recommended since it is not guarded against 
 *         race conditions and conflicting file names.
 * 
 */
#define CTI_LIBAUDIT_ENV_VAR            "CRAY_LD_VAL_LIBRARY"
#define CTI_LIBAUDIT_KEYFILE_ENV_VAR    "CRAY_LD_VAL_KEYFILE"
#define CTI_DBG_LOG_DIR_ENV_VAR         "CRAY_DBG_LOG_DIR"
#define CTI_USER_DEF_APRUN_EXE_ENV_VAR  "CRAY_APRUN_PATH"
#define CTI_ATTRIBS_TIMEOUT_ENV_VAR     "CRAY_CTI_PMI_FOPEN_TIMEOUT"
#define CTI_EXTRA_SLEEP_ENV_VAR         "CRAY_CTI_PMI_EXTRA_SLEEP"
#define CTI_CFG_DIR_ENV_VAR             "CRAY_CTI_CFG_DIR"
#define CTI_DAEMON_STAGE_DIR_ENV_VAR    "CRAY_CTI_STAGE_DIR"

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

typedef struct
{
	uint64_t	apid;
	pid_t		aprunPid;
} cti_aprunProc_t;

enum cti_wlm_type
{
	CTI_WLM_NONE,	// error/unitialized state
	CTI_WLM_ALPS,
	CTI_WLM_CRAY_SLURM,
	CTI_WLM_SLURM
};
typedef enum cti_wlm_type	cti_wlm_type;

typedef uint64_t	cti_app_id_t;
typedef int 		cti_manifest_id_t;
typedef int			cti_session_id_t;


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
extern const char *	cti_error_str(void);

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
extern cti_wlm_type	cti_current_wlm(void);

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
extern const char *	cti_wlm_type_toString(cti_wlm_type wlm_type);

/*
 * cti_deregisterApp - Assists in cleaning up internal allocated memory
 *                     associated with a previously registered application.
 * 
 * Detail
 *      For applications that use the tool interface that wish to operate over
 *      many different launcher sessions, this function can be used to free up
 *      and destroy any internal data structures that were created for use
 *      with the app_id of the registered application.
 *
 * Arguments
 *      app_id - The cti_app_id_t of the previously registered application.
 *
 * Returns
 *      Returns no value.
 *
 */
extern void	cti_deregisterApp(cti_app_id_t app_id);

/*
 * cti_getHostName - Returns the hostname of the current login node.
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
 *      A string containing the cname host, or else a null string on error.
 * 
 */
extern char *	cti_getHostName();

/*
 * cti_getLauncherHostName - Returns the hostname of the login node where the
 *                           application launcher process resides.
 * 
 * Detail
 *      This function determines the hostname of the login node where the 
 *      application launcher used to launch the registerd app_id resides. This
 *      hostname may be different from the result returned by cti_getHostName.
 *
 * Arguments
 *      app_id -  The cti_app_id_t of the registered application.
 *
 * Returns
 *      A string containing the launcher host, or else a null string on error.
 * 
 */
extern char *	cti_getLauncherHostName(cti_app_id_t app_id);

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
extern int	cti_getNumAppPEs(cti_app_id_t app_id);

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
extern int	cti_getNumAppNodes(cti_app_id_t app_id);

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
extern char **	cti_getAppHostsList(cti_app_id_t app_id);

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
extern cti_hostsList_t *	cti_getAppHostsPlacement(cti_app_id_t app_id);

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
extern void	cti_destroyHostsList(cti_hostsList_t *placement_list);


/*******************************************************************************
 * cti_run functions - Functions related to starting and/or killing applications
 *                     using system application launchers like aprun, srun, or
 *                     mpirun.
 ******************************************************************************/

/*
 * cti_launchAppBarrier - Start an application using the application launcher
 *                        with the provided argv array and have the launcher
 *                        hold the application at its startup barrier for 
 *                        MPI/SHMEM/UPC/CAF applications.
 * 
 * Detail
 *      This function is the preferred way for users to use the tool interface
 *      to launch applications.
 *
 *      The launcher to use will be automatically determined based on the
 *      current workload manager on the system. It is up to the caller to obtain
 *      valid launcher_argv arguments. This interface will simply pass the
 *      arguments through directly to the launcher program. This launcher is
 *      either aprun, srun, or mpirun.
 *
 *      This function will hold the application at its startup barrier until
 *      the barrier release function is called with the app_id returned by this
 *      call. This only applies to applications using programming models that
 *      call some sort of init function (MPI_Init for example).
 *
 *      This function can redirect the stdin of the launcher to a file. This is 
 *      enabled by setting the argument redirectInput to true and providing a 
 *      string that represents the pathname to the file. If redirectInput is 
 *      false, the function will redirect the stdin of the launcher to /dev/null 
 *      to prevent the launcher  from grabbing any input on stdin which may be
 *      used by other tool programs. 
 *
 *      This function can also optionally redirect stdout/stderr to provided
 *      open file descriptors by setting redirectOutput to true (non-zero).
 *
 *      This function can optionally set the current working directory for the 
 *      launcher process. This is useful if the launcher should start somewhere
 *      other than the current PWD if the application creates files based on 
 *      PWD.
 *
 *      This function can optionally set environment variables for the launcher
 *      process. This is useful if the launcher should use an environment
 *      variable that is different than the current value set in the parent.
 *
 * Arguments
 *      launcher_argv -  A null terminated list of arguments to pass directly to
 *                       the launcher. This differs from a traditional argv in
 *                       the sense that launcher_argv[0] is the start of the
 *                       actual arguments passed to the launcher and not the
 *                       name of launcher itself.
 *      redirectOutput - Toggles redirecting launcher stdout/stderr to the
 *                       provided file descriptors.
 *      redirectInput -  Toggles redirecting launcher stdin to the provided file
 *                       name.
 *      stdout_fd -      The open file descriptor to redirect stdout to if 
 *                       redirectOutput evaluates to true.
 *      stderr_fd -      The open file descriptor to redirect stderr to if
 *                       redirectOutput evaluates to true.
 *      inputFile -      The pathname of a file to open and redirect stdin to if
 *                       redirectInput evaluates to true.
 *      chdirPath -      The path to change the current working directory of 
 *                       the launcher to, or NULL if no chdir is to take place.
 *      env_list -       A null terminated list of strings of the form 
 *                       "name=value". The value of name in the environment will
 *                       be set to value irrespective of name already existing
 *                       in the environment. 
 *
 * Returns
 *      A cti_app_id_t that contains the id registered in this interface. This
 *      app_id should be used in subsequent calls. 0 is returned on error.
 * 
 */
extern cti_app_id_t cti_launchAppBarrier(   char ** launcher_argv,
                                            int     redirectOutput,
                                            int     redirectInput,
                                            int     stdout_fd,
                                            int     stderr_fd,
                                            char *  inputFile,
                                            char *  chdirPath, 
                                            char ** env_list);

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
extern int	cti_releaseAppBarrier(cti_app_id_t app_id);

/*
 * cti_killApp - Send a signal using the appropriate launcher kill mechanism to 
 *               an application launcher.
 * 
 * Detail
 *      This function is used to send the provided signal to the app_id 
 *      associated with a valid application session. The app_id must have been
 *      obtained by calling cti_launchAppBarrier or an appropriate register
 *      function.
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
extern int	cti_killApp(cti_app_id_t app_id, int signum);


/*******************************************************************************
 * ALPS WLM functions - Functions valid with the ALPS WLM only.
 ******************************************************************************/

/*
 * cti_registerApid -   Assists in registering the apid of an already running 
 *                      aprun session for use with the Cray tool interface.
 * 
 * Detail
 *      This function is used for registering a valid aprun session that was 
 *      previously launched through external means for use with the tool 
 *      interface. It is recommended to use the built-in functions to launch 
 *      aprun sessions, however sometimes this is impossible (such is the case 
 *      for a debug attach scenario). In order to use any of the functions
 *      defined in this interface, the apid of the aprun session *must* be
 *      registered. This is done automatically when using the built-in functions
 *      to launch aprun sessions. The apid can be obtained from apstat.
 *
 * Arguments
 *      apid - The apid of the aprun session to register.
 *
 * Returns
 *      A cti_app_id_t that contains the id registered in this interface. This
 *      app_id should be used in subsequent calls. 0 is returned on error.
 * 
 */
extern cti_app_id_t	cti_registerApid(uint64_t apid);

/*
 * cti_getAprunInfo - Obtain information about the aprun process
 *
 * Detail
 *      This function is used to obtain the apid of an aprun session and the
 *      pid_t of the aprun process based on the passed in app_id. It is the 
 *      callers responsibility to free the allocated storage when it is no 
 *      longer needed.
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
extern cti_aprunProc_t *	cti_getAprunInfo(cti_app_id_t);


/*******************************************************************************
 * cti_transfer functions - Functions related to shipping files, shared
 *                          libraries, and binaries to compute nodes and
 *                          possibly launching tool daemons.
 *
 * NOTE: The functions defined in this section will keep track of files that
 *       were previously shipped to compute nodes for a session and will not
 *       allow a naming conflict to occur between consecutive calls. This
 *       eliminates redundant shipping of dependencies between multiple calls
 *       in the same session.
 ******************************************************************************/

/*
 * cti_execToolDaemon - Launch a tool daemon onto compute nodes associated with
 *                      a registered application app_id.
 * 
 * Detail
 *      This function is used to launch a program binary onto compute nodes.
 *      It will take care of shipping the binary, determine shared library
 *      dependencies using the LD_AUDIT interface, ship any required shared
 *      library dependencies to compute nodes, and start the shipped binary
 *      using the daemon_launcher wrapper.
 *
 *      The daemon_launcher will take care of setting up the compute node 
 *      environment to ensure that any shared library dependency, file, or
 *      binary that was shipped can be found in PATH/LD_LIBRARY_PATH. The
 *      user can also provide a null terminated list of environment variables
 *      they wish to set in the environment of the tool process. This list
 *      shall be formed in a "envVar=val" manner. The arguments to pass to the
 *      tool program are defined by the provided null terminated list of args.
 *      Note that args[0] is the begining of the program arguments and not the
 *      name of the program itself.
 *      
 *      A cti_manifest_id_t can be provided to ship a manifest of binaries, 
 *      libraries, and files that may be required by the tool daemon. The 
 *      manifest must be created before calling this function and will be 
 *      cleaned up by this function. A unique directory will be created on the
 *      compute node in the temporary storage space associated with the
 *      application. This avoids naming conflicts between other tools using this
 *      interface. This unique directory will contain subdirectories /bin that
 *      contains all binaries and /lib that contains all libraries. Any files
 *      will not be placed in a subdirectory and are available directly in the
 *      current working directory of the tool daemon. All binaires will be found
 *      in the PATH of the tool daemon process, and all libraries will be found
 *      in the LD_LIBRARY_PATH of the tool daemon process.
 *
 *      A cti_session_id_t can be provided to associate this tool daemon with an
 *      existing tool daemon's environment previously setup on the compute node.
 *      Any libraries or files that were previously shipped will also be
 *      available to the new tool daemon. This can also be used to exec the tool
 *      daemon inside of a previously shipped manifest that was created by the
 *      call to sendManifest(). If a session id is provided, the return value
 *      from this function will be the same session id on success.
 *
 *      If both cti_manifest_id_t and cti_session_id_t arguments are provided,
 *      the manifest must have been created using the same session, otherwise an
 *      error will occur.
 *
 *      If the debug option evaluates to true, daemon launcher will attempt to 
 *      read the environment variable defined by CTI_DBG_LOG_DIR_ENV_VAR and
 *      create a log file at this location. If the environment variable is not
 *      found or is null, it will create a log file in the /tmp directory on the
 *      compute node. It will then dup the STDOUT/STDERR file channels to this
 *      log file. This is the only way to capture stdout/stderr output from a
 *      tool program on the compute nodes.
 *
 * Arguments
 *      app_id -  The cti_app_id_t of the registered application.
 *      mid -     The optional manifest id of a previously created manifest, or
 *                0 if no manifest is required.
 *      sid -     The optional session id of a previously created session, or
 *                0 if no session association should be made.
 *      fstr -    The name of the binary to exec onto the compute nodes.
 *                This can either be a fullpath name to the file or else
 *                the file name if the binary is found within the users
 *                PATH.
 *      args -    The null terminated list of arguments to pass to the
 *                fstr binary upon launch.
 *      env -     The null terminated list of environment variables to
 *                set in the environment of the fstr process. The strings
 *                in this list shall be formed in a "envVar=val" manner.
 *      debug -   If true, create a log file at the location provided by
 *                CTI_DBG_LOG_DIR_ENV_VAR. Redirect stdout/stderr to the log
 *                files fd.
 *
 * Returns
 *      A non-zero cti_session_id_t on success, or else 0 on failure.
 * 
 */
extern cti_session_id_t cti_execToolDaemon( cti_app_id_t      app_id, 
                                            cti_manifest_id_t mid, 
                                            cti_session_id_t  sid, 
                                            char *            fstr, 
                                            char **           args,
                                            char **           env,
                                            int               debug);

/*
 * cti_createNewManifest - Create a new manifest to ship additional binaries, 
 *                         libraries, and files when exec'ing a tool daemon.
 * Detail
 *      This function is used to create a new internal manifest list used to
 *      ship additional binaries, libraries, and files with a tool daemon
 *      process. The internal list will be cleaned up upon passing it as an
 *      argument when launching the tool daemon. Only unique binary, library,
 *      and file names will be added to the manifest. For instance, if multiple
 *      binaries require libc.so, it will only be added once to the manifest.
 *
 *      An optional cti_session_id_t argument can be used to associate the new 
 *      manifest with an existing session created by a call to
 *      cti_execToolDaemon() or cti_sendManifest(). In this case the existing
 *      file hiearchy will be used from the previous call and uniqueness
 *      requirements will apply to the previous manifest. If this is a brand new
 *      instance, pass in 0 for the sid argument.
 *
 * Arguments
 *      sid -     The optional session id of a previously create session, or
 *                0 if no session association should be made.
 *
 * Returns
 *      A non-zero cti_manifest_id_t on success, or else 0 on failure.
 *
 */
extern cti_manifest_id_t	cti_createNewManifest(cti_session_id_t sid);

/*
 * cti_destroyManifest - Cleanup an existing manifest.
 *
 * Detail
 *      This function is used to cleanup the internal memory associated with an
 *      existing manifest. This can be used to force cleanup to happen without
 *      shipping the manifest during error handling.
 *
 * Arguments
 *      mid -     The cti_manifest_id_t of the existing manifest.
 *
 * Returns
 *      Returns no value.
 *
 */
extern void	cti_destroyManifest(cti_manifest_id_t mid);

/*
 * cti_addManifestBinary - Add a program executable to an existing manifest.
 * 
 * Detail
 *      This function is used to add a program binary to an existing manifest
 *      based on the cti_manifest_id_t argument. The program binary along with
 *      any required shared library dependencies determined by the LD_AUDIT 
 *      interface will be added to the manifest. This is useful if a tool daemon
 *      needs to fork/exec another program at some point during its lifetime.
 *      All binaries will be found in the PATH of the tool daemon, and all
 *      libraries will be found in the LD_LIBRARY_PATH of the tool daemon.
 *
 * Arguments
 *      mid -     The cti_manifest_id_t of the existing manifest.
 *      fstr -    The name of the binary to add to the manifest. This can either
 *                be a fullpath name to the file or else the file name if the 
 *                binary is found within PATH.
 *
 * Returns
 *      0 on success, or else 1 on failure.
 * 
 */
extern int	cti_addManifestBinary(cti_manifest_id_t mid, char *fstr);

/*
 * cti_addManifestLibrary - Add a library to an existing manifest.
 * 
 * Detail
 *      This function is used to add a shared library to an existing manifest
 *      based on the cti_manifest_id_t argument. The library will only be added
 *      to the manifest if it has a unique name to avoid redundant shipping.
 *      this is useful if a tool daemon needs to dlopen a shared library at some
 *      point during its lifetime. All libraries will be found in the 
 *      LD_LIBRARY_PATH of the tool daemon.
 *
 * Arguments
 *      mid -     The cti_manifest_id_t of the existing manifest.
 *      fstr -    The name of the shared library to add to the manifest. This
 *                can either be a fullpath name to the library or else the 
 *                library name if the library is found within LD_LIBRARY_PATH or
 *                any of the default system locations where shared libraries are
 *                stored.
 *
 * Returns
 *      0 on success, or else 1 on failure.
 * 
 */
extern int	cti_addManifestLibrary(cti_manifest_id_t mid, char *fstr);

/*
 * cti_addManifestFile - Add a regular file to an existing manifest.
 * 
 * Detail
 *      This function is used to add a regular file to an existing manifest
 *      based on the cti_manifest_id_t argument. The file will only be added to
 *      the manifest if it has a unique name to avoid redundant shipping. This
 *      is useful if a tool daemon needs to read a required configuration file
 *      fo a program or any other file that may be required by a tool daemon.
 *
 * Arguments
 *      mid -     The cti_manifest_id_t of the existing manifest.
 *      fstr -    The name of the file to add to the manifest. This can either 
 *                be a fullpath name to the file or else the file name if the 
 *                file is found within the users PATH.
 *
 * Returns
 *      0 on success, or else 1 on failure.
 * 
 */
extern int	cti_addManifestFile(cti_manifest_id_t mid, char *fstr);

/*
 * cti_sendManifest - Ship a manifest to an app_id and unpack it into temporary
 *                    storage.
 * 
 * Detail
 *      This function is used to ship a manifest to the compute nodes and unpack
 *      it into the applications toolhelper directory. This is useful if there 
 *      is a need to interact with third party tools that do not use this this
 *      interface. The returned cti_session_id_t can be used in the future to
 *      send additional manfiests to, or to exec tool daemons. In that case the 
 *      future manifests/tool daemons will share the same directory structure.
 *
 *      The manifest argument must be a valid cti_manifest_id_t that was created 
 *      before calling this function and will be cleaned up by this function. A 
 *      unique directory will be created on the compute node in the
 *      temporary storage space associated with the application. This avoids
 *      naming conflicts between other tools using this interface. This unique
 *      directory will contains subdirectories /bin that contains all binaries
 *      and /lib that contains all libaries. Any files are available directly
 *      in the current working directory of the tool daemon.
 *
 *      If the staging directory unpacked in the applications toolhelper 
 *      directory needs to be static, the CTI_DAEMON_STAGE_DIR_ENV_VAR
 *      environment variable can be used to define the location.
 *
 *      If the debug option evaluates to true, daemon launcher will attempt to 
 *      read the environment variable defined by CTI_DBG_LOG_DIR_ENV_VAR and
 *      create a log file at this location. If the environment variable is not
 *      found or is null, it will create a log file in the /tmp directory on the
 *      compute node. It will then dup the STDOUT/STDERR file channels to this
 *      log file. This is the only way to capture stdout/stderr output from a
 *      tool program on the compute nodes.
 *
 * Arguments
 *      app_id -  The cti_app_id_t of the registered application.
 *      mid -     The cti_manifest_id_t of the existing manifest.
 *      debug -   If true, create a log file at the location provided by
 *                CTI_DBG_LOG_DIR_ENV_VAR. Redirect stdout/stderr to the log
 *                files fd.
 *
 * Returns
 *      A non-zero SESSION_ID on success, or else 0 on failure.
 * 
 */
extern cti_session_id_t	cti_sendManifest(   cti_app_id_t      app_id, 
                                            cti_manifest_id_t mid, 
                                            int               debug);

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
 *      sid -     The cti_session_id_t of the existing session.
 *
 * Returns
 *      A null terminated array of strings, or else NULL on error.
 * 
 */
extern char **	cti_getSessionLockFiles(cti_session_id_t sid);

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
 *      sid -     The cti_session_id_t of the existing session.
 *
 * Returns
 *      A pointer to the path on success, or NULL on error.
 * 
 */
extern char *	cti_getSessionRootDir(cti_session_id_t sid);

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
 *      sid -     The cti_session_id_t of the existing session.
 *
 * Returns
 *      A pointer to the path on success, or NULL on error.
 * 
 */
extern char *	cti_getSessionBinDir(cti_session_id_t sid);

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
 *      sid -     The cti_session_id_t of the existing session.
 *
 * Returns
 *      A pointer to the path on success, or NULL on error.
 * 
 */
extern char *	cti_getSessionLibDir(cti_session_id_t sid);

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
 *      sid -     The cti_session_id_t of the existing session.
 *
 * Returns
 *      A pointer to the path on success, or NULL on error.
 * 
 */
extern char *	cti_getSessionFileDir(cti_session_id_t sid);

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
 *      sid -     The cti_session_id_t of the existing session.
 *
 * Returns
 *      A pointer to the path on success, or NULL on error.
 * 
 */
extern char *	cti_getSessionTmpDir(cti_session_id_t sid);

#endif /* _CRAY_TOOLS_FE_H */
