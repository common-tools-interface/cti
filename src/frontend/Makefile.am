#
# Makefile.am for the frontend components of the common tools interface
#
# Copyright 2014-2019 Cray Inc. All Rights Reserved.
#

SUBDIRS				= checksum transfer frontend_impl mpir_iface daemon

SRC             	= @COMMONTOOL_DIR@/src
INCLUDE				= @COMMONTOOL_DIR@/include

AM_LIBTOOLFLAGS 	= --quiet
AM_CXXFLAGS			= -Wall -I$(SRC) -I. -I$(SRC)/frontend/frontend_impl -I$(INCLUDE)

@CODE_COVERAGE_RULES@

lib_LTLIBRARIES = libcommontools_fe.la

libcommontools_fe_la_SOURCES	=	Frontend.cpp cti_fe_iface.cpp
libcommontools_fe_la_CXXFLAGS	=	$(AM_CXXFLAGS) -fPIC $(DYNINST_CXXFLAGS) $(BOOST_CXXFLAGS) $(CODE_COVERAGE_CXXFLAGS)
libcommontools_fe_la_CPPFLAGS	=	$(DYNINST_CPPFLAGS) $(BOOST_CPPFLAGS) $(CODE_COVERAGE_CPPFLAGS)
libcommontools_fe_la_LIBADD		=	$(SRC)/useful/libuseful.la \
									daemon/libfe_daemon_iface.la \
									transfer/libtransfer.la \
									frontend_impl/libfrontend_impl.la \
									mpir_iface/libmpir_iface.la \
									checksum/libchecksum.la \
									-ldl -lrt -lstdc++ \
									$(ARCHIVE_LIBS) $(DYNINST_LIBS) \
									$(SSH2_LIBS) $(CODE_COVERAGE_LIBS)
libcommontools_fe_la_LDFLAGS	=	-shared -Wl,-z,origin -Wl,-rpath,\$$ORIGIN \
									-Wl,--enable-new-dtags -Wl,--no-undefined \
									-Wl,--as-needed -version-info $(COMMONTOOL_FE_VERSION) \
									$(ARCHIVE_LDFLAGS) $(DYNINST_LDFLAGS) $(SSH2_LDFLAGS) \
									$(CODE_COVERAGE_LDFLAGS)
libcommontools_fe_la_CFLAGS		=	$(CODE_COVERAGE_CFLAGS)
noinst_HEADERS					=	cti_fe_iface.hpp Frontend.hpp

clean-local: code-coverage-clean
distclean-local: code-coverage-dist-clean
