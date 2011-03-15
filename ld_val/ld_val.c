/*********************************************************************************\
 * ld_val.c - A library that takes advantage of the rtld audit interface library
 *            to recieve the locations of the shared libraries that are required
 *            by the runtime dynamic linker for a specified program. This is the
 *            static portion of the code to link into a program wishing to use
 *            this interface.
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

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include "ld_val_defs.h"
#include "ld_val.h"

/* Internal prototypes */
static int creat_shm_segs(void);
static int attach_shm_segs(void);
static int destroy_shm_segs(void);
static int save_str(char *);
static char ** make_rtn_array(void);

/* global variables */
key_t key_a;
key_t key_b;
int shmid;
int shm_ctlid;
char *shm;
char *shm_ctl;

int num_ptrs;
int num_alloc;
char **tmp_array = (char **)NULL;

int
creat_shm_segs()
{
        /*
        *  Create the shm segments - Note that these will behave as a semaphore in the
        *  event that multiple programs are trying to access this interface at once.
        *
        *  There is no way to avoid a deadlock if the key is not removed by the
        *  first caller.
        */
        
        // start out by creating the keys from a well known file location and a character id
        if ((key_a = ftok(KEYFILE, ID_A)) == (key_t)-1)
        {
                perror("IPC error: ftok");
                return 1;
        }
        if ((key_b = ftok(KEYFILE, ID_B)) == (key_t)-1)
        {
                perror("IPC error: ftok");
                return 1;
        }
        
        while ((shmid = shmget(key_a, PATH_MAX, IPC_CREAT | IPC_EXCL | 0666)) < 0) 
        {
                if (errno == EEXIST)
                        continue;
                
                perror("IPC error: shmget");
                return 1;
        }
        
        while ((shm_ctlid = shmget(key_b, CTL_CHANNEL_SIZE, IPC_CREAT | IPC_EXCL | 0666)) < 0)
        {
                if (errno == EEXIST)
                        continue;
                        
                perror("IPC error: shmget");
                return 1;
        }
        
        return 0;
}

int
attach_shm_segs()
{
        /*
        *  Attach the shm segments to our data space.
        */
        if ((shm = shmat(shmid, NULL, 0)) == (char *)-1) 
        {
                perror("IPC error: shmat");
                return 1;
        }
        if ((shm_ctl = shmat(shm_ctlid, NULL, 0)) == (char *)-1)
        {
                perror("IPC error: shmat");
                return 1;
        }
        
        return 0;
}

int
destroy_shm_segs()
{
        int ret = 0;
        
        if (shmctl(shmid, IPC_RMID, NULL) < 0)
        {
                perror("IPC error: shmctl");
                ret = 1;
        }
        
        if (shmctl(shm_ctlid, IPC_RMID, NULL) < 0)
        {
                perror("IPC error: shmctl");
                ret = 1;
        }
        
        return ret;
}

int
save_str(char *str)
{
        if (str == (char *)NULL)
                return -1;
        
        if (num_ptrs >= num_alloc)
        {
                num_alloc += BLOCK_SIZE;
                if ((tmp_array = realloc((void *)tmp_array, num_alloc * sizeof(char *))) == (char **)NULL)
                {
                        perror("realloc");
                        return -1;
                }
        }
        
        tmp_array[num_ptrs++] = str;
        
        return num_ptrs;
}

char **
make_rtn_array()
{
        char **rtn;
        int i;
        
        if (tmp_array == (char **)NULL)
                return (char **)NULL;
        
        // create the return array
        if ((rtn = calloc(num_ptrs+1, sizeof(char *))) == (char **)NULL)
        {
                perror("calloc");
                return (char **)NULL;
        }
        
        // assign each element of the return array
        for (i=0; i<num_ptrs; i++)
        {
                rtn[i] = tmp_array[i];
        }
        
        // set the final element to null
        rtn[i] = (char *)NULL;
        
        // free the temp array
        free(tmp_array);
        
        return rtn;
}

char *
ld_verify(char *executable)
{
        const char *linker = NULL;
        int pid, status, fc, i=1;
        
        if (executable == (char *)NULL)
                return (char *)NULL;

        // Verify that the linker is able to perform relocations on our binary
        // This should be able to handle both 32 and 64 bit executables
        // We will simply choose the first one that works for us.
        for (linker = linkers[0]; linker != NULL; linker = linkers[i++])
        {
                pid = fork();
                
                // error case
                if (pid < 0)
                {
                        perror("fork");
                        return (char *)NULL;
                }
                
                // child case
                if (pid == 0)
                {
                        // redirect our stdout/stderr to /dev/null
                        fc = open("/dev/null", O_WRONLY);
                        dup2(fc, STDERR_FILENO);
                        dup2(fc, STDOUT_FILENO);
                        
                        // exec the linker to verify it is able to load our program
                        execl(linker, linker, "--verify", executable, (char *)NULL);
                        perror("execl");
                }
                
                // parent case
                // wait for child to return
                waitpid(pid, &status, 0);
                if (WIFEXITED(status))
                {
                        // if we recieved an exit status of 0, the verify was successful
                        if (WEXITSTATUS(status) == 0)
                                break;
                }
        }
        
        return (char *)linker;
}

int
ld_load(char *linker, char *executable, char *lib)
{
        int pid, fc;
        
        if (linker == (char *)NULL || executable == (char *)NULL)
                return -1;
        
        // invoke the rtld interface.
        pid = fork();
        
        // error case
        if (pid < 0)
        {
                perror("fork");
                return pid;
        }
        
        // child case
        if (pid == 0)
        {
                // set the LD_AUDIT environment variable for this process
                if (setenv(LD_AUDIT, lib, 1) < 0)
                {
                        perror("setenv");
                        fprintf(stderr, "Failed to set LD_AUDIT environment variable.\n");
                        exit(1);
                }
                
                // redirect our stdout/stderr to /dev/null
                fc = open("/dev/null", O_WRONLY);
                dup2(fc, STDERR_FILENO);
                dup2(fc, STDOUT_FILENO);
                
                // exec the linker with --list to get a list of our dso's
                execl(linker, linker, "--list", executable, (char *)NULL);
                perror("execl");
        }
        
        // parent case
        return pid;
}

char *
ld_get_lib(int pid)
{
        char *libstr;
        
        if (pid <= 0)
                return (char *)NULL;
        
        // wait for the child to signal us on the shm_ctl channel
        // as long as the child is alive
        while ((*shm_ctl != '1') && !waitpid(pid, NULL, WNOHANG));
        
        // only read if signaled
        if (*shm_ctl == '1')
        {
                // copy the library location string
                libstr = strdup(shm);
                
                // reset the shm segment
                memset(shm, '\0', PATH_MAX);
                
                // reset the control channel
                *shm_ctl = '0';
                
                // return the string
                return libstr;
        }
        
        // if we get here, we are done so return null
        return (char *)NULL;
}

char **
ld_val(char *executable)
{
        char *linker;
        int pid;
        int rec = 0;
        char *tmp_audit;
        char *audit_location;
        char *libstr;
        char **rtn;
        
        // reset global vars
        key_a = 0;
        key_b = 0;
        shmid = 0;
        shm_ctlid = 0;
        shm = NULL;
        shm_ctl = NULL;
        num_ptrs = 0;
        
        if (executable == (char *)NULL)
                return (char **)NULL;
        
        // ensure that we found a valid linker that was verified
        if ((linker = ld_verify(executable)) == (char *)NULL)
        {
                fprintf(stderr, "FATAL: Failed to locate a working dynamic linker for the specified binary.\n");
                return (char **)NULL;
        }
        
        // We now have a valid linker to use, so lets set up our shm segments
        
        // Create our shm segments
        // This will spin if another caller is using this interface
        if (creat_shm_segs())
        {
                fprintf(stderr, "Failed to create shm segments.\n");
                return (char **)NULL;
        }

        // Attach the segments to our data space.
        if (attach_shm_segs())
        {
                fprintf(stderr, "Failed to attach shm segments.\n");
                destroy_shm_segs();
                return (char **)NULL;
        }
        
        // create space for the tmp_array
        if ((tmp_array = calloc(BLOCK_SIZE, sizeof(char *))) == (void *)0)
        {
                perror("calloc");
                destroy_shm_segs();
                return (char **)NULL;
        }
        num_alloc = BLOCK_SIZE;
        
        // get the location of the audit library
        if ((tmp_audit = getenv(LIBAUDIT_ENV)) != (char *)NULL)
        {
                audit_location = strdup(tmp_audit);
        } else
        {
                fprintf(stderr, "Could not read LD_VAL_LIBRARY to get location of libaudit.so.\n");
                destroy_shm_segs();
                return (char **)NULL;
        }
        
        // Now we load our program using the list command to get its dso's
        if ((pid = ld_load(linker, executable, audit_location)) <= 0)
        {
                fprintf(stderr, "Failed to load the program using the linker.\n");
                destroy_shm_segs();
                return (char **)NULL;
        }
        
        // Read from the shm segment while the child process is still alive
        do {
                libstr = ld_get_lib(pid);
                
                // we want to ignore the first library we recieve
                // as it will always be the ld.so we are using to
                // get the shared libraries.
                if (++rec == 1)
                        continue;
                
                // if we recieved a null, we might be done.
                if (libstr == (char *)NULL)
                        continue;
                        
                if ((save_str(libstr)) <= 0)
                {
                        fprintf(stderr, "Unable to save temp string.\n");
                        destroy_shm_segs();
                        return (char **)NULL;
                }
        } while (!waitpid(pid, NULL, WNOHANG));
        
        rtn = make_rtn_array();
        
        // destroy the shm segments
        destroy_shm_segs();
        
        // cleanup memory
        free((void *)audit_location);

        return rtn;
}

