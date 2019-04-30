#
# Makefile.am for the Cray Slurm frontend implementation.
#
# Copyright 2019 Cray Inc.  All Rights Reserved.
#
# Unpublished Proprietary Information.
# This unpublished work is protected to trade secret, copyright and other laws.
# Except as permitted by contract or express written permission of Cray Inc.,
# no part of this work or its content may be used, reproduced or disclosed
# in any form.
#

SRC             	= @CRAYTOOL_DIR@/src
AM_LIBTOOLFLAGS 	= --quiet
AM_CXXFLAGS			= -Wall -I$(SRC) -I$(SRC)/frontend -I$(SRC)/frontend/frontend_impl

noinst_LTLIBRARIES = libcrayslurm.la

libcrayslurm_la_SOURCES		=	Frontend.cpp
libcrayslurm_la_CXXFLAGS	=	$(AM_CXXFLAGS) -fPIC
libcrayslurm_la_LDFLAGS		=	-static -Wl,--no-undefined
noinst_HEADERS				=	Frontend.hpp
