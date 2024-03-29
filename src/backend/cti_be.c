/*********************************************************************************\
 * cti_be.c - A interface to interact with placement information on compute
 *        nodes. This provides the tool developer with an easy to use interface
 *        to obtain application information for backend tool daemons running on
 *        the compute nodes.
 *
 * Copyright 2011-2023 Hewlett Packard Enterprise Development LP.
 * SPDX-License-Identifier: Linux-OpenIB
 ******************************************************************************/

// This pulls in config.h
#include "cti_defs.h"

#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "cti_be.h"

/* wlm specific proto objects defined elsewhere */
extern cti_be_wlm_proto_t   _cti_be_slurm_wlmProto;
extern cti_be_wlm_proto_t   _cti_be_generic_ssh_wlmProto;
extern cti_be_wlm_proto_t   _cti_be_localhost_wlmProto;

#ifdef HAVE_PALS
extern cti_be_wlm_proto_t   _cti_be_pals_wlmProto;
#endif

#ifdef HAVE_ALPS
extern cti_be_wlm_proto_t   _cti_be_alps_wlmProto;
#endif

#ifdef HAVE_FLUX
extern cti_be_wlm_proto_t   _cti_be_flux_wlmProto;
#endif

// Global vars
/* noneness wlm proto object */
static cti_be_wlm_proto_t   _cti_be_nonenessProto =
{
    CTI_WLM_NONE,                       // wlm_type
    _cti_be_wlm_init_none,              // wlm_init
    _cti_be_wlm_fini_none,              // wlm_fini
    _cti_be_wlm_findAppPids_none,       // wlm_findAppPids
    _cti_be_wlm_getNodeHostname_none,   // wlm_getNodeHostname
    _cti_be_wlm_getNodeFirstPE_none,    // wlm_getNodeFirstPE
    _cti_be_wlm_getNodePEs_none         // wlm_getNodePEs
};

/* global wlm proto object - this is initialized to noneness by default */
static cti_be_wlm_proto_t * _cti_be_wlmProto    = &_cti_be_nonenessProto;
// init/fini guard - both the constructor and destructor gets called twice sometimes
static bool                 _cti_be_isInit      = false;
static bool                 _cti_be_isFini      = false;

// Constructor function
void __attribute__((constructor))
_cti_be_init(void)
{
    char *  wlm_str;

    // Ensure we have not already called init
    if (_cti_be_isInit)
        return;

    // We do not want to call init if we are running on the frontend
    if (getenv(BE_GUARD_ENV_VAR) == NULL)
        return;

    // get the wlm string from the environment
    if ((wlm_str = getenv(WLM_ENV_VAR)) == NULL)
    {
        fprintf(stderr, "Env var %s not set!\n", WLM_ENV_VAR);
        return;
    }

    // verify that the wlm string is valid
    switch (atoi(wlm_str))
    {
#ifdef HAVE_ALPS
        case CTI_WLM_ALPS:
            _cti_be_wlmProto = &_cti_be_alps_wlmProto;
            break;
#endif

#ifdef HAVE_PALS
        case CTI_WLM_PALS:
            _cti_be_wlmProto = &_cti_be_pals_wlmProto;
            break;
#endif

        case CTI_WLM_SLURM:
            _cti_be_wlmProto = &_cti_be_slurm_wlmProto;
            break;

        case CTI_WLM_SSH:
            _cti_be_wlmProto = &_cti_be_generic_ssh_wlmProto;
            break;

#ifdef HAVE_FLUX
        case CTI_WLM_FLUX:
            _cti_be_wlmProto = &_cti_be_flux_wlmProto;
            break;
#endif

        case CTI_WLM_LOCALHOST:
            _cti_be_wlmProto = &_cti_be_localhost_wlmProto;
            break;

        case CTI_WLM_NONE:
        case CTI_WLM_MOCK:
            // These wlm are not supported
            fprintf(stderr, "wlm %s is not yet supported!\n", cti_be_wlm_type_toString(atoi(wlm_str)));
            return;

        default:
            fprintf(stderr, "Env var %s is invalid.\n", WLM_ENV_VAR);
            return;
    }

    if (_cti_be_wlmProto->wlm_init())
    {
        // We failed to init, so ensure we set the WLM proto to noneness
        _cti_be_wlmProto = &_cti_be_nonenessProto;
        return;
    }

    _cti_be_isInit = true;
}

// Destructor function
void __attribute__((destructor))
_cti_be_fini(void)
{
    // Ensure this is only called once
    if (_cti_be_isFini)
        return;

    // call the wlm finish function
    _cti_be_wlmProto->wlm_fini();

    // reset the wlm proto to noneness
    _cti_be_wlmProto = &_cti_be_nonenessProto;

    _cti_be_isFini = true;

    return;
}

const char *
cti_be_version(void)
{
    return CTI_BE_VERSION;
}

cti_wlm_type_t
cti_be_current_wlm(void)
{
    return _cti_be_wlmProto->wlm_type;
}

const char *
cti_be_wlm_type_toString(cti_wlm_type_t wlm_type)
{
    switch (wlm_type)
    {
        case CTI_WLM_ALPS:
            return CTI_WLM_TYPE_ALPS_STR;

        case CTI_WLM_SLURM:
            return CTI_WLM_TYPE_SLURM_STR;

        case CTI_WLM_SSH:
            return CTI_WLM_TYPE_SSH_STR;

        case CTI_WLM_PALS:
            return CTI_WLM_TYPE_PALS_STR;

        case CTI_WLM_FLUX:
            return CTI_WLM_TYPE_FLUX_STR;

        case CTI_WLM_LOCALHOST:
            return CTI_WLM_TYPE_LOCALHOST_STR;

        case CTI_WLM_NONE:
        case CTI_WLM_MOCK:
            return "No WLM detected";
    }

    // Shouldn't get here
    return "Invalid WLM.";
}

char *
cti_be_getAppId(void)
{
    char *  apid_str;

    // get the apid string from the environment
    if ((apid_str = getenv(APID_ENV_VAR)) == NULL)
    {
        return NULL;
    }

    return strdup(apid_str);
}

cti_pidList_t *
cti_be_findAppPids()
{
    // Call the appropriate function based on the wlm
    return _cti_be_wlmProto->wlm_findAppPids();
}

void
cti_be_destroyPidList(cti_pidList_t *lst)
{
    // sanity check
    if (lst == NULL)
        return;

    if (lst->pids != NULL)
        free(lst->pids);

    free(lst);
}

char *
cti_be_getNodeHostname()
{
    // Call the appropriate function based on the wlm
    return _cti_be_wlmProto->wlm_getNodeHostname();
}

int
cti_be_getNodeFirstPE()
{
    // Call the appropriate function based on the wlm
    return _cti_be_wlmProto->wlm_getNodeFirstPE();
}

int
cti_be_getNodePEs()
{
    // Call the appropriate function based on the wlm
    return _cti_be_wlmProto->wlm_getNodePEs();
}

// This should be hidden from everyone outside of internal library code
char *
_cti_be_getToolDir()
{
    char *  tool_str;

    // get the string from the environment
    if ((tool_str = getenv(TOOL_DIR_VAR)) == NULL)
    {
        return NULL;
    }

    return strdup(tool_str);
}

// This should be hidden from everyone outside of internal library code
char *
_cti_be_getAttribsDir()
{
    char *  attribs_str;

    // get the string from the environment
    if ((attribs_str = getenv(PMI_ATTRIBS_DIR_VAR)) == NULL)
    {
        return NULL;
    }

    return strdup(attribs_str);
}

char *
cti_be_getRootDir()
{
    char *  root_str;

    // get the string from the environment
    if ((root_str = getenv(ROOT_DIR_VAR)) == NULL)
    {
        return NULL;
    }

    return strdup(root_str);
}

char *
cti_be_getBinDir()
{
    char *  bin_str;

    // get the string from the environment
    if ((bin_str = getenv(BIN_DIR_VAR)) == NULL)
    {
        return NULL;
    }

    return strdup(bin_str);
}

char *
cti_be_getLibDir()
{
    char *  lib_str;

    // get the string from the environment
    if ((lib_str = getenv(LIB_DIR_VAR)) == NULL)
    {
        return NULL;
    }

    return strdup(lib_str);
}

char *
cti_be_getFileDir()
{
    char *  file_str;

    // get the string from the environment
    if ((file_str = getenv(FILE_DIR_VAR)) == NULL)
    {
        return NULL;
    }

    return strdup(file_str);
}

char *
cti_be_getTmpDir()
{
    char *  tmp_str;

    // get the string from the environment
    if ((tmp_str = getenv(SCRATCH_ENV_VAR)) == NULL)
    {
        return NULL;
    }

    return strdup(tmp_str);
}


/* Noneness functions for wlm proto */

int
_cti_be_wlm_init_none(void)
{
    fprintf(stderr, "\nwlm_init() not supported for %s\n", cti_be_wlm_type_toString(_cti_be_wlmProto->wlm_type));
    return 1;
}

void
_cti_be_wlm_fini_none(void)
{
    return;
}

cti_pidList_t *
_cti_be_wlm_findAppPids_none(void)
{
    fprintf(stderr, "\nwlm_findAppPids() not supported for %s\n", cti_be_wlm_type_toString(_cti_be_wlmProto->wlm_type));
    return NULL;
}

char *
_cti_be_wlm_getNodeHostname_none(void)
{
    fprintf(stderr, "\nwlm_getNodeHostname() not supported for %s\n", cti_be_wlm_type_toString(_cti_be_wlmProto->wlm_type));
    return NULL;
}

int
_cti_be_wlm_getNodeFirstPE_none(void)
{
    fprintf(stderr, "\nwlm_getNodeFirstPE() not supported for %s\n", cti_be_wlm_type_toString(_cti_be_wlmProto->wlm_type));
    return -1;
}

int
_cti_be_wlm_getNodePEs_none(void)
{
    fprintf(stderr, "\nwlm_getNodeFirstPE() not supported for %s\n", cti_be_wlm_type_toString(_cti_be_wlmProto->wlm_type));
    return -1;
}

