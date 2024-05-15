#!/bin/bash
set -xe
echo Entering $0

./runBuildPackage.sh
./runInstallTest.sh

find / -path /proc -prune -o -name \*.rpm

# ./runPostBuild.sh is not needed since the following put the RPMs in the correct location
mkdir -p $PWD/RPMS/$TARGET_OS
cp -r /workspace/rpmbuild/RPMS/* $PWD/RPMS/$TARGET_OS

echo Exiting $0
