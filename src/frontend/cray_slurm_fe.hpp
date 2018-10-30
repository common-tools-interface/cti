/******************************************************************************\
 * cray_slurm_fe.h - A header file for the Cray slurm specific frontend 
 *                   interface.
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
 ******************************************************************************/

#pragma once

#include <stdint.h>
#include <sys/types.h>

#include "frontend/Frontend.hpp"

class CraySLURMFrontend : public Frontend {

public: // types
	struct SrunInfo {
		uint32_t jobid;
		uint32_t stepid;
	};

public: // interface
	AppId registerJobStep(pid_t launcher_pid);
	SrunInfo getSrunInfo(cti_app_id_t appId);
};
