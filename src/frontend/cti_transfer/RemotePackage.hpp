/******************************************************************************\
 * RemotePackage.hpp - Represents a remote tarball ready for the cti_daemon to
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

#pragma once

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
