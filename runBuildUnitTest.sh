#!/bin/bash
#
# runUnitTest.sh - Build steps for CTI
#
# Copyright 2019 Cray Inc. All Rights Reserved.
#
# Unpublished Proprietary Information.
# This unpublished work is protected to trade secret, copyright and other laws.
# Except as permitted by contract or express written permission of Cray Inc.,
# no part of this work or its content may be used, reproduced or disclosed
# in any form.
#
gcc_ver=8.1.0

function check_exit_status(){
    if [ $1 -ne 0 ]
    then
        echo "$0: error code of $1 from $2"
        return_code=$1
    fi
}

echo "############################################"
echo "#            Running Unit Tests            #"
echo "############################################"

#Ensure we can use modules
source /opt/cray/pe/modules/default/init/bash

#Ensure CTI is build with $gcc_ver
module load gcc/$gcc_ver
check_exit_status $? module-load-gcc

# libssh2 make check requires USER to be set
tmp_user=$USER
if [ -z "$tmp_user" ]
then
    tmp_user="root"
fi

USER=$tmp_user make check
return_code=$?

if [ $return_code -ne 0 ]
then
  echo "############################################"
  echo "#              Unit Test Log               #"
  echo "############################################"
  cat tests/unit/test-suite.log
fi

echo "############################################"
echo "#              Done with Tests             #"
echo "############################################"

exit $return_code
