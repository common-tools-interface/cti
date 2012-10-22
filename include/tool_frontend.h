/******************************************************************************\
 * tool_frontend.h - The public API definitions for the frontend portion of the
 *                   tool_interface.
 *
 * © 2011-2012 Cray Inc.  All Rights Reserved.
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
 * are defined here.
 *
 * LIBAUDIT_ENV_VAR     Used to define the absolute path to the audit library
 * DBG_LOG_ENV_VAR      Optional variable used to define a path to write log
 *                      files to. Note that this location must be accessible by 
 *                      the compute nodes.
 */
#define LIBAUDIT_ENV_VAR        "LD_VAL_LIBRARY"
#define DBG_LOG_ENV_VAR         "DBG_LOG_DIR"

/* 
 * struct typedefs used in return values 
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
                               char *inputFile, char *chdirPath);

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
 * sendCNodeExec - Launch a tool program onto compute nodes associated with a
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
 *      The user can make multiple consecutive calls to this function and any
 *      previously shipped shared library dependency that is also used by the
 *      newest binary will not be redundantly shipped. This interface does
 *      not currently support naming conflicts between files and will refuse to
 *      ship a file with a conflicting name to a previously shipped file.
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
extern int	sendCNodeExec(uint64_t apid, char *fstr, char **args, 
                        char **env, int debug);

/*
 * sendCNodeBinrary - Ship a program executable to the compute nodes associated 
 *                    with a registered aprun pid without actually launching 
 *                    the binary itself.
 * 
 * Detail
 *      This function is used to ship a program binary to compute nodes. It will
 *      take care of shipping the binrary, determine shared library dependencies
 *      using the LD_AUDIT interface, and ship any required shared library
 *      dependencies to compute nodes. It will not actually start the shipped
 *      binary. This is useful if a running tool process needs to fork/exec 
 *      another program at some point during its lifetime.
 *
 * Arguments
 *      apid -    The uint64_t apid of the registered aprun session to ship the
 *                program binary to.
 *      fstr -    The name of the binary to ship to the compute nodes.
 *                This can either be a fullpath name to the file or else
 *                the file name if the binary is found within the users
 *                PATH.
 *
 * Returns
 *      0 on success, or else 1 on failure.
 * 
 */
extern int	sendCNodeBinary(uint64_t apid, char *fstr);

/*
 * sendCNodeLibrary - Ship a shared library to the compute nodes associated
 *                    with a registered aprun pid.
 * 
 * Detail
 *      This function is used to ship a shared library to compute nodes. This is
 *      useful for programs that wish to dlopen a shared library at some point
 *      during its lifetime.
 *
 * Arguments
 *      apid -    The uint64_t apid of the registered aprun session to ship the
 *                shared library to.
 *      fstr -    The name of the shared library to ship to the compute
 *                nodes. This can either be a fullpath name to the library
 *                or else the library name if the library is found within
 *                the users LD_LIBRARY_PATH or any of the default system
 *                locations where shared libraries are stored.
 *
 * Returns
 *      0 on success, or else 1 on failure.
 * 
 */
extern int	sendCNodeLibrary(uint64_t apid, char *fstr);

/*
 * sendCNodeFile - Ship a regular file to the compute nodes associated with a
 *                 registered aprun pid.
 * 
 * Detail
 *      This function is used to ship a regular file to compute nodes. This is
 *      useful for shipping a required configuration file for a program or any
 *      other file that may be required by a tool program.
 *
 * Arguments
 *      apid -    The uint64_t apid of the registered aprun session to ship the
 *                file to.
 *      fstr -    The name of the file to ship to the compute nodes. This
 *                can either be a fullpath name to the file or else the
 *                file name if the file is found within the users PATH.
 *
 * Returns
 *      0 on success, or else 1 on failure.
 * 
 */
extern int	sendCNodeFile(uint64_t apid, char *fstr);

#endif /* _TOOL_FRONTEND_H */
