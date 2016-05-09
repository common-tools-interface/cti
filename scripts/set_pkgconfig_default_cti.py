#!/usr/bin/env python
content='''#!/bin/ksh

set install_root [install_dir]
set CRAY_CTI_LEVEL [version_string]
set PE_PRODUCT_DIR $install_root/cti/$CRAY_CTI_LEVEL
set PRODUCT_NAME "PE-CTI"

prepend-path PE_PKG_CONFIG_PATH $PE_PRODUCT_DIR/lib/pkgconfig
'''
