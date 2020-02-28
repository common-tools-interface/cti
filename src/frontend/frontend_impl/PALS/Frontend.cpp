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
#include <variant>

#include <sstream>
#include <fstream>

#include "transfer/Manifest.hpp"

#include "PALS/Frontend.hpp"

// Boost Strand
#include <boost/asio/io_context_strand.hpp>

// Boost JSON
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>

// Boost Beast
#include <boost/beast.hpp>

// Boost array stream
#include <boost/iostreams/device/array.hpp>
#include <boost/iostreams/stream.hpp>

#include "useful/cti_websocket.hpp"

/* helper functions */

template<class... Ts> struct overload : Ts... { using Ts::operator()...; };
template<class... Ts> overload(Ts...) -> overload<Ts...>;

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

// Cray CLI tool query functions
namespace craycli
{

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

} // namespace craycli

namespace pals
{

    namespace rpc
    {

    static constexpr auto streamRpcCallPattern = " \
        { \"jsonrpc\": \"2.0\" \
        , \"method\": \"stream\" \
        , \"params\": { \"apid\": \"%s\" } \
        , \"id\": \"%s\" \
        }";
    static void writeStream(cti::WebSocketStream& stream, std::string const& apId) {
        // Send RPC call
        auto const uuid = boost::uuids::to_string(boost::uuids::random_generator()());
        auto const rpcJson = cti::cstr::asprintf(streamRpcCallPattern, apId.c_str(), uuid.c_str());
        fprintf(stderr, "stream json: '%s'\n", rpcJson.c_str());
        stream.write(boost::asio::buffer(rpcJson));

        // TODO: Verify response
        auto const streamResponse = cti::webSocketReadString(stream);
        fprintf(stderr, "RPC stream response: '%s'\n", streamResponse.c_str());
    }

    static constexpr auto startRpcCallPattern = " \
        { \"jsonrpc\": \"2.0\" \
        , \"method\": \"start\" \
        , \"params\": { \"apid\": \"%s\" } \
        , \"id\": \"%s\" \
    }";
    static void writeStart(cti::WebSocketStream& stream, std::string const& apId) {
        // Send RPC call
        auto const uuid = boost::uuids::to_string(boost::uuids::random_generator()());
        auto const rpcJson = cti::cstr::asprintf(startRpcCallPattern, apId.c_str(), uuid.c_str());
        fprintf(stderr, "start json: '%s'\n", rpcJson.c_str());
        stream.write(boost::asio::buffer(rpcJson));

        // TODO: Verify response
        auto const startResponse = cti::webSocketReadString(stream);
        fprintf(stderr, "RPC start response: '%s'\n", startResponse.c_str());
    }

    // TODO: see if escaping is needed
    static constexpr auto stdinJsonPattern = " \
        { \"jsonrpc\": \"2.0\" \
        , \"method\": \"stdin\" \
        , \"params\": { \"content\": \"%s\" } \
        , \"id\": \"%s\" \
    }";
    static auto generateStdinJson(std::string const& content) {
        // Generate RPC call
        auto const uuid = boost::uuids::to_string(boost::uuids::random_generator()());
        return cti::cstr::asprintf(stdinJsonPattern, content.c_str(), uuid.c_str());
    }

    static constexpr auto stdinEofJsonPattern = " \
        { \"jsonrpc\": \"2.0\" \
        , \"method\": \"stdin\" \
        , \"params\": { \"eof\": true } \
        , \"id\": \"%s\" \
    }";
    static auto generateStdinEofJson() {
        // Generate RPC call
        auto const uuid = boost::uuids::to_string(boost::uuids::random_generator()());
        return cti::cstr::asprintf(stdinEofJsonPattern, uuid.c_str());
    }

    } // namespace rpc

    namespace response
    {
        // Extract and map application and node placement information from JSON string
        static auto parseLaunchInfo(std::string const& launchInfoJson)
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
            auto apId = root.get<std::string>("apid");
            auto hostsPlacement = std::vector<CTIHost>{};

            // Create list of hostnames with no PEs
            for (auto const& hostnameNodePair : root.get_child("nodes")) {
                hostsPlacement.emplace_back(CTIHost
                    { .hostname = hostnameNodePair.second.template get<std::string>("")
                    , .numPEs   = 0
                });
            }

            // Count PEs
            for (auto const& hostPlacementNodePair : root.get_child("placement")) {
                auto const nodeIdx = hostPlacementNodePair.second.template get<int>("");
                hostsPlacement[nodeIdx].numPEs++;
            }

            return std::make_tuple(apId, hostsPlacement);
        }

        struct StdoutData { std::string content; };
        struct StderrData { std::string content; };
        struct ExitData   { int rank; int status; };
        struct Complete   {};
        using StdioNotification = std::variant<StdoutData, StderrData, ExitData, Complete>;

        // Extract relevant data from stdio stream notifications
        static StdioNotification parseStdio(std::string const& stdioJson)
        {
            namespace pt = boost::property_tree;

            // Create stream from string source
            auto jsonSource = boost::iostreams::array_source{stdioJson.c_str(), stdioJson.size()};
            auto jsonStream = boost::iostreams::stream<boost::iostreams::array_source>{jsonSource};

            // Load and parse JSON
            auto root = pt::ptree{};
            try {
                pt::read_json(jsonStream, root);
            } catch (pt::json_parser::json_parser_error const& parse_ex) {
                throw std::runtime_error("failed to parse json: '" + stdioJson + "'");
            }

            // Parse based on method
            auto const method = root.get<std::string>("method");
            if (method == "stdout") {
                return StdoutData
                    { .content = root.get<std::string>("params.content")
                };

            } else if (method == "stderr") {
                return StdoutData
                    { .content = root.get<std::string>("params.content")
                };

            } else if (method == "exit") {
                return ExitData
                    { .rank = root.get<int>("params.rankid")
                    , .status = root.get<int>("params.status")
                };

            } else if (method == "complete") {
                return Complete{};
            }

            throw std::runtime_error("unknown method: " + method);
        }
    } // namespace response

} // namespace pals

/* PALSFrontend implementation */

// Forward-declared in Frontend.hpp
struct PALSFrontend::CtiWSSImpl
{
    boost::asio::io_context ioc;
    cti::WebSocketStream websocket;

    CtiWSSImpl(std::string const& hostname, std::string const& port, std::string const& accessToken)
        : ioc{}
        , websocket{cti::make_WebSocketStream(
            boost::asio::io_context::strand(ioc),
            hostname, port, accessToken)}
    {}
};

bool
PALSFrontend::isSupported()
{
    return false;
}

std::weak_ptr<App>
PALSFrontend::launchBarrier(CArgArray launcher_argv, int stdout_fd, int stderr_fd,
    CStr inputFile, CStr chdirPath, CArgArray env_list)
{
    auto ret = m_apps.emplace(std::make_shared<PALSApp>(*this,
        launcher_argv, stdout_fd, stderr_fd, inputFile, chdirPath, env_list));
    if (!ret.second) {
        throw std::runtime_error("Failed to create new App object.");
    }
    return *ret.first;
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
PALSFrontend::getPalsLaunchInfo(std::string const& apId)
{
    throw std::runtime_error{"not implemented: " + std::string{__func__}};
}

static auto parse_argv(int argc, char* const argv[])
{
    auto nranks = int{-1};
    auto ppn    = int{-1};
    auto depth  = int{-1};
    auto nodeListSpec = std::string{};

    auto incomingArgv = cti::IncomingArgv<PALSLauncherArgv>{argc, argv};
    while (true) {
        auto const [c, optarg] = incomingArgv.get_next();
        if (c < 0) {
            break;
        }

        switch (c) {

        case PALSLauncherArgv::NRanks.val:
            nranks = std::stoi(optarg);
            break;

        case PALSLauncherArgv::PPN.val:
            ppn = std::stoi(optarg);
            break;

        case PALSLauncherArgv::Depth.val:
            depth = std::stoi(optarg);
            break;

        case PALSLauncherArgv::NodeList.val:
            nodeListSpec = optarg;
            break;

        case '?':
        default:
            throw std::runtime_error("invalid launcher argument: " + std::string{(char)c});

        }
    }

    auto const binaryArgv = incomingArgv.get_rest();

    return std::make_tuple(nranks, ppn, depth, nodeListSpec, binaryArgv);
}

static auto make_launch_json(const char * const launcher_argv[], const char *chdirPath,
    const char * const env_list[])
{
    // Parse launcher_argv
    auto launcher_argc = int{0};
    while (launcher_argv[launcher_argc] != nullptr) { launcher_argc++; }
    auto const [nranks, ppn, depth, nodeListSpec, binaryArgv] = parse_argv(launcher_argc + 1, (char* const*)(launcher_argv - 1));

    // Create launch JSON command
    auto launchJsonStream = std::stringstream{};

    launchJsonStream << R"({ "argv": [)";
    for (char const* const* arg = binaryArgv; *arg != nullptr; arg++) {
        launchJsonStream << ((arg == binaryArgv) ? "\"" : ",\"") << *arg << "\"";
    }
    launchJsonStream << "]";

    // If no chdirPath specified, use CWD
    launchJsonStream <<  R"(, "wdir": ")" << (chdirPath ? chdirPath : cti::cstr::getcwd()) << "\"";

    // Read list of hostnames for PALS
    if (!nodeListSpec.empty()) {
        // TODO: determine necessary zero-padding for node numbers above 9
        launchJsonStream << R"(, "hosts": ["nid00000[)" << nodeListSpec << R"(]"])";
        // launchJsonStream << R"(, "hosts": ["nid000001","nid000002"])";
    } else if (auto const nodeFilePath = ::getenv("PBS_NODEFILE")) {
        auto fileStream = std::ifstream{nodeFilePath};
        auto line = std::string{};

        launchJsonStream << R"(, "hosts": [)";
        auto first = true;
        while (std::getline(fileStream, line)) {
            launchJsonStream << (first ? "\"" : ",\"") << line << "\"";
            first = false;
        }
        launchJsonStream << "]";
    } else {
        throw std::runtime_error("no node list provided");
    }

    // Add parsed node count information
    if (nranks > 0) {
        launchJsonStream <<  R"(, "nranks": )" << std::to_string(nranks);
    }
    if (ppn > 0) {
        launchJsonStream <<  R"(, "ppn": )" << std::to_string(ppn);
    }
    if (depth > 0) {
        launchJsonStream <<  R"(, "depth": )" << std::to_string(depth);
    }

    // Add necessary environment variables
    launchJsonStream << R"(, "environment": [)";
    auto first = true;
    for (auto&& envVar : {"PATH", "USER", "LD_LIBRARY_PATH"}) {
        if (auto const envVal = ::getenv(envVar)) {
            launchJsonStream << (first ? "\"" : ",\"") << envVar << "=" << envVal << "\"";
            first = false;
        }
    }
    // Add user-supplied environment variables
    if (env_list != nullptr) {
        for (char const* const* env_val = env_list; *env_val != nullptr; env_val++) {
            launchJsonStream << (first ? "\"" : ",\"") << *env_val << "\"";
            launchJsonStream << ",\"" << *env_val << "\"";
            first = false;
        }
    }
    launchJsonStream << "]";

    // Add default environment alias
    launchJsonStream << R"(, "envalias": { "APRUN_APP_ID": "PALS_APID" })";

    // Terminate map
    launchJsonStream << "}";
    return launchJsonStream.str();
}

PALSFrontend::PalsLaunchInfo
PALSFrontend::launchApp(const char * const launcher_argv[], int stdout_fd,
        int stderr_fd, const char *inputFile, const char *chdirPath, const char * const env_list[])
{
    // Create launch JSON from launch arguments
    auto const launchJson = make_launch_json(launcher_argv, chdirPath, env_list);
    fprintf(stderr, "launch json: '%s'\n", launchJson.c_str());

    // Send launch JSON command
    auto const launchResult = cti::postJsonReq(m_palsApiInfo.hostname, "/apis/pals/v1/apps", m_palsApiInfo.accessToken, launchJson);
    fprintf(stderr, "launch result: '%s'\n", launchResult.c_str());

    // Extract launch result information
    auto [apId, hostsPlacement] = pals::response::parseLaunchInfo(launchResult);
    fprintf(stderr, "apId: %s\n", apId.c_str());
    for (auto&& ctiHost : hostsPlacement) {
        fprintf(stderr, "host %s has %lu ranks\n", ctiHost.hostname.c_str(), ctiHost.numPEs);
    }

    // Collect results
    return PalsLaunchInfo
        { .apId = std::move(apId)
        , .hostsPlacement = std::move(hostsPlacement)
    };
}

PALSFrontend::PALSFrontend()
    : m_palsApiInfo{}
{
    // Read hostname and username from active Cray CLI configuration
    auto const activeConfig = craycli::readActiveConfig(
        cti::cstr::asprintf(craycli::defaultActiveConfigFilePattern, home_directory()));
    std::tie(m_palsApiInfo.hostname, m_palsApiInfo.username) = craycli::readHostnameUsernamePair(
        cti::cstr::asprintf(craycli::defaultConfigFilePattern, home_directory(), activeConfig.c_str()));

    // Read access token from active Cray CLI configuration
    auto const tokenName = craycli::hostnameToName(m_palsApiInfo.hostname);
    m_palsApiInfo.accessToken = craycli::readAccessToken(
        cti::cstr::asprintf(craycli::defaultTokenFilePattern, home_directory(), tokenName.c_str(), m_palsApiInfo.username.c_str()));
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


// PALS websocket callbacks

static auto constexpr WebsocketContinue = false;
static auto constexpr WebsocketComplete = true;

static auto make_stdioInputCallback()
{
    return [](std::string& line) {
        line = pals::rpc::generateStdinEofJson();
        return WebsocketComplete;
    };
}

static auto make_stdioOutputCallback()
{
    return [](char const* line) {

        // Parse stdio notification
        auto const stdioNotification = pals::response::parseStdio(line);

        // React to each notification type
        return std::visit(overload

        { [](pals::response::StdoutData const& stdoutData) {
                fprintf(stdout, stdoutData.content.c_str());
                return WebsocketContinue;
            }

        , [](pals::response::StderrData const& stderrData) {
                fprintf(stderr, stderrData.content.c_str());
                return WebsocketContinue;
            }

        , [](pals::response::ExitData const& exitData) {
                fprintf(stderr, "rank %d exited with status %d\n", exitData.rank, exitData.status);
                return WebsocketContinue;
            }

        , [](pals::response::Complete) {
                fprintf(stderr, "all ranks completed\n");
                return WebsocketComplete;
            }

        }, stdioNotification);
    };
}

PALSApp::PALSApp(PALSFrontend& fe, PALSFrontend::PalsLaunchInfo&& palsLaunchInfo)
    : App{fe}
    , m_apId{std::move(palsLaunchInfo.apId)}
    , m_beDaemonSent{}
    , m_hostsPlacement{std::move(palsLaunchInfo.hostsPlacement)}
    , m_toolPath{}
    , m_attribsPath{}
    , m_stagePath{}
    , m_extraFiles{}

    , m_stdioStream{std::make_unique<PALSFrontend::CtiWSSImpl>(
        fe.getApiInfo().hostname, "80", fe.getApiInfo().accessToken)}
    , m_stdioInputFuture{}
    , m_stdioOutputFuture{}
{
    // Initialize websocket stream
    m_stdioStream->websocket.handshake(fe.getApiInfo().hostname, "/apis/pals/v1/apps/" + m_apId + "/stdio");

    // Request application stream mode and start application
    pals::rpc::writeStream(m_stdioStream->websocket, m_apId);
    pals::rpc::writeStart(m_stdioStream->websocket, m_apId);

    // Launch stdio input generation thread
    m_stdioInputFuture = std::async(std::launch::async, [this]() {
        try {
            cti::webSocketInputTask(m_stdioStream->websocket, make_stdioInputCallback());
        } catch (std::exception const& ex) {
            fprintf(stderr, "write loop exception: %s\n", ex.what());
            return -1;
        }
        return 0;
    });

    // Launch stdio output responder thread
    m_stdioOutputFuture = std::async(std::launch::async, [this]() {
        try {
            cti::webSocketOutputTask(m_stdioStream->websocket, make_stdioOutputCallback());
        } catch (std::exception const& ex) {
            fprintf(stderr, "read loop exception: %s\n", ex.what());
            return -1;
        }
        return 0;
    });

    // Check stdio task results
    if (auto const rc = m_stdioInputFuture.get()) {
        throw std::runtime_error("websocket input task failed with code " + std::to_string(rc));
    }
    if (auto const rc = m_stdioOutputFuture.get()) {
        throw std::runtime_error("websocket print task failed with code " + std::to_string(rc));
    }

    // Close stdio stream
    m_stdioStream->websocket.close(boost::beast::websocket::close_code::normal);

    // End test
    _exit(1);
}

PALSApp::PALSApp(PALSFrontend& fe, std::string const& apId)
    : PALSApp{fe, fe.getPalsLaunchInfo(apId)}
{}

PALSApp::PALSApp(PALSFrontend& fe, const char * const launcher_argv[], int stdout_fd,
    int stderr_fd, const char *inputFile, const char *chdirPath, const char * const env_list[])
    : PALSApp{fe, fe.launchApp(launcher_argv, stdout_fd, stderr_fd, inputFile, chdirPath, env_list)}
{}

PALSApp::~PALSApp()
{}