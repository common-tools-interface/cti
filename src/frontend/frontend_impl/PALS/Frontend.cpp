/******************************************************************************\
 * Frontend.cpp - PALS specific frontend library functions.
 *
 * Copyright 2014-2019 Cray Inc. All Rights Reserved.
 *
 * Unpublished Proprietary Information.
 * This unpublished work is protected to trade secret, copyright and other laws.
 * Except as permitted by contract or express written permission of Cray Inc.,
 * no part of this work or its content may be used, reproduced or disclosed
 * in any form.
 *
 ******************************************************************************/

// This pulls in config.h
#include "cti_defs.h"
#include "cti_argv_defs.hpp"

#include <memory>
#include <thread>

#include <fstream>

#include "transfer/Manifest.hpp"

#include "PALS/Frontend.hpp"

#include "useful/cti_websocket.hpp"

// Boost JSON
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>

// Boost array stream
#include <boost/iostreams/device/array.hpp>
#include <boost/iostreams/stream.hpp>

/* helper functions */

static auto const home_directory()
{
    static auto const _dir = []() {
        if (auto const homeDir = ::getenv("HOME")) {
            return std::string{homeDir};
        }

        auto const [pwd, pwd_buf] = cti::getpwuid(geteuid());

        return std::string{pwd.pw_dir};
    }();

    return _dir.c_str();
}

// Get name of active configuration
static constexpr auto defaultActiveConfigFilePattern  = "%s/.config/cray/active_config";
static auto readActiveConfig(std::string const& activeConfigFilePath)
{
    auto result = std::string{};

    auto fileStream = std::ifstream{activeConfigFilePath};
    if (std::getline(fileStream, result)) {
        return result;
    }

    throw std::runtime_error("failed to read active config from " + activeConfigFilePath);
}

// Get pair of hostname, username for authentication
static constexpr auto defaultConfigFilePattern = "%s/.config/cray/configurations/%s";
static auto readHostnameUsernamePair(std::string const& configFilePath)
{
    auto hostname = std::string{};
    auto username = std::string{};

    auto fileStream = std::ifstream{configFilePath};
    auto line = std::string{};
    while (std::getline(fileStream, line)) {
        if (line.substr(0, 20) == "hostname = \"https://") {
            hostname = line.substr(20, line.length() - 21);
        } else if (line.substr(0, 12) == "username = \"") {
            username = line.substr(12, line.length() - 13);
        }
    }

    if (hostname.empty() || username.empty()) {
        throw std::runtime_error("failed to read hostname and username from " + configFilePath);
    }

    return std::make_pair(hostname, username);
}

// Hostname into token name
// See `hostname_to_name` in https://stash.us.cray.com/projects/CLOUD/repos/craycli/browse/cray/utils.py
static auto hostnameToName(std::string url)
{
    // Extract hostname from URL
    auto const endpointSep = url.find("/");
    auto const len = (endpointSep != std::string::npos)
        ? endpointSep
        : std::string::npos;
    url = url.substr(0, len);

    // Replace - and . with _
    std::replace(url.begin(), url.end(), '-', '_');
    std::replace(url.begin(), url.end(), '.', '_');

    return url;
}

// Load token from disk
static constexpr auto defaultTokenFilePattern = "%s/.config/cray/tokens/%s.%s";
static auto readAccessToken(std::string const& tokenPath)
{
    namespace pt = boost::property_tree;

    // Load and parse token JSON
    auto root = pt::ptree{};
    try {
        pt::read_json(tokenPath, root);
    } catch (pt::json_parser::json_parser_error const& parse_ex) {
        throw std::runtime_error("failed to read token file at " + tokenPath);
    }

    // Extract token value
    try {
        return root.get<std::string>("access_token");
    } catch (pt::ptree_bad_path const& path_ex) {
        throw std::runtime_error("failed to find 'access_token' in file " + tokenPath);
    }
}

// Extract and map application and node placement information from JSON string
static auto parsePalsLaunchInfo(std::string const& launchInfoJson)
{
    namespace pt = boost::property_tree;

    // Create stream from string source
    auto jsonSource = boost::iostreams::array_source{launchInfoJson.c_str(), launchInfoJson.size()};
    auto jsonStream = boost::iostreams::stream<boost::iostreams::array_source>{jsonSource};

    // Load and parse JSON
    auto root = pt::ptree{};
    try {
        pt::read_json(jsonStream, root);
    } catch (pt::json_parser::json_parser_error const& parse_ex) {
        throw std::runtime_error("failed to parse json: '" + launchInfoJson + "'");
    }

    // Extract PALS hostname and placement data into CTI host array
    auto result = PALSFrontend::PalsLaunchInfo
        { .apId = root.get<std::string>("apid")
        , .hostsPlacement = {}
    };

    // Create list of hostnames with no PEs
    for (auto const& hostnameNodePair : root.get_child("hosts")) {
        result.hostsPlacement.emplace_back(CTIHost
            { .hostname = hostnameNodePair.second.template get<std::string>("")
            , .numPEs   = 0
        });
    }

    // Count PEs
    for (auto const& hostPlacementNodePair : root.get_child("placement")) {
        auto const nodeIdx = hostPlacementNodePair.second.template get<int>("");
        result.hostsPlacement[nodeIdx].numPEs++;
    }

    return result;
}

static constexpr auto launchJson = R"(
{ "argv": ["/lus/adangelo/signals"]
, "wdir": "/lus/adangelo"
, "hosts":
    [ "nid000001"
    , "nid000002"
    ]
, "nranks": 4
, "ppn": 2
, "depth": 1
, "environment":
    [ "PATH=/bin"
    , "USER=uastest"
    , "LD_LIBRARY_PATH=/opt/cray/pe/papi/default/lib64:/opt/cray/libfabric/1.9.0a1-064cb7444/lib64"
    ]
, "envalias":
    { "APRUN_APP_ID": "PALS_APID"
    }
}
)";

/* PALSFrontend implementation */

bool in_test = false;

bool
PALSFrontend::isSupported()
{
    if (in_test) {
        return false;
    }
    in_test = true;

    auto const activeConfig = readActiveConfig(
        cti::cstr::asprintf(defaultActiveConfigFilePattern, home_directory()));
    auto const [hostname, username] = readHostnameUsernamePair(
        cti::cstr::asprintf(defaultConfigFilePattern, home_directory(), activeConfig.c_str()));
    auto const tokenName = hostnameToName(hostname);
    auto const accessToken = readAccessToken(
        cti::cstr::asprintf(defaultTokenFilePattern, home_directory(), tokenName.c_str(), username.c_str()));

    auto const launchResult = cti::postJsonReq(hostname, "/apis/pals/v1/apps", accessToken, launchJson);
    fprintf(stderr, "launch result: '%s'\n", launchResult.c_str());

    auto const launchInfo = parsePalsLaunchInfo(launchResult);
    fprintf(stderr, "apId: %s\n", launchInfo.apId.c_str());
    for (auto&& ctiHost : launchInfo.hostsPlacement) {
        fprintf(stderr, "host %s has %lu ranks\n", ctiHost.hostname.c_str(), ctiHost.numPEs);
    }

    auto ioc = boost::asio::io_context{};
    auto resolver = boost::asio::ip::tcp::resolver{ioc};
    auto webSocketStream = boost::beast::websocket::stream<boost::asio::ip::tcp::socket>{ioc};

    auto const resolver_results = resolver.resolve(hostname, "80");
    boost::asio::connect(webSocketStream.next_layer(), resolver_results.begin(), resolver_results.end());

    webSocketStream.set_option(boost::beast::websocket::stream_base::decorator(
        [hostname, accessToken](boost::beast::websocket::request_type& req) {
            req.set(boost::beast::http::field::host, hostname);
            req.set("Authorization", "Bearer " + accessToken);
            req.set(boost::beast::http::field::user_agent, CTI_RELEASE_VERSION);

            req.set(boost::beast::http::field::accept, "application/json");
            req.set(boost::beast::http::field::content_type, "application/json");
        }));

    webSocketStream.handshake(hostname, "/apis/pals/v1/apps/" + launchInfo.apId + "/stdio");

    auto buffer = boost::beast::flat_buffer{};
    auto ec = boost::beast::error_code{};

    auto const streamUuid = boost::uuids::to_string(boost::uuids::random_generator()());
    auto const streamRpc = cti::cstr::asprintf(" \
        { \"jsonrpc\": \"2.0\" \
        , \"method\": \"stream\" \
        , \"params\": { \"apid\": \"%s\" } \
        , \"id\": \"%s\" \
        }", launchInfo.apId.c_str(), streamUuid.c_str());
    fprintf(stderr, "write: '%s'\n", streamRpc.c_str());
    webSocketStream.write(boost::asio::buffer(streamRpc));

    auto const startUuid = boost::uuids::to_string(boost::uuids::random_generator()());
    auto const startRpc = cti::cstr::asprintf(" \
        { \"jsonrpc\": \"2.0\" \
        , \"method\": \"start\" \
        , \"params\": { \"apid\": \"%s\" } \
        , \"id\": \"%s\" \
        }", launchInfo.apId.c_str(), startUuid.c_str());
    fprintf(stderr, "write: '%s'\n", startRpc.c_str());
    webSocketStream.write(boost::asio::buffer(startRpc));

    while (true) {
        auto const bytes_read = webSocketStream.read(buffer, ec);
        if (ec == boost::beast::websocket::error::closed) {
            fprintf(stderr, "closed\n");
            break;
        } else if (ec) {
            auto const errmsg = ec.message();
            fprintf(stderr, "failed: %s\n", errmsg.c_str());
            break;
        }
        auto charsPtr = static_cast<char*>(buffer.data().data());
        charsPtr[bytes_read] = '\0';
        fprintf(stderr, "output: '%s'\n", charsPtr);
        buffer.clear();
    }

    // webSocketStream.close(boost::beast::websocket::close_code::normal);

    in_test = false;

    return false;
}

std::weak_ptr<App>
PALSFrontend::launchBarrier(CArgArray launcher_argv, int stdout_fd, int stderr_fd,
    CStr inputFile, CStr chdirPath, CArgArray env_list)
{
    throw std::runtime_error{"not implemented: " + std::string{__func__}};
}

std::weak_ptr<App>
PALSFrontend::registerJob(size_t numIds, ...)
{
    throw std::runtime_error{"not implemented: " + std::string{__func__}};
}

std::string
PALSFrontend::getHostname() const
{
    throw std::runtime_error{"not implemented: " + std::string{__func__}};
}

std::string
PALSFrontend::getLauncherName() const
{
    throw std::runtime_error{"not implemented: " + std::string{__func__}};
}

PALSFrontend::PalsLaunchInfo
PALSFrontend::getAprunLaunchInfo(std::string const& apId)
{
    throw std::runtime_error{"not implemented: " + std::string{__func__}};
}

PALSFrontend::PalsLaunchInfo
PALSFrontend::launchApp(const char * const launcher_argv[], int stdout_fd,
        int stderr_fd, const char *inputFile, const char *chdirPath, const char * const env_list[])
{
    throw std::runtime_error{"not implemented: " + std::string{__func__}};
}

PALSFrontend::PALSFrontend()
{
    throw std::runtime_error{"not implemented: " + std::string{__func__}};
}

/* PALSApp implementation */

std::string
PALSApp::getJobId() const
{
    throw std::runtime_error{"not implemented: " + std::string{__func__}};
}

std::string
PALSApp::getLauncherHostname() const
{
    throw std::runtime_error{"not implemented: " + std::string{__func__}};
}

bool
PALSApp::isRunning() const
{
    throw std::runtime_error{"not implemented: " + std::string{__func__}};
}

size_t
PALSApp::getNumPEs() const
{
    throw std::runtime_error{"not implemented: " + std::string{__func__}};
}

size_t
PALSApp::getNumHosts() const
{
    throw std::runtime_error{"not implemented: " + std::string{__func__}};
}

std::vector<std::string>
PALSApp::getHostnameList() const
{
    throw std::runtime_error{"not implemented: " + std::string{__func__}};
}

void
PALSApp::releaseBarrier()
{
    throw std::runtime_error{"not implemented: " + std::string{__func__}};
}

void
PALSApp::kill(int signal)
{
    throw std::runtime_error{"not implemented: " + std::string{__func__}};
}

void
PALSApp::shipPackage(std::string const& tarPath) const
{
    throw std::runtime_error{"not implemented: " + std::string{__func__}};
}

void
PALSApp::startDaemon(const char* const args[])
{
    throw std::runtime_error{"not implemented: " + std::string{__func__}};
}

PALSApp::PALSApp(PALSFrontend& fe, std::string const& apId)
    : App{fe}
    , m_beDaemonSent{}
    , m_hostsPlacement{}
    , m_toolPath{}
    , m_attribsPath{}
    , m_stagePath{}
    , m_extraFiles{}
{
    throw std::runtime_error{"not implemented: " + std::string{__func__}};
}

PALSApp::PALSApp(PALSFrontend& fe, const char * const launcher_argv[], int stdout_fd,
    int stderr_fd, const char *inputFile, const char *chdirPath, const char * const env_list[])
    : App{fe}
    , m_beDaemonSent{}
    , m_hostsPlacement{}
    , m_toolPath{}
    , m_attribsPath{}
    , m_stagePath{}
    , m_extraFiles{}
{
    throw std::runtime_error{"not implemented: " + std::string{__func__}};
}

PALSApp::~PALSApp()
{}