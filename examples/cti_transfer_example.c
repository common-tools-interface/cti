/*********************************************************************************\
 * cti_transfer_example.c - An example program which takes advantage of the Cray
 *          tools interface which will launch an application session from the
 *          given argv and transfer test files.
 *
 * Copyright 2011-2016 Cray Inc.  All Rights Reserved.
 *
 *********************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "cray_tools_fe.h"

void
usage(char *name)
{
    fprintf(stdout, "USAGE: %s [LAUNCHER STRING]\n", name);
    fprintf(stdout, "Launch an application using the Cray Tools Interface\n");
    fprintf(stdout, "and transfer a test file to the compute node.\n");
    return;
}

int
main(int argc, char **argv)
{
    cti_app_id_t        myapp;
    cti_session_id_t    mysid;
    cti_manifest_id_t   mymid;
    char *              file_loc;
    cti_wlm_type        mywlm;
    int                 r;

    if (argc < 2)
    {
        usage(argv[0]);
        return 1;
    }

    /*
     * cti_launchAppBarrier - Start an application using the application launcher
     *                        with the provided argv array and have the launcher
     *                        hold the application at its startup barrier for
     *                        MPI/SHMEM/UPC/CAF applications.
     */
    myapp = cti_launchAppBarrier((const char * const *)&argv[1],-1,-1,NULL,NULL,NULL);
    if (myapp == 0)
    {
        fprintf(stderr, "Error: cti_launchAppBarrier failed!\n");
        fprintf(stderr, "CTI error: %s\n", cti_error_str());
        return 1;
    }

    // Ensure app is valid
    if(cti_appIsValid(myapp) == 0){
        fprintf(stderr, "Error: app is invalid!\n");
        return 1;
    }

    // Create a new session based on the app_id
    mysid = cti_createSession(myapp);
    if (mysid == 0)
    {
        fprintf(stderr, "Error: cti_createSession failed!\n");
        fprintf(stderr, "CTI error: %s\n", cti_error_str());
        return 1;
    }

    // Ensure session is valid
    if(cti_sessionIsValid(mysid) == 0){
        fprintf(stderr, "Error: session is invalid!\n");
        return 1;
    }

    // Create a manifest based on the session
    mymid = cti_createManifest(mysid);
    if (mymid == 0)
    {
        fprintf(stderr, "Error: cti_createManifest failed!\n");
        fprintf(stderr, "CTI error: %s\n", cti_error_str());
        return 1;
    }

    // Ensure manifest is valid
    if(cti_manifestIsValid(mymid) == 0){
        fprintf(stderr, "Error: manifest is invalid!\n");
        return 1;
    }

    // Add the file to the manifest
    r = cti_addManifestFile(mymid, "testing.info");
    if (r)
    {
        fprintf(stderr, "Error: cti_addManifestFile failed!\n");
        fprintf(stderr, "CTI error: %s\n", cti_error_str());
        return 1;
    }

    // Send the manifest to the compute node
    r = cti_sendManifest(mymid);
    if (r)
    {
        fprintf(stderr, "Error: cti_sendManifest failed!\n");
        fprintf(stderr, "CTI error: %s\n", cti_error_str());
        return 1;
    }

    // Get the location of the directory where the file now resides on the
    // compute node
    file_loc = cti_getSessionFileDir(mysid);
    if (file_loc == NULL)
    {
        fprintf(stderr, "Error: cti_getSessionFileDir failed!\n");
        fprintf(stderr, "CTI error: %s\n", cti_error_str());
        return 1;
    }

    printf("Sent testing.info to the directory %s on the compute node(s).\n", file_loc);

    // Get the current WLM in use so that we can verify based on that
    mywlm = cti_current_wlm();
    if(mywlm == CTI_WLM_NONE){
        fprintf(stderr, "Error: Could not succesfully detect workload manager!\n");
        return 1;
    }

    // Conduct WLM specific calls
    switch (mywlm)
    {
        case CTI_WLM_CRAY_SLURM:
        {
            cti_cray_slurm_ops_t * slurm_ops;
            cti_wlm_type ret = cti_open_ops(&slurm_ops);
            if (ret != mywlm)
            {
                fprintf(stderr, "Error: cti_open_ops returned mismatched wlm!\n");
                return 1;
            } else if (slurm_ops == NULL)
            {
                fprintf(stderr, "Error: cti_open_ops did not return any ops!\n");
                return 1;
            }
            /*
             * getSrunInfo - Obtain information about the srun process
             */
             cti_srunProc_t *mysruninfo = slurm_ops->getSrunInfo(myapp);
             if (mysruninfo == NULL)
             {
                fprintf(stderr, "Error: getSrunInfo failed!\n");
                fprintf(stderr, "CTI error: %s\n", cti_error_str());
                return 1;
             } else
             {
                printf("\nVerify by issuing the following commands in another terminal:\n\n");
                printf("srun --jobid=%lu --gres=none --mem-per-cpu=0 ls %s\n", (long unsigned int)mysruninfo->jobid, file_loc);
                free(mysruninfo);
             }
        }
            break;

        case CTI_WLM_SSH:
            break;

        default:
            // do nothing
            printf("Unsupported wlm!\n");
            break;
    }

    free(file_loc);

    printf("\nHit return to release the application from the startup barrier...");

    // just read a single character from stdin then release the app/exit
    (void)getchar();

    r = cti_releaseAppBarrier(myapp);
    if (r)
    {
        fprintf(stderr, "Error: cti_releaseAppBarrier failed!\n");
        fprintf(stderr, "CTI error: %s\n", cti_error_str());
        return 1;
    }

    cti_deregisterApp(myapp);

    return 0;
}
