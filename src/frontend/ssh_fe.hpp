/******************************************************************************\
 * ssh_fe.h - A header file for the fallback (SSH based) workload manager
 *
 * Copyright 2017 Cray Inc.	All Rights Reserved.
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

class SSHFrontend : public Frontend {

public: // interface
	AppId registerJob(pid_t launcher_pid);
};
