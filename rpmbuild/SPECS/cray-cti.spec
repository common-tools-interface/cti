# Packaging definitions
%global pkgversion           %(%{_sourcedir}/get_package_data --crayversion)
%global branch               %(%{_sourcedir}/get_package_data --branch)
%global pkgversion_separator -
%global copyright_date       %(date +%%Y)
%global copyright            Copyright 2010-%{copyright_date} Hewlett Packard Enterprise Development LP.

# RPM build time
%global release_date %(date +%%B\\ %%Y)

%global cray_product_prefix cray-
%global product cti
%global cray_product %{product}
%global cray_name    %{cray_product_prefix}%{cray_product}

# Path definitions
%global cray_prefix /opt/cray/pe

#FIXME: This should be relocatable
%global external_build_dir %{cray_prefix}/%{cray_product}/%{pkgversion}
%global tests_source_dir   %{_topdir}/../tests

# modulefile definitions
%global modulefile_name       %{cray_name}
%global devel_modulefile_name %{cray_name}%{pkgversion_separator}devel

%global module_template_name       %{modulefile_name}.module.template
%global devel_module_template_name %{devel_modulefile_name}.module.template

# set_default modulefile definitions
%global set_default_command       set_default
%global set_default_template      set_default_template
%global set_default_path          admin-pe/set_default_files

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

# Disable /usr/lib/.build-id files
%global _build_id_links none

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
%global privlibs %{privlibs}|libgtest
%global privlibs %{privlibs}|libstdc++.*
%global __requires_exclude ^(%{privlibs})\\.so*

# Dist tags for SuSE need to be manually set
%if 0%{?suse_version}
%if 0%{?sle_version} == 150100
%global dist .sles15sp1
%global OS_HW_TAG 7.0
%global OS_WB_TAG sles15sp1
%endif
%if 0%{?sle_version} == 150200
%global dist .sles15sp2
%global OS_HW_TAG 7.0
%global OS_WB_TAG sles15sp2
%endif
%if 0%{?sle_version} == 150300
%global dist .sles15sp3
%global OS_HW_TAG 7.0
%global OS_WB_TAG sles15sp3
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
%if %{branch} == "release"
Release:    %(echo ${BUILD_NUMBER})%{dist}
%else
Release:    1%{dist}
%endif
Prefix:     %{cray_prefix}
License:    Dual BSD or GPLv2
Vendor:     Hewlett Packard Enterprise Development LP
Group:      Development/System
Provides:   %{cray_name} = %{pkgversion}
Requires:   set_default_3, cray-cdst-support >= %{cdst_support_pkgversion_min}, cray-cdst-support < %{cdst_support_pkgversion_max}
Source0:    %{module_template_name}
Source1:    %{devel_module_template_name}
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
Requires:   set_default_3, %{cray_name} = %{pkgversion}
Source8:    %{lmod_template_cti_devel}
%description -n %{cray_name}-devel-%{pkgversion}
Development files for Cray Common Tools Interface

%package -n %{cray_name}-tests-%{pkgversion}
Summary:    Cray Common Tools Interface test binariess
Group:      Development
Provides:   %{cray_name}-tests = %{pkgversion}
Requires:   cray-cdst-support-devel >= %{cdst_support_pkgversion_min}, cray-cdst-support-devel < %{cdst_support_pkgversion_max}, %{cray_name} = %{pkgversion}, %{cray_name}-devel = %{pkgversion}
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
%{__cp} -a %{external_build_dir}/lib/libctiaudit.so ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/lib
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
%{__cp} -a %{external_build_dir}/libexec/mpir_shim%{pkgversion} ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/libexec
%{__cp} -a %{external_build_dir}/libexec/cti_diagnostics ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/libexec
%{__cp} -a %{external_build_dir}/libexec/cti_diagnostics_backend ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/libexec
%{__cp} -a %{external_build_dir}/libexec/cti_diagnostics_target ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/libexec
%{__cp} -a %{external_build_dir}/libexec/cti_first_subprocess%{pkgversion} ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/libexec
# Binaries

# Share
%{__install} -d ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/share
%{__install} -d ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/share/man/man1
%{__install} -d ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/share/man/man3
%{__cp} -a %{external_build_dir}/share/man/man1/cti.1 ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/share/man/man1
%{__cp} -a %{external_build_dir}/share/man/man3/cti.3 ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/share/man/man3

# modulefile
%{__install} -d ${RPM_BUILD_ROOT}/%{prefix}/modulefiles/%{modulefile_name}
%{__sed} 's|<PREFIX>|%{external_build_dir}|g;s|<CRAY_NAME>|%{cray_name}|g;s|<VERSION>|%{pkgversion}|g' %{SOURCE0} > ${RPM_BUILD_ROOT}/%{prefix}/modulefiles/%{modulefile_name}/%{pkgversion}

# devel modulefile
%{__install} -d ${RPM_BUILD_ROOT}/%{prefix}/modulefiles/%{devel_modulefile_name}
%{__sed} 's|<PREFIX>|%{external_build_dir}|g;s|<CRAY_NAME>|%{devel_modulefile_name}|g;s|<CRAY_BASE_NAME>|%{cray_name}|g;s|<VERSION>|%{pkgversion}|g' %{SOURCE1} > ${RPM_BUILD_ROOT}/%{prefix}/modulefiles/%{devel_modulefile_name}/%{pkgversion}

# Cray PE dependency resolver
%{__sed} 's|<CRAY_PREFIX>|%{cray_prefix}|g;s|<PKG_VERSION>|%{pkgversion}|g;s|<PE_PRODUCT>|%{cray_product}|g' %{SOURCE3} > ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/%{cray_dependency_resolver}

# set_default template
%{__sed} 's|\[product_name\]|%{cray_product}|g;s|\[version_string\]|%{pkgversion}|g;s|\[install_dir\]|%{prefix}|g;s|\[module_dir\]|%{prefix}/modulefiles|g;s|\[module_name_list\]|%{modulefile_name}|g;s|\[lmod_dir_list\]|%{prefix}/lmod/modulefiles/core|g;s|\[lmod_name_list\]|%{modulefile_name}|g' %{_sourcedir}/set_default/%{set_default_template} > ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/%{set_default_command}_%{cray_name}_%{pkgversion}

# set_default template - devel
%{__sed} 's|\[product_name\]|%{cray_product}|g;s|\[version_string\]|%{pkgversion}|g;s|\[install_dir\]|%{prefix}|g;s|\[module_dir\]|%{prefix}/modulefiles|g;s|\[module_name_list\]|%{devel_modulefile_name}|g;s|\[lmod_dir_list\]|%{prefix}/lmod/modulefiles/core|g;s|\[lmod_name_list\]|%{devel_modulefile_name}|g' %{_sourcedir}/set_default/%{set_default_template} > ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/%{set_default_command}_%{devel_modulefile_name}_%{pkgversion}

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
# yaml files
%global start_rmLine %(sed -n /section-3/= %{SOURCE9})
%global end_rmLine %(sed -n /admin-pe/= %{SOURCE9} | tail -1)
%global devel_pkg_name %{cray_name}-devel-%{pkgversion}-%{version}-%{release}.%{_arch}.rpm
%global tests_pkg_name %{cray_name}-tests-%{pkgversion}-%{version}-%{release}.%{_arch}.rpm
%global devel_rm_name %{cray_name}-devel-%{pkgversion}-%{version}-%{release}.%{_arch}
%global tests_rm_name %{cray_name}-tests-%{pkgversion}-%{version}-%{release}.%{_arch}
%global rpm_list %{devel_pkg_name},%{tests_pkg_name}
%if %{branch} == "release"
# yaml file - cray-cti
%{__sed} 's|<PRODUCT>|%{cray_name}|g;s|<VERSION>|%{pkgversion}|g;s|<BUILD_METADATA>|%{version}|g;s|<RELEASE>|%{release}|g;s|<ARCH>|%{_arch}|g;s|<REMOVAL_DATE>|%{removal_date}|g;s|<SYS_HW_TAG>|%{SYS_HW_TAG}|g;s|<SYS_WB_TAG>|%{SYS_WB_TAG}|g;s|<OS_HW_TAG>|%{OS_HW_TAG}|g;s|<OS_WB_TAG>|%{OS_WB_TAG}|g;s|<RPM_LST>|%{rpm_list}|g;s|<RPM_1>|%{devel_pkg_name}|g;s|<RPM_2>|%{tests_pkg_name}|g;s|<RPM_RM_2>|%{tests_rm_name}|g;s|<RPM_RM_1>|%{devel_rm_name}|g' %{SOURCE9} > %{_rpmdir}/%{_arch}/%{cray_name}-%{pkgversion}-%{version}-%{release}.%{_arch}.rpm.yaml
%else
%{__sed} '%{start_rmLine},%{end_rmLine}d;s|section-5|section-3|g;s|<PRODUCT>|%{cray_name}|g;s|<VERSION>|%{pkgversion}|g;s|<BUILD_METADATA>|%{version}|g;s|<RELEASE>|%{release}|g;s|<ARCH>|%{_arch}|g;s|<REMOVAL_DATE>|%{removal_date}|g;s|<SYS_HW_TAG>|%{SYS_HW_TAG}|g;s|<SYS_WB_TAG>|%{SYS_WB_TAG}|g;s|<OS_HW_TAG>|%{OS_HW_TAG}|g;s|<OS_WB_TAG>|%{OS_WB_TAG}|g;s|<RPM_LST>|%{rpm_list}|g;s|<RPM_1>|%{devel_pkg_name}|g;s|<RPM_2>|%{tests_pkg_name}|g;s|<RPM_RM_2>|%{tests_rm_name}|g;s|<RPM_RM_1>|%{devel_rm_name}|g' %{SOURCE9} > %{_rpmdir}/%{_arch}/%{cray_name}-%{pkgversion}-%{version}-%{release}.%{_arch}.rpm.yaml
%endif

# Test files
%{__install} -d ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests
%{__cp} -a %{tests_source_dir}/function/Makefile                 ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/Makefile
%{__cp} -a %{tests_source_dir}/function/cdst-test/test_tool.py   ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/test_tool.py
%{__cp} -a %{tests_source_dir}/function/test_tool_config.yaml    ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/test_tool_config.yaml
%{__cp} -a %{tests_source_dir}/function/README.md                ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/README.md
%{__cp} -a %{tests_source_dir}/function/avocado_config.yaml      ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/avocado_config.yaml
%{__cp} -a %{tests_source_dir}/function/tests.py                 ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/tests.py

%{__install} -d ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/cdst_test
%{__cp} -a %{tests_source_dir}/function/cdst-test/cdst_test/README.md                   ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/cdst_test/README.md
%{__cp} -a %{tests_source_dir}/function/cdst-test/cdst_test/README_config.md            ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/cdst_test/README_config.md
%{__cp} -a %{tests_source_dir}/function/cdst-test/cdst_test/README_textcheck.md         ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/cdst_test/README_textcheck.md
%{__cp} -a %{tests_source_dir}/function/cdst-test/cdst_test/__init__.py                 ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/cdst_test/__init__.py
%{__cp} -a %{tests_source_dir}/function/cdst-test/cdst_test/artifactory.py              ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/cdst_test/artifactory.py
%{__cp} -a %{tests_source_dir}/function/cdst-test/cdst_test/config.py                   ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/cdst_test/config.py
%{__cp} -a %{tests_source_dir}/function/cdst-test/cdst_test/config_default.py           ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/cdst_test/config_default.py
%{__cp} -a %{tests_source_dir}/function/cdst-test/cdst_test/config_nodes.py             ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/cdst_test/config_nodes.py
%{__cp} -a %{tests_source_dir}/function/cdst-test/cdst_test/env.py                      ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/cdst_test/env.py
%{__cp} -a %{tests_source_dir}/function/cdst-test/cdst_test/files.py                    ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/cdst_test/files.py
%{__cp} -a %{tests_source_dir}/function/cdst-test/cdst_test/job_environment.py          ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/cdst_test/job_environment.py
%{__cp} -a %{tests_source_dir}/function/cdst-test/cdst_test/log.py                      ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/cdst_test/log.py
%{__cp} -a %{tests_source_dir}/function/cdst-test/cdst_test/modules.py                  ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/cdst_test/modules.py
%{__cp} -a %{tests_source_dir}/function/cdst-test/cdst_test/remote_test_tool_startup.py ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/cdst_test/remote_test_tool_startup.py
%{__cp} -a %{tests_source_dir}/function/cdst-test/cdst_test/remote_test_tool_ui.py      ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/cdst_test/remote_test_tool_ui.py
%{__cp} -a %{tests_source_dir}/function/cdst-test/cdst_test/ssh.py                      ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/cdst_test/ssh.py
%{__cp} -a %{tests_source_dir}/function/cdst-test/cdst_test/test_server.py              ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/cdst_test/test_server.py
%{__cp} -a %{tests_source_dir}/function/cdst-test/cdst_test/test_server_commands.py     ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/cdst_test/test_server_commands.py
%{__cp} -a %{tests_source_dir}/function/cdst-test/cdst_test/test_server_proto.py        ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/cdst_test/test_server_proto.py
%{__cp} -a %{tests_source_dir}/function/cdst-test/cdst_test/textcheck.py                ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/cdst_test/textcheck.py
%{__cp} -a %{tests_source_dir}/function/cdst-test/cdst_test/useful.py                   ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/cdst_test/useful.py

%{__install} -d ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/cdst_test/data
%{__cp} -a %{tests_source_dir}/function/cdst-test/cdst_test/data/hosts.yaml    ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/cdst_test/data/hosts.yaml
%{__cp} -a %{tests_source_dir}/function/cdst-test/cdst_test/data/products.yaml ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/cdst_test/data/products.yaml

%{__install} -d ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/cdst_test/scripts
%{__cp} -a %{tests_source_dir}/function/cdst-test/cdst_test/scripts/avo_config.py        ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/cdst_test/scripts/avo_config.py
%{__cp} -a %{tests_source_dir}/function/cdst-test/cdst_test/scripts/passwordedscp.expect ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/cdst_test/scripts/passwordedscp.expect
%{__cp} -a %{tests_source_dir}/function/cdst-test/cdst_test/scripts/passwordedssh.expect ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/cdst_test/scripts/passwordedssh.expect
%{__cp} -a %{tests_source_dir}/function/cdst-test/cdst_test/scripts/sshcopyid.expect     ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/cdst_test/scripts/sshcopyid.expect
%{__cp} -a %{tests_source_dir}/function/cdst-test/cdst_test/scripts/uai.expect           ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/cdst_test/scripts/uai.expect
%{__cp} -a %{tests_source_dir}/function/cdst-test/cdst_test/scripts/validate_ssh.sh      ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/cdst_test/scripts/validate_ssh.sh

%{__install} -d ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/cdst_test/src
%{__cp} -a %{tests_source_dir}/function/cdst-test/cdst_test/src/Makefile    ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/cdst_test/src/Makefile
%{__cp} -a %{tests_source_dir}/function/cdst-test/cdst_test/src/hello_mpi.c ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/cdst_test/src/hello_mpi.c

# we package pre-built binaries along with their source code so that a user doesn't
# need cray-cti-devel to run tests
%{__install} -d ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/src
%{__cp} -a %{tests_source_dir}/function/src/Makefile                   ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/src/Makefile
%{__cp} -a %{tests_source_dir}/function/src/cti_barrier                ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/src/cti_barrier
%{__cp} -a %{tests_source_dir}/function/src/cti_barrier_test.c         ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/src/cti_barrier_test.c
%{__cp} -a %{tests_source_dir}/function/src/cti_callback               ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/src/cti_callback
%{__cp} -a %{tests_source_dir}/function/src/cti_callback_daemon        ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/src/cti_callback_daemon
%{__cp} -a %{tests_source_dir}/function/src/cti_callback_daemon.c      ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/src/cti_callback_daemon.c
%{__cp} -a %{tests_source_dir}/function/src/cti_callback_test.c        ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/src/cti_callback_test.c
%{__cp} -a %{tests_source_dir}/function/src/cti_callback_test.h        ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/src/cti_callback_test.h
%{__cp} -a %{tests_source_dir}/function/src/cti_double_daemon          ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/src/cti_double_daemon
%{__cp} -a %{tests_source_dir}/function/src/cti_double_daemon_test.cpp ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/src/cti_double_daemon_test.cpp
%{__cp} -a %{tests_source_dir}/function/src/cti_environment            ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/src/cti_environment
%{__cp} -a %{tests_source_dir}/function/src/cti_environment_test.cpp   ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/src/cti_environment_test.cpp
%{__cp} -a %{tests_source_dir}/function/src/cti_fd_in                  ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/src/cti_fd_in
%{__cp} -a %{tests_source_dir}/function/src/cti_fd_in_test.cpp         ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/src/cti_fd_in_test.cpp
%{__cp} -a %{tests_source_dir}/function/src/cti_fe_common.c            ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/src/cti_fe_common.c
%{__cp} -a %{tests_source_dir}/function/src/cti_fe_common.h            ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/src/cti_fe_common.h
%{__cp} -a %{tests_source_dir}/function/src/cti_fe_function_test.cpp   ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/src/cti_fe_function_test.cpp
%{__cp} -a %{tests_source_dir}/function/src/cti_fe_function_test.hpp   ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/src/cti_fe_function_test.hpp
%{__cp} -a %{tests_source_dir}/function/src/cti_file_in                ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/src/cti_file_in
%{__cp} -a %{tests_source_dir}/function/src/cti_file_in_test.cpp       ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/src/cti_file_in_test.cpp
%{__cp} -a %{tests_source_dir}/function/src/cti_file_transfer          ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/src/cti_file_transfer
%{__cp} -a %{tests_source_dir}/function/src/cti_file_transfer_test.cpp ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/src/cti_file_transfer_test.cpp
%{__cp} -a %{tests_source_dir}/function/src/cti_info                   ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/src/cti_info
%{__cp} -a %{tests_source_dir}/function/src/cti_info_test.c            ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/src/cti_info_test.c
%{__cp} -a %{tests_source_dir}/function/src/cti_kill                   ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/src/cti_kill
%{__cp} -a %{tests_source_dir}/function/src/cti_kill_test.c            ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/src/cti_kill_test.c
%{__cp} -a %{tests_source_dir}/function/src/cti_launch                 ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/src/cti_launch
%{__cp} -a %{tests_source_dir}/function/src/cti_launch_test.c          ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/src/cti_launch_test.c
%{__cp} -a %{tests_source_dir}/function/src/cti_ld_preload             ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/src/cti_ld_preload
%{__cp} -a %{tests_source_dir}/function/src/cti_ld_preload_test.cpp    ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/src/cti_ld_preload_test.cpp
%{__cp} -a %{tests_source_dir}/function/src/cti_link                   ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/src/cti_link
%{__cp} -a %{tests_source_dir}/function/src/cti_linking_test.c         ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/src/cti_linking_test.c
%{__cp} -a %{tests_source_dir}/function/src/cti_manifest               ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/src/cti_manifest
%{__cp} -a %{tests_source_dir}/function/src/cti_manifest_test.cpp      ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/src/cti_manifest_test.cpp
%{__cp} -a %{tests_source_dir}/function/src/cti_mpir_shim              ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/src/cti_mpir_shim
%{__cp} -a %{tests_source_dir}/function/src/cti_mpir_shim_test.cpp     ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/src/cti_mpir_shim_test.cpp
%{__cp} -a %{tests_source_dir}/function/src/cti_mpmd                   ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/src/cti_mpmd
%{__cp} -a %{tests_source_dir}/function/src/cti_mpmd_test.c            ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/src/cti_mpmd_test.c
%{__cp} -a %{tests_source_dir}/function/src/cti_redirect               ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/src/cti_redirect
%{__cp} -a %{tests_source_dir}/function/src/cti_redirect_test.cpp      ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/src/cti_redirect_test.cpp
%{__cp} -a %{tests_source_dir}/function/src/cti_release_twice          ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/src/cti_release_twice
%{__cp} -a %{tests_source_dir}/function/src/cti_release_twice_test.cpp ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/src/cti_release_twice_test.cpp
%{__cp} -a %{tests_source_dir}/function/src/cti_session                ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/src/cti_session
%{__cp} -a %{tests_source_dir}/function/src/cti_session_test.cpp       ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/src/cti_session_test.cpp
%{__cp} -a %{tests_source_dir}/function/src/cti_tool_daemon            ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/src/cti_tool_daemon
%{__cp} -a %{tests_source_dir}/function/src/cti_tool_daemon_test.cpp   ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/src/cti_tool_daemon_test.cpp
%{__cp} -a %{tests_source_dir}/function/src/cti_wlm                    ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/src/cti_wlm
%{__cp} -a %{tests_source_dir}/function/src/cti_wlm_test.c             ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/src/cti_wlm_test.c

%{__install} -d ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/src/static
%{__cp} -a %{tests_source_dir}/function/src/static/inputFileData.txt ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/src/static/inputFileData.txt
%{__cp} -a %{tests_source_dir}/function/src/static/mpmd.conf         ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/src/static/mpmd.conf
%{__cp} -a %{tests_source_dir}/function/src/static/testing.info      ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/src/static/testing.info

# support files must be built on the test machine, so we only provide the source
%{__install} -d ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/src/support
%{__cp} -a %{tests_source_dir}/function/src/support/Makefile           ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/src/support/Makefile
%{__cp} -a %{tests_source_dir}/function/src/support/hello_mpi.c        ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/src/support/hello_mpi.c
%{__cp} -a %{tests_source_dir}/function/src/support/hello_mpi_wait.c   ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/src/support/hello_mpi_wait.c
%{__cp} -a %{tests_source_dir}/function/src/support/mpi_wrapper.c      ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/src/support/mpi_wrapper.c
%{__cp} -a %{tests_source_dir}/function/src/support/one_print.c        ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/src/support/one_print.c
%{__cp} -a %{tests_source_dir}/function/src/support/one_socket.c       ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/src/support/one_socket.c
%{__cp} -a %{tests_source_dir}/function/src/support/remote_filecheck.c ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/src/support/remote_filecheck.c
%{__cp} -a %{tests_source_dir}/function/src/support/two_socket.c       ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/src/support/two_socket.c
%{__cp} -a %{tests_source_dir}/function/src/support/wrapper_script.sh  ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/src/support/wrapper_script.sh

%{__install} -d ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/src/support/message_one
%{__cp} -a %{tests_source_dir}/function/src/support/message_one/message.c ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/src/support/message_one/message.c
%{__cp} -a %{tests_source_dir}/function/src/support/message_one/message.h ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/src/support/message_one/message.h

%{__install} -d ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/src/support/message_two
%{__cp} -a %{tests_source_dir}/function/src/support/message_two/message.c ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/src/support/message_two/message.c
%{__cp} -a %{tests_source_dir}/function/src/support/message_two/message.h ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/src/support/message_two/message.h

# Touch the cray dynamic file list which will be populated/updated post-install
touch ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/%{cray_dso_list}

%post
# Generate a list of dynamic shared objects (libraries) to add to Cray PE cache
find ${RPM_INSTALL_PREFIX}/%{cray_product}/%{pkgversion} -name '*.so*' > ${RPM_INSTALL_PREFIX}/%{cray_product}/%{pkgversion}/%{cray_dso_list}

# Add Shasta configuration files for PE content projection
if [ "${RPM_INSTALL_PREFIX}" = "%{prefix}" ]; then
    mkdir -p /etc/%{prefix}/admin-pe/bindmount.conf.d/
    mkdir -p /etc/%{prefix}/admin-pe/modulepaths.conf.d/
    echo "%{prefix}/%{product}/" > /etc/%{prefix}/admin-pe/bindmount.conf.d/%{cray_name}.conf
    echo "%{prefix}/modulefiles/%{modulefile_name}" > /etc/%{prefix}/admin-pe/modulepaths.conf.d/%{modulefile_name}.conf
    echo "%{prefix}/lmod/modulefiles/core/%{cray_name}" >> /etc/%{prefix}/admin-pe/modulepaths.conf.d/%{modulefile_name}.conf
fi

# Process relocations
if [ "${RPM_INSTALL_PREFIX}" != "%{prefix}" ]
then
    # Modulefile
    %{__sed} -i "s|^\([[:space:]]*set CTI_BASEDIR[[:space:]]*\)\(.*\)|\1${RPM_INSTALL_PREFIX}/%{cray_product}/%{pkgversion}|" ${RPM_INSTALL_PREFIX}/modulefiles/%{modulefile_name}/%{pkgversion}
    # set default command
    %{__sed} -i "s|^\(export CRAY_inst_dir=\).*|\1${RPM_INSTALL_PREFIX}|" ${RPM_INSTALL_PREFIX}/%{cray_product}/%{pkgversion}/%{set_default_command}_%{cray_name}_%{pkgversion}
else
    # Only call set_default if we are not relocating the rpm and there's not already a default set unless someone passes in CRAY_INSTALL_DEFAULT=1
    if [ ${CRAY_INSTALL_DEFAULT:-0} -eq 1 ] || [ ! -f ${RPM_INSTALL_PREFIX}/modulefiles/%{cray_name}/.version ]
    then
        # Set default - TCL
        ${RPM_INSTALL_PREFIX}/%{set_default_path}/%{set_default_command}_%{cray_name}_%{pkgversion}
    fi
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
    # Only call set_default if we are not relocating the rpm and there's not already a default set unless someone passes in CRAY_INSTALL_DEFAULT=1
    if [ ${CRAY_INSTALL_DEFAULT:-0} -eq 1 ] || [ ! -f ${RPM_INSTALL_PREFIX}/modulefiles/%{devel_modulefile_name}/.version ]
    then
        # set default
        ${RPM_INSTALL_PREFIX}/%{set_default_path}/%{set_default_command}_%{devel_modulefile_name}_%{pkgversion}
    fi
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

# If the install dir is empty
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

# If the install dir is empty
if [[ -z `ls ${RPM_INSTALL_PREFIX}/%{cray_product} ]]; then
  if [ -f /etc%{prefix}/admin-pe/modulepaths.conf.d/%{devel_modulefile_name}.conf ]; then
    %{__rm} -rf /etc%{prefix}/admin-pe/modulepaths.conf.d/%{devel_modulefile_name}.conf
  fi
  if [ -d ${RPM_INSTALL_PREFIX}/lmod/modulefiles/core/%{devel_modulefile_name} ]; then
    %{__rm} -rf ${RPM_INSTALL_PREFIX}/lmod/modulefiles/core/%{devel_modulefile_name}
  fi
  if [ -d ${RPM_INSTALL_PREFIX}/modulefiles/%{devel_modulefile_name} ]; then
    %{__rm} -rf ${RPM_INSTALL_PREFIX}/modulefiles/%{devel_modulefile_name}
  fi
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
%attr(755, root, root) %{prefix}/%{cray_product}/%{pkgversion}/lib/libctiaudit.so
%{prefix}/%{cray_product}/%{pkgversion}/lib/libcommontools_be.so*
%{prefix}/%{cray_product}/%{pkgversion}/lib/libcommontools_fe.so*
%attr(755, root, root) %{prefix}/%{cray_product}/%{pkgversion}/libexec/cti_be_daemon%{pkgversion}
%attr(755, root, root) %{prefix}/%{cray_product}/%{pkgversion}/libexec/cti_fe_daemon%{pkgversion}
%attr(755, root, root) %{prefix}/%{cray_product}/%{pkgversion}/libexec/mpir_shim%{pkgversion}
%attr(755, root, root) %{prefix}/%{cray_product}/%{pkgversion}/libexec/cti_diagnostics
%attr(755, root, root) %{prefix}/%{cray_product}/%{pkgversion}/libexec/cti_diagnostics_backend
%attr(755, root, root) %{prefix}/%{cray_product}/%{pkgversion}/libexec/cti_diagnostics_target
%attr(755, root, root) %{prefix}/%{cray_product}/%{pkgversion}/libexec/cti_first_subprocess%{pkgversion}
%attr(755, root, root) %{prefix}/%{cray_product}/%{pkgversion}/%{set_default_command}_%{cray_name}_%{pkgversion}
%attr(755, root, root) %{prefix}/%{set_default_path}/%{set_default_command}_%{cray_name}_%{pkgversion}
%attr(755, root, root) %{prefix}/modulefiles/%{modulefile_name}/%{pkgversion}
%attr(644, root, root) %{prefix}/lmod/modulefiles/core/%{cray_name}/%{pkgversion}.lua
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/%{cray_dependency_resolver}
%attr(644, root, root) %verify(not md5 size mtime) %{prefix}/%{cray_product}/%{pkgversion}/%{cray_dso_list}

%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/share/man/man1/cti.1
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/share/man/man3/cti.3

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
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/Makefile
%attr(755, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/test_tool.py
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/test_tool_config.yaml
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/README.md
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/avocado_config.yaml
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/tests.py

%dir %{prefix}/%{cray_product}/%{pkgversion}/tests/cdst_test
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/cdst_test/README.md
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/cdst_test/README_config.md
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/cdst_test/README_textcheck.md
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/cdst_test/__init__.py
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/cdst_test/artifactory.py
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/cdst_test/config.py
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/cdst_test/config_default.py
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/cdst_test/config_nodes.py
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/cdst_test/env.py
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/cdst_test/files.py
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/cdst_test/job_environment.py
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/cdst_test/log.py
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/cdst_test/modules.py
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/cdst_test/remote_test_tool_startup.py
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/cdst_test/remote_test_tool_ui.py
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/cdst_test/ssh.py
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/cdst_test/test_server.py
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/cdst_test/test_server_commands.py
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/cdst_test/test_server_proto.py
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/cdst_test/textcheck.py
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/cdst_test/useful.py

%dir %{prefix}/%{cray_product}/%{pkgversion}/tests/cdst_test/data
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/cdst_test/data/hosts.yaml
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/cdst_test/data/products.yaml

%dir %{prefix}/%{cray_product}/%{pkgversion}/tests/cdst_test/scripts
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/cdst_test/scripts/avo_config.py
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/cdst_test/scripts/passwordedscp.expect
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/cdst_test/scripts/passwordedssh.expect
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/cdst_test/scripts/sshcopyid.expect
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/cdst_test/scripts/uai.expect
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/cdst_test/scripts/validate_ssh.sh

%dir %{prefix}/%{cray_product}/%{pkgversion}/tests/cdst_test/src
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/cdst_test/src/Makefile
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/cdst_test/src/hello_mpi.c

# we package pre-built binaries along with their source code so that a user doesn't
# need cray-cti-devel to run tests
%dir %{prefix}/%{cray_product}/%{pkgversion}/tests/src
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/src/Makefile
%attr(755, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/src/cti_barrier
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/src/cti_barrier_test.c
%attr(755, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/src/cti_callback
%attr(755, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/src/cti_callback_daemon
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/src/cti_callback_daemon.c
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/src/cti_callback_test.c
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/src/cti_callback_test.h
%attr(755, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/src/cti_double_daemon
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/src/cti_double_daemon_test.cpp
%attr(755, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/src/cti_environment
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/src/cti_environment_test.cpp
%attr(755, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/src/cti_fd_in
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/src/cti_fd_in_test.cpp
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/src/cti_fe_common.c
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/src/cti_fe_common.h
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/src/cti_fe_function_test.cpp
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/src/cti_fe_function_test.hpp
%attr(755, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/src/cti_file_in
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/src/cti_file_in_test.cpp
%attr(755, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/src/cti_file_transfer
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/src/cti_file_transfer_test.cpp
%attr(755, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/src/cti_info
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/src/cti_info_test.c
%attr(755, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/src/cti_kill
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/src/cti_kill_test.c
%attr(755, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/src/cti_launch
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/src/cti_launch_test.c
%attr(755, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/src/cti_ld_preload
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/src/cti_ld_preload_test.cpp
%attr(755, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/src/cti_link
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/src/cti_linking_test.c
%attr(755, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/src/cti_manifest
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/src/cti_manifest_test.cpp
%attr(755, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/src/cti_mpir_shim
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/src/cti_mpir_shim_test.cpp
%attr(755, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/src/cti_mpmd
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/src/cti_mpmd_test.c
%attr(755, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/src/cti_redirect
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/src/cti_redirect_test.cpp
%attr(755, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/src/cti_release_twice
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/src/cti_release_twice_test.cpp
%attr(755, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/src/cti_session
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/src/cti_session_test.cpp
%attr(755, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/src/cti_tool_daemon
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/src/cti_tool_daemon_test.cpp
%attr(755, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/src/cti_wlm
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/src/cti_wlm_test.c

%dir %{prefix}/%{cray_product}/%{pkgversion}/tests/src/static
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/src/static/inputFileData.txt
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/src/static/mpmd.conf
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/src/static/testing.info

# support files must be built on the test machine, so we only provide the source
%dir %{prefix}/%{cray_product}/%{pkgversion}/tests/src/support
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/src/support/Makefile
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/src/support/hello_mpi.c
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/src/support/hello_mpi_wait.c
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/src/support/mpi_wrapper.c
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/src/support/one_print.c
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/src/support/one_socket.c
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/src/support/remote_filecheck.c
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/src/support/two_socket.c
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/src/support/wrapper_script.sh

%dir %{prefix}/%{cray_product}/%{pkgversion}/tests/src/support/message_one
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/src/support/message_one/message.c
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/src/support/message_one/message.h

%dir %{prefix}/%{cray_product}/%{pkgversion}/tests/src/support/message_two
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/src/support/message_two/message.c
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/src/support/message_two/message.h
