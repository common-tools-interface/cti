#
# cray_extensions.m4 Cray configure extensions.
#
# ©2012 Cray Inc.  All Rights Reserved.
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
dnl
AC_DEFUN([cray_INIT],
[
	dnl Pull in the revision information from the release_versioning file
	m4_define([CRAYTOOL_RELEASE], [m4_esyscmd_s([. release_versioning; echo $craytool_major.$craytool_minor])])
	
	m4_define([CRAYTOOL_BE_CURRENT], [m4_esyscmd_s([. release_versioning; echo $be_current])])
	m4_define([CRAYTOOL_BE_REVISION], [m4_esyscmd_s([. release_versioning; echo $be_revision])])
	m4_define([CRAYTOOL_BE_AGE], [m4_esyscmd_s([. release_versioning; echo $be_age])])
	
	m4_define([CRAYTOOL_FE_CURRENT], [m4_esyscmd_s([. release_versioning; echo $fe_current])])
	m4_define([CRAYTOOL_FE_REVISION], [m4_esyscmd_s([. release_versioning; echo $fe_revision])])
	m4_define([CRAYTOOL_FE_AGE], [m4_esyscmd_s([. release_versioning; echo $fe_age])])
])

dnl
dnl create alps files in a staging location
dnl
AC_DEFUN([cray_SETUP_ALPS_RPMS],
[
	dnl Set this to the location of the alps RPMS we should build against
	ALPS_RPM_VERS="4.1"
	ALPS_RPM_DIR_DEF="/cray/css/release/cray/build/alps_gemini/sles11sp1/x86_64/RB-4.1/published/latest/RPMS/x86_64"
	ALPS_RPM_1="alps-4.1.0-2.0401.7442.1.gem.x86_64.rpm"
	ALPS_RPM_2="alps-app-devel-4.1.0-2.0401.7442.1.gem.x86_64.rpm"

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
	_cray_tmpdir="$_cray_curdir/alps_base/$ALPS_RPM_VERS"

	AS_IF(	[test ! -d "$_cray_tmpdir"],
			[	AS_MKDIR_P([$_cray_tmpdir])
				cd $_cray_tmpdir
				$RPM2CPIO $ALPS_RPM_DIR_DEF/$ALPS_RPM_1 | $CPIO -idv
				$RPM2CPIO $ALPS_RPM_DIR_DEF/$ALPS_RPM_2 | $CPIO -idv
				cd $_cray_curdir
				AC_MSG_NOTICE([Created ALPS staging location at $_cray_tmpdir])
			])
	
	AC_SUBST([ALPS_LIB_PATH], [$_cray_tmpdir/usr/lib/alps/])
	AC_SUBST([ALPS_INC_PATH], [$_cray_tmpdir/usr/include/alps/])
])
