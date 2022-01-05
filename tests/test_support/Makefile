# Copyright 2021 Hewlett Packard Enterprise Development LP.

ARTIFACTS = libgtest.so libgmock.so message_one/libmessage.so message_two/libmessage.so one_socket libgtest.so libgmock.so

GTEST = googletest/googletest
GMOCK = googletest/googlemock

all: $(ARTIFACTS)

clean:
	rm -f $(ARTIFACTS)

libgtest.so : googletest/googletest/src/gtest-all.cc
	$(CXX) -pthread -fPIC -I$(GTEST)/include -I$(GTEST) -shared $^ -o $@

libgmock.so : googletest/googlemock/src/gmock-all.cc
	$(CXX) -pthread -fPIC -I$(GTEST)/include -I$(GMOCK)/include -I$(GMOCK) -shared $^ -o $@


message_one/libmessage.so : message_one/message.c message_one/message.h
	gcc -fPIC -shared message_one/message.c -o message_one/libmessage.so

message_two/libmessage.so : message_two/message.c message_two/message.h
	gcc -fPIC -shared message_two/message.c -o message_two/libmessage.so

one_socket : one_socket.c message_one/libmessage.so
	gcc one_socket.c -L./message_one -lmessage -Wl,-rpath,${CURDIR}/message_one -o one_socket

