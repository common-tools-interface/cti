#
# Makefile.am for the useful components of the craytool interface.
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
AM_LIBTOOLFLAGS = --quiet
AM_CFLAGS		= -fPIC -Wall -I$(SRC)

noinst_LTLIBRARIES      = libuseful.la

libuseful_la_SOURCES    = log.c log.h path.c path.h stack.c stack.h stringList.c stringList.h
libuseful_la_CFLAGS     = $(AM_CFLAGS)
libuseful_la_LDFLAGS    = -static -no-undefined

noinst_HEADERS			= useful.h
