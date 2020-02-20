#!/bin/bash  
#
# runBuildPrep.sh - Preps the build environment
#
# Copyright 2020 Cray Inc. All Rights Reserved.
#
# Unpublished Proprietary Information.
# This unpublished work is protected to trade secret, copyright and other laws.
# Except as permitted by contract or express written permission of Cray Inc.,
# no part of this work or its content may be used, reproduced or disclosed
# in any form.
#

gcc_ver=8.1.0
return_code=0

function check_exit_status(){
    if [ $1 -ne 0 ]
    then
        echo "runBuildPrep.sh: error code of $1 from $2"
        return_code=$1
    fi
}

echo "############################################"
echo "#             Installing deps              #"
echo "############################################"
yum --assumeyes install autoconf \
    automake \
    binutils \
    binutils-devel \
    glibc-devel\
    m4 \
    make \
    libtool \
    rpm-build \
    zlib-devel \
    tcl \
    wget
check_exit_status $? sys-pkgs

echo "############################################"
echo "#     Installing centos specific deps      #"
echo "############################################"
# Centos8 distro does not provide autoconf-archive
wget http://mirror.centos.org/centos/8/PowerTools/x86_64/os/Packages/autoconf-archive-2018.03.13-1.el8.noarch.rpm

yum-config-manager --add-repo http://car.dev.cray.com/artifactory/pe-base/PE-CENV/sle15_premium/x86_64/dev/master
check_exit_status $? car-shasta-premium-add-repo

yum --assumeyes --nogpgcheck install cray-pe-set-default-3.0 

check_exit_status $? car-shasta-premium-install-modules

yum-config-manager --disable car.dev.cray.com_artifactory_pe-base_PE-CENV_sle15_premium_x86_64_dev_master

# Currently CS & aarch64 are not built in dst pipeline, so install rpms directly from dropoff
rpm -ivh /cray/css/pe/dropoff/craype-2.6.5-202002101712.ed5b67c9e93e2-1.rhel8.aarch64.rpm \
/cray/css/pe/dropoff/cray-gcc-8.1.0-201902191726.214562397661c-21.sles15.aarch64.rpm \
/cray/css/pe/dropoff/cray-set-gcc-libs-1.0.4-201704192335.3b11ab60d4504-15.aarch64.rpm

rpm -ivh $PWD/autoconf-archive-2018.03.13-1.el8.noarch.rpm

rm -rf $PWD/autoconf-archive-2018.03.13-1.el8.noarch.rpm



echo "############################################"
echo "#      Capturing Jenkins Env Vars          #"
echo "############################################"
#Get the Jenkins BUILD_NUMBER from the ENV.
BUILD_NUMBER=$(echo $BUILD_NUMBER)
if [ ! -z $BUILD_NUMBER ]
then
  #update the release_versioning file
  sed -i.bak "s/revision=\"9999\"/revision=\"$BUILD_NUMBER\"/g" $PWD/release_versioning
  echo "Set version to $BUILD_NUMBER in release_versioning"
else
  echo "Unable to determine Jenkins BUILD_NUMBER"
  return_code=1
fi

echo "############################################"
echo "#          Done with build prep            #"
echo "############################################"
exit $return_code
