% cti(1)
% Hewlett Packard Enterprise Development LP.
% 2022-09-06

# NAME
**cti** --- *Common Tools Interface* User Reference

# DESCRIPTION

The Common Tools Interface (CTI) is an infrastructure framework to
enable tools to launch, interact with, and run utilities
alongside applications on HPC systems.

It is used to support Cray PE debugging applications such as gdb4hpc,
ATP, STAT, and valgrind4hpc. Some systems may require environment
configuration for CTI to enable these applications to launch and function
correctly.

This man page is intended to help users of CTI based tools
understand CTI's error messages and to find environment
settings that may be necessary for their specific system
when running PE debugger tools.

Note that there are both general environment variables, as well as specific
environment variables for different system workload managers.

# ENVIRONMENT VARIABLES

## General variables

- *CTI_INSTALL_DIR*: This is the CTI installation location, where libraries
  and utilities are located. You usually will not have to set this value, as it
  is set by loading the **cray-cti** module. If you see an error that this
  value is not set, try loading your system's default **cray-cti** module.

- *CTI_WLM_IMPL*: The system type and workload manager pair is automatically
  detected by CTI. However, if there is a detection problem in your environment,
  you can manually set this variable to specify the system / WLM pair.

  Systems using HPCM management software include HPE Apollo and Everest systems.
  If your system runs PBSPro as its scheduler, it should also be running
  the PALS workload manager. For PBSPro, specify the **pals** option.

  Supported system and WLM configurations:

  - Shasta / Slurm: "shasta/slurm"
  - Shasta / PALS:  "shasta/pals"
  - HPCM / Slurm:   "hpcm/slurm"
  - HPCM / PALS:    "hpcm/pals"
  - HPCM / Flux:    "hpcm/flux"
  - XC / Slurm:     "xc/slurm"
  - XC / ALPS:      "xc/alps"
  - CS / mpiexec:   "cs/mpiexec"
  - SSH with MPIR-compliant launcher: "linux/ssh"

- *CTI_LAUNCHER_NAME*: The launcher name (such as **srun**
  for Slurm systems and **qsub** for PBSPro / PALS systems) is by
  default determined from the workload manager type, but can be
  overridden here.

- *CTI_DEBUG*: If enabled, CTI will produce debug logs of its startup
  and attach process, as well as output from the debug tools' utilities
  running remotely. When setting this variable, it is recommended to also set
  *CTI_LOG_DIR*, described below.

- *CTI_LOG_DIR*: If *CTI_DEBUG* is enabled, set this variable
  to a cross-mounted directory (such as inside a home or shared storage
  directory) to produce debug logs. Multiple log files will be created,
  from both the processes running on the local system, as well as remote
  tool processes running on compute nodes.

- *CTI_HOST_ADDRESS*: Debug tools will use CTI to determine an externally-
  accessible address to which remote tool utilities can connect. This
  automatically-detected address can be overridden by setting this environment
  variable to the desired address. Ensure that the address can be reached
  from your system's compute nodes.

- *CTI_LAUNCHER_WRAPPER*: Slurm jobs may be launched under the control of
  wrapper utilities, for example the library-loading utility Spindle. To start
  a Slurm job under a specific launcher wrapper, set this variable to the utility command.

  To pass an argument that includes spaces, surround the argument in quotes.
  To pass an argument that includes quotes, escape the quotes with **\\**.

  For example, setting *CTI_LAUNCHER_WRAPPER="spindle --pull"* will result
  in an internal call to **spindle --pull srun a.out** when launching
  **a.out** under gdb4hpc.

- *CTI_BACKEND_WRAPPER*: CTI allows debug tools such as gdb4hpc to attach
  to jobs running inside wrapper programs, such as inside Singularity
  containers. To specify the wrapper program, set this variable to the name
  of the binary that wraps the target job binaries on the compute nodes.

  For example, when jobs are running inside a Singularity container, the
  job's processes will be direct children of the **singularity** daemon on
  compute nodes. So, to pass this information to CTI, set
  *CTI_BACKEND_WRAPPER=singularity*.

## Slurm-specific variables

- *CTI_SLURM_DAEMON_GRES*: Starting with Slurm 21.08, there is a known
  bug that may result in hanging job launches
  (https://bugs.schedmd.com/show_bug.cgi?id=12642). If you are experiencing
  job hangs with this Slurm version, try setting this variable to an empty
  string, or to your system's required job GRES parameter, if one is needed.

- *CTI_SRUN_OVERRIDE*: Replace all default **srun** arguments with
  these arguments.

- *CTI_SRUN_APPEND*: Add these arguments to all generated **srun** commands.

## SSH-specific variables

For workload managers that do not natively provide a file-shipping or
remote process-management interface, CTI uses SSH to launch remote
utilities and ship files to compute nodes. To function correctly,
you will need to configure passwordless, key-based SSH access to
compute nodes associated with the target job to be debugged. This allows
debug tools to use CTI to start utilities remotely without requesting
a password.

- *CTI_SSH_PASSPHRASE*: If your SSH keys require a passphrase to
  access, set the passphrase here.

- *CTI_SSH_DIR*: If your SSH configuration directory is nonstandard
  (usually **~/.ssh**), you can set this variable to the location
  of your SSH directory. It should contain the **knownhosts** file, as
  well as private and public keys to access compute nodes.

Alternatively, you can set the following variables to specify the direct
paths to the required SSH files:

- *CTI_SSH_KNOWNHOSTS_PATH*: The direct path to your SSH **knownhosts**
  file.
- *CTI_SSH_PUBKEY_PATH*: The direct path to your SSH public key for
  compute node access.
- *CTI_SSH_PRIKEY_PATH*: The direct path to your SSH private key for
  compute node access.

## Flux-specific variables

- *FLUX_INSTALL_DIR*: The installation directory of the Flux workload
  manager is automatically detected from the path of the Flux launcher.
  To override this, set this variable to the Flux installation directory.
- *LIBFLUX_PATH*: The location of the **libflux** library is
  automatically detected from the dependency list of the Flux launcher.
  To override this, set this variable to the **libflux** library path.
- *CTI_FLUX_DEBUG*: The **libflux** library is currently in active
  development and its interface is subject to change. CTI will verify
  at runtime if your system is running a different version of Flux; this
  check can be bypassed by setting *CTI_FLUX_DEBUG=1*.

## ALPS-specific variables

- *CTI_APRUN_PATH*: By default, the **aprun** launcher is used
  from the current **PATH** value. To override this, set this variable
  to the direct path to the desired **aprun** binary.

## PALS-specific variables

- *CTI_PALS_EXEC_HOST*: To use a PALS application ID instead of a
  PBS job ID for attaching to running jobs, set this variable to the
  execution host (usually the hostname) of the node hosting the PBS job.
  This can be found in the "Nodes" field when running **palstat** inside
  the PBS reservation, or the "exec_host" field when running **qstat -f**.