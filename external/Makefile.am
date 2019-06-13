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
SUBDIRS = elfutils libssh2

install-data-hook:
	cp @LIBSSH_LOC@ ${libdir}/libssh.so.4
	cd @BOOST_SRC@ && @BOOST_SRC@/b2 install
	cp @TBB_LIBRARY@/*.so* ${libdir}
	cd @DYNINST_BUILD@ && $(MAKE) $(AM_MAKEFLAGS) install

distclean-local:
	-rm -rf @CRAYTOOL_EXTERNAL_INSTALL@
	cd @LIBARC_SRC@ && $(MAKE) distclean
	-rm -rf @BOOST_SRC@/bin.v2
	-rm -rf @BOOST_SRC@/stage
	-rm -rf @DYNINST_BUILD@
