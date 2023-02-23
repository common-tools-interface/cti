/******************************************************************************\
 * Copyright 2022 Hewlett Packard Enterprise Development LP.
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
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
