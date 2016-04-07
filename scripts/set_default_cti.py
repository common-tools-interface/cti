#!/usr/bin/env python
content='''#!/bin/ksh
# SVNID @(#)$Id: set_default_template 1277 2011-06-20 17:45:47Z bam $

# Builder: You must edit this script to define:
#    product_name= product name as it appears in /opt or /opt/cray 
#    version_string= version being installed
#    install_dir= either /opt/cray or /opt
#    module_dir= either /opt/modulefiles or /opt/cray/modulefiles
#    mod_names= list of names of modulefiles for this install  i.e. ="xt-mpt xt-mpich2 xt-shmem"
#
#    Save this file in [install_dir]/[version_string] as  set_default_[product_name]_[version_string]
#    You can invoke your finished script with "--help" or with "-cray_links_only"

export CRAY_product=cti
export CRAY_version=[version_string]
export CRAY_inst_dir=[install_dir]
export CRAY_mod_dir=[install_dir]/modulefiles
export CRAY_mod_names=cray-cti

if [ -L /opt/cray/bin/set_default ] ; then
  /opt/cray/bin/set_default "$@"
elif [ -L /opt/cray/pe/bin/set_default ] ; then
  /opt/cray/pe/bin/set_default "$@"
else
  echo "The set_default script is not installed in /opt/cray/bin,"
  echo "or /opt/cray/pe/bin. CrayPE must be installed."
  exit 1
fi'''
