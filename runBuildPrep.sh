#!/bin/bash -x
#
# runBuildPrep.sh - Preps the build environment
#
# Copyright 2019 Cray Inc.  All Rights Reserved.
#
# Unpublished Proprietary Information.
# This unpublished work is protected to trade secret, copyright and other laws.
# Except as permitted by contract or express written permission of Cray Inc.,
# no part of this work or its content may be used, reproduced or disclosed
# in any form.
#

# TODO: Once we move away from monolithic builds, point at the proper repositories
# ./add_rpm_repo.sh http://car.dev.cray.com/artifactory/internal/PE-CDST/ /x86_64/dev/master/

# Install the rpms in an OS agnostic fashion using DST scripts
#./install_rpm.sh cmake
# FIXME: install_rpm.sh not supported in pe pipeline
echo "############################################"
echo "#             Installing deps              #"
echo "############################################"
zypper --non-interactive --no-gpg-check install cmake
zypper --non-interactive --no-gpg-check install flex
zypper --non-interactive --no-gpg-check install bison
zypper --non-interactive --no-gpg-check install binutils-devel

echo "############################################"
echo "#      Generating configure files          #"
echo "############################################"
# Create autotools generated files for this build environment
autoreconf -ifv

echo "############################################"
echo "#            Calling Configure             #"
echo "############################################"
# Create the make files
./configure --enable-static=no
return_code=$?

# Dump config.log if configure fails
if [ $return_code -ne 0 ]; then
    echo "############################################"
    echo "#          Dumping config.log              #"
    echo "############################################"
    cat config.log
fi

echo "xx$BUILD_NUMBER"

# We want to capture the config.log in the jenkins output on error.
# But we also want to return with the return code from the configure
# call. So do that below.
exit $return_code
