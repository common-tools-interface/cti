#
# Top level Makefile.am for the craytool interface.
#
# ©2011 Cray Inc.  All Rights Reserved.
#
# Unpublished Proprietary Information.
# This unpublished work is protected to trade secret, copyright and other laws.
# Except as permitted by contract or express written permission of Cray Inc.,
# no part of this work or its content may be used, reproduced or disclosed
# in any form.
#
# $HeadURL$
# $Date$
# $Rev$
# $Author$
#
SRCDIR=@craytool_srcdir@

ACLOCAL_AMFLAGS = -I m4

SUBDIRS = src/ld_val src

EXTRA_DIST = cray-tool_backend.pc.in cray-tool_frontend.pc.in cray-audit.pc.in \
	     release_versioning

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = cray-tool_backend.pc cray-tool_frontend.pc \
		 cray-audit.pc

include_HEADERS = include/tool_backend.h include/tool_frontend.h

