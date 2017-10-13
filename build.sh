#!/bin/bash

cmake_module="cmake/3.5.2"

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
  if [ -e "/etc/redhat-release" ]; then
    if [[ -e /usr/share/Modules/init/bash ]]; then
      source /usr/share/Modules/init/bash
    fi
  else
    if [[ -e $modules_prefix/modules/default/init/bash ]]; then
      source $modules_prefix/modules/default/init/bash
    fi
  fi
}

function set_prefix(){
    SLES_VER=$(cat /etc/SuSE-release 2>&1 /dev/null | grep VERSION | cut -f3 -d" ")
    if [[ $SLES_VER -lt 12 ]]
    then
	modules_prefix=/opt
    else
	modules_prefix=/opt/cray/pe
    fi
}

set_prefix
source_module_script

module purge 2>/dev/null
module load $cmake_module
module list

autoreconf -ifv

./configure --prefix=$BUILD_DIR --with-gdb;

make;
make install
make tests;

mkdir $BUILD_DIR/docs
cp $PWD/extras/docs/ATTRIBUTIONS_cti.txt $BUILD_DIR/docs
mkdir $BUILD_DIR/examples
cp -R $PWD/examples/* $BUILD_DIR/examples

rm -f $BUILD_DIR/lib/pkgconfig/*
cp $PWD/scripts/craytools_be.pc  $BUILD_DIR/lib/pkgconfig/craytools_be.pc
cp $PWD/scripts/craytools_fe.pc  $BUILD_DIR/lib/pkgconfig/craytools_fe.pc
chmod a+rwx $BUILD_DIR/lib/pkgconfig/craytools_be.pc
chmod a+rwx $BUILD_DIR/lib/pkgconfig/craytools_fe.pc

echo "Done"
