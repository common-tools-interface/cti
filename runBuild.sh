#!/bin/bash
#
# runBuild.sh - Build steps for CTI
#
# Copyright 2019-2022 Hewlett Packard Enterprise Development LP
#
# Unpublished Proprietary Information.
# This unpublished work is protected to trade secret, copyright and other laws.
# Except as permitted by contract or express written permission of Hewlett
# Packard Enterprise Development LP., no part of this work or its content may be
# used, reproduced or disclosed in any form.
#

nodeps=
for arg in "$@"; do
    if [ $arg == "--nodeps" ]; then
	nodeps=1
    fi
done

echo "############################################"
echo "#            Setup environment.            #"
echo "############################################"

top_level=$PWD
source ./external/cdst_build_library/build_lib_gcc

source $top_level/release_versioning
cti_version=
branch_name=$(get_branch_info)
branch_type=$(echo "$branch_name" | cut -d'/' -f1)
if [ "$branch_type" != "release" ]; then
  cti_version="$common_tool_major.$common_tool_minor.$revision.$build_number"
else
  cti_version="$common_tool_major.$common_tool_minor.$revision"
fi
install_dir="/opt/cray/pe/cti/$cti_version"

if [[ ! -z $BRANCH_NAME ]]; then
  echo "cleaning build env"
  rm -rf /opt/cray/pe/cti
fi

setup_modules

module load cray-dyninst
check_exit_status

module list

if [ -z "$nodeps" ]; then 
    echo "############################################"
    echo "#             Building libssh2             #"
    echo "############################################"

    # ensure submodule is checked out
    if [[ ! -f $top_level/external/libssh2/README.md ]]; then
	echo "libssh2 submodule not checked out..."
	exit 1
    fi

    cd $top_level/external/libssh2
    check_exit_status

    ./buildconf
    check_exit_status

    ./configure --prefix=$install_dir
    check_exit_status

    make $cdst_j_flags
    check_exit_status

    make $cdst_j_flags install
    check_exit_status

    cd $top_level

    echo "############################################"
    echo "#            Building libarchive           #"
    echo "############################################"

    # ensure submodule is checked out
    if [[ ! -f $top_level/external/libarchive/README.md ]]; then
	echo "libarchive submodule not checked out..."
	exit 1
    fi

    cd $top_level/external/libarchive
    check_exit_status

    autoreconf -ifv
    check_exit_status

    ./configure --prefix=$install_dir --with-pic --without-expat --without-xml2 \
		--without-openssl --without-nettle --without-lzo2 --without-lzma --without-libiconv-prefix --without-iconv \
		--without-lzmadec --without-bz2lib --disable-bsdtar --disable-bsdcpio --disable-acl
    check_exit_status

    make $cdst_j_flags
    check_exit_status

    make $cdst_j_flags install
    check_exit_status

    # the .la file interferes with the static build for the cti backend
    rm $install_dir/lib/libarchive.la 

    cd $top_level

fi

echo "############################################"
echo "#      Generating configure files          #"
echo "############################################"
# Create autotools generated files for this build environment
autoreconf -ifv
check_exit_status

echo "############################################"
echo "#            Calling Configure             #"
echo "############################################"

echo "CRAY_DYNINST_INSTALL_DIR = $CRAY_DYNINST_INSTALL_DIR"

# Create the make files
./configure --prefix="$install_dir"
if [ "$?" -ne 0 ]; then
    cat config.log
fi
check_exit_status

# Dump config.log if configure fails
get_exit_status
if [[ $? -ne 0 ]]; then
    if [[ -f config.log ]]; then
        # We want to capture the config.log in the jenkins output on error.
        echo "############################################"
        echo "#          Dumping config.log              #"
        echo "############################################"
        cat config.log
    fi
fi

echo "############################################"
echo "#               Running make               #"
echo "############################################"
make $cdst_j_flags
check_exit_status

echo "############################################"
echo "#          Running make install            #"
echo "############################################"
make $cdst_j_flags install
check_exit_status

echo "############################################"
echo "#              Done with build             #"
echo "############################################"

exit_with_status
