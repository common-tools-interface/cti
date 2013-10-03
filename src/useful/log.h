/*********************************************************************************\
 * log.h - Header file for the log interface.
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

#ifndef _LOG_H
#define _LOG_H

FILE *create_log(int, char *);
int write_log(FILE *, char *);
int close_log(FILE *);
int hook_stdoe(FILE *);

#endif /* _LOG_H */
