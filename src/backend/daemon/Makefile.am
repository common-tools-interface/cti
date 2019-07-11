#
# Makefile.am for the backend daemon components of the craytool interface.
#
# Copyright 2014-2019 Cray Inc.  All Rights Reserved.
#
# Unpublished Proprietary Information.
# This unpublished work is protected to trade secret, copyright and other laws.
# Except as permitted by contract or express written permission of Cray Inc.,
# no part of this work or its content may be used, reproduced or disclosed
# in any form.
#

SRC                 = @CRAYTOOL_DIR@/src
INCLUDE				= @CRAYTOOL_DIR@/include
USEFUL              = $(SRC)/useful

AM_CFLAGS       	= -I$(SRC) -I$(INCLUDE)
AM_LIBTOOLFLAGS 	= --quiet

libexec_PROGRAMS	= cti_be_daemon@CRAYTOOL_RELEASE_VERSION@

cti_be_daemon@CRAYTOOL_RELEASE_VERSION@_SOURCES	= cti_be_daemon.c cray_slurm_dl.c slurm_dl.c
cti_be_daemon@CRAYTOOL_RELEASE_VERSION@_CFLAGS	= $(AM_CFLAGS) -I$(USEFUL) $(ARCHIVE_CFLAGS)
cti_be_daemon@CRAYTOOL_RELEASE_VERSION@_LDADD	= $(USEFUL)/libuseful.la $(ARCHIVE_LIBS)
cti_be_daemon@CRAYTOOL_RELEASE_VERSION@_LDFLAGS	= -all-static -Wl,--no-undefined -Wl,--as-needed $(ARCHIVE_LDFLAGS)

