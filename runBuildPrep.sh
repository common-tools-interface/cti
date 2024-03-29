#!/bin/bash
#
# runBuildPrep.sh - Preps the build environment
#
# Copyright 2019-2023 Hewlett Packard Enterprise Development LP
# SPDX-License-Identifier: Linux-OpenIB

source ./external/cdst_build_library/build_lib

echo "############################################"
echo "#             Installing deps              #"
echo "############################################"
target_pm=$(get_pm)
target_os=$(get_os)
target_arch=$(get_arch)
if [[ "$target_pm" == "$cdst_pm_zypper" ]]; then
    if [[ "$target_arch" == "$cdst_arch_x86_64" ]]; then
      sudo zypper refresh -f
    elif [[ "$target_arch" == "$cdst_arch_aarch64" && "$target_os" == "$cdst_os_sles15sp5" ]]; then
      sudo zypper --non-interactive install libopenssl-devel
    fi
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
        python3-pip \
        zlib-devel
    check_exit_status

    if [[ "$target_os" == "$cdst_os_sles15sp4" || "$target_os" == "$cdst_os_sles15sp3" ]]; then
      zypper --non-interactive install libopenssl-1_1-devel
      check_exit_status
    fi

elif [[ "$target_pm" == "$cdst_pm_yum" ]]; then
    if [[ "$target_os" == "$cdst_os_centos8" ]]; then 
      # Note the following will be different on build VMs vs DST. Errors are okay.
      yum config-manager --set-enabled powertools
    fi
    # Install yum based components
    yum --assumeyes install \
        autoconf \
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
        python3-pip \
        wget
    check_exit_status

    if [[ "$target_arch" == "$cdst_arch_aarch64" && "$target_os" == "$cdst_os_rhel84" ]]; then
      yum --assumeyes install \
      https://arti.hpc.amslabs.hpecorp.net/artifactory/hpe-rhel-remote/EL8/Update4/GA/CRB/os/Packages/autoconf-archive-2018.03.13-1.el8.noarch.rpm
    else
      yum --assumeyes install autoconf-archive
    fi
    check_exit_status
else
    # Unknown OS! Exit with error.
    echo "Unsupported Package Manager detected!"
    exit 1
fi

# Install the common PE components
install_common_pe
check_exit_status

# Install cdst_support
install_cdst_support
check_exit_status

# Install Dyninst
install_dyninst
check_exit_status

capture_jenkins_build
check_exit_status

echo "############################################"
echo "#          Done with build prep            #"
echo "############################################"

exit_with_status
