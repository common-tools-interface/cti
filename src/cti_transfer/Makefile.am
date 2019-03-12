#
# Makefile.am for the CTI transfer components of the craytool interface.
#
# Copyright 2014-2019 Cray Inc.  All Rights Reserved.
#
# Unpublished Proprietary Information.
# This unpublished work is protected to trade secret, copyright and other laws.
# Except as permitted by contract or express written permission of Cray Inc.,
# no part of this work or its content may be used, reproduced or disclosed
# in any form.
#

SRC             = @CRAYTOOL_DIR@/src
FRONTEND        = $(SRC)/frontend
AM_CXXFLAGS		= -I$(SRC) -I$(FRONTEND)
AM_LIBTOOLFLAGS = --quiet

noinst_LTLIBRARIES			=	libcti_transfer.la

libcti_transfer_la_SOURCES	=	cti_wrappers.hpp \
								Archive.cpp Archive.hpp \
								RemotePackage.cpp RemotePackage.hpp \
								Manifest.cpp Manifest.hpp \
								Session.cpp Session.hpp
libcti_transfer_la_CXXFLAGS	= 	$(AM_CXXFLAGS) -fPIC $(ARCHIVE_CFLAGS)
libcti_transfer_la_LDFLAGS	=	-static -Wl,--as-needed \
								$(ARCHIVE_LDFLAGS)
libcti_transfer_la_LIBADD	=	$(ARCHIVE_LIBS)
