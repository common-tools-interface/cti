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

SRC             = @CRAYTOOL_DIR@/src
AM_LIBTOOLFLAGS = --quiet

lib_LTLIBRARIES = libcraytools_fe.la

libcraytools_fe_la_SOURCES	=	cti_fe_iface.h cti_fe_iface.cpp \
								cti_error.c cti_error.h \
								Frontend.hpp \
								cray_slurm_fe.cpp cray_slurm_fe.hpp \
								# ssh_fe.cpp ssh_fe.hpp
libcraytools_fe_la_CFLAGS	=	$(AM_CFLAGS) -fPIC -I$(SRC) \
								$(ARCHIVE_CFLAGS) $(WLM_DETECT_CFLAGS) $(DYNINST_CFLAGS)
libcraytools_fe_la_CXXFLAGS	=	$(AM_CXXFLAGS) $(libcraytools_fe_la_CFLAGS)
libcraytools_fe_la_LIBADD	=	$(SRC)/useful/libuseful.la \
								$(SRC)/cti_transfer/libcti_transfer.la \
								$(SRC)/ld_val/libld_val.la \
								$(SRC)/mpir_iface/libmpir_iface.la \
								 -ldl -lrt -lstdc++ $(ARCHIVE_LIBS) $(DYNINST_LIBS)
libcraytools_fe_la_LDFLAGS	=	-Wl,-z,origin -Wl,-rpath,\$$ORIGIN -Wl,--enable-new-dtags -Wl,--no-undefined -Wl,--as-needed -version-info \
								$(CRAYTOOL_FE_VERSION) $(ARCHIVE_LDFLAGS) $(DYNINST_LDFLAGS)
