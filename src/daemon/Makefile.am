#
# Makefile.am for the dlaunch components of the craytool interface.
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
USEFUL              = $(SRC)/useful

AM_CFLAGS       	= -I$(SRC)
AM_LIBTOOLFLAGS 	= --quiet

libexec_PROGRAMS	= cti_dlaunch

cti_dlaunch_SOURCES	= cti_daemon.c cray_slurm_dl.c slurm_dl.c
cti_dlaunch_CFLAGS	= $(AM_CFLAGS) -I$(USEFUL) $(ARCHIVE_CFLAGS)
cti_dlaunch_LDADD	= $(USEFUL)/libuseful.la $(ARCHIVE_LIBS)
cti_dlaunch_LDFLAGS	= -all-static -Wl,--no-undefined -Wl,--as-needed $(ARCHIVE_LDFLAGS)

