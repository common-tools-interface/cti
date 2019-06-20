#!/bin/bash
MPIEXEC_TIMEOUT=10 CRAY_CTI_DIR=$PWD/../../install CRAY_CTI_LAUNCHER_NAME=/opt/cray/pe/snplauncher/default/bin/mpiexec CRAY_CTI_WLM=generic "$@"
