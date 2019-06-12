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
SUBDIRS				= ld_val

SRC             = @CRAYTOOL_DIR@/src
AM_LIBTOOLFLAGS = --quiet
AM_CFLAGS		= -Wall -I$(SRC) -I.

noinst_LTLIBRARIES      = libuseful.la

libuseful_la_SOURCES    = 	cti_log.c cti_path.c cti_stack.c ArgvDefs-c++11-workaround.cpp
libuseful_la_CFLAGS     = 	$(AM_CFLAGS) -fPIC
libuseful_la_CXXFLAGS   = 	$(AM_CXXFLAGS) $(libuseful_la_CFLAGS)
libuseful_la_LIBADD		=	ld_val/libld_val.la
libuseful_la_LDFLAGS    = 	-static -Wl,--no-undefined

noinst_HEADERS			= 	cti_argv.hpp cti_log.h cti_path.h cti_split.hpp \
							cti_stack.h cti_wrappers.hpp cti_dlopen.hpp \
							cti_execvp.hpp cti_useful.h
