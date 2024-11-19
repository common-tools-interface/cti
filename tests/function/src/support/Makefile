# make sure this is run from the directory it lives in

CFLAGS := -Wall -g -O0 $(CFLAGS)

EXECS =  hello_mpi hello_mpi_alt hello_mpi_wait mpi_wrapper remote_filecheck one_socket \
		two_socket one_print

all : $(EXECS)

clean:
	rm -f $(EXECS)
	rm -f ./message_one/libmessage.so ./message_two/libmessage.so

hello_mpi : hello_mpi.c
	cc $(CFLAGS) $^ -o $@

# hello_mpi_alt is a clone of hello_mpi used in mpmd tests
hello_mpi_alt: hello_mpi
	ln -s hello_mpi hello_mpi_alt

hello_mpi_wait : hello_mpi_wait.c
	cc $(CFLAGS) $^ -o $@

hello_mpi_stop : hello_mpi_stop.c
	cc $(CFLAGS) $^ -o $@

mpi_wrapper : mpi_wrapper.c
	cc $(CFLAGS) $^ -o $@

remote_filecheck : remote_filecheck.c
	cc $(CFLAGS) $^ -o $@

one_socket : one_socket.c message_one/libmessage.so
	cc $(CFLAGS) $< -o $@ -I./message_one -L./message_one -lmessage -Wl,-rpath,${PWD}/message_one

two_socket : two_socket.c message_two/libmessage.so
	cc $(CFLAGS) $< -o $@ -I./message_one -L./message_two -lmessage -Wl,-rpath,${PWD}/message_two

one_print : one_print.c message_one/libmessage.so
	cc $(CFLAGS) $< -o $@ -I./message_one -L./message_one -lmessage -Wl,-rpath,${PWD}/message_one

# XXX: using gcc on these libraries because the cray compiler automatically links
#      extra libraries that won't be found when this library is LD_PRELOAD'd
message_one/libmessage.so: message_one/message.c
	gcc -fPIC -shared $^ -o $@

message_two/libmessage.so: message_two/message.c
	gcc -fPIC -shared $^ -o $@

