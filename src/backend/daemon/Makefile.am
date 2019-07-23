#
# Makefile.am for the backend daemon components of the craytool interface.
#
# Copyright 2014-2019 Cray Inc. All Rights Reserved.
#

SRC                 = @CRAYTOOL_DIR@/src
INCLUDE				= @CRAYTOOL_DIR@/include
USEFUL              = $(SRC)/useful

AM_CFLAGS       	= -I$(SRC) -I$(INCLUDE)
AM_LIBTOOLFLAGS 	= --quiet

libexec_PROGRAMS	= cti_be_daemon@CRAYTOOL_RELEASE_VERSION@

cti_be_daemon@CRAYTOOL_RELEASE_VERSION@_SOURCES	= cti_be_daemon.c slurm_dl.c generic_ssh_dl.c
cti_be_daemon@CRAYTOOL_RELEASE_VERSION@_CFLAGS	= $(AM_CFLAGS) -I$(USEFUL) $(ARCHIVE_CFLAGS)
cti_be_daemon@CRAYTOOL_RELEASE_VERSION@_LDADD	= $(USEFUL)/libuseful.la $(ARCHIVE_LIBS)
cti_be_daemon@CRAYTOOL_RELEASE_VERSION@_LDFLAGS	= -all-static -Wl,--no-undefined -Wl,--as-needed $(ARCHIVE_LDFLAGS)

