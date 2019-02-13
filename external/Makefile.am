#
# Makefile.am for the external components of the craytool interface.
#
# Copyright 2015-2019 Cray Inc.  All Rights Reserved.
#
# Unpublished Proprietary Information.
# This unpublished work is protected to trade secret, copyright and other laws.
# Except as permitted by contract or express written permission of Cray Inc.,
# no part of this work or its content may be used, reproduced or disclosed
# in any form.
#

SUBDIRS = elfutils

install-data-local:
	cp @LIBSSH_LOC@ ${libdir}/libssh.so.4
	@BOOST_SRC@/b2 install
	cp @TBB_LIBRARY@/*.so* ${libdir}
	cd @DYNINST_BUILD@ && $(MAKE) $(AM_MAKEFLAGS) install

distclean-local:
	-rm -rf @CRAYTOOL_EXTERNAL_INSTALL@
	-rm -rf @ALPS_BASE@
	-rm -rf @WLM_DETECT_BASE@
	-rm -rf @SLURM_BASE@
	cd @LIBARC_SRC@ && $(MAKE) distclean
	-rm -rf @LIBSSH_SRC@/build
	-rm -rf @BOOST_SRC@/bin.v2
	-rm -rf @BOOST_SRC@/stage
	-rm -rf @DYNINST_BUILD@
