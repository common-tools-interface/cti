#include <stdio.h>
#include <unistd.h>

char const* get_message() __attribute__ ((weak));

int main(int argc, char** argv) {
    if (!get_message) {
        printf("Missing linkage to weak symbol!");
        return 1;
    }

    printf("%s", get_message());
    return 0;
}

