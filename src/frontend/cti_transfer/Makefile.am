#
# Makefile.am for the CTI transfer components of the craytool interface.
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

@CODE_COVERAGE_RULES@

AM_CXXFLAGS		= -Wall -I$(SRC) -I$(src)/frontend 
AM_LIBTOOLFLAGS = --quiet
noinst_LTLIBRARIES			=	libcti_transfer.la

libcti_transfer_la_SOURCES	=	Archive.cpp Manifest.cpp Session.cpp
libcti_transfer_la_CXXFLAGS	= 	$(AM_CXXFLAGS) -fPIC $(ARCHIVE_CFLAGS) $(CODE_COVERAGE_CXXFLAGS)
libcti_transfer_la_LDFLAGS	=	-static -Wl,--no-undefined $(CODE_COVERAGE_LDFLAGS)
libcti_transfer_la_CFLAGS	=	$(CODE_COVERAGE_CFLAGS)
libcti_transfer_la_CPPFLAGS	=	$(CODE_COVERAGE_CPPFLAGS)
libcti_transfer_la_LIBADD	=	$(CODE_COVERAGE_LIBS)
noinst_HEADERS				=	Archive.hpp Manifest.hpp Session.hpp

clean-local: code-coverage-clean
distclean-local: code-coverage-dist-clean
