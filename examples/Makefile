CC = cc
CFLAGS += $(shell pkg-config --cflags craytools_fe)
LDFLAGS += $(shell pkg-config --libs craytools_fe)

all:
	${CC} $(CFLAGS) $(LDFLAGS) cti_transfer_example.c -o cti_transfer
clean:
	rm -f cti_transfer
