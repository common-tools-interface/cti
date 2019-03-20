#
# Makefile.am for the frontend components of the craytool interface.
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
GTEST           = @CRAYTOOL_DIR@/tests/googletest/googletest
AM_LIBTOOLFLAGS = --quiet
AUTOMAKE_OPTIONS = subdir-objects

noinst_LTLIBRARIES   = libgtest.la
libgtest_la_SOURCES  = ../googletest/googletest/src/gtest-all.cc
libgtest_la_CPPFLAGS = -I$(GTEST)/include -I$(GTEST)
libgtest_la_LDFLAGS  = -pthread

TESTS = unit_tests
noinst_PROGRAMS = unit_tests
check_PROGRAMS = unit_tests

unit_tests_SOURCES  = unit_tests.cpp cti_fe_iface_test.cpp
unit_tests_CFLAGS   = $(AM_CFLAGS) -fPIC -I$(SRC) -I$(GTEST)/include -I$(GTEST) \
	$(ARCHIVE_CFLAGS) $(WLM_DETECT_CFLAGS) $(DYNINST_CFLAGS)
unit_tests_CXXFLAGS = $(AM_CXXFLAGS) $(unit_tests_CFLAGS)
unit_tests_LDADD    = $(SRC)/frontend/libcraytools_fe.la libgtest.la -ldl -lrt -lstdc++
unit_tests_LDFLAGS  =	-pthread -Wl,-z,origin -Wl,-rpath,\$$ORIGIN -Wl,--enable-new-dtags \
	-Wl,--no-undefined -Wl,--as-needed -version-info \
	$(CRAYTOOL_FE_VERSION) $(ARCHIVE_LDFLAGS) $(DYNINST_LDFLAGS)
