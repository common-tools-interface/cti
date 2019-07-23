#
# Makefile.am for the MPIR interface components of the craytool interface.
#
# Copyright 2018-2019 Cray Inc.  All Rights Reserved.
#

SRC             = @CRAYTOOL_DIR@/src
INCLUDE			= @CRAYTOOL_DIR@/include

@CODE_COVERAGE_RULES@

AM_CXXFLAGS		= -Wall -I$(SRC) -I$(INCLUDE)
AM_LIBTOOLFLAGS = --quiet

noinst_LTLIBRARIES      	=	libmpir_iface.la

libmpir_iface_la_SOURCES	=	MPIRInstance.cpp Inferior.cpp
libmpir_iface_la_CXXFLAGS	= 	$(AM_CXXFLAGS) -fPIC $(DYNINST_CXXFLAGS) $(BOOST_CXXFLAGS) $(CODE_COVERAGE_CXXFLAGS)
libmpir_iface_la_CPPFLAGS	=	$(DYNINST_CPPFLAGS) $(BOOST_CPPFLAGS) $(CODE_COVERAGE_CPPFLAGS)
libmpir_iface_la_LDFLAGS	=	-static -Wl,--no-undefined $(CODE_COVERAGE_CODE_COVERAGE_LDFLAGS)
libmpir_iface_la_LIBADD		=	$(CODE_COVERAGE_LIBS)
libmpir_iface_la_CFLAGS		=	$(CODE_COVERAGE_CFLAGS)
noinst_HEADERS				=	Inferior.hpp MPIRInstance.hpp
