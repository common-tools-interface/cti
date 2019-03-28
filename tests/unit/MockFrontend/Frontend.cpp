/******************************************************************************\
 * Frontend.cpp - A mock frontend implementation
 *
 *
 * Unpublished Proprietary Information.
 * This unpublished work is protected to trade secret, copyright and other laws.
 * Except as permitted by contract or express written permission of Cray Inc.,
 * no part of this work or its content may be used, reproduced or disclosed
 * in any form.
 *
 ******************************************************************************/

// getpid
#include <sys/types.h>
#include <unistd.h>

#include <archive.h>
#include <archive_entry.h>

#include "Frontend.hpp"

#include "useful/make_unique_destr.hpp"

using ::testing::Return;
using ::testing::_;
using ::testing::Invoke;
using ::testing::WithArgs;
using ::testing::WithoutArgs;

/* MockFrontend implementation */

MockFrontend::MockFrontend()
{
	// describe behavior of mock launchBarrier
	ON_CALL(*this, launchBarrier(_, _, _, _, _, _))
		.WillByDefault(WithoutArgs(Invoke([]() {
			return std::make_unique<MockApp::Nice>(getpid());
		})));
}

/* MockApp implementation */

static size_t appCount = 0;

MockApp::MockApp(pid_t launcherPid)
	: m_launcherPid{launcherPid}
	, m_jobId{std::to_string(m_launcherPid) + std::to_string(appCount++)}
	, m_atBarrier{true}
{
	// describe behavior of mock releaseBarrier
	ON_CALL(*this, releaseBarrier())
		.WillByDefault(WithoutArgs(Invoke([&]() {
			if (!m_atBarrier) {
				throw std::runtime_error("app not at startup barrier");
			}
			m_atBarrier = false;
		})));

	// describe behavior of mock getToolPath
	ON_CALL(*this, getToolPath())
		.WillByDefault(Return("/mock/"));

	// describe behavior of mock shipPackage
	// add all files in archive to list to be checked by unit test
	ON_CALL(*this, shipPackage(_))
		.WillByDefault(WithArgs<0>(Invoke([&](std::string const& tarPath) {
			// initialize archive struct
			auto archPtr = make_unique_destr(archive_read_new(), archive_read_free);
			archive_read_support_filter_all(archPtr.get());
			archive_read_support_format_all(archPtr.get());

			// open archive
			if (archive_read_open_filename(archPtr.get(), tarPath.c_str(), 10240) != ARCHIVE_OK) {
				throw std::runtime_error("failed to open archive");
			}

			// read each file / directory entry
			struct archive_entry *entry;
			while (archive_read_next_header(archPtr.get(), &entry) == ARCHIVE_OK) {
				auto const path = std::string{archive_entry_pathname(entry)};
				// only add it to the list if it's a file
				if (!path.empty() && (path.back() != '/')) {
					m_shippedFilePaths.push_back(path);
				}
				archive_read_data_skip(archPtr.get());
			}
		})));
}
