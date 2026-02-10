/*********************************************************************************\
 * cti_pmix_util.c - PMIx tool query utility
 *
 * Copyright 2025 Hewlett Packard Enterprise Development LP.
 * SPDX-License-Identifier: Linux-OpenIB
 *********************************************************************************/

#include <stdlib.h>
#include <stdio.h>

#include <pmix_tool.h>

int main(int argc, char** argv)
{
    char const* tool_file_path = NULL;
    char const* key = NULL;

    pmix_status_t rc;
    pmix_proc_t myproc;
    pmix_proc_t proc;
    pmix_info_t info;
    pmix_value_t *val = NULL;

    if (argc != 3) {
        fprintf(stderr, "usage: %s tool_file key\n", argv[0]);
        exit(1);
    }

    tool_file_path = argv[1];
    key = argv[2];

    rc = PMIx_Info_load(&info, PMIX_TOOL_ATTACHMENT_FILE, tool_file_path, PMIX_STRING);
    if (rc != PMIX_SUCCESS) {
        fprintf(stderr, "PMIx_Info_load failed: %d\n", rc);
        exit(1);
    }

    rc = PMIx_tool_init(&myproc, &info, 1);
    if (rc != PMIX_SUCCESS) {
        fprintf(stderr, "PMIx_tool_init failed: %d\n", rc);
        exit(1);
    }

    PMIX_PROC_LOAD(&proc, myproc.nspace, PMIX_RANK_WILDCARD);

    rc = PMIx_Get(&proc, key, NULL, 0, &val);
    if (rc != PMIX_SUCCESS) {
        fprintf(stderr, "PMIx_Get %s failed: %d\n", key, rc);
        exit(1);
    }

    switch (val->type) {

        case PMIX_UINT32:
            fprintf(stdout, "%zu\n", val->data.uint32);
            break;

        case PMIX_STRING:
            fprintf(stdout, "%s\n", val->data.string);
            break;

        default:
            fprintf(stderr, "Unsupported PMIx type: %hu\n", val->type);
            PMIx_tool_finalize();
            return -1;
    }

    PMIx_tool_finalize();

    return 0;
}
