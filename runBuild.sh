#!/bin/bash
#
# runBuild.sh - Build steps for CTI
#
# Copyright 2019 Cray Inc. All Rights Reserved.
#
# Unpublished Proprietary Information.
# This unpublished work is protected to trade secret, copyright and other laws.
# Except as permitted by contract or express written permission of Cray Inc.,
# no part of this work or its content may be used, reproduced or disclosed
# in any form.
#
echo "############################################"
echo "#               Running make               #"
echo "############################################"

make -j32
return_code=$?

echo "############################################"
echo "#              Done with build             #"
echo "############################################"

exit $return_code
