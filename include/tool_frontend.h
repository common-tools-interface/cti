/******************************************************************************\
 * tool_frontend.h - The public API definitions for the frontend portion of the
 *                   tool_interface.
 *
 * © 2011-2013 Cray Inc.  All Rights Reserved.
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

#ifndef _TOOL_FRONTEND_H
#define _TOOL_FRONTEND_H

#include <stdint.h>
#include <sys/types.h>

/*
 * The tool interface needs to read environment specific locations
 * dynamically at run time. The environment variables that are read
 * are defined here. Their values are the values users should use to set in 
 * their environment. Note that these are compile time constants in the library
 * and cannot be overridden.
 *
 * LIBAUDIT_ENV_VAR     Used to define the absolute path to the audit library
 * LIBAUDIT_KEYFILE_ENV_VAR
 *                      Used to define a path to a file used to create sys-v 
 *                      keys in the audit library. If the file doesn't exist, it
 *                      will be created at the provided location.
 * DBG_LOG_ENV_VAR      Optional variable used to define a path to write log
 *                      files to. Note that this location must be accessible by 
 *                      the compute nodes.
 * USER_DEF_APRUN_LOC_ENV_VAR
 *                      Used to define the absolute path to the aprun binary.
 *                      This is used when a site has renamed the real aprun
 *                      binary to something else.
 * PMI_ATTRIBS_TIMEOUT_ENV_VAR
 *                      Used to define the amount of time the daemon will spend
 *                      attempting to open the pmi_attribs file when gathering
 *                      application pid information on the compute node. If this
 *                      is not set, the default timeout period is 60 seconds.
 * PMI_EXTRA_SLEEP_ENV_VAR
 *                      Used to define an extra amount of time to sleep after
 *                      reading the pmi_attribs file if it was not immediately
 *                      available. This is to avoid a potential race condition.
 *                      If this is not set, the default is to wait an order of
 *                      magnitude less than the amount of time it took to open
 *                      the pmi_attribs file.
 * CFG_DIR_VAR          Used to define a location to write internal temporary
 *                      files and directories to. The caller must have write
 *                      permission inside this directory.
 * 
 */
#define LIBAUDIT_ENV_VAR            "CRAY_LD_VAL_LIBRARY"
#define LIBAUDIT_KEYFILE_ENV_VAR    "CRAY_LD_VAL_KEYFILE"
#define DBG_LOG_ENV_VAR             "CRAY_DBG_LOG_DIR"
#define USER_DEF_APRUN_LOC_ENV_VAR  "CRAY_APRUN_PATH"
#define PMI_ATTRIBS_TIMEOUT_ENV_VAR "CRAY_CTI_PMI_FOPEN_TIMEOUT"
#define PMI_EXTRA_SLEEP_ENV_VAR     "CRAY_CTI_PMI_EXTRA_SLEEP"
#define CFG_DIR_VAR                 "CRAY_CTI_CFG_DIR"
#define DAEMON_STAGE_DIR            "CRAY_CTI_STAGE_DIR"

/* 
 * typedefs used in return values 
 */

typedef struct
{
        char *  hostname;
        int     numPes;
} nodeHostPlacement_t;

typedef struct
{
        int                     numHosts;
        nodeHostPlacement_t *   hosts;
} appHostPlacementList_t;

typedef struct
{
	uint64_t	apid;
	pid_t		aprunPid;
} aprunProc_t;

typedef int MANIFEST_ID;

/*
 * alps_application functions - Functions relating directly to the application.
 */

/*
 * getApid - Obtain the apid associated with the aprun pid.
 *
 * Detail
 *      This function is used to obtain the apid of an aprun session based on
 *      the pid of the aprun binary. This can be used in place of apstat if the
 *      pid_t of aprun is already known.
 *
 * Arguments
 *      aprunPid - The pid_t of the registered aprun session.
 *
 * Returns
 *      apid if found, or else 0 on failure/not found.
 * 
 */
extern uint64_t	getApid(pid_t aprunPid);

/*
 * registerApid -   Assists in registering the apid of an already running 
 *                  aprun session for use with the tool interface.
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
 *      apid - The uint64_t apid of the aprun session to register.
 *
 * Returns
 *      0 on success, or else 1 on failure.
 * 
 */
extern int	registerApid(uint64_t apid);

/*
 * deregisterApid - Assists in cleaning up internal allocated memory
 *                  associated with a previously registered aprun sessions
 *                  apid.
 * 
 * Detail
 *      For applications that use the tool interface that wish to operate over
 *      many different aprun sessions, this function can be used to free up
 *      and destroy any internal data structures that were created for use
 *      with the apid of the aprun session.
 *
 * Arguments
 *      apid - The uint64_t apid of the previously registered aprun session.
 *
 * Returns
 *      Returns no value.
 *
 */
extern void	deregisterApid(uint64_t apid);

/*
 * getNodeCName - Returns the cabinet hostname of the callers login node.
 * 
 * Detail
 *      This function determines the cname of the callers login node where
 *      any aprun sessions will be launched from.
 *
 * Arguments
 *      None.
 *
 * Returns
 *      A string containing the cname host, or else a null string on error.
 * 
 */
extern char *	getNodeCName();

/*
 * getNodeNid - Returns the node id of the callers login node.
 * 
 * Detail
 *      This function determines the nid (node id) of the callers login node
 *      where any aprun sessions will be launched from. This can be used to
 *      check if the current nid differs from the applications nid.
 *
 * Arguments
 *      None.
 *
 * Returns
 *      The integer value of the nid, or else -1 on error.
 * 
 */
extern int	getNodeNid();

/*
 * getAppNid - Returns the node id for the application associated with the apid.
 * 
 * Detail
 *      This function determines the nid (node id) associated with the given 
 *      apid. This can be used to check if the current nid differs from the 
 *      applications nid.
 *
 * Arguments
 *      None.
 *
 * Returns
 *      The integer value of the nid, or else -1 on error.
 * 
 */
extern int	getAppNid();

/*
 * getNumAppPEs - Returns the number of processing elements in the application
 *                associated with the apid.
 * 
 * Detail
 *      This function is used to determine the number of PEs (processing
 *      elements) that were propagated by ALPS for the application associated
 *      with the given apid.
 *
 * Arguments
 *      apid - The uint64_t apid of the previously registered aprun session.
 *
 * Returns
 *      Number of PEs in the application, or else 0 on error.
 * 
 */
extern int	getNumAppPEs(uint64_t apid);

/*
 * getNumAppNodes - Returns the number of compute nodes allocated for the
 *                  application associated with the apid.
 * 
 * Detail
 *      This function is used to determine the number of compute nodes that
 *      was allocated by ALPS for the application associated with the given
 *      apid.
 *
 * Arguments
 *      apid - The uint64_t apid of the previously registered aprun session.
 *
 * Returns
 *      Number of compute nodes allocated for the application,
 *      or else 0 on error.
 * 
 */
extern int	getNumAppNodes(uint64_t apid);

/*
 * getAppHostsList - Returns a null terminated array of strings containing
 *                   the hostnames of the compute nodes allocated by ALPS
 *                   for the application associated with the apid.
 * 
 * Detail
 *      This function creates a list of compute node hostnames for each
 *      compute node assoicated with the given apid. These hostnames
 *      can be used to communicate with the compute nodes over socket
 *      connections. The list is null terminated.
 *
 * Arguments
 *      apid - The uint64_t apid of the previously registered aprun session.
 *
 * Returns
 *      A null terminated list of pointers to strings, or else a null
 *      pointer on error.
 * 
 */
extern char **	getAppHostsList(uint64_t apid);

/*
 * getAppHostsPlacement - Returns a appHostPlacementList_t struct containing
 *                        nodeHostPlacement_t entries that contain the hostname
 *                        of the compute nodes allocated by ALPS and the number
 *                        of PEs assigned to that host for the application 
 *                        associated with the apid.
 * 
 * Detail
 *      This function creates a appHostPlacementList_t struct that contains
 *      the number of hosts associated with the application and 
 *      nodeHostPlacement_t struct entries that contain the hostname string
 *      along with the number of PEs assigned to this host. Note that there is
 *      a nodeHostPlacement_t entry for each compute node hostname that is 
 *      assoicated with the given apid. These hostnames can be used to
 *      communicate with the compute nodes over socket connections.
 *
 * Arguments
 *      apid - The uint64_t apid of the previously registered aprun session.
 *
 * Returns
 *      An appHostPlacementList_t struct that contains the number of hosts in
 *      the application and an array of nodeHostPlacement_t structs for each
 *      host assigned to the application.
 * 
 */
extern appHostPlacementList_t *	getAppHostsPlacement(uint64_t apid);

/*
 * destroy_AppHostsPlacement - Used to destroy the memory allocated for a 
 *                        appHostPlacementList_t struct.
 * 
 * Detail
 *      This function free's a appHostPlacementList_t struct. This is used to
 *      safely destroy the data structure returned by a call to the 
 *      getAppHostsPlacement function when the caller is done with the data
 *      that was allocated during its creation.
 *
 * Arguments
 *      placement_list - A pointer to the appHostPlacementList_t to free.
 *
 * Returns
 *      Void. This function behaves similarly to free().
 *
 */
extern void	destroy_appHostPlacementList(appHostPlacementList_t *placement_list);

/*
 * alps_run functions - Functions related to launching a new aprun session
 */


/*
 * launchAprun_barrier - Start a new aprun session from the provided argv array
 *                       and have ALPS hold the application at its MPI startup
 *                       barrier.
 * 
 * Detail
 *      This function is the preferred way for users to use the tool interface
 *      to launch aprun sessions. Its important to note that the argv is not
 *      a traditional argv in the sense that argv[0] is the start of actual
 *      arguments provided to the aprun call and not the name of aprun itself.
 *
 *      This function will hold the application at its startup barrier until
 *      the barrier release function is called with the apid returned by this
 *      function.
 *
 *      This function can redirect the stdin of aprun to a file in order to
 *      provide arguments to a application. This is enabled by setting the
 *      argument redirectInput to true and providing a string that represents
 *      the pathname to the file. If redirectInput is false, the function will
 *      redirect the stdin of aprun to /dev/null to prevent aprun from grabbing
 *      any input on stdin which may be used by other tool programs. 
 *
 *      This function can also optionally redirect stdout/stderr to provided
 *      open file descriptors by setting redirectOutput to true (non-zero).
 *
 *      This function can optionally set the current working directory for the 
 *      aprun process. This is useful if the aprun session should start 
 *      somewhere other than the current PWD if the application creates files. 
 *
 *      This function can optionally set environment variables for the aprun
 *      process. This is useful if the aprun session should use an environment
 *      variable that is different than the current value set in the parent.
 *
 * Arguments
 *      aprun_argv -    A null terminated list of arguments to pass directly
 *                      to aprun. This differs from a traditional argv in the
 *                      sense that aprun_argv[0] is the start of the actual
 *                      arguments passed to aprun and not the name of aprun
 *                      itself.
 *      redirectOutput - Toggles redirecting aprun stdout/stderr to the provided
 *                      file descriptors.
 *      redirectInput - Toggles redirecting aprun stdin to the provided file
 *                      name.
 *      stdout_fd -     The open file descriptor to redirect stdout to if 
 *                      redirectOutput evaluates to true.
 *      stderr_fd -     The open file descriptor to redirect stderr to if
 *                      redirectOutput evaluates to true.
 *      inputFile -     The pathname of a file to open and redirect stdin to if
 *                      redirectInput evaluates to true.
 *      chdirPath -     The path to change the current working directory of 
 *                      aprun to, or NULL if no chdir is to take place.
 *      env_list -      A null terminated list of strings of the form 
 *                      "name=value". The value of name in the environment will
 *                      be set to value irrespective of name already existing in
 *                      the environment. 
 *
 * Returns
 *      A aprunProc_t pointer that contains the pid of the aprun process as well
 *      as its apid. The returned apid should be use in subsequent calls to this
 *      interface. NULL is returned on error. It is the callers responsibility
 *      to free the returned type with a call to free().
 * 
 */
extern aprunProc_t * launchAprun_barrier(char **aprun_argv, int redirectOutput, 
                               int redirectInput, int stdout_fd, int stderr_fd,
                               char *inputFile, char *chdirPath, 
                               char **env_list);

/*
 * releaseAprun_barrier - Release the aprun session launched with the
 *                        launchAprun_barrier function from its MPI startup
 *                        barrier.
 * 
 * Detail
 *      This function communicates to the aprun process that ALPS should
 *      release the application from the MPI startup barrier it is currently
 *      being held at. Note that this function must be used in conjunction with
 *      a valid uint64_t apid that was created by the launchAprun_barrier 
 *      function.
 *
 * Arguments
 *      apid - The uint64_t apid of the aprun session started with a call to
 *             launchAprun_barrier
 *
 * Returns
 *      0 on success, or else 1 on failure.
 * 
 */
extern int	releaseAprun_barrier(uint64_t apid);

/*
 * killAprun - Send a signal using the apkill mechanism to a aprun session
 * 
 * Detail
 *      This function is used to send a provided signal to the uint64_t apid
 *      associated with a valid aprun session. It uses the apkill mechansim to 
 *      ensure the proper delivery of the signal to compute nodes.
 *
 * Arguments
 *      apid -    The uint64_t apid of the aprun session to send the signal to.
 *      signum -  The signal number (defined in signal.h) to send to the aprun 
 *                session.
 *
 * Returns
 *      0 on success, or else 1 on failure.
 * 
 */
extern int	killAprun(uint64_t apid, int signum);


/*
 * alps_transfer functions - Functions related to shipping files, shared 
 *                           libraries, and binaries to compute nodes and 
 *                           possibly launching tool daemons.
 *
 * NOTE: Any of the functions defined in this section will keep track of files
 *       that were previously shipped to compute nodes and will not allow a
 *       naming conflict to occur between consecutive calls. This eliminates
 *       redundant shipping of dependencies between multiple calls.
 */


/*
 * execToolDaemon - Launch a tool program onto compute nodes associated with a
 *                 registered aprun pid.
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
 *      A MANIFEST_ID can be provided to ship a manifest of binaries, libraries,
 *      and files that may be required by the tool daemon. The manifest must be
 *      created before calling this function and will be cleaned up by this
 *      function. A unique directory will be created on the compute node in the
 *      temporary storage space associated with the application. This avoids
 *      naming conflicts between other tools using the ALPS toolhelper
 *      interface. This unique directory will contains subdirectories /bin that
 *      contains all binaries and /lib that contains all libaries. Any files
 *      will not be placed in a subdirectory and are available directly in the
 *      current working directory of the tool daemon. All binaries will be found
 *      in the PATH of the tool daemon process, and all libraries will be found
 *      in the LD_LIBRARY_PATH of the tool daemon process.
 *
 *      If the debug option evaluates to true, daemon launcher will attempt to 
 *      read the environment variable defined by DBG_LOG_ENV_VAR and create a
 *      log file at this location. If the environment variable is not found or
 *      is null, it will create a log file in the /tmp directory on the compute
 *      node. It will then dup the STDOUT/STDERR file channels to this log file.
 *      This is the only way to capture stdout/stderr output from a tool program
 *      on the compute nodes.
 *
 * Arguments
 *      apid -    The uint64_t apid of the registered aprun session to launch
 *                the tool program to.
 *      mid -     The optional manifest id of a previously created manifest, or
 *                0 if no manifest is required.
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
 *                DBG_LOG_ENV_VAR. Redirect stdout/stderr to the log
 *                files fd.
 *
 * Returns
 *      0 on success, or else 1 on failure.
 * 
 */
extern int	execToolDaemon(uint64_t apid, MANIFEST_ID mid, char *fstr, 
                           char **args, char **env, int debug);

/*
 * createNewManifest - Create a new manifest to ship additional binaries, 
 *                     libraries, and files when exec'ing a tool daemon.
 * Detail
 *      This function is used to create a new internal manifest list used to
 *      ship additional binaries, libraries, and files with a tool daemon
 *      process. The internal list will be cleaned up upon passing it as an
 *      argument when launching the tool daemon. Only unique binary, library,
 *      and file names will be added to the manifest. For instance, if multiple
 *      binaries require libc.so, it will only be added once to the manifest.
 *
 * Arguments
 *      None.
 *
 * Returns
 *      A non-zero MANIFEST_ID on success, or else 0 on failure.
 *
 */
extern MANIFEST_ID	createNewManifest(void);

/*
 * addManifestBinary - Add a program executable to an existing manifest.
 * 
 * Detail
 *      This function is used to add a program binary to an existing manifest
 *      based on the MANIFEST_ID argument. The program binary along with any
 *      required shared library dependencies determined by the LD_AUDIT 
 *      interface will be added to the manifest. This is useful if a tool daemon
 *      needs to fork/exec another program at some point during its lifetime.
 *      All binaries will be found in the PATH of the tool daemon, and all
 *      libraries will be found in the LD_LIBRARY_PATH of the tool daemon.
 *
 * Arguments
 *      mid -     The MANIFEST_ID of the existing manifest.
 *      fstr -    The name of the binary to add to the manifest. This can either
 *                be a fullpath name to the file or else the file name if the 
 *                binary is found within PATH.
 *
 * Returns
 *      0 on success, or else 1 on failure.
 * 
 */
extern int	addManifestBinary(MANIFEST_ID mid, char *fstr);

/*
 * addManifestLibrary - Add a library to an existing manifest.
 * 
 * Detail
 *      This function is used to add a shared library to an existing manifest
 *      based on the MANIFEST_ID argument. The library will only be added to the
 *      manifest if it has a unique name to avoid redundant shipping. This is
 *      useful if a tool daemon needs to dlopen a shared library at some point
 *      during its lifetime. All libraries will be found in the LD_LIBRARY_PATH
 *      of the tool daemon.
 *
 * Arguments
 *      mid -     The MANIFEST_ID of the existing manifest.
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
extern int	addManifestLibrary(MANIFEST_ID mid, char *fstr);

/*
 * addManifestFile - Add a regular file to an existing manifest.
 * 
 * Detail
 *      This function is used to add a regular file to an existing manifest
 *      based on the MANIFEST_ID argument. The file will only be added to the
 *      manifest if it has a unique name to avoid redundant shipping. This is
 *      useful if a tool daemon needs to read a required configuration file for
 *      a program or any other file that may be required by a tool daemon.
 *
 * Arguments
 *      mid -     The MANIFEST_ID of the existing manifest.
 *      fstr -    The name of the file to add to the manifest. This can either 
 *                be a fullpath name to the file or else the file name if the 
 *                file is found within the users PATH.
 *
 * Returns
 *      0 on success, or else 1 on failure.
 * 
 */
extern int	addManifestFile(MANIFEST_ID mid, char *fstr);

#endif /* _TOOL_FRONTEND_H */
