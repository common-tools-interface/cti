#
# Makefile.am for the frontend components of the craytool interface.
#
# Copyright 2014-2017 Cray Inc.  All Rights Reserved.
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


AM_CXXFLAGS				= -Wall -std=c++11

SRC             = @CRAYTOOL_DIR@/src
AM_CFLAGS		= -Wall
AM_LIBTOOLFLAGS = --quiet

lib_LTLIBRARIES = libcraytools_fe.la

libcraytools_fe_la_SOURCES	=	cti_fe.h cti_fe.cpp \
								cti_run.c cti_run.h \
								cti_transfer.cpp cti_transfer.h \
								cti_error.c cti_error.h \
								Frontend.hpp \
								alps_fe.c alps_fe.h \
								cray_slurm_fe.c cray_slurm_fe.h \
								slurm_fe.c slurm_fe.h \
								ssh_fe.c ssh_fe.h
libcraytools_fe_la_CFLAGS	=	$(AM_CFLAGS) -fPIC -I$(SRC) \
								$(ARCHIVE_CFLAGS) $(ALPS_CFLAGS) $(WLM_DETECT_CFLAGS)
libcraytools_fe_la_CXXFLAGS	=	$(AM_CXXFLAGS) $(libcraytools_fe_la_CFLAGS)
libcraytools_fe_la_LIBADD	=	$(SRC)/useful/libuseful.la \
								$(SRC)/cti_transfer/libcti_transfer.la \
								$(SRC)/ld_val/libld_val.la \
								$(SRC)/mpir_iface/libmpir_iface.la \
								$(SRC)/slurm_util/libslurm_util.la \
								 -ldl -lrt -lstdc++ $(ARCHIVE_LIBS)
libcraytools_fe_la_LDFLAGS	=	-Wl,-z,origin -Wl,-rpath,\$$ORIGIN -Wl,--enable-new-dtags -Wl,--no-undefined -Wl,--as-needed -version-info $(CRAYTOOL_FE_VERSION) \
								$(ARCHIVE_LDFLAGS)

