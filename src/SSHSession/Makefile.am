#
# Makefile.am for the SSH helper implementation.
#
# Copyright 2019-2023 Hewlett Packard Enterprise Development LP.
# SPDX-License-Identifier: Linux-OpenIB
#

SRC                 = @COMMONTOOL_DIR@/src
INCLUDE				= @COMMONTOOL_DIR@/include

AM_LIBTOOLFLAGS     = --quiet
AM_CXXFLAGS         = -Wall

noinst_LTLIBRARIES = libsshsession.la

libsshsession_la_SOURCES    = SSHSession.cpp
libsshsession_la_CXXFLAGS   = -I$(SRC) -I$(INCLUDE) -fPIC $(LIBSSH2_CFLAGS) \
							$(CODE_COVERAGE_CXXFLAGS) $(AM_CXXFLAGS)
libsshsession_la_CPPFLAGS	= $(CODE_COVERAGE_CPPFLAGS) $(AM_CPPFLAGS)
libsshsession_la_LDFLAGS	= -Wl,--no-undefined $(AM_LDFLAGS)
libsshsession_la_LIBADD		= $(LIBSSH2_LIBS) $(CODE_COVERAGE_LIBS)
noinst_HEADERS				= SSHSession.hpp

if CODE_COVERAGE_ENABLED
clean-local: code-coverage-clean
distclean-local: code-coverage-dist-clean
endif
