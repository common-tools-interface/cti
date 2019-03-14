/*********************************************************************************\
 * audit.c - A custom rtld audit interface to deliver locations of loaded dso's
 *	         over stdout.
 *
 * Copyright 2011-2014 Cray Inc.  All Rights Reserved.
 *
 * Unpublished Proprietary Information.
 * This unpublished work is protected to trade secret, copyright and other laws.
 * Except as permitted by contract or express written permission of Cray Inc.,
 * no part of this work or its content may be used, reproduced or disclosed
 * in any form.
 *
 * $HeadURL$
 * $Date$
 * $Rev$
 * $Author$
 *
 *********************************************************************************/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <limits.h>
#include <link.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ld_val_defs.h"

// This is always the first thing called
unsigned int
la_version(unsigned int version)
{
	// Set stderr to be fully buffered. This should not be larger than the
	// capicity of the pipe, otherwise we will have problems.
	// XXX: How to handle failure? I don't think this will matter too much if
	// things fail.
	setvbuf(stderr, NULL, _IOFBF, READ_BUF_LEN);
	
	return version;
}

// This is called every time a shared library is loaded
unsigned int
la_objopen(struct link_map *map, Lmid_t lmid, uintptr_t *cookie)
{
	char *s, *c;

	// Ensure the library name has a length, otherwise return
	if (strlen(map->l_name) != 0)
	{
		// write the lib string followed by a null terminator
		fprintf(stderr, "%s%c", map->l_name, '\0');
		// flush the output
		fflush(stderr);
	}

	// return normally
	return LA_FLG_BINDTO | LA_FLG_BINDFROM;
}

