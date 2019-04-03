#!/bin/bash -x
#
# runBuildPackage.sh - Package steps for CTI
#
# Copyright 2019 Cray Inc.  All Rights Reserved.
#
# Unpublished Proprietary Information.
# This unpublished work is protected to trade secret, copyright and other laws.
# Except as permitted by contract or express written permission of Cray Inc.,
# no part of this work or its content may be used, reproduced or disclosed
# in any form.
#

make install -j32

# FIXME: Get rid of the packaging script. We should be creating an rpm inside the CTI build system.
./scripts/package_cti -b /opt/cray/pe/cti/2.0.0
