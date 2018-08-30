#
# Makefile.am for the CTI transfer components of the craytool interface.
#
# Copyright2014 Cray Inc.  All Rights Reserved.
#
# Unpublished Proprietary Information.
# This unpublished work is protected to trade secret, copyright and other laws.
# Except as permitted by contract or express written permission of Cray Inc.,
# no part of this work or its content may be used, reproduced or disclosed
# in any form.
#
## $HeadURL$
## $Date$
## $Rev$
## $Author$

SRC             = @CRAYTOOL_DIR@/src
AM_CXXFLAGS	=	-Wall -I$(SRC) -std=c++11
AM_LIBTOOLFLAGS = --quiet

noinst_LTLIBRARIES	=	libcti_transfer.la

libcti_transfer_la_SOURCES	=	cti_transfer.cpp cti_transfer.h Manifest.cpp Manifest.hpp Session.cpp Session.hpp
libcti_transfer_la_CXXFLAGS		= $(AM_CXXFLAGS) -fPIC $(BOOST_CXXFLAGS)
libcti_transfer_la_CPPFLAGS	=	$(BOOST_CPPFLAGS)
libcti_transfer_la_LDFLAGS	=	-static -Wl,--no-undefined -Wl,--as-needed $(BOOST_LDFLAGS)
libcti_transfer_la_LIBADD		=	$(BOOST_LIBS)