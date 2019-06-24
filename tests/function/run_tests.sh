#!/bin/bash
if test ./avocado-virtual-environment ; then
    echo "Valid avocado virtual environment for testing..."
    if ./valid_ssh.sh ; then
        export MPIEXEC_TIMEOUT=10
        export CRAY_CTI_DIR=$PWD/../../install
        export CRAY_CTI_LAUNCHER_NAME=/opt/cray/pe/snplauncher/default/bin/mpiexec
        export CRAY_CTI_WLM=generic
        ./avocado-virtual-environment/avocado/bin/avocado run ./avocado_tests.py
    else
        echo "No valid SSH setup. Cannot execute tests"
    fi
else
    echo "No avocado environment setup. Cannot execute tests"
    exit 1
fi
