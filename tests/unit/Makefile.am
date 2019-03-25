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
GMOCK           = @CRAYTOOL_DIR@/tests/googletest/googlemock

AM_LIBTOOLFLAGS = --quiet
AUTOMAKE_OPTIONS = subdir-objects

# google test framework library
noinst_LTLIBRARIES   = libgtest.la
libgtest_la_SOURCES  = ../googletest/googletest/src/gtest-all.cc
libgtest_la_CPPFLAGS = -I$(GTEST)/include -I$(GTEST)
libgtest_la_LDFLAGS  = -pthread

# google mock framework library
noinst_LTLIBRARIES  += libgmock.la
libgmock_la_SOURCES  = ../googletest/googlemock/src/gmock-all.cc
libgmock_la_CPPFLAGS = -I$(GMOCK)/include -I$(GMOCK) -I$(GTEST)/include -I$(GTEST)
libgmock_la_LDFLAGS  = -pthread

# specify test binaries
TESTS = unit_tests
noinst_PROGRAMS = unit_tests function_tests
check_PROGRAMS = unit_tests

# unit test binary
unit_tests_SOURCES  = unit_tests.cpp cti_fe_unit_test.cpp
unit_tests_CFLAGS   = $(AM_CFLAGS) -fPIC -I$(SRC) -I$(GTEST)/include -I$(GTEST) \
	-I$(GMOCK)/include $(ARCHIVE_CFLAGS) $(WLM_DETECT_CFLAGS) $(DYNINST_CFLAGS)
unit_tests_CXXFLAGS = $(AM_CXXFLAGS) $(unit_tests_CFLAGS)
unit_tests_LDADD    = $(SRC)/frontend/libcraytools_fe.la libgtest.la libgmock.la -ldl -lrt -lstdc++
unit_tests_LDFLAGS  =	-pthread -Wl,-z,origin -Wl,-rpath,\$$ORIGIN -Wl,--enable-new-dtags \
	-Wl,--no-undefined -Wl,--as-needed -version-info \
	$(CRAYTOOL_FE_VERSION) $(ARCHIVE_LDFLAGS) $(DYNINST_LDFLAGS)

# function test binary
function_tests_SOURCES  = function_tests.cpp cti_fe_function_test.cpp
function_tests_CFLAGS   = $(AM_CFLAGS) -fPIC -I$(SRC) -I$(GTEST)/include -I$(GTEST) \
	$(ARCHIVE_CFLAGS) $(WLM_DETECT_CFLAGS) $(DYNINST_CFLAGS)
function_tests_CXXFLAGS = $(AM_CXXFLAGS) $(unit_tests_CFLAGS)
function_tests_LDADD    = $(SRC)/frontend/libcraytools_fe.la libgtest.la -ldl -lrt -lstdc++
function_tests_LDFLAGS  =	-pthread -Wl,-z,origin -Wl,-rpath,\$$ORIGIN -Wl,--enable-new-dtags \
	-Wl,--no-undefined -Wl,--as-needed -version-info \
	$(CRAYTOOL_FE_VERSION) $(ARCHIVE_LDFLAGS) $(DYNINST_LDFLAGS)

# support binaries for function tests

# stage test helpers (one_printer, two_printer, libprint.so)
noinst_PROGRAMS += stage_test/one_printer stage_test/two_printer
noinst_LTLIBRARIES += stage_test/print_one/libprint.la stage_test/print_two/libprint.la

stage_test_print_one_libprint_la_SOURCES = stage_test/print_one/print.c
stage_test_print_one_libprint_la_LDFLAGS = -version-info 0:0:0

stage_test_one_printer_SOURCES = stage_test/one_printer.c
stage_test_one_printer_LDADD   = -L./stage_test/print_one -lprint
stage_test_one_printer_LDFLAGS = -Wl,-rpath,\$$ORIGIN/print_one

stage_test_print_two_libprint_la_SOURCES = stage_test/print_two/print.c
stage_test_print_two_libprint_la_LDFLAGS = -version-info 0:0:0

stage_test_two_printer_SOURCES = stage_test/two_printer.c
stage_test_two_printer_LDADD   = -L./stage_test/print_two -lprint
stage_test_two_printer_LDFLAGS = -Wl,-rpath,\$$ORIGIN/print_two
