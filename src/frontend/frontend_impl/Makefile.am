#
# Makefile.am for the frontend impl components of the craytool interface.
#
# Copyright 2019 Cray Inc.  All Rights Reserved.
#
# Unpublished Proprietary Information.
# This unpublished work is protected to trade secret, copyright and other laws.
# Except as permitted by contract or express written permission of Cray Inc.,
# no part of this work or its content may be used, reproduced or disclosed
# in any form.
#

SUBDIRS				= CraySLURM GenericSSH

SRC             	= @CRAYTOOL_DIR@/src
AM_LIBTOOLFLAGS 	= --quiet

noinst_LTLIBRARIES	= libfrontend_impl.la

libfrontend_impl_la_SOURCES	=
libfrontend_impl_la_LIBADD	= 	CraySLURM/libcrayslurm.la \
								GenericSSH/libgenericssh.la
libfrontend_impl_la_LDFLAGS	=	-static -Wl,--no-undefined
noinst_HEADERS				=	Frontend_impl.hpp
