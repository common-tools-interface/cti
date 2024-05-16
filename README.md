![build](https://github.hpe.com/hpcde/cdst-cti/actions/workflows/build_pipeline.yaml/badge.svg)

# Common Tools Interface

Contents

[A. Introduction](#introduction)

[B. Common Tools Interface synopsis](#common-tools-interface-synopsis)

[C. Library structure](#library-structure)

[D. General CTI frontend interface](#general-cti-frontend-interface)

[D.1. Function listing](#function-listing)

[E. Applications](#applications)

[E.1. Application launch](#application-launch)

[E.2. Application launch with barrier](#application-launch-with-barrier)

[E.3. Application attach](#application-attach)

[E.4. WLM-specific Extensions](#wlm-specific-extensions)

[E.5. Application facilities](#application-facilities)

[E.5.1. Function listing](#function-listing-1)

[F. File transfer support](#file-transfer-support)

[F.1. Sessions](#sessions)

[F.1.1. Function listing](#function-listing-2)

[F.2. Manifests](#manifests)

[F.2.1. Function listing](#function-listing-3)

[F.3. Object lifetime](#object-lifetime)

[G. Tool daemons](#tool-daemons)

[H. Backend library](#backend-library)

[H.1. Function listing](#function-listing-4)

 Introduction
============

This document is organized as follows. Section B provides a synopsis of
the Common Tools Interface (CTI) which is an API designed by Cray to
facilitate bootstrapping of tool launch alongside applications. Section
C provides an overview of the library layout and associated header
files. Sections D through G describe functionality available on the tool
frontend. Section H describes functionality available on the tool
backend.

CTI is available on github at the following link:
<https://github.com/common-tools-interface/cti>

 Common Tools Interface synopsis
===============================

The Common Tools Interface (CTI) is an infrastructure framework to
enable tool developers to launch, interact with, and run tool daemons
alongside applications on HPC systems. CTI provides a collection of
interfaces and supporting utilities to export services for use by tools.
CTI enables vendors to provide workload manager-specific implementations
to rapidly enable support for existing tools that use CTI.

CTI abstracts the process of application launch and lifecycle management
by providing an API that allows underlying implementations to
transparently handle workload manager-specific details. For example, a
SLURM system makes use of `srun` to launch applications, `skill` to send
signals to a running application, `sbcast` to ship files to compute
nodes, and the MPIR standard to harvest placement information about an
application. In contrast, a generic implementation uses `ssh` to launch
applications and send signals, `scp` to ship files, and might lack MPIR
support. CTI provides a common API for developers of tools to run these
essential HPC job management tasks across different implementations.

From a tool developer's perspective, CTI manages interaction with an
application, tool daemon launch, and staging dependencies to compute
nodes. A tool may need to do tasks such as starting daemons on compute
nodes alongside an application, make various binaries and shared library
dependencies available to the tools on the compute node, use
automatically generated support files created on a login node, write
temporary files to a safe location, determine the PIDs of application
processes, etc.

All CTI functionality is available regardless of system configuration.
System specific details such as availability of a parallel file system
are transparent from the API level. This allows the underlying
implementation to make decisions based on the particular system and in
turn allow tools to be extensible in a diverse HPC ecosystem.

 Library structure
=================

CTI is split into two libraries for use by tool developers: a frontend
library for use on the login node, and a backend library for use by a
tool daemon. These libraries have associated `pkg-config` files to
facilitate ease of linking against the associated libraries. Each
library has its own associated header file. These associations are
listed below.

| Library name | Header File | pkg-config file |
| ------------ | ----------- | --------------- |
| `libcommontools_fe.so` | `common_tools_fe.h` | `common_tools_fe.pc` |
| `libcommontools_be.so` | `common_tools_be.h` | `common_tools_be.pc` |

The frontend library is for use on the login node. It is used for
application launch/registration, obtaining information about the
application, and bootstrapping tool daemons. It implements the bulk of
CTI functionality.

The backend library is for use by tool daemons. It allows developers to
determine node local information such as PIDs of application processes,
logical PE placement of application ranks, and filesystem locations of
staged files. To use the backend library, it must be linked against a
tool daemon launched via `cti_execToolDaemon` by the frontend library.
For more information on tool daemon launch see section Tool daemons.

The header file `common_tools_shared.h` defines the types shared by the
frontend and backend library, as well as environment variables.

 General CTI frontend interface
==============================

Several functions exist for setting configuration with the CTI frontend,
querying error information, or other information about the login node
that doesn't require knowledge about an application.

The functions listed in section Function listing are available for use
at any time.

Function listing
----------------

    const char * cti_version(void)

> `cti_version` returns a string containing the current frontend library
> version in the form `major.minor.revision`.

    const char * cti_error_str(void)

> When a CTI frontend function returns in error, the `cti_error_str`
> function can be used to obtain a verbose error string. It returns a
> string containing the human parsable error message, or else \"Unknown
> CTI error\".

    int cti_error_str_r(char *buf, size_t buf_len)

-   `buf`: Provided buffer to write the error string to.

-   `buf_len`: Length of the user provided buffer.

> `cti_error_str_r` is a re-entrant version of `cti_error_str`. It
> allows a user specified buffer to be passed in versus using a static
> global buffer. If the error string is longer than the provided buffer,
> the string is truncated and null-terminated.

    cti_wlm_type_t cti_current_wlm(void)

> `cti_current_wlm` is used to obtain the detected WLM. CTI has built in
> heuristics to detect which WLM is in use on the system. Users can
> explicitly override automatic WLM detection at runtime by setting the
> `CTI_WLM_IMPL` environment variable defined by the macro
> `CTI_WLM_IMPL_ENV_VAR`. See `common_tools_fe.h` for more information.

    const char * cti_wlm_type_toString(cti_wlm_type_t wlm_type)

> `wlm_type`: The `cti_wlm_type_t` to describe.`cti_wlm_type_toString`
> is used to obtain a human readable string representation of a
> `cti_wlm_type_t`.

    char * cti_getHostname(void)

> `cti_getHostname` is used to determine an externally-accessible hostname
> or IP address for the current node. This is the hostname of the network
> interface that can open socket connections between the login node and
> compute node. This is useful on systems where multiple network
> interfaces make a standard `gethostname(2)` call from `glibc` ambiguous.

    int cti_setAttribute(cti_attr_type_t attrib, const char *value)

-   `attrib`: attribute to modify.

-   `value`: attribute specific value to set.`cti_setAttribute` is used
    to modify internal CTI configuration values. See `common_tools_fe.h`
    for a full accounting of `attrib=value` options that are
    available.

    const char * cti_getAttribute(cti_attr_type_t attrib)

-   `attrib`: The requested `cti_attr_type_t` to obtain the current
    value.

> `cti_getAttribute` is used to obtain the current value of the
> requested attribute. See `common_tools_fe.h` for a full accounting of
> available attribute options.

 Applications
============

To use the majority of CTI functionality, a tool developer must first
launch a new application under CTI control or register an already
running application. When launching, CTI can also hold an application at
a startup barrier before `main`. This allows the developer to launch
tool daemons or stage files that are expected to be present before the
application begins execution.

Upon successful launch or attach, a `cti_app_id_t` handle is returned.
This opaque identifier is used for all application-specific
functionality in CTI. The validity of an application handle can be
determined using `cti_appIsValid`. An application handle is considered
valid until the application exists (either normally/abnormally), or
`cti_deregisterApp` is called. Signals can be sent to an application via
the `cti_killApp` function.

Application launch
------------------

The `cti_launchApp` function is used to programmatically launch an
interactive application. This replaces the manual `fork`/`exec` of
launch commands such as `aprun`, `srun`, or `mpiexec`. CTI assumes a
node allocation has been previously acquired, or nodes are marked as
interactive, making compute resources available to the caller before
use.

The application launcher employed is automatically detected by CTI. This
logic is based on CTI detection of the workload manager (WLM) in use.
See `cti_current_wlm` in the General frontend functions section for more
info. A custom launcher can be explicitly specified with the
`CTI_LAUNCHER_NAME` environment variable defined by the macro
`CTI_LAUNCHER_NAME_ENV_VAR`.

    cti_app_id_t cti_launchApp(const char * const   launcher_argv[],
                               int                  stdout_fd,
                               int                  stderr_fd,
                               const char *         inputFile,
                               const char *         chdirPath,
                               const char * const   env_list[])

-   `launcher_argv`: A null-terminated list of arguments to pass
    directly to the launcher. It is the caller's responsibility to
    ensure that valid `launcher_argv` arguments are provided for the
    application launcher. The caller can use the `cti_current_wlm`
    function to determine which launcher is used by the system. Note
    that `launcher_argv[0]` must be the start of the actual
    arguments passed to the launcher, and not the name of launcher
    itself.

-   `stdout_fd`: File descriptor in which to redirect `stdout`, or `-1`
    if no redirection should take place.

-   `stderr_fd`: File descriptor in which to redirect `stderr`, or `-1`
    if no redirection should take place.

-   `inputFile`: The pathname of a file in which to redirect `stdin`, or
    NULL to redirect `/dev/null` to `stdin`.

-   `chdirPath`: The path in which to change the current working
    directory before launching the application, or NULL to use the
    existing current working directory.

-   `env_list`: A null-terminated list of strings of the form
    `"NAME=value"` to set NAME in the application environment to
    value.

> Upon success a non-zero `cti_app_id_t` is returned. On error, 0 is
> returned.

Application launch with barrier
-------------------------------

A tool may require attaching onto an application before it begins
execution, as is the case of a debugger, or bootstrapping itself early
on. CTI supports an application launch variant `cti_launchAppBarrier`
where the target application is held at a startup barrier before main.
The `cti_launchAppBarrier` function takes the same arguments and has the
same return value as `cti_launchApp` described in the Application launch
section.

When a tool is ready to release the application from the startup
barrier, it calls `cti_releaseAppBarrier`. This allows the application
to continue normal execution.


    int cti_releaseAppBarrier(cti_app_id_t app_id)

-   `app_id`: The `cti_app_id_t` of the application launched via
    `cti_launchAppBarrier`.

Application attach
------------------

It is possible to use the CTI daemon and file transfer facilities with
applications that were not started under direct control of CTI. In that
case, there is no barrier equivalent as the application is already
executing.

Registration of an existing app is largely specific to the WLM
implementation. For example, an MPIR based launcher might require a
`pid_t` of the application launcher process to which it is attached via
`ptrace`. Alternative mechanisms besides MPIR are also available to
exercise similar capabilities. For that reason, CTI uses a WLM specific
identifier when possible. For example, registering an application with a
Slurm based WLM requires two identifiers, `jobid` and `stepid`.

Because there is no one universal way to register existing applications
with CTI, the different mechanisms are implemented as WLM-specific
extensions. These are documented in the section WLM-specific Extensions.

WLM-specific Extensions
-----------------------

Most workload managers provide implementation-specific functionality.
The most common example is in the attach case; each workload manager
uses a different form of job identification to determine which
application to attach. See section Application attach for more
information.

CTI provides a generic extensible interface to add additional workload
manager-specific functionality. To determine which workload manager is
in use and thus which WLM extensions to call, use `cti_current_wlm`. See
the General frontend functions section for more information.

See `common_tools_fe.h` for a list of all available WLM extensions.

Below is an example of attaching to a SLURM job using the CTI WLM
extensions interface:

>     // Defined in common_tools_fe.h:
>     typedef struct {
>         cti_app_id_t (*registerJobStep)(uint32_t job_id, uint32_t step_id);
>
>         // Other SLURM operations...
>     } cti_slurm_ops_t;
>
>     // Application code
>     assert(cti_current_wlm() == CTI_WLM_SLURM);
>     cti_slurm_ops_t *slurm_ops = NULL;
>     assert(cti_open_ops(&slurm_ops) == CTI_WLM_SLURM);
>     cti_app_id_t const app_id = slurm_ops->registerJobStep(job_id, step_id);

Application facilities {#application-facilities .AlphaHeading2}
----------------------

Once an application is registered, whether by launch or attach
mechanisms, a variety of useful runtime facilities are available. These
include querying application layout information, launching remote tool
daemons on compute nodes, along with transferring files, binaries,
libraries, and applicable dynamic library dependencies to a file system
location accessible on the compute node.

Most runtime functions require an associated instance of `cti_app_id_t`
to be provided, which is the application ID returned by the
launch/attach described in the Application Lifetime section.

### Function listing

    int cti_getNumAppPEs(cti_app_id_t app_id)

> Returns the number of processing elements (PE) in the application
> associated with the `app_id`. A PE represents an MPI rank for MPI
> based programming models.

    int cti_getNumAppNodes(cti_app_id_t app_id)

> Returns the number of compute nodes allocated for the application
> associated with the `app_id`.

    char** cti_getAppHostsList(cti_app_id_t app_id)

> Returns a null-terminated array of strings containing the hostnames of
> the compute nodes allocated by the application launcher for the
> application associated with the `app_id`.

    cti_hostsList_t* cti_getAppHostsPlacement(cti_app_id_t app_id)

> Returns a `cti_hostsList_t` containing entries that contain the
> hostname of the compute nodes allocated by the application launcher
> and the number of PEs assigned to that host for the application
> associated with the `app_id`.

 File transfer support
=====================

A common requirement for tools is the ability to launch tool daemons
alongside application ranks on compute nodes. This includes access to
dependencies such as shared libraries or configuration files. CTI aims
to provide an extensible interface that operates under many different
constraints. A tool typically isn't concerned where a dependency resides
on the file system. Rather, it cares that the dependency is accessible
in a performant way.

For example, CTI aims to provide an interface that can cope with HPC
systems that either have, or lack, a performant parallel file system.
This may require co-locating the dependencies onto the compute nodes
directly. It should also have the ability to provide system specific
optimizations that prevent redundant transfer of dependencies already
available via a parallel file system. All of this is achieved in a way
that is transparent to the caller.

CTI manages unique storage locations via the paired concepts of sessions
and manifests. These are described in following sections.

Sessions
--------

The concept of a session allows CTI to manage different file system
locations to which a tool daemon is guaranteed to have read/write
access. A session represents a unique storage location where
dependencies can be co-located, new files can be written, and is
guaranteed to be cleaned up after the session/application exit.

Depending on tool need, multiple tool daemons can share the same
session, or be isolated into different sessions. A session is always
associated with an application via a `cti_app_id_t`. This is because a
session must be associated with a file system location that may be
unique to each compute node. This requires an associated application to
describe this set of compute nodes.

The unique storage location of a session may be a parallel file system,
or it may be a temporary storage location such as `/tmp`. The choice
of where the storage location resides is implementation specific. CTI
automatically creates unique directories in the base file system to
create logical isolation between different sessions. This way, multiple
tools can co-locate dependencies and run tool daemons concurrently
without worry of clobbering file system locations.

Creation of the storage location associated with a session is deferred
until a manifest (described in the Manifests section) is shipped or the
tool daemon associated with the manifest is launched. A session has
child directories for different dependencies: `/bin` for binaries,
`/lib` for libraries, and `/tmp` for temporary storage. The `TMPDIR`
environment variable of a tool daemon process will contain the
associated session's `/tmp` location. Likewise `LD_LIBRARY_PATH` and
`PATH` will point to the `/bin` and `/lib` location of the session
respectiviely.

### Function listing

    cti_session_id_t cti_createSession(cti_app_id_t app_id)

-   `app_id`: Application handle for a session.

> A session is created with `cti_createSession`. This returns a
> `cti_session_id_t` session identifier for use with other interface
> calls. The validity of a session identifier can be determined using
> `cti_sessionIsValid`. A session is automatically invalidated if
> the associated `cti_app_id_t` becomes invalid.

    int cti_destroySession(cti_session_id_t sid)

-   `sid`: Session handle to destroy.

> A session is destroyed via `cti_destroySession`. This will terminate
> every tool daemon associated with the session handle and remove the
> unique storage location if it was created. Tool daemons are terminated
> by sending a SIGTERM to the daemon process followed by a SIGKILL after
> 10 seconds. Upon completion, the session identifier becomes invalid
> for future use.

Manifests
---------

Once a unique storage location is specified through the creation of a
session, dependencies can be made available to it. This is achieved by
generating a manifest and populating it with a list of files. A manifest
is always associated with an owning session identifier. Sessions keep
track of dependences previously made available to compute nodes. When a
manifest is made available to a session, only those dependencies which
are not already accessible to the session are co-located. This avoids
redundant shipping of dependencies.

### Function listing

    cti_manifest_id_t cti_createManifest(cti_session_id_t sid)

-   `sid`: Session id for the manifest.

> A new manifest is created with `cti_createManifest`. This returns a
> `cti_manifest_id_t` manifest identifier for use with other interface
> calls. A manifest is automatically invalidated if the owning session
> becomes invalid. Dependencies contained within a manifest are not
> available to the session until a call is made to `cti_sendManifest` or
> `cti_execToolDaemon`. Once a manifest has been made available to the
> session, it is finalized and invalid for future modification. The
> validity of a manifest identifier can be determined using
> `cti_manifestIsValid`.

    int cti_addManifestBinary(cti_manifest_id_t mid, const char *fstr)

-   `mid`: The manifest id to which to add the dependency.

-   `fstr`: The name of the binary to add to the manifest. This can
    either be the full path name of the binary or the base name of the
    binary in which case `PATH` is searched.

> `cti_addManifestBinary` is used to add a program binary to a manifest.
> If the program binary is dynamically linked, its shared library
> dependencies will be automatically detected and added to the manifest.
> If the binary uses `dlopen` to open shared library dependencies, those
> libraries need to be added explicitly by calling
> `cti_addManifestLibrary`. This call is primarily for cases where a
> tool daemon launched via `cti_execToolDaemon` needs to `fork`/`exec`
> another program binary. This binary will be found in `PATH` and any
> shared library dependencies will be found in `LD_LIBRARY_PATH` of the
> environment of a tool daemon process.
>
> If a shared library dependency is not available on the compute node
> and needs to be collocated, CTI is able to handle naming collisions
> across library names. CTI does this automatically via use of unique
> directories created under the session's `/lib` along with setting an
> appropriate `LD_LIBRARY_PATH` for the tool daemon(s). The same is not
> true for binaries or files; only unique binaries and files can be
> added to a session.

    int cti_addManifestLibrary(cti_manifest_id_t mid, const char *fstr)

-   `mid`: The manifest id to which to add the dependency.

-   `fstr`: The name of the shared library to add to the manifest.
    This can either be a full path name, or the base name of the
    library. If a base name is specified, a search of library lookup
    paths will be conducted.

    int cti_addManifestLibDir(cti_manifest_id_t mid, const char *fstr)

-   `mid`: The manifest id to which to add the dependency.

-   `fstr`: The full path name of the directory to add to the manifest
    and make available within the /lib directory of the session.

> `cti_addManifestLibDir` is used to add every library contained within
> a directory to the manifest. This is useful for programs that `dlopen`
> many dependencies. The directory structure will be preserved and found
> within the `/lib` directory of the session.

    int cti_addManifestFile(cti_manifest_id_t mid, const char *fstr)

-   `mid`: The manifest id to which to add the dependency.

-   `fstr`: The full path name of the file to add to the manifest.

> `cti_addManifestFile`is used to add an ordinary file to a manifest.

Object lifetime
------------------------------------------------------------------------------------------------

There is an explicit ownership hierarchy defined within CTI. The topmost
object is an application, represented by `cti_app_id_t`. The next object
is a session, represented by a `cti_session_id_t`. At the bottom is a
manifest, represented by a `cti_manifest_id_t`. Applications own
sessions, which in turn own manifests. An important characteristic of
CTI to recognize is this ownership definition. An application can own
one or more session(s), and a session can own one or more manifest(s).
That way, if the lifetime of an application ends, all owned sessions are
invalidated, and internal data structures cleaned up. Likewise, if the
lifetime of a session ends, all owned pending manifests are invalidated,
and internal data structures cleaned up.

When invalidating a session via `cti_destroySession`, any tool daemons
started within that session will also be killed. This behavior can be
bypassed by calling `cti_deregisterApp` without explicitly calling
`cti_destroySession`. This is useful for tools which are interested in
bootstrapping their tool daemons from a login node without keeping a
frontend presence alive. If a tool frontend exits without calling
`cti_deregisterApp` or `cti_destroySession`, all launched applications
and tool daemons will be killed.

There is no explicit way in the interface to invalidate a manifest.
Manifests are lightweight lists of files and don't require any
management considerations. A pending manifest has no impact on other
manifests, state consideration happens only after a manifest has been
made available to a session.

Tool daemons
============

Once a session is established and a manifest is created, tool daemon(s)
can be launched onto the compute nodes associated with the session's
application. CTI will launch a single tool daemon process onto every
compute node associated with the application. It is up to the tool
developer to `fork`/`exec` additional tool daemons if necessary, or exit
if tool daemons need to execute only on a subset of compute nodes.

A manifest is required to be provided as part of a tool daemon's launch
even if no other dependencies are required (i.e. the manifest is empty).
Association with an application is made with the manifest argument: the
manifest has an owning session which contains a list of already staged
dependencies, and the session has an owning application to determine
which nodes tool daemons need to be started.

CTI will conduct setup of the tool daemon environment before calling
`exec`. This includes steps like the following:

-   The tool daemon will have any binaries that have been made available
    to the session found within `PATH`.

-   Shared libraries will be found within `LD_LIBRARY_PATH`.

-   `TMPDIR` will point at the session specific `/tmp` location and is
    guaranteed to have read/write access.

-   A null-terminated list of environment variables can be provided to
    set tool daemon specific environment. The specified argument array
    is provided to each tool daemon process.

-   Any implementation specific tasks will be conducted for a particular
    system

By default, tool daemon processes will have their `stdout`/`stderr`
redirected to `/dev/null`. This can be overridden by use of the
`CTI_DEBUG` and `CTI_LOG_DIR` (see
`common_tools_shared.h` for more information). This allows a tool daemon
to write debug logs to a known location in a parallel file system. There
will be one file per compute node with names correlating to each compute
node number. The `cti_setAttribute` interface an also be used to define
the logging behavior.

    int cti_execToolDaemon( cti_manifest_id_t   mid,
                            const char *        fstr,
                            const char * const  args[],
                            const char * const  env[])

-   `mid`: The manifest id to which to add the tool daemon binary.

-   `fstr`: The name of the tool daemon binary. This can either be the
    full path name of the binary or the base name of the binary in which
    case `PATH` is searched.

-   `args`: Null-terminated list of arguments to pass to the tool
    daemon. `args[0]` should be the first argument, not the name of
    the tool daemon binary.

-   `env`: Null-terminated list of environment variables to set in the
    environment of the tool daemon process. Each variable setting should
    have the format `envVar=val`.

 Backend library
===============

Once a tool daemon is launched, the CTI backend library is available for
use. This interface is defined in the `common_tools_be.h` header file.
The `libcommontools_be` library should be linked into the tool daemon
binary launched with `cti_execToolDaemon`. The backend library is used
for determining node local information about the associated application.
This information can be things like the logical PE ranks located on the
node, the PID(s) of all application processes on the node, or filesystem
layout of the session directory.

A subset of available functions is listed below.

Function listing
----------------

    cti_wlm_type_t cti_be_current_wlm(void)

> Returns the WLM in use by the application.

    cti_pidList_t * cti_be_findAppPids(void)

> Returns a `cti_pidList_t` containing the mapping of PE `pid_t` to
> logical PE rank.

    char * cti_be_getNodeHostname()

> Returns the hostname of the node.

    int cti_be_getNodeFirstPE(void)

> Returns the first logical PE number that resides on the node.

    int cti_be_getNodePEs(void)

> Returns the number of PE's on the node.

