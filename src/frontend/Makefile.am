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

SRC             = @CRAYTOOL_DIR@/src
LDVAL           = $(SRC)/ld_val
USEFUL          = $(SRC)/useful
MPIR_IFACE		= $(SRC)/mpir_iface

AM_CFLAGS		= -Wall -I$(SRC)
AM_LIBTOOLFLAGS = --quiet

lib_LTLIBRARIES = libcraytools_fe.la

libcraytools_fe_la_SOURCES	=	cti_fe.c cti_fe.h cti_run.c cti_run.h cti_error.c cti_error.h \
								cti_transfer.c cti_transfer.h alps_fe.c alps_fe.h cray_slurm_fe.c \
								cray_slurm_fe.h slurm_fe.c slurm_fe.h ssh_fe.c ssh_fe.h
libcraytools_fe_la_CFLAGS	=	$(AM_CFLAGS) -fPIC -I$(MPIR_IFACE) -I$(LDVAL) -I$(USEFUL) \
								$(ARCHIVE_CFLAGS) $(ALPS_CFLAGS) $(WLM_DETECT_CFLAGS)
libcraytools_fe_la_LIBADD	=	$(USEFUL)/libuseful.la $(LDVAL)/libld_val.la \
								$(MPIR_IFACE)/libmpir_iface.la -ldl -lrt -lssh \
								$(ARCHIVE_LIBS)
libcraytools_fe_la_LDFLAGS	=	-Wl,-z,origin -Wl,-rpath,\$$ORIGIN -Wl,--enable-new-dtags -Wl,--no-undefined -Wl,--as-needed -version-info $(CRAYTOOL_FE_VERSION) \
								$(ARCHIVE_LDFLAGS)

