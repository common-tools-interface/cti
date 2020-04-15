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

        // Extract tool helper ID JSON string
        static auto parseToolInfo(std::string const& toolInfoJson)
        {
            namespace pt = boost::property_tree;

            // Create stream from string source
            auto jsonSource = boost::iostreams::array_source{toolInfoJson.c_str(), toolInfoJson.size()};
            auto jsonStream = boost::iostreams::stream<boost::iostreams::array_source>{jsonSource};

            // Load and parse JSON
            auto root = pt::ptree{};
            try {
                pt::read_json(jsonStream, root);
            } catch (pt::json_parser::json_parser_error const& parse_ex) {
                throw std::runtime_error("failed to parse json: '" + toolInfoJson + "'");
            }

            // Extract tool ID
            return root.get<std::string>("toolid");
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
    // Check that PBS is installed (required for PALS)
    auto rpmArgv = cti::ManagedArgv { "rpm", "-q", "pbspro-client" };
    if (cti::Execvp{"rpm", rpmArgv.get()}.getExitStatus() != 0) {
        return false;
    }

    // Check that craycli tool is available (Shasta system)
    auto whichArgv = cti::ManagedArgv { "which", "cray" };
    if (cti::Execvp{"which", whichArgv.get()}.getExitStatus() != 0) {
        return false;
    }

    // Check that the craycli tool is properly authenticated, as we will be using its token
    auto craycliArgv = cti::ManagedArgv { "cray", "pals", "apps", "list" };
    if (cti::Execvp{"cray", craycliArgv.get()}.getExitStatus() != 0) {
        fprintf(stderr, "craycli check failed. You may need to authenticate using `cray auth login`.\n");
        return false;
    }

    return true;
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
    if (numIds != 1) {
        throw std::logic_error("expecting single apId argument to register app");
    }

    va_list idArgs;
    va_start(idArgs, numIds);

    char const* apId = va_arg(idArgs, char const*);

    va_end(idArgs);

    auto ret = m_apps.emplace(std::make_shared<PALSApp>(*this, apId));
    if (!ret.second) {
        throw std::runtime_error("Failed to create new App object.");
    }
    return *ret.first;
}

std::string
PALSFrontend::getHostname() const
{
    auto const make_addrinfo = [](std::string const& hostname) {
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
    auto const resolveHostname = [](const struct addrinfo& addr_info) {
        constexpr auto MAXADDRLEN = 15;
        // Extract IP address string
        char ip_addr[MAXADDRLEN + 1];
        if (auto const rc = getnameinfo(addr_info.ai_addr, addr_info.ai_addrlen, ip_addr, MAXADDRLEN, NULL, 0, NI_NUMERICHOST)) {
            throw std::runtime_error("getnameinfo failed: " + std::string{gai_strerror(rc)});
        }
        ip_addr[MAXADDRLEN] = '\0';
        return std::string{ip_addr};
    };

    // On Shasta, look up and return IPv4 address instead of hostname
    // UAS hostnames cannot be resolved on compute node
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
}

std::string
PALSFrontend::getLauncherName() const
{
    throw std::runtime_error{"not supported for PALS: " + std::string{__func__}};
}

PALSFrontend::PalsLaunchInfo
PALSFrontend::getPalsLaunchInfo(std::string const& apId)
{
    // Send HTTP GET request
    auto const appResult = cti::httpGetReq(getApiInfo().hostname, "/apis/pals/v1/apps" + apId,
        getApiInfo().accessToken);

    // Extract app information
    auto [resultApId, hostsPlacement] = pals::response::parseLaunchInfo(appResult);

    // Collect results
    return PalsLaunchInfo
        { .apId = std::move(resultApId)
        , .hostsPlacement = std::move(hostsPlacement)
        , .stdinFd  = ::open("/dev/null", O_RDONLY)
        , .stdoutFd = dup(STDOUT_FILENO)
        , .stderrFd = dup(STDERR_FILENO)
    };
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
    namespace pt = boost::property_tree;
    auto launchPtree = pt::ptree{};

    auto const make_array_elem = [](std::string const& value) {
        auto node = pt::ptree{};
        node.put("", value);
        return std::make_pair("", node);
    };

    { auto argvPtree = pt::ptree{};
        for (char const* const* arg = binaryArgv; *arg != nullptr; arg++) {
            argvPtree.push_back(make_array_elem(*arg));
        }
        launchPtree.add_child("argv", std::move(argvPtree));
    }

    // If no chdirPath specified, use CWD
    launchPtree.put("wdir", chdirPath ? chdirPath : cti::cstr::getcwd());

    // Read list of hostnames for PALS
    if (!nodeListSpec.empty()) {
        // User manually specified host list argument
        launchPtree.put("hosts", nodeListSpec);

    // Read each node name from node file
    } else if (auto const nodeFilePath = ::getenv("PBS_NODEFILE")) {
        auto fileStream = std::ifstream{nodeFilePath};
        auto line = std::string{};

        // Insert into hostname list
        auto hostsPtree = pt::ptree{};
        while (std::getline(fileStream, line)) {
            hostsPtree.push_back(make_array_elem(line));
        }

        launchPtree.add_child("hosts", std::move(hostsPtree));
    } else {
        throw std::runtime_error("no node list provided");
    }

    // Add parsed node count information
    if (nranks > 0) {
        launchPtree.put("nranks", std::to_string(nranks));
    }
    if (ppn > 0) {
        launchPtree.put("ppn", std::to_string(ppn));
    }
    if (depth > 0) {
        launchPtree.put("depth", std::to_string(depth));
    }

    // Add necessary environment variables
    { auto environmentPtree = pt::ptree{};

        // Add required inherited environment variables
        for (auto&& envVar : {"PATH", "USER", "LD_LIBRARY_PATH"}) {
            if (auto const envVal = ::getenv(envVar)) {
                environmentPtree.push_back(make_array_elem(envVar + std::string{"="} + envVal));
            }
        }

        // Add user-supplied environment variables
        if (env_list != nullptr) {
            for (char const* const* env_val = env_list; *env_val != nullptr; env_val++) {
                environmentPtree.push_back(make_array_elem(*env_val));
            }
        }

        launchPtree.add_child("environment", std::move(environmentPtree));
    }

    // Add default environment alias
    { auto envaliasPtree = pt::ptree{};
        envaliasPtree.put("APRUN_APP_ID", "PALS_APID");

        launchPtree.add_child("envalias", std::move(envaliasPtree));
    }

    // Encode as json string
    auto launchJsonStream = std::stringstream{};
    pt::json_parser::write_json(launchJsonStream, launchPtree);
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
    auto const launchResult = cti::httpPostJsonReq(getApiInfo().hostname, "/apis/pals/v1/apps", getApiInfo().accessToken, launchJson);
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
        , .stdinFd  = ::open(inputFile ? inputFile : "/dev/null", O_RDONLY)
        , .stdoutFd = (stdout_fd < 0) ? dup(STDOUT_FILENO) : stdout_fd
        , .stderrFd = (stderr_fd < 0) ? dup(STDERR_FILENO) : stderr_fd
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
    auto const tokenName = craycli::hostnameToName(getApiInfo().hostname);
    m_palsApiInfo.accessToken = craycli::readAccessToken(
        cti::cstr::asprintf(craycli::defaultTokenFilePattern, home_directory(), tokenName.c_str(), getApiInfo().username.c_str()));
}

/* PALSApp implementation */

std::string
PALSApp::getJobId() const
{
    return m_apId;
}

std::string
PALSApp::getLauncherHostname() const
{
    throw std::runtime_error{"not supported for PALS: " + std::string{__func__}};
}

bool
PALSApp::isRunning() const
{
    try {
        return !cti::httpGetReq(m_palsApiInfo.hostname, "/apis/pals/v1/apps/" + m_apId,
            m_palsApiInfo.accessToken).empty();
    } catch (...) {
        return false;
    }
}

std::vector<std::string>
PALSApp::getHostnameList() const
{
    std::vector<std::string> result;
    // extract hostnames from CTIHost list
    std::transform(m_hostsPlacement.begin(), m_hostsPlacement.end(), std::back_inserter(result),
        [](CTIHost const& ctiHost) { return ctiHost.hostname; });
    return result;
}

// PALS websocket callbacks

static auto constexpr WebsocketContinue = false;
static auto constexpr WebsocketComplete = true;

static int stdioInputTask(cti::WebSocketStream& webSocketStream, int stdinFd)
{
    int rc = 0;

    // Callback implementation
    auto const stdioInputCallback = [stdinFd](std::string& line) {
        // Read from FD
        char buf[8192];
        auto const bytes_read = ::read(stdinFd, buf, sizeof(buf)- 1);

        // Generate RPC input notification
        if (bytes_read > 0) {
            buf[bytes_read] = '\0';

            line = pals::rpc::generateStdinJson(buf);
            return WebsocketContinue;

        // Notify EOF
        } else {
            line = pals::rpc::generateStdinEofJson();
            return WebsocketComplete;
        }
    };

    // Wrap read relay loop to return success / failure code
    try {
        // Relay from stdinFd to provided websocket
        cti::webSocketInputTask(webSocketStream, stdioInputCallback);

    } catch (std::exception const& ex) {
        fprintf(stderr, "write loop exception: %s\n", ex.what());
        rc = -1;
        goto cleanup_stdioInputTask;
    }

cleanup_stdioInputTask:
    // Close descriptor
    ::close(stdinFd);

    return rc;
}

static int stdioOutputTask(cti::WebSocketStream& webSocketStream, int stdoutFd, int stderrFd)
{
    int rc = 0;

    // Callback implementation
    auto const stdioOutputCallback = [stdoutFd, stderrFd](char const* line) {

        // Parse stdio notification
        auto const stdioNotification = pals::response::parseStdio(line);

        // React to each notification type
        return std::visit(overload

        { [stdoutFd](pals::response::StdoutData const& stdoutData) {
                writeLoop(stdoutFd, stdoutData.content.c_str(), stdoutData.content.size() + 1);
                return WebsocketContinue;
            }

        , [stderrFd](pals::response::StderrData const& stderrData) {
                writeLoop(stderrFd, stderrData.content.c_str(), stderrData.content.size() + 1);
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

    // Wrap read relay loop to return success / failure code
    try {
        // Respond to output notifications from provided websocket
        cti::webSocketOutputTask(webSocketStream, stdioOutputCallback);

    } catch (std::exception const& ex) {
        fprintf(stderr, "write loop exception: %s\n", ex.what());
        rc = -1;
        goto cleanup_stdioOutputTask;
    }

cleanup_stdioOutputTask:
    // Close descriptors
    ::close(stdoutFd);
    ::close(stderrFd);

    return rc;
}

void
PALSApp::releaseBarrier()
{
    // Initialize websocket stream
    m_stdioStream->websocket.handshake(m_palsApiInfo.hostname, "/apis/pals/v1/apps/" + m_apId + "/stdio");

    // Request application stream mode
    pals::rpc::writeStream(m_stdioStream->websocket, m_apId);

    // Launch stdio output responder thread
    m_stdioOutputFuture = std::async(std::launch::async, stdioOutputTask,
        std::ref(m_stdioStream->websocket), m_queuedOutFd, m_queuedErrFd);

    // Request start application
    pals::rpc::writeStart(m_stdioStream->websocket, m_apId);

    // Launch stdio input generation thread
    m_stdioInputFuture = std::async(std::launch::async, stdioInputTask,
        std::ref(m_stdioStream->websocket), m_queuedInFd);

}

static constexpr auto signalJsonPattern = "{\"signum\": %d}";
void
PALSApp::kill(int signal)
{
    cti::httpPostJsonReq(m_palsApiInfo.hostname, "/apis/pals/v1/apps/" + m_apId + "/signal",
        m_palsApiInfo.accessToken, cti::cstr::asprintf(signalJsonPattern, signal));
}

static constexpr auto filesJsonPattern = "{\"name\": \"%s\", \"path\": \"%s\"}";
void
PALSApp::shipPackage(std::string const& tarPath) const
{
    auto const hostname = m_palsApiInfo.hostname;
    auto const endpointBase = "/apis/pals/v1/apps/" + m_apId + "/files";
    auto const token = m_palsApiInfo.accessToken;
    auto const fileName = cti::cstr::basename(tarPath);
    auto const endpoint = endpointBase + "?name=" + fileName;


    fprintf(stderr, "POST: %s %s\n", endpoint.c_str(), tarPath.c_str());
    auto ioc = boost::asio::io_context{};

    auto resolver = boost::asio::ip::tcp::resolver{ioc};
    auto stream = boost::beast::tcp_stream{ioc};

    auto const resolver_results = resolver.resolve(hostname, "80");

    stream.connect(resolver_results);

    auto req = boost::beast::http::request<boost::beast::http::file_body>{boost::beast::http::verb::post, endpoint, 11};
    req.set(boost::beast::http::field::host, hostname);
    req.set("Authorization", "Bearer " + token);
    req.set(boost::beast::http::field::user_agent, CTI_RELEASE_VERSION);
    req.set(boost::beast::http::field::accept, "application/json");
    req.set(boost::beast::http::field::content_type, "application/octet-stream");

    auto ec = boost::beast::error_code{};
    req.body().open(tarPath.c_str(), boost::beast::file_mode::read, ec);
    if (ec) {
        throw boost::beast::system_error{ec};
    }
    req.prepare_payload();

    boost::beast::http::write(stream, req);

    auto buffer = boost::beast::flat_buffer{};
    auto resp = boost::beast::http::response<boost::beast::http::string_body>{};

    boost::beast::http::read(stream, buffer, resp);

    auto const result = std::string{resp.body().data()};

    stream.socket().shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);

    if (ec && (ec != boost::beast::errc::not_connected)) {
        throw boost::beast::system_error{ec};
    }

    fprintf(stderr, "result '%s'\n", result.c_str());
}

void
PALSApp::startDaemon(const char* const args[])
{
    // Send daemon if not already shipped
    if (!m_beDaemonSent) {
        // shipPackage(Frontend::inst().getBEDaemonPath());
        m_beDaemonSent = true;
    }

    // Prepare to start daemon binary on compute node
    auto const remoteBEDaemonPath = m_toolPath + "/" + CTI_BE_DAEMON_BINARY;

    // Create tool launch JSON command
    namespace pt = boost::property_tree;
    auto toolLaunchPtree = pt::ptree{};

    auto const make_array_elem = [](std::string const& value) {
        auto node = pt::ptree{};
        node.put("", value);
        return std::make_pair("", node);
    };

    { auto argvPtree = pt::ptree{};
        // Add daemon path
        argvPtree.push_back(make_array_elem(remoteBEDaemonPath));

        // Add specified arguments
        for (char const* const* arg = args; *arg != nullptr; arg++) {
            argvPtree.push_back(make_array_elem(*arg));
        }

        toolLaunchPtree.add_child("argv", std::move(argvPtree));
    }

    // Encode as json string
    auto toolLaunchJsonStream = std::stringstream{};
    pt::json_parser::write_json(toolLaunchJsonStream, toolLaunchPtree);
    auto const toolLaunchJson = toolLaunchJsonStream.str();

    // Make POST request
    auto const toolInfoJson = cti::httpPostJsonReq(m_palsApiInfo.hostname,
        "/apis/pals/v1/apps/" + m_apId + "/tools", m_palsApiInfo.accessToken, toolLaunchJson);
    fprintf(stderr, "daemon response: %s\n", toolInfoJson.c_str());

    // Track tool ID
    m_toolIds.emplace_back(pals::response::parseToolInfo(toolInfoJson));
}

PALSApp::PALSApp(PALSFrontend& fe, PALSFrontend::PalsLaunchInfo&& palsLaunchInfo)
    : App{fe}
    , m_apId{std::move(palsLaunchInfo.apId)}
    , m_beDaemonSent{false}
    , m_numPEs{std::accumulate(
        palsLaunchInfo.hostsPlacement.begin(), palsLaunchInfo.hostsPlacement.end(), size_t{},
        [](size_t total, CTIHost const& ctiHost) { return total + ctiHost.numPEs; })}
    , m_hostsPlacement{std::move(palsLaunchInfo.hostsPlacement)}
    , m_palsApiInfo{fe.getApiInfo()}


    , m_toolPath{"/var/run/palsd/" + m_apId + "/files"}
    , m_attribsPath{"/tmp"}
    , m_stagePath{cti::cstr::mkdtemp(std::string{m_frontend.getCfgDir() + "/palsXXXXXX"})}
    , m_extraFiles{}

    , m_stdioStream{std::make_unique<PALSFrontend::CtiWSSImpl>(
        m_palsApiInfo.hostname, "80", m_palsApiInfo.accessToken)}
    , m_queuedInFd{palsLaunchInfo.stdinFd}
    , m_queuedOutFd{palsLaunchInfo.stdoutFd}
    , m_queuedErrFd{palsLaunchInfo.stderrFd}
    , m_stdioInputFuture{}
    , m_stdioOutputFuture{}

    , m_toolIds{}
{}

PALSApp::PALSApp(PALSFrontend& fe, std::string const& apId)
    : PALSApp{fe, fe.getPalsLaunchInfo(apId)}
{}

PALSApp::PALSApp(PALSFrontend& fe, const char * const launcher_argv[], int stdout_fd,
    int stderr_fd, const char *inputFile, const char *chdirPath, const char * const env_list[])
    : PALSApp{fe, fe.launchApp(launcher_argv, stdout_fd, stderr_fd, inputFile, chdirPath, env_list)}
{}

PALSApp::~PALSApp()
{
    // Delete application from PALS
    try {
        cti::httpDeleteReq(m_palsApiInfo.hostname, "/apis/pals/v1/apps" + m_apId, m_palsApiInfo.accessToken);
    } catch (std::exception const& ex) {
        fprintf(stderr, "warning: PALS delete failed: %s\n", ex.what());
    }

    // Check stdio task results
    if (auto const rc = m_stdioInputFuture.get()) {
        fprintf(stderr, "warning: websocket input task failed with code %d\n", rc);
    }
    if (auto const rc = m_stdioOutputFuture.get()) {
        fprintf(stderr, "warning: websocket output task failed with code %d\n", rc);
    }

    // Close stdio stream
    m_stdioStream->websocket.close(boost::beast::websocket::close_code::normal);
}