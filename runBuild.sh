#!/bin/bash
#
# runBuild.sh - Build steps for CTI
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
return_code=0
j_flags=$(rpm -E %{?_smp_mflags})

function check_exit_status(){
    if [ $1 -ne 0 ]
    then
        echo "runBuild.sh: error code of $1 from $2"
        return_code=$1
    fi
}

#Ensure we can use modules
source /opt/cray/pe/modules/default/init/bash

#Ensure CTI is build with $gcc_ver
module load gcc/$gcc_ver
check_exit_status $? module-load-gcc

module load cray-cdst-support
check_exit_status $? module-load-cray-cdst-support

echo "############################################"
echo "#      Generating configure files          #"
echo "############################################"
# Create autotools generated files for this build environment
autoreconf -ifv
check_exit_status $? autoreconf-ifv

echo "############################################"
echo "#            Calling Configure             #"
echo "############################################"
#TODO: add param to script to optionally run configure with caching enabled?
# Create the make files
./configure
check_exit_status $? configure

# Dump config.log if configure fails
if [ $return_code -ne 0 ]; then
    # We want to capture the config.log in the jenkins output on error.
    echo "############################################"
    echo "#          Dumping config.log              #"
    echo "############################################"
    if [ ! -f config.log ]; then
	check_exit_status 1 config-log-dump-to-stdout
    else
	cat config.log
    fi
fi

echo "############################################"
echo "#               Running make               #"
echo "############################################"
make $j_flags
check_exit_status $? make

echo "############################################"
echo "#          Running make install            #"
echo "############################################"
make $j_flags install
check_exit_status $? "make install"

echo "############################################"
echo "#          Running make check              #"
echo "############################################"
# runBuildUnitTest isn't run by Jenkins, so building it
# here to runBuildPackage can make cti-tests.rpm

# libssh2 make check requires USER to be set
USER=${USER:-root} make $j_flags check TESTS=
check_exit_status $? "make check"



echo "############################################"
echo "#              Done with build             #"
echo "############################################"
exit $return_code
