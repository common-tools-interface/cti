#
# Makefile.am for the MPIR interface components of the craytool interface.
#
# Copyright 2018-2019 Cray Inc.  All Rights Reserved.
#
# Unpublished Proprietary Information.
# This unpublished work is protected to trade secret, copyright and other laws.
# Except as permitted by contract or express written permission of Cray Inc.,
# no part of this work or its content may be used, reproduced or disclosed
# in any form.
#
SRC             = @CRAYTOOL_DIR@/src
AM_CXXFLAGS		= -Wall -I$(SRC)
AM_LIBTOOLFLAGS = --quiet

noinst_LTLIBRARIES      	=	libmpir_iface.la

libmpir_iface_la_SOURCES	=	MPIRInstance.cpp Inferior.cpp
libmpir_iface_la_CXXFLAGS	= 	$(AM_CXXFLAGS) -fPIC $(DYNINST_CXXFLAGS) $(BOOST_CXXFLAGS)
libmpir_iface_la_CPPFLAGS	=	$(DYNINST_CPPFLAGS) $(BOOST_CPPFLAGS)
libmpir_iface_la_LDFLAGS	=	-static -Wl,--no-undefined
noinst_HEADERS				=	Inferior.hpp MPIRInstance.hpp
