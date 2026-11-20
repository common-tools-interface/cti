cray-cti 2.22.0
================================================================================

Product Description
---------------------------
- Cray Common Tools Interface (CTI) is a library that abstracts debugger tool support
  to a common interface regardless of the underlying work load manager. It facilitates
  the staging of files and the launch of tool daemons on the compute nodes associated 
  with a parallel job. Options and interfaces can be found in the cti(1) and cti(3) 
  manpages.

Notes
---------------------------
- CPE-13288: Contains a workaround for an issue where PALS tool daemon launches will fail if
  /tmp is mounted noexec. In this case, set CTI_BACKEND_TMPDIR to a backend directory on
  an executable filesystem.

Changelog
---------------------------
=======================================================================
## 2.23.0 Unified Open Source Version
=======================================================================

### Build changes

* Replaced the autotools based build with meson.
* Changed dyninst, boost, and libssh2 dependencies to use OS available version.
* PALS and flux support available as a optional configurations.
* First version committed to be buildable outside of an HPE environment.

### Bug Fixes

* Slurm: request GPUs per node from the job allocation for tool daemon steps, including
  for attach. Fixes "Insufficient GRES available in allocation" when a multi-node job
  is allocated with `-G` / `--gpus`.

=======================================================================
## 2.22.0
=======================================================================
 
### Bug Fixes
 
* CPE-15800: Build with GCC 12  
* CPE-15394: Add workaround for SLURM_GPUS bug  
* CPE-14832: Fix ASCII job ID parse for attach  
* CPE-13691: Work around PMIx hostname bug 
 
=======================================================================
## [2.21.0-rc3]
=======================================================================
 
### Bug Fixes
 
* CPE-14234: Don't exit on task prolog failure  
* CPE-12977: Terminate `flux job attach` with SIGKILL  
* CPE-14013: Add fallback for missing PMIx hostname operation  
 
=======================================================================
## [2.21.0]
=======================================================================
 
### Bug Fixes
 
* CPE-13288: Add CTI_BACKEND_TMPDIR for backend directory setting  
* CPE-11579: Fix pals_fini dlsym  
* CPE-9580: Update libflux API for 0.75  
 
### Features
 
* CPE-8701: Add waiting attach functions for Slurm, PALS, Flux 
 
 

OS Dependencies
---------------------------
- HPE CSM systems with SLES 15 SP6 - SP7
- HPE HPCM systems with SLES 15 SP6 - SP7 or RHEL 8.10, RHEL 9.5 - 9.6
- HPE Apollo systems with RHEL 8.10

Product Dependencies
---------------------------
- cray-dyninst 12.3.6 or later installed

Documentation
---------------------------
- See manpages cti.1 and cti.3 for user and developer documentation respectively

Installation Instructions
---------------------------
- rpm -ivh cray-cti-2.22.0-20260609183709_f2c565ae267f-292.sles15sp7.x86_64.rpm

  To make cray-cti 2.22.0 the default version of cray-cti, execute:
  /opt/cray/admin-pe/set_default_files/set_default_cray-cti-2.22.0

Legal Notices
---------------------------
Certain components, files or programs contained within this package or product are Copyright 2010-2026 Hewlett Packard Enterprise Development LP.  SPDX-License-Identifier: Linux-OpenIB 

Attribution notices for open source licensed software for this package are detailed in the file:
  /opt/cray/pe/cti/2.22.0/ATTRIBUTIONS_cti.txt

