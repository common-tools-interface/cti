# Copyright 2021 Hewlett Packard Enterprise Development LP.

CTI = ${CTI_INSTALL_DIR}

TESTS = ../..

CRAY_CTI_FE_LIBS = -L$(CTI)/lib -lcommontools_fe
CRAY_CTI_BE_LIBS = -lcommontools_be

PTHREAD_FLAGS = -pthread

CC  := cc
CXX := $(shell echo $(shell which CC || echo g++))

CFLAGS = -Wall -g -O0 -I$(CTI)/include
LDFLAGS = -Wl,-rpath,$(CTI)/lib $(CRAY_CTI_FE_LIBS)

EXECS = cti_barrier cti_launch cti_launch_badenv cti_info cti_link cti_callback \
      cti_callback_daemon cti_kill cti_wlm cti_mpmd cti_file_transfer \
	  cti_tool_daemon cti_tool_daemon_argv cti_tool_daemon_badenv \
	  cti_manifest cti_session cti_environment cti_fd_in cti_double_daemon cti_ld_preload \
	  cti_mpir_shim cti_release_twice cti_redirect cti_file_in cti_release_app cti_ops \
	  cti_mpmd_daemon cti_multithread

all : support tests

tests : check-cti $(EXECS)

clean:
	rm -f $(EXECS)
	$(MAKE) -C support clean

no-module-error:
	$(error module cray-cti-devel must be loaded to build tests) 

support: FORCE
	PWD=${PWD}/support $(MAKE) -C support all

check-cti :
	echo "CTI="$(CTI)
	@if [ ! -d $(CTI)/include ]; then $(MAKE) no-module-error; fi

cti_kill : cti_kill_test.cpp cti_fe_common.c cti_fe_function_test.cpp
	$(CXX) $(CFLAGS) $^ -o $@ $(LDFLAGS)

cti_wlm : cti_wlm_test.c cti_fe_common.c
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

cti_barrier : cti_barrier_test.c cti_fe_common.c
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

cti_launch : cti_launch_test.c cti_fe_common.c
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

cti_launch_badenv : cti_launch_badenv_test.cpp cti_fe_function_test.cpp
	$(CXX) $(CFLAGS) $^ -o $@ $(LDFLAGS)

cti_info : cti_info_test.c cti_fe_common.c
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

cti_mpmd : cti_mpmd_test.c cti_fe_common.c
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

cti_mpmd_daemon : cti_mpmd_daemon_test.cpp cti_fe_common.c
	$(CXX) $(CFLAGS) $^ -o $@ cti_fe_function_test.cpp $(LDFLAGS)

cti_link : cti_linking_test.c
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS) $(CRAY_CTI_BE_LIBS)

cti_callback : cti_callback_test.c
	$(CC) $(PTHREAD_FLAGS) $(CFLAGS) $^ -o $@ $(LDFLAGS)

cti_callback_daemon : cti_callback_daemon.c
	$(CC) -Wall -g -O0 -I$(CTI)/include $^ -o $@ \
      -Wl,-rpath,$(CTI)/lib -fPIC -L$(CTI)/lib $(CRAY_CTI_BE_LIBS)

cti_file_transfer : cti_file_transfer_test.cpp
	$(CXX) $(CFLAGS) $^ -o $@ cti_fe_function_test.cpp $(LDFLAGS)

cti_tool_daemon : cti_tool_daemon_test.cpp
	$(CXX) $(CFLAGS) $^ -o $@ cti_fe_function_test.cpp $(LDFLAGS)

cti_tool_daemon_argv : cti_tool_daemon_argv_test.cpp
	$(CXX) $(CFLAGS) $^ -o $@ cti_fe_function_test.cpp $(LDFLAGS)

cti_tool_daemon_badenv : cti_tool_daemon_badenv_test.cpp
	$(CXX) $(CFLAGS) $^ -o $@ cti_fe_function_test.cpp $(LDFLAGS)

cti_manifest : cti_manifest_test.cpp
	$(CXX) $(CFLAGS) $^ -o $@ cti_fe_function_test.cpp $(LDFLAGS)

cti_session : cti_session_test.cpp
	$(CXX) $(CFLAGS) $^ -o $@ cti_fe_function_test.cpp $(LDFLAGS)

cti_environment : cti_environment_test.cpp
	$(CXX) $(CFLAGS) $^ -o $@ cti_fe_function_test.cpp $(LDFLAGS)

cti_fd_in : cti_fd_in_test.cpp
	$(CXX) $(CFLAGS) $^ -o $@ cti_fe_function_test.cpp $(LDFLAGS)

cti_double_daemon : cti_double_daemon_test.cpp
	$(CXX) $(CFLAGS) $^ -o $@ cti_fe_function_test.cpp $(LDFLAGS)

cti_ld_preload : cti_ld_preload_test.cpp
	$(CXX) $(CFLAGS) $^ -o $@ cti_fe_function_test.cpp $(LDFLAGS)

cti_mpir_shim : cti_mpir_shim_test.cpp
	$(CXX) $(CFLAGS) $^ -o $@ cti_fe_function_test.cpp $(LDFLAGS)

cti_release_twice : cti_release_twice_test.cpp
	$(CXX) $(CFLAGS) $^ -o $@ cti_fe_function_test.cpp $(LDFLAGS)

cti_redirect : cti_redirect_test.cpp
	$(CXX) $(CFLAGS) $^ -o $@ cti_fe_function_test.cpp $(LDFLAGS)

cti_file_in : cti_file_in_test.cpp
	$(CXX) $(CFLAGS) $^ -o $@ cti_fe_function_test.cpp $(LDFLAGS)

cti_release_app : cti_release_app_test.cpp
	$(CXX) $(CFLAGS) -std=c++17 $^ -o $@ cti_fe_function_test.cpp $(LDFLAGS)

cti_ops : cti_ops_test.cpp
	$(CXX) $(CFLAGS) -std=c++17 $^ -o $@ cti_fe_function_test.cpp $(LDFLAGS)

cti_multithread : cti_multithread_test.cpp
	$(CXX) $(CFLAGS) -std=c++17 $^ -o $@ cti_fe_function_test.cpp $(LDFLAGS) $(PTHREAD_FLAGS)

# dummy rule to force other rules to run every time
FORCE: ;
