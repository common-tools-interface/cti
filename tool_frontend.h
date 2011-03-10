/*********************************************************************************\
 * tool_frontend.h - The public API definitions for the frontend portion of the
 *                   tool_interface.
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

#ifndef _TOOL_FRONTEND_H
#define _TOOL_FRONTEND_H

#include <stdint.h>
#include <sys/types.h>

#define LIBAUDIT_ENV_VAR        "LD_VAL_LIBRARY"
#define DBG_LOG_ENV_VAR         "DBG_LOG_DIR"

/*
 * alps_application commands
 */
extern int              registerAprunPid(pid_t);
extern int              deregisterAprunPid(pid_t);
extern uint64_t         getApid(pid_t);
extern char *           getCName(void);
extern int              getNid(void);
extern int              getNumAppPEs(pid_t);
extern int              getNumAppNodes(pid_t);
extern char **          getAppHostsList(pid_t aprunPid);

/*
 * alps_run commands
 */
extern pid_t            launchAprun_barrier(char **, int, int, int);
extern int              releaseAprun_barrier(pid_t);
extern int              killAprun(pid_t, int);

/*
 * alps_transfer commands
 */
extern int              sendCNodeExec(pid_t, char *, char **, char **);
extern int              sendCNodeBinary(pid_t, char *);
extern int              sendCNodeLibrary(pid_t, char *);
extern int              sendCNodeFile(pid_t, char *);

#endif /* _TOOL_FRONTEND_H */
