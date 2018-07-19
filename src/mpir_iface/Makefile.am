#
# Makefile.am for the MPIR interface components of the craytool interface.
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
AM_CXXFLAGS		= -Wall -I$(SRC)
AM_LIBTOOLFLAGS = --quiet

noinst_LTLIBRARIES      	=	libmpir_iface.la

libmpir_iface_la_SOURCES	=	mpir_iface.cpp mpir_iface.h MPIRInstance.cpp MPIRInstance.hpp MPIRInferior.cpp MPIRInferior.hpp
libmpir_iface_la_CXXFLAGS	=	$(AM_CXXFLAGS) -fPIC $(DYNINST_CXXFLAGS) $(BOOST_CXXFLAGS)
libmpir_iface_la_LIBADD		=	$(DYNINST_LIBS) $(BOOST_LIBS)
libmpir_iface_la_LDFLAGS	=	-static -Wl,--no-undefined -Wl,--as-needed $(DYNINST_LDFLAGS) $(BOOST_LDFLAGS)
