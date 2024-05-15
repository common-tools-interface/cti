/******************************************************************************\
 * Use various heuristics to find the externally-accessible frontend hostname
 *
 * Copyright 2020 Hewlett Packard Enterprise Development LP.
 * SPDX-License-Identifier: Linux-OpenIB
 ******************************************************************************/

// This pulls in config.h
#include "cti_defs.h"

#include <string>
#include <fstream>
#include <future>

#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>

#include "useful/cti_execvp.hpp"
#include "useful/cti_wrappers.hpp"
#include "useful/cti_split.hpp"

static auto make_addrinfo(std::string const& hostname)
{
    // Get hostname information
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    struct addrinfo *info_ptr = nullptr;
    if (auto const rc = getaddrinfo(hostname.c_str(), nullptr, &hints, &info_ptr)) {
        throw std::runtime_error("getaddrinfo failed: " + std::string{gai_strerror(rc)});
    }
    if ( info_ptr == nullptr ) {
        throw std::runtime_error("failed to resolve hostname " + hostname);
    }
    return cti::take_pointer_ownership(std::move(info_ptr), freeaddrinfo);
}

// Resolve a hostname to IPv4 address
// FIXME: PE-26874 change this once DNS support is added
static auto resolve_hostname(const struct addrinfo& addr_info)
{
    constexpr auto MAXADDRLEN = 15;
    // Extract IP address string
    char ip_addr[MAXADDRLEN + 1];
    if (auto const rc = getnameinfo(addr_info.ai_addr, addr_info.ai_addrlen, ip_addr, MAXADDRLEN, NULL, 0, NI_NUMERICHOST)) {
        throw std::runtime_error("getnameinfo failed: " + std::string{gai_strerror(rc)});
    }
    ip_addr[MAXADDRLEN] = '\0';
    return std::string{ip_addr};
}

// Try to connect to the given address and port
static auto try_connect(std::string const& address, int port)
{
    // Create conncetion socket
    auto sockfd = cti::fd_handle{::socket(AF_INET, SOCK_STREAM, 0)};

    // Set connect port number
    struct sockaddr_in serv_addr;
    ::memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = ::htons(port);

    // Set address name
    if (::inet_pton(AF_INET, address.c_str(), &(serv_addr.sin_addr)) <= 0) {
        return false;
    }

    // Set timeout to half a second
    struct timeval timeout { .tv_sec = 0, .tv_usec = 500000 };
    if (::setsockopt(sockfd.fd(), SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) < 0) {
        return false;
    }

    // Try to connect to the socket
    if (::connect(sockfd.fd(), (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        return false;
    }

    // Send a byte
    if (::send(sockfd.fd(), address.c_str(), 1, 0) < 0) {
        return false;
    }

    return true;
}

// Determine whether the local machine is reachable via the given address
static auto is_local_address_reachable(std::string const& address)
{
    try {
        // Host on arbitrary port
        auto sockfd = cti::fd_handle{::socket(AF_INET, SOCK_STREAM, 0)};

        // Get socket information
        struct sockaddr_in serv_addr;
        ::memset(&serv_addr, 0, sizeof(serv_addr));
        serv_addr.sin_family = AF_INET;
        serv_addr.sin_addr.s_addr = ::htonl(INADDR_ANY);
        serv_addr.sin_port = 0;

        // Set timeout to half a second
        struct timeval timeout { .tv_sec = 0, .tv_usec = 500000 };
        if (::setsockopt(sockfd.fd(), SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
            return false;
        }

        // Bind to socket
        if (::bind(sockfd.fd(), (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
            return false;
        }

        // Get port number
        socklen_t len = sizeof(serv_addr);
        ::getsockname(sockfd.fd(), (struct sockaddr*)&serv_addr, &len);
        auto port = ::ntohs(serv_addr.sin_port);
        if (port == 0) {
            return false;
        }

        // Launch connector thread
        auto try_connect_future = std::async(std::launch::async,
            try_connect, address, port);

        // Listen for incoming connections
        ::listen(sockfd.fd(), 5);
        auto readfds = fd_set{};
        FD_ZERO(&readfds);
        FD_SET(sockfd.fd(), &readfds);

        // Wait for incoming connection with timeout
        auto ready = ::select(sockfd.fd() + 1, &readfds, nullptr, nullptr, &timeout);
        if (ready < 0) {
            return false;
        } else if (ready == 0) {
            // Timed out
            return false;
        }
        if (!FD_ISSET(sockfd.fd(), &readfds)) {
            return false;
        }

        // Accept incoming connection
        auto clientfd = cti::fd_handle{::accept(sockfd.fd(), (struct sockaddr*)nullptr, nullptr)};

        // Receive byte
        char recv_byte;
        if (::recv(clientfd.fd(), &recv_byte, 1, 0) < 0) {
            return false;
        }

        return try_connect_future.get();
 
    } catch (std::exception const& ex) {
        return false;
    }
}

namespace cti
{

static inline std::string
detectFrontendHostname()
{
    // Look up and resolve hostname
    static auto _address = []() {
        auto hostname = cti::cstr::gethostname();
        auto info = make_addrinfo(hostname);
        try {
            return resolve_hostname(*info);
        } catch (std::exception const& ex) {
            if (::getenv(CTI_DBG_ENV_VAR) != nullptr) {
                fprintf(stderr, "warning: %s, using system hostname\n", ex.what());
            }
            return hostname;
        }
    }();
    return _address;
}

// Run cminfo query
static auto cminfo_query(char const* option)
{
    char const* cminfoArgv[] = { "cminfo", option, nullptr };

    // Start cminfo
    try {
        auto cminfoOutput = cti::Execvp{"cminfo", (char* const*)cminfoArgv, cti::Execvp::stderr::Ignore};

        // Return first line of query
        auto& cminfoStream = cminfoOutput.stream();
        std::string line;
        if (std::getline(cminfoStream, line)) {
            return line;
        }

    } catch (...) {
        return std::string{};
    }

    return std::string{};
};

static inline std::string
detectHpcmAddress()
{
    // Get names of high speed networks
    auto networkNames = cminfo_query("--data_net_names");

    // Default to `hsnX` as network name if it is listed
    auto hsnNetworkNames = std::vector<std::string>{};
    auto nonHsnNetworkNames = std::vector<std::string>{};

    // Check all reported names
    while (!networkNames.empty()) {

        // Extract first HSN name in comma-separated list
        auto [networkName, rest] = cti::split::string<2>(std::move(networkNames), ',');

        // Store non-HSN network names for next query
        if (networkName.rfind("hsn", 0) == 0) {
            hsnNetworkNames.push_back(std::move(networkName));
        } else {
            nonHsnNetworkNames.push_back(std::move(networkName));
        }

        // Retry with next name
        networkNames = std::move(rest);
    }

    auto is_network_reachable = [](std::string const& networkName) {
        auto const addressOption = "--" + networkName + "_ip";

        // Query network address
        if (auto address = cminfo_query(addressOption.c_str()); !address.empty()) {

            // Verify address reachable
            if (is_local_address_reachable(address)) {
                return address;
            }
        }

        return std::string{};
    };

    // Check HSN first
    for (auto&& networkName : hsnNetworkNames) {
        if (auto address = is_network_reachable(networkName); !address.empty()) {
            return address;
        }
    }

    // Query other network addresses
    for (auto&& networkName : nonHsnNetworkNames) {
        if (auto address = is_network_reachable(networkName); !address.empty()) {
            return address;
        }
    }

    // Fall back to shared implementation supporting both XC and Shasta
    return cti::detectFrontendHostname();
}

} // namespace cti
