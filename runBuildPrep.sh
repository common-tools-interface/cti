#!/bin/bash
#
# runBuildPrep.sh - Preps the build environment
#
# Copyright 2019 Cray Inc. All Rights Reserved.
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
return_code=0

function check_exit_status(){

    if [ $1 -ne 0 ]
    then
        echo "There was an error installing $2: $return_value"
        return_code=$1
    fi
}

echo "############################################"
echo "#             Installing deps              #"
echo "############################################"
zypper --non-interactive install autoconf \
				 automake \
				 binutils-devel \
				 bison \
				 bzip2 \
				 cmake \
				 ctags \
				 flex \
				 m4 \
				 make \
				 makeinfo \
				 mksh \
				 ncurses \
				 ncurses-devel \
				 libbz2-devel \
				 liblzma5 \
				 libtool \
				 tcl \
				 python-devel \
				 which \
				 xz-devel
check_exit_status $? sys-pkgs

zypper addrepo http://car.dev.cray.com/artifactory/shasta-premium/SHASTA-OS/sle15_premium/x86_64/cray/sles15-premium/ car-shasta-premium
check_exit_status $? car-shasta-premium-add-repo

zypper --non-interactive --no-gpg-check install craype \
					cray-set-gcc-libs \
					cray-gcc-$gcc_ver \
					cray-modules
check_exit_status $? car-shasta-premium-install-modules

zypper rr car-shasta-premium

source /opt/cray/pe/modules/default/init/bash
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
  return_code=1 
fi

echo "############################################"
echo "#      Generating configure files          #"
echo "############################################"
# Create autotools generated files for this build environment
autoreconf -ifv
#TODO: should we cature autoreconf return value?

echo "############################################"
echo "#            Calling Configure             #"
echo "############################################"
# Create the make files
#TODO: add param to script to optionally run configure with caching enabled?
./configure --enable-static=no
return_code=$?

# Dump config.log if configure fails
if [ $return_code -ne 0 ]; then
    echo "############################################"
    echo "#          Dumping config.log              #"
    echo "############################################"
    if [ ! -f config.log ]; then
	echo "config.log not found!"
    else
	cat config.log
    fi
fi

echo "############################################"
echo "#          Done with build prep            #"
echo "############################################"
# We want to capture the config.log in the jenkins output on error.
# But we also want to return with the return code from the configure
# call. So do that below.
exit $return_code
