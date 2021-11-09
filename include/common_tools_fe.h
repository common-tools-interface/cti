/*****************************************************************************\
 * common_tools_fe.h - The public API definitions for the frontend portion of
 *                     the common tools interface. Frontend refers to the
 *                     location where applications are launched.
 *
 * Copyright 2011-2021 Hewlett Packard Enterprise Development LP.
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 *****************************************************************************/

#ifndef _COMMON_TOOLS_FE_H
#define _COMMON_TOOLS_FE_H

#include "common_tools_shared.h"

#ifdef __cplusplus
extern "C" {
#endif

/************************************************************
 * Types defined by the common tools interface frontend
 ***********************************************************/

/*
 *  This enum enumerates the various attributes that
 *  can be set by cti_setAttribute.
 */
typedef enum
{
    CTI_ATTR_STAGE_DEPENDENCIES,
    CTI_LOG_DIR,
    CTI_DEBUG,
    CTI_PMI_FOPEN_TIMEOUT,
    CTI_EXTRA_SLEEP
} cti_attr_type_t;

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
    char **  binaries;
    int *    rankMap;
} cti_binaryList_t;

typedef int64_t cti_app_id_t;
typedef int64_t cti_session_id_t;
typedef int64_t cti_manifest_id_t;

/*******************************************************************************
 * The common tools interface frontend calls are defined below.
 ******************************************************************************/

/*******************************************************************************
 * The following functions can be called at any time.
 ******************************************************************************/

/*
 * cti_version - Returns the version string of the frontend library.
 *
 * Detail
 *      This function returns the version string of the frontend library. This
 *      can be used to check for compatibility.
 *
 * Arguments
 *      None.
 *
 * Returns
 *      A string containing the current frontend library version in the form
 *      major.minor.revision.   For a libtool current:revison:age format
 *      major = current - age and minor = age.
 *
 */
const char * cti_version(void);

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
const char * cti_error_str(void);

/*
 * cti_error_str_r - Copies an error string associated with a command that
 *                   returned an error into user-provided buffer (reentrant)
 *
 * Detail
 *      This function copies the internal error string associated with a failed
 *      command into a user-provided buffer. This string can be used to print
 *      an informative message about why an API call failed. If no error is
 *      known, the string will contain "Unknown CTI error". This function is
 *      reentrant.
 *
 * Arguments
 *      buf - buffer to fill with error string
 *      buf_len - length of user-provided buffer
 *
 * Returns
 *      0 upon success, or ERANGE if buf_len is invalid
 *
 */
int cti_error_str_r(char *buf, size_t buf_len);

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
 *      A cti_wlm_type_t that contains the current WLM in use on the system.
 *
 */
cti_wlm_type_t cti_current_wlm(void);

/*
 * cti_wlm_type_toString - Obtain the stringified representation of the
 *                         cti_wlm_type_t.
 *
 * Detail
 *      This call can be used to turn the cti_wlm_type_t returned by
 *      cti_current_wlm into a human readable format.
 *
 * Arguments
 *      wlm_type - The cti_wlm_type_t to stringify
 *
 * Returns
 *      A string containing the human readable format.
 *
 */
const char * cti_wlm_type_toString(cti_wlm_type_t wlm_type);

/*
 * cti_getHostname - Returns an externally-accessible address for
 * the current node.
 *
 * Detail
 *      This function determines an externally-accessible hostname
 *      or IP address for the current node. This address can be used
 *      by tool daemons running on other systems to create socket
 *      connections to the node.
 *
 * Arguments
 *      None.
 *
 * Returns
 *      A string containing the address, or else a null string on error.
 *
 */
char * cti_getHostname();

/*
 * cti_setAttribute - Set an attribute to a specified value. Used for
 *                    modifying runtime configuration.
 *
 * Detail
 *      This function sets the attribute 'attrib' to requested 'value'.
 *
 * Arguments
 *      attrib - The cti_attr_type_t as defined below.
 *
 *          CTI_ATTR_STAGE_DEPENDENCIES
 *              Define whether binary and library dso dependencies should be
 *              automatically staged by cti_addManifestBinary and
 *              cti_addManifestLibrary. Set to "0" or "1" to disable or enable
 *              respectively.
 *
 *              Default: "1" or enabled
 *
 *          CTI_LOG_DIR
 *              Define a path to write log files to. This location must be
 *              cross mounted and accessible by the compute nodes in order
 *              to receive debug logs from tool daemons. The value set here
 *              overrides the CTI_LOG_DIR environment variable.
 *
 *              Default: "/tmp"
 *
 *          CTI_DEBUG
 *              Used to turn on debug logging and redirection of tool daemon
 *              stdout/stderr to a log file. This should be used in conjuction
 *              with the CTI_LOG_DIR environment variable or CTI_LOG_DIR
 *              attrib. The value set here overrides the CTI_DEBUG
 *              environment variable. Set to "0" or "1" to disable or enable
 *              respectively.
 *
 *              Default: "0" or disabled
 *
 *          CTI_PMI_FOPEN_TIMEOUT
 *              Used to define the amount of time in seconds the backend daemon
 *              will attempt to open the pmi_attribs file when gathering
 *              application pid information on the compute node. This file may
 *              be generated by the system PMI, or it might be delivered as part
 *              of the underlying CTI implementation.
 *
 *              Default: "60" or 60 seconds
 *
 *          CTI_EXTRA_SLEEP
 *              Used to define an extra amount of time to sleep before reading
 *              from the pmi_attribs file if it was not immediately available
 *              for reading. This is to avoid a potential race condition during
 *              attach. If the pmi_attribs file is generated by the system pmi
 *              implementation, starting a tool daemon early in the application
 *              lifecycle can encounter a race condition where the file is in
 *              the process of being written while the backend daemon is reading
 *              from it.
 *
 *              Default: variable or wait an order of magnitude less time in
 *                       seconds than the time it took to discover the
 *                       pmi_attribs file.
 *
 * Returns
 *      0 on success, or else 1 on failure
 *
 */
int cti_setAttribute(cti_attr_type_t attrib, const char *value);

/*
 * cti_getAttribute - Get 'value' of 'attrib'
 *
 * Detail
 *      This function returns the current 'value' of the requested attribute
 *      'attrib'. The content of the returned string is defined by the
 *      specific 'attrib'. See cti_setAttribute above to details about the
 *      'value' string.
 *
 * Arguments
 *      attrib - The requested cti_attr_type_t. See cti_setAttribute for
 *               details.
 *
 * Returns
 *      A string containing the 'value', or else a null string on error.
 *
 */
const char * cti_getAttribute(cti_attr_type_t attrib);

/******************************************************************************
 * The following functions require the application to be started or registered
 * with the interface before calling. All API calls require a cti_app_id_t
 * argument.
 *****************************************************************************/

/*
 * cti_appIsValid - Test if a cti_app_id_t is valid.
 *
 * Detail
 *      This function is used to test if a cti_app_id_t is valid. An app_id
 *      becomes invalid for future use upon calling the cti_deregisterApp
 *      function, or upon job completion.
 *
 * Arguments
 *      app_id - The cti_app_id_t of the previously registered application.
 *
 * Returns
 *      0 if the app_id is invalid, 1 if the app_id is still valid.
 *
 */
int cti_appIsValid(cti_app_id_t app_id);

/*
 * cti_deregisterApp - Assists in cleaning up internal allocated memory
 *                     associated with a previously registered application.
 *
 * Detail
 *      For applications that use the tool interface that wish to operate over
 *      many different applications at once, this function can be used to free
 *      up and destroy any internal data structures that were created for use
 *      with the app_id of the registered application. The app_id will no longer
 *      be valid for future use.
 *
 *      If the cti_launchApp or cti_launchAppBarrier functions were used to
 *      start the application, the caller must call cti_deregisterApp before
 *      exiting. Failing to do so will cause the application process to be
 *      force killed with SIGKILL.
 *
 *      Any tool daemons started on the compute nodes will continue executing
 *      after calling this function. If the tool daemons need to be killed,
 *      the cti_destroySession function needs to be called before calling this
 *      function.
 *
 * Arguments
 *      app_id - The cti_app_id_t of the previously registered application.
 *
 * Returns
 *      Returns no value.
 *
 */
void cti_deregisterApp(cti_app_id_t app_id);

/*
 * cti_getLauncherHostName - Returns the hostname of the login node where the
 *                           application launcher process resides.
 *
 * Detail
 *      This function determines the hostname of the login node where the
 *      application launcher used to launch the registerd app_id resides. This
 *      hostname may be different from the result returned by cti_getHostname.
 *      An application launcher refers to the srun/mpiexec process.
 *
 * Arguments
 *      app_id -  The cti_app_id_t of the registered application.
 *
 * Returns
 *      A string containing the launcher host, or else a null string on error.
 *
 */
char * cti_getLauncherHostName(cti_app_id_t app_id);

/*
 * cti_getNumAppPEs - Returns the number of processing elements in the
 *                    application associated with the app_id.
 *
 * Detail
 *      This function is used to determine the number of PEs (processing
 *      elements) for the application associated with the given app_id. A PE
 *      typically represents a single MPI rank. For MPMD applications, this
 *      returns the total PEs across all apps.
 *
 * Arguments
 *      app_id -  The cti_app_id_t of the registered application.
 *
 * Returns
 *      Number of PEs in the application, or else 0 on error.
 *
 */
int cti_getNumAppPEs(cti_app_id_t app_id);

/*
 * cti_getNumAppNodes - Returns the number of compute nodes allocated for the
 *                      application associated with the app_id.
 *
 * Detail
 *      This function is used to determine the number of compute nodes that
 *      are allocated by the application launcher for the application associated
 *      with the given app_id. For MPMD applications, this returns the number of
 *      compute nodes allocated across all apps.
 *
 * Arguments
 *      app_id -  The cti_app_id_t of the registered application.
 *
 * Returns
 *      Number of compute nodes allocated for the application,
 *      or else 0 on error.
 *
 */
int cti_getNumAppNodes(cti_app_id_t app_id);

/*
 * cti_getAppHostsList - Returns a null terminated array of strings containing
 *                       the hostnames of the compute nodes allocated by the
 *                       application launcher for the application associated
 *                       with the app_id.
 *
 * Detail
 *      This function returns a list of compute node hostnames for each
 *      compute node assoicated with the given app_id. These hostnames
 *      can be used to communicate with the compute nodes over socket
 *      connections. The list is null terminated. It is the callers
 *      responsibility to free the returned list of strings. For MPMD
 *      applications, this returns the hosts associated with all apps.
 *
 * Arguments
 *      app_id -  The cti_app_id_t of the registered application.
 *
 * Returns
 *      A null terminated list of pointers to strings, or else a null
 *      pointer on error.
 *
 */
char ** cti_getAppHostsList(cti_app_id_t app_id);

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
 *      For MPMD applications, this returns the hosts associated with all apps.
 *
 * Arguments
 *      app_id -  The cti_app_id_t of the registered application.
 *
 * Returns
 *      An cti_hostsList_t that contains the number of hosts in the application
 *      and an array of cti_host_t for each host assigned to the application.
 *
 */
cti_hostsList_t * cti_getAppHostsPlacement(cti_app_id_t app_id);

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
void cti_destroyHostsList(cti_hostsList_t *placement_list);

/*
 * cti_getAppBinaryList - Returns a structure containing the following:
 *      * array of strings containing the paths to the binaries for the
 *        application associated with the app_id. The array is NULL-terminated
 *      * array of ints, where index N refers to the binary path for rank N.
 *        The total number of elements is equal to the number of ranks,
 *        obtained from cti_getNumAppPEs.
 *
 * Detail
 *      This function returns a list of binary paths associated with the
 *      given app_id. For non-MPMD applications, the structure will contain
 *      one binary path. For MPMD applications, the structure contains multiple
 *      binary paths. The rank-binary map portion of the structure maps a rank N
 *      at index N to an index in the binary path list.
 *
 * Arguments
 *      app_id -  The cti_app_id_t of the registered application.
 *
 * Returns
 *      A completed cti_binaryList_t object as described above, or else a null
 *      pointer on error. The resulting object should be freed with
 *      cti_destroyBinaryList.
 *
 */
cti_binaryList_t* cti_getAppBinaryList(cti_app_id_t app_id);


/*
 * cti_destroyBinariesList - Used to destroy the memory allocated for a
    *                        cti_binaryList_t struct.
 *
 * Detail
 *      This function frees a cti_binaryList struct. This is used to
 *      safely destroy the data structure returned by a call to the
 *      cti_getAppBinaryList function when the caller is done with the data
 *      that was allocated during its creation. Performs no operation when
 *      provided a NULL pointer.
 *
 * Arguments
 *      binary_list - A pointer to the cti_binaryList_t to free.
 *
 * Returns
 *      Void. This function behaves similarly to free().
 *
 */
void cti_destroyBinaryList(cti_binaryList_t *binary_list);


/*******************************************************************************
 * Run functions - Functions related to starting and/or killing applications
 *                 using system application launchers like aprun, srun, or
 *                 mpirun.
 ******************************************************************************/

/*
 * cti_launchApp - Launch an application using the application launcher.
 *
 * Detail
 *      This function will launch an application. The application launcher to
 *      use will be automatically determined based on the current workload
 *      manager of the system. It is up to the caller to ensure that valid
 *      launcher_argv arguments are provided that correspond to the application
 *      launcher.
 *
 *      The stdout/stderr of the launcher process can be redirected to an open
 *      file descriptor. This is enabled by providing valid file descriptors
 *      that are opened for writing to the stdout_fd and/or stderr_fd arguments.
 *      If the stdout_fd and/or stderr_fd arguments are -1, then the
 *      stdout/stderr inherited from the caller will be used.
 *
 *      The stdin of the launcher process can be redirected from a file. This
 *      is enabled by providing a string with the pathname of the file to the
 *      inputFile argument. If inputFile is NULL, then the stdin of the
 *      launcher will be redirected from /dev/null.
 *
 *      The current working directory for the launcher process can be changed
 *      from its current location. This is enabled by providing a string with
 *      the pathname of the directory to cd to to the chdirPath argument. If
 *      chdirPath is NULL, then no cd will take place.
 *
 *      The environment of the launcher can be modified. Any environment
 *      variable to set in the launcher process should be provided as a null
 *      terminated list of strings of the form "name=value" to the env_list
 *      argument. All other environment variables set in the caller process
 *      will be inherited by the launcher process. If env_list is NULL, no
 *      changes to the environment will be made.
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
cti_app_id_t cti_launchApp( const char * const  launcher_argv[],
                            int                 stdout_fd,
                            int                 stderr_fd,
                            const char *        inputFile,
                            const char *        chdirPath,
                            const char * const  env_list[]);

/*
 * cti_launchApp_fd - Same as cti_launchApp, but taking a file descriptor
 *                    input parameter instead of an input file path.
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
 *      stdin_fd  -      The file descriptor from which to redirect stdin or
 *                       -1 if no redirection should take place. If -1,
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
cti_app_id_t cti_launchApp_fd( const char * const  launcher_argv[],
                              int                 stdout_fd,
                              int                 stderr_fd,
                              int                 stdin_fd,
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
 *      The startup barrier is the point at which the application processes
 *      have been started, but are being held in a CTOR before main() has been
 *      called. Holding an application at this point can guarantee that tool
 *      daemons can be started before the application code starts executing.
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
cti_app_id_t cti_launchAppBarrier(  const char * const  launcher_argv[],
                                    int                 stdout_fd,
                                    int                 stderr_fd,
                                    const char *        inputFile,
                                    const char *        chdirPath,
                                    const char * const  env_list[]);

/*
 * cti_launchAppBarrier_fd - Same as cti_launchAppBarrier, but taking a
 *                           file descriptor input parameter instead of
 *                           an input file path.
 * Arguments
 *      See the cti_launchApp_fd description for more information.
 *
 * Returns
 *      A cti_app_id_t that contains the id registered in this interface. This
 *      app_id should be used in subsequent calls. 0 is returned on error.
 *
 */
cti_app_id_t cti_launchAppBarrier_fd(   const char * const  launcher_argv[],
                                        int                 stdout_fd,
                                        int                 stderr_fd,
                                        int                 stdin_fd,
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
int cti_releaseAppBarrier(cti_app_id_t app_id);

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
int cti_killApp(cti_app_id_t app_id, int signum);

/*******************************************************************************
 * WLM specific extensions - Interface for defining WLM specific extensions.
 ******************************************************************************/

/*
 * cti_open_ops - Open the WLM specific extensions for the current WLM in use
 *                on the system
 *
 * Detail
 *      This function is used to open the WLM specific interface extensions for
 *      the current WLM in use on the system. The details of returned ops
 *      interfaces returned are defined below for each WLM.
 *
 * Arguments
 *      ops - A pointer to a pointer of one of the WLM ops structs defined below.
 *
 * Returns
 *      A cti_wlm_type_t that contains the current WLM in use on the system.
 *      The ops argument will be set to point at the cooresponding ops struct
 *      for the WLM in use on the system, or NULL if there are no WLM specific
 *      extensions.
 *
 */
cti_wlm_type_t cti_open_ops(void **ops);

/*-----------------------------------------------------------------------------
 * cti_alps_ops extensions - Extensions for the ALPS WLM
 *-----------------------------------------------------------------------------
 * registerApid - Assists in registering the application ID of an already
 *                running aprun application for use with the common tools
 *                interface.
 *
 * Detail
 *      This function is used for registering a valid aprun application that was
 *      previously launched through external means for use with the tool
 *      interface. It is recommended to use the built-in functions to launch
 *      applications, however sometimes this is impossible (such is the case for
 *      a debug attach scenario). In order to use any of the functions defined
 *      in this interface, the apid of the aprun application must be
 *      registered. This is done automatically when using the built-in functions
 *      to launch applications. The apid can be obtained from apstat.
 *
 * Arguments
 *      apid - The application ID of the aprun application to register.
 *
 * Returns
 *      A cti_app_id_t that contains the id registered in this interface. This
 *      app_id should be used in subsequent calls. 0 is returned on error.
 *-----------------------------------------------------------------------------
 * getApid - Obtain application ID of aprun process
 *
 * Detail
 *      This function is used to obtain the apid of an existing aprun
 *      application.
 *
 * Arguments
 *      aprunPid - The PID of the aprun application to query.
 *
 * Returns
 *      An ALPS application ID. 0 is returned on error.
 *-----------------------------------------------------------------------------
 * getAprunInfo - Obtain information about the aprun process
 *
 * Detail
 *      This function is used to obtain the apid / launcher PID of an aprun
 *      application based on the registered app_id. It is the caller's
 *      responsibility to free the allocated storage with free() when it is no
 *      longer needed.
 *
 * Arguments
 *      appId -  The cti_app_id_t of the registered application.
 *
 * Returns
 *      A cti_aprunProc_t pointer that contains the apid / launcher PID of aprun.
 *      NULL is returned on error. The caller should free() the returned pointer
 *      when finished using it.
 *-----------------------------------------------------------------------------
 * getAlpsOverlapOrdinal -
 *
 * Detail
 *
 * Arguments
 *      appId -  The cti_app_id_t of the registered application.
 *
 * Returns
 *      -1 is returned on error.
 *-----------------------------------------------------------------------------
 */

typedef struct
{
    uint64_t apid;
    pid_t    aprunPid;
} cti_aprunProc_t;

typedef struct {
    cti_app_id_t     (*registerApid)(uint64_t apid);
    uint64_t         (*getApid)(pid_t aprunPid);
    cti_aprunProc_t* (*getAprunInfo)(cti_app_id_t appId);
    int              (*getAlpsOverlapOrdinal)(cti_app_id_t appId);
} cti_alps_ops_t;

/*-----------------------------------------------------------------------------
 * cti_slurm_ops extensions - Extensions for the SLURM WLM
 *-----------------------------------------------------------------------------
 * getJobInfo - Obtain information about the srun process from its pid.
 *
 * Detail
 *      This function is used to obtain the jobid/stepid of an srun application
 *      based on its pid It is the callers responsibility to free the allocated
 *      storage with free() when it is no longer needed.
 *
 * Arguments
 *      srunPid -  The pid_t of the srun process.
 *
 * Returns
 *      A cti_srunProc_t pointer that contains the jobid and stepid of srun.
 *      NULL is returned on error. The caller should free() the returned pointer
 *      when finished using it.
 *-----------------------------------------------------------------------------
 * registerJobStep - Assists in registering the jobid and stepid of an already
 *                   running srun application for use with the common tools
 *                   interface.
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
 *-----------------------------------------------------------------------------
 * getSrunInfo - Obtain information about the srun process
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
 *-----------------------------------------------------------------------------
 */

typedef struct
{
    uint32_t  jobid;
    uint32_t  stepid;
} cti_srunProc_t;

typedef struct {
    cti_srunProc_t* (*getJobInfo)(pid_t srunPid);
    cti_app_id_t    (*registerJobStep)(uint32_t job_id,uint32_t step_id);
    cti_srunProc_t* (*getSrunInfo)(cti_app_id_t appId);
} cti_slurm_ops_t;

/*-----------------------------------------------------------------------------
 * cti_pals_ops extensions - Extensions for the PALS WLM
 *-----------------------------------------------------------------------------
 * getApid - Obtain PALS application ID running in craycli process
 *
 * Detail
 *      This function is used to obtain the apid of an existing PALS
 *      application from the craycli process that launched it.
 *
 * Arguments
 *      craycliPid - The PID of the craycli process to query.
 *
 * Returns
 *      A PALS application ID string to be freed by user.
 *      NULL is returned on error.
 *-----------------------------------------------------------------------------
 * registerApid - Register the application ID of an already
 *                running PALS application for use with the Common Tools
 *                Interface.
 *
 * Detail
 *      This function is used for registering a PALS application that was
 *      previously launched for use with the Common Tools Interface.
 *      It is recommended to use the built-in functions to launch
 *      applications, however sometimes this is impossible (such is the case for
 *      a debug attach scenario). In order to use any of the functions defined
 *      in this interface, the application ID must be registered. This is done
 *      automatically when using the built-in CTI functions to launch
 *      applications.
 *
 * Arguments
 *      apid - The application ID of the PALS application to register.
 *
 * Returns
 *      A cti_app_id_t that contains the id registered in this interface. This
 *      app_id should be used in subsequent calls. 0 is returned on error.
 *-----------------------------------------------------------------------------
 */

typedef struct {
    char*            (*getApid)(pid_t craycliPid);
    cti_app_id_t     (*registerApid)(char const* apid);
} cti_pals_ops_t;

/*-----------------------------------------------------------------------------
 * cti_ssh_ops extensions - Extensions for the Generic SSH based WLM
 *-----------------------------------------------------------------------------
 * registerJob - Registers an already running application for use with the
 *               common tools interface.
 *
 * Detail
 *      This function is used for registering a valid application that was
 *      previously launched through external means for use with the tool
 *      interface. It is recommended to use the built-in functions to launch
 *      applications, however sometimes this is impossible (such is the case for
 *      a debug attach scenario). In order to use any of the functions defined
 *      in this interface, the pid of the launcher must be supplied.
 *
 * Arguments
 *      launcher_pid - The pid of the running launcher to which to attach
 *
 * Returns
 *      A cti_app_id_t that contains the id registered in this interface. This
 *      app_id should be used in subsequent calls. 0 is returned on error.
 *-----------------------------------------------------------------------------
 */
typedef struct {
    cti_app_id_t    (*registerJob)(pid_t launcher_pid);
    cti_app_id_t    (*registerRemoteJob)(char const* hostname, pid_t launcher_pid);
    cti_app_id_t    (*registerLauncherPid)(pid_t launcher_pid);
} cti_ssh_ops_t;

/*-----------------------------------------------------------------------------
 * cti_flux_ops extensions - Extensions for the Flux WLM
 *-----------------------------------------------------------------------------
 * registerJob - Registers an already running application for use with the
 *               common tools interface.
 *
 * Detail
 *      This function is used for registering a valid application that was
 *      previously launched through external means for use with the tool
 *      interface. It is recommended to use the built-in functions to launch
 *      applications, however sometimes this is impossible (such is the case for
 *      a debug attach scenario).
 *
 * Arguments
 *      job_id - The ID string of the Flux job to which to attach
 *
 * Returns
 *      A cti_app_id_t that contains the ID registered in this interface. This
 *      app_id should be used in subsequent calls. 0 is returned on error.
 *-----------------------------------------------------------------------------
 */

typedef struct {
    cti_app_id_t    (*registerJob)(char const* job_id);
} cti_flux_ops_t;

/*******************************************************************************
 * Transfer functions - Functions related to shipping files, shared libraries,
 *                      and binaries to compute nodes and launching tool
 *                      daemons.
 ******************************************************************************/

/*
 * cti_createSession - Create a new session abstraction that represents a unique
 *                     storage space on the compute nodes associated with a
 *                     registered application id.
 *
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
 *      Files that were previously shipped to compute nodes associated with a
 *      session will be tracked. This elimintates redundant shipping of
 *      dependencies between multiple transfer calls associated with the same
 *      session.
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
cti_session_id_t cti_createSession(cti_app_id_t app_id);

/*
 * cti_sessionIsValid - Test if a cti_session_id_t is valid
 *
 * Detail
 *      This function is used to test if a cti_session_id_t returned from the
 *      cti_createSession call is valid. A session becomes invalid for future
 *      use upon calling the cti_deregisterApp function with the app_id
 *      associated with the session.
 *
 * Arguments
 *      sid - The cti_session_id_t of the session.
 *
 * Returns
 *      0 if the session is invalid, 1 if the session is still valid.
 *
 */
int cti_sessionIsValid(cti_session_id_t sid);

/*
 * cti_destroySession - Kill every tool daemon associated with a cti_session_id_t
 *                      and make the session invalid for future use.
 *
 * Detail
 *      This function is used to terminate every tool daemon associated with a
 *      cti_session_id_t. After calling, the session becomes invalid for future
 *      use. The tool daemon processes will receive a SIGTERM followed by a
 *      SIGKILL after 10 seconds. Only the tool daemon process will receive a
 *      signal. If it has forked off any children, it is the daemons
 *      responsibilty to ensure they are also terminated during the ten second
 *      period before SIGKILL. Any files that reside in the session directory
 *      on the compute node will be unlinked.
 *
 * Arguments
 *      sid - The cti_session_id_t of the session.
 *
 * Returns
 *      0 on success, or else 1 on failure. On failure, the session is still
 *      valid for future use.
 *
 */
int cti_destroySession(cti_session_id_t sid);

/*
 * cti_createManifest - Create a new manifest abstraction that represents a
 *                      list of binaries, libraries, library directories, and
 *                      files to be sent to the session storage space.
 *
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
cti_manifest_id_t cti_createManifest(cti_session_id_t sid);

/*
 * cti_manifestIsValid - Test if a cti_manifest_id_t is valid.
 *
 * Detail
 *      This function is used to test if a cti_manifest_id_t returned from the
 *      cti_createManifest call is valid. A manifest becomes invalid for future
 *      use upon passing it to cti_sendManifest or cti_execToolDaemon calls or
 *      by calling the cti_deregisterApp function.
 *
 * Arguments
 *      mid - The cti_manifest_id_t of the manifest.
 *
 * Returns
 *      0 if the manifest is invalid, 1 if the manifest is still valid.
 *
 */
int cti_manifestIsValid(cti_manifest_id_t mid);

/*
 * cti_addManifestBinary - Add a program binary to a manifest.
 *
 * Detail
 *      This function is used to add a program binary to a manifest based on the
 *      cti_manifest_id_t argument. The program binary along with any shared
 *      library dependencies will be added to the manifest. If the program uses
 *      dlopen to open libraries, those libraries will need to be manually added
 *      by calling cti_addManifestLibrary. Adding a program binary is necessary
 *      when a tool daemon will fork/exec another program binary. The binary can
 *      be an absolute path, a relative path, or file name upon which the PATH
 *      environment variable will be searched. Upon shipment, the binary can be
 *      found in PATH and shared library dependencies can be found in the
 *      LD_LIBRARY_PATH of the tool daemon environment or by using the CTI
 *      backend API to determine these locations.
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
int cti_addManifestBinary(cti_manifest_id_t mid, const char *fstr);

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
int cti_addManifestLibrary(cti_manifest_id_t mid, const char *fstr);

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
int cti_addManifestLibDir(cti_manifest_id_t mid, const char *fstr);

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
int cti_addManifestFile(cti_manifest_id_t mid, const char *fstr);

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
 *      If the environment variable CTI_DEBUG is defined, the environment
 *      variable defined by CTI_LOG_DIR will be read and log files
 *      will be created in this location. If CTI_LOG_DIR is not
 *      defined, log files will be created in the /tmp directory on the compute
 *      nodes. This log will contain all output during shipment of the manifest
 *      and can be used to locate problems with file shipment.
 *      The cti_setAttribute interface can also be used to define the debug and
 *      log directory settings.
 *
 * Arguments
 *      mid -   The cti_manifest_id_t of the manifest.
 *
 * Returns
 *      0 on success, or else 1 on failure.
 *
 */
int cti_sendManifest(cti_manifest_id_t mid);

/*
 * cti_execToolDaemon - Launch a tool daemon onto all the compute nodes
 *                      associated with the provided manifest session.
 *
 * Detail
 *      This function is used to launch a program binary onto compute nodes.
 *      It will take care of starting up the binary and ensuring all of the
 *      files in the manifest are found in its environment as described above.
 *      One tool daemon will be started on each compute node of the
 *      application.
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
 *      to the tool daemon can be provided by a null terminaed list of args.
 *      In this case, the args[0] is the beginning of actual tool daemon
 *      arguments and not the name of the tool daemon binary.
 *
 *      If the environment variable CTI_DEBUG is defined, the environment
 *      variable defined by CTI_LOG_DIR will be read and log files
 *      will be created in this location. If CTI_LOG_DIR is not
 *      defined, log files will be created in the /tmp directory on the compute
 *      nodes. These log files will contain any output written to stdout/stderr
 *      of the tool daemons. If CTI_DEBUG is not defined, tool daemon
 *      stdout/stderr will be redirected to /dev/null. The cti_setAttribute
 *      interface can also be used to define the debug and log directory
 *      settings.
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
 *
 * Returns
 *      0 on success, or else 1 on failure.
 *
 */
int cti_execToolDaemon( cti_manifest_id_t   mid,
                        const char *        fstr,
                        const char * const  args[],
                        const char * const  env[]);

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
char ** cti_getSessionLockFiles(cti_session_id_t sid);

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
char * cti_getSessionRootDir(cti_session_id_t sid);

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
char * cti_getSessionBinDir(cti_session_id_t sid);

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
char * cti_getSessionLibDir(cti_session_id_t sid);

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
char * cti_getSessionFileDir(cti_session_id_t sid);

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
char * cti_getSessionTmpDir(cti_session_id_t sid);

#ifdef __cplusplus
}
#endif

#endif /* _COMMON_TOOLS_FE_H */
