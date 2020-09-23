/******************************************************************************\
 * Use various heuristics to find the externally-accessible frontend hostname
 *
 * Copyright 2020 Hewlett Packard Enterprise Development LP.
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
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

// This pulls in config.h
#include "cti_defs.h"

#include <string>
#include <fstream>

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#include "useful/cti_wrappers.hpp"

namespace cti
{

static inline std::string
detectFrontendHostname()
{
    auto make_addrinfo = [](std::string const& hostname) {
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
    };

    // Resolve a hostname to IPv4 address
    // FIXME: PE-26874 change this once DNS support is added
    auto resolveHostname = [](const struct addrinfo& addr_info) {
        constexpr auto MAXADDRLEN = 15;
        // Extract IP address string
        char ip_addr[MAXADDRLEN + 1];
        if (auto const rc = getnameinfo(addr_info.ai_addr, addr_info.ai_addrlen, ip_addr, MAXADDRLEN, NULL, 0, NI_NUMERICHOST)) {
            throw std::runtime_error("getnameinfo failed: " + std::string{gai_strerror(rc)});
        }
        ip_addr[MAXADDRLEN] = '\0';
        return std::string{ip_addr};
    };

    // Get the hostname of the interface that is accessible from compute nodes
    // Behavior changes based on XC / Shasta UAI+UAN
    auto detectAddress = [&make_addrinfo, &resolveHostname]() {
        // Try the nid file first. This is the preferred mechanism since it corresponds to the HSN.
        // XT / XC NID file
        try {
            // Use the NID to create the XT hostname format.
            // We expect this file to have a numeric value giving our current Node ID.
            std::string nidString;
            if (!std::getline(std::ifstream{CRAY_XT_NID_FILE}, nidString)) {
                throw std::runtime_error("failed to parse NID file");
            }
            auto const nid = std::stoi(nidString);
            auto nidXTHostname = cti::cstr::asprintf(CRAY_XT_HOSTNAME_FMT, nid);
            // Ensure we can resolve the hostname
            make_addrinfo(nidXTHostname);
            // Hostname checks out so return it
            return nidXTHostname;
        } catch (std::exception const& ex) {
            // continue processing
        }

        // Shasta UAN xname file
        try {
            // Try to extract the hostname from the xname file path
            std::string xnameString;
            if (std::getline(std::ifstream{CRAY_SHASTA_UAN_XNAME_FILE}, xnameString)) {
                return xnameString;
            }
        } catch (std::exception const& ex) {
            // continue processing
        }

        // On Shasta UAI, look up and return IPv4 address instead of hostname
        // UAI hostnames cannot be resolved on compute node
        // FIXME: PE-26874 change this once DNS support is added
        auto const hostname = cti::cstr::gethostname();
        try {
            // Compute-accessible macVLAN hostname is UAI hostname appended with '-nmn'
            // See https://connect.us.cray.com/jira/browse/CASMUSER-1391
            // https://stash.us.cray.com/projects/UAN/repos/uan-img/pull-requests/51/diff#entrypoint.sh
            auto const macVlanHostname = hostname + "-nmn";
            auto info = make_addrinfo(macVlanHostname);
            // FIXME: Remove this when PE-26874 is fixed
            auto macVlanIPAddress = resolveHostname(*info);
            return macVlanIPAddress;
        }
        catch (std::exception const& ex) {
            // continue processing
        }
        // Try using normal hostname
        auto info = make_addrinfo(hostname);
        return hostname;
    };

    // Cache the hostname result.
    static auto hostname = detectAddress();
    return hostname;
}

} // namespace cti
