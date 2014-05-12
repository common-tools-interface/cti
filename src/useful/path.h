/*********************************************************************************\
 * path.h - Header file for the path interface.
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

#ifndef _PATH_H
#define _PATH_H

char *	_cti_pathFind(const char *, const char *);
char *	_cti_libFind(const char *);
int		_cti_adjustPaths(char *);
char *	_cti_pathToName(char *);

#endif /* _PATH_H */
