#
# Makefile.am for the CTI transfer components of the craytool interface.
#
# Copyright 2014-2019 Cray Inc. All Rights Reserved.
#

@CODE_COVERAGE_RULES@

SRC             = @CRAYTOOL_DIR@/src
INCLUDE			= @CRAYTOOL_DIR@/include

AM_CXXFLAGS		= -Wall -I$(SRC) -I$(SRC)/frontend -I$(INCLUDE)
AM_LIBTOOLFLAGS = --quiet

noinst_LTLIBRARIES		=	libtransfer.la

libtransfer_la_SOURCES	=	Archive.cpp Manifest.cpp Session.cpp
libtransfer_la_CXXFLAGS	= 	$(AM_CXXFLAGS) -fPIC $(ARCHIVE_CFLAGS) $(CODE_COVERAGE_CXXFLAGS)
libtransfer_la_LDFLAGS	=	-static -Wl,--no-undefined $(CODE_COVERAGE_LDFLAGS)
libtransfer_la_CFLAGS	=	$(CODE_COVERAGE_CFLAGS)
libtransfer_la_CPPFLAGS	=	$(CODE_COVERAGE_CPPFLAGS)
libtransfer_la_LIBADD	=	$(CODE_COVERAGE_LIBS)
noinst_HEADERS			=	Archive.hpp Manifest.hpp Session.hpp

clean-local: code-coverage-clean
distclean-local: code-coverage-dist-clean
