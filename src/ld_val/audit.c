/*********************************************************************************\
 * audit.c - A custom rtld audit interface to deliver locations of loaded dso's
 *	   over a shared memory segment. This is the dynamic portion of the
 *	   code which is used by the static library interface.
 *
 * © 2011-2013 Cray Inc.  All Rights Reserved.
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

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/shm.h>

#include "ld_val_defs.h"

static int		sem_ctrlid = 0;
static int		shmid = 0;
static char *	shm = NULL;

// This is always the first thing called
unsigned int
la_version(unsigned int version)
{
	char *	key_file = NULL;
	key_t	key_a = 0;
	key_t	key_b = 0;
	
	// get the location of the keyfile or else set it to the default value
	if ((key_file = getenv(LIBAUDIT_KEYFILE_ENV_VAR)) == (char *)NULL)
	{
		key_file = DEFAULT_KEYFILE;
	}
	
	// Lets attach to our shm segments
	if (shm == NULL)
	{
		// create the shm key
		if ((key_a = ftok(key_file, ID_A)) == (key_t)-1)
		{
			return version;
		}
		
		// locate the segment
		if ((shmid = shmget(key_a, PATH_MAX, SHM_R | SHM_W)) < 0) 
		{
			shmid = 0;
			return version;
		}
		
		// now attach the segment to our data space
		if ((shm = shmat(shmid, NULL, 0)) == (char *) -1) 
		{
			shm = NULL;
			return version;
		}
	}

	if (sem_ctrlid == 0)
	{
		// create the shm key
		if ((key_b = ftok(key_file, ID_B)) == (key_t)-1)
		{
			return version;
		}
		
		// get the id of the semaphore
		if ((sem_ctrlid = semget(key_b, 0, 0)) < 0)
		{
			sem_ctrlid = 0;
			return version;
		}
	}

	return version;
}

unsigned int
la_objopen(struct link_map *map, Lmid_t lmid, uintptr_t *cookie)
{
	char *s, *c;
	struct sembuf	sops[1];

	// return if opening of the shm segments failed
	if ((shmid == 0) || (sem_ctrlid == 0))
		return LA_FLG_BINDTO | LA_FLG_BINDFROM;

	if (strlen(map->l_name) != 0)
	{
		// wait for a resource from ld_val
		sops[0].sem_num = LDVAL_SEM;	// operate on ld_val sema
		sops[0].sem_op = -1;			// grab 1 resource
		sops[0].sem_flg = SEM_UNDO;
		
		if (semop(sem_ctrlid, sops, 1) == -1)
		{
			return LA_FLG_BINDTO | LA_FLG_BINDFROM;
		}
		
		// write this string to the shm segment
		s = shm;
		for (c = map->l_name; *c != '\0'; c++)
		{
			*s++ = *c;
		}
		*s = '\0';
	
		// give 1 resource on our sema
		sops[0].sem_num = AUDIT_SEM;	// operate on our sema
		sops[0].sem_op = 1;				// give 1 resource
		sops[0].sem_flg = SEM_UNDO;
		
		if (semop(sem_ctrlid, sops, 1) == -1)
		{
			return LA_FLG_BINDTO | LA_FLG_BINDFROM;
		}
	}

	return LA_FLG_BINDTO | LA_FLG_BINDFROM;
}

