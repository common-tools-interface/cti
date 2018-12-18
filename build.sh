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
topLevel=$PWD

boostSO_Major=1
boostSO_Minor=66
boostSO_Fix=0
ulib=/cray/css/ulib
redhat_release_file="/etc/redhat-release"
suse_release_file="/etc/SuSE-release"
os_release_file="/etc/os-release"
buildRelease=0           # default to not being a build release
swDebugStr="-DCMAKE_BUILD_TYPE=RelWithDebInfo"
swCXXFLAGS=""
boost_inst_base=""
boost_inc=""
boost_full_name=${boostSO_Major}_${boostSO_Minor}_${boostSO_Fix}
arch=""
SLES_VER=
OS=""
modules_prefix=""

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
      swCXXFLAGS="-fopenmp"
    elif [[ $SLES_VER = 12 ]]; then
      OS="SLES12"
      modules_prefix=/opt/cray/pe
    fi
  fi
  export OS
  export arch

  gccVer=6.1.0
  # use OS and arch to set right gcc, boost
  if [[ $arch == "aarch64" ]]; then
    boost_inst_base=$ulib/boost/1_66_aarch64/boost_${boost_full_name}/install
    boost_inc=$boost_inst_base/include
  elif [[ $arch == "x86_64" ]]; then
    boost_inst_base=$ulib/boost/1_66/boost_${boost_full_name}/install
    boost_inc=$boost_inst_base/include
  fi

  export gccVer
}

#_______________________ Start of main code ______________________________


set_OS
source_module_script
module load gcc/$gccVer
module load $cmake_module

# Build elfutils
elfDir=$PWD/external/install
cd external/elfutils/
autoreconf -i -f
./configure --prefix=$elfDir --enable-maintainer-mode
make
make install
cd $topLevel

#
# Build DyninstAPI
#
PLATFORM=
if [[ $arch == "x86_64" ]]; then
  export PLATFORM=x86_64_cnl
  export PLATFORM=x86_64-unknown-linux2.4 # Is PLATFORM even needed?
elif [[ $arch == "aarch64" ]]; then
  export PLATFORM=aarch64-unknown-linux-gnu
fi
echo "PLATFORM IS: $PLATFORM"

swSO_Major=10
swSO_Minor=0

swPackageName=dyninst           # Dyninst 9.3 as a submodule

swSourceDir=$PWD/external/$swPackageName
swBuildDir=$swSourceDir
swInstallDir=$PWD/external/install
swPrefix=$swInstallDir

if [ -e "$swInstallDir/lib/libdyninstAPI.so" ]; then
  buildDyninstAPI=0
else
  buildDyninstAPI=1
fi

if [[ $buildDyninstAPI == 1 ]]
then
  echo "--------------------------------------------- Building DyninstAPI"
  echo  "swDebugStr='$swDebugStr'"
  mkdir -p $swBuildDir
  cd $swBuildDir
  #updated for Aarch64  (c++ to g++)
  echo arch: "$arch"
  if [[ $arch == "x86_64" ]]
  then
    set -x
    cmake $swSourceDir \
      $swDebugStr \
      -DCMAKE_C_COMPILER=/opt/gcc/$gccVer/bin/gcc \
      -DCMAKE_CXX_COMPILER=/opt/gcc/$gccVer/bin/g++ \
      -DCMAKE_INSTALL_PREFIX=$swPrefix \
      -DCMAKE_INSTALL_EXEC_PREFIX=$swPrefix \
      -DPATH_BOOST=$boost_inst_base \
      -DBoost_INCLUDE_DIR=$boost_inc \
      -DLIBDWARF_INCLUDE_DIR=$elfDir/include \
      -DLIBDWARF_LIBRARIES=$elfDir/lib/libdw.so \
      -DLIBELF_INCLUDE_DIR=$elfDir/include \
      -DLIBELF_LIBRARIES=$elfDir/lib/libelf.so \
      -DCMAKE_CXX_FLAGS=$swCXXFLAGS \
      -DUSE_OpenMP=OFF \
      $swSourceDir
      set +x
      #-DCMAKE_BUILD_TYPE=RelWithDebInfo \

  elif [[ $arch == "aarch64" ]]
  then
    set -x
    cmake $swSourceDir \
      $swDebugStr \
      -DCMAKE_C_COMPILER=/opt/gcc/$gccVer/bin/gcc \
      -DCMAKE_CXX_COMPILER=/opt/gcc/$gccVer/bin/g++ \
      -DCMAKE_INSTALL_PREFIX=$swPrefix \
      -DCMAKE_INSTALL_EXEC_PREFIX=$swPrefix \
      -DPATH_BOOST=$boost_inst_base \
      -DBoost_INCLUDE_DIR=$boost_inc \
      -DLIBDWARF_INCLUDE_DIR=$elfDir/include \
      -DLIBDWARF_LIBRARIES=$elfDir/lib/libdw.so \
      -DLIBELF_INCLUDE_DIR=$elfDir/include/libelf \
      -DLIBELF_LIBRARIES=$elfDir/lib/libelf.so \
      -DCMAKE_CXX_FLAGS=$swCXXFLAGS \
      -DUSE_OpenMP=OFF \
      $swSourceDir
      set +x
  fi

  cd $swSourceDir
  export tbb_os=linux
  make -j32
  make install
fi

cd $topLevel
autoreconf -ifv
set -x
./configure --prefix=$BUILD_DIR --with-boost=$boost_inst_base --with-dyninst=$swInstallDir --with-dyninst-libdir=$swInstallDir/lib
set +x

# Deliver DSOs and commnode on install
if [[ $doingInstall == "1" ]]
then
  mkdir -p $BUILD_DIR/lib/ $BUILD_DIR/libexec
  if [ $arch == "x86_64" ]
  then
    cp $elfDir/lib/libelf.so.1 $BUILD_DIR/lib/
  elif [ $arch = "aarch64" ]
  then
    cp $elfDir/lib64/libelf.so.1 $BUILD_DIR/lib/
  fi
  chmod -R 755 $BUILD_DIR/lib/ $BUILD_DIR/libexec
fi

make DWARF_HOME=$dwarfDir \
    SW_HOME=$swPrefix \
    BOOST_HOME=$boost_inst_base
make install

# install DSOs
mkdir -p $BUILD_DIR/lib/
cp -P $swPrefix/lib/libdyninstAPI.so* $BUILD_DIR/lib/
cp -P $swPrefix/lib/libsymtabAPI.so* $BUILD_DIR/lib/
cp -P $swPrefix/lib/libdynDwarf.so* $BUILD_DIR/lib/
cp -P $swPrefix/lib/libdynElf.so* $BUILD_DIR/lib/
cp -P $swPrefix/lib/libpcontrol.so* $BUILD_DIR/lib/
cp -P $swPrefix/lib/libcommon.so* $BUILD_DIR/lib/

cp -P $swPrefix/lib/libstackwalk.so* $BUILD_DIR/lib/
cp -P $swPrefix/lib/libpatchAPI.so* $BUILD_DIR/lib/
cp -P $swPrefix/lib/libparseAPI.so* $BUILD_DIR/lib/
cp -P $swPrefix/lib/libinstructionAPI.so* $BUILD_DIR/lib/

cp -P $swPrefix/lib/libtbb.so* $BUILD_DIR/lib/
cp -P $swPrefix/lib/libtbbmalloc.so* $BUILD_DIR/lib/
cp -P $swPrefix/lib/libtbbmalloc_proxy.so* $BUILD_DIR/lib/

cp -P $elfDir/lib/libdw*.so* $BUILD_DIR/lib/

cp $boost_inst_base/lib/libboost_thread.so.$boostSO_Major.$boostSO_Minor.$boostSO_Fix $BUILD_DIR/lib/
cp $boost_inst_base/lib/libboost_system.so.$boostSO_Major.$boostSO_Minor.$boostSO_Fix $BUILD_DIR/lib/
cp $boost_inst_base/lib/libboost_date_time.so.$boostSO_Major.$boostSO_Minor.$boostSO_Fix $BUILD_DIR/lib/
cp $boost_inst_base/lib/libboost_atomic.so.$boostSO_Major.$boostSO_Minor.$boostSO_Fix $BUILD_DIR/lib/
cp $boost_inst_base/lib/libboost_chrono.so.$boostSO_Major.$boostSO_Minor.$boostSO_Fix $BUILD_DIR/lib/
chmod -R 755 $BUILD_DIR/lib/

LD_LIBRARY_PATH=$BUILD_DIR/lib/ make tests;

cd $topLevel
mkdir -p $BUILD_DIR/docs
cp $PWD/extras/docs/ATTRIBUTIONS_cti.txt $BUILD_DIR/docs
mkdir $BUILD_DIR/examples
cp -R $PWD/examples/* $BUILD_DIR/examples

mkdir -p $BUILD_DIR/lib/pkgconfig/
rm -f $BUILD_DIR/lib/pkgconfig/*
cp $PWD/scripts/craytools_be.pc  $BUILD_DIR/lib/pkgconfig/craytools_be.pc
cp $PWD/scripts/craytools_fe.pc  $BUILD_DIR/lib/pkgconfig/craytools_fe.pc
chmod a+rwx $BUILD_DIR/lib/pkgconfig/craytools_be.pc
chmod a+rwx $BUILD_DIR/lib/pkgconfig/craytools_fe.pc

echo "Done"
