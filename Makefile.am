#
# Top level Makefile.am for the common tools interface.
#
# Copyright 2011-2019 Cray Inc. All Rights Reserved.
#

ACLOCAL_AMFLAGS = -I m4
AM_LIBTOOLFLAGS = --quiet

SUBDIRS = src tests external

EXTRA_DIST = pkgconfig \
             release_versioning

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = pkgconfig/common_tools_be.pc \
                 pkgconfig/common_tools_fe.pc \
                 pkgconfig/patchAPI.pc \
                 pkgconfig/parseAPI.pc \
                 pkgconfig/dynC_API.pc \
                 pkgconfig/dynElf.pc \
                 pkgconfig/dynDwarf.pc \
                 pkgconfig/common.pc \
                 pkgconfig/symLite.pc \
                 pkgconfig/pcontrol.pc \
                 pkgconfig/instructionAPI.pc \
                 pkgconfig/dyninstAPI.pc \
                 pkgconfig/dyninstAPI_RT.pc \
                 pkgconfig/stackwalk.pc \
                 pkgconfig/symtabAPI.pc


include_HEADERS = include/common_tools_be.h include/common_tools_fe.h \
				  include/common_tools_shared.h

.PHONY: tests
tests:
	cd tests && $(MAKE) $(AM_MAKEFLAGS) tests

.PHONY: rpm
rpm:	RPMS/$(ARCH)/$(PKG) $(PKG).yaml

.PHONY: dirs
dirs:
	mkdir -p SOURCES BUILD/$(PKG_NAME)-$(PKG_VERSION) RPMS/$(ARCH)

$(NAME).spec: $(CUR_DIR)/scripts/spec.template
	sed -e"s:\[namespace\]:cray:g" \
	-e"s:\[intranamespace_name\]:$(NAME):g" \
        -e"s:\[package_branch_two_digit\]:$(TWO_DIGIT_VER):g" \
        -e"s:\[package_branch_two_digit_nodot\]:$(TWO_DIGIT_NODOT_VER):g" \
        -e"s:\[version\]:$(PKG_VERSION):g" \
        -e"s:\[rpm_release\]:$(REL):g" \
        -e"s:\[cur_path\]:$(CUR_DIR):g" \
        -e"s:\[sles_version\]:$(OS_VER):g" \
        -e"s:\[tarball\]:$(PKG_NAME)-$(PKG_VERSION).tgz:g" $< > $@
	sed --in-place "/Packager/i URL: $(GIT_FULL_REV)" $@

RPMS/$(ARCH)/$(PKG): $(NAME).spec SOURCES/$(PKG_NAME)-$(PKG_VERSION).tgz
	rpmbuild -bb --define "_tmppath $(PWD)" --define "_topdir $(PWD)" ./$(NAME).spec

$(PKG).yaml: $(CUR_DIR)/scripts/yaml_template
	sed -e"s:\[VERSION\]:$(PKG_VERSION):g" \
	-e"s:\[BUILD_METADATA\]:$(BUILD_METADATA):g" \
	-e"s:\[ARCH\]:$(ARCH):g" \
	-e"s:\[OS_WB\]:$(OS_LIST_WB):g" \
	-e"s:\[OS_HW\]:$(OS_LIST_HW):g" \
	-e"s:\[SYSTEM_TYPE_WB\]:$(SYSTEM_TYPE_WB):g" \
	-e"s:\[SYSTEM_TYPE_HW\]:$(SYSTEM_TYPE_HW):g" \
	-e"s:\[SYSTEM_TYPE_REMOVE\]:$(SYSTEM_TYPE_REMOVE):g" $< > $@
	cp $@ RPMS/$(ARCH)/

set_default_$(PKG_NAME)_$(PKG_VERSION): set_default/set_default_template
	cp set_default/set_default_template .
	sed -e"s:\[product_name\]:$(NAME):g" \
	-e"s:\[version_string\]:$(PKG_VERSION):g" \
	-e"s:\[module_name_list\]:$(PKG_NAME):g" $< > $@
	cp $@ $(INSTALL_DIR)/$(PKG_VERSION)/

set_pkgconfig_default_$(PKG_NAME): $(CUR_DIR)/scripts/set_pkgconfig_default_template
	sed -e"s:\[version\]:$(PKG_VERSION):g" $< > $@
	cp $@ $(INSTALL_DIR)/$(PKG_VERSION)/

module: $(CUR_DIR)/scripts/module_template
	sed -e"s:<version>:$(PKG_VERSION):g" $< > $@
	mv $(CUR_DIR)/module $(CUR_DIR)/$(PKG_VERSION)
	cp $(CUR_DIR)/$(PKG_VERSION) $(INSTALL_DIR)/$(PKG_VERSION)/

release_info: $(CUR_DIR)/scripts/release_info_template
	sed -e"s:\[VERSION\]:$(PKG_VERSION):g" \
	-e"s:\[BUILD_METADATA\]:$(BUILD_METADATA):g" \
	-e"s:\[DATE\]:$(REL_DATE):g" \
	-e"s:\[OS\]:$(OS_VER):g" \
	-e"s:\[ARCH\]:$(ARCH):g" $< > $@
	cp $@ $(INSTALL_DIR)/$(PKG_VERSION)/

SOURCES/$(PKG_NAME)-$(PKG_VERSION).tgz: set_default_$(PKG_NAME)_$(PKG_VERSION) set_pkgconfig_default_$(PKG_NAME) module release_info dirs
	mv $(word 2,$^) $(INSTALL_DIR)/$(PKG_VERSION)
	cp $< $(INSTALL_DIR)/$(PKG_VERSION)
	tar czvf $(PKG_NAME)-$(PKG_VERSION).tgz $(INSTALL_DIR)/$(PKG_VERSION) \
		--transform s:opt/cray/pe/cti:$(PKG_NAME)-$(PKG_VERSION)$(INSTALL_DIR):g
	mkdir -p  $(CUR_DIR)/BUILD/$(PKG_NAME)-$(PKG_VERSION)
	cp $(PKG_NAME)-$(PKG_VERSION).tgz $@

distclean-local:
	rm -f set_default_cray-cti_*
	rm -f release_info
	rm -f cti.spec
	rm -f *.tgz
	rm -f *.yaml
	rm -rf BUILD
	rm -rf BUILDROOT
	rm -rf SOURCES
	rm -rf RPMS
	rm -rf SRPMS
	rm -rf SPECS
