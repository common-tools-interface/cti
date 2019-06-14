#!/bin/bash -x
#
# runBuildPackage.sh - Package steps for CTI
#
# Copyright 2019 Cray Inc.  All Rights Reserved.
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
if [ -f /etc/SuSE-release ]
then
	SYSTEM=SuSE
	OS=`cat /etc/SuSE-release | head -1 | cut -d' ' -f5`
	OS_VER=sles$OS
elif [ -f /etc/redhat-release ]
then
	SYSTEM=CS
	OS_VER=el7
elif [ -f /etc/os-release ]
then
	SYSTEM=SuSE
	OS=`cat /etc/os-release | grep VERSION | head -1 | cut -d'"' -f2`
	OS_VER=sles$OS
fi

DATE_1=`git log -n 1 --pretty=format:"%ci" | tail -1 | cut -d' ' -f1-2 | head -c16`
DATE_2=$(echo $DATE_1 | sed 's/[ ]//g' | tr -d - | tr -d : | tr -d ' ') 
DATE=$(echo $DATE_2 | xargs)
GIT_FULL_REV=$(git log -n1 --pretty=%H)
GIT_SHORT_REV=$(echo $GIT_FULL_REV | head -c13)
GIT_STR=$DATE.$GIT_SHORT_REV
STATIC_VERSION=
STATIC_RELEASE_DATE=
BRANCH=$(git rev-parse --abbrev-ref HEAD)
FIELD=$(expr $BRANCH_LENGTH + 1)
REL=0

if [ $ARCH == "x86_64" ]
then
  echo "Building for x86_64"
  if [ $OS_VER == "sles11" ]
  then
    echo "Building for sles11"
	os_list_hw=5.2
	os_list_wb=sles11sp3
	system_type_hw=HARDWARE
	system_type_wb=WHITEBOX
	system_type_remove=$system_type_hw,$system_type_wb
  elif [ $OS_VER == "sles12" ]
  then
    echo "Building for sles12"
    os_list_hw=6.0,6.1,6.2,6.3,7.0
    os_list_wb=sles12
    system_type_hw=HARDWARE
    system_type_wb=WHITEBOX
    system_type_remove=$system_type_hw,$system_type_wb
  elif [ $OS_VER == "sles15" ]
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
elif [ $ARCH == "aarch64" ]
then
  echo "Building for aarch64"
  os_list_hw=6.0,6.1,6.2,6.3,7.0
  os_list_wb=sles12
  system_type_hw=AARCH64
  system_type_wb=AARCH64
  system_type_remove=AARCH64
fi

REL_DATE=`source $PWD/scripts/find_release_date.sh`
#if branch does not have a date associated with it
if [ "$STATIC_RELEASE_DATE"  == "" ]
then
  echo "Using standard release date."
  #assign standard date to it - cause we dont care
  RELEASE_DATE=$REL_DATE
#if the branch does have a release date associated with it
else
  echo "Using default release date."
  #assign release date to it
  RELEASE_DATE=$REL_DATE
fi
echo "Release Date: $RELEASE_DATE"


if [ "$STATIC_VERSION" != "" ]
then
  PKG_VERSION=$(echo $STATIC_VERSION)
  echo $PKG_VERSION  
  REL=0
fi
DATE_COMMIT=$GIT_STR-$REL
REL_OS=$REL.$OS_VER

echo " package version "
PKG_MAJOR=$(cat $PWD/release_versioning | grep ^craytool_major= | cut -d'"' -f2)
PKG_MINOR=$(cat $PWD/release_versioning | grep ^craytool_minor= | cut -d'"' -f2)
PKG_BUGFIX=$(cat $PWD/release_versioning | grep ^revision= | cut -d'"' -f2)
PKG_VERSION=$PKG_MAJOR.$PKG_MINOR.$PKG_BUGFIX
INSTALL_DIR=/opt/cray/pe/$NAME
TWO_DIGIT_VER=$PKG_MAJOR.$PKG_MINOR
TWO_DIGIT_NODOT_VER=$PKG_MAJOR$PKG_MINOR
PKG=$PKG_NAME-$PKG_VERSION-$DATE_COMMIT.$OS_VER.$ARCH.rpm

#Ensure CTI is build with gcc/6.1.0
module load gcc/6.1.0

echo "############################################"
echo "#          Running make install            #"
echo "############################################"

make install -j32 \
         PKG_VERSION=$PKG_VERSION \
         NAME=$NAME \
         PKG_NAME=$PKG_NAME \
         INSTALL_DIR=$INSTALL_DIR \

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
