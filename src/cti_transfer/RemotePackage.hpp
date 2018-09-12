#pragma once

/* RemotePackage: represents a remote tarball ready for the cti_daemon to extract and / or 
	run a tooldaemon with. Created as a result of finalizing and shipping a Manifest.
*/

#include <string>

// pointer management
#include <memory>

#include "Session.hpp"
#include "Archive.hpp"

class RemotePackage final {
private: // variables
	const std::string archiveName;
	std::weak_ptr<Session> sessionPtr;
	const size_t instanceCount;

private: // functions
	inline void invalidate() { sessionPtr.reset(); }

public: // interface

	// run WLM shipping routine to stage archivePath
	RemotePackage(Archive&& archiveToShip, const std::string& archiveName_,
		std::shared_ptr<Session> liveSession, size_t instanceCount_);

	// object finalized after running extraction routines
	void extract();
	void extractAndRun(const char * const daemonPath, const char * const daemonArgs[], 
		const char * const envVars[]);
};