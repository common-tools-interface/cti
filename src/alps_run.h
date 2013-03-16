/*********************************************************************************\
 * alps_run.h - A header file for the alps_run interface.
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

#ifndef _ALPS_RUN_H
#define _ALPS_RUN_H

#include <stdint.h>

#define APRUN			"aprun"
#define APKILL			"apkill"
#define DEFAULT_SIG	9

#define OLD_APRUN_LOCATION	"/usr/bin/aprun"
#define OBS_APRUN_LOCATION	"/opt/cray/alps/default/bin/aprun"
// defined in tool_frontend.h, used to define absolute location of the real
// aprun binary
#define USER_DEF_APRUN_LOCATION "CRAY_APRUN_PATH"

/* struct typedefs */
typedef struct
{
	int pipe_r;
	int pipe_w;
	int sync_int;
} barrierCtl_t;

struct aprunInv
{
	uint64_t			apid;
	pid_t				aprunPid;
	barrierCtl_t		pipeCtl;
	struct aprunInv *	next;
};
typedef struct aprunInv aprunInv_t;

typedef struct
{
	uint64_t	apid;
	pid_t		aprunPid;
} aprunProc_t;

/* function prototypes */
aprunProc_t	*	launchAprun_barrier(char **, int, int, int, int, char *, char *, char **);
int				releaseAprun_barrier(uint64_t);
int				killAprun(uint64_t, int);
void			reapAprunInv(uint64_t);

#endif /* _ALPS_RUN_H */
