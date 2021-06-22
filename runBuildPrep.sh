#!/bin/bash
#
# runBuildPrep.sh - Preps the build environment
#
# Copyright 2019-2020 Hewlett Packard Enterprise Development LP
#
# Unpublished Proprietary Information.
# This unpublished work is protected to trade secret, copyright and other laws.
# Except as permitted by contract or express written permission of Hewlett
# Packard Enterprise Development LP., no part of this work or its content may be
# used, reproduced or disclosed in any form.
#

source ./external/cdst_build_library/build_lib

echo "############################################"
echo "#             Installing deps              #"
echo "############################################"
target_pm=$(get_pm)
target_os=$(get_os)
if [[ "$target_pm" == "$cdst_pm_zypper" ]]; then
    # Install zypper based dependencies
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
        mksh \
        libtool \
        rpm-build \
        zlib-devel
    check_exit_status
elif [[ "$target_pm" == "$cdst_pm_yum" && "$target_os" == "$cdst_os_centos8" ]]; then
    # Install yum based components
    # Note the following will be different on build VMs vs DST. Errors are okay.
    yum config-manager --set-enabled PowerTools
    yum config-manager --set-enabled dst-remote-centos8-PowerTools
    yum --assumeyes install \
        autoconf \
        autoconf-archive \
        automake \
        binutils \
        binutils-devel \
        environment-modules \
        glibc-devel \
        m4 \
        make \
        libtool \
        rpm-build \
        zlib-devel \
        tcl \
        wget
    check_exit_status
elif [[ "$target_pm" == "$cdst_pm_yum" && "$target_os" == "$cdst_os_rhel83" ]]; then
    # Install yum based components
    # Note the following will be different on build VMs vs DST. Errors are okay.
    yum subscription-manager repos --enable codeready-builder-for-rhel-8-x86_64-rpms
    yum --assumeyes install \
        autoconf \
        autoconf-archive \
        automake \
        binutils \
        binutils-devel \
        environment-modules \
        glibc-devel \
        m4 \
        make \
        libtool \
        rpm-build \
        zlib-devel \
        tcl \
        wget
    check_exit_status
else
    # Unknown OS! Exit with error.
    echo "Unsupported Package Manager detected!"
    exit 1
fi

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
