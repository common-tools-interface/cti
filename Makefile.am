#
# Top level Makefile.am for the common tools interface.
#
# Copyright 2011-2019 Cray Inc. All Rights Reserved.
#

ACLOCAL_AMFLAGS = -I m4
AM_LIBTOOLFLAGS = --quiet

SUBDIRS = src tests

EXTRA_DIST = pkgconfig \
             release_versioning

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = pkgconfig/common_tools_be.pc \
                 pkgconfig/common_tools_fe.pc


include_HEADERS = include/common_tools_be.h include/common_tools_fe.h \
		 include/common_tools_shared.h include/common_tools_version.h

VERSION_PATTERN=`grep '^[a-z]' release_versioning | sed 's% *= *"%>/%;s%^%s/<%;s%".*%/%' | tr '\n' ';'`

include/common_tools_version.h: src/common_tools_version.h.template release_versioning
	sed -e $(VERSION_PATTERN) $< > $@



