/******************************************************************************\
 * RemotePackage.cpp - Represents a remote tarball ready for the cti_daemon to
 * extract and / or run a tooldaemon with. Created as a result of finalizing
 * and shipping a Manifest.
 *
 * Copyright 2013-2019 Cray Inc.    All Rights Reserved.
 *
 * Unpublished Proprietary Information.
 * This unpublished work is protected to trade secret, copyright and other laws.
 * Except as permitted by contract or express written permission of Cray Inc.,
 * no part of this work or its content may be used, reproduced or disclosed
 * in any form.
 *
 ******************************************************************************/

#include "RemotePackage.hpp"

#include "useful/cti_wrappers.hpp"
#include "cti_defs.h"

// helper: promote session pointer to a shared pointer (otherwise throw)
static std::shared_ptr<Session> getSessionHandle(std::weak_ptr<Session> sessionPtr) {
	if (auto liveSession = sessionPtr.lock()) { return liveSession; }
	throw std::runtime_error("Manifest is not valid, already shipped.");
}

RemotePackage::RemotePackage(std::string const& archivePath, std::string const& archiveName,
	std::shared_ptr<Session>& liveSession, size_t instanceCount) :
		m_archiveName{archiveName},
		m_sessionPtr{liveSession},
		m_instanceCount{instanceCount} {

	liveSession->shipPackage(archivePath.c_str());
}

void RemotePackage::extract() {
	if (m_archiveName.empty()) { return; }

	auto liveSession = getSessionHandle(m_sessionPtr);

	// create DaemonArgv
	cti::OutgoingArgv<DaemonArgv> daemonArgv("cti_daemon");
	{ using DA = DaemonArgv;
		daemonArgv.add(DA::ApID,         liveSession->m_jobId);
		daemonArgv.add(DA::ToolPath,     liveSession->m_toolPath);
		daemonArgv.add(DA::WLMEnum,      liveSession->m_wlmType);
		daemonArgv.add(DA::ManifestName, m_archiveName);
		daemonArgv.add(DA::Directory,    liveSession->m_stageName);
		daemonArgv.add(DA::InstSeqNum,   std::to_string(m_instanceCount));
		if (getenv(DBG_ENV_VAR)) { daemonArgv.add(DA::Debug); };
	}

	// call transfer function with DaemonArgv
	liveSession->writeLog("finalizeAndExtract %d: starting daemon\n", m_instanceCount);
	// wlm_startDaemon adds the argv[0] automatically, so argv.get() + 1 for arguments.
	liveSession->startDaemon(daemonArgv.get() + 1);

	invalidate();
}

void RemotePackage::extractAndRun(const char * const daemonBinary,
	const char * const daemonArgs[], const char * const envVars[]) {

	auto liveSession = getSessionHandle(m_sessionPtr);

	// get real name of daemon binary
	const std::string binaryName(cti::getNameFromPath(cti::findPath(daemonBinary)));

	// create DaemonArgv
	liveSession->writeLog("extractAndRun: creating daemonArgv for %s\n", daemonBinary);
	cti::OutgoingArgv<DaemonArgv> daemonArgv("cti_daemon");
	{ using DA = DaemonArgv;
		daemonArgv.add(DA::ApID,         liveSession->m_jobId);
		daemonArgv.add(DA::ToolPath,     liveSession->m_toolPath);
		if (!liveSession->m_attribsPath.empty()) {
			daemonArgv.add(DA::PMIAttribsPath, liveSession->m_attribsPath);
		}
		if (!liveSession->getLdLibraryPath().empty()) {
			daemonArgv.add(DA::LdLibraryPath, liveSession->getLdLibraryPath());
		}
		daemonArgv.add(DA::WLMEnum,      liveSession->m_wlmType);
		if (!m_archiveName.empty()) { daemonArgv.add(DA::ManifestName, m_archiveName); }
		daemonArgv.add(DA::Binary,       binaryName);
		daemonArgv.add(DA::Directory,    liveSession->m_stageName);
		daemonArgv.add(DA::InstSeqNum,   std::to_string(m_instanceCount));
		daemonArgv.add(DA::Directory,    liveSession->m_stageName);
		if (getenv(DBG_ENV_VAR)) { daemonArgv.add(DA::Debug); };
	}

	// add env vars
	if (envVars != nullptr) {
		for (const char* const* var = envVars; *var != nullptr; var++) {
			daemonArgv.add(DaemonArgv::EnvVariable, *var);
		}
	}

	// add daemon arguments
	cti::ManagedArgv rawArgVec(daemonArgv.eject());
	if (daemonArgs != nullptr) {
		rawArgVec.add("--");
		for (const char* const* var = daemonArgs; *var != nullptr; var++) {
			rawArgVec.add(*var);
		}
	}

	// call launch function with DaemonArgv
	liveSession->writeLog("extractAndRun: starting daemon\n");
	// wlm_startDaemon adds the argv[0] automatically, so argv.get() + 1 for arguments.
	liveSession->startDaemon(rawArgVec.get() + 1);
	liveSession->writeLog("daemon started\n");

	invalidate();
}
