#
# cray_extensions.m4 Cray configure extensions.
#
# ©2012-2014 Cray Inc.  All Rights Reserved.
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
])

dnl
dnl create alps files in a staging location
dnl
AC_DEFUN([cray_SETUP_ALPS_RPMS],
[
	dnl Set this to the CLE base version we are buidling against. Note that this
	dnl does not include update level information.
	CLE_RPM_VERS="4.2"

	dnl Set this to the location of the alps RPMS we should build against
	ALPS_RPM_DIR="/cray/css/release/cray/build/alps_gemini/sles11sp1/x86_64/RB-4.2UP00/published/latest/RPMS/x86_64/"

	dnl Set this to the location of the xmlrpc-epi RPMS we should build against
	XMLRPC_RPM_DIR="/cray/css/release/cray/build/xt/sles11sp1/x86_64/RB-4.2-gem/working/latest-RB-4.2UP00/3rd-party/x86_64/"

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

	dnl Create a temporary directory to extract the alps libraries to
	_cray_curdir=$(pwd)
	_cray_stage="$_cray_curdir/external"
	_cray_tmpdir="$_cray_stage/alps_base"
	
	dnl ensure the top level stage directory exists
	if test ! -d "$_cray_stage"; then
		AS_MKDIR_P([$_cray_stage])
	fi
	
	AC_MSG_CHECKING([for ALPS stage directory])

	AS_IF(	[test ! -d "$_cray_tmpdir"],
			[	AC_MSG_RESULT([no])
				AC_MSG_NOTICE([Creating ALPS staging directory in $_cray_tmpdir])
				
				dnl resolve the names of the alps rpms
				if test -d "$ALPS_RPM_DIR"; then
					ALPS_RPM_1=$(find $ALPS_RPM_DIR/alps-$CLE_RPM_VERS.[[0-9\.\-]]*.gem.x86_64.rpm 2>&AS_MESSAGE_LOG_FD)
					if test ! -e "$ALPS_RPM_1"; then
						AC_MSG_ERROR([ALPS RPM alps-$CLE_RPM_VERS not found.])
					fi
					ALPS_RPM_2=$(find $ALPS_RPM_DIR/alps-app-devel-$CLE_RPM_VERS.[[0-9\.\-]]*.gem.x86_64.rpm 2>&AS_MESSAGE_LOG_FD)
					if test ! -e "$ALPS_RPM_2"; then
						AC_MSG_ERROR([ALPS RPM alps-app-devel-$CLE_RPM_VERS not found.])
					fi
					ALPS_RPM_3=$(find $ALPS_RPM_DIR/alps-devel-$CLE_RPM_VERS.[[0-9\.\-]]*.gem.x86_64.rpm 2>&AS_MESSAGE_LOG_FD)
					if test ! -e "$ALPS_RPM_3"; then
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
				$RPM2CPIO $XMLRPC_RPM_1 | $CPIO -idv >&AS_MESSAGE_LOG_FD 2>&AS_MESSAGE_LOG_FD
				$RPM2CPIO $XMLRPC_RPM_2 | $CPIO -idv >&AS_MESSAGE_LOG_FD 2>&AS_MESSAGE_LOG_FD
				cd $_cray_curdir
			],
			[AC_MSG_RESULT([yes])])
	
	AC_MSG_CHECKING([if ALPS libraries are setup])
	
	if test ! -d "$_cray_tmpdir/usr/lib/alps/"; then
		AC_MSG_RESULT([not found])
		AC_MSG_ERROR([ALPS lib directory $_cray_tmpdir/usr/lib/alps/ not found.])
	fi
	
	if test ! -d "$_cray_tmpdir/usr/include/"; then
		AC_MSG_RESULT([not found])
		AC_MSG_ERROR([ALPS include directory $_cray_tmpdir/usr/include/ not found.])
	fi
	
	if test ! -d "$_cray_tmpdir/usr/lib64/"; then
		AC_MSG_RESULT([not found])
		AC_MSG_ERROR([XMLRPC lib directory $_cray_tmpdir/usr/lib64/ not found.])
	fi
	
	AC_MSG_RESULT([yes])
	
	AC_SUBST([ALPS_LDFLAGS], ["-L$_cray_tmpdir/usr/lib/alps -L$_cray_tmpdir/usr/lib64"])
	AC_SUBST([ALPS_CFLAGS], ["-I$_cray_tmpdir/usr/include"])
])

dnl
dnl create slurm files in a staging location
dnl
AC_DEFUN([cray_SETUP_SLURM_RPMS],
[
	dnl Set this to the http address of the slurm RPMS we should build against
	dnl TODO: This might need more versions in the future.
	SLURM_HTTP_RPM_SITE="http://download.buildservice.us.cray.com/native-slurm:/trunk/SLE_11_SP2/x86_64/"

	AC_PROG_MKDIR_P
	AC_PROG_AWK
	
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

	dnl Create a temporary directory to extract the slurm libraries to
	_cray_curdir=$(pwd)
	_cray_stage="$_cray_curdir/external"
	_cray_tmpdir="$_cray_stage/slurm_base"
	
	dnl ensure the top level stage directory exists
	if test ! -d "$_cray_stage"; then
		AS_MKDIR_P([$_cray_stage])
	fi
	
	AC_MSG_CHECKING([for SLURM stage directory])

	AS_IF(	[test ! -d "$_cray_tmpdir"],
			[	AC_MSG_RESULT([no])
				AC_MSG_NOTICE([Creating SLURM staging directory in $_cray_tmpdir])
				
				dnl setup the staging directory
				AS_MKDIR_P([$_cray_tmpdir])
				cd $_cray_tmpdir
				
				dnl wget the rpms
				$WGET -r -np -nH --cut-dirs=5 -R index.html* -P . -A slurm-[[0-9]]*,slurm-devel-* $SLURM_HTTP_RPM_SITE >&AS_MESSAGE_LOG_FD 2>&AS_MESSAGE_LOG_FD
				
				dnl ensure the wget worked
				SLURM_RPM_1=$(find slurm-[[0-9]]* 2>&AS_MESSAGE_LOG_FD)
				if test ! -e "$SLURM_RPM_1"; then
					cd $_cray_curdir
					AC_MSG_ERROR([slurm rpm from $SLURM_HTTP_RPM_SITE not found.])
				fi
				SLURM_RPM_2=$(find slurm-devel-*)
				if test ! -e "$SLURM_RPM_2"; then
					cd $_cray_curdir
					AC_MSG_ERROR([slurm-devel rpm from $SLURM_HTTP_RPM_SITE not found.])
				fi
				$RPM2CPIO $SLURM_RPM_1 | $CPIO -idv >&AS_MESSAGE_LOG_FD 2>&AS_MESSAGE_LOG_FD
				$RPM2CPIO $SLURM_RPM_2 | $CPIO -idv >&AS_MESSAGE_LOG_FD 2>&AS_MESSAGE_LOG_FD
				cd $_cray_curdir
			],
			[AC_MSG_RESULT([yes])])
	
	dnl find the rpm again for awk
	cd $_cray_tmpdir
	SLURM_RPM_1=$(find slurm-[[0-9]]* 2>&AS_MESSAGE_LOG_FD)
	SLURM_BASE_NAME=$(echo $SLURM_RPM_1 | $AWK '{match($[1],/slurm-([[0-9a-f.-]]*.ari).x86_64.rpm/,a)}END{print a[[1]]}' 2>&AS_MESSAGE_LOG_FD)
	cd $_cray_curdir
	if test "x$SLURM_BASE_NAME" = x; then
		rm -r -f $_cray_tmpdir
		AC_MSG_ERROR([Could not obtain slurm base rpm name.])
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
	
	dnl get rid of libtool junk since it will screw us up
	rm -r -f $_cray_tmpdir/opt/slurm/$SLURM_BASE_NAME/lib64/*.la
	
	AC_SUBST([SLURM_LDFLAGS], ["-L$_cray_tmpdir/opt/slurm/$SLURM_BASE_NAME/lib64"])
	AC_SUBST([SLURM_CFLAGS], ["-I$_cray_tmpdir/opt/slurm/$SLURM_BASE_NAME/include"])
])

