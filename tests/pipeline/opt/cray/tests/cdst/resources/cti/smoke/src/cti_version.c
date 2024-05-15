#include "common_tools_fe.h"
#include <stdio.h>

int main() {
    const char* version = cti_version();
    if (version == NULL) {
	printf("cti_version returned null\n");
        return -1;
    }

    printf("cti_version: %s\n", version);
    return 0;
}
