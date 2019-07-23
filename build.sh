#!/bin/bash
#
# Copyright 2011-2019 Cray Inc.  All Rights Reserved.
#

cmake_module="cmake/3.5.2"
gcc_module="gcc/8.1.0"
redhat_release_file="/etc/redhat-release"
suse_release_file="/etc/SuSE-release"
os_release_file="/etc/os-release"
arch=""
SLES_VER=
OS=""
modules_prefix=""

if [[ $1 == "-d" && ! -z $2 ]]
then
  BUILD_DIR=$2
else
  BUILD_DIR=$PWD/install
  if [ -d $BUILD_DIR ]
  then
    rm -rf $BUILD_DIR
  fi
  mkdir $BUILD_DIR
  chmod a+rwx $BUILD_DIR
fi
echo "Building to " $BUILD_DIR

function source_module_script() {

  if [ -e $redhat_release_file ]
  then
    # centos
    if [[ -e /usr/share/Modules/init/bash ]]; then
      source /usr/share/Modules/init/bash
    fi
  # load sles script
  elif [[ -e $modules_prefix/modules/default/init/bash ]]; then
    source $modules_prefix/modules/default/init/bash
  fi

}

function set_OS(){
  # set OS, arch
  arch=`uname -m`
  if [ -e "$redhat_release_file" ]; then
    OS="CentOS"
    SLES_VER="SLES12"
    modules_prefix=/opt/cray/pe
  elif [ -e "$os_release_file" ]; then
    SLES_VER=$(cat /etc/os-release | grep VERSION | head -1 | cut -d'"' -f2)
    if [[ $SLES_VER = 12 ]]; then
      OS="SLES12"
    elif [[ $SLES_VER = 15 ]]; then
      OS="SLES15"
    fi
    modules_prefix=/opt/cray/pe
  elif [ -e "$suse_release_file" ]; then
    SLES_VER=$(cat $suse_release_file | grep VERSION | cut -f3 -d" ")
    if [[ $SLES_VER = 11 ]]; then
      OS="SLES11"
      modules_prefix=/opt
    elif [[ $SLES_VER = 12 ]]; then
      OS="SLES12"
      modules_prefix=/opt/cray/pe
    fi
  fi
  export OS
  export arch
}

#_______________________ Start of main code ______________________________


set_OS
source_module_script
module purge
module load $gcc_module
module load $cmake_module

autoreconf -ifv
set -x
./configure --enable-static=no --prefix=$BUILD_DIR #--enable-code-coverage
set +x

make -j32
make -j32 install

LD_LIBRARY_PATH=$BUILD_DIR/lib/ make check

mkdir -p $BUILD_DIR/docs
cp $PWD/extras/docs/ATTRIBUTIONS_cti.txt $BUILD_DIR/docs

mkdir -p $BUILD_DIR/lib/pkgconfig/
rm -f $BUILD_DIR/lib/pkgconfig/*
cp $PWD/craytools_be.pc  $BUILD_DIR/lib/pkgconfig/craytools_be.pc
cp $PWD/craytools_fe.pc  $BUILD_DIR/lib/pkgconfig/craytools_fe.pc
chmod a+rwx $BUILD_DIR/lib/pkgconfig/craytools_be.pc
chmod a+rwx $BUILD_DIR/lib/pkgconfig/craytools_fe.pc

# run functional test suite

./tests/function/build_run.sh ./tests/function/

echo "Done"
