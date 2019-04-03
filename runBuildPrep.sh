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
./install_rpm.sh cmake

# Ensure git submodules are checked out
# TODO: DST claims that they can automatically do this for us...
git submodule udpate --init --recursive --remote

# Create autotools generated files for this build environment
autoreconf -ifv

# Create the make files
./configure --enable-static=no
