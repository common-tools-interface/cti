#!/bin/bash
set -xe
echo Entering $0

echo Show TARGET_OS and TARGET_ARCH
set | grep TARGET || true

# For backward compatability with Jenkins builds
ln -s /workspace /home/jenkins 
git config --global --add safe.directory /workspace

test -f ./runBuildPrep.sh && ./runBuildPrep.sh

echo Exiting $0
