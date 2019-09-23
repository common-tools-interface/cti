#!/bin/bash
#
# A basic script to call the Jenkins scripts to build cti and all of its
# dependencies.
#
# Copyright 2011-2019 Cray Inc. All Rights Reserved.
#
# Unpublished Proprietary Information.
# This unpublished work is protected to trade secret, copyright and other laws.
# Except as permitted by contract or express written permission of Cray Inc.,
# no part of this work or its content may be used, reproduced or disclosed
# in any form.
./runBuildPrep.sh
./runBuild.sh
