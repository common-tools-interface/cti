#
# Top level Makefile.am for the craytool interface.
#
# Copyright 2011-2015 Cray Inc.  All Rights Reserved.
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

ACLOCAL_AMFLAGS = -I m4
AM_LIBTOOLFLAGS = --quiet

SUBDIRS = src tests

EXTRA_DIST = cray-cti_be.pc.in cray-cti_fe.pc.in release_versioning

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = cray-cti_be.pc cray-cti_fe.pc

include_HEADERS = include/cray_tools_be.h include/cray_tools_fe.h

tests:
	cd tests && $(MAKE) $(AM_MAKEFLAGS) tests

.PHONY: tests
