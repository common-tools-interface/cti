#!/bin/bash -x
#
# runBuildPrep.sh - Preps the build environment
#
# Copyright 2019 Cray Inc.  All Rights Reserved.
#
# Unpublished Proprietary Information.
# This unpublished work is protected to trade secret, copyright and other laws.
# Except as permitted by contract or express written permission of Cray Inc.,
# no part of this work or its content may be used, reproduced or disclosed
# in any form.
#

# TODO: Once we move away from monolithic builds, point at the proper repositories
# ./add_rpm_repo.sh http://car.dev.cray.com/artifactory/internal/PE-CDST/ /x86_64/dev/master/

gcc_ver=8.1.0

function check_exit_status(){

    if [ $1 -ne 0 ]
    then
        echo "There was an error installing $2: $return_value"
        exit $return_value
    fi
}

echo "############################################"
echo "#             Installing deps              #"
echo "############################################"
zypper --non-interactive --no-gpg-check install cmake
check_exit_status $? cmake

zypper --non-interactive --no-gpg-check install flex
check_exit_status $? flex

zypper --non-interactive --no-gpg-check install bison
check_exit_status $? bison

zypper --non-interactive --no-gpg-check install binutils-devel
check_exit_status $? binutils-devel

zypper --non-interactive --no-gpg-check install mksh
check_exit_status $? mksh
    
zypper --non-interactive --no-gpg-check install bzip2
check_exit_status $? bzip2

zypper --non-interactive --no-gpg-check install libbz2-devel
check_exit_status $? libbz2-devel

zypper --non-interactive --no-gpg-check install liblzma5
check_exit_status $? liblzma5

zypper --non-interactive --no-gpg-check install xz-devel
check_exit_status $? xz-devel

zypper --non-interactive --no-gpg-check install tcl
check_exit_status $? tcl

zypper addrepo http://car.dev.cray.com/artifactory/shasta-premium/SHASTA-OS/sle15_premium/x86_64/cray/sles15-premium/ CAR
check_exit_status $? CAR

zypper --non-interactive --no-gpg-check install craype
check_exit_status $? craype

zypper --non-interactive --no-gpg-check install cray-set-gcc-libs
check_exit_status $? cray-set-gcc-libs

zypper --non-interactive --no-gpg-check install cray-gcc-$gcc_ver
check_exit_status $? cray-gcc-$gcc_ver

zypper --non-interactive --no-gpg-check install cray-modules
check_exit_status $? cray-modules

export SHELL=/bin/sh

#Ensure we can use modules
module use /opt/cray/pe/modules/default/init/bash

#Ensure CTI is build with $gcc_ver
module load gcc/$gcc_ver

echo "############################################"
echo "#      Capturing Jenkins Env Vars          #"
echo "############################################"
#Get the Jenkins BUILD_NUMBER from the ENV.
BUILD_NUMBER=$(echo $BUILD_NUMBER)
if [ ! -z $BUILD_NUMBER ]
then
  #update the release_versioning file
  sed -i.bak "s/9999/$BUILD_NUMBER/g" $PWD/release_versioning
  echo "Set version to $BUILD_NUMBER in release_versioning"
else
  echo "Unable to determine Jenkins BUILD_NUMBER"
  exit 1
fi

echo "############################################"
echo "#      Generating configure files          #"
echo "############################################"
# Create autotools generated files for this build environment
autoreconf -ifv

echo "############################################"
echo "#            Calling Configure             #"
echo "############################################"
# Create the make files
./configure --enable-static=no
return_code=$?

# Dump config.log if configure fails
if [ $return_code -ne 0 ]; then
    echo "############################################"
    echo "#          Dumping config.log              #"
    echo "############################################"
    cat config.log
fi

echo "############################################"
echo "#          Done with build prep            #"
echo "############################################"
# We want to capture the config.log in the jenkins output on error.
# But we also want to return with the return code from the configure
# call. So do that below.
exit $return_code
