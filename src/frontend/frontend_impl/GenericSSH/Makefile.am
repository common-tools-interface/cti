#
# Makefile.am for the generic SSH frontend implementation.
#
# Copyright 2019 Cray Inc.  All Rights Reserved.
#
# Unpublished Proprietary Information.
# This unpublished work is protected to trade secret, copyright and other laws.
# Except as permitted by contract or express written permission of Cray Inc.,
# no part of this work or its content may be used, reproduced or disclosed
# in any form.
#

SRC                 = @CRAYTOOL_DIR@/src
AM_LIBTOOLFLAGS     = --quiet
AM_CXXFLAGS         = -Wall -I$(SRC)

noinst_LTLIBRARIES = libgenericssh.la

libgenericssh_la_SOURCES    =   Frontend.cpp
libgenericssh_la_CXXFLAGS   =   $(AM_CXXFLAGS) -fPIC
libgenericssh_la_LDFLAGS	=   -static -Wl,--no-undefined
noinst_HEADERS				=   Frontend.hpp
