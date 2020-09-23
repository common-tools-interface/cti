#
# Makefile.am for the frontend components of the common tools interface
#
# Copyright 2014-2020 Hewlett Packard Enterprise Development LP.
#

SUBDIRS				= checksum transfer frontend_impl mpir_iface daemon

SRC             	= @COMMONTOOL_DIR@/src
INCLUDE				= @COMMONTOOL_DIR@/include

AM_LIBTOOLFLAGS 	= --quiet
AM_CXXFLAGS			= -Wall

lib_LTLIBRARIES = libcommontools_fe.la

libcommontools_fe_la_SOURCES	= Frontend.cpp cti_fe_iface.cpp
libcommontools_fe_la_CXXFLAGS	= -I$(SRC) -I. -I$(SRC)/frontend/frontend_impl -I$(INCLUDE) -fPIC \
								$(CODE_COVERAGE_CXXFLAGS) $(CDST_SUP_CFLAGS) $(AM_CXXFLAGS)
libcommontools_fe_la_CPPFLAGS	= $(CODE_COVERAGE_CPPFLAGS) $(AM_CPPFLAGS)
libcommontools_fe_la_LIBADD		= $(SRC)/useful/libuseful.la \
								daemon/libfe_daemon_iface.la \
								transfer/libtransfer.la \
								frontend_impl/libfrontend_impl.la \
								mpir_iface/libmpir_iface.la \
								checksum/libchecksum.la \
								-ldl -lrt -lstdc++ \
								$(CODE_COVERAGE_LIBS)
libcommontools_fe_la_LDFLAGS	= -shared -Wl,--no-undefined -Wl,--as-needed \
								-version-info $(COMMONTOOL_FE_VERSION) \
								$(CDST_SUP_LIBS) $(AM_LDFLAGS) -lssl -lcrypto
noinst_HEADERS					= cti_fe_iface.hpp Frontend.hpp

if CODE_COVERAGE_ENABLED
clean-local: code-coverage-clean
distclean-local: code-coverage-dist-clean
endif
