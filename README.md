# Common Tools Interface

## Synopsis

The Common Tools Interface (CTI) is an infrastructure framework to enable tool developers to interact with, and run tool daemons alongside, applications on HPC systems. CTI provides a collection of interfaces and supporting utilities to export services for use by tools. Additionally, CTI enables vendors to provide workload manager-specific customizations to rapidly enable support on their systems.

CTI abstracts the process of job launch and lifecycle management by providing an API that allows underlying implementations to transparently handle workload manager-specific details. For example, a SLURM system makes use of `srun` to launch jobs, `skill` to send signals to a running job, and `sbcast` to ship files to compute nodes. A generic SSH implementation, however, might use `ssh` to launch jobs and send signals, and `scp` to ship files. CTI provides a common API for developer tools to run these general, yet essential HPC job management tasks.

## Application Lifetime

From a developer point of view, CTI primarily manages application and helper daemon launch processes. A tool may require running alongside helper daemons, or make use of automatically-generated support files. CTI can hold an application at a startup barrier before `main` while launching remote helper daemons, or transferring support files to compute nodes. When setup is completed, it directs CTI to release the startup barrier and allow the tool to continue launching.

Upon successful launch or attach, a `cti_app_id_t` is returned. This application ID is used for all job-specific functionality in CTI.

### Launch

The CTI function `cti_launchApp` is used to programmatically launch a job. The application launcher to use will be automatically determined based on the current workload manager of the system.

	cti_app_id_t
	cti_launchApp(const char * const  launcher_argv[],
	              int                 stdout_fd,
	              int                 stderr_fd,
	              const char *        inputFile,
	              const char *        chdirPath,
	              const char * const  env_list[])


* `launcher_argv` -  A null-terminated list of arguments to pass directly to the launcher. If provided, it is up to the caller to ensure that valid `launcher_argv` arguments are provided for the current application launcher. Note that `launcher_argv[0]` must be the start of the actual arguments passed to the launcher, and not the name of launcher itself. 

* `stdout_fd`: File descriptor to which to redirect `stdout`, or `-1` if no redirection should take place.
* `stderr_fd`: File descriptor to which to redirect `stderr`, or `-1` if no redirection should take place.
* `inputFile`: The pathname of a file from which to redirect `stdin`, or NULL to use `/dev/null`.
* `chdirPath`: The path to which to change the current working directory, or NULL if none should take place.
* `env_list `: A null-terminated list of strings of the form `"NAME=value"` to set `NAME` in the job environment to `value`.

### Launch with Barrier

A tool may require running alongside helper daemons, or use support files during its startup process. To enable this functionality, CTI supports an application launch where the target application is held at a startup barrier, before `main`. The function, `cti_launchAppBarrier`, takes the same arguments as `cti_launchApp` and returns a `cti_app_id_t`.

* `cti_releaseAppBarrier(cti_app_id_t app_id)`: After launching required daemons or shipping required files, the application is be released from its barrier and allowed to continue with its startup process.

### Attach

It is also possible to use CTI's daemon and file transfer facilities with jobs that were not started under control of CTI. In this situation, there is no barrier equivalent, as startup has already passed.

The primary identifying key for registering an application depends on the workload manager implementation. For example, SSH uses a single system process ID, but SLURM uses both a job and a step ID. Because of this, attaching requires WLM-specific calls.

For more information on how to load and run WLM-specific extensions, see the final section of this document, "WLM-specific Extensions".

### Runtime Facilities

Once an application is registered and controlled with CTI, whether by launch or attach, a variety of useful runtime facilities are available, including querying job layout information, launching remote helper daemons, and transferring files, binaries, libraries, along with applicable dynamic library dependencies.

These functions use a `cti_app_id_t`, which is the application ID as produced by CTI launch or attach.

For a full selection and documentation for job runtime functions, see the header `common_tools_fe.h`.

* `cti_getNumAppPEs(cti_app_id_t app_id)`: Returns the number of processing elements in the application associated with the `app_id`.
* `cti_getNumAppNodes(cti_app_id_t app_id)`: Returns the number of compute nodes allocated for the application associated with the `app_id`.
* `char** cti_getAppHostsList(cti_app_id_t app_id)`: Returns a null-terminated array of strings containing the hostnames of the compute nodes allocated by the application launcher for the application associated with the `app_id`.

		typedef struct {
		    int            numHosts;
		    cti_host_t *   hosts;
		} cti_hostsList_t;

* `cti_hostsList_t* cti_getAppHostsPlacement(cti_app_id_t app_id)`: Returns a `cti_hostsList_t` containing entries that contain the hostname of the compute nodes allocated by the application launcher and the number of PEs assigned to that host for the application associated with the `app_id`.

#### File transfer support

* Sessions and Manifests

	Running jobs or remote tool daemons often require support files, such as shared library dependencies, or job layout information files. Per job, CTI can manage several temporary unique storage locations, maintaining an in-progress list of files to ship. These directories is guaranteed to be cleaned up upon tool daemon exit.

	In the API, each unique storage location is represented as a Session. A Session is created and associated with an application with `cti_session_id_t cti_createSession(cti_app_id_t app_id)`. 

	Sessions generate in-progress lists of files called Manifests. A Manifest is created and associated with a Session with `cti_manifest_id_t cti_createManifest(cti_session_id_t sid)`.

* Adding to Manifest

	Only uniquely named binaries, libraries, library directories, and files are added to the Manifest. Added and shipped files are tracked to avoid redundant shipment of files and inadvertent naming collisions.

	* `cti_addManifestBinary(cti_manifest_id_t mid, const char *fstr)`, `cti_addManifestLibrary`: Add a program binary or library to the specified Manifest. The program binary or library along with any shared library dependencies will be added. If the program uses `dlopen` to open libraries, those libraries will need to be manually added using `cti_addManifestLibrary`.
	* `cti_addManifestLibDir(cti_manifest_id_t mid, const char *fstr)`: Recursively add all shared libraries in a directory to a Manifest.
	* `cti_addManifestFile(cti_manifest_id_t mid, const char *fstr)`: Add a regular file to a Manifest.

* Shipping Manifest

	* `cti_execToolDaemon`, detailed in the next section, "Tool daemon support", is the normal method with which to complete and ship Manifests.
	* `cti_sendManifest(cti_manifest_id_t mid)`: Ship all files in the Manifest to the remote storage associated with the current Session.

* Session cleanup

	* `cti_destroySession(cti_session_id_t sid)`: Terminate every tool daemon associated with the specified Session. Each tool daemon processes will receive a `SIGTERM`, then `SIGKILL`.

	If the daemon has forked off any children, it is the daemon's responsibility to terminate them upon receiving `SIGTERM`. Any files that reside in the Session directory on the compute node will be unlinked.

#### Tool daemon support

This function is used to launch a program binary onto compute nodes. It will take care of starting up the binary and ensuring all of the files in the manifest are found in its environment as described above. One tool daemon will be started on each compute node of the application.

	cti_execToolDaemon(cti_manifest_id_t   mid,
	                   const char *        fstr,
	                   const char * const  args[],
	                   const char * const  env[])

Any files in the provided manifest argument will be shipped and made available to the tool daemon. Even if no other file dependencies are required, an empty Manifest must still be provided. It is not necessary to add the tool daemon binary to the Manifest before calling this function. The Manifest will be invalidated by this function.

## WLM-specific Extensions

Most workload managers provide implementation-specific functionality. The most common example is in the attach case; each workload manager uses a different form of job identification to determine to which job to attach. For example, SSH uses a single system process ID, but SLURM uses both a job and a step ID.

To determine which workload manager is in use, the function `cti_current_wlm()` returns an enum `cti_wlm_type_t` value, representing one of the CTI-supported workload managers. Next, CTI provides a generic interface to load workload manager-specific functionality. Below is an example of attaching to a SLURM job:

	// Defined in common_tools_fe.h:
	typedef struct {
		cti_app_id_t (*registerJobStep)(uint32_t job_id, uint32_t step_id);

		// Other SLURM operations...
	} cti_slurm_ops_t;

	// Application code
	assert(cti_current_wlm() == CTI_WLM_SLURM);
	cti_slurm_ops_t *slurm_ops = NULL;
	assert(cti_open_ops(&slurm_ops) == CTI_WLM_SLURM);
	cti_app_id_t const app_id = slurm_ops->registerJobStep(job_id, step_id);
