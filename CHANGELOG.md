=======================================================================
## [unreleased]
=======================================================================

### Features

* Enable attaching to PALS jobs outside of interactive allocation  [4054ad6]

=======================================================================
## [2.15.14] - 2022-08-03
=======================================================================

### Bug Fixes

* CTI Frontend daemon conflicting with Dyninst breakpoints  [f9b9560]

=======================================================================
## [2.15.13] - 2022-07-07
=======================================================================

### Bug Fixes

* Capture srun stderr output during launch  [f5a05bc]
* Default to HSN interface for HPCM PALS and HPCM Slurm  [117c06b]
* Copy environment to back end in generic/ssh implementation  [f2cf003]

### Features

* Add Slurm multi-cluster / allocation detection  [a6a6e70]
* Switch to HPCM PALS highspeed network  [2083143]
* Added support for rhel86 x86 - PE-41140 (#427)  [41b5e27]

=======================================================================
## [2.15.12] - 2022-05-06
=======================================================================

### Bug Fixes

* Resolve ordering issues with HPCM PALS backend  [659eb71]
* Ending main loop from signal handler in daemon will also end in-progress MPIR launch  [e9e4616]

### Features

* Added support for sles15sp4 x86 - PE-39146 #423  [d2b0712]
* Added autogen changelog/release notes functionality - PE-40699 #421  [9b44518]

=======================================================================
## [2.15.11] - 2022-04-14
=======================================================================

### Bug Fixes

* PALS implementation's getApid function - PE-40533  [7ce0d4c]
* Re-add CTI manpages to RPM  [ac5a673]

### Features

* Update release notes  [33ffb44]
* Added sles15sp1 aarch64 jenkinsfile - PE-36379 #418  [5ab8ca9]
* Added support for rhel85 x86 - PE-40102 (#414)  [53c8642]

