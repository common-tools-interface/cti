# Copyright 2020 Hewlett Packard Enterprise Development LP.

CTI = ${CTI_INSTALL_DIR}

TESTS = ../..
SUPPORT = $(TESTS)/test_support
EXTERNAL = $(TESTS)/include
GTEST = $(SUPPORT)/googletest/googletest

CRAY_CTI_FE_LIBS = -L$(CTI)/lib -lcommontools_fe
CRAY_CTI_BE_LIBS = -lcommontools_be

PTHREAD_FLAGS = -pthread

TEST_FLAGS = -Wall -g -O0 -I$(EXTERNAL) -I$(CTI)/include -Wl,-rpath,$(CTI)/lib $(CRAY_CTI_FE_LIBS)

EXECS = cti_barrier cti_launch cti_info cti_link cti_callback \
      cti_callback_daemon cti_kill cti_wlm cti_mpmd function_tests

all : check-cti $(EXECS)


clean:
	rm -f $(EXECS)

no-module-error:
	$(error module cray-cti-devel must be loaded to build tests) 

check-cti :
	echo "CTI="$(CTI)
	@if [ ! -d $(CTI)/include ]; then $(MAKE) no-module-error; fi

cti_kill : cti_kill_test.c cti_fe_common.c
	$(CC) $(TEST_FLAGS) $^ -o $@

cti_wlm : cti_wlm_test.c cti_fe_common.c
	$(CC) $(TEST_FLAGS) $^ -o $@

cti_barrier : cti_barrier_test.c cti_fe_common.c
	$(CC) $(TEST_FLAGS) $^ -o $@

cti_launch : cti_launch_test.c cti_fe_common.c
	$(CC) $(TEST_FLAGS) $^ -o $@

cti_info : cti_info_test.c cti_fe_common.c
	$(CC) $(TEST_FLAGS) $^ -o $@

cti_mpmd : cti_mpmd_test.c cti_fe_common.c
	$(CC) $(TEST_FLAGS) $^ -o $@

cti_link : cti_linking_test.c
	$(CC) $(TEST_FLAGS) $(CRAY_CTI_BE_LIBS) $^ -o $@

cti_callback : cti_callback_test.c
	$(CC) $(TEST_FLAGS) $(PTHREAD_FLAGS) $^ -o $@

cti_callback_daemon : cti_callback_daemon.c
	$(CC) -Wall -g -O0 -I$(EXTERNAL) -I$(CTI)/include \
       -Wl,-rpath,$(CTI)/lib -fPIC -L$(CTI)/lib $(CRAY_CTI_BE_LIBS) $^ -o $@

function_tests : function_tests.cpp cti_fe_function_test.cpp
	$(CXX) $(TEST_FLAGS) -fPIC -I$(GTEST)/include -I$(GTEST) \
       $(PTHREAD_FLAGS) -L$(SUPPORT) -lgtest \
       -ldl -lrt -lstdc++ -Wl,--enable-new-dtags -Wl,--no-undefined \
       -Wl,-rpath,$(SUPPORT) \
       $^ -o $@
