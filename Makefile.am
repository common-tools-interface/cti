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

EXTRA_DIST = cray-tools_be.pc.in cray-tools_fe.pc.in cray-audit.pc.in \
	     release_versioning

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = cray-tools_be.pc cray-tools_fe.pc \
		 cray-audit.pc

include_HEADERS = include/cray_tools_be.h include/cray_tools_fe.h

tests:
	cd tests && $(MAKE) $(AM_MAKEFLAGS) tests

.PHONY: tests
