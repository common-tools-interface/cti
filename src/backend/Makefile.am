#
# Makefile.am for the backend components of the craytool interface.
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

SRC				= @CRAYTOOL_DIR@/src

AM_CFLAGS		= -Wall -I$(SRC)
AM_LIBTOOLFLAGS = --quiet

lib_LTLIBRARIES = libcraytools_be.la

libcraytools_be_la_SOURCES  =	cti_be.c alps_be.c cray_slurm_be.c \
								pmi_attribs_parser.c slurm_be.c
libcraytools_be_la_CFLAGS   =   $(AM_CFLAGS) -fPIC 
libcraytools_be_la_LIBADD	=	-ldl
libcraytools_be_la_LDFLAGS  =   -Wl,--no-undefined -Wl,--as-needed -version-info $(CRAYTOOL_BE_VERSION)
