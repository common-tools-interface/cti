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

# Install the rpms in an OS agnostic fashion using DST scripts
#./install_rpm.sh cmake
# FIXME: install_rpm.sh not supported in pe pipeline

echo "############################################"
echo "#             Installing deps              #"
echo "############################################"
zypper --non-interactive --no-gpg-check install cmake
zypper --non-interactive --no-gpg-check install flex
zypper --non-interactive --no-gpg-check install bison
zypper --non-interactive --no-gpg-check install binutils-devel
zypper --non-interactive --no-gpg-check install mksh
zypper --non-interactive --no-gpg-check install tcl

export SHELL=/bin/sh

#Download cray-gcc-6.1.0 & cray-modules from artifactory if system does not have it installed.
artifactory_repo="http://car.dev.cray.com/artifactory"
shasta_repo="shasta-premium/SHASTA-OS/sle15_premium/x86_64/cray/sles15-premium/kernel-team/Cray-shasta-compute-sles15-1.0.149-20190612161557-gd8b9d40.rpmdir"

gcc_pkg="cray-gcc-6.1.0-201811010911.2fb57b1fae09e-343.sles15.x86_64.rpm"
gcc_url="$artifactory_repo/$shasta_repo/$gcc_pkg"

#gcclibs and craype are dependencies of gcc
gcclibs_pkg="cray-set-gcc-libs-1.0.4-201702220827.c43e1964399fe-12.x86_64.rpm"
gcclibs_url="$artifactory_repo/$shasta_repo/$gcclibs_pkg"

craype_pkg="craype-2.5.19.5-201903131827.0acb5ac2283e3-1.sles15.x86_64.rpm"
craype_url="$artifactory_repo/$shasta_repo/$craype_pkg"

modules_pkg="cray-modules-3.2.11.1-2S.201901112058.67ac7225c9b34.sles12.x86_64.rpm"
modules_url="$artifactory_repo/$shasta_repo/$modules_pkg"


queryStr=`echo $gcc_pkg | cut -d'.' -f1-6`
gcc_installed=`rpm -qa | grep $queryStr`
if [ -z $gcc_installed ]
then
  curl -v -L -o $gcclibs_pkg $gcclibs_url
  curl -v -L -o $craype_pkg $craype_url 
  curl -v -L -o $gcc_pkg $gcc_url
  rpm -ivh $PWD/$craype_pkg $PWD/$gcclibs_pkg $PWD/$gcc_pkg
  return_value=$?
  if [ $return_value -eq 1 ]
  then
    echo "There was an error installing $gcc_pkg"
    exit $return_value
  fi

fi

#get the modules package and install
queryStr="cray-modules"
modules_installed=`rpm -qa | grep $queryStr | head -1`
if [ -z $modules_installed ]
then
  curl -v -L -o $modules_pkg $modules_url
  rpm -ivh $PWD/$modules_pkg
  return_value=$?
  if [ $return_value -eq 1 ]
  then
    echo "There was an error installing $modules_pkg"
    exit $return_value
  fi
fi

#Ensure we can use modules
module use /opt/modulefiles

#Ensure CTI is build with gcc/6.1.0
module load gcc/6.1.0

echo "############################################"
echo "#      Capturing Jenkins Env Vars          #"
echo "############################################"
#Get the Jenkins BUILD_NUMBER from the ENV.
BUILD_NUMBER=$(echo $BUILD_NUMBER)
if [ ! -z $BUILD_NUMBER ]
then
  #update the release_versioning file
  sed -i .bak "s/9999/$BUILD_NUMBER/g" $PWD/release_versioning
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
