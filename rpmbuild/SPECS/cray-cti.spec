# Packaging definitions
%global pkgversion %(%{_sourcedir}/get_package_data --crayversion)
%global pkgversion_separator -
%global copyright (C) Copyright 2010-2020 Hewlett Packard Enterprise Development LP.

# RPM build time
%global release_date %(date +%%B\\ %%Y)

%global cray_product_prefix cray-
%global product cti
%global cray_product %{product}
%global cray_name %{cray_product_prefix}%{cray_product}

# Path definitions
%global cray_prefix /opt/cray/pe
%global test_prefix /opt/cray/tests

#FIXME: This should be relocatable
%global external_build_dir %{cray_prefix}/%{cray_product}/%{pkgversion}
%global tests_source_dir %{_topdir}/../tests

# modulefile definitions
%global modulefile_name %{cray_name}
%global devel_modulefile_name %{cray_name}%{pkgversion_separator}devel

%global module_template_name %{modulefile_name}.module.template
%global devel_module_template_name %{devel_modulefile_name}.module.template

# set_default modulefile definitions
%global set_default_command set_default
%global set_default_template_name %{set_default_command}.template
%global set_default_path admin-pe/set_default_files

# This file is sourced by craype to resolve dependencies for products without
# loading modules.
%global cray_dependency_resolver set_pkgconfig_default_%{cray_name}
%global cray_dependency_resolver_template_name %{cray_dependency_resolver}.template

# lmod modulefiles
%global lmod_template_cti template_%{product}.lua
%global lmod_template_cti_devel template_%{product}-devel.lua

# release info
%global release_info_name release_info
%global release_info_template_name %{release_info_name}.template

# yaml file
%global yaml_template yaml.template
%global removal_date %(date '+%Y-%m-%d' -d "+5 years")

# copyright file
%global copyright_name COPYRIGHT

# attributions file
%global attributions_name ATTRIBUTIONS_cti.txt

# dso list of files added to pe cache
%global cray_dso_list .cray_dynamic_file_list

# cdst-support version
%global cdst_support_pkgversion_min %(%{_sourcedir}/get_package_data --cdstversionmin)
%global cdst_support_pkgversion_max %(%{_sourcedir}/get_package_data --cdstversionmax)

# Disable debug package
%global debug_package %{nil}

# Disable stripping of binaries
# Particularly, backend daemon's hash is hardcoded into frontend library, don't want RPM to strip
# or modify the binary
%global __os_install_post %{nil}

# System strip command may be too old, use current path
%global __strip strip

# Filter requires - these all come from the cdst-support rpm
# These should match what is excluded in the cdst-support rpm specfile!
%global privlibs             libboost.*
%global privlibs %{privlibs}|libarchive
%global privlibs %{privlibs}|libasm
%global privlibs %{privlibs}|libcommon
%global privlibs %{privlibs}|libdyn.*
%global privlibs %{privlibs}|libinstructionAPI
%global privlibs %{privlibs}|libInst
%global privlibs %{privlibs}|libparseAPI
%global privlibs %{privlibs}|libpatchAPI
%global privlibs %{privlibs}|libpcontrol
%global privlibs %{privlibs}|libstackwalk
%global privlibs %{privlibs}|libsymLite
%global privlibs %{privlibs}|libsymtabAPI
%global privlibs %{privlibs}|libdw
%global privlibs %{privlibs}|libebl.*
%global privlibs %{privlibs}|libelf
%global privlibs %{privlibs}|libssh2
%global privlibs %{privlibs}|libtbb.*
%global __requires_exclude ^(%{privlibs})\\.so*

# Dist tags for SuSE need to be manually set
%if 0%{?suse_version}
%if 0%{?sle_version} == 150000
%global dist .sles15
%global OS_HW_TAG 7.0,7.1
%global OS_WB_TAG sles15
%endif
%if 0%{?sle_version} == 150100
%global dist .sles15sp1
%global OS_HW_TAG 7.0,7.1
%global OS_WB_TAG sles15sp1
%endif
%endif

%if %{_arch} == aarch64
%global SYS_HW_TAG AARCH64
%global SYS_WB_TAG AARCH64
%endif
%if %{_arch} == x86_64
%global SYS_HW_TAG HARDWARE
%global SYS_WB_TAG WHITEBOX
%endif
%if 0%{?rhel} == 8
%global OS_HW_TAG el8
%global OS_WB_TAG el8
%endif

Summary:    Cray Common Tools Interface
Name:       %{cray_name}%{pkgversion_separator}%{pkgversion}
# BUILD_METADATA is set by Jenkins
Version:    %(echo ${BUILD_METADATA})
Release:    1%{dist}
Prefix:     %{cray_prefix}
License:    Dual BSD or GPLv2
Vendor:     Cray Inc.
Group:      Development/System
Provides:   %{cray_name} = %{pkgversion}
Requires:   set_default_2, cray-cdst-support >= %{cdst_support_pkgversion_min}, cray-cdst-support < %{cdst_support_pkgversion_max}
Source0:    %{module_template_name}
Source1:    %{devel_module_template_name}
Source2:    %{set_default_template_name}
Source3:    %{cray_dependency_resolver_template_name}
Source4:    %{release_info_template_name}
Source5:    %{copyright_name}
Source6:    %{attributions_name}
Source7:    %{lmod_template_cti}
Source9:    %{yaml_template}

%description
Cray Common Tools Interface %{pkgversion}
Certain components, files or programs contained within this package or product are %{copyright}

%package -n %{cray_name}-devel-%{pkgversion}
Summary:    Cray Common Tools Interface development files
Group:      Development
Provides:   %{cray_name}-devel = %{pkgversion}
Requires:   set_default_2, cray-gcc-8.1.0, %{cray_name} = %{pkgversion}
Source8:    %{lmod_template_cti_devel}
%description -n %{cray_name}-devel-%{pkgversion}
Development files for Cray Common Tools Interface

%package -n %{cray_name}-tests-%{pkgversion}
Summary:    Cray Common Tools Interface test binariess
Group:      Development
Provides:   %{cray_name}-tests = %{pkgversion}
Requires:   cray-gcc-8.1.0, cray-cdst-support-devel >= %{cdst_support_pkgversion_min}, cray-cdst-support-devel < %{cdst_support_pkgversion_max}, %{cray_name} = %{pkgversion}, %{cray_name}-devel = %{pkgversion}
%description -n %{cray_name}-tests-%{pkgversion}
Test files for Cray Common Tools Interface


%prep
# Run q(uiet) with build directory name, c(reate) subdirectory, disable T(arball) unpacking
%setup -q -n %{name} -c -T
%build
# external build
%{__sed} 's|<RELEASE_DATE>|%{release_date}|g;s|<VERSION>|%{pkgversion}|g;s|<RELEASE>|%{release}|g;s|<date>\.<REVISION>|%{version}|g;s|<COPYRIGHT>|%{copyright}|g;s|<CRAY_NAME>|%{cray_name}|g;s|<CRAY_PREFIX>|%{cray_prefix}|g;s|<ARCH>|%{_target_cpu}|g' %{SOURCE4} > ${RPM_BUILD_DIR}/%{release_info_name}
%{__cp} -a %{SOURCE5} ${RPM_BUILD_DIR}/%{copyright_name}
%{__cp} -a %{SOURCE6} ${RPM_BUILD_DIR}/%{attributions_name}

%install
# copy files from external install
#
# Cray PE package root
%{__install} -d ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}
# Information files
%{__cp} -a ${RPM_BUILD_DIR}/%{release_info_name} ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/%{release_info_name}
%{__cp} -a ${RPM_BUILD_DIR}/%{copyright_name} ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/%{copyright_name}
%{__cp} -a ${RPM_BUILD_DIR}/%{attributions_name} ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/%{attributions_name}
# Libraries
%{__install} -d ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/lib
%{__cp} -a %{external_build_dir}/lib/libaudit.so ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/lib
%{__cp} -a %{external_build_dir}/lib/libcommontools_be.so* ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/lib
%{__cp} -a %{external_build_dir}/lib/libcommontools_fe.so* ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/lib
# Libraries static
# pkg-config
%{__install} -d ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/lib/pkgconfig
%{__cp} -a %{external_build_dir}/lib/pkgconfig/common_tools_be.pc ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/lib/pkgconfig
%{__cp} -a %{external_build_dir}/lib/pkgconfig/common_tools_fe.pc ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/lib/pkgconfig
# Headers
%{__install} -d ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/include
%{__cp} -a %{external_build_dir}/include/common_tools_be.h ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/include
%{__cp} -a %{external_build_dir}/include/common_tools_fe.h ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/include
%{__cp} -a %{external_build_dir}/include/common_tools_shared.h ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/include
%{__cp} -a %{external_build_dir}/include/common_tools_version.h ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/include
# libexec binaries
%{__install} -d ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/libexec
%{__cp} -a %{external_build_dir}/libexec/cti_be_daemon%{pkgversion} ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/libexec
%{__cp} -a %{external_build_dir}/libexec/cti_fe_daemon%{pkgversion} ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/libexec
# Binaries
# Share
# modulefile
%{__install} -d ${RPM_BUILD_ROOT}/%{prefix}/modulefiles/%{modulefile_name}
%{__sed} 's|<PREFIX>|%{external_build_dir}|g;s|<CRAY_NAME>|%{cray_name}|g;s|<VERSION>|%{pkgversion}|g' %{SOURCE0} > ${RPM_BUILD_ROOT}/%{prefix}/modulefiles/%{modulefile_name}/%{pkgversion}
# devel modulefile
%{__install} -d ${RPM_BUILD_ROOT}/%{prefix}/modulefiles/%{devel_modulefile_name}
%{__sed} 's|<PREFIX>|%{external_build_dir}|g;s|<CRAY_NAME>|%{devel_modulefile_name}|g;s|<CRAY_BASE_NAME>|%{cray_name}|g;s|<VERSION>|%{pkgversion}|g' %{SOURCE1} > ${RPM_BUILD_ROOT}/%{prefix}/modulefiles/%{devel_modulefile_name}/%{pkgversion}
# Cray PE dependency resolver
%{__sed} 's|<CRAY_PREFIX>|%{cray_prefix}|g;s|<PKG_VERSION>|%{pkgversion}|g;s|<PE_PRODUCT>|%{cray_product}|g' %{SOURCE3} > ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/%{cray_dependency_resolver}
# set_default scripts
%{__sed} 's|<PREFIX>|%{prefix}|g;s|<CRAY_PRODUCT>|%{cray_product}|g;s|<VERSION>|%{pkgversion}|g;s|<MODULEFILE_NAME>|%{modulefile_name}|g' %{SOURCE2} > ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/%{set_default_command}_%{cray_name}_%{pkgversion}
%{__sed} 's|<PREFIX>|%{prefix}|g;s|<CRAY_PRODUCT>|%{cray_product}|g;s|<VERSION>|%{pkgversion}|g;s|<MODULEFILE_NAME>|%{devel_modulefile_name}|g' %{SOURCE2} > ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/%{set_default_command}_%{devel_modulefile_name}_%{pkgversion}
# set_default into admin-pe
%{__install} -d ${RPM_BUILD_ROOT}/%{prefix}/%{set_default_path}
%{__install} -D ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/%{set_default_command}_%{cray_name}_%{pkgversion} ${RPM_BUILD_ROOT}/%{prefix}/%{set_default_path}/%{set_default_command}_%{cray_name}_%{pkgversion}
%{__install} -D ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/%{set_default_command}_%{devel_modulefile_name}_%{pkgversion} ${RPM_BUILD_ROOT}/%{prefix}/%{set_default_path}/%{set_default_command}_%{devel_modulefile_name}_%{pkgversion}
# lmod modulefile
%{__install} -d ${RPM_BUILD_ROOT}/%{prefix}/lmod/modulefiles/core/%{cray_name}
%{__sed} 's|\[@%PREFIX_PATH%@\]|%{prefix}|g;s|\[@%MODULE_VERSION%@\]|%{pkgversion}|g' %{SOURCE7} > ${RPM_BUILD_ROOT}/%{prefix}/lmod/modulefiles/core/%{cray_name}/%{pkgversion}.lua
%{__install} -d ${RPM_BUILD_ROOT}/%{prefix}/lmod/modulefiles/core/%{devel_modulefile_name}
%{__sed} 's|\[@%PREFIX_PATH%@\]|%{prefix}|g;s|\[@%MODULE_VERSION%@\]|%{pkgversion}|g' %{SOURCE8} > ${RPM_BUILD_ROOT}/%{prefix}/lmod/modulefiles/core/%{devel_modulefile_name}/%{pkgversion}.lua
%{__mkdir} -p %{_rpmdir}/%{_arch}
# yaml file - cray-cti
%{__sed} 's|<PRODUCT>|%{cray_name}|g;s|<VERSION>|%{pkgversion}|g;s|<BUILD_METADATA>|%{version}|g;s|<RELEASE>|%{release}|g;s|<ARCH>|%{_arch}|g;s|<REMOVAL_DATE>|%{removal_date}|g;s|<SYS_HW_TAG>|%{SYS_HW_TAG}|g;s|<SYS_WB_TAG>|%{SYS_WB_TAG}|g;s|<OS_HW_TAG>|%{OS_HW_TAG}|g;s|<OS_WB_TAG>|%{OS_WB_TAG}|g' %{SOURCE9} > %{_rpmdir}/%{_arch}/%{cray_name}-%{pkgversion}-%{version}-%{release}.%{_arch}.rpm.yaml
# yaml file - cray-cti-devel
%{__sed} 's|<PRODUCT>|%{cray_name}-devel|g;s|<VERSION>|%{pkgversion}|g;s|<BUILD_METADATA>|%{version}|g;s|<RELEASE>|%{release}|g;s|<ARCH>|%{_arch}|g;s|<REMOVAL_DATE>|%{removal_date}|g;s|<SYS_HW_TAG>|%{SYS_HW_TAG}|g;s|<SYS_WB_TAG>|%{SYS_WB_TAG}|g;s|<OS_HW_TAG>|%{OS_HW_TAG}|g;s|<OS_WB_TAG>|%{OS_WB_TAG}|g' %{SOURCE9} > %{_rpmdir}/%{_arch}/%{cray_name}-devel-%{pkgversion}-%{version}-%{release}.%{_arch}.rpm.yaml
# yaml file - cray-cti-tests
%global start_rmLine %(sed -n /section-3/= %{SOURCE9})
%global end_rmLine %(sed -n /admin-pe/= %{SOURCE9} | tail -1)
%{__sed} '%{start_rmLine},%{end_rmLine}d;s|section-5|section-3|g;s|<PRODUCT>|%{cray_name}-tests|g;s|<VERSION>|%{pkgversion}|g;s|<BUILD_METADATA>|%{version}|g;s|<RELEASE>|%{release}|g;s|<ARCH>|%{_arch}|g;s|<REMOVAL_DATE>|%{removal_date}|g;s|<SYS_HW_TAG>|%{SYS_HW_TAG}|g;s|<SYS_WB_TAG>|%{SYS_WB_TAG}|g;s|<OS_HW_TAG>|%{OS_HW_TAG}|g;s|<OS_WB_TAG>|%{OS_WB_TAG}|g;'/admin-pe'/d' %{SOURCE9} > %{_rpmdir}/%{_arch}/%{cray_name}-tests-%{pkgversion}-%{version}-%{release}.%{_arch}.rpm.yaml
# Test files
%{__install} -d ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests
%{__cp} -a %{tests_source_dir}/configure.ac             ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/configure.ac
%{__cp} -a %{tests_source_dir}/Makefile.am              ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/Makefile.am
%{__cp} -a %{tests_source_dir}/test_tool_starter.py     ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/test_tool_starter.py
%{__cp} -a %{tests_source_dir}/test_tool_config.yaml    ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/test_tool_config.yaml
%{__cp} -a %{tests_source_dir}/README.md                ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/README.md

%{__install} -d ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/function
%{__cp} -a %{tests_source_dir}/function/avocado_config.yaml      ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/function/avocado_config.yaml
%{__cp} -a %{tests_source_dir}/function/tests.py                 ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/function/tests.py
%{__cp} -a %{tests_source_dir}/function/Makefile.am              ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/function/Makefile.am
%{__cp} -a %{tests_source_dir}/function/README.md                ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/function/README.md

%{__install} -d ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/function/bin
%{__cp} -a %{tests_source_dir}/function/bin/cti_barrier            ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/function/bin/cti_barrier
%{__cp} -a %{tests_source_dir}/function/bin/cti_callback_daemon    ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/function/bin/cti_callback_daemon
%{__cp} -a %{tests_source_dir}/function/bin/cti_callback           ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/function/bin/cti_callback
%{__cp} -a %{tests_source_dir}/function/bin/cti_info               ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/function/bin/cti_info
%{__cp} -a %{tests_source_dir}/function/bin/cti_kill               ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/function/bin/cti_kill
%{__cp} -a %{tests_source_dir}/function/bin/cti_launch             ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/function/bin/cti_launch
%{__cp} -a %{tests_source_dir}/function/bin/cti_link               ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/function/bin/cti_link
%{__cp} -a %{tests_source_dir}/function/bin/cti_mpmd               ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/function/bin/cti_mpmd
%{__cp} -a %{tests_source_dir}/function/bin/cti_wlm                ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/function/bin/cti_wlm
%{__cp} -a %{tests_source_dir}/function/bin/function_tests         ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/function/bin/function_tests
%{__cp} -a %{tests_source_dir}/function/bin/Makefile.am.dummy      ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/function/bin/Makefile.am

%{__install} -d ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/function/src
%{__cp} -a %{tests_source_dir}/function/src/Makefile.am              ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/function/src/Makefile.am
%{__cp} -a %{tests_source_dir}/function/src/hello_mpi.c              ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/function/src/hello_mpi.c
%{__cp} -a %{tests_source_dir}/function/src/hello_mpi_wait.c         ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/function/src/hello_mpi_wait.c
%{__cp} -a %{tests_source_dir}/function/src/mpi_wrapper.c            ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/function/src/mpi_wrapper.c
%{__cp} -a %{tests_source_dir}/function/src/mpmd.conf                ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/function/src/mpmd.conf
%{__cp} -a %{tests_source_dir}/function/src/testing.info             ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/function/src/testing.info
%{__cp} -a %{tests_source_dir}/function/src/inputFileData.txt        ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/function/src/inputFileData.txt

%{__install} -d ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/test_support
%{__cp} -a %{tests_source_dir}/test_support/Makefile.am        ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/test_support/Makefile.am
%{__cp} -a %{tests_source_dir}/test_support/one_print.c        ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/test_support/one_print.c
%{__cp} -a %{tests_source_dir}/test_support/one_socket.c       ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/test_support/one_socket.c
%{__cp} -a %{tests_source_dir}/test_support/two_socket.c       ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/test_support/two_socket.c
%{__cp} -a %{tests_source_dir}/test_support/remote_filecheck.c ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/test_support/remote_filecheck.c

%{__install} -d ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/test_support/message_one
%{__cp} -a %{tests_source_dir}/test_support/message_one/libmessage.so ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/test_support/message_one/libmessage.so
%{__cp} -a %{tests_source_dir}/test_support/message_one/message.c     ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/test_support/message_one/message.c
%{__cp} -a %{tests_source_dir}/test_support/message_one/message.h     ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/test_support/message_one/message.h

%{__install} -d ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/test_support/message_two
%{__cp} -a %{tests_source_dir}/test_support/message_two/libmessage.so ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/test_support/message_two/libmessage.so
%{__cp} -a %{tests_source_dir}/test_support/message_two/message.c     ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/test_support/message_two/message.c
%{__cp} -a %{tests_source_dir}/test_support/message_two/message.h     ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/test_support/message_two/message.h

%{__install} -d ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/test_support/googletest
%{__install} -d ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/test_support/googletest/googletest
%{__install} -d ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/test_support/googletest/googletest/src
%{__install} -d ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/test_support/googletest/googletest/include
%{__cp} -a %{tests_source_dir}/test_support/googletest/googletest/src/* ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/test_support/googletest/googletest/src
%{__cp} -a %{tests_source_dir}/test_support/googletest/googletest/include/* ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/test_support/googletest/googletest/include
%{__install} -d ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/test_support/googletest/googlemock
%{__install} -d ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/test_support/googletest/googlemock/src
%{__install} -d ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/test_support/googletest/googlemock/include
%{__cp} -a %{tests_source_dir}/test_support/googletest/googlemock/src/* ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/test_support/googletest/googlemock/src
%{__cp} -a %{tests_source_dir}/test_support/googletest/googlemock/include/* ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/test_support/googletest/googlemock/include

# Touch the cray dynamic file list which will be populated/updated post-install
touch ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/%{cray_dso_list}

%post
# Generate a list of dynamic shared objects (libraries) to add to Cray PE cache
find ${RPM_INSTALL_PREFIX}/%{cray_product}/%{pkgversion} -name '*.so*' > ${RPM_INSTALL_PREFIX}/%{cray_product}/%{pkgversion}/%{cray_dso_list}

# Add Shasta configuration files for PE content projection
if [ "${RPM_INSTALL_PREFIX}" = "%{prefix}" ]; then
    mkdir -p /etc/%{prefix}/admin-pe/bindmount.conf.d/
    mkdir -p /etc/%{prefix}/admin-pe/modulepaths.conf.d/
    echo "%{prefix}/%{product}/%{pkgversion}" > /etc/%{prefix}/admin-pe/bindmount.conf.d/%{cray_name}.conf
    echo "%{prefix}/modulefiles/%{modulefile_name}" > /etc/%{prefix}/admin-pe/modulepaths.conf.d/%{modulefile_name}.conf
    echo "%{prefix}/lmod/modulefiles/core/%{cray_name}" >> /etc/%{prefix}/admin-pe/modulepaths.conf.d/%{modulefile_name}.conf

    echo -e '#%Modulefile\r\nset  ModulesVersion "%{pkgversion}"' > %{prefix}/lmod/modulefiles/core/%{cray_name}/.version
fi

# Process relocations
if [ "${RPM_INSTALL_PREFIX}" != "%{prefix}" ]
then
    # Modulefile
    %{__sed} -i "s|^\([[:space:]]*set CTI_BASEDIR[[:space:]]*\)\(.*\)|\1${RPM_INSTALL_PREFIX}/%{cray_product}/%{pkgversion}|" ${RPM_INSTALL_PREFIX}/modulefiles/%{modulefile_name}/%{pkgversion}
    # set default command
    %{__sed} -i "s|^\(export CRAY_inst_dir=\).*|\1${RPM_INSTALL_PREFIX}|" ${RPM_INSTALL_PREFIX}/%{cray_product}/%{pkgversion}/%{set_default_command}_%{cray_name}_%{pkgversion}
else
    # Set default - TCL
    ${RPM_INSTALL_PREFIX}/%{set_default_path}/%{set_default_command}_%{cray_name}_%{pkgversion}

    # Don't want to set LD_LIBRARY_PATH if we are not relocating since rpath was set properly
    # tcl module
    %{__sed} -i "/^ prepend-path[[:space:]]*LD_LIBRARY_PATH.*/d" ${RPM_INSTALL_PREFIX}/modulefiles/%{modulefile_name}/%{pkgversion}
    # lua module
    %{__sed} -i "/^prepend-path[[:space:]]*LD_LIBRARY_PATH.*/d" ${RPM_INSTALL_PREFIX}/lmod/modulefiles/core/%{cray_name}/%{pkgversion}.lua
fi

# run ldconfig for good measure
if [ -w /etc/ld.so.cache ]
then
    if [[ ${RPM_INSTALL_PREFIX} = "%{prefix}" ]]
    then
        /sbin/ldconfig
    fi
fi

%post -n %{cray_name}-devel-%{pkgversion}
# Add Shasta configuration files for PE content projection
if [ "${RPM_INSTALL_PREFIX}" = "%{prefix}" ]; then
    %{__mkdir} -p /etc/%{prefix}/admin-pe/modulepaths.conf.d/
    echo "%{prefix}/modulefiles/%{devel_modulefile_name}" > /etc/%{prefix}/admin-pe/modulepaths.conf.d/%{devel_modulefile_name}.conf
    echo "%{prefix}/lmod/modulefiles/core/%{devel_modulefile_name}" >> /etc/%{prefix}/admin-pe/modulepaths.conf.d/%{devel_modulefile_name}.conf

    echo -e '#%Modulefile\r\nset  ModulesVersion "%{pkgversion}"' > %{prefix}/lmod/modulefiles/core/%{devel_modulefile_name}/.version
fi

# Process relocations
if [ "${RPM_INSTALL_PREFIX}" != "%{prefix}" ]
then
    # Modulefile for the devel package
    %{__sed} -i "s|^\([[:space:]]*set CTI_BASEDIR[[:space:]]*\)\(.*\)|\1${RPM_INSTALL_PREFIX}/%{cray_product}/%{pkgversion}|" ${RPM_INSTALL_PREFIX}/modulefiles/%{devel_modulefile_name}/%{pkgversion}
    # pkg-config pc files
    find ${RPM_INSTALL_PREFIX}/%{cray_product}/%{pkgversion}/lib/pkgconfig -name '*.pc' -exec %{__sed} -i "s|%{prefix}|${RPM_INSTALL_PREFIX}|g" {} \;
    # set default command
    %{__sed} -i "s|^\(export CRAY_inst_dir=\).*|\1${RPM_INSTALL_PREFIX}|" ${RPM_INSTALL_PREFIX}/%{cray_product}/%{pkgversion}/%{set_default_command}_%{devel_modulefile_name}_%{pkgversion}

    # dependency resolver
    %{__sed} -i "s|^\(set install_root \).*|\1${RPM_INSTALL_PREFIX}|" ${RPM_INSTALL_PREFIX}/%{cray_product}/%{pkgversion}/%{cray_dependency_resolver}
else
    # set default
    ${RPM_INSTALL_PREFIX}/%{set_default_path}/%{set_default_command}_%{devel_modulefile_name}_%{pkgversion}
fi

%preun
default_link="${RPM_INSTALL_PREFIX}/%{cray_product}/default"

# Cleanup module .version if it points to this version
if [ -f ${RPM_INSTALL_PREFIX}/modulefiles/%{modulefile_name}/.version ]
then
  dotversion=`grep ModulesVersion ${RPM_INSTALL_PREFIX}/modulefiles/%{modulefile_name}/.version | cut -f2 -d'"'`

  if [ "$dotversion" == "%{pkgversion}" ]
  then
    %{__rm} -f ${RPM_INSTALL_PREFIX}/modulefiles/%{modulefile_name}/.version
    echo "Uninstalled version and .version file match = ${default_version}."
    echo "Removing %{modulefile_name} .version file."
    %{__rm} -f ${default_link}
  fi
fi

%postun
if [ $1 == 1 ]
then
  exit 0
fi

# run ldconfig for good measure to ensure ldcache is cleaned up
if [ -w /etc/ld.so.cache ]
then
    if [[ ${RPM_INSTALL_PREFIX} = "%{prefix}" ]]
    then
        /sbin/ldconfig
    fi
fi

# If the install dir exists
if [[ -z `ls ${RPM_INSTALL_PREFIX}/%{cray_product}` ]]; then
  %{__rm} -rf ${RPM_INSTALL_PREFIX}/%{cray_product}
  if [ -f /etc%{prefix}/admin-pe/bindmount.conf.d/%{cray_name}.conf ]; then
    %{__rm} -rf /etc%{prefix}/admin-pe/bindmount.conf.d/%{cray_name}.conf
  fi
  if [ -f /etc%{prefix}/admin-pe/modulepaths.conf.d/%{cray_name}.conf ]; then
    %{__rm} -rf /etc%{prefix}/admin-pe/modulepaths.conf.d/%{cray_name}.conf
  fi
  if [ -d ${RPM_INSTALL_PREFIX}/lmod/modulefiles/core/%{cray_name} ]; then
    %{__rm} -rf ${RPM_INSTALL_PREFIX}/lmod/modulefiles/core/%{cray_name}
  fi
  if [ -d ${RPM_INSTALL_PREFIX}/modulefiles/%{cray_name} ]; then
    %{__rm} -rf ${RPM_INSTALL_PREFIX}/modulefiles/%{cray_name}
  fi
fi

%postun -n %{cray_name}-devel-%{pkgversion}
if [ $1 == 1 ]
then
  exit 0
fi

# If the install dir exists
if [ -f /etc%{prefix}/admin-pe/modulepaths.conf.d/%{devel_modulefile_name}.conf ]; then
  %{__rm} -rf /etc%{prefix}/admin-pe/modulepaths.conf.d/%{devel_modulefile_name}.conf
fi
if [ -d ${RPM_INSTALL_PREFIX}/lmod/modulefiles/core/%{devel_modulefile_name} ]; then
  %{__rm} -rf ${RPM_INSTALL_PREFIX}/lmod/modulefiles/core/%{devel_modulefile_name}
fi
if [ -d ${RPM_INSTALL_PREFIX}/modulefiles/%{devel_modulefile_name} ]; then
  %{__rm} -rf ${RPM_INSTALL_PREFIX}/modulefiles/%{devel_modulefile_name}
fi

%postun -n %{cray_name}-tests-%{pkgversion}
if [ $1 == 1 ]
then
  exit 0
fi

# If the install dir exists
if [ -d %{prefix}/%{cray_product}/%{pkgversion}/tests/test-support/googletest/googlemock ]; then
  %{__rm} -rf %{prefix}/%{cray_product}/%{pkgversion}/tests/test-support/googletest/googlemock
fi
if [ -d %{prefix}/%{cray_product}/%{pkgversion}/tests/test-support/googletest/googletest ]; then
  %{__rm} -rf %{prefix}/%{cray_product}/%{pkgversion}/tests/test-support/googletest/googletest
fi
if [ -d %{prefix}/%{cray_product}/%{pkgversion}/tests/test-support/googletest ]; then
  %{__rm} -rf %{prefix}/%{cray_product}/%{pkgversion}/tests/test-support/googletest
fi
if [ -d %{prefix}/%{cray_product}/%{pkgversion}/tests/test-support ]; then
  %{__rm} -rf %{prefix}/%{cray_product}/%{pkgversion}/tests/test-support
fi
if [ -d %{prefix}/%{cray_product}/%{pkgversion}/tests ]; then
  %{__rm} -rf %{prefix}/%{cray_product}/%{pkgversion}/tests
fi

%files
%defattr(755, root, root)
%dir %{prefix}/%{cray_product}/%{pkgversion}
%dir %{prefix}/%{cray_product}/%{pkgversion}/lib
%dir %{prefix}/%{cray_product}/%{pkgversion}/libexec
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/%{release_info_name}
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/%{copyright_name}
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/%{attributions_name}
%attr(755, root, root) %{prefix}/%{cray_product}/%{pkgversion}/lib/libaudit.so
%{prefix}/%{cray_product}/%{pkgversion}/lib/libcommontools_be.so*
%{prefix}/%{cray_product}/%{pkgversion}/lib/libcommontools_fe.so*
%attr(755, root, root) %{prefix}/%{cray_product}/%{pkgversion}/libexec/cti_be_daemon%{pkgversion}
%attr(755, root, root) %{prefix}/%{cray_product}/%{pkgversion}/libexec/cti_fe_daemon%{pkgversion}
%attr(755, root, root) %{prefix}/%{cray_product}/%{pkgversion}/%{set_default_command}_%{cray_name}_%{pkgversion}
%attr(755, root, root) %{prefix}/%{set_default_path}/%{set_default_command}_%{cray_name}_%{pkgversion}
%attr(755, root, root) %{prefix}/modulefiles/%{modulefile_name}/%{pkgversion}
%attr(644, root, root) %{prefix}/lmod/modulefiles/core/%{cray_name}/%{pkgversion}.lua
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/%{cray_dependency_resolver}
%attr(644, root, root) %verify(not md5 size mtime) %{prefix}/%{cray_product}/%{pkgversion}/%{cray_dso_list}

%files -n %{cray_name}-devel-%{pkgversion}
%dir %{prefix}/%{cray_product}/%{pkgversion}/include
%dir %{prefix}/%{cray_product}/%{pkgversion}/lib/pkgconfig
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/include/common_tools_be.h
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/include/common_tools_fe.h
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/include/common_tools_shared.h
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/include/common_tools_version.h
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/lib/pkgconfig/common_tools_be.pc
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/lib/pkgconfig/common_tools_fe.pc
%attr(755, root, root) %{prefix}/%{cray_product}/%{pkgversion}/%{set_default_command}_%{cray_name}%{pkgversion_separator}devel_%{pkgversion}
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/%{cray_dependency_resolver}
%attr(755, root, root) %{prefix}/%{set_default_path}/%{set_default_command}_%{cray_name}%{pkgversion_separator}devel_%{pkgversion}
%attr(755, root, root) %{prefix}/modulefiles/%{devel_modulefile_name}/%{pkgversion}
%attr(644, root, root) %{prefix}/lmod/modulefiles/core/%{devel_modulefile_name}/%{pkgversion}.lua

%files -n %{cray_name}-tests-%{pkgversion}
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/%{cray_dependency_resolver}

%dir %{prefix}/%{cray_product}/%{pkgversion}/tests
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/configure.ac
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/Makefile.am
%attr(755, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/test_tool_starter.py
%attr(755, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/test_tool_config.yaml
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/README.md

%dir %{prefix}/%{cray_product}/%{pkgversion}/tests/function
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/function/avocado_config.yaml
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/function/tests.py
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/function/Makefile.am
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/function/README.md

%dir %{prefix}/%{cray_product}/%{pkgversion}/tests/function/bin
%attr(755, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/function/bin/cti_barrier
%attr(755, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/function/bin/cti_callback_daemon
%attr(755, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/function/bin/cti_callback
%attr(755, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/function/bin/cti_info
%attr(755, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/function/bin/cti_kill
%attr(755, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/function/bin/cti_launch
%attr(755, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/function/bin/cti_link
%attr(755, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/function/bin/cti_mpmd
%attr(755, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/function/bin/cti_wlm
%attr(755, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/function/bin/function_tests
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/function/bin/Makefile.am

%dir %{prefix}/%{cray_product}/%{pkgversion}/tests/function/src
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/function/src/Makefile.am
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/function/src/hello_mpi.c
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/function/src/hello_mpi_wait.c
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/function/src/mpi_wrapper.c
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/function/src/mpmd.conf
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/function/src/testing.info
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/function/src/inputFileData.txt

%dir %{prefix}/%{cray_product}/%{pkgversion}/tests/test_support
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/test_support/Makefile.am
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/test_support/one_print.c
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/test_support/one_socket.c
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/test_support/two_socket.c
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/test_support/remote_filecheck.c

%dir %{prefix}/%{cray_product}/%{pkgversion}/tests/test_support/message_one
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/test_support/message_one/libmessage.so
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/test_support/message_one/message.c
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/test_support/message_one/message.h

%dir %{prefix}/%{cray_product}/%{pkgversion}/tests/test_support/message_two
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/test_support/message_two/libmessage.so
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/test_support/message_two/message.c
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/test_support/message_two/message.h

%{prefix}/%{cray_product}/%{pkgversion}/tests/test_support/googletest/googletest/src
%{prefix}/%{cray_product}/%{pkgversion}/tests/test_support/googletest/googletest/include
%{prefix}/%{cray_product}/%{pkgversion}/tests/test_support/googletest/googlemock/src
%{prefix}/%{cray_product}/%{pkgversion}/tests/test_support/googletest/googlemock/include

