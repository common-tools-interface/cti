#!/usr/bin/env python
content='''#%Module######################################################################
 #
 #       (C) COPYRIGHT CRAY INC.
 #       UNPUBLISHED PROPRIETARY INFORMATION.
 #       ALL RIGHTS RESERVED.
 #
 
 set CRAY_CTI_LEVEL   <version> 
 set CRAY_CTI_CURPATH  [install_dir]/cray-cti/$CRAY_CTI_LEVEL
 
 setenv CRAY_CTI_VERSION  $CRAY_CTI_LEVEL
 setenv CRAY_CTI_DIR $CRAY_CTI_CURPATH
 setenv CRAY_CTI_LIB_DIR $CRAY_CTI_CURPATH/lib
 
 append-path	PE_PRODUCT_LIST   CRAY-CTI
 prepend-path	PATH $CRAY_CTI_CURPATH/bin
 prepend-path	MANPATH $CRAY_CTI_CURPATH/man

 if { [ file exists ${CRAY_CTI_CURPATH}/release_info ] } {
       set REL_INFO [ exec cat ${CRAY_CTI_CURPATH}/release_info ]
    } else {
       set REL_INFO ""
    }


 proc ModulesHelp { } {
    global CRAY_CTI_CURPATH
    global CRAY_CTI_LEVEL
    global REL_INFO
    puts stderr "The modulefile defines the system paths and"
    puts stderr "variables for the product cray-cti."
    puts stderr "$REL_INFO"
    puts stderr "============================================="
    puts stderr "To re-display cray-cti/$CRAY_CTI_LEVEL release information,"
    puts stderr "type:    less $CRAY_CTI_CURPATH/release_info"
    puts stderr "==============================================\n"
    puts stderr ""
 }


module-whatis   "Loads the Cray Tools Interface."

'''
