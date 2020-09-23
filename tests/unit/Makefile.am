#
# Makefile.am for the CTI unit tests
#
# Copyright 2019-2020 Hewlett Packard Enterprise Development LP.
#

SRC             = @COMMONTOOL_DIR@/src
SUPPORT         = @COMMONTOOL_DIR@/tests/test_support
MOCK            = @COMMONTOOL_DIR@/tests/mock
EXTERNAL		= @COMMONTOOL_DIR@/include

GTEST           = $(SUPPORT)/googletest/googletest
GMOCK           = $(SUPPORT)/googletest/googlemock

AM_LIBTOOLFLAGS = --quiet
AUTOMAKE_OPTIONS = subdir-objects

# specify test binaries
TESTS = unit_tests websocket_tests
check_PROGRAMS = unit_tests websocket_tests

# unit test binary
unit_tests_SOURCES	=	unit_tests.cpp cti_fe_unit_test.cpp cti_archive_unit_test.cpp \
						cti_manifest_unit_test.cpp cti_session_unit_test.cpp \
						cti_useful_unit_test.cpp cti_be_unit_test.cpp
unit_tests_CPPFLAGS	=	$(CODE_COVERAGE_CPPFLAGS)
unit_tests_CFLAGS	=	$(AM_CFLAGS) -fPIC -I$(SRC) -I$(EXTERNAL) \
						-I$(GTEST)/include -I$(GTEST) \
						-I$(GMOCK)/include -I$(GMOCK) \
						$(CDST_SUP_CFLAGS) $(CODE_COVERAGE_CFLAGS) \
						-I$(MOCK) \
						-DINSTALL_PATH=\"$(prefix)\"
unit_tests_CXXFLAGS	=	$(AM_CXXFLAGS) $(unit_tests_CFLAGS) \
						$(CODE_COVERAGE_CXXFLAGS)
unit_tests_LDADD	=	$(SRC)/frontend/libcommontools_fe.la \
						$(SRC)/backend/libcommontools_be.la \
						$(SUPPORT)/libgtest.la \
						$(SUPPORT)/libgmock.la \
						$(MOCK)/libmock.la \
						$(ARCHIVE_LIBS) \
						-ldl -lrt -lstdc++ \
        				$(CODE_COVERAGE_LIBS)
unit_tests_LDFLAGS	=	-pthread -Wl,-rpath,$(prefix)/lib \
						-Wl,--enable-new-dtags -Wl,--no-undefined \
						-Wl,--as-needed $(CDST_SUP_LIBS)

noinst_HEADERS		=	cti_archive_unit_test.hpp cti_fe_unit_test.hpp \
						cti_manifest_unit_test.hpp cti_session_unit_test.hpp \
						cti_useful_unit_test.hpp cti_be_unit_test.hpp

websocket_tests_SOURCES  = cti_websocket_unit_test.cpp
websocket_tests_CPPFLAGS = $(CODE_COVERAGE_CPPFLAGS)
websocket_tests_CFLAGS   = $(AM_CFLAGS) -fPIC -I$(SRC) -I$(EXTERNAL) \
	-I$(GTEST)/include -I$(GTEST) \
	-I$(GMOCK)/include -I$(GMOCK) \
	$(CDST_SUP_CFLAGS) $(CODE_COVERAGE_CFLAGS) \
	-I$(MOCK) \
	-DINSTALL_PATH=\"$(prefix)\"

websocket_tests_CXXFLAGS = $(AM_CXXFLAGS) $(unit_tests_CFLAGS) \
	$(CODE_COVERAGE_CXXFLAGS)

websocket_tests_LDADD = $(SUPPORT)/libgtest.la $(SUPPORT)/libgmock.la \
	$(MOCK)/libmock.la $(ARCHIVE_LIBS) \
	-ldl -lrt -lstdc++ -lcrypto -lssl \
        $(CODE_COVERAGE_LIBS)

websocket_tests_LDFLAGS	= -pthread -Wl,-rpath,$(prefix)/lib \
	-Wl,--enable-new-dtags -Wl,--no-undefined \
	-Wl,--as-needed $(CDST_SUP_LIBS)

if CODE_COVERAGE_ENABLED
clean-local: code-coverage-clean
distclean-local: code-coverage-dist-clean
endif
