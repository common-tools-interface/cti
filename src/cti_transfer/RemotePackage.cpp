#include "RemotePackage.hpp"

#include "useful/cti_wrappers.hpp"
#include "ArgvDefs.hpp"

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
	OutgoingArgv<DaemonArgv> daemonArgv("cti_daemon");
	{ using DA = DaemonArgv;
		daemonArgv.add(DA::ApID,         liveSession->jobId);
		daemonArgv.add(DA::ToolPath,     liveSession->toolPath);
		daemonArgv.add(DA::WLMEnum,      liveSession->wlmEnum);
		daemonArgv.add(DA::ManifestName, m_archiveName);
		daemonArgv.add(DA::Directory,    liveSession->stageName);
		daemonArgv.add(DA::InstSeqNum,   std::to_string(m_instanceCount));
		if (getenv(DBG_ENV_VAR)) { daemonArgv.add(DA::Debug); };
	}

	// call transfer function with DaemonArgv
	DEBUG_PRINT("finalizeAndExtract " << m_instanceCount << ": starting daemon" << std::endl);
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
	DEBUG_PRINT("extractAndRun: creating daemonArgv for " << daemonBinary << std::endl);
	OutgoingArgv<DaemonArgv> daemonArgv("cti_daemon");
	{ using DA = DaemonArgv;
		daemonArgv.add(DA::ApID,         liveSession->jobId);
		daemonArgv.add(DA::ToolPath,     liveSession->toolPath);
		if (!liveSession->attribsPath.empty()) {
			daemonArgv.add(DA::PMIAttribsPath, liveSession->attribsPath);
		}
		if (!liveSession->getLdLibraryPath().empty()) {
			daemonArgv.add(DA::LdLibraryPath, liveSession->getLdLibraryPath());
		}
		daemonArgv.add(DA::WLMEnum,      liveSession->wlmEnum);
		if (!m_archiveName.empty()) { daemonArgv.add(DA::ManifestName, m_archiveName); }
		daemonArgv.add(DA::Binary,       binaryName);
		daemonArgv.add(DA::Directory,    liveSession->stageName);
		daemonArgv.add(DA::InstSeqNum,   std::to_string(m_instanceCount));
		daemonArgv.add(DA::Directory,    liveSession->stageName);
		if (getenv(DBG_ENV_VAR)) { daemonArgv.add(DA::Debug); };
	}

	// add env vars
	if (envVars != nullptr) {
		for (const char* const* var = envVars; *var != nullptr; var++) {
			daemonArgv.add(DaemonArgv::EnvVariable, *var);
		}
	}

	// add daemon arguments
	ManagedArgv rawArgVec(daemonArgv.eject());
	if (daemonArgs != nullptr) {
		rawArgVec.add("--");
		for (const char* const* var = daemonArgs; *var != nullptr; var++) {
			rawArgVec.add(*var);
		}
	}

	// call launch function with DaemonArgv
	DEBUG_PRINT("extractAndRun: starting daemon" << std::endl);
	// wlm_startDaemon adds the argv[0] automatically, so argv.get() + 1 for arguments.
	liveSession->startDaemon(rawArgVec.get() + 1);
	DEBUG_PRINT("daemon started" << std::endl);

	invalidate();
}