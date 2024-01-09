# this makefile assumes that the cray-cti-devel module is loaded and that cc and pkg-config are available.
# each rule maps to a test in tests.py of the same name.

FE_PKG_CONFIG = $(shell pkg-config --cflags --libs common_tools_fe)

PREBUILT = \
  test_cti_version \
  test_cti_current_wlm

ALL = \
  test_build \
  $(PREBUILT)

all: $(ALL)

bindir:
	install -d ./bin

test_build: bindir
	cc -g -O0 $(FE_PKG_CONFIG) ./src/cti_version.c -o ./bin/test_build

# prebuilt binaries are built at cti build time and included with the test rpm.
# since these are built at cti build time, the cti pkg-config is not available.
# for development, set CTI_INSTALL_DIR.

# taken from the cti pkg-config
PREBUILT_CFLAGS=-I$(CTI_INSTALL_DIR)/include
PREBUILT_LIBS=-L$(CTI_INSTALL_DIR)/lib -Wl,--enable-new-dtags -Wl,-rpath,/opt/cray/pe/lib64 -Wl,-rpath,/opt/cray/pe/cti/default/lib -lcommontools_fe
PREBUILT_FLAGS=$(PREBUILT_CFLAGS) $(PREBUILT_LIBS)

prebuilt: prebuiltdir $(PREBUILT)

prebuiltdir:
	install -d ./prebuilt

test_cti_version: prebuiltdir
	cc -g -O0 $(PREBUILT_FLAGS) ./src/cti_version.c -o ./prebuilt/test_cti_version

test_cti_current_wlm: prebuiltdir
	cc -g -O0 $(PREBUILT_FLAGS) ./src/cti_current_wlm.c -o ./prebuilt/test_cti_current_wlm

clean:
	rm -rf ./bin ./prebuilt
