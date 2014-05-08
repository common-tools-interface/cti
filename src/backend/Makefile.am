#
# Makefile.am for the backend components of the craytool interface.
#
# ©2014 Cray Inc.  All Rights Reserved.
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

SRC             = @craytool_dir@/src

AM_CFLAGS       = -fPIC -Wall -I$(SRC)
AM_LIBTOOLFLAGS = --quiet

lib_LTLIBRARIES = libcraytools_be.la

libcraytools_be_la_SOURCES  =	cti_be.c cti_be.h alps_be.c alps_be.h \
								pmi_attribs_parser.c pmi_attribs_parser.h
libcraytools_be_la_CFLAGS   =   $(AM_CFLAGS)
libcraytools_be_la_LDFLAGS  =   -ldl -version-info $(CRAYTOOL_BE_VERSION)
