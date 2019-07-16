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

SUBDIRS				= cti_transfer frontend_impl mpir_iface daemon

SRC             	= @CRAYTOOL_DIR@/src
INCLUDE				= @CRAYTOOL_DIR@/include

AM_LIBTOOLFLAGS 	= --quiet
AM_CXXFLAGS			= -Wall -I$(SRC) -I. -I$(SRC)/frontend/frontend_impl -I$(INCLUDE)

@CODE_COVERAGE_RULES@

lib_LTLIBRARIES = libcraytools_fe.la

libcraytools_fe_la_SOURCES	=	Frontend.cpp cti_fe_iface.cpp
libcraytools_fe_la_CXXFLAGS	=	$(AM_CXXFLAGS) -fPIC $(DYNINST_CXXFLAGS) $(BOOST_CXXFLAGS) $(CODE_COVERAGE_CXXFLAGS)
libcraytools_fe_la_CPPFLAGS	=	$(DYNINST_CPPFLAGS) $(BOOST_CPPFLAGS) $(CODE_COVERAGE_CPPFLAGS)
libcraytools_fe_la_LIBADD	=	$(SRC)/useful/libuseful.la \
								daemon/libfe_daemon_iface.la \
								cti_transfer/libcti_transfer.la \
								frontend_impl/libfrontend_impl.la \
								mpir_iface/libmpir_iface.la \
								-ldl -lrt -lstdc++ $(ARCHIVE_LIBS) $(DYNINST_LIBS) $(SSH2_LIBS) $(CODE_COVERAGE_LIBS)
libcraytools_fe_la_LDFLAGS	=	-shared -Wl,-z,origin -Wl,-rpath,\$$ORIGIN -Wl,--enable-new-dtags -Wl,--no-undefined \
								-Wl,--as-needed -version-info $(CRAYTOOL_FE_VERSION) \
								$(ARCHIVE_LDFLAGS) $(DYNINST_LDFLAGS) $(SSH2_LDFLAGS) $(CODE_COVERAGE_LDFLAGS)
libcraytools_fe_la_CFLAGS	=	$(CODE_COVERAGE_CFLAGS)
noinst_HEADERS				=	cti_fe_iface.hpp Frontend.hpp

clean-local: code-coverage-clean
distclean-local: code-coverage-dist-clean
