#
# Makefile.am for the BE daemon components of the common tools interface
#
# Copyright 2014-2023 Hewlett Packard Enterprise Development LP.
# SPDX-License-Identifier: Linux-OpenIB
#

SRC                 = @COMMONTOOL_DIR@/src
INCLUDE				= @COMMONTOOL_DIR@/include
USEFUL              = $(SRC)/useful

AM_CFLAGS       	= -Wall
AM_LIBTOOLFLAGS 	= --quiet

libexec_PROGRAMS	= cti_be_daemon@COMMONTOOL_RELEASE_VERSION@

cti_be_daemon@COMMONTOOL_RELEASE_VERSION@_SOURCES	= cti_be_daemon.c slurm_dl.c generic_ssh_dl.c localhost_dl.c cti_daemon.h 
cti_be_daemon@COMMONTOOL_RELEASE_VERSION@_CFLAGS	= -I$(SRC) -I$(INCLUDE) -I$(USEFUL) $(CDST_SUP_CFLAGS) $(CODE_COVERAGE_CFLAGS) $(AM_CFLAGS)

if ENABLE_ALPS
cti_be_daemon@COMMONTOOL_RELEASE_VERSION@_SOURCES += alps_dl.c
endif
if ENABLE_PALS
cti_be_daemon@COMMONTOOL_RELEASE_VERSION@_SOURCES += pals_dl.c
endif
if ENABLE_FLUX
cti_be_daemon@COMMONTOOL_RELEASE_VERSION@_SOURCES += flux_dl.c
endif

cti_be_daemon@COMMONTOOL_RELEASE_VERSION@_LDADD		= $(USEFUL)/libuseful.la $(CODE_COVERAGE_LIBS) ${CRAY_CDST_SUPPORT_INSTALL_DIR}/lib/libarchive.a -ldl
cti_be_daemon@COMMONTOOL_RELEASE_VERSION@_LDFLAGS	= -all-static -Wl,--no-undefined $(CDST_SUP_LIBS) $(AM_LDFLAGS)
cti_be_daemon@COMMONTOOL_RELEASE_VERSION@_CPPFLAGS 	= $(CODE_COVERAGE_CPPFLAGS) $(AM_CPPFLAGS)

if CODE_COVERAGE_ENABLED
clean-local: code-coverage-clean
distclean-local: code-coverage-dist-clean
endif
