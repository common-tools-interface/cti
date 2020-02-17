#include <stdio.h>
#include <unistd.h>

char const* get_message() __attribute__ ((weak)) {
    return "In weak function!";
}

int main(int argc, char** argv) {
    printf("%s", get_message());
    return 0;
}

