#!/bin/bash
#
# runBuildPackage.sh - Package steps for CTI
#
# Copyright 2019-2020 Hewlett Packard Enterprise Development LP
#
# Unpublished Proprietary Information.
# This unpublished work is protected to trade secret, copyright and other laws.
# Except as permitted by contract or express written permission of Hewlett
# Packard Enterprise Development LP., no part of this work or its content may be
# used, reproduced or disclosed in any form.
#

source ./external/cdst_build_library/build_lib

setup_modules

module load cray-cdst-support
check_exit_status

echo "############################################"
echo "#             Creating rpm                 #"
echo "############################################"
rpmbuilddir=$PWD/rpmbuild
cd ${rpmbuilddir}
check_exit_status
rpmbuild -bb -D "_topdir ${rpmbuilddir}" SPECS/cray-cti.spec
check_exit_status

echo "############################################"
echo "#          Done with packaging             #"
echo "############################################"

exit_with_status
