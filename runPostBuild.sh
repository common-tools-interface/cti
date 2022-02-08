#!/bin/bash
#
# runPostBuild.sh - Build delivery steps for CTI
#
# Copyright 2019-2021 Hewlett Packard Enterprise Development LP
#
# Unpublished Proprietary Information.
# This unpublished work is protected to trade secret, copyright and other laws.
# Except as permitted by contract or express written permission of Hewlett
# Packard Enterprise Development LP., no part of this work or its content may be
# used, reproduced or disclosed in any form.
#

source ./external/cdst_build_library/build_lib

arch=$(uname -m)
mkdir -p /home/jenkins/rpmbuild/RPMS
check_exit_status
cp -a $PWD/rpmbuild/RPMS/$arch/* /home/jenkins/rpmbuild/RPMS
check_exit_status

exit_with_status
