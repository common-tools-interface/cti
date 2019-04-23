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

#include "frontend/mpir_iface/MPIRInstance.hpp"

namespace cti {
namespace fe_daemon {

using MPIRId = int;

// request types

enum ReqType : long {
	ForkExecvpApp,
	ForkExecvpUtil,

	LaunchMPIR,
	ReleaseMPIR,

	RegisterApp,
	RegisterUtil,
	DeregisterApp,

	Shutdown
};

// ForkExecvpApp, ForkExecvpUtil, LaunchMPIR
struct LaunchReq
{
	pid_t app_pid; // unused for ForkExecvpApp, LaunchMPIR
	int stdout_fd;
	int stderr_fd;
	// after sending this struct, send a list of null-terminated strings:
	// - file path string
	// - each argument string
	// - EMPTY STRING
	// - each environment variable string (format VAR=VAL)
	// - EMPTY STRING
	// sum of lengths of these strings including null-terminators should equal `file_and_argv_len`
};

struct ReleaseMPIRReq
{
	MPIRId mpir_id;
};

// RegisterApp, DeregisterApp
struct AppReq
{
	pid_t app_pid;
};

// RegisterUtil
struct UtilReq
{
	pid_t app_pid;
	pid_t util_pid;
};

// Response types

enum RespType : long {
	// Shutdown, RegisterApp, RegisterUtil, ReleaseMPIR
	OK,

	// ForkExecvpApp, ForkExecvpUtil
	PID,

	// LaunchMPIR
	MPIRProcTable,
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

struct MPIRProcTableResp
{
	RespType type;
	MPIRId mpir_id;
	int num_pids;
	// after sending this struct, send:
	// - list of `num_pids` pids
	// - list of `num_pids` null-terminated hostnames
};

}; // fe_daemon
}; // cti
