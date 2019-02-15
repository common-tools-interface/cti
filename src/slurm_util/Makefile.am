#
# Makefile.am for SLURM utilities.
#
# Copyright 2019 Cray Inc.  All Rights Reserved.
#
# Unpublished Proprietary Information.
# This unpublished work is protected to trade secret, copyright and other laws.
# Except as permitted by contract or express written permission of Cray Inc.,
# no part of this work or its content may be used, reproduced or disclosed
# in any form.
#

USEFUL = @CRAYTOOL_DIR@/src/useful

SRC             = @CRAYTOOL_DIR@/src
AM_CXXFLAGS		= -I$(SRC)
AM_LIBTOOLFLAGS = --quiet

noinst_LTLIBRARIES      	=	libslurm_util.la

libslurm_util_la_SOURCES	=	slurm_util.cpp slurm_util.h
libslurm_util_la_CXXFLAGS	=	$(AM_CXXFLAGS) -fPIC
libslurm_util_la_LIBADD		=
libslurm_util_la_LDFLAGS	=	-static -Wl,--no-undefined -Wl,--as-needed
