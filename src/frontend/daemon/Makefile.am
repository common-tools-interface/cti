#
# Makefile.am for the CTI frontend daemon
#
# Copyright 2019 Cray Inc. All Rights Reserved.
#

SRC             = @COMMONTOOL_DIR@/src
INCLUDE			= @COMMONTOOL_DIR@/include

AM_LIBTOOLFLAGS = --quiet
AM_CXXFLAGS		= -Wall

noinst_LTLIBRARIES = libfe_daemon_iface.la

libfe_daemon_iface_la_SOURCES	= cti_fe_daemon_iface.cpp
libfe_daemon_iface_la_CXXFLAGS	= -I$(SRC) -I. -I$(INCLUDE) -fPIC \
								$(CDST_SUP_CFLAGS) $(CODE_COVERAGE_CXXFLAGS) \
								$(AM_CXXFLAGS)
libfe_daemon_iface_la_LDFLAGS	= -Wl,--no-undefined \
								$(CDST_SUP_LIBS) $(AM_LDFLAGS)
libfe_daemon_iface_la_CPPFLAGS	= $(CODE_COVERAGE_CPPFLAGS) $(AM_CPPFLAGS)
libfe_daemon_iface_la_LIBADD	= $(CODE_COVERAGE_LIBS)
noinst_HEADERS					= cti_fe_daemon_iface.hpp

libexec_PROGRAMS = cti_fe_daemon@COMMONTOOL_RELEASE_VERSION@

cti_fe_daemon@COMMONTOOL_RELEASE_VERSION@_SOURCES	= 	cti_fe_daemon.cpp
cti_fe_daemon@COMMONTOOL_RELEASE_VERSION@_CPPFLAGS	=	$(CODE_COVERAGE_CPPFLAGS) $(AM_CPPFLAGS)
cti_fe_daemon@COMMONTOOL_RELEASE_VERSION@_CXXFLAGS	=	-I$(SRC) -I. -I$(INCLUDE) \
														$(CDST_SUP_CFLAGS) \
														$(CODE_COVERAGE_CXXFLAGS) \
														$(AM_CXXFLAGS)
cti_fe_daemon@COMMONTOOL_RELEASE_VERSION@_LDFLAGS	=	-Wl,--no-undefined -Wl,--as-needed \
														-pthread $(CDST_SUP_LIBS)
cti_fe_daemon@COMMONTOOL_RELEASE_VERSION@_LDADD		=	libfe_daemon_iface.la ../mpir_iface/libmpir_iface.la \
														$(CODE_COVERAGE_LIBS)

if CODE_COVERAGE_ENABLED
clean-local: code-coverage-clean
distclean-local: code-coverage-dist-clean
endif

