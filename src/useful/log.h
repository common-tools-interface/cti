/*********************************************************************************\
 * log.h - Header file for the log interface.
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

#ifndef _LOG_H
#define _LOG_H

FILE *	_cti_create_log(int, char *);
int 	_cti_write_log(FILE *, char *);
int 	_cti_close_log(FILE *);
int 	_cti_hook_stdoe(FILE *);

#endif /* _LOG_H */
