#!/bin/bash
#
# runBuildPackage.sh - Package steps for CTI
#
# Copyright 2019 Cray Inc. All Rights Reserved.
#
# Unpublished Proprietary Information.
# This unpublished work is protected to trade secret, copyright and other laws.
# Except as permitted by contract or express written permission of Cray Inc.,
# no part of this work or its content may be used, reproduced or disclosed
# in any form.
#
gcc_ver=8.1.0
return_code=0
top_level=$PWD
j_flags=$(rpm -E %{?_smp_mflags})

function check_exit_status(){
    if [ $1 -ne 0 ]
    then
        echo "$0: error code of $1 from $2"
        return_code=$1
    fi
}

#Ensure we can use modules
source /usr/share/Modules/init/bash

module use /opt/modulefiles
#Ensure CTI is build with $gcc_ver
module load gcc/$gcc_ver

module use /opt/cray/pe/modulefiles
#Ensure cdst-support module is loaded
module load cray-cdst-support
check_exit_status $? module-load-cray-cdst-support

echo "############################################"
echo "#             Creating rpm                 #"
echo "############################################"
rpmbuilddir=${top_level}/rpmbuild
cd ${rpmbuilddir}
rpmbuild -bb -D "_topdir ${rpmbuilddir}" SPECS/cray-cti.spec
return_code=$?

echo "############################################"
echo "#          Done with packaging             #"
echo "############################################"

exit $return_code
