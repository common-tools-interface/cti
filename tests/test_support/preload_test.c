#include <stdio.h>
#include <unistd.h>

__attribute__ ((weak)) char const* get_message() {
    return "In weak function!";
}

int main(int argc, char** argv) {
    printf("%s", get_message());
    return 0;
}

