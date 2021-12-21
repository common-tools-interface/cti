#!/bin/bash

# backup method to build the binaries that need to be built on the
# testing machine.   Currently manual, this should get integrated into
# the test process sooner or later (or autotools will become universally
# available)

#  Copyright 2021 Hewlett Packard Enterprise Development LP.
  
cd test_support

gcc -fPIC -shared message_one/message.c -o message_one/libmessage.so
gcc -fPIC -shared message_two/message.c -o message_two/libmessage.so

gcc -fPIC -shared message_one/message.c -o message_one/libmessage.so
gcc -g -O0 -L./message_one -lmessage -Wl,-rpath,${PWD}/message_one -o one_print one_print.c
gcc -g -O0 -L./message_one -lmessage -Wl,-rpath,${PWD}/message_one -o one_socket one_socket.c
gcc -g -O0 -L./message_two -lmessage -Wl,-rpath,${PWD}/message_two -o two_socket two_socket.c
gcc remote_filecheck.c -o remote_filecheck

cd googletest

CC -fPIC googlemock/src/gmock-all.cc -Igooglemock/include -Igooglemock -Igoogletest/include  --shared -o googlemock/src/libgmock.so

CC -fPIC googletest/src/gtest-all.cc -Igoogletest/include -Igoogletest --shared -o googlemock/src/libgtest.so

cd ../../function/src
cc -g -O0 -o hello_mpi hello_mpi.c
cc -g -O0 -o hello_mpi_wait hello_mpi_wait.c
cc -g -O0 -o mpi_wrapper mpi_wrapper.c

