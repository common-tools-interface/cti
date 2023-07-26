/******************************************************************************\
 * cti_path.h - Header file for the path interface.
 *
 * Copyright 2011-2020 Hewlett Packard Enterprise Development LP.
 * SPDX-License-Identifier: Linux-OpenIB
 ******************************************************************************/

#ifndef _CTI_PATH_H
#define _CTI_PATH_H

#ifdef __cplusplus
extern "C" {
#endif

char *  _cti_pathFind(const char *, const char *);
char *  _cti_libFind(const char *);
int     _cti_adjustPaths(const char *, const char*);
int     _cti_removeDirectory(const char *);

#ifdef __cplusplus
}
#endif

#endif /* _CTI_PATH_H */
