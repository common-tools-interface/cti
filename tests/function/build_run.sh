#!/bin/bash
chmod +x ./avo_setup.sh
chmod +x ./launch_functional_test.sh
if ./avo_setup.sh ; then
    ./avocado-virtual-environment/avocado/bin/avocado run simp_avo_test.py
else
    echo "Failed to setup avocado environment"
fi

