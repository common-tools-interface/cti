#
# Makefile.am for the useful components of the craytool interface.
#
# Copyright 2014-2019 Cray Inc.  All Rights Reserved.
#
# Unpublished Proprietary Information.
# This unpublished work is protected to trade secret, copyright and other laws.
# Except as permitted by contract or express written permission of Cray Inc.,
# no part of this work or its content may be used, reproduced or disclosed
# in any form.
#

SRC             = @CRAYTOOL_DIR@/src
AM_LIBTOOLFLAGS = --quiet
AM_CFLAGS		= -Wall -I$(SRC)

noinst_LTLIBRARIES      = libuseful.la

libuseful_la_SOURCES    = 	cti_error.c cti_log.c cti_overwatch.c \
							cti_path.c cti_stack.c ArgvDefs-c++11-workaround.cpp
libuseful_la_CFLAGS     = 	$(AM_CFLAGS) -fPIC
libuseful_la_CXXFLAGS   = 	$(AM_CXXFLAGS) $(libuseful_la_CFLAGS)
libuseful_la_LDFLAGS    = 	-static -Wl,--no-undefined

libexec_PROGRAMS		= 	cti_overwatch

cti_overwatch_SOURCES	= 	cti_overwatch_process.c
cti_overwatch_CFLAGS	= 	$(AM_CFLAGS)
cti_overwatch_LDFLAGS	= 	-Wl,--no-undefined -Wl,--as-needed

noinst_HEADERS			= 	cti_argv.hpp cti_error.h cti_log.h cti_overwatch.h \
							cti_path.h cti_split.hpp cti_stack.h cti_wrappers.hpp \
							Dlopen.hpp ExecvpOutput.hpp cti_useful.h
