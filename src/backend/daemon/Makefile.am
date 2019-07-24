#
# Makefile.am for the BE daemon components of the common tools interface
#
# Copyright 2014-2019 Cray Inc. All Rights Reserved.
#

SRC                 = @COMMONTOOL_DIR@/src
INCLUDE				= @COMMONTOOL_DIR@/include
USEFUL              = $(SRC)/useful

AM_CFLAGS       	= -I$(SRC) -I$(INCLUDE)
AM_LIBTOOLFLAGS 	= --quiet

libexec_PROGRAMS	= cti_be_daemon@COMMONTOOL_RELEASE_VERSION@

cti_be_daemon@COMMONTOOL_RELEASE_VERSION@_SOURCES	=	cti_be_daemon.c slurm_dl.c generic_ssh_dl.c \
														cti_daemon.h
cti_be_daemon@COMMONTOOL_RELEASE_VERSION@_CFLAGS	=	$(AM_CFLAGS) -I$(USEFUL) $(ARCHIVE_CFLAGS)
cti_be_daemon@COMMONTOOL_RELEASE_VERSION@_LDADD		=	$(USEFUL)/libuseful.la $(ARCHIVE_LIBS)
cti_be_daemon@COMMONTOOL_RELEASE_VERSION@_LDFLAGS	=	-all-static -Wl,--no-undefined -Wl,--as-needed $(ARCHIVE_LDFLAGS)

