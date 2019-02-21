#pragma once

/* RemotePackage: represents a remote tarball ready for the cti_daemon to extract and / or 
	run a tooldaemon with. Created as a result of finalizing and shipping a Manifest.
*/

#include <string>

// pointer management
#include <memory>

#include "Session.hpp"

class RemotePackage final {
private: // variables
	std::string const m_archiveName;
	std::weak_ptr<Session> m_sessionPtr;
	size_t const m_instanceCount;

private: // functions
	inline void invalidate() { m_sessionPtr.reset(); }

public: // interface

	// run WLM shipping routine to stage archivePath
	RemotePackage(std::string const& archivePath, std::string const& archiveName,
		std::shared_ptr<Session>& liveSession, size_t instanceCount);

	// object finalized after running extraction routines
	void extract();
	void extractAndRun(const char * const daemonPath, const char * const daemonArgs[], 
		const char * const envVars[]);
};