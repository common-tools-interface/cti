/*********************************************************************************\
 * alps_run.h - A header file for the alps_run interface.
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

#ifndef _ALPS_RUN_H
#define _ALPS_RUN_H

#define APRUN           "aprun"
#define APKILL          "apkill"
#define DEFAULT_SIG     9

/* struct typedefs */
typedef struct
{
        int pipe_r;
        int pipe_w;
        int sync_int;
} barrierCtl_t;

struct aprunInv
{
        pid_t                   aprunPid;
        barrierCtl_t            pipeCtl;
        struct aprunInv *      next;
};
typedef struct aprunInv aprunInv_t;

/* function prototypes */
pid_t   launchAprun_barrier(char **, int, int, int);
int     releaseAprun_barrier(pid_t);
int     killAprun(pid_t, int);

#endif /* _ALPS_RUN_H */
