#
# Makefile.am for the CTI unit tests
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
SUPPORT         = @CRAYTOOL_DIR@/tests/test_support

GTEST           = $(SUPPORT)/googletest/googletest
GMOCK           = $(SUPPORT)/googletest/googlemock

AM_LIBTOOLFLAGS = --quiet
AUTOMAKE_OPTIONS = subdir-objects

# google test framework library
check_LTLIBRARIES   = libgtest.la
libgtest_la_SOURCES  = googletest/googletest/src/gtest-all.cc
libgtest_la_CPPFLAGS = -I$(GTEST)/include -I$(GTEST)
libgtest_la_LDFLAGS  = -pthread

# google mock framework library
check_LTLIBRARIES  += libgmock.la
libgmock_la_SOURCES  = googletest/googlemock/src/gmock-all.cc
libgmock_la_CPPFLAGS = -I$(GTEST)/include -I$(GTEST) \
	 -I$(GMOCK)/include -I$(GMOCK)
libgmock_la_LDFLAGS  = -pthread

# support binaries for function and unit tests

# stage test helpers (one_printer, two_printer, libprint.so)
check_PROGRAMS = one_printer two_printer one_socket two_socket

print_one/libprint.so: print_one/print.c
	cc -fPIC -shared print_one/print.c -o print_one/libprint.so

one_printer_DEPENDENCIES = print_one/libprint.so
one_printer_SOURCES = one_printer.c
one_printer_LDADD   = -L./print_one -lprint
one_printer_LDFLAGS = -Wl,-rpath,\$$ORIGIN/print_one

clean-local:
	rm -f print_one/libprint.so
	rm -f print_two/libprint.so

print_two/libprint.so: print_two/print.c
	cc -fPIC -shared print_two/print.c -o print_two/libprint.so

two_printer_DEPENDENCIES = print_two/libprint.so
two_printer_SOURCES = two_printer.c
two_printer_LDADD   = -L./print_two -lprint
two_printer_LDFLAGS = -Wl,-rpath,\$$ORIGIN/print_two

one_socket_SOURCES = one_socket.c

two_socket_SOURCES = two_socket.c
