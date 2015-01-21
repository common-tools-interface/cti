/******************************************************************************\
 * cti_log.h - Header file for the log interface.
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
 ******************************************************************************/

#ifndef _CTI_LOG_H
#define _CTI_LOG_H

FILE *	_cti_create_log(int, char *);
int 	_cti_write_log(FILE *, char *);
int 	_cti_close_log(FILE *);
int 	_cti_hook_stdoe(FILE *);

#endif /* _CTI_LOG_H */
