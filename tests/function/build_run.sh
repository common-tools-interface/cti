#!/bin/bash
chmod +x ./avo_setup.sh
chmod +x ./launch_functional_test.sh
if ./avo_setup.sh ; then
    if ssh $HOSTNAME exit ; then #If MPI apps can run
        ./avocado-virtual-environment/avocado/bin/avocado run ./avocado_tests.py
    else
        echo "SSH not properly setup to allow for testing: TODO: JUST FIX THAT HERE"
    fi
else
    echo "Failed to setup avocado environment."
fi

