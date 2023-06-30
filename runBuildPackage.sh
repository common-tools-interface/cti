#!/bin/bash
#
# runBuildPackage.sh - Package steps for CTI
#
# Copyright 2019-2023 Hewlett Packard Enterprise Development LP
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
echo "#          Generating Cpedocs              #"
echo "############################################"

# This doesn't deliver any documentation anywhere, it only tests the
# low bar that collect_cpedocs.sh still runs without an error.
pushd doc/cpedocs
./collect_cpedocs.sh
check_exit_status
popd

echo "############################################"
echo "#          Updating Changelog              #"
echo "############################################"
# Generate the release notes from DE template
pip install -r external/changelog/requirements.txt

rpmbuilddir=$PWD/rpmbuild
/usr/bin/python3 ./external/changelog/generate_release_notes.py \
   -t $PWD/external/changelog/release-notes-template.md.j2 \
   -y ${rpmbuilddir}/SOURCES/release_notes_data.yaml \
   -d ${rpmbuilddir}/SOURCES

# Update the changelog & release notes
source ./external/changelog/manage_release_notes.sh -c -r
check_exit_status

echo "############################################"
echo "#             Creating rpm                 #"
echo "############################################"
cd ${rpmbuilddir}
check_exit_status
rpmbuild -bb -D "_topdir ${rpmbuilddir}" SPECS/cray-cti.spec
check_exit_status

if [ -f $PWD/rpmbuild/BUILD/release_notes.md ]; then
  echo
  echo
  echo "Release Notes in rpmbuild/BUILD:"
  cat $PWD/BUILD/release_notes.md
fi

echo "############################################"
echo "#          Done with packaging             #"
echo "############################################"

exit_with_status
