/*********************************************************************************\
 * alps_fe.h - A header file for the alps specific frontend interface.
 *
 * Copyright 2014 Cray Inc.	All Rights Reserved.
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

#pragma once

#include <stdint.h>
#include <sys/types.h>

#include "frontend/Frontend.hpp"

class ALPSFrontend : public Frontend {

public: // types
	struct AprunInfo {
		uint64_t apid;
		pid_t aprunPid;
	};

public: // interface
	AppId registerApid(uint64_t apid);
	uint64_t getApid(pid_t appPid);
	AprunInfo getAprunInfo(AppId appId);
	int getAlpsOverlapOrdinal(AppId appId);
};
