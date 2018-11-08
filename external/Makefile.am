#
# Makefile.am for the external components of the craytool interface.
#
# Copyright 2015-2017 Cray Inc.  All Rights Reserved.
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

install-data-local:
if BUILT_SSH
	cp @LIBSSH_LOC@ ${libdir}/libssh.so.4
endif

distclean-local:
	rm -rf @CRAYTOOL_EXTERNAL_INSTALL@
	rm -rf @ALPS_BASE@
	rm -rf @WLM_DETECT_BASE@
	rm -rf @SLURM_BASE@
if BUILT_ARC
	cd @LIBARC_SRC@ && $(MAKE) distclean
endif
if BUILT_SSH
	cd @LIBSSH_SRC@ && rm -r build
endif
