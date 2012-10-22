/*********************************************************************************\
 * alps_transfer.h - A header file for the alps_transfer interface.
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

#ifndef _ALPS_TRANSFER_H
#define _ALPS_TRANSFER_H

#define ALPS_LAUNCHER   "dlaunch"

/* function prototypes */
int             sendCNodeExec(uint64_t, char *, char **, char **, int);
int             sendCNodeBinary(uint64_t, char *);
int             sendCNodeLibrary(uint64_t, char *);
int             sendCNodeFile(uint64_t, char *);

#endif /* _ALPS_TRANSFER_H */
