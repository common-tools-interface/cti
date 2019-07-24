#
# Makefile.am for the backend daemon components of the craytool interface.
#
# Copyright 2014-2019 Cray Inc.  All Rights Reserved.
#

SRC                 = @CRAYTOOL_DIR@/src
INCLUDE				= @CRAYTOOL_DIR@/include
USEFUL              = $(SRC)/useful

AM_CFLAGS       	= -I$(SRC) -I$(INCLUDE)
AM_LIBTOOLFLAGS 	= --quiet

@CODE_COVERAGE_RULES@

libexec_PROGRAMS	= cti_be_daemon@CRAYTOOL_RELEASE_VERSION@

cti_be_daemon@CRAYTOOL_RELEASE_VERSION@_SOURCES	= cti_be_daemon.c cray_slurm_dl.c slurm_dl.c
cti_be_daemon@CRAYTOOL_RELEASE_VERSION@_CFLAGS	= $(AM_CFLAGS) -I$(USEFUL) $(ARCHIVE_CFLAGS) $(CODE_COVERAGE_CFLAGS)
cti_be_daemon@CRAYTOOL_RELEASE_VERSION@_LDADD	= $(USEFUL)/libuseful.la $(ARCHIVE_LIBS) $(CODE_COVERAGE_LIBS)
cti_be_daemon@CRAYTOOL_RELEASE_VERSION@_LDFLAGS	= -all-static -Wl,--no-undefined -Wl,--as-needed $(ARCHIVE_LDFLAGS) $(CODE_COVERAGE_LDFLAGS)
cti_be_daemon@CRAYTOOL_RELEASE_VERSION@_CPPFLAGS = $(CODE_COVERAGE_CPPFLAGS)
cti_be_daemon@CRAYTOOL_RELEASE_VERSION@_CXXFLAGS = $(CODE_COVERAGE_CXXFLAGS)
