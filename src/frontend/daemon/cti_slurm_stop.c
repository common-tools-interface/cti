/******************************************************************************\
 * Copyright 2022 Hewlett Packard Enterprise Development LP.
 * SPDX-License-Identifier: Linux-OpenIB
 ******************************************************************************/

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

int main(int argc, char** argv)
{
	char const* cti_install_dir = getenv("CTI_INSTALL_DIR");
	if (cti_install_dir == NULL) {
		exit(1);
	}

	char* stop_library_path = NULL;
	asprintf(&stop_library_path, "%s/lib/%s", cti_install_dir,
		CTI_STOP_LIBRARY);
	if (stop_library_path == NULL) {
		exit(2);
	}

	char const* ld_preload = getenv("LD_PRELOAD");
	if (ld_preload != NULL) {
		fprintf(stdout, "export LD_PRELOAD=%s:%s\n",
			ld_preload, stop_library_path);
	} else {
		fprintf(stdout, "export LD_PRELOAD=%s\n",
			stop_library_path);
	}

	free(stop_library_path);
}
