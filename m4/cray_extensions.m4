#
# cray_extensions.m4 Cray configure extensions.
#
# Copyright 2012-2015 Cray Inc.  All Rights Reserved.
#
# Unpublished Proprietary Information.
# This unpublished work is protected to trade secret, copyright and other laws.
# Except as permitted by contract or express written permission of Cray Inc.,
# no part of this work or its content may be used, reproduced or disclosed
# in any form.
#
dnl $HeadURL$
dnl $Date$
dnl $Rev$
dnl $Author$

dnl
dnl read release and library version information from disk
dnl This is not portable due to tr -d and source...
dnl
AC_DEFUN([cray_INIT],
[
	dnl Pull in the revision information from the release_versioning file
	m4_define([CRAYTOOL_RELEASE], [m4_esyscmd([source release_versioning; echo $craytool_major.$craytool_minor | tr -d '\n'])])
	
	m4_define([CRAYTOOL_BE_CURRENT], [m4_esyscmd([source release_versioning; echo $be_current | tr -d '\n'])])
	m4_define([CRAYTOOL_BE_REVISION], [m4_esyscmd([source release_versioning; echo $be_revision | tr -d '\n'])])
	m4_define([CRAYTOOL_BE_AGE], [m4_esyscmd([source release_versioning; echo $be_age | tr -d '\n'])])
	
	m4_define([CRAYTOOL_FE_CURRENT], [m4_esyscmd([source release_versioning; echo $fe_current | tr -d '\n'])])
	m4_define([CRAYTOOL_FE_REVISION], [m4_esyscmd([source release_versioning; echo $fe_revision | tr -d '\n'])])
	m4_define([CRAYTOOL_FE_AGE], [m4_esyscmd([source release_versioning; echo $fe_age | tr -d '\n'])])
	
	AC_SUBST([CRAYTOOL_BE_VERSION], [CRAYTOOL_BE_CURRENT:CRAYTOOL_BE_REVISION:CRAYTOOL_BE_AGE])
	AC_SUBST([CRAYTOOL_FE_VERSION], [CRAYTOOL_FE_CURRENT:CRAYTOOL_FE_REVISION:CRAYTOOL_FE_AGE])
	AC_SUBST([CRAYTOOL_RELEASE_VERSION], [CRAYTOOL_RELEASE])
	
	AC_DEFINE_UNQUOTED([CTI_BE_VERSION], ["CRAYTOOL_BE_CURRENT.CRAYTOOL_BE_REVISION.CRAYTOOL_BE_AGE"], [Version number of CTI backend.])
	AC_DEFINE_UNQUOTED([CTI_FE_VERSION], ["CRAYTOOL_FE_CURRENT.CRAYTOOL_FE_REVISION.CRAYTOOL_FE_AGE"], [Version number of CTI frontend.])
	
	AC_SUBST([CRAYTOOL_EXTERNAL], [${CRAYTOOL_DIR}/external])
	AC_SUBST([CRAYTOOL_EXTERNAL_INSTALL], [${CRAYTOOL_DIR}/external/install])
	
	if test ! -d "${CRAYTOOL_EXTERNAL_INSTALL}"; then
		AS_MKDIR_P([${CRAYTOOL_EXTERNAL_INSTALL}])
	fi
])

dnl
dnl build libarchive automatically
dnl
AC_DEFUN([cray_BUILD_LIBARCHIVE],
[
	cray_cv_lib_archive_build=no

	dnl Temporary directory to stage files to
	_cray_tmpdir="${CRAYTOOL_EXTERNAL}/libarchive"

	AC_MSG_CHECKING([for libarchive stage directory])
	
	dnl Ensure the libarchive source was checked out
	AS_IF(	[test ! -d "$_cray_tmpdir"],
			[AC_MSG_ERROR([git submodule libarchive not found.])],
			[AC_MSG_RESULT([yes])]
			)
	
	dnl cd to the checked out source directory
	cd ${_cray_tmpdir}
	
	AC_MSG_NOTICE([Building libarchive...])
	
	dnl run configure with options that work on build systems
	./configure --prefix=${CRAYTOOL_EXTERNAL_INSTALL} --disable-shared --with-pic --without-expat --without-xml2 \
	--without-openssl --without-nettle --without-lzo2 --without-lzma --without-libiconv-prefix --without-iconv \
	--without-lzmadec --without-bz2lib --without-zlib --disable-bsdtar --disable-bsdcpio \
	--disable-rpath >&AS_MESSAGE_LOG_FD 2>&AS_MESSAGE_LOG_FD
	
	dnl make
	make >&AS_MESSAGE_LOG_FD 2>&AS_MESSAGE_LOG_FD
	make install >&AS_MESSAGE_LOG_FD 2>&AS_MESSAGE_LOG_FD
	
	dnl go home
	cd ${CRAYTOOL_DIR}
	
	if test -f "${CRAYTOOL_EXTERNAL_INSTALL}/lib/libarchive.a"; then
		cray_cv_lib_archive_build=yes
	fi
])

dnl
dnl define post-cache libarchive env
dnl
AC_DEFUN([cray_ENV_LIBARCHIVE],
[
	AC_SUBST([LIBARC_SRC], [${CRAYTOOL_EXTERNAL}/libarchive])
	AC_SUBST([INTERNAL_LIBARCHIVE], [${CRAYTOOL_EXTERNAL_INSTALL}])
])

dnl
dnl build libmi automatically
dnl
AC_DEFUN([cray_BUILD_LIBMI],
[
	cray_cv_lib_mi_build=no

	dnl Temporary directory to stage files to
	_cray_tmpdir="${CRAYTOOL_EXTERNAL}/libmi"

	AC_MSG_CHECKING([for libmi stage directory])
	
	dnl Ensure the libmi source was checked out
	AS_IF(	[test ! -d "$_cray_tmpdir"],
			[AC_MSG_ERROR([git submodule libmi not found.])],
			[AC_MSG_RESULT([yes])]
			)
	
	dnl cd to the checked out source directory
	cd ${_cray_tmpdir}
	
	AC_MSG_NOTICE([Building libmi...])
	
	dnl run configure with options that work on build systems
	./configure --prefix=${CRAYTOOL_EXTERNAL_INSTALL} >&AS_MESSAGE_LOG_FD 2>&AS_MESSAGE_LOG_FD
	
	dnl make
	make >&AS_MESSAGE_LOG_FD 2>&AS_MESSAGE_LOG_FD
	make install >&AS_MESSAGE_LOG_FD 2>&AS_MESSAGE_LOG_FD
	
	dnl go home
	cd ${CRAYTOOL_DIR}
	
	if test -f "${CRAYTOOL_EXTERNAL_INSTALL}/lib/libmi.so"; then
		cray_cv_lib_mi_build=yes
	fi
])

dnl
dnl define post-cache libmi env
dnl
AC_DEFUN([cray_ENV_LIBMI],
[
	AC_SUBST([LIBMI_SRC], [${CRAYTOOL_EXTERNAL}/libmi])
	AC_SUBST([INTERNAL_LIBMI], [${CRAYTOOL_EXTERNAL_INSTALL}])
	AC_SUBST([LIBMI_LOC], [${CRAYTOOL_EXTERNAL_INSTALL}/lib/libmi.so])
])

dnl
dnl build gdb automatically
dnl
AC_DEFUN([cray_BUILD_GDB],
[
	cray_cv_prog_gdb_build=no

	dnl Temporary directory to stage files to
	_cray_tmpdir="${CRAYTOOL_EXTERNAL}/gdb"

	AC_MSG_CHECKING([for libmi stage directory])
	
	dnl Ensure the gdb source was checked out
	AS_IF(	[test ! -d "$_cray_tmpdir"],
			[AC_MSG_ERROR([git submodule gdb not found.])],
			[AC_MSG_RESULT([yes])]
			)
	
	dnl cd to the checked out source directory
	cd ${_cray_tmpdir}
	
	AC_MSG_NOTICE([Building gdb...])
	
	dnl run configure with options that work on build systems
	./configure --prefix=${CRAYTOOL_EXTERNAL_INSTALL} --program-prefix="cti_approved_" --disable-rpath --without-separate-debug-dir --without-gdb-datadir --with-mmalloc --without-python >&AS_MESSAGE_LOG_FD 2>&AS_MESSAGE_LOG_FD
	
	dnl make
	make >&AS_MESSAGE_LOG_FD 2>&AS_MESSAGE_LOG_FD
	make install >&AS_MESSAGE_LOG_FD 2>&AS_MESSAGE_LOG_FD
	
	dnl go home
	cd ${CRAYTOOL_DIR}
	
	if test -f "${CRAYTOOL_EXTERNAL_INSTALL}/bin/cti_approved_gdb"; then
		cray_cv_prog_gdb_build=yes
	fi
])

dnl
dnl define post-cache gdb env
dnl
AC_DEFUN([cray_ENV_GDB],
[
	AC_SUBST([GDB_SRC], [${CRAYTOOL_EXTERNAL}/gdb])
	AC_SUBST([INTERNAL_GDB], [${CRAYTOOL_EXTERNAL_INSTALL}])
	AC_SUBST([GDB_LOC], [${CRAYTOOL_EXTERNAL_INSTALL}/bin/cti_approved_gdb])
])

dnl
dnl create alps files in a staging location
dnl
AC_DEFUN([cray_SETUP_ALPS_RPMS],
[
	dnl Set this to the CLE base version we are buidling against. Note that this
	dnl does not include update level information.
	CLE_RPM_VERS="5.2"

	dnl Set this to the location of the alps RPMS we should build against
	ALPS_RPM_DIR="/cray/css/release/cray/build/xt/sles11sp3/x86_64/RB-5.2UP00-ari/working/latest-RB-5.2UP00/ALPS/x86_64"

	dnl Set this to the location of the xmlrpc-epi RPMS we should build against
	XMLRPC_RPM_DIR="/cray/css/release/cray/build/xt/sles11sp3/x86_64/RB-5.2UP00-ari/working/latest-RB-5.2UP00/3rd-party/x86_64/"

	dnl Create a temporary directory to extract the alps libraries to
	_cray_tmpdir="${CRAYTOOL_EXTERNAL}/alps_base"
	
	AC_MSG_CHECKING([for ALPS stage directory])
	
	AS_IF(	[test ! -d "$_cray_tmpdir"],
			[	AC_MSG_RESULT([no])
				
				dnl check required execs
				AC_PROG_MKDIR_P
				
				AC_ARG_VAR([RPM2CPIO], [Location of rpm2cpio])
				AC_ARG_VAR([CPIO], [Location of cpio])

				AC_PATH_PROG([RPM2CPIO], [rpm2cpio], [rpm2cpio])
				if test -z "$RPM2CPIO"; then
					AC_MSG_ERROR([rpm2cpio not found.])
				fi

				AC_PATH_PROG([CPIO], [cpio], [cpio])
				if test -z "$CPIO"; then
					AC_MSG_ERROR([cpio not found.])
				fi
				
				AC_MSG_NOTICE([Creating ALPS staging directory in $_cray_tmpdir])
				
				dnl resolve the names of the alps rpms
				if test -d "$ALPS_RPM_DIR"; then
					ALPS_RPM_1=$(find $ALPS_RPM_DIR/cray-libalps0-$CLE_RPM_VERS.[[0-9\.\-]]*.ari.x86_64.rpm 2>&AS_MESSAGE_LOG_FD)
					if test ! -e "$ALPS_RPM_1"; then
						AC_MSG_ERROR([ALPS RPM cray-libalps0-$CLE_RPM_VERS not found.])
					fi
					ALPS_RPM_2=$(find $ALPS_RPM_DIR/cray-libalps-devel-$CLE_RPM_VERS.[[0-9\.\-]]*.ari.x86_64.rpm 2>&AS_MESSAGE_LOG_FD)
					if test ! -e "$ALPS_RPM_2"; then
						AC_MSG_ERROR([ALPS RPM cray-libalps-devel-$CLE_RPM_VERS not found.])
					fi
					ALPS_RPM_3=$(find $ALPS_RPM_DIR/cray-libalpsutil0-$CLE_RPM_VERS.[[0-9\.\-]]*.ari.x86_64.rpm 2>&AS_MESSAGE_LOG_FD)
					if test ! -e "$ALPS_RPM_3"; then
						AC_MSG_ERROR([ALPS RPM cray-libalpsutil0-$CLE_RPM_VERS not found.])
					fi
					ALPS_RPM_4=$(find $ALPS_RPM_DIR/cray-libalpsutil-devel-$CLE_RPM_VERS.[[0-9\.\-]]*.ari.x86_64.rpm 2>&AS_MESSAGE_LOG_FD)
					if test ! -e "$ALPS_RPM_4"; then
						AC_MSG_ERROR([ALPS RPM alps-devel-$CLE_RPM_VERS not found.])
					fi
				else
					AC_MSG_ERROR([ALPS RPM directory $ALPS_RPM_DIR not found.])
				fi
				
				dnl resolve the names of the xmlrpc rpms
				if test -d "$XMLRPC_RPM_DIR"; then
					XMLRPC_RPM_1=$(find $XMLRPC_RPM_DIR/cray-libxmlrpc-epi0-[[0-9\.\-]]*.x86_64.rpm 2>&AS_MESSAGE_LOG_FD)
					if test ! -e "$XMLRPC_RPM_1"; then
						AC_MSG_ERROR([XMLRPC RPM cray-libxmlrpc-epi0 not found.])
					fi
					XMLRPC_RPM_2=$(find $XMLRPC_RPM_DIR/cray-libxmlrpc-epi-devel-[[0-9\.\-]]*.x86_64.rpm 2>&AS_MESSAGE_LOG_FD)
					if test ! -e "$XMLRPC_RPM_1"; then
						AC_MSG_ERROR([XMLRPC RPM cray-libxmlrpc-epi-devel not found.])
					fi
				else
					AC_MSG_ERROR([XMLRPC RPM directory $XMLRPC_RPM_DIR not found.])
				fi
				
				AS_MKDIR_P([$_cray_tmpdir])
				cd $_cray_tmpdir
				$RPM2CPIO $ALPS_RPM_1 | $CPIO -idv >&AS_MESSAGE_LOG_FD 2>&AS_MESSAGE_LOG_FD
				$RPM2CPIO $ALPS_RPM_2 | $CPIO -idv >&AS_MESSAGE_LOG_FD 2>&AS_MESSAGE_LOG_FD
				$RPM2CPIO $ALPS_RPM_3 | $CPIO -idv >&AS_MESSAGE_LOG_FD 2>&AS_MESSAGE_LOG_FD
				$RPM2CPIO $ALPS_RPM_4 | $CPIO -idv >&AS_MESSAGE_LOG_FD 2>&AS_MESSAGE_LOG_FD
				$RPM2CPIO $XMLRPC_RPM_1 | $CPIO -idv >&AS_MESSAGE_LOG_FD 2>&AS_MESSAGE_LOG_FD
				$RPM2CPIO $XMLRPC_RPM_2 | $CPIO -idv >&AS_MESSAGE_LOG_FD 2>&AS_MESSAGE_LOG_FD
				cd ${CRAYTOOL_DIR}
			],
			[AC_MSG_RESULT([yes])])
	
	AC_ARG_VAR([READLINK], [Location of readlink])
	
	AC_PATH_PROG([READLINK], [readlink], [readlink])
	if test -z "$READLINK"; then
		AC_MSG_ERROR([readlink not found.])
	fi
	
	dnl We need to resolve the stage location since this will change with each alps version.
	_cray_alps_inst=$($READLINK -m ${_cray_tmpdir}/opt/cray/alps/*)
	
	AC_MSG_CHECKING([for presence of ALPS libraries])
	
	if test ! -d "${_cray_alps_inst}/lib64/"; then
		AC_MSG_RESULT([not found])
		AC_MSG_ERROR([ALPS lib directory $_cray_alps_inst/lib64/ not found.])
	fi
	
	if test ! -d "${_cray_alps_inst}/include/"; then
		AC_MSG_RESULT([not found])
		AC_MSG_ERROR([ALPS include directory $_cray_alps_inst/include/ not found.])
	fi
	
	if test ! -d "${_cray_tmpdir}/usr/lib64"; then
		AC_MSG_RESULT([not found])
		AC_MSG_ERROR([XMLRPC lib directory $_cray_tmpdir/usr/lib64 not found.])
	fi
	
	AC_MSG_RESULT([yes])
	
	AC_SUBST([ALPS_BASE], [${CRAYTOOL_EXTERNAL}/alps_base])
	AC_SUBST([ALPS_LDFLAGS], ["-L${_cray_alps_inst}/lib64 -L${_cray_tmpdir}/usr/lib64"])
	AC_SUBST([ALPS_CFLAGS], ["-I${_cray_alps_inst}/include"])
])

dnl
dnl create wlm_detect files in a staging location
dnl
AC_DEFUN([cray_SETUP_WLM_DETECT_RPMS],
[
	dnl Set this to the location of the wlm_detect RPMS we should build against
	WLM_DETECT_RPM_DIR="/cray/css/release/cray/build/xt/sles11sp3/x86_64/RB-5.2UP00-ari/working/latest/rpms/x86_64/"
	
	dnl Create a temporary directory to extract the wlm_detect libraries to
	_cray_tmpdir="${CRAYTOOL_EXTERNAL}/wlm_detect_base"
	
	AC_PROG_AWK
	
	AC_MSG_CHECKING([for wlm_detect stage directory])
	
	AS_IF(	[test ! -d "$_cray_tmpdir"],
			[	AC_MSG_RESULT([no])
				
				dnl check required execs
				AC_PROG_MKDIR_P
				
				AC_ARG_VAR([RPM2CPIO], [Location of rpm2cpio])
				AC_ARG_VAR([CPIO], [Location of cpio])
				
				AC_PATH_PROG([RPM2CPIO], [rpm2cpio], [rpm2cpio])
				if test -z "$RPM2CPIO"; then
					AC_MSG_ERROR([rpm2cpio not found.])
				fi
	
				AC_PATH_PROG([CPIO], [cpio], [cpio])
				if test -z "$CPIO"; then
					AC_MSG_ERROR([cpio not found.])
				fi
				
				AC_MSG_NOTICE([Creating wlm_detect staging directory in $_cray_tmpdir])
				
				dnl resolve the names of the wlm_detect rpms
				if test -d "$WLM_DETECT_RPM_DIR"; then
					WLM_DETECT_RPM_1=$(find $WLM_DETECT_RPM_DIR/cray-libwlm_detect0* 2>&AS_MESSAGE_LOG_FD)
					if test ! -e "$WLM_DETECT_RPM_1"; then
						AC_MSG_ERROR([wlm_detect rpm cray-libwlm_detect not found.])
					fi
					WLM_DETECT_RPM_2=$(find $WLM_DETECT_RPM_DIR/cray-libwlm_detect-devel-* 2>&AS_MESSAGE_LOG_FD)
					if test ! -e "$WLM_DETECT_RPM_2"; then
						AC_MSG_ERROR([wlm_detect rpm cray-libwlm_detect-devel not found.])
					fi
				else
					AC_MSG_ERROR([wlm_detect rpm directory $WLM_DETECT_RPM_DIR not found.])
				fi
				
				AS_MKDIR_P([$_cray_tmpdir])
				cd $_cray_tmpdir
				$RPM2CPIO $WLM_DETECT_RPM_1 | $CPIO -idv >&AS_MESSAGE_LOG_FD 2>&AS_MESSAGE_LOG_FD
				$RPM2CPIO $WLM_DETECT_RPM_2 | $CPIO -idv >&AS_MESSAGE_LOG_FD 2>&AS_MESSAGE_LOG_FD
				cd ${CRAYTOOL_DIR}
			],
			[AC_MSG_RESULT([yes])])
	
	dnl find libwlm_detect.so for awk
	WLM_DETECT_LIB_LOC=$(find $_cray_tmpdir -name libwlm_detect.so 2>&AS_MESSAGE_LOG_FD)
	WLM_DETECT_BASE_NAME=$(echo $WLM_DETECT_LIB_LOC | $AWK '{match($[1],/wlm_detect\/([[0-9a-f.-]]*.ari)\/lib64\/libwlm_detect.so/,a)}END{print a[[1]]}' 2>&AS_MESSAGE_LOG_FD)
	if test "x$WLM_DETECT_BASE_NAME" = x; then
		AC_MSG_ERROR([Could not obtain wlm_detect install directory name.])
	fi
	
	AC_MSG_CHECKING([for presence of wlm_detect libraries])
	
	if test ! -d "$_cray_tmpdir/opt/cray/wlm_detect/$WLM_DETECT_BASE_NAME/lib64/"; then
		AC_MSG_RESULT([not found])
		AC_MSG_ERROR([wlm_detect lib directory $_cray_tmpdir/opt/cray/wlm_detect/$WLM_DETECT_BASE_NAME/lib64/ not found.])
	fi
	
	if test ! -d "$_cray_tmpdir/opt/cray/wlm_detect/$WLM_DETECT_BASE_NAME/include/"; then
		AC_MSG_RESULT([not found])
		AC_MSG_ERROR([wlm_detect include directory $_cray_tmpdir/opt/cray/wlm_detect/$WLM_DETECT_BASE_NAME/include/ not found.])
	fi
	
	AC_MSG_RESULT([yes])
	
	AC_SUBST([WLM_DETECT_BASE], [${CRAYTOOL_EXTERNAL}/wlm_detect_base])
	AC_SUBST([WLM_DETECT_LDFLAGS], ["-L$_cray_tmpdir/opt/cray/wlm_detect/$WLM_DETECT_BASE_NAME/lib64"])
	AC_SUBST([WLM_DETECT_CFLAGS], ["-I$_cray_tmpdir/opt/cray/wlm_detect/$WLM_DETECT_BASE_NAME/include"])
])

dnl
dnl create slurm files in a staging location
dnl
AC_DEFUN([cray_SETUP_SLURM_RPMS],
[
	dnl Set this to the http address of the slurm RPMS we should build against
	dnl TODO: This might need more versions in the future.
	dnl SLURM_HTTP_RPM_SITE="http://download.buildservice.us.cray.com/native-slurm:/master/SLE_11_SP3/x86_64/"
	SLURM_HTTP_RPM_SITE="http://download.buildservice.us.cray.com/native-slurm:/master/SLE_11_SP3/x86_64/"

	dnl Create a temporary directory to extract the slurm libraries to
	_cray_tmpdir="${CRAYTOOL_EXTERNAL}/slurm_base"
	
	AC_PROG_AWK
	
	AC_MSG_CHECKING([for SLURM stage directory])

	AS_IF(	[test ! -d "$_cray_tmpdir"],
			[	AC_MSG_RESULT([no])
				
				dnl check required execs
				AC_PROG_MKDIR_P
				
				AC_ARG_VAR([WGET], [Location of wget])
				AC_PATH_PROG([WGET], [wget], [wget])
				if test -z "$WGET"; then
					AC_MSG_ERROR([wget not found.])
				fi
	
				AC_ARG_VAR([RPM2CPIO], [Location of rpm2cpio])
				AC_PATH_PROG([RPM2CPIO], [rpm2cpio], [rpm2cpio])
				if test -z "$RPM2CPIO"; then
					AC_MSG_ERROR([rpm2cpio not found.])
				fi

				AC_ARG_VAR([CPIO], [Location of cpio])
				AC_PATH_PROG([CPIO], [cpio], [cpio])
				if test -z "$CPIO"; then
					AC_MSG_ERROR([cpio not found.])
				fi
			
				AC_MSG_NOTICE([Creating SLURM staging directory in $_cray_tmpdir])
				
				dnl setup the staging directory
				AS_MKDIR_P([$_cray_tmpdir])
				cd $_cray_tmpdir
				
				dnl wget the rpms
				$WGET -r -np -nH --cut-dirs=5 -R index.html* -P . -A slurm-[[0-9]]*,slurm-devel-* $SLURM_HTTP_RPM_SITE >&AS_MESSAGE_LOG_FD 2>&AS_MESSAGE_LOG_FD
				
				dnl ensure the wget worked
				SLURM_RPM_1=$(find slurm-[[0-9]]* 2>&AS_MESSAGE_LOG_FD)
				if test ! -e "$SLURM_RPM_1"; then
					cd ${CRAYTOOL_DIR}
					AC_MSG_ERROR([slurm rpm from $SLURM_HTTP_RPM_SITE not found.])
				fi
				SLURM_RPM_2=$(find slurm-devel-*)
				if test ! -e "$SLURM_RPM_2"; then
					cd ${CRAYTOOL_DIR}
					AC_MSG_ERROR([slurm-devel rpm from $SLURM_HTTP_RPM_SITE not found.])
				fi
				
				dnl extract the rpms
				$RPM2CPIO $SLURM_RPM_1 | $CPIO -idv >&AS_MESSAGE_LOG_FD 2>&AS_MESSAGE_LOG_FD
				$RPM2CPIO $SLURM_RPM_2 | $CPIO -idv >&AS_MESSAGE_LOG_FD 2>&AS_MESSAGE_LOG_FD
				
				dnl find libslurm.so for awk
				SLURM_LIB_LOC=$(find $_cray_tmpdir -name libslurm.so 2>&AS_MESSAGE_LOG_FD)
				SLURM_BASE_NAME=$(echo $SLURM_LIB_LOC | $AWK '{match($[1],/slurm\/([[0-9a-f.-]]*.ari)\/lib64\/libslurm.so/,a)}END{print a[[1]]}' 2>&AS_MESSAGE_LOG_FD)
				if test "x$SLURM_BASE_NAME" = x; then
					cd ${CRAYTOOL_DIR}
					AC_MSG_ERROR([Could not obtain slurm install directory name.])
				fi
				
				dnl cleanup
				rm -f $SLURM_RPM_1
				rm -f $SLURM_RPM_2
				dnl get rid of libtool junk since it will screw us up
				rm -f $_cray_tmpdir/opt/slurm/$SLURM_BASE_NAME/lib64/*.la
				
				cd ${CRAYTOOL_DIR}
			],
			[AC_MSG_RESULT([yes])])
	
	dnl find libslurm.so for awk
	SLURM_LIB_LOC=$(find $_cray_tmpdir -name libslurm.so 2>&AS_MESSAGE_LOG_FD)
	SLURM_BASE_NAME=$(echo $SLURM_LIB_LOC | $AWK '{match($[1],/slurm\/([[0-9a-f.-]]*.ari)\/lib64\/libslurm.so/,a)}END{print a[[1]]}' 2>&AS_MESSAGE_LOG_FD)
	if test "x$SLURM_BASE_NAME" = x; then
		AC_MSG_ERROR([Could not obtain slurm install directory name.])
	fi
	
	AC_MSG_CHECKING([for presence of SLURM libraries])
	
	if test ! -d "$_cray_tmpdir/opt/slurm/$SLURM_BASE_NAME/lib64/"; then
		AC_MSG_RESULT([not found])
		AC_MSG_ERROR([SLURM lib directory $_cray_tmpdir/opt/slurm/$SLURM_BASE_NAME/lib64/ not found.])
	fi

	if test ! -d "$_cray_tmpdir/opt/slurm/$SLURM_BASE_NAME/include/"; then
		AC_MSG_RESULT([not found])
		AC_MSG_ERROR([SLURM include directory $_cray_tmpdir/opt/slurm/$SLURM_BASE_NAME/include/ not found.])
	fi
	
	AC_MSG_RESULT([yes])
	
	AC_SUBST([SLURM_BASE], [${CRAYTOOL_EXTERNAL}/slurm_base])
	AC_SUBST([SLURM_LDFLAGS], ["-L$_cray_tmpdir/opt/slurm/$SLURM_BASE_NAME/lib64"])
	AC_SUBST([SLURM_CFLAGS], ["-I$_cray_tmpdir/opt/slurm/$SLURM_BASE_NAME/include"])
])

