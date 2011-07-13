/*********************************************************************************\
 * audit.c - A custom rtld audit interface to deliver locations of loaded dso's
 *           over a shared memory segment. This is the dynamic portion of the
 *           code which is used by the static library interface.
 *
 * © 2011 Cray Inc.  All Rights Reserved.
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
#include        <config.h>
#endif /* HAVE_CONFIG_H */

#include <limits.h>
#include <link.h>
#include <stdio.h>
#include <string.h>

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>

#include "ld_val_defs.h"

static key_t key_a = 0;
static key_t key_b = 0;
static int shmid = 0;
static int shm_ctlid = 0;
static char *shm = NULL;
static char *shm_ctl = NULL;

// This is always the first thing called
unsigned int
la_version(unsigned int version)
{
        // Lets attach to our shm segments
        if (shm == NULL)
        {
                // create the shm key
                if ((key_a = ftok(KEYFILE, ID_A)) == (key_t)-1)
                {
                        return LA_FLG_BINDTO | LA_FLG_BINDFROM;
                }
                
                // locate the segment
                if ((shmid = shmget(key_a, PATH_MAX, 0666)) < 0) 
                {
			shmid = 0;
                        return LA_FLG_BINDTO | LA_FLG_BINDFROM;
                }
                
                // now attach the segment to our data space
                if ((shm = shmat(shmid, NULL, 0)) == (char *) -1) 
                {
                        shm = NULL;
                        return LA_FLG_BINDTO | LA_FLG_BINDFROM;
                }
        }

        if (shm_ctl == NULL)
        {
                // create the shm key
                if ((key_b = ftok(KEYFILE, ID_B)) == (key_t)-1)
                {
                        return LA_FLG_BINDTO | LA_FLG_BINDFROM;
                }
                
                // locate the segment
                if ((shm_ctlid = shmget(key_b, CTL_CHANNEL_SIZE, 0666)) < 0) 
                {
			shm_ctlid = 0;
                        return LA_FLG_BINDTO | LA_FLG_BINDFROM;
                }
                
                // now attach the segment to our data space
                if ((shm_ctl = shmat(shm_ctlid, NULL, 0)) == (char *) -1) 
                {
                        shm_ctl = NULL;
                        return LA_FLG_BINDTO | LA_FLG_BINDFROM;
                }
        }        

        return version;
}

unsigned int
la_objopen(struct link_map *map, Lmid_t lmid, uintptr_t *cookie)
{
        char *s, *c;

	// return if opening of the shm segments failed
	if ((shmid == 0) || (shm_ctlid == 0))
		return LA_FLG_BINDTO | LA_FLG_BINDFROM;

        if (strlen(map->l_name) != 0)
        {
                // write this string to the shm segment
                s = shm;
                for (c = map->l_name; *c != '\0'; c++)
                        *s++ = *c;
                *s = '\0';
        
                // signal control channel and spin wait for reset condition
                *shm_ctl = '1';
		// fyi: If reader dies, we deadlock here.
                while (*shm_ctl != '0');
        }

        return LA_FLG_BINDTO | LA_FLG_BINDFROM;
}

