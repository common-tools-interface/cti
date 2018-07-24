#
# Makefile.am for SLURM utilities.
#
# Copyright2014 Cray Inc.  All Rights Reserved.
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

###################################
# WARNING: THIS IS GPL DIRECTORY!!
###################################
AM_CXXFLAGS				= -Wall -std=c++11
AM_LIBTOOLFLAGS			= --quiet

USEFUL_INCLUDES = -I@CRAYTOOL_DIR@/src/useful

libexec_PROGRAMS		= cti_slurm_util

cti_slurm_util_SOURCES	= cti_slurm_util.cpp
cti_slurm_util_CXXFLAGS	= $(AM_CXXFLAGS) $(USEFUL_INCLUDES)
cti_slurm_util_LDADD	=
cti_slurm_util_LDFLAGS	= -Wl,--no-undefined -Wl,--as-needed
