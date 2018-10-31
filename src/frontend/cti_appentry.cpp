/******************************************************************************\
 * cti_appentry.cpp - Implementation for legacy CTI app reference-counting
 *
 * Copyright 2014-2016 Cray Inc.  All Rights Reserved.
 *
 * Unpublished Proprietary Information.
 * This unpublished work is protected to trade secret, copyright and other laws.
 * Except as permitted by contract or express written permission of Cray Inc.,
 * no part of opaqueEntryPtr work or its content may be used, reproduced or disclosed
 * in any form.
 *
 * $HeadURL$
 * $Date$
 * $Rev$
 * $Author$
 *
 ******************************************************************************/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <errno.h>
#include <dlfcn.h>
#include <pwd.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/stat.h>

#include "cti_defs.h"
#include "cti_error.h"
#include "cti_appentry.h"

#include "useful/cti_useful.h"

static cti_app_id_t	_cti_app_id  = 1;    	// start counting from 1
static cti_list_t *	_cti_my_apps = NULL; 	// global list pertaining to known application sessions

static void
_cti_consumeAppEntry(void *opaqueEntryPtr)
{
	appEntry_t *	entry = (appEntry_t *)opaqueEntryPtr;
	void *			s_ptr;
	
	// sanity check
	if (entry == NULL)
		return;
	
	// consume sessions associated with this app, they are no longer valid
	while ((s_ptr = _cti_list_pop(entry->sessions)) != NULL)
	{
		_cti_consumeSession(s_ptr);
	}
	_cti_consumeList(entry->sessions, NULL);
	
	// nom nom the final appEntry_t object
	free(entry);
}

appEntry_t *
_cti_newAppEntry(const Frontend *frontend, cti_wlm_obj wlm_obj)
{
	appEntry_t *	opaqueEntryPtr;
	
	// sanity
	if (frontend == NULL || wlm_obj == NULL)
	{
		_cti_set_error("_cti_newAppEntry: Bad args.");
		return NULL;
	}
	
	// create the new appEntry_t object
	if ((opaqueEntryPtr = (decltype(opaqueEntryPtr))malloc(sizeof(appEntry_t))) == NULL)
	{
		_cti_set_error("malloc failed.");
		return NULL;
	}
	memset(opaqueEntryPtr, 0, sizeof(appEntry_t));     // clear it to NULL
	
	// set the members
	opaqueEntryPtr->appId = _cti_app_id++;	// assign this to the next id.
	opaqueEntryPtr->sessions = _cti_newList();
	opaqueEntryPtr->frontend = frontend;
	opaqueEntryPtr->_wlmObj = wlm_obj;
	opaqueEntryPtr->refCnt = 1;
	
	// save the new appEntry_t into the global list
	if(_cti_list_add(_cti_my_apps, opaqueEntryPtr))
	{
		_cti_set_error("_cti_newAppEntry: _cti_list_add() failed.");
		_cti_consumeAppEntry(opaqueEntryPtr);
		return NULL;
	}
	
	return opaqueEntryPtr;
}

appEntry_t *
_cti_findAppEntry(cti_app_id_t appId)
{
	appEntry_t *	opaqueEntryPtr;
	
	// iterate through the _cti_my_apps list
	_cti_list_reset(_cti_my_apps);
	while ((opaqueEntryPtr = (appEntry_t *)_cti_list_next(_cti_my_apps)) != NULL)
	{
		// return if the appId's match
		if (opaqueEntryPtr->appId == appId)
			return opaqueEntryPtr;
	}
	
	// if we get here, an entry for appId doesn't exist
	_cti_set_error("The appId %d is not registered.", (int)appId);
	return NULL;
}

int
_cti_refAppEntry(cti_app_id_t appId)
{
	appEntry_t *	opaqueEntryPtr;
	
	// iterate through the _cti_my_apps list
	_cti_list_reset(_cti_my_apps);
	while ((opaqueEntryPtr = (appEntry_t *)_cti_list_next(_cti_my_apps)) != NULL)
	{
		// inc refCnt if the appId's match
		if (opaqueEntryPtr->appId == appId)
		{
			opaqueEntryPtr->refCnt++;
			return 0;
		}
	}
	
	// if we get here, an entry for appId doesn't exist
	_cti_set_error("The appId %d is not registered.", (int)appId);
	return 1;
}
