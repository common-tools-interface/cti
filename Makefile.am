#
# Top level Makefile.am for the common tools interface.
#
# Copyright 2011-2020 Hewlett Packard Enterprise Development LP.
#

ACLOCAL_AMFLAGS = -I m4
AM_LIBTOOLFLAGS = --quiet

SUBDIRS = src tests/mock tests/unit doc/man

EXTRA_DIST = pkgconfig \
			 release_versioning

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = pkgconfig/common_tools_be.pc \
				 pkgconfig/common_tools_fe.pc


include_HEADERS = include/common_tools_be.h include/common_tools_fe.h \
		 include/common_tools_shared.h include/common_tools_version.h

install-data-hook:
	export PKG_CONFIG_PATH=$(libdir)/pkgconfig:$(PKG_CONFIG_PATH); \
	export CTI_INSTALL_DIR=$(prefix); \
	$(MAKE) -C ./tests/test_support all; \
	$(MAKE) -C ./tests/function/src tests; \
	$(MAKE) -C ./tests/pipeline/opt/cray/tests/cdst/resources/cti/smoke prebuilt
