/******************************************************************************\
 * mpir_iface.cpp
 *
 * Copyright 2018 Cray Inc.  All Rights Reserved.
 *
 * Unpublished Proprietary Information.
 * This unpublished work is protected to trade secret, copyright and other laws.
 * Except as permitted by contract or express written permission of Cray Inc.,
 * no part of this work or its content may be used, reproduced or disclosed
 * in any form.
 *
 ******************************************************************************/

#include <memory>

#include <unistd.h>
#include <string.h>
#include <fcntl.h>

#include "MPIRInstance.hpp"

#include "mpir_iface.h"

static std::map<mpir_id_t, std::unique_ptr<MPIRInstance>> mpirInstances;

static const mpir_id_t INSTANCE_ERROR = -1;
static mpir_id_t newId() noexcept {
	static mpir_id_t nextId = 1;
	return nextId++;
}

template <typename FuncType, typename ResultType = decltype(std::declval<FuncType>()())>
inline static ResultType runSafely(std::string const& caller, FuncType func,
	ResultType onError = ResultType()) noexcept {

	try { // try to run the function
		DEBUG(std::cerr, caller << std::endl);
		return func();
	} catch (const std::exception& ex) {
		return onError;
	}
}

mpir_id_t _cti_mpir_newLaunchInstance(const char *launcher, const char * const launcher_args[],
	const char * const env_list[], int stdin_fd, int stdout_fd, int stderr_fd) {
	return runSafely("_cti_mpir_newLaunchInstance", [&](){

	mpir_id_t id = newId();

	/* construct argv array & instance*/
	std::vector<std::string> launcherArgv{launcher};
	for (const char* const* arg = launcher_args; *arg != nullptr; arg++) {
		launcherArgv.emplace_back(*arg);
	}

	/* env_list null-terminated strings in format <var>=<val>*/
	std::vector<std::string> envVars;
	if (env_list != nullptr) {
		for (const char* const* arg = env_list; *arg != nullptr; arg++) {
			envVars.emplace_back(*arg);
		}
	}

	/* provide proper input / output fds */
	if (stdin_fd < 0) {
		DEBUG(std::cerr, "stdin: " << stdin_fd << " stdout: " << stdout_fd << " stderr: " << stderr_fd << std::endl);
		return INSTANCE_ERROR;
	}
	std::map<int, int> remapFds {
		{stdin_fd,  STDIN_FILENO}
	};
	if (stdout_fd >= 0) { remapFds[stdout_fd] = STDOUT_FILENO; }
	if (stderr_fd >= 0) { remapFds[stderr_fd] = STDERR_FILENO; }

	/* construct the instance */
	try {
		mpirInstances.emplace(id, new MPIRInstance(std::string(launcher), launcherArgv, envVars, remapFds));
	} catch (...) {
		return INSTANCE_ERROR;
	}

	return id;
});}

mpir_id_t _cti_mpir_newAttachInstance(const char *launcher, pid_t pid) {
	DEBUG(std::cerr, "_cti_mpir_newAttachInstance" << std::endl);
	mpir_id_t id = newId();

	try {
		mpirInstances.emplace(id, new MPIRInstance(launcher, pid));
	} catch (...) {
		return INSTANCE_ERROR;
	}

	return id;
}

int _cti_mpir_releaseInstance(mpir_id_t id) {
	DEBUG(std::cerr, "_cti_mpir_releaseInstance" << std::endl);
	auto it = mpirInstances.find(id);
	if (it == mpirInstances.end()) { return 1; }

	DEBUG(std::cerr, "releasing id " << id << std::endl);

	mpirInstances.erase(it);
	return 0;
}

void _cti_mpir_releaseAllInstances(void) {
	DEBUG(std::cerr, "_cti_mpir_releaseAllInstances" << std::endl);
	mpirInstances.clear();
}

char* _cti_mpir_getStringAt(mpir_id_t id, const char *symbol) {
	DEBUG(std::cerr, "_cti_mpir_getStringAt" << std::endl);
	auto it = mpirInstances.find(id);
	if (it == mpirInstances.end()) { return nullptr; }

	return strdup(it->second->readStringAt(symbol).c_str());
}

cti_mpir_procTable_t* _cti_mpir_newProcTable(mpir_id_t id) {
	return runSafely("_cti_mpir_newProcTable", [&](){

	auto it = mpirInstances.find(id);
	if (it == mpirInstances.end()) { return static_cast<cti_mpir_procTable_t*>(nullptr); }

	cti_mpir_procTable_t *procTable_C = new cti_mpir_procTable_t;

	/* get instance proctable */
	std::vector<MPIRInstance::MPIR_ProcTableElem> procTable_CXX = it->second->getProcTable();

	/* allocate subtables */
	procTable_C->num_pids  = procTable_CXX.size();
	procTable_C->pids      = new pid_t[procTable_C->num_pids];
	procTable_C->hostnames = new char*[procTable_C->num_pids];

	/* copy elements */
	for (size_t i = 0; i < procTable_C->num_pids; i++) {
		procTable_C->pids[i]      = procTable_CXX[i].pid;
		procTable_C->hostnames[i] = strdup(procTable_CXX[i].hostname.c_str());
	}

	return procTable_C;
});}

void _cti_mpir_deleteProcTable(cti_mpir_procTable_t *proc_table) {
	DEBUG(std::cerr, "_cti_mpir_deleteProcTable" << std::endl);
	if (proc_table == nullptr) { return; }

	for (size_t i = 0; i < proc_table->num_pids; i++) {
		free(proc_table->hostnames[i]);
	}
	delete[] proc_table->hostnames;
	delete[] proc_table->pids;
	delete   proc_table;
}

pid_t _cti_mpir_getLauncherPid(mpir_id_t id) {
	DEBUG(std::cerr, "_cti_mpir_getLauncherPid" << std::endl);
	auto it = mpirInstances.find(id);
	if (it == mpirInstances.end()) { return 0; }

	return it->second->getLauncherPid();
}
