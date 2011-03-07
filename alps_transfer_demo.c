/*********************************************************************************\
 * alps_transfer_demo.c - An example program which takes advantage of the libtransfer.so
 *                        library which will launch an aprun session from the given
 *                        argv and transfer demo files.
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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "alps_run.h"
#include "alps_transfer.h"

void
usage(char *name)
{
        fprintf(stdout, "USAGE: %s [APRUN STRING]\n", name);
        fprintf(stdout, "Launch an aprun session using the alps_transfer interface\n");
        fprintf(stdout, "Written by andrewg@cray.com\n");
        return;
}

int
main(int argc, char **argv)
{
        pid_t   mypid;
        
        if (argc < 2)
        {
                usage(argv[0]);
                return 1;
        }
        
        if ((mypid = launchAprun_barrier(&argv[1])) <= 0)
        {
                fprintf(stderr, "Err: Could not launch aprun!\n");
                return 1;
        }
        
        if (releaseAprun_barrier(mypid))
        {
                fprintf(stderr, "Err: Failed to release app from barrier!\n");
                return 1;
        }
        
        if (sendCNodeFile(mypid, "./demos/testing.info"))
        {
                fprintf(stderr, "Err: Failed to send file to cnodes!\n");
                killAprun(mypid, 9);
                return 1;
        }
        
        sleep(300);
        
        /*
        if (killAprun(mypid, 9))
        {
                fprintf(stderr, "Err: Failed to kill app!\n");
                return 1;
        }*/
        
        fprintf(stdout, "Done.\n");
        
        return 0;
}
