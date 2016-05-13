#!/usr/bin/env python
content='''%define namespace [namespace]
%define intranamespace_name [intranamespace_name]
%define two_dig_version [package_branch_two_digit]
%define two_dig_nodot_version [package_branch_two_digit_nodot]
%define scripts_d [script_dir]

#%define man_source /cray/css/compiler/comp_rel/pubs/manpages/lgdb/%{two_dig_version}/xt_lgdb_%{two_dig_nodot_version}.cpio

Summary: Performance Application Programming Interface
Name: %{namespace}-%{intranamespace_name}
Version: [version] 
Release: [rpm_release]_[package_revision][sles_sub]
License: BSD
Group: Development/System
URL: [url]
Source: [tarball]
BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}-root
Prefix: /opt/cray/pe


%define _use_internal_dependency_generator 0
%define __find_requires %{scripts_d}/find-requires
%define debug_package%{nil}

%description
Cray Tools Interface.

# version
%define major_version %(echo %{version} | awk -v n=1 'BEGIN { FS = "." } ; { print $n }')
%define minor_version %(echo %{version} | awk -v n=2 'BEGIN { FS = "." } ; { print $n }')

# _prefix
%define _namespace_prefix %{prefix}
%define _name_prefix %{_namespace_prefix}/%{intranamespace_name}
%define _version_prefix %{_name_prefix}/%{version}

# _moduledir
%define _namespace_moduledir %{prefix}/modulefiles
%define _name_moduledir %{_namespace_moduledir}/%{namespace}-%{intranamespace_name}

%prep
%setup
%build

%install
mkdir -p %{buildroot}%{_version_prefix}
cp -r cray-cti/* %{buildroot}%{_version_prefix}/
rm -rf %{buildroot}%{_version_prefix}/modulefiles

install -D modulefile/%{version} %{buildroot}%{_name_moduledir}/%{version}
install -D cray-cti/release_info %{buildroot}%{_version_prefix}/
echo "%{version}-%{release}" > %{buildroot}%{_version_prefix}/.cray_rpm_release
mkdir -p %{buildroot}%{_namespace_prefix}/admin-pe/set_default_files/
install -D set_default_%{namespace}-%{intranamespace_name}_%{version} %{buildroot}%{_namespace_prefix}/admin-pe/set_default_files/set_default_%{namespace}-%{intranamespace_name}_%{version}
mkdir -p %{buildroot}%{_namespace_prefix}/admin-pe/pkgconfig_default_files/
install -D set_pkgconfig_default_%{namespace}-%{intranamespace_name}_%{version} %{buildroot}%{_namespace_prefix}/admin-pe/pkgconfig_default_files/set_pkgconfig_default_%{namespace}-%{intranamespace_name}_%{version}
install -D set_default_%{namespace}-%{intranamespace_name}_%{version} %{buildroot}%{_version_prefix}/
install -D set_pkgconfig_default_%{namespace}-%{intranamespace_name}_%{version} %{buildroot}%{_version_prefix}/
mv cray-cti/docs/ATTRIBUTIONS_cti.txt %{buildroot}%{_version_prefix}/ATTRIBUTIONS_cti.%{version}.txt

# hardlink duplicate files to save space
#%fdupes %{buildroot}

%clean
#rm -rf $RPM_BUILD_ROOT

%files
%defattr(-,root,root,-)
%dir %{_name_moduledir}
%dir %{_namespace_prefix}
%dir %{_name_prefix}
%dir %{_version_prefix}
%dir %{_version_prefix}/docs
%dir %{_version_prefix}/libexec
%dir %{_version_prefix}/lib
%dir %{_version_prefix}/include

%{_version_prefix}/libexec/
%{_version_prefix}/libexec/cti_overwatch
%{_version_prefix}/libexec/cti_slurm_step_util.sh
%{_version_prefix}/libexec/cti_starter
%{_version_prefix}/libexec/cti_attach
%{_version_prefix}/libexec/cti_dlaunch1.0
%{_version_prefix}/libexec/cti_approved_gdb

%{_version_prefix}/lib
%{_version_prefix}/lib/libaudit.so
%{_version_prefix}/lib/libaudit.la
%{_version_prefix}/lib/libcraytools_fe.so.1.0.0
%{_version_prefix}/lib/libcraytools_fe.so.1
%{_version_prefix}/lib/libcraytools_fe.so
%{_version_prefix}/lib/libcraytools_fe.la
%{_version_prefix}/lib/libcraytools_fe.a
%{_version_prefix}/lib/libcraytools_be.so.1.0.0
%{_version_prefix}/lib/libcraytools_be.so.1
%{_version_prefix}/lib/libcraytools_be.so
%{_version_prefix}/lib/libcraytools_be.la
%{_version_prefix}/lib/libcraytools_be.a
%{_version_prefix}/lib/libmi.so
%{_version_prefix}/lib/pkgconfig/
%{_version_prefix}/lib/pkgconfig/craytools_be.pc
%{_version_prefix}/lib/pkgconfig/craytools_fe.pc


%{_version_prefix}/include
%{_version_prefix}/include/cray_tools_be.h
%{_version_prefix}/include/cray_tools_fe.h

%{_namespace_prefix}/admin-pe/set_default_files/set_default_%{namespace}-%{intranamespace_name}_%{version}
%{_name_moduledir}/%{version}
%{_version_prefix}/.cray_rpm_release
%{_version_prefix}/set_default_%{namespace}-%{intranamespace_name}_%{version}
%{_version_prefix}/release_info
%{_version_prefix}/ATTRIBUTIONS_cti.%{version}.txt
%{_version_prefix}/docs/ATTRIBUTIONS_cti.txt
%{_namespace_prefix}/admin-pe/pkgconfig_default_files/set_pkgconfig_default_%{namespace}-%{intranamespace_name}_%{version}
%{_version_prefix}/set_pkgconfig_default_%{namespace}-%{intranamespace_name}_%{version}


%post

#
# Set as default if no default exists either because this is first install of
# cti or CRAY_INSTALL_DEFAULT=1 and previous default was deleted
#

#Set the install path in the modulefile & set_default script(s)
sed -i "s,\[install_dir\],$RPM_INSTALL_PREFIX,g" \
$RPM_INSTALL_PREFIX/modulefiles/%{name}/%{version} \
$RPM_INSTALL_PREFIX/admin-pe/set_default_files/set_default_%{name}_%{version} \
$RPM_INSTALL_PREFIX/%{intranamespace_name}/%{version}/set_default_%{name}_%{version} \
$RPM_INSTALL_PREFIX/admin-pe/pkgconfig_default_files/set_pkgconfig_default_%{name}_%{version} \
$RPM_INSTALL_PREFIX/%{intranamespace_name}/%{version}/set_pkgconfig_default_%{name}_%{version}

sed -i "s,\[install_dir\],$RPM_INSTALL_PREFIX/%{intranamespace_name}/%{version},g" \
$RPM_INSTALL_PREFIX/%{intranamespace_name}/%{version}/lib/pkgconfig/craytools_be.pc

sed -i "s,\[install_dir\],$RPM_INSTALL_PREFIX/%{intranamespace_name}/%{version},g" \
$RPM_INSTALL_PREFIX/%{intranamespace_name}/%{version}/lib/pkgconfig/craytools_fe.pc

find $RPM_INSTALL_PREFIX/%{intranamespace_name}/%{version} | grep "libcraytools_[bf]e\.so\.1" \
| sed "/.*\.so\.[0-9]\.[0-9]/d" > $RPM_INSTALL_PREFIX/%{intranamespace_name}/%{version}/.cray_dynamic_file_list


# prevent echo of new directory as it messes up install output
if [[ $RPM_INSTALL_PREFIX = "/opt/cray" ]] || [[ $RPM_INSTALL_PREFIX = "/opt/cray/pe" ]]
  then
    if [ ${CRAY_INSTALL_DEFAULT:-0} -eq 1 ] || [ ! -f $RPM_INSTALL_PREFIX/modulefiles/%{name}/.version ]
    then
      $RPM_INSTALL_PREFIX/admin-pe/set_default_files/set_default_%{name}_%{version}
    else
      echo "%{name}_%{version} has been installed as non-default."
    fi
fi

%preun
# Cleanup default link if it point to this
# version-%{intranamespace_name}-%{intranamespace_name}
rm $RPM_INSTALL_PREFIX/%{intranamespace_name}/%{version}/.cray_dynamic_file_list
default_link="${RPM_INSTALL_PREFIX}/%{intranamespace_name}/default"
version="%{version}"

# Cleanup module .version if it points to this version
if [ -f ${RPM_INSTALL_PREFIX}/modulefiles/%{namespace}-%{intranamespace_name}/.version ]
then
  dotversion=`grep ModulesVersion ${RPM_INSTALL_PREFIX}/modulefiles/%{namespace}-%{intranamespace_name}/.version | cut -f2 -d'"'`
  
  if [ "$dotversion" = "$version" ]
  then
    /bin/rm -f ${RPM_INSTALL_PREFIX}/modulefiles/%{namespace}-%{intranamespace_name}/.version
    echo "Uninstalled version and .version file match = ${version}."
    echo "Removing %{intranamespace_name} .version file."
    rm -f ${default_link}
  fi
fi

if [[ -z `ls ${RPM_INSTALL_PREFIX}/%{intranamespace_name}` ]]
then
  rm -rf ${RPM_INSTALL_PREFIX}/%{intranamespace_name}
fi

if [[ -z `ls ${RPM_INSTALL_PREFIX}/modulefiles/%{namespace}-%{intranamespace_name}`/ ]]
then
  rm -rf ${RPM_INSTALL_PREFIX}/modulefiles/%{namespace}-%{intranamespace_name}
fi

%changelog

'''