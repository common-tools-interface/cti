#!/bin/bash
chmod +x ./avo_setup.sh
chmod +x ./launch_functional_test.sh
if ./avo_setup.sh ; then
    #If mpi apps can run
    if ./ssh_valid.sh ; then
        export MPIEXEC_TIMEOUT=10
        export CRAY_CTI_DIR=$PWD/../../install
        export CRAY_CTI_LAUNCHER_NAME=/opt/cray/pe/snplauncher/default/bin/mpiexec
        export CRAY_CTI_WLM=generic
        ./avocado-virtual-environment/avocado/bin/avocado run ./avocado_tests.py
    else
        echo "SSH not properly setup to allow for testing."
        exit 1
    fi
else
    echo "Failed to setup avocado environment."
    exit 1
fi

