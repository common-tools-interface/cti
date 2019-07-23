#!/bin/bash
#
# runPostBuild.sh - Build delivery steps for CTI
#
# Copyright 2019 Cray Inc. All Rights Reserved.
#
# Unpublished Proprietary Information.
# This unpublished work is protected to trade secret, copyright and other laws.
# Except as permitted by contract or express written permission of Cray Inc.,
# no part of this work or its content may be used, reproduced or disclosed
# in any form.
#
arch=$(uname -m)
mkdir -p /home/jenkins/rpmbuild/RPMS
cp $PWD/RPMS/$arch/cray-cti*.rpm /home/jenkins/rpmbuild/RPMS
return_code=$?

exit $return_code

