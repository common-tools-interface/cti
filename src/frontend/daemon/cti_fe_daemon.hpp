/******************************************************************************\
 * cti_overwatch.hpp - command interface for overwatch daemon
 *
 * Copyright 2019 Cray Inc.  All Rights Reserved.
 *
 * Unpublished Proprietary Information.
 * This unpublished work is protected to trade secret, copyright and other laws.
 * Except as permitted by contract or express written permission of Cray Inc.,
 * no part of this work or its content may be used, reproduced or disclosed
 * in any form.
 *
 ******************************************************************************/
#pragma once

namespace cti {
namespace fe_daemon {

using MPIRId = int;

// request types

enum ReqType : long {
	ForkExecvpApp,
	ForkExecvpUtil,

	LaunchMPIR,
	AttachMPIR,
	ReleaseMPIR,

	RegisterApp,
	RegisterUtil,
	DeregisterApp,

	Shutdown
};

enum RunMode : int {
	Asynchronous,
	Synchronous
};

/* communication protocol
	before sending any data, send the request type
*/

// ForkExecvpApp
// LaunchMPIR
/*
	Application launch parameters:
	send socket control message to share FD access rights
	- array of stdin, stdout, stderr FDs
	then send list of null-terminated strings:
	- file path string
	- each argument string
	- EMPTY STRING
	- each environment variable string (format VAR=VAL)
	- EMPTY STRING
*/

// ForkExecvpUtil
/*
	send PID of owning application
	send RunMode indicating synchronous or asynchronous run
	send "Application launch parameters" as for ForkExecvpApp
*/

// AttachMPIR
// RegisterApp
// DeregisterApp
/*
	send PID of target application
*/

// RegisterUtil
/*
	send PID of owning application
	send PID of target utility
*/

// ReleaseMPIR
/*
	send MPIR ID provided by LaunchMPIR request
*/

// Shutdown
/* No data */

// Response types

enum RespType : long {
	// Shutdown, RegisterApp, RegisterUtil, ReleaseMPIR
	OK,

	// ForkExecvpApp, ForkExecvpUtil
	PID,

	// LaunchMPIR
	MPIR,
};

struct OKResp
{
	RespType type;
	bool success;
};

struct PIDResp
{
	RespType type;
	pid_t pid;
};

struct MPIRResp
{
	RespType type;
	MPIRId mpir_id;
	pid_t launcher_pid;
	uint32_t job_id;
	uint32_t step_id;
	int num_pids;
	// after sending this struct, send `num_pids` elements of:
	// - pid followed by null-terminated hostname
};

}; // fe_daemon
}; // cti
