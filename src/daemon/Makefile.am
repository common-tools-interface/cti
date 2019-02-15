#
# Makefile.am for the dlaunch components of the craytool interface.
#
# Copyright2014 Cray Inc.  All Rights Reserved.
#
# Unpublished Proprietary Information.
# This unpublished work is protected to trade secret, copyright and other laws.
# Except as permitted by contract or express written permission of Cray Inc.,
# no part of this work or its content may be used, reproduced or disclosed
# in any form.
#
## $HeadURL$
## $Date$
## $Rev$
## $Author$

SRC                 = @CRAYTOOL_DIR@/src
USEFUL              = $(SRC)/useful

AM_CFLAGS       	= -I$(SRC)
AM_LIBTOOLFLAGS 	= --quiet

libexec_PROGRAMS	= cti_dlaunch@CRAYTOOL_RELEASE_VERSION@

cti_dlaunch@CRAYTOOL_RELEASE_VERSION@_SOURCES	= cti_daemon.c alps_dl.c cray_slurm_dl.c slurm_dl.c
cti_dlaunch@CRAYTOOL_RELEASE_VERSION@_CFLAGS	= $(AM_CFLAGS) -I$(USEFUL) $(ARCHIVE_CFLAGS)
cti_dlaunch@CRAYTOOL_RELEASE_VERSION@_LDADD	= $(USEFUL)/libuseful.la $(ARCHIVE_LIBS)
cti_dlaunch@CRAYTOOL_RELEASE_VERSION@_LDFLAGS	= -all-static -Wl,--no-undefined -Wl,--as-needed $(ARCHIVE_LDFLAGS)

