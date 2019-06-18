#
# Makefile.am for the CTI frontend daemon
#
# Copyright 2019 Cray Inc.  All Rights Reserved.
#
# Unpublished Proprietary Information.
# This unpublished work is protected to trade secret, copyright and other laws.
# Except as permitted by contract or express written permission of Cray Inc.,
# no part of this work or its content may be used, reproduced or disclosed
# in any form.
#

SRC             = @CRAYTOOL_DIR@/src
AM_LIBTOOLFLAGS = --quiet
AM_CFLAGS		= -Wall -I$(SRC) -I.

noinst_LTLIBRARIES      = libfe_daemon_iface.la

libfe_daemon_iface_la_SOURCES  = cti_fe_daemon_iface.cpp
libfe_daemon_iface_la_CFLAGS   = $(AM_CFLAGS) -fPIC
libfe_daemon_iface_la_CXXFLAGS = $(AM_CXXFLAGS) $(libfe_daemon_iface_la_CFLAGS)
libfe_daemon_iface_la_LDFLAGS  = -static -Wl,--no-undefined

libexec_PROGRAMS		= 	cti_fe_daemon@CRAYTOOL_RELEASE_VERSION@

cti_fe_daemon@CRAYTOOL_RELEASE_VERSION@_SOURCES  = cti_fe_daemon.cpp 
cti_fe_daemon@CRAYTOOL_RELEASE_VERSION@_CPPFLAGS = $(DYNINST_CPPFLAGS) $(BOOST_CPPFLAGS)
cti_fe_daemon@CRAYTOOL_RELEASE_VERSION@_CXXFLAGS = $(AM_CFLAGS) $(AM_CXXFLAGS) $(DYNINST_CXXFLAGS) $(BOOST_CXXFLAGS)
cti_fe_daemon@CRAYTOOL_RELEASE_VERSION@_LDFLAGS  = -Wl,--no-undefined -Wl,--as-needed -Wl,-rpath,\$$ORIGIN/../lib -pthread $(DYNINST_LDFLAGS) $(BOOST_LDFLAGS)
cti_fe_daemon@CRAYTOOL_RELEASE_VERSION@_LDADD    = libfe_daemon_iface.la ../mpir_iface/libmpir_iface.la $(DYNINST_LIBS) $(BOOST_LIBS)
