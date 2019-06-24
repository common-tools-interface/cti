#!/bin/bash
chmod +x ./avo_setup.sh
chmod +x ./launch_functional_test.sh
if ./avo_setup.sh ; then
    #If mpi apps can run
    if ./valid_ssh.sh ; then
        ./run_tests.sh
    else
        echo "SSH not properly setup to allow for testing."
        exit 1
    fi
else
    echo "Failed to setup avocado environment."
    exit 1
fi

