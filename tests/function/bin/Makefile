
# Copyright 2020 Hewlett Packard Enterprise Development LP.

CDST = ${CRAY_CDST_SUPPORT_INSTALL_DIR}
CTI = ${CTI_INSTALL_DIR}

TESTS = ../..
SUPPORT = $(TESTS)/test_support
EXTERNAL = $(TESTS)/include
GTEST = $(SUPPORT)/googletest

CRAY_CTI_FE_LIBS = -L$(CTI)/lib -lcommontools_fe
CRAY_CTI_BE_LIBS = -lcommontools_be

PTHREAD_FLAGS = -pthread

TEST_FLAGS = -Wall -g -O0 -I$(CDST)/include -I$(EXTERNAL)  -Wl,-rpath,$(libdir) -Wl,-rpath,$(CTI_INSTALL_DIR)/lib $(CRAY_CTI_FE_LIBS)

EXECS = cti_barrier cti_launch cti_info cti_link cti_callback \
      cti_callback_daemon cti_kill cti_wlm cti_mpmd function_tests

all : $(EXECS)


clean:
	rm -f $(EXECS)

cti_kill : cti_kill_test.c cti_fe_common.c
	cc $(TEST_FLAGS) $^ -o $@

cti_wlm : cti_wlm_test.c cti_fe_common.c
	cc $(TEST_FLAGS) $^ -o $@
n
cti_barrier : cti_barrier_test.c cti_fe_common.c
	cc $(TEST_FLAGS) $^ -o $@

cti_launch : cti_launch_test.c cti_fe_common.c
	cc $(TEST_FLAGS) $^ -o $@

cti_info : cti_info_test.c cti_fe_common.c
	cc $(TEST_FLAGS) $^ -o $@
nn
cti_mpmd : cti_mpmd_test.c cti_fe_common.c
	cc $(TEST_FLAGS) $^ -o $@

cti_link : cti_linking_test.c
	cc $(TEST_FLAGS) $(CRAY_CTI_BE_LIBS) $^ -o $@

cti_callback : cti_callback_test.c
	cc $(TEST_FLAGS) $(PTHREAD_FLAGS) $^ -o $@

cti_callback_daemon : cti_callback_daemon.c
	cc $(TEST_FLAGS) $(CRAY_CTI_BE_CFLAGS) $^ -o $@

function_tests : function_tests.cpp cti_fe_function_test.cpp
	CC -Wall -fPIC -I$(EXTERNAL) -I$(GTEST)/include -I$(GTEST) -I$(EXTERNAL)
           $(PTHREAD_FLAGS) $(CRAY_CTI_FE_LIBS) -L$(GTEST)/googlemock/src -lgtest \
           -ldl -lrt -lstdc++ \
	   -Wl,--enable-new-dtags -Wl,--no-undefined \
           -Wl,-rpath,$(GTEST)/googlemock/src -Wl,-rpath,$(CTI)/lib 




