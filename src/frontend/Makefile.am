#
# Makefile.am for the frontend components of the craytool interface.
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

ARCHIVE_LIBS    = @archive_libs@
SRC             = @craytool_dir@/src
LDVAL           = $(SRC)/ld_val
USEFUL          = $(SRC)/useful

AM_CFLAGS		= -fPIC -Wall -I$(SRC)
AM_LIBTOOLFLAGS = --quiet

lib_LTLIBRARIES = libcraytools_fe.la

libcraytools_fe_la_SOURCES	=	cti_fe.c cti_fe.h cti_run.c cti_run.h cti_error.c cti_error.h \
								cti_transfer.c cti_transfer.h alps_fe.c alps_fe.h 
libcraytools_fe_la_CFLAGS	=	$(AM_CFLAGS) -I$(LDVAL) -I$(USEFUL)
libcraytools_fe_la_LIBADD	=	$(USEFUL)/libuseful.la $(LDVAL)/libld_val.la $(ARCHIVE_LIBS)
libcraytools_fe_la_LDFLAGS	=	-ldl -version-info $(CRAYTOOL_FE_VERSION)
