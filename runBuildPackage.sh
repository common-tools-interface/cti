#!/bin/bash
#
# runBuildPackage.sh - Package steps for CTI
#
# Copyright 2019 Cray Inc. All Rights Reserved.
#
# Unpublished Proprietary Information.
# This unpublished work is protected to trade secret, copyright and other laws.
# Except as permitted by contract or express written permission of Cray Inc.,
# no part of this work or its content may be used, reproduced or disclosed
# in any form.
#

NAME=cti
PKG_NAME=cray-cti
CUR_DIR=$PWD
ARCH=`uname -m`
OS_VER=""
OS=""
SYSTEM=""
PKG_VERSION=""
PKG=""
RPM_REL=""
FINAL_REL=""
REL=""
GIT_STR=""
os_list_hw=
os_list_wb=
system_type_hw=
system_type_wb=
system_type_remove=
MAN_DIR=$PWD/external/install/share/man

#check what system we are building on
if [ -f /etc/redhat-release ]
then
	SYSTEM=CS
	OS_VER=el7
elif [ -f /etc/os-release ]
then
	SYSTEM=SuSE
	OS=`cat /etc/os-release | grep VERSION_ID= | cut -d'"' -f2`
	OS_VER=sles$OS
fi

DATE_1=`git log -n 1 --pretty=format:"%ci" | tail -1 | cut -d' ' -f1-2 | head -c16`
DATE_2=$(echo $DATE_1 | sed 's/[ ]//g' | tr -d - | tr -d : | tr -d ' ')
DATE=$(echo $DATE_2 | xargs)
GIT_FULL_REV=$(git log -n1 --pretty=%H)
GIT_SHORT_REV=$(echo $GIT_FULL_REV | head -c13)
GIT_STR=$DATE.$GIT_SHORT_REV
STATIC_VERSION=
BRANCH=$(git rev-parse --abbrev-ref HEAD)
FIELD=$(expr $BRANCH_LENGTH + 1)
REL=0

if [ $OS_VER == "sles15" ]
then
    echo "Building for sles15"
    os_list_hw=7.0
    os_list_wb=sles15
    system_type_hw=HARDWARE
    system_type_wb=WHITEBOX
    system_type_remove=$system_type_hw,$system_type_wb
elif [ $OS_VER == "el7" ]
then
    echo "Building for el7"
    os_list_hw=6.0,6.1,6.2,6.3,7.0
    os_list_wb=sles12
    system_type_hw=HARDWARE
    system_type_wb=WHITEBOX
    system_type_remove=$system_type_hw,$system_type_wb
fi

RELEASE_DATE=`source $PWD/scripts/find_release_date.sh`
REL_OS=$REL.$OS_VER
PKG_MAJOR=$(cat $PWD/release_versioning | grep ^common_tool_major= | cut -d'"' -f2)
PKG_MINOR=$(cat $PWD/release_versioning | grep ^common_tool_minor= | cut -d'"' -f2)
PKG_BUGFIX=$(cat $PWD/release_versioning | grep ^revision= | cut -d'"' -f2)
PKG_VERSION=$PKG_MAJOR.$PKG_MINOR.$PKG_BUGFIX
INSTALL_DIR=/opt/cray/pe/$NAME
TWO_DIGIT_VER=$PKG_MAJOR.$PKG_MINOR
TWO_DIGIT_NODOT_VER=$PKG_MAJOR$PKG_MINOR
BUILD_META=$BUILD_METADATA
PKG=$PKG_NAME-$PKG_VERSION-$BUILD_META.$ARCH.rpm

#set gcc version
gcc_ver=8.1.0

#Ensure we can use modules
source /opt/cray/pe/modules/default/init/bash

#Ensure CTI is build with $gcc_ver
module load gcc/$gcc_ver

echo "############################################"
echo "#          Running make install            #"
echo "############################################"

make install -j32
return_code=$?
# Short circuit if make install failed
if [ $return_code -ne 0 ]; then
    exit $return_code
fi

echo "############################################"
echo "#             Creating rpm                 #"
echo "############################################"
make rpm \
         PKG=$PKG \
         ARCH=$ARCH \
         PKG_VERSION=$PKG_VERSION \
         GIT_REV=$GIT_STR \
         TWO_DIGIT_VER=$TWO_DIGIT_VER \
         TWO_DIGIT_NODOT_VER=$TWO_DIGIT_NODOT_VER \
         OS_VER=.$OS_VER \
         REL=$REL \
         GIT_FULL_REV=$GIT_FULL_REV \
         NAME=$NAME \
    	 PKG_NAME=$PKG_NAME \
         CUR_DIR=$CUR_DIR \
	 BUILD_METADATA=$BUILD_META \
         INSTALL_DIR=$INSTALL_DIR \
	 REL_DATE="$RELEASE_DATE" \
	 OS_LIST_HW=$os_list_hw \
         OS_LIST_WB=$os_list_wb \
         SYSTEM_TYPE_HW=$system_type_hw \
	 SYSTEM_TYPE_WB=$system_type_wb \
	 SYSTEM_TYPE_REMOVE=$system_type_remove \
         MANPATH=$MAN_DIR

return_code=$?

echo "############################################"
echo "#          Done with packaging             #"
echo "############################################"

exit $return_code
