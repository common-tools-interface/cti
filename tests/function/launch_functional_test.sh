#!/bin/bash
if [ "$#" -lt 1 ]; then
    echo "Illegal number of arguments"
    echo "Expected at least 1 executable with optional parameters"
else
    MPIEXEC_TIMEOUT=10 CRAY_CTI_DIR=$PWD/../../install CRAY_CTI_LAUNCHER_NAME=/opt/cray/pe/snplauncher/default/bin/mpiexec CRAY_CTI_WLM=generic "$@"
fi
