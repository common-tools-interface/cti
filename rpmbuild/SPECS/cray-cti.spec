# Packaging definitions
%global pkgversion %(%{_sourcedir}/get_package_data --crayversion)
%global pkgversion_separator -
%global copyright Copyright 2010-2019 Cray Inc. All rights reserved.

# RPM build time
%global release_date %(date +%%B\\ %%e,\\ %%Y)

%global cray_product_prefix cray-
%global product cti
%global cray_product %{product}
%global cray_name %{cray_product_prefix}%{cray_product}

# Path definitions
%global cray_prefix /opt/cray/pe
%global tests_prefix /opt/cray/tests

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

# release info
%global release_info_name release_info
%global release_info_template_name %{release_info_name}.template

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

# Check for the dist tag and set if not populated
# Note: for now sles 15 is the only supported OS without a dist tag, this will need updating if that changes
%{!?dist:
%global dist .sles15
}

Summary:    Cray Common Tools Interface
Name:       %{cray_name}
Version:    %{pkgversion}
# BUILD_METADATA is set by Jenkins
Release:    %(echo ${BUILD_METADATA})%{dist}
Prefix:     %{cray_prefix}
License:    Dual BSD or GPLv2
Vendor:     Cray Inc.
Group:      Development/System
Provides:   %{cray_name} = %{pkgversion}
Requires:   cray-cdst-support >= %{cdst_support_pkgversion_min}, cray-cdst-support < %{cdst_support_pkgversion_max}
Source0:    %{module_template_name}
Source1:    %{devel_module_template_name}
Source2:    %{set_default_template_name}
Source3:    %{cray_dependency_resolver_template_name}
Source4:    %{release_info_template_name}
Source5:    %{copyright_name}
Source6:    %{attributions_name}

%description
Cray Common Tools Interface %{pkgversion}
Certain components, files or programs contained within this package or product are %{copyright}

%package -n %{cray_name}%{pkgversion_separator}devel
Summary:    Cray Common Tools Interface development files
Group:      Development
Provides:   %{cray_name}%{pkgversion_separator}devel = %{pkgversion}
Requires:   cray-gcc-8.1.0, %{cray_name} = %{pkgversion}
%description -n %{cray_name}%{pkgversion_separator}devel
Development files for Cray Common Tools Interface

%package -n %{cray_name}%{pkgversion_separator}tests
Summary:    Cray Common Tools Interface test binariess
Group:      Development
Provides:   %{cray_name}%{pkgversion_separator}tests = %{pkgversion}
Requires:   cray-gcc-8.1.0, %{cray_name} = %{pkgversion}
%description -n %{cray_name}%{pkgversion_separator}tests
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
%{__sed} 's|<PREFIX>|%{external_build_dir}|g;s|<CRAY_NAME>|%{cray_name}%{pkgversion_separator}devel|g;s|<CRAY_BASE_NAME>|%{cray_name}|g;s|<VERSION>|%{pkgversion}|g' %{SOURCE1} > ${RPM_BUILD_ROOT}/%{prefix}/modulefiles/%{devel_modulefile_name}/%{pkgversion}
# Cray PE dependency resolver
%{__sed} 's|<CRAY_PREFIX>|%{cray_prefix}|g;s|<PKG_VERSION>|%{pkgversion}|g;s|<PE_PRODUCT>|%{cray_product}|g' %{SOURCE3} > ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/%{cray_dependency_resolver}
# Set default script into admin-pe
# set_default scripts
%{__sed} 's|<PREFIX>|%{prefix}|g;s|<CRAY_PRODUCT>|%{cray_product}|g;s|<VERSION>|%{pkgversion}|g;s|<MODULEFILE_NAME>|%{modulefile_name}|g' %{SOURCE2} > ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/%{set_default_command}_%{cray_name}_%{pkgversion}
%{__sed} 's|<PREFIX>|%{prefix}|g;s|<CRAY_PRODUCT>|%{cray_product}|g;s|<VERSION>|%{pkgversion}|g;s|<MODULEFILE_NAME>|%{devel_modulefile_name}|g' %{SOURCE2} > ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/%{set_default_command}_%{cray_name}%{pkgversion_separator}devel_%{pkgversion}
# set_default into admin-pe
%{__install} -d ${RPM_BUILD_ROOT}/%{prefix}/%{set_default_path}
%{__install} -D ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/%{set_default_command}_%{cray_name}_%{pkgversion} ${RPM_BUILD_ROOT}/%{prefix}/%{set_default_path}/%{set_default_command}_%{cray_name}_%{pkgversion}
%{__install} -D ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/%{set_default_command}_%{cray_name}%{pkgversion_separator}devel_%{pkgversion} ${RPM_BUILD_ROOT}/%{prefix}/%{set_default_path}/%{set_default_command}_%{cray_name}%{pkgversion_separator}devel_%{pkgversion}

# Test files
%{__install} -d ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests
%{__install} -d ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/examples
%{__install} -d ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/function
%{__install} -d ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/scripts
%{__install} -d ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/test_support
%{__install} -d ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/test_support/message_one
%{__install} -d ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/test_support/message_two

%{__cp} -a %{tests_source_dir}/examples/cti_barrier ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/examples/cti_barrier
%{__cp} -a %{tests_source_dir}/examples/cti_callback ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/examples/cti_callback
%{__cp} -a %{tests_source_dir}/examples/cti_callback_daemon ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/examples/cti_callback_daemon
%{__cp} -a %{tests_source_dir}/examples/cti_info ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/examples/cti_info
%{__cp} -a %{tests_source_dir}/examples/cti_kill ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/examples/cti_kill
%{__cp} -a %{tests_source_dir}/examples/cti_launch ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/examples/cti_launch
%{__cp} -a %{tests_source_dir}/examples/cti_link ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/examples/cti_link
%{__cp} -a %{tests_source_dir}/examples/cti_transfer ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/examples/cti_transfer
%{__cp} -a %{tests_source_dir}/examples/cti_wlm ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/examples/cti_wlm
%{__cp} -a %{tests_source_dir}/examples/testing.info ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/examples/testing.info
%{__cp} -a %{tests_source_dir}/function/avocado_test_params.yaml ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/function/avocado_test_params.yaml
%{__cp} -a %{tests_source_dir}/function/avo_config.py ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/function/avo_config.py
%{__cp} -a %{tests_source_dir}/function/function_tests ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/function/function_tests
%{__cp} -a %{tests_source_dir}/function/hello_mpi_wait.c ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/function/hello_mpi_wait.c
%{__cp} -a %{tests_source_dir}/function/avocado_tests.py ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/function/avocado_tests.py
%{__cp} -a %{tests_source_dir}/function/build_run.sh ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/function/build_run.sh
%{__cp} -a %{tests_source_dir}/function/hello_mpi.c ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/function/hello_mpi.c
%{__cp} -a %{tests_source_dir}/scripts/validate_ssh.sh ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/scripts/validate_ssh.sh
%{__cp} -a %{tests_source_dir}/test_support/inputFileData.txt ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/test_support/inputFileData.txt
%{__cp} -a %{tests_source_dir}/test_support/one_print ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/test_support/one_print
%{__cp} -a %{tests_source_dir}/test_support/one_socket ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/test_support/one_socket
%{__cp} -a %{tests_source_dir}/test_support/two_socket ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/test_support/two_socket
%{__cp} -a %{tests_source_dir}/test_support/message_one/libmessage.so ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/test_support/message_one/libmessage.so
%{__cp} -a %{tests_source_dir}/test_support/message_two/libmessage.so ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/tests/test_support/message_two/libmessage.so

# Touch the cray dynamic file list which will be populated/updated post-install
touch ${RPM_BUILD_ROOT}/%{prefix}/%{cray_product}/%{pkgversion}/%{cray_dso_list}

%post
# Generate a list of dynamic shared objects (libraries) to add to Cray PE cache
find ${RPM_INSTALL_PREFIX}/%{cray_product}/%{pkgversion} -name '*.so*' > ${RPM_INSTALL_PREFIX}/%{cray_product}/%{pkgversion}/%{cray_dso_list}

# Add Shasta configuration files for PE content projection
if [ "${RPM_INSTALL_PREFIX}" = "%{cray_prefix}" ]; then
    mkdir -p /etc/%{cray_prefix}/admin-pe/bindmount.conf.d/
    mkdir -p /etc/%{cray_prefix}/admin-pe/modulepaths.conf.d/
    echo "%{cray_prefix}/%{product}/%{pkgversion}" > /etc/%{cray_prefix}/admin-pe/bindmount.conf.d/%{cray_name}.conf
    echo "%{cray_prefix}/modulefiles/%{modulefile_name}" > /etc/%{cray_prefix}/admin-pe/modulepaths.conf.d/%{modulefile_name}.conf
fi

# Process relocations
if [ "${RPM_INSTALL_PREFIX}" != "%{prefix}" ]
then
    # Modulefile
    %{__sed} -i "s|^\([[:space:]]*set CTI_BASEDIR[[:space:]]*\)\(.*\)|\1${RPM_INSTALL_PREFIX}/%{cray_product}/%{pkgversion}|" ${RPM_INSTALL_PREFIX}/modulefiles/%{modulefile_name}/%{pkgversion}
    # set default command
    %{__sed} -i "s|^\(export CRAY_inst_dir=\).*|\1${RPM_INSTALL_PREFIX}|" ${RPM_INSTALL_PREFIX}/%{cray_product}/%{pkgversion}/%{set_default_command}_%{cray_name}_%{pkgversion}
else
    # Only call set_default if we are not relocating the rpm
    # Set as default if no default exists either because this is first install
    # or CRAY_INSTALL_DEFAULT=1 and previous default was deleted
    # Only call set_default if we are not relocating the rpm
    # Set as default if no default exists either because this is first install
    # or CRAY_INSTALL_DEFAULT=1 and previous default was deleted
    if [ "${CRAY_INSTALL_DEFAULT}" = "1" ] || [ ! -f ${RPM_INSTALL_PREFIX}/modulefiles/%{modulefile_name}/.version ]
    then
        ${RPM_INSTALL_PREFIX}/%{set_default_path}/%{set_default_command}_%{cray_name}_%{pkgversion}
    else
        ${RPM_INSTALL_PREFIX}/%{set_default_path}/%{set_default_command}_%{cray_name}_%{pkgversion} -cray_links_only
    fi
    # Don't want to set LD_LIBRARY_PATH if we are not relocating since rpath was set properly
    %{__sed} -i "/^ prepend-path[[:space:]]*LD_LIBRARY_PATH.*/d" ${RPM_INSTALL_PREFIX}/modulefiles/%{modulefile_name}/%{pkgversion}
fi

# run ldconfig for good measure
if [ -w /etc/ld.so.cache ]
then
    if [[ ${RPM_INSTALL_PREFIX} = "%{cray_prefix}" ]]
    then
        /sbin/ldconfig
    fi
fi

%post -n %{cray_name}%{pkgversion_separator}devel
# Add Shasta configuration files for PE content projection
if [ "${RPM_INSTALL_PREFIX}" = "%{cray_prefix}" ]; then
    mkdir -p /etc/%{cray_prefix}/admin-pe/modulepaths.conf.d/
    echo "%{cray_prefix}/modulefiles/%{devel_modulefile_name}" > /etc/%{cray_prefix}/admin-pe/modulepaths.conf.d/%{devel_modulefile_name}.conf
fi

# Process relocations
if [ "${RPM_INSTALL_PREFIX}" != "%{prefix}" ]
then
    # Modulefile for the devel package
    %{__sed} -i "s|^\([[:space:]]*set CTI_BASEDIR[[:space:]]*\)\(.*\)|\1${RPM_INSTALL_PREFIX}/%{cray_product}/%{pkgversion}|" ${RPM_INSTALL_PREFIX}/modulefiles/%{devel_modulefile_name}/%{pkgversion}
    # pkg-config pc files
    find ${RPM_INSTALL_PREFIX}/%{cray_product}/%{pkgversion}/lib/pkgconfig -name '*.pc' -exec %{__sed} -i "s|%{prefix}|${RPM_INSTALL_PREFIX}|g" {} \;
    # set default command
    %{__sed} -i "s|^\(export CRAY_inst_dir=\).*|\1${RPM_INSTALL_PREFIX}|" ${RPM_INSTALL_PREFIX}/%{cray_product}/%{pkgversion}/%{set_default_command}_%{cray_name}%{pkgversion_separator}devel_%{pkgversion}

    # dependency resolver
    %{__sed} -i "s|^\(set install_root \).*|\1${RPM_INSTALL_PREFIX}|" ${RPM_INSTALL_PREFIX}/%{cray_product}/%{pkgversion}/%{cray_dependency_resolver}
else
    # Only call set_default if we are not relocating the rpm
    # Set as default if no default exists either because this is first install
    # or CRAY_INSTALL_DEFAULT=1 and previous default was deleted
    if [ "${CRAY_INSTALL_DEFAULT}" = "1" ] || [ ! -f ${RPM_INSTALL_PREFIX}/modulefiles/%{devel_modulefile_name}/.version ]
    then
        ${RPM_INSTALL_PREFIX}/%{set_default_path}/%{set_default_command}_%{cray_name}%{pkgversion_separator}devel_%{pkgversion}

    else
        ${RPM_INSTALL_PREFIX}/%{set_default_path}/%{set_default_command}_%{cray_name}%{pkgversion_separator}devel_%{pkgversion} -cray_links_only

    fi
fi

%postun
# Nothing to do here for shasta.

%files
%defattr(755, root, root)
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/%{release_info_name}
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/%{copyright_name}
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/%{attributions_name}
%attr(755, root, root) %{prefix}/%{cray_product}/%{pkgversion}/lib/libaudit.so
%attr(755, root, root) %{prefix}/%{cray_product}/%{pkgversion}/lib/libcommontools_be.so*
%attr(755, root, root) %{prefix}/%{cray_product}/%{pkgversion}/lib/libcommontools_fe.so*
%attr(755, root, root) %{prefix}/%{cray_product}/%{pkgversion}/libexec/cti_be_daemon%{pkgversion}
%attr(755, root, root) %{prefix}/%{cray_product}/%{pkgversion}/libexec/cti_fe_daemon%{pkgversion}
%attr(755, root, root) %{prefix}/%{cray_product}/%{pkgversion}/%{set_default_command}_%{cray_name}_%{pkgversion}
%attr(755, root, root) %{prefix}/%{set_default_path}/%{set_default_command}_%{cray_name}_%{pkgversion}
%attr(755, root, root) %{prefix}/modulefiles/%{modulefile_name}/%{pkgversion}
%attr(644, root, root) %verify(not md5 size mtime) %{prefix}/%{cray_product}/%{pkgversion}/%{cray_dso_list}

%files -n %{cray_name}%{pkgversion_separator}devel
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

%files -n %{cray_name}%{pkgversion_separator}tests
%attr(755, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/examples/cti_barrier
%attr(755, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/examples/cti_callback
%attr(755, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/examples/cti_callback_daemon
%attr(755, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/examples/cti_info
%attr(755, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/examples/cti_kill
%attr(755, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/examples/cti_launch
%attr(755, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/examples/cti_link
%attr(755, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/examples/cti_transfer
%attr(755, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/examples/cti_wlm
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/examples/testing.info
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/function/avocado_test_params.yaml
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/function/avocado_tests.py
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/function/avo_config.py
%attr(755, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/function/build_run.sh
%attr(755, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/function/function_tests
%attr(755, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/function/hello_mpi.c
%attr(755, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/function/hello_mpi_wait.c
%attr(755, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/scripts/validate_ssh.sh
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/test_support/inputFileData.txt
%attr(755, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/test_support/message_one/libmessage.so
%attr(755, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/test_support/message_two/libmessage.so
%attr(755, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/test_support/one_print
%attr(755, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/test_support/one_socket
%attr(755, root, root) %{prefix}/%{cray_product}/%{pkgversion}/tests/test_support/two_socket
%attr(644, root, root) %{prefix}/%{cray_product}/%{pkgversion}/%{cray_dependency_resolver}


