/******************************************************************************\
 * cti_diagnostics.cpp - Diagnose CTI launch process
 *
 * Copyright 2021 Hewlett Packard Enterprise Development LP.
 *
 * Redistribution and use in source and binary forms, with or
 * without modification, are permitted provided that the following
 * conditions are met:
 *
 *  - Redistributions of source code must retain the above
 *copyright notice, this list of conditions and the following
 *disclaimer.
 *
 *  - Redistributions in binary form must reproduce the above
 *copyright notice, this list of conditions and the following
 *disclaimer in the documentation and/or other materials
 *provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 ******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <ifaddrs.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <netdb.h>

#include <string>
#include <memory>

#include "common_tools_fe.h"

static auto find_external_address()
{
	// Get information structs for all network interfaces
	auto ifaddr = std::unique_ptr<struct ifaddrs, decltype(&freeifaddrs)>{nullptr, freeifaddrs};
	{ struct ifaddrs *raw_ifaddr = nullptr;
		if (getifaddrs(&raw_ifaddr) < 0) {
			throw std::runtime_error(strerror(errno));
		}
		ifaddr = {raw_ifaddr, freeifaddrs};
	}

	// Find the first IP address that isn't localhost
	for (struct ifaddrs* ifa = ifaddr.get(); ifa != nullptr; ifa = ifa->ifa_next) {
		if (ifa->ifa_addr == NULL) {
			continue;
		}

		// Limit to IPv4 and IPv6
		auto const family = ifa->ifa_addr->sa_family;
		if ((family == AF_INET) || (family == AF_INET6)) {

			// Skip if loopback
			if (ifa->ifa_flags & IFF_LOOPBACK) {
				continue;
			}

			// Get hostname for interface
			char address[NI_MAXHOST];
			auto const sockaddr_size = (family == AF_INET)
				? sizeof(struct sockaddr_in)
				: sizeof(struct sockaddr_in6);

			if (auto const rc = getnameinfo(ifa->ifa_addr, sockaddr_size,
				address, NI_MAXHOST, nullptr, 0, NI_NUMERICHOST)) {
				throw std::runtime_error(strerror(errno));
			}

			return std::string{address};
		}
	}

	throw std::runtime_error("failed to find any external address");
}

static auto bind_any(std::string const& address)
{
	// Initialize socket connection hints
	struct addrinfo hints;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_NUMERICSERV;

	struct addrinfo *raw_listener = nullptr;
	if (auto const rc = getaddrinfo(address.c_str(), "0", &hints, &raw_listener)) {
		throw std::runtime_error(gai_strerror(rc));
	}
	auto listener = std::unique_ptr<struct addrinfo, decltype(&freeaddrinfo)>(std::move(raw_listener), freeaddrinfo);
	raw_listener = nullptr;

	// Create the socket
	auto const socket_fd = socket(listener->ai_family, listener->ai_socktype, listener->ai_protocol);
	if (socket_fd < 0) {
		throw std::runtime_error(strerror(errno));
	}

	// Bind the socket
	if (bind(socket_fd, listener->ai_addr, listener->ai_addrlen) < 0) {
		throw std::runtime_error(strerror(errno));
	}

	return socket_fd;
}

static auto launch_diagnostics_backend(cti_session_id_t session_id)
{
	// Get externally accessable address and bind
	auto const address = find_external_address();
	auto const socket = bind_any(address);

	// Listen on socket
	if (auto const listen_rc = listen(socket, 1)) {
		throw std::runtime_error(strerror(listen_rc));
	}

	// Get socket connection information
	auto port = std::string{};
	{ struct sockaddr_in sa;
		auto sa_len = socklen_t{sizeof(sa)};
		memset(&sa, 0, sizeof(sa));
		if (auto const getsockname_rc = getsockname(socket, (struct sockaddr*)&sa, &sa_len)) {
			throw std::runtime_error(strerror(getsockname_rc));
		}

		// Extract socket port
		port = std::to_string(ntohs(sa.sin_port));
	}

	// Create backend arguments
	char const* backend_args[] = {address.c_str(), port.c_str(), nullptr};

	// Create manifest
	auto const manifest_id = cti_createManifest(session_id);

	// Build path to backend executable
	auto backend_path = std::string{};
	if (auto const cti_install_dir = getenv(CTI_BASE_DIR_ENV_VAR)) {
		backend_path = std::string{cti_install_dir} + "/libexec/cti_diagnostics_backend";
	} else {
		throw std::runtime_error("Unable to locate CTI installation. Ensure \
a CTI module is loaded. Try `module load cray-cti` to load the system default CTI installation.");
	}

	// Launch backend
	if (cti_execToolDaemon(manifest_id, backend_path.c_str(), backend_args, nullptr)) {
		throw std::runtime_error("failed to launch diagnostics backend: " + std::string{cti_error_str()});
	}

	return socket;
}

static auto accept_connection(int const socket_fd)
{
	struct sockaddr_in  wa;
	auto wa_len = socklen_t{sizeof(wa)};

	auto const result_fd = accept(socket_fd, (struct sockaddr*)&wa, &wa_len);
	if (result_fd < 0) {
		throw std::runtime_error(strerror(errno));
	}

	return result_fd;
}

int main(int argc, char **argv)
{
	auto rc = int{-1};
	auto app_id = cti_app_id_t{0};
	auto session_id = cti_session_id_t{0};
	auto socket_fd = int{-1};
	auto backend_fd = int{-1};

	// Build path to test application
	auto application_path = std::string{};
	if (auto const cti_install_dir = getenv(CTI_BASE_DIR_ENV_VAR)) {
		application_path = std::string{cti_install_dir} + "/libexec/cti_diagnostics_target";
	} else {
		throw std::runtime_error("Unable to locate CTI installation. Ensure \
a CTI module is loaded. Try `module load cray-cti` to load the system default CTI installation.");
	}

	// Create launcher arguments
	char const* launcher_args[] = {application_path.c_str(), nullptr};

	// Launch test application
	app_id = cti_launchApp(launcher_args, -1, -1, nullptr, nullptr, nullptr);
	if (app_id == 0) {
		throw std::runtime_error("failed to launch diagnostic target at " + application_path + ": " + std::string{cti_error_str()});
	}

	// Create session for application
	session_id = cti_createSession(app_id);
	if (app_id == 0) {
		throw std::runtime_error("failed to create CTI session: " + std::string{cti_error_str()});
	}

	// Launch backend and accept connection
	socket_fd = launch_diagnostics_backend(session_id);
	backend_fd = accept_connection(socket_fd);

	// Read backend tests results
	{ char buf[1024];
		if (auto const len = read(backend_fd, buf, sizeof(buf) - 1)) {
			buf[len] = '\0';
			fprintf(stderr, "%s\n", buf);
		}
	}

	// Backend tests completed
	fprintf(stderr, "Diagnostic tests have completed. \
You may see a warning message about the diagnostic job being terminated by the workload manager\n");

	rc = 0;

cleanup:
	if (backend_fd >= 0) {
		close(backend_fd);
		backend_fd = -1;
	}
	if (socket_fd >= 0) {
		close(socket_fd);
		socket_fd = -1;
	}
	if (session_id) {
		cti_destroySession(session_id);
		session_id = 0;
	}
	if (app_id) {
		cti_deregisterApp(app_id);
		app_id = 0;
	}

	return rc;
}
