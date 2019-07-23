#
# Makefile.am for the Cray Slurm frontend implementation.
#
# Copyright 2019 Cray Inc.  All Rights Reserved.
#

SRC             	= @CRAYTOOL_DIR@/src
INCLUDE				= @CRAYTOOL_DIR@/include

@CODE_COVERAGE_RULES@

AM_LIBTOOLFLAGS 	= --quiet
AM_CXXFLAGS			= -Wall -I$(SRC) -I$(SRC)/frontend -I$(SRC)/frontend/frontend_impl -I$(INCLUDE)

noinst_LTLIBRARIES = libcrayslurm.la

libcrayslurm_la_SOURCES		=	Frontend.cpp
libcrayslurm_la_CXXFLAGS	=	$(AM_CXXFLAGS) -fPIC $(CODE_COVERAGE_CXXFLAGS)
libcrayslurm_la_CPPFLAGS	=	$(CODE_COVERAGE_CPPFLAGS)
libcrayslurm_la_CFLAGS		=	$(CODE_COVERAGE_CFLAGS)
libcrayslurm_la_LDFLAGS		=	-static -Wl,--no-undefined $(CODE_COVERAGE_LDFLAGS)
libcrayslurm_la_LIBADD		=	$(CODE_COVERAGE_LIBS)
noinst_HEADERS				=	Frontend.hpp
