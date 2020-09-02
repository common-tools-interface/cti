#
# Makefile.am for the BE daemon components of the common tools interface
#
# Copyright 2014-2020 Hewlett Packard Enterprise Development LP.
#

SRC                 = @COMMONTOOL_DIR@/src
INCLUDE				= @COMMONTOOL_DIR@/include
USEFUL              = $(SRC)/useful

AM_CFLAGS       	= -Wall
AM_LIBTOOLFLAGS 	= --quiet

libexec_PROGRAMS	= cti_be_daemon@COMMONTOOL_RELEASE_VERSION@

cti_be_daemon@COMMONTOOL_RELEASE_VERSION@_SOURCES	= cti_be_daemon.c pals_dl.c alps_dl.c slurm_dl.c generic_ssh_dl.c \																							cti_daemon.h
cti_be_daemon@COMMONTOOL_RELEASE_VERSION@_CFLAGS	= -I$(SRC) -I$(INCLUDE) -I$(USEFUL) \
													$(CDST_SUP_CFLAGS) $(CODE_COVERAGE_CFLAGS) $(AM_CFLAGS)
cti_be_daemon@COMMONTOOL_RELEASE_VERSION@_LDADD		= $(USEFUL)/libuseful.la $(CODE_COVERAGE_LIBS) $(ARCHIVE_LIBS) -ldl
cti_be_daemon@COMMONTOOL_RELEASE_VERSION@_LDFLAGS	= -all-static -Wl,--no-undefined \
													$(CDST_SUP_LIBS) $(AM_LDFLAGS)
cti_be_daemon@COMMONTOOL_RELEASE_VERSION@_CPPFLAGS 	= $(CODE_COVERAGE_CPPFLAGS) $(AM_CPPFLAGS)

if CODE_COVERAGE_ENABLED
clean-local: code-coverage-clean
distclean-local: code-coverage-dist-clean
endif
