#!/bin/bash
#
# runInstallTest.sh - Install Test step for the CTI
#
# Copyright 2007-2021 Hewlett Packard Enterprise Development LP
#
# Unpublished Proprietary Information.
# This unpublished work is protected to trade secret, copyright and other laws.
# Except as permitted by contract or express written permission of Hewlett
# Packard Enterprise Development LP., no part of this work or its content may be
# used, reproduced or disclosed in any form.
#

source ./external/cdst_build_library/build_lib

# Remove build dir
if [ -d /opt/cray/pe/cti ]; then
  rm -rf /opt/cray/pe/cti
fi

arch=$(uname -m)
target_pm=$(get_pm)
if [[ "$target_pm" == "$cdst_pm_zypper" ]]; then
  zypper --non-interactive install $PWD/rpmbuild/RPMS/$arch/*.rpm
  check_exit_status
elif [[ "$target_pm" == "$cdst_pm_yum" ]]; then
  yum --assumeyes install $PWD/rpmbuild/RPMS/$arch/*.rpm
  check_exit_status
fi

check_exit_status

