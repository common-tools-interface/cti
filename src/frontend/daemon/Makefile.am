#
# Makefile.am for the CTI frontend daemon
#
# Copyright 2019 Cray Inc. All Rights Reserved.
#

@CODE_COVERAGE_RULES@

SRC             = @CRAYTOOL_DIR@/src
INCLUDE			= @CRAYTOOL_DIR@/include

AM_LIBTOOLFLAGS = --quiet
AM_CFLAGS		= -Wall -I$(SRC) -I. -I$(INCLUDE)

noinst_LTLIBRARIES      = libfe_daemon_iface.la

libfe_daemon_iface_la_SOURCES  = cti_fe_daemon_iface.cpp
libfe_daemon_iface_la_CFLAGS   = $(AM_CFLAGS) -fPIC $(CODE_COVERAGE_CFLAGS)
libfe_daemon_iface_la_CXXFLAGS = $(AM_CXXFLAGS) $(libfe_daemon_iface_la_CFLAGS) $(CODE_COVERAGE_CXXFLAGS)
libfe_daemon_iface_la_LDFLAGS  = -static -Wl,--no-undefined $(CODE_COVERAGE_LDFLAGS)
libfe_daemon_iface_la_CPPFLAGS	= $(CODE_COVERAGE_CPPFLAGS)
libfe_daemon_iface_la_LIBADD	= $(CODE_COVERAGE_LIBS)

libexec_PROGRAMS		= 	cti_fe_daemon@CRAYTOOL_RELEASE_VERSION@

cti_fe_daemon@CRAYTOOL_RELEASE_VERSION@_SOURCES  = cti_fe_daemon.cpp
cti_fe_daemon@CRAYTOOL_RELEASE_VERSION@_CPPFLAGS = $(DYNINST_CPPFLAGS) $(BOOST_CPPFLAGS) $(CODE_COVERAGE_CPPFLAGS)
cti_fe_daemon@CRAYTOOL_RELEASE_VERSION@_CXXFLAGS = $(AM_CFLAGS) $(AM_CXXFLAGS) $(DYNINST_CXXFLAGS) $(BOOST_CXXFLAGS) $(CODE_COVERAGE_CXXFLAGS)
cti_fe_daemon@CRAYTOOL_RELEASE_VERSION@_LDFLAGS  = -Wl,--no-undefined -Wl,--as-needed -Wl,-rpath,\$$ORIGIN/../lib -pthread $(DYNINST_LDFLAGS) $(BOOST_LDFLAGS) $(CODE_COVERAGE_LDFLAGS)
cti_fe_daemon@CRAYTOOL_RELEASE_VERSION@_LDADD    = libfe_daemon_iface.la ../mpir_iface/libmpir_iface.la $(DYNINST_LIBS) $(BOOST_LIBS) $(CODE_COVERAGE_LIBS)
cti_fe_daemon@CRAYTOOL_RELEASE_VERSION@_CFLAGS	= $(CODE_COVERAGE_CFLAGS)
