all: print_one/print.so print_two/print.so one_printer two_printer

print_one/print.so: print_one/print.c
	gcc -shared -o print_one/libprint.so -fPIC print_one/print.c

print_two/print.so: print_two/print.c
	gcc -shared -o print_two/libprint.so -fPIC print_two/print.c

one_printer: one_printer.c print_one/print.so
	gcc one_printer.c -Wl,-rpath,\$$ORIGIN/print_one -Lprint_one -lprint -o one_printer

two_printer: two_printer.c print_two/print.so
	gcc two_printer.c -Wl,-rpath,\$$ORIGIN/print_two -Lprint_two -lprint -o two_printer
