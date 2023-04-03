#!/bin/bash
#
# runUnitTest.sh - Build steps for CTI
#
# Copyright 2019-2021 Hewlett Packard Enterprise Development LP
#
# Unpublished Proprietary Information.
# This unpublished work is protected to trade secret, copyright and other laws.
# Except as permitted by contract or express written permission of Hewlett
# Packard Enterprise Development LP., no part of this work or its content may be
# used, reproduced or disclosed in any form.
#

source ./external/cdst_build_library/build_lib_gcc

setup_modules

module load cray-cdst-support
check_exit_status

echo "############################################"
echo "#            Running Unit Tests            #"
echo "############################################"

# libssh2 make check requires USER to be set
USER=${USER:-root} make $cdst_j_flags check
check_exit_status

# Dump test log if make check fails
get_exit_status
if [[ $? -ne 0 ]]; then
    if [[ -f tests/unit/test-suite.log ]]; then
        echo "############################################"
        echo "#              Unit Test Log               #"
        echo "############################################"
        cat tests/unit/test-suite.log
    fi
fi

echo "############################################"
echo "#              Done with Tests             #"
echo "############################################"

exit_with_status
