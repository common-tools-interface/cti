#
# configure.ac script for the craytool interface.
#
# © 2010-2014 Cray Inc. All Rights Reserved
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

AC_PREREQ([2.63])

dnl We need to use the m4_esyscmd here because the Cray macro extensions are
dnl not pulled in until after AC_INIT. This is not portable...
AC_INIT([craytool_interface], [m4_esyscmd([source release_versioning; echo $craytool_major.$craytool_minor | tr -d '\n'])], [andrewg@cray.com])
AC_COPYRIGHT([© 2011-2014 Cray Inc. All Rights Reserved.])

AC_CONFIG_SRCDIR([src/cti_defs.h])
AC_CONFIG_AUX_DIR([config])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_HEADERS([config.h])

dnl Turn on Posix extensions
AC_USE_SYSTEM_EXTENSIONS

AM_INIT_AUTOMAKE([foreign -Wall])
LT_INIT

dnl Pull in Cray specific m4 extensions
cray_INIT

AC_SUBST([CRAYTOOL_DIR], ["`cd $srcdir && pwd`"])
AC_SUBST([CRAYTOOL_BE_VERSION], [CRAYTOOL_BE_CURRENT:CRAYTOOL_BE_REVISION:CRAYTOOL_BE_AGE])
AC_SUBST([CRAYTOOL_FE_VERSION], [CRAYTOOL_FE_CURRENT:CRAYTOOL_FE_REVISION:CRAYTOOL_FE_AGE])
AC_SUBST([CRAYTOOL_RELEASE_VERSION], [CRAYTOOL_RELEASE])

m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

#############################
### Check for --with tags ###
#############################
dnl Check for --with-libarchive
AC_ARG_WITH([libarchive],
	    [AS_HELP_STRING([--with-libarchive],
			    [Specify path to libarchive install.]
			    )
	    ],
	    [libarchive_path=$withval]
	    )

dnl Check for --with-libmi
AC_ARG_WITH([libmi],
			[AS_HELP_STRING([--with-libmi],
							[Specify path to libmi install.]
							)
			],
			[libmi_path=$withval]
			)

###########################
### Check for programs. ###
###########################
AC_PROG_CC
AC_PROG_CC_C99
AM_PROG_CC_C_O
AC_PROG_INSTALL
AC_PROG_LIBTOOL
AC_PROG_LN_S
AC_PROG_SED

#########################
### Setup env options ###
#########################

dnl This m4 macro will grab the required ALPS RPM from CSS and extract it with
dnl cpio. It will create varibales $ALPS_LIB_PATH and $ALPS_INC_PATH which point
dnl to the alps libraries and headers respectively. It will also modify LDFLAGS,
dnl CFLAGS, and CPPFLAGS.
cray_SETUP_ALPS_RPMS

dnl Ditto for the slurm libraries
cray_SETUP_SLURM_RPMS

dnl Ditto for the wlm_detect libraries
cray_SETUP_WLM_DETECT_RPMS

############################
### Check for libraries. ###
############################
dnl Check for libc
AC_CHECK_LIB(	[c],
	     		[free],
	     		[],
	     		[AC_MSG_FAILURE([libc not found!!!])]
	     		)

dnl Check for libalps
save_LDFLAGS="$LDFLAGS"
LDFLAGS="$LDFLAGS $ALPS_LDFLAGS"
AC_CHECK_LIB(	[alps], 
				[alpsGetMyNid],
				[AC_DEFINE(	[HAVE_LIBALPS],
								[1],
								[Define to 1 if you have the alps library (-lalps).]
							)
				],
				[AC_MSG_FAILURE([libalps not found.])],
				[-lxmlrpc-epi]
				)
dnl Check for libalpsutil
AC_CHECK_LIB(	[alpsutil], 
				[alps_get_placement_info],
				[AC_DEFINE(	[HAVE_LIBALPSUTIL],
								[1],
								[Define to 1 if you have the alpsutil library (-lalpsutil).]
							)
				],
				[AC_MSG_FAILURE([libalpsutil not found.])]
				)	
dnl restore flags
LDFLAGS="$save_LDFLAGS"

dnl Check for libslurm
dnl TODO: Add extra versions as needed
save_LDFLAGS="$LDFLAGS"
LDFLAGS="$LDFLAGS $SLURM_LDFLAGS"
AC_CHECK_LIB(	[slurm],
				[slurm_api_version],
				[AC_DEFINE(	[HAVE_SLURM],
								[1],
								[Define to 1 if you have the slurm library (-lslurm).]
							)
				],
				[AC_MSG_FAILURE([libslurm not found.])]
				)
AC_SUBST([SLURM_LIBS], ["-lslurm"])
dnl restore flags
LDFLAGS="$save_LDFLAGS"

dnl Check for libwlm_detect
save_LDFLAGS="$LDFLAGS"
LDFLAGS="$LDFLAGS $WLM_DETECT_LDFLAGS"
AC_CHECK_LIB(	[wlm_detect],
				[wlm_detect_get_default],
				[AC_DEFINE(	[HAVE_WLM_DETECT],
								[1],
								[Define to 1 if you have the wlm_detect library (-lwlm_detect).]
							)
				],
				[AC_MSG_FAILURE([libwlm_detect not found.])]
				)
AC_SUBST([WLM_DETECT_LIBS], ["-lwlm_detect"])
dnl restore flags
LDFLAGS="$save_LDFLAGS"

dnl Check for libarchive
save_LDFLAGS="$LDFLAGS"
if test -f "$libarchive_path/lib/libarchive.a"; then
	AC_SUBST([ARCHIVE_LDFLAGS], ["-L$libarchive_path/lib"])
	LDFLAGS="$LDFLAGS $ARCHIVE_LDFLAGS"
fi
AC_CHECK_LIB(	[archive],
				[archive_read_new],
				[AC_DEFINE(	[HAVE_ARCHIVE],
								[1],
								[Define to 1 if you have the archive library (-larchive).]
							)
				],
				[AC_MSG_ERROR([libarchive.a not found.])]
				)
AC_SUBST([ARCHIVE_LIBS], ["-larchive"])
dnl restore flags
LDFLAGS="$save_LDFLAGS"

dnl Check for libmi
save_LDFLAGS="$LDFLAGS"
if test -f "$libmi_path/lib/libmi.so"; then
	AC_SUBST([MI_LDFLAGS], ["-L$libmi_path/lib"])
	LDFLAGS="$LDFLAGS $MI_LDFLAGS"
fi
AC_CHECK_LIB(	[mi],
				[MICommandFree],
				[AC_DEFINE(	[HAVE_MI],
								[1],
								[Define to 1 if you have the mi library (-lmi).]
							)
				],
			 	[AC_MSG_ERROR([libmi.so not found.])]
			 	)
AC_SUBST([MI_LIBS], ["-lmi"])
dnl restore flags
LDFLAGS="$save_LDFLAGS"

###############################
### Check for header files. ###
###############################
AC_CHECK_HEADERS([fcntl.h inttypes.h limits.h netinet/in.h stdint.h stdlib.h string.h strings.h unistd.h])

dnl Check for alps.h
save_CFLAGS="$CFLAGS"
CFLAGS="$CFLAGS $ALPS_CFLAGS"
AC_CHECK_HEADER([alps/alps.h],
		[],
		[AC_MSG_FAILURE([alps/alps.h not found.])]
		)
dnl Check for alps_toolAssist.h
AC_CHECK_HEADER([alps/alps_toolAssist.h],
		[],
		[AC_MSG_FAILURE([alps/alps_toolAssist.h not found.])]
		)
dnl Check for apInfo.h
AC_CHECK_HEADER([alps/apInfo.h],
		[],
		[AC_MSG_FAILURE([alps/apInfo.h not found.])]
		)
dnl Check for libalps.h
AC_CHECK_HEADER([alps/libalps.h],
		[],
		[AC_MSG_FAILURE([alps/libalps.h not found.])],
		[#include <alps/alps_toolAssist.h>
		 #include <netinet/in.h>
		 ]
		)
dnl Check for libalpsutil.h
AC_CHECK_HEADER([alps/libalpsutil.h],
		[],
		[AC_MSG_FAILURE([alps/libalpsutil.h not found.])],
		[#include <alps/alps_toolAssist.h>
		 #include <netinet/in.h>
		 ]
		)
dnl restore flags
CFLAGS="$save_CFLAGS"

dnl Check for libslurm headers
save_CFLAGS="$CFLAGS"
save_CPPFLAGS="$CPPFLAGS"
CFLAGS="$CFLAGS $SLURM_CFLAGS"
CPPFLAGS="$CPPFLAGS $SLURM_CFLAGS"
AC_CHECK_HEADER(	[slurm/slurm.h],
					[],
					[AC_MSG_FAILURE([slurm/slurm.h not found.])]
					)
dnl restore flags
CFLAGS="$save_CFLAGS"
CPPFLAGS="$save_CPPFLAGS"

dnl Check for libwlm_detect headers
save_CFLAGS="$CFLAGS"
save_CPPFLAGS="$CPPFLAGS"
CFLAGS="$CFLAGS $WLM_DETECT_CFLAGS"
CPPFLAGS="$CPPFLAGS $WLM_DETECT_CFLAGS"
AC_CHECK_HEADER(	[wlm_detect.h],
					[],
					[AC_MSG_FAILURE([wlm_detect.h not found.])]
					)
dnl restore flags
CFLAGS="$save_CFLAGS"
CPPFLAGS="$save_CPPFLAGS"

dnl Check for libarchive headers
save_CFLAGS="$CFLAGS"
if test -f "$libarchive_path/include/archive.h"; then
	AC_SUBST([ARCHIVE_CFLAGS], ["-I$libarchive_path/include"])
	CFLAGS="$CFLAGS $ARCHIVE_CFLAGS"
fi
AC_CHECK_HEADER([archive.h],
		[],
		[AC_MSG_FAILURE([archive.h not found.])],
		[]
		)
dnl restore flags
CFLAGS="$save_CFLAGS"

dnl Check for libmi headers
save_CFLAGS="$CFLAGS"
if test -f "$libmi_path/include/MI.h"; then
	AC_SUBST([MI_CFLAGS], ["-I$libmi_path/include"])
	CFLAGS="$CFLAGS $MI_CFLAGS"
fi
AC_CHECK_HEADER([MI.h],
				[],
				[AC_MSG_FAILURE([MI.h not found.])],
				[]
				)
dnl restore flags
CFLAGS="$save_CFLAGS"

#####################################################################
### Check for typedefs, structures, and compiler characteristics. ###
#####################################################################
AC_TYPE_UID_T
AC_TYPE_INT32_T
AC_TYPE_INT64_T
AC_TYPE_PID_T
AC_TYPE_SIZE_T
AC_TYPE_UINT16_T
AC_TYPE_UINT32_T
AC_TYPE_UINT64_T

####################################
### Check for library functions. ###
####################################
AC_FUNC_FORK
AC_FUNC_MALLOC
AC_FUNC_REALLOC
AC_CHECK_FUNCS([dup2 memset setenv strdup strrchr strstr])

################
### Finalize ###
################
AC_CONFIG_FILES([	Makefile
					cray-tools_be.pc
					cray-tools_fe.pc
					cray-audit.pc
					src/Makefile
					src/backend/Makefile
					src/daemon/Makefile
					src/frontend/Makefile
					src/ld_val/Makefile
					src/mpir_iface/Makefile
					src/slurm_util/Makefile
					src/useful/Makefile
					demos/Makefile
])

AC_OUTPUT
