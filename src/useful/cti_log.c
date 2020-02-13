/******************************************************************************\
 * cti_log.c - Functions used to create log files.
 *
 * Copyright 2011-2019 Cray Inc. All Rights Reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 ******************************************************************************/

// This pulls in config.h
#include "cti_defs.h"

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#include <linux/limits.h>

#include "cti_log.h"

cti_log_t*
_cti_create_log(char const* directory, char const* filename, int suffix)
{
    char logfile[PATH_MAX];
    FILE *fp;

    // sanity checks
    if (filename == (char *)NULL)
        return (cti_log_t*)NULL;

    // determine where to create the log
    if (directory == (char *)NULL) {
        // Write to /tmp as default
        directory = "/tmp";
    }

    // create the fullpath string to the log file using PATH_MAX plus a null term
    snprintf(logfile, PATH_MAX+1, "%s/dbglog_%s.%d.log", directory, filename, suffix);

    if ((fp = fopen(logfile, "a")) == (FILE *)NULL)
    {
        // could not open log file for writing at the specififed location
        return (cti_log_t*)NULL;
    }

    // set the log to be unbuffered - don't return error if this fails
    setvbuf(fp, NULL, _IONBF, 0);

    return fp;
}

int
_cti_close_log(cti_log_t* log_file)
{
    FILE* fp = (FILE*)log_file;
    if (fp != NULL) {
        fclose(fp);
    }

    return 0;
}

int
_cti_write_log(cti_log_t* log_file, const char *fmt, ...)
{
    FILE* fp = (FILE*)log_file;
    if (fp != NULL) {
        va_list vargs;

        struct timeval tv;
        int rc = -1;
        // try to get the time of day 10 times, otherwise, we'll just print zeros below.
        for (int count = 0; rc == -1 && count < 10; ++count) {
            rc = gettimeofday(&tv, NULL);
        }

        if (rc == -1) {
            fprintf(fp, "0000-00-00 00:00:00.%06ld: ", 0ul, 0ul);
        } else {
            if ((struct tm *tmptr = localtime(tv.tv_sec)) == NULL) {
                fprintf(fp, "%ld.%06ld: ", tv.tv_sec, tv.tv_usec);
            } else {
                fprintf(fp, "%Y-%m-%d %H:%M:%S.%06ld: ", tmptr->tm_year,
                        tmptr->tm_mon, tmptr->tm_mday, tmptr->tm_hour,
                        tmptr->tm_min, tmptr->tm_sec, tv.tv_usec);
            }   
        }

        va_start(vargs, fmt);
        vfprintf(fp, fmt, vargs);
        va_end(vargs);
    }

    return 0;
}

int
_cti_hook_stdoe(cti_log_t* log_file)
{
    FILE* fp = (FILE*)log_file;
    if (fp != NULL) {
        if (dup2(fileno(fp), STDOUT_FILENO) < 0) {
            return 1;
        }
        if (dup2(fileno(fp), STDERR_FILENO) < 0) {
            return 1;
        }
    }

    return 0;
}
