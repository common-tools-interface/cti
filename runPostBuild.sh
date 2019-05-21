#!/bin/bash -x
#
# runPostBuild.sh - Build delivery steps for CTI
#
# Copyright 2019 Cray Inc.  All Rights Reserved.
#
# Unpublished Proprietary Information.
# This unpublished work is protected to trade secret, copyright and other laws.
# Except as permitted by contract or express written permission of Cray Inc.,
# no part of this work or its content may be used, reproduced or disclosed
# in any form.
#
arch=$(uname -m)
branch=$(git branch | grep '^*' | colrm 1 2)
return_code=0
if [ $branch == 'master' ]
then
  mkdir -p /home/jenkins/rpmbuild/RPMS
  cp $PWD/RPMS/$arch/cray-cti*.rpm /home/jenkins/rpmbuild/RPMS/
  return_code=$?
fi
exit $return_code

