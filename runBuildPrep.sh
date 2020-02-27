#!/bin/bash
#
# runBuildPrep.sh - Preps the build environment
#
# Copyright 2019-2020 Cray Inc. All Rights Reserved.
#
# Unpublished Proprietary Information.
# This unpublished work is protected to trade secret, copyright and other laws.
# Except as permitted by contract or express written permission of Cray Inc.,
# no part of this work or its content may be used, reproduced or disclosed
# in any form.
#

source ./cdst_build_library/build_lib

echo "############################################"
echo "#             Installing deps              #"
echo "############################################"
zypper --non-interactive install \
    autoconf \
    autoconf-archive \
    automake \
    binutils \
    binutils-devel \
    glibc-devel-static \
    m4 \
    make \
    make-lang \
    libtool \
    rpm-build \
    zlib-devel
check_exit_status $? sys-pkgs

# Install the common PE components
install_common_pe

# Install cdst_support
install_cdst_support

capture_jenkins_build
check_exit_status

echo "############################################"
echo "#          Done with build prep            #"
echo "############################################"

exit_with_status
