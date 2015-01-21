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

AM_CFLAGS				= -Wall
AM_LIBTOOLFLAGS			= --quiet

libexec_PROGRAMS		= cti_slurm_util

cti_slurm_util_SOURCES	= slurm_util.c
cti_slurm_util_CFLAGS	= $(AM_CFLAGS) $(SLURM_CFLAGS)
cti_slurm_util_LDADD	= $(SLURM_LIBS)
cti_slurm_util_LDFLAGS	= -Wl,--no-undefined -Wl,--as-needed $(SLURM_LDFLAGS)
