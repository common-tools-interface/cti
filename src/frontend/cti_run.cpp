/******************************************************************************\
 * cti_run.c - A generic interface to launch and interact with applications.
 *	      This provides the tool developer with an easy to use interface to
 *	      start new instances of an application.
 *
 * Copyright 2011-2014 Cray Inc.  All Rights Reserved.
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
 
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <sys/types.h>
#include <sys/stat.h>

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

#include "cti_error.h"
#include "cti_fe.h"
#include "cti_run.h"

#include "frontend/Frontend.hpp"

// run code that can throw and use it to set cti error instead
#include <functional>
static int runSafely(std::string const& caller, std::function<void()> f) noexcept {
	try { // try to run the function
		f();
		return 0;
	} catch (const std::exception& ex) {
		// if we get an exception, set cti error instead
		_cti_set_error((caller + ": " + ex.what()).c_str());
		return 1;
	}
}

static int
_cti_checkofd(int fd) {
	int flags;
	
	// if fd is -1, then the fd arg is meant to be ignored
	if (fd == -1)
		return 0;
	
	errno = 0;
	flags = fcntl(fd, F_GETFL);
	if (errno != 0)
		return 1;
	flags &= O_ACCMODE;
	if ((flags & O_WRONLY) || (flags & O_RDWR))
		return 0;
		
	return 1;
}

// This does sanity checking on args in common for both launchApp and launchAppBarrier
static void
_cti_checkLaunchArgs(	int stdout_fd, int stderr_fd, const char *inputFile, 
						const char *chdirPath) {
	// check stdout, stderr
	if (_cti_checkofd(stdout_fd)) {
		throw std::runtime_error("Invalid stdout_fd argument.");
	}
	if (_cti_checkofd(stderr_fd)) {
		throw std::runtime_error("Invalid stderr_fd argument.");
	}

	// verify inputfile is good
	if (inputFile != nullptr) {
		struct stat st;
		if (stat(inputFile, &st)) { // make sure inputfile exists
			throw std::runtime_error("Invalid inputFile argument. File does not exist.");
		}
		if (!S_ISREG(st.st_mode)) { // make sure it is a regular file
			throw std::runtime_error("Invalid inputFile argument. The file is not a regular file.");
		}
		if (access(inputFile, R_OK)) { // make sure we can access it
			throw std::runtime_error("Invalid inputFile argument. Bad permissions.");
		}
	}

	// verify chdirpath is good
	if (chdirPath != nullptr) {
		struct stat st;
		if (stat(chdirPath, &st)) { // make sure chdirpath exists
			throw std::runtime_error("Invalid chdirPath argument. Directory does not exist.");
		}
		if (!S_ISDIR(st.st_mode)) { // make sure it is a directory
			throw std::runtime_error("Invalid chdirPath argument. The file is not a directory.");
		}
		if (access(chdirPath, R_OK | W_OK | X_OK)) { // make sure we can access it
			throw std::runtime_error("Invalid chdirPath argument. Bad permissions.");
		}
	}
}

cti_app_id_t
cti_launchApp(	const char * const launcher_argv[], int stdout_fd, int stderr_fd,
				const char *inputFile, const char *chdirPath,
				const char * const env_list[]) {

	cti_app_id_t appId;
	return runSafely("cti_launchApp", [&](){
		_cti_checkLaunchArgs(stdout_fd, stderr_fd, inputFile, chdirPath);
		appId = _cti_getCurrentFrontend().launch(launcher_argv, stdout_fd, stderr_fd, inputFile, chdirPath, env_list);
	}) ? 0 : appId;
}

cti_app_id_t
cti_launchAppBarrier(	const char * const launcher_argv[], int stdout_fd, int stderr_fd,
				const char *inputFile, const char *chdirPath,
				const char * const env_list[]) {

	cti_app_id_t appId;
	return runSafely("cti_launchAppBarrier", [&](){
		_cti_checkLaunchArgs(stdout_fd, stderr_fd, inputFile, chdirPath);
		appId = _cti_getCurrentFrontend().launchBarrier(launcher_argv, stdout_fd, stderr_fd, inputFile, chdirPath, env_list);
	}) ? 0 : appId;
}

int
cti_releaseAppBarrier(cti_app_id_t appId) {
	return runSafely("cti_releaseAppBarrier", [&](){
		_cti_getCurrentFrontend().releaseBarrier(appId);
	});
}

int
cti_killApp(cti_app_id_t appId, int signum) {
	return runSafely("cti_killApp", [&](){
		_cti_getCurrentFrontend().killApp(appId, signum);
	});
}

