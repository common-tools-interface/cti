#
# Makefile.am for the Slurm frontend implementation.
#
# Copyright 2019 Cray Inc. All Rights Reserved.
#

SRC             	= @COMMONTOOL_DIR@/src
INCLUDE				= @COMMONTOOL_DIR@/include

@CODE_COVERAGE_RULES@

AM_LIBTOOLFLAGS 	= --quiet
AM_CXXFLAGS			= -Wall -I$(SRC) -I$(SRC)/frontend -I$(SRC)/frontend/frontend_impl -I$(INCLUDE)

noinst_LTLIBRARIES = libslurm.la

libslurm_la_SOURCES		=	Frontend.cpp
libslurm_la_CXXFLAGS	=	$(AM_CXXFLAGS) -fPIC $(CODE_COVERAGE_CXXFLAGS)
libslurm_la_CPPFLAGS	=	$(CODE_COVERAGE_CPPFLAGS)
libslurm_la_CFLAGS		=	$(CODE_COVERAGE_CFLAGS)
libslurm_la_LDFLAGS		=	-static -Wl,--no-undefined $(CODE_COVERAGE_LDFLAGS)
libslurm_la_LIBADD		=	$(CODE_COVERAGE_LIBS)
noinst_HEADERS			=	Frontend.hpp
