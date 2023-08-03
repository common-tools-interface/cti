/******************************************************************************\
 * Copyright 2022 Hewlett Packard Enterprise Development LP.
 * SPDX-License-Identifier: Linux-OpenIB
 ******************************************************************************/

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>

__attribute__((constructor))
static void libctistop_constructor()
{
	raise(SIGSTOP);
}
