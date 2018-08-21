################################################################################
# Copyright (c) 2010-2014 Krell Institute. All Rights Reserved.
#
# This program is free software; you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free Software
# Foundation; either version 2 of the License, or (at your option) any later
# version.
#
# This program is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
# FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
# details.
#
# You should have received a copy of the GNU General Public License along with
# this program; if not, write to the Free Software Foundation, Inc., 59 Temple
# Place, Suite 330, Boston, MA  02111-1307  USA
################################################################################


################################################################################
# Check for Dyninst (http://www.dyninst.org)
################################################################################

AC_DEFUN([AX_DYNINST], [

    AC_ARG_WITH(dyninst,
                AC_HELP_STRING([--with-dyninst=DIR],
                               [Dyninst installation @<:@/usr@:>@]),
                dyninst_dir=$withval, dyninst_dir="/usr")

    AC_ARG_WITH(dyninst-version,
                AC_HELP_STRING([--with-dyninst-version=VERS],
                               [dyninst-version installation @<:@8.2@:>@]),
                dyninst_vers=$withval, dyninst_vers="8.2")

    AC_ARG_WITH([dyninst-libdir],
                AS_HELP_STRING([--with-dyninst-libdir=LIB_DIR],
                [Force given directory for dyninst libraries. Note that this will overwrite library path detection, so use this parameter only if default library detection fails and you know exactly where your dyninst libraries are located.]),
                [
                if test -d $withval 
                then
                        ac_dyninst_lib_path="$withval"
                else
                        AC_MSG_ERROR(--with-dyninst-libdir expected directory name)
                fi ], 
                [ac_dyninst_lib_path=""])


    DYNINST_CPPFLAGS="-I$dyninst_dir/include -I$dyninst_dir/include/dyninst"
    if test "x$ac_dyninst_lib_path" == "x"; then
       DYNINST_LDFLAGS="-L$dyninst_dir/$abi_libdir"
       DYNINST_LIBDIR="$dyninst_dir/$abi_libdir"
    else
       DYNINST_LDFLAGS="-L$ac_dyninst_lib_path"
       DYNINST_LIBDIR="$ac_dyninst_lib_path"
    fi

    DYNINST_DIR="$dyninst_dir" 
    DYNINST_VERS="$dyninst_vers"

#   The default is to use 6.0 dyninst cppflags and libs.  
#   Change that (the default case entry) when you change the default vers to something other than 6.0
    case "$dyninst_vers" in
	"7.0.1")
            DYNINST_CPPFLAGS="$DYNINST_CPPFLAGS -DUSE_STL_VECTOR"
            DYNINST_LIBS="-ldyninstAPI -lcommon -lsymtabAPI -linstructionAPI -lparseAPI" 
            ;;
	"8.0.0")
            DYNINST_CPPFLAGS="$DYNINST_CPPFLAGS -DUSE_STL_VECTOR"
            DYNINST_LIBS="-ldyninstAPI -lcommon -lsymtabAPI -linstructionAPI -lparseAPI -lpatchAPI -lstackwalk -lpcontrol -ldynElf -ldynDwarf -lsymLite" 
	    DYNINST_SYMTABAPI_LIBS="-lcommon -lsymtabAPI -linstructionAPI -lparseAPI -ldynElf -ldynDwarf -lsymLite"
            ;;
	"8.1")
            DYNINST_CPPFLAGS="$DYNINST_CPPFLAGS -DUSE_STL_VECTOR"
            DYNINST_LIBS="-ldyninstAPI -lcommon -lsymtabAPI -linstructionAPI -lparseAPI -lpatchAPI -lstackwalk -lpcontrol -ldynElf -ldynDwarf -lsymLite" 
	    DYNINST_SYMTABAPI_LIBS="-lcommon -lsymtabAPI -linstructionAPI -lparseAPI -ldynElf -ldynDwarf -lsymLite"
            ;;
	"8.1.1")
            DYNINST_CPPFLAGS="$DYNINST_CPPFLAGS -DUSE_STL_VECTOR"
            DYNINST_LIBS="-ldyninstAPI -lcommon -lsymtabAPI -linstructionAPI -lparseAPI -lpatchAPI -lstackwalk -lpcontrol -ldynElf -ldynDwarf -lsymLite" 
	    DYNINST_SYMTABAPI_LIBS="-lcommon -lsymtabAPI -linstructionAPI -lparseAPI -ldynElf -ldynDwarf -lsymLite"
            ;;
	"8.1.2")
            DYNINST_CPPFLAGS="$DYNINST_CPPFLAGS -DUSE_STL_VECTOR -std=c++0x"
            DYNINST_LIBS="-ldyninstAPI -lcommon -lsymtabAPI -linstructionAPI -lparseAPI -lpatchAPI -lstackwalk -lpcontrol -ldynElf -ldynDwarf -lsymLite" 
	    DYNINST_SYMTABAPI_LIBS="-lcommon -lsymtabAPI -linstructionAPI -lparseAPI -ldynElf -ldynDwarf -lsymLite"
            ;;
	"8.2")
            DYNINST_CPPFLAGS="$DYNINST_CPPFLAGS -DUSE_STL_VECTOR -std=c++0x"
            DYNINST_LIBS="-ldyninstAPI -lcommon -lsymtabAPI -linstructionAPI -lparseAPI -lpatchAPI -lstackwalk -lpcontrol -ldynElf -ldynDwarf -lsymLite" 
	    DYNINST_SYMTABAPI_LIBS="-lcommon -lsymtabAPI -linstructionAPI -lparseAPI -ldynElf -ldynDwarf -lsymLite"
            ;;
	*)
            DYNINST_CPPFLAGS="$DYNINST_CPPFLAGS -DUSE_STL_VECTOR -std=c++0x"
            DYNINST_LIBS="-ldyninstAPI -lcommon -lsymtabAPI -linstructionAPI -lparseAPI -lpatchAPI -lstackwalk -lpcontrol -ldynElf -ldynDwarf -lsymLite" 
	    DYNINST_SYMTABAPI_LIBS="-lcommon -lsymtabAPI -linstructionAPI -lparseAPI -ldynElf -ldynDwarf -lsymLite"
            ;;
    esac


    AC_LANG_PUSH(C++)
    AC_REQUIRE_CPP

    dyninst_saved_CPPFLAGS=$CPPFLAGS
    dyninst_saved_LDFLAGS=$LDFLAGS
    dyninst_saved_LIBS=$LIBS

    CPPFLAGS="$CPPFLAGS $DYNINST_CPPFLAGS $BOOST_CPPFLAGS"
    LDFLAGS="$LDFLAGS $DYNINST_LDFLAGS $BINUTILS_LDFLAGS $LIBDWARF_LDFLAGS $LIBELF_LDFLAGS $BOOST_LDFLAGS"
    LIBS="$DYNINST_LIBS $BINUTILS_IBERTY_LIB $LIBDWARF_LIBS $LIBELF_LIBS $BOOST_SYSTEM_LIB $BOOST_THREAD_LIB"

    AC_MSG_CHECKING([for Dyninst API library and headers])

    AC_LINK_IFELSE([AC_LANG_PROGRAM([[
	    #include <BPatch.h>
        ]], [[
	    BPatch bpatch();
        ]])], [ AC_MSG_RESULT(yes)

            AM_CONDITIONAL(HAVE_DYNINST, true)
            AC_DEFINE(HAVE_DYNINST, 1, [Define to 1 if you have Dyninst.])

        ], [ AC_MSG_RESULT(no)

            AM_CONDITIONAL(HAVE_DYNINST, false)
            DYNINST_CPPFLAGS=""
            DYNINST_LDFLAGS=""
            DYNINST_LIBS=""
            DYNINST_LIBDIR=""
            DYNINST_SYMTABAPI_LIBS=""
            DYNINST_DIR=""
            DYNINST_VERS=""
        ]
    )

    CPPFLAGS=$dyninst_saved_CPPFLAGS
    LDFLAGS=$dyninst_saved_LDFLAGS
    LIBS=$dyninst_saved_LIBS

    AC_LANG_POP(C++)

    AC_SUBST(DYNINST_CPPFLAGS)
    AC_SUBST(DYNINST_LDFLAGS)
    AC_SUBST(DYNINST_LIBS)
    AC_SUBST(DYNINST_LIBDIR)
    AC_SUBST(DYNINST_SYMTABAPI_LIBS)
    AC_SUBST(DYNINST_DIR)
    AC_SUBST(DYNINST_VERS)

])

