/******************************************************************************\
 *
 * Copyright 2019-2020 Hewlett Packard Enterprise Development LP.
 * SPDX-License-Identifier: Linux-OpenIB
 ******************************************************************************/

#include <stdio.h>
#include <unistd.h>
#include "message_one/message.h"

int main(int argc, char** argv) {
    printf("%s", get_message());
    return 0;
}

