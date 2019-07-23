#
# Makefile.am for the external components of the craytool interface.
#
# Copyright 2015-2019 Cray Inc. All Rights Reserved.
#

SUBDIRS = elfutils libssh2

install-data-hook:
	cd @BOOST_SRC@ && @BOOST_SRC@/b2 install
	cp @TBB_LIBRARY@/*.so* ${libdir}
	cp -R @TBB_INCLUDE_DIR@/tbb ${includedir}
	cd @DYNINST_BUILD@ && $(MAKE) $(AM_MAKEFLAGS) install

distclean-local:
	-rm -rf @CRAYTOOL_EXTERNAL_INSTALL@
	cd @LIBARC_SRC@ && $(MAKE) distclean
	-rm -rf @BOOST_SRC@/bin.v2
	-rm -rf @BOOST_SRC@/stage
	-rm -rf @DYNINST_BUILD@
