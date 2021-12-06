#!/bin/bash
#
# runInstallTest.sh - Install Test step for the CTI
#
# Copyright 2021 Hewlett Packard Enterprise Development LP
#
# Unpublished Proprietary Information.
# This unpublished work is protected to trade secret, copyright and other laws.
# Except as permitted by contract or express written permission of Hewlett
# Packard Enterprise Development LP., no part of this work or its content may be
# used, reproduced or disclosed in any form.
#

top_level=$PWD
githubName=hpc-pe-cdst

source ./external/${githubName}-cdst_build_library/build_lib

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

# Remove build dir
if [ -d ${install_dir} ]; then
   mv ${install_dir} ${install_dir}_orig
fi

arch=$(uname -m)
target_pm=$(get_pm)
if [[ "$target_pm" == "$cdst_pm_zypper" ]]; then
  zypper --non-interactive --no-gpg-checks install $PWD/rpmbuild/RPMS/$arch/*.rpm
  check_exit_status
elif [[ "$target_pm" == "$cdst_pm_yum" ]]; then
  yum --assumeyes  --nogpgcheck install $PWD/rpmbuild/RPMS/$arch/*.rpm
  check_exit_status
fi

check_exit_status

