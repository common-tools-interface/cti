# Copyright 2020 Hewlett Packard Enterprise Development LP.

# test_support is used by both the unit tests and the functional tests.   For the
# functional tests we - I think - want a version of these files compiled on the
# target machine.   Since autotools is not necessarily available, we include a
# vanilla Makefile set-up to build it there.

ARTIFACTS = libgtest.so libgmock.so message_one/libmessage.so message_two/libmessage.so one_socket two_socket remote_filecheck

GTEST = googletest/googletest
GMOCK = googletest/googlemock

all: $(ARTIFACTS)

clean:
	rm -f $(ARTIFACTS)

libgtest.so : googletest/googletest/src/gtest-all.cc
	CC -pthread -I$(GTEST)/include -I$(GTEST) -shared $^ -o $@

libgmock.so : googletest/googlemock/src/gmock-all.cc
	CC -pthread -I$(GTEST)/include -I$(GMOCK)/include -I$(GMOCK) -shared $^ -o $@


message_one/libmessage.so : message_one/message.c message_one/message.h
	gcc -fPIC -shared message_one/message.c -o message_one/libmessage.so

message_two/libmessage.so : message_two/message.c message_two/message.h
	gcc -fPIC -shared message_two/message.c -o message_two/libmessage.so

one_socket : one_socket.c message_one/libmessage.so
	gcc one_socket.c -L./message_one -lmessage -Wl,-rpath,${CURDIR}/message_one -o one_socket

two_socket : two_socket.c message_two/libmessage.so 
	gcc two_socket.c -L./message_two -lmessage -Wl,-rpath,${CURDIR}/message_two -o two_socket

remote_filecheck : remote_filecheck.c
	gcc $^ -o $@






