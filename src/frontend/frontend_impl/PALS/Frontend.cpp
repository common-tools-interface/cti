/******************************************************************************\
 * Frontend.cpp - PALS specific frontend library functions.
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
#include "cti_argv_defs.hpp"

#include <memory>
#include <thread>
#include <variant>
#include <algorithm>

#include <sstream>
#include <fstream>

#include "transfer/Manifest.hpp"

#include "PALS/Frontend.hpp"

// Boost Strand
#include <boost/asio/io_context_strand.hpp>

// Boost JSON
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/algorithm/string/replace.hpp>

// Boost Beast
#include <boost/beast.hpp>

// Boost array stream
#include <boost/iostreams/device/array.hpp>
#include <boost/iostreams/stream.hpp>

#include "useful/cti_websocket.hpp"
#include "useful/cti_hostname.hpp"
#include "useful/cti_split.hpp"
#include "frontend/mpir_iface/Inferior.hpp"

namespace args
{

struct MpiexecArgv : public cti::Argv
{
    using Opt = cti::Argv::Option;
    using Par = cti::Argv::Parameter;

    static constexpr Opt Envall           { "envall",              256 };
    static constexpr Opt Envnone          { "envnone",             257 };
    static constexpr Opt Transfer         { "transfer",            258 };
    static constexpr Opt NoTransfer       { "no-transfer",         259 };
    static constexpr Opt Label            { "label",               'l' };
    static constexpr Opt NoLabel          { "no-label",            261 };
    static constexpr Opt Exclusive        { "exclusive",           262 };
    static constexpr Opt Shared           { "shared",              263 };
    static constexpr Opt LineBuffer       { "line-buffer",         264 };
    static constexpr Opt NoLineBuffer     { "no-line-buffer",      265 };
    static constexpr Opt AbortOnFailure   { "abort-on-failure",    266 };
    static constexpr Opt NoAbortOnFailure { "no-abort-on-failure", 267 };

    static constexpr Par Np           { "np",            'n' };
    static constexpr Par Ppn          { "ppn",           269 };
    static constexpr Par Soft         { "soft",          270 };
    static constexpr Par Host         { "host",          271 };
    static constexpr Par Hosts        { "hosts",         272 };
    static constexpr Par Hostlist     { "hostlist",      273 };
    static constexpr Par Hostfile     { "hostfile",      274 };
    static constexpr Par Arch         { "arch",          275 };
    static constexpr Par Wdir         { "wdir",          276 };
    static constexpr Par Path         { "path",          277 };
    static constexpr Par File         { "file",          278 };
    static constexpr Par Configfile   { "configfile",    'f' };
    static constexpr Par Umask        { "umask",         280 };
    static constexpr Par Env          { "env",           281 };
    static constexpr Par Envlist      { "envlist",       282 };
    static constexpr Par CpuBind      { "cpu-bind",      283 };
    static constexpr Par MemBind      { "mem-bind",      284 };
    static constexpr Par Depth        { "depth",         'd' };
    static constexpr Par IncludeTasks { "include-tasks", 286 };
    static constexpr Par ExcludeTasks { "exclude-tasks", 287 };
    static constexpr Par Pmi          { "pmi",           288 };
    static constexpr Par Rlimits      { "rlimits",       289 };

    static constexpr GNUOption long_options[] =
        { Envall, Envnone, Transfer, NoTransfer, Label, NoLabel, Exclusive
        , Shared, LineBuffer, NoLineBuffer, AbortOnFailure, NoAbortOnFailure

        , Np, Ppn, Soft, Host, Hosts, Hostlist, Hostfile, Arch, Wdir, Path
        , File, Configfile, Umask, Env, Envlist, CpuBind, MemBind, Depth
        , IncludeTasks, ExcludeTasks, Pmi, Rlimits

        , long_options_done
    };
};

struct AprunArgv : public cti::Argv
{
    using Opt = cti::Argv::Option;
    using Par = cti::Argv::Parameter;

    static constexpr Opt BypassAppTransfer       { "bypass-app-transfer",       'b' };
    static constexpr Opt AbortOnFailure          { "abort-on-failure",          256 };
    static constexpr Opt NoAbortOnFailure        { "no-abort-on-failure",       257 };
    static constexpr Opt StrictMemoryContainment { "strict-memory-containment", 258 };
    static constexpr Opt Ss                      { "ss",                        259 };
    static constexpr Opt SyncOutput              { "sync-output",               'T' };
    static constexpr Opt BatchArgs               { "batch-args",                'B' };
    static constexpr Opt Reconnect               { "reconnect",                 'C' };
    static constexpr Opt Relaunch                { "relaunch",                  'R' };
    static constexpr Opt ZoneSort                { "zone-sort",                 'z' };

    static constexpr Par Pes                 { "pes",                    'n' };
    static constexpr Par PesPerNode          { "pes-per-node",           'N' };
    static constexpr Par CpusPerPe           { "cpus-per-pe",            267 };
    static constexpr Par Wdir                { "wdir",                   268 };
    static constexpr Par CpuBinding          { "cpu-binding",            269 };
    static constexpr Par Cc                  { "cc",                     270 };
    static constexpr Par CpuBindingFile      { "cpu-binding-file",       271 };
    static constexpr Par Cp                  { "cp",                     272 };
    static constexpr Par AccessMode          { "access-mode",            'F' };
    static constexpr Par NodeList            { "node-list",              'L' };
    static constexpr Par NodeListFile        { "node-list-file",         275 };
    static constexpr Par ExcludeNodeList     { "exclude-node-list",      'E' };
    static constexpr Par ExcludeNodeListFile { "exclude-node-list-file", 277 };
    static constexpr Par EnvironmentOverride { "environment-override",   'e' };
    static constexpr Par MemoryPerPe         { "memory-per-pe",          'm' };
    static constexpr Par Pmi                 { "pmi",                    280 };
    static constexpr Par ProcinfoFile        { "procinfo-file",          281 };
    static constexpr Par Debug               { "debug",                  'D' };
    static constexpr Par Architecture        { "architecture",           'a' };
    static constexpr Par CpusPerCu           { "cpus-per-cu",            'j' };
    static constexpr Par MpmdEnv             { "mpmd-env",               285 };
    static constexpr Par PesPerNumaNode      { "pes-per-numa-node",      'S' };
    static constexpr Par ProtectionDomain    { "protection-domain",      'p' };
    static constexpr Par PGovernor           { "p-governor",             288 };
    static constexpr Par PState              { "p-state",                289 };
    static constexpr Par SpecializedCpus     { "specialized-cpus",       'r' };
    static constexpr Par ZoneSortSecs        { "zone-sort-secs",         'Z' };

    static constexpr Par Depth { "depth", 'd' };
    static constexpr Par Umask { "umask",  293 };

    static constexpr GNUOption long_options[] =
        { BypassAppTransfer, AbortOnFailure, NoAbortOnFailure, StrictMemoryContainment, Ss, SyncOutput, BatchArgs
        , Reconnect, Relaunch, ZoneSort
        , Pes, PesPerNode, CpusPerPe, Wdir, CpuBinding, Cc, CpuBindingFile, Cp, AccessMode
        , NodeList, NodeListFile, ExcludeNodeList, ExcludeNodeListFile, EnvironmentOverride
        , MemoryPerPe, Pmi, ProcinfoFile, Debug, Architecture, CpusPerCu, MpmdEnv
        , PesPerNumaNode, ProtectionDomain, PGovernor, PState, SpecializedCpus, ZoneSortSecs
        , long_options_done
    };
};

// Restricted argument set for subsequent MPMD commands
struct AprunMpmdArgv : public cti::Argv
{
    using Opt = cti::Argv::Option;
    using Par = cti::Argv::Parameter;

    static constexpr Par Pes   { "pes",   'n' };
    static constexpr Par Depth { "depth", 'd' };
    static constexpr Par Umask { "umask", 257 };
    static constexpr Par Wdir  { "wdir",  258 };

    static constexpr GNUOption long_options[] =
        { Pes, Depth, Umask, Wdir
        , long_options_done
    };
};

struct PalsCmdOpts
{
    std::string wdir;
    int umask, np, depth, argc;
    std::vector<std::string> argv;
};

struct PalsLaunchArgs
{
    int np, ppn;
    std::string jobid, soft, hostlist, hostfile, arch, wdir, path, file, configfile;
    int umask;
    std::vector<std::string> env;
    int nenv, envlen;
    std::string envlist;
    bool envall, transfer;
    std::string cpuBind, memBind;
    int depth;
    bool label;
    std::string includeTasks, excludeTasks;
    bool exclusive, line_buffer, abort_on_failure;
    std::string pmi, rlimits;
    int fanout, rpc_timeout;
    std::vector<PalsCmdOpts> cmds;
    std::vector<std::string> hosts;
    int mem_per_pe;
    bool strict_memory_containment;
    std::string nodeList, nodeListFile, excludeNodeList, excludeNodeListFile, envAliases, procinfoFile, nidFormat;
    std::map<std::string, std::string> envAliasMap;
};

static std::string getenv_string(char const* env_var, std::string const& default_value)
{
    if (auto const env_val = ::getenv(env_var)) {
        return std::string{env_val};
    }

    return default_value;
}

static int getenv_int(char const* env_var, int default_value)
{
    if (auto const env_val = ::getenv(env_var)) {
        if (env_val[0] != '\0') {
            try {
                return std::stoi(env_var);
            } catch (...) {
                throw std::invalid_argument("failed to parse numerical environment variable: "
                    + std::string{env_var} + " set to " + std::string{env_val});
            }
        }
    }

    return default_value;
}

static bool getenv_bool(char const* env_var, bool default_value)
{
    return static_cast<bool>(getenv_int(env_var, (default_value) ? 1 : 0));
}

// Split string on delimiter into vector
static auto split_on(std::string const& str, char delim)
{
auto result = std::vector<std::string>{};
    if (str.empty()) {
        return result;
    }

    auto ss = std::stringstream{str};
    auto tok = std::string{};
    while (std::getline(ss, tok, delim)) {
        result.emplace_back(std::move(tok));
    }

    return result;
}

// Parse numeric list in form #,#-# and add into set of IDs
static void add_rangelist_to_ids(std::set<int>& ids, std::string const& rangeList)
{
    try {
        // Split list on commas
        auto const ranges = split_on(rangeList, ',');
        for (auto&& range : ranges) {

            // Split range on hyphen
            auto const hyphen = range.find("-");

            // No hyphen, add ID
            if (hyphen == std::string::npos) {
                ids.insert(std::stoi(range));
            } else {

                // Add every ID in range to set
                auto const begin_id = std::stoi(range.substr(0, hyphen));
                auto const end_id = std::stoi(range.substr(hyphen + 1));
                for (int i = begin_id; i <= end_id; i++) {
                    ids.insert(i);
                }
            }
        }
    } catch (...) {
        throw std::runtime_error("invalid range list: " + rangeList);
    }
}

// Read file contents and add to ID set
static void add_rangelist_file_to_ids(std::set<int>& ids, std::string const& rangeListFile)
{
    // Read in rangelist file
    auto fileStream = std::ifstream{rangeListFile};
    auto line = std::string{};
    while (std::getline(fileStream, line)) {
        add_rangelist_to_ids(ids, line);
    }
}

// Intersect node list and exclude node list, use node format
// string to produce a final node list for submission
static auto generate_aprun_hostlist(
    std::string const& nodeList, std::string const& nodeListFile,
    std::string const& excludeNodeList, std::string const& excludeNodeListFile,
    std::string const& nidFormat)
{
    if (nidFormat.empty()) {
        throw std::invalid_argument("invalid nid format string: " + nidFormat);
    }

    auto result = std::vector<std::string>{};

    // Generate included node set
    auto includedNodes = std::set<int>{};
    if (!nodeList.empty()) {
        add_rangelist_to_ids(includedNodes, nodeList);
    }
    if (!nodeListFile.empty()) {
        add_rangelist_file_to_ids(includedNodes, nodeListFile);
    }

    // Generate excluded node set
    auto excludedNodes = std::set<int>{};
    if (!excludeNodeList.empty()) {
        add_rangelist_to_ids(excludedNodes, excludeNodeList);
    }
    if (!excludeNodeListFile.empty()) {
        add_rangelist_file_to_ids(excludedNodes, excludeNodeListFile);
    }

    // Generate node ID list
    auto ids = std::set<int>{};
    std::set_difference(
        includedNodes.begin(), includedNodes.end(),
        excludedNodes.begin(), excludedNodes.end(),
        std::inserter(ids, ids.end()));

    // Use node IDs to produce hostname list
    for (auto&& id : ids) {
        result.emplace_back(cti::cstr::asprintf(nidFormat.c_str(), id));
    }

    return result;
}

// Split node list string or read node list file to generate
// node list
static auto generate_mpiexec_hostlist(
    std::string const& hostList, std::string const& hostListFile)
{
    auto result = std::vector<std::string>{};

    // Use host list argument if provided
    if (!hostList.empty()) {
        auto const nodes = split_on(hostList, ',');
        result.insert(result.end(), nodes.begin(), nodes.end());
    }

    // Add hosts from file if provided
    if (!hostListFile.empty()) {

        auto fileStream = std::ifstream{hostListFile};
        auto line = std::string{};

        // Insert into hostname list
        while (std::getline(fileStream, line)) {
            result.emplace_back(std::move(line));
        }
    }

    return result;
}

static auto get_umask()
{
    auto result = mode_t{};
    ::umask(result);
    return result;
}

static std::string get_cpuBind(std::string const& cpuBind)
{
    if (cpuBind.empty() || (cpuBind == "cpu")) {
        return "thread";
    } else if (cpuBind == "depth") {
        return "depth";
    } else if (cpuBind == "numa_node") {
        return "numa";
    } else if (cpuBind == "none") {
        return "none";
    } else if (cpuBind == "core") {
        return "core";
    } else {
        char *cpu_binding = nullptr;
        if (::asprintf(&cpu_binding, "list:%s", cpuBind.c_str()) > 0) {
            auto const result = cti::take_pointer_ownership(std::move(cpu_binding), std::free);
            return std::string{result.get()};
        }
    }

    return "";
}

static std::string get_memBind(bool strict_memory_containment)
{
    if (strict_memory_containment) {
        return "local";
    }

    return "none";
}

static std::string get_rlimits(int mem_per_pe)
{
    auto result = std::string{"CORE,CPU"};

    auto send_limits = getenv_int("APRUN_XFER_LIMITS", 0);
    auto stack_limit = getenv_int("APRUN_XFER_STACK_LIMIT", 0);

    if (send_limits) {
        result += ",RSS,STACK,FSIZE,DATA,NPROC,NOFILE,"
        "MEMLOCK,AS,LOCKS,SIGPENDING,MSGQUEUE,NICE,RTPRIO";
    } else {
        if (mem_per_pe) {
            result += ",RSS";
        }
        if (stack_limit) {
            result += ",STACK";
        }
    }

    return result;
}

// Split comma-delimited envalias settings into map
static auto get_envAliasMap(std::string const& envAliases)
{
    auto result = std::map<std::string, std::string>{};

    auto const aliases = split_on(envAliases, ',');
    for (auto&& alias : aliases) {
        auto [var, val] = cti::split::string<2>(alias, ':');
        result.emplace(std::move(var), std::move(val));
    }

    return result;
}

static bool get_exclusive(std::string const& accessMode)
{
    if (accessMode.empty()) {
        throw std::runtime_error("empty access mode");
    } else if (accessMode.at(0) == 'e') {
        return true;
    } else if (accessMode.at(0) == 's') {
        return false;
    }

    throw std::runtime_error("invalid access mode: " + accessMode);
}

static void apply_mpiexec_env(PalsLaunchArgs& opts)
{
    opts.np = getenv_int("PALS_NRANKS", 1);
    opts.ppn = getenv_int("PALS_PPN", 0);
    opts.jobid = getenv_string("PBS_JOBID", "");
    opts.soft = getenv_string("PALS_SOFT", "");
    opts.hostlist = getenv_string("PALS_HOSTLIST", "");
    opts.hostfile = getenv_string("PBS_NODEFILE", "");
    if (opts.hostfile.empty()) {
        opts.hostfile = getenv_string("PALS_HOSTFILE", "");
    }
    opts.arch = "";
    opts.wdir = getenv_string("PALS_WDIR", cti::cstr::getcwd());
    opts.path = "";
    opts.file = "";
    opts.configfile = getenv_string("PALS_CONFIGFILE", "");
    opts.umask = getenv_int("PALS_UMASK", get_umask());
    umask(opts.umask);
    opts.env = {};
    opts.nenv = 0;
    opts.envlen = 0;
    opts.envall = getenv_bool("PALS_ENVALL", true);
    opts.transfer = getenv_bool("PALS_TRANSFER", true);
    opts.cpuBind = getenv_string("PALS_CPU_BIND", "");
    opts.memBind = getenv_string("PALS_MEM_BIND", "");
    opts.depth = getenv_int("PALS_DEPTH", 1);
    opts.label = getenv_bool("PALS_LABEL", false);
    opts.includeTasks = getenv_string("PALS_INCLUDE_TASKS", "");
    opts.excludeTasks = getenv_string("PALS_EXCLUDE_TASKS", "");
    opts.exclusive = getenv_bool("PALS_EXCLUSIVE", false);
    opts.line_buffer = getenv_bool("PALS_LINE_BUFFER", false);
    opts.abort_on_failure = getenv_bool("PALS_ABORT_ON_FAILURE", true);
    opts.pmi = getenv_string("PALS_PMI", "cray");
    opts.rlimits = getenv_string("PALS_RLIMITS", "CORE,CPU");
    opts.fanout = getenv_int("PALS_FANOUT", 0);
    opts.rpc_timeout = getenv_int("PALS_RPC_TIMEOUT", -1);
}

static void apply_aprun_env(PalsLaunchArgs& opts)
{
    opts.np = getenv_int("APRUN_PES", 1);
    opts.ppn = getenv_int("APRUN_PPN", 0);
    opts.jobid = getenv_string("PBS_JOBID", "");
    opts.soft = "";
    opts.hostlist = "";
    opts.hostfile = getenv_string("PBS_NODEFILE", "");
    if (opts.hostfile.empty()) {
        opts.hostfile = getenv_string("APRUN_HOSTFILE", "");
    }
    opts.arch = "";
    opts.wdir = getenv_string("APRUN_WDIR", cti::cstr::getcwd());
    opts.path = "";
    opts.file = "";
    opts.configfile = "";
    opts.umask = getenv_int("APRUN_UMASK", get_umask());
    opts.env = {};
    opts.nenv = 0;
    opts.envlen = 0;
    opts.envlist = "";
    opts.envall = getenv_bool("APRUN_ENVALL", true);
    opts.transfer = getenv_bool("APRUN_TRANSFER", true);
    opts.cpuBind = getenv_string("APRUN_CPU_BIND", "");
    opts.memBind = getenv_string("APRUN_MEM_BIND", "");
    opts.depth = getenv_int("APRUN_DEPTH", 1);
    opts.label = getenv_bool("APRUN_LABEL", false);
    opts.includeTasks = getenv_string("APRUN_INCLUDE_TASKS", "");
    opts.excludeTasks = getenv_string("APRUN_EXCLUDE_TASKS", "");
    opts.exclusive = getenv_bool("APRUN_EXCLUSIVE", false);
    opts.line_buffer = getenv_bool("APRUN_SYNC_TTY", false);
    opts.abort_on_failure = getenv_bool("APRUN_ABORT_ON_FAILURE", true);
    opts.pmi = getenv_string("APRUN_PMI", "cray");
    opts.rlimits = getenv_string("APRUN_RLIMITS", "CORE,CPU");
    opts.fanout = getenv_int("APRUN_FANOUT", 0);
    opts.rpc_timeout = getenv_int("APRUN_RPC_TIMEOUT", -1);
    opts.cmds = {};
    opts.hosts = {};
    opts.mem_per_pe = getenv_int("APRUN_DEFAULT_MEMORY", 0);
    opts.strict_memory_containment = getenv_bool("APRUN_STRICTMEM",
            false);
    opts.nodeList = getenv_string("APRUN_NODELIST", "");
    opts.nodeListFile = getenv_string("APRUN_NODELIST_FILE", "");
    opts.excludeNodeList = getenv_string("APRUN_EXCLUDE_NODELIST", "");
    opts.excludeNodeListFile = getenv_string("APRUN_EXCLUDE_NODELIST_FILE", "");
    opts.envAliases = getenv_string("APRUN_ENV_ALIAS",
        "ALPS_APP_DEPTH:PALS_DEPTH,"
        "ALPS_APP_ID:PALS_APID,"
        "ALPS_APP_PE:PALS_RANKID");
    opts.procinfoFile = getenv_string("APRUN_PROCINFO_FILE", "");
    opts.nidFormat = getenv_string("APRUN_NID_FORMAT", "n%03d");
}

// Call std::stoi, but give a better error message when processing parameters
static int parameter_stoi(std::string const& str)
{
    try {
        return std::stoi(str);
    } catch (...) {
        throw std::invalid_argument("expected numeric argument for parameter, got '" + str + "'");
    }
}

static void apply_mpiexec_args(PalsLaunchArgs& opts, int launcher_argc, char const* const* launcher_argv)
{
    // mpiexec does not support MPMD mode, all arguments are filled into first command slot
    auto const cmd_idx = int{0};
    opts.cmds.emplace_back(PalsCmdOpts{});

    auto incomingArgv = cti::IncomingArgv<args::MpiexecArgv>{launcher_argc, (char* const*)launcher_argv};
    while (true) {
        auto const [c, optarg] = incomingArgv.get_next();
        if (c < 0) {
            break;
        }

        switch (c) {

        case args::MpiexecArgv::Envall.val:
            opts.envall = true;
            break;
        case args::MpiexecArgv::Envnone.val:
            opts.envall = false;
            break;
        case args::MpiexecArgv::Transfer.val:
            opts.transfer = true;
            break;
        case args::MpiexecArgv::NoTransfer.val:
            opts.transfer = false;
            break;
        case args::MpiexecArgv::Label.val:
            opts.label = true;
            break;
        case args::MpiexecArgv::NoLabel.val:
            opts.label = false;
            break;
        case args::MpiexecArgv::Exclusive.val:
            opts.exclusive = true;
            break;
        case args::MpiexecArgv::Shared.val:
            opts.exclusive = false;
            break;
        case args::MpiexecArgv::LineBuffer.val:
            opts.line_buffer = true;
            break;
        case args::MpiexecArgv::NoLineBuffer.val:
            opts.line_buffer = false;
            break;
        case args::MpiexecArgv::AbortOnFailure.val:
            opts.abort_on_failure = true;
            break;
        case args::MpiexecArgv::NoAbortOnFailure.val:
            opts.abort_on_failure = false;
            break;

        case args::MpiexecArgv::Np.val:
            opts.cmds[cmd_idx].np = parameter_stoi(optarg);
            break;
        case args::MpiexecArgv::Ppn.val:
            opts.ppn = parameter_stoi(optarg);
            break;
        case args::MpiexecArgv::Soft.val:
            opts.soft = optarg;
            break;
        case args::MpiexecArgv::Host.val:
        case args::MpiexecArgv::Hosts.val:
        case args::MpiexecArgv::Hostlist.val:
            opts.hostlist = optarg;
            break;
        case args::MpiexecArgv::Hostfile.val:
            opts.hostfile = optarg;
            break;
        case args::MpiexecArgv::Arch.val:
            opts.arch = optarg;
            break;
        case args::MpiexecArgv::Wdir.val:
            opts.cmds[cmd_idx].wdir = optarg;
            break;
        case args::MpiexecArgv::Path.val:
            opts.path = optarg;
            break;
        case args::MpiexecArgv::File.val:
            opts.file = optarg;
            break;
        case args::MpiexecArgv::Configfile.val:
            opts.configfile = optarg;
            break;
        case args::MpiexecArgv::Umask.val:
            opts.cmds[cmd_idx].umask = parameter_stoi(optarg);
            break;
        case args::MpiexecArgv::Env.val:
            opts.env.emplace_back(optarg);
            break;
        case args::MpiexecArgv::Envlist.val:
            opts.envlist = optarg;
            break;
        case args::MpiexecArgv::CpuBind.val:
            opts.cpuBind = optarg;
            break;
        case args::MpiexecArgv::MemBind.val:
            opts.memBind = optarg;
            break;
        case args::MpiexecArgv::Depth.val:
            opts.cmds[cmd_idx].depth = parameter_stoi(optarg);
            break;
        case args::MpiexecArgv::IncludeTasks.val:
            opts.includeTasks = optarg;
            break;
        case args::MpiexecArgv::ExcludeTasks.val:
            opts.excludeTasks = optarg;
            break;
        case args::MpiexecArgv::Pmi.val:
            opts.pmi = optarg;
            break;
        case args::MpiexecArgv::Rlimits.val:
            opts.rlimits = optarg;
            break;

        case '?':
        default:
            throw std::runtime_error("invalid launcher argument: " + std::string{(char)c});

        }
    }

    // Copy rest of binary arguments into first command slot
    auto const binary_argv = incomingArgv.get_rest();
    for (auto arg = binary_argv; *arg != nullptr; arg++) {
        opts.cmds[cmd_idx].argv.emplace_back(*arg);
    }
}

static void apply_aprun_args(PalsLaunchArgs& opts, int launcher_argc, char const* const* launcher_argv)
{
    // Fill in command array by default, even in non-MPMD mode
    auto cmd_idx = int{0};
    opts.cmds.emplace_back(PalsCmdOpts{});

    auto incomingArgv = cti::IncomingArgv<args::AprunArgv>{launcher_argc, (char* const*)launcher_argv};
    while (true) {
        auto const [c, optarg] = incomingArgv.get_next();
        if (c < 0) {
            break;
        }

        switch (c) {

        // Options

        case args::AprunArgv::BypassAppTransfer.val:
            opts.transfer = false;
            break;
        case args::AprunArgv::AbortOnFailure.val:
            opts.abort_on_failure = true;
            break;
        case args::AprunArgv::NoAbortOnFailure.val:
            opts.abort_on_failure = false;
            break;
        case args::AprunArgv::StrictMemoryContainment.val:
        case args::AprunArgv::Ss.val:
            opts.strict_memory_containment = true;
            break;
        case args::AprunArgv::SyncOutput.val:
            opts.line_buffer = true;
            break;

        // Ignored by mpiexec
        case args::AprunArgv::BatchArgs.val:
        case args::AprunArgv::Reconnect.val:
        case args::AprunArgv::Relaunch.val:
        case args::AprunArgv::ZoneSort.val:
            break;

        // Parameters

        case args::AprunArgv::Pes.val:
            opts.cmds[cmd_idx].np = parameter_stoi(optarg);
            break;
        case args::AprunArgv::PesPerNode.val:
            opts.ppn = parameter_stoi(optarg);
            break;
        case args::AprunArgv::CpusPerPe.val:
            opts.cmds[cmd_idx].depth = parameter_stoi(optarg);
            break;
        case args::AprunArgv::Wdir.val:
            opts.cmds[cmd_idx].wdir = optarg;
            break;
        case args::AprunArgv::CpuBinding.val:
        case args::AprunArgv::Cc.val:
            opts.cpuBind = args::get_cpuBind(optarg);
            break;
        case args::AprunArgv::CpuBindingFile.val:
        case args::AprunArgv::Cp.val:
            // Ignored by mpiexec
            break;
        case args::AprunArgv::AccessMode.val:
            opts.exclusive = args::get_exclusive(optarg);
            break;
        case args::AprunArgv::NodeList.val:
            opts.nodeList = optarg;
            break;
        case args::AprunArgv::NodeListFile.val:
            opts.nodeListFile = optarg;
            break;
        case args::AprunArgv::ExcludeNodeList.val:
            opts.excludeNodeList = optarg;
            break;
        case args::AprunArgv::ExcludeNodeListFile.val:
            opts.excludeNodeListFile = optarg;
            break;
        case args::AprunArgv::EnvironmentOverride.val:
            opts.env.emplace_back(optarg);
            break;
        case args::AprunArgv::MemoryPerPe.val:
            opts.mem_per_pe = parameter_stoi(optarg);
            break;
        case args::AprunArgv::Pmi.val:
            opts.pmi = optarg;
            break;
        case args::AprunArgv::ProcinfoFile.val:
            opts.procinfoFile = optarg;
            break;

        case args::AprunArgv::Depth.val:
            opts.cmds[cmd_idx].depth = parameter_stoi(optarg);
            break;
        case args::AprunArgv::Umask.val:
            opts.cmds[cmd_idx].umask = parameter_stoi(optarg);
            break;

        // Ignored by mpiexec
        case args::AprunArgv::Architecture.val:
        case args::AprunArgv::CpusPerCu.val:
        case args::AprunArgv::Debug.val:
        case args::AprunArgv::MpmdEnv.val:
        case args::AprunArgv::PesPerNumaNode.val:
        case args::AprunArgv::ProtectionDomain.val:
        case args::AprunArgv::PGovernor.val:
        case args::AprunArgv::PState.val:
        case args::AprunArgv::SpecializedCpus.val:
        case args::AprunArgv::ZoneSortSecs.val:
            break;

        case '?':
        default:
            throw std::runtime_error("invalid launcher argument: " + std::string{(char)c});

        }
    }

    // Add binary arguments, loop on MPMD parsing
    auto binary_argv = incomingArgv.get_rest();
    auto binary_argc = incomingArgv.get_rest_argc();
    while (true) {

        // Stop on end of binary arguments
        if (*binary_argv == nullptr) {
            break;
        }

        // Copy argument if not an MPMD separator
        if (strcmp(*binary_argv, ":") != 0) {
            opts.cmds[cmd_idx].argv.emplace_back(*binary_argv);

            binary_argv++;
            binary_argc--;

        // Otherwise, begin MPMD parsing
        } else {

            // Exit if trailing :
            if (*(binary_argv + 1) == nullptr) {
                break;
            }

            // Increment MPMD command index
            cmd_idx++;
            opts.cmds.emplace_back(PalsCmdOpts{});

            // Parse limited MPMD flags
            auto mpmdArgv = cti::IncomingArgv<args::AprunMpmdArgv>{binary_argc, (char* const*)binary_argv};

            while (true) {
                auto const [c, optarg] = mpmdArgv.get_next();
                if (c < 0) {
                    break;
                }

                switch (c) {

                case args::AprunMpmdArgv::Pes.val:
                    opts.cmds[cmd_idx].np = parameter_stoi(optarg);
                    break;
                case args::AprunMpmdArgv::Wdir.val:
                    opts.cmds[cmd_idx].wdir = optarg;
                    break;
                case args::AprunArgv::Depth.val:
                    opts.cmds[cmd_idx].depth = parameter_stoi(optarg);
                    break;
                case args::AprunArgv::Umask.val:
                    opts.cmds[cmd_idx].umask = parameter_stoi(optarg);
                    break;

                case '?':
                default:
                    throw std::runtime_error("invalid launcher MPMD argument: " + std::string{(char)c});

                }
            }

            // Advance binary_argv to next
            binary_argv = mpmdArgv.get_rest();
            binary_argc = mpmdArgv.get_rest_argc();
        }
    }
}

enum class ParseStyle
    {   Mpiexec
    ,   Aprun
};

// Select proper argument-parsing function and build PalsLauncherArgs struct
static auto parse_launcher_args(ParseStyle const& parseStyle, char const* const* launcher_args)
{
    auto palsLaunchArgs = args::PalsLaunchArgs{};

    // Count number of arguments
    auto launcher_argc = int{0};
    while (launcher_args[launcher_argc] != nullptr) { launcher_argc++; }

    // Make new argv array with an argv[0] for getopt
    launcher_argc++;
    char const* launcher_argv[launcher_argc + 1];
    launcher_argv[0] = "launcher";
    for (int i = 0; i < launcher_argc; i++) {
        launcher_argv[i + 1] = launcher_args[i];
    }
    launcher_argv[launcher_argc] = nullptr;

    // Tool may have specified how arguments are to be interpreted
    if (parseStyle == ParseStyle::Aprun) {

        // Set name of launcher style
        launcher_argv[0] = "aprun";

        // Parse aprun-compatible environment variables
        args::apply_aprun_env(palsLaunchArgs);

        // Skip --aprun flag in parsing
        args::apply_aprun_args(palsLaunchArgs, launcher_argc, launcher_argv);

    } else if (parseStyle == ParseStyle::Mpiexec) {

        // Set name of launcher style
        launcher_argv[0] = "mpiexec";

        // Parse mpiexec-compatible environment variables
        args::apply_mpiexec_env(palsLaunchArgs);

        // Skip --mpiexec flag in parsing
        args::apply_mpiexec_args(palsLaunchArgs, launcher_argc, launcher_argv);

    }

    return palsLaunchArgs;
}

} // args

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

    // Parse the configuration file
    auto fileStream = std::ifstream{configFilePath};
    auto line = std::string{};
    while (std::getline(fileStream, line)) {

        // Extract hostname
        if (line.substr(0, 20) == "hostname = \"https://") {
            hostname = line.substr(20, line.length() - 21);

        // Extract username
        } else if (line.substr(0, 12) == "username = \"") {
            username = line.substr(12, line.length() - 13);
        }
    }

    if (hostname.empty() || username.empty()) {
        throw std::runtime_error("failed to read hostname and username from " + configFilePath);
    }

    return std::make_pair(hostname, username);
}

static auto formatTokenName(std::string hostname, std::string username)
{
    // Hostname into token name
    // See `hostname_to_name` in https://stash.us.cray.com/projects/CLOUD/repos/craycli/browse/cray/utils.py
    // Extract hostname from URL
    hostname = hostname.substr(0, hostname.find("/"));

    // Replace - and . with _
    std::replace(hostname.begin(), hostname.end(), '-', '_');
    std::replace(hostname.begin(), hostname.end(), '.', '_');

    // Process username into token name, replace - and . with _
    // See `set_name` in https://stash.us.cray.com/projects/CLOUD/repos/craycli/browse/cray/auth.py
    std::replace(username.begin(), username.end(), '.', '_');

    return hostname + "." + username;
}

// Load token from disk
static constexpr auto defaultTokenFilePattern = "%s/.config/cray/tokens/%s";
static auto readAccessToken(std::string const& tokenPath)
{
    namespace pt = boost::property_tree;

    // If PALS debug mode is enabled, API server will accept any token
    if (::getenv(PALS_DEBUG)) {
        return std::string{"PALS_DEBUG_MODE"};
    }

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
        stream.write(boost::asio::buffer(rpcJson));

        // TODO: Verify response
        auto const streamResponse = cti::webSocketReadString(stream);
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
        stream.write(boost::asio::buffer(rpcJson));

        // TODO: Verify response
        auto const startResponse = cti::webSocketReadString(stream);
    }

    static auto generateStdinJson(std::string const& content) {
        namespace pt = boost::property_tree;

        // Create JSON
        auto const uuid = boost::uuids::to_string(boost::uuids::random_generator()());
        auto rpcPtree = pt::ptree{};
        rpcPtree.put("jsonrpc", "2.0");
        rpcPtree.put("method", "stdin");
        { auto paramsPtree = pt::ptree{};

            // Content will be properly escaped
            paramsPtree.put("content", content);
            rpcPtree.add_child("params", std::move(paramsPtree));
        }
        rpcPtree.put("id", uuid);

        // Encode as json string
        auto rpcJsonStream = std::stringstream{};
        pt::json_parser::write_json(rpcJsonStream, rpcPtree);
        return rpcJsonStream.str();
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
        // If JSON response contains error, throw
        static auto checkErrorJson(boost::property_tree::ptree const& root)
        {
            if (auto const errorPtree = root.get_child_optional("error")) {
                auto const errorCode = errorPtree->get_optional<std::string>("code");
                auto const errorMessage = errorPtree->get_optional<std::string>("message");

                // Report error
                if (errorCode && errorMessage) {
                    throw std::runtime_error(*errorMessage + " (code " + *errorCode + ")");
                } else {
                    throw std::runtime_error("malformed error response");
                }
            }
        }

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

            // Check for error
            checkErrorJson(root);

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

            // Fill in application binary paths
            auto binaryPaths = std::vector<std::string>{};
            for (auto const& commandInfoPair : root.get_child("cmds")) {
                auto const commandInfo = commandInfoPair.second;
                binaryPaths.emplace_back(commandInfo.get_child("argv").begin()->second.template get<std::string>(""));
            }
            if (binaryPaths.empty()) {
                binaryPaths.emplace_back(root.get_child("argv").begin()->second.template get<std::string>(""));
            }

            // Fill in MPMD rank map
            auto binaryRankMap = std::map<std::string, std::vector<int>>{};
            size_t rank = 0;
            for (auto const& commandIdxPair : root.get_child("cmdidxs")) {
                auto const commandIdx = commandIdxPair.second.template get<size_t>("");
                if (commandIdx >= binaryPaths.size()) {
                    throw std::runtime_error("invalid command index: " + std::to_string(commandIdx));
                }
                binaryRankMap[binaryPaths[commandIdx]].push_back(rank);
                rank++;
            }

            return std::make_tuple(apId, std::move(hostsPlacement), std::move(binaryRankMap));
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

            // Check for error
            checkErrorJson(root);

            // Extract tool ID
            return root.get<std::string>("toolid");
        }

        struct StdoutData { std::string content; };
        struct StderrData { std::string content; };
        struct ExitData   { int rank; int status; };
        struct Complete   {};
        struct AcknowledgementData { std::string id; };
        using StdioNotification = std::variant<StdoutData, StderrData, ExitData, Complete, AcknowledgementData>;

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

            // Check for error
            checkErrorJson(root);

            // Parse based on method
            auto const method = root.get_optional<std::string>("method");
            if (!method) {
                // Check for acknowledgement
                if (auto const result = root.get_optional<std::string>("result")) {
                    if (*result != "null") {
                        throw std::runtime_error("request failed: " + *result);
                    }

                    return AcknowledgementData
                        { .id = root.get<std::string>("id")
                    };
                }

                // Message was not an acknowledgement
                throw std::runtime_error("stdio failed: no method found in malformed response '" + stdioJson + "'");
            }
            if (*method == "stdout") {
                return StdoutData
                    { .content = root.get<std::string>("params.content")
                };

            } else if (*method == "stderr") {
                return StdoutData
                    { .content = root.get<std::string>("params.content")
                };

            } else if (*method == "exit") {
                return ExitData
                    { .rank = root.get<int>("params.rankid")
                    , .status = root.get<int>("params.status")
                };

            } else if (*method == "complete") {
                return Complete{};
            }

            throw std::runtime_error("unknown method: " + *method);
        }
    } // namespace response

} // namespace pals

/* PALSFrontend implementation */

// Forward-declared in Frontend.hpp
struct PALSFrontend::CtiWSSImpl
{
    boost::asio::io_context ioc;
    boost::asio::ssl::context ssl_ctx;
    cti::WebSocketStream websocket;

    CtiWSSImpl(std::string const& hostname, std::string const& port, std::string const& accessToken)
        : ioc{}
        , ssl_ctx{boost::asio::ssl::context::tlsv12_client}
        , websocket{cti::make_WebSocketStream(
            boost::asio::io_context::strand(ioc), ssl_ctx,
            hostname, port, accessToken)}
    {}
};

std::weak_ptr<App>
PALSFrontend::launch(CArgArray launcher_argv, int stdout_fd, int stderr_fd,
    CStr inputFile, CStr chdirPath, CArgArray env_list)
{
    auto ret = m_apps.emplace(std::make_shared<PALSApp>(*this,
        launchApp(launcher_argv, stdout_fd, stderr_fd, inputFile, chdirPath, env_list,
        LaunchBarrierMode::Disabled)));
    if (!ret.second) {
        throw std::runtime_error("Failed to create new App object.");
    }
    return *ret.first;
}

std::weak_ptr<App>
PALSFrontend::launchBarrier(CArgArray launcher_argv, int stdout_fd, int stderr_fd,
    CStr inputFile, CStr chdirPath, CArgArray env_list)
{
    auto ret = m_apps.emplace(std::make_shared<PALSApp>(*this,
        launchApp(launcher_argv, stdout_fd, stderr_fd, inputFile, chdirPath, env_list,
        LaunchBarrierMode::Enabled)));
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

    auto ret = m_apps.emplace(std::make_shared<PALSApp>(*this, getPalsLaunchInfo(apId)));
    if (!ret.second) {
        throw std::runtime_error("Failed to create new App object.");
    }
    return *ret.first;
}

std::string
PALSFrontend::getHostname() const
{
    // Delegate to shared implementation supporting both XC and Shasta
    return cti::detectFrontendHostname();
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
    auto const appResult = cti::httpGetReq(
        getApiInfo().hostname,
        getApiInfo().endpointBase + "v1/apps/" + apId,
        getApiInfo().accessToken);

    // Extract app information
    auto [resultApId, hostsPlacement, binaryRankMap] = pals::response::parseLaunchInfo(appResult);

    // Collect results
    return PalsLaunchInfo
        { .apId = std::move(resultApId)
        , .hostsPlacement = std::move(hostsPlacement)
        , .binaryRankMap = std::move(binaryRankMap)
        , .stdinFd  = ::open("/dev/null", O_RDONLY)
        , .stdoutFd = dup(STDOUT_FILENO)
        , .stderrFd = dup(STDERR_FILENO)
        , .started = true
        , .atBarrier = false
    };
}

static auto make_launch_json(args::ParseStyle const& parseStyle,
    const char * const launcher_args[], const char *chdirPath,
    const char * const env_list[], PALSFrontend::LaunchBarrierMode const launchBarrierMode)
{
    // If the raw launcher request JSON was supplied as an argument, use that
    if (strcmp(launcher_args[0], "--json-file") == 0) {

        if (launcher_args[1] == nullptr) {
            throw std::runtime_error("expected launcher arguments in form `--json-file <path>`, but got only `--json-file`");
        }

        auto jsonFileStream = std::ifstream(launcher_args[1]);
        auto rawRequest = std::string{};

        // Seek to end of file and reserve string space
        jsonFileStream.seekg(0, std::ios::end);
        rawRequest.reserve(jsonFileStream.tellg());
        jsonFileStream.seekg(0, std::ios::beg);

        // Read file into string
        rawRequest.assign((std::istreambuf_iterator<char>(jsonFileStream)),
            std::istreambuf_iterator<char>());

        return rawRequest;
    }

    // Disable argument reordering
    auto const posixly_correct = (::getenv("POSIXLY_CORRECT") != nullptr);
    if (!posixly_correct) {
        ::setenv("POSIXLY_CORRECT", "1", 0);
    }

    // Parse launcher_argv (does not include argv[0])
    auto opts = args::parse_launcher_args(parseStyle, launcher_args);

    // Restore argument reordering
    if (!posixly_correct) {
        ::unsetenv("POSIXLY_CORRECT");
    }

    // Process string arguments to fill in more request data

    // aprun-style hostlist generation
    if ((!opts.nodeList.empty() || !opts.nodeListFile.empty())
     && !opts.nidFormat.empty()) {
        opts.hosts = args::generate_aprun_hostlist(
            opts.nodeList, opts.nodeListFile,
            opts.excludeNodeList, opts.excludeNodeListFile,
            opts.nidFormat);

    // mpiexec-style hostlist generation
    } else if (!opts.hostlist.empty() || !opts.hostfile.empty()) {
        opts.hosts = args::generate_mpiexec_hostlist(
            opts.hostlist, opts.hostfile);
    }

    opts.memBind = args::get_memBind(
        opts.strict_memory_containment);
    opts.rlimits = args::get_rlimits(opts.mem_per_pe);
    if (!opts.envlist.empty()) {
        opts.env = args::split_on(opts.envlist, ',');
    }
    opts.envAliasMap = args::get_envAliasMap(opts.envAliases);

    // Apply job wdir / umask options to unset cmd options
    if (chdirPath != nullptr) {
        opts.wdir = chdirPath;
    }
    for (auto&& cmd : opts.cmds) {
        if (cmd.wdir.empty()) {
            cmd.wdir = opts.wdir;
        }
        if (cmd.umask == 0) {
            cmd.umask = opts.umask;
        }
        if (cmd.depth == 0) {
            cmd.depth = opts.depth;
        }
    }

    // Create launch JSON command
    namespace pt = boost::property_tree;
    auto launchPtree = pt::ptree{};

    // Ptree formats all booleans / integers as strings in JSON,
    // so need to post-process the JSON
    auto integerReplacements = std::map<std::string, int>{};
    auto booleanReplacements = std::map<std::string, bool>{};

    auto const make_array_elem = [](std::string const& value) {
        auto node = pt::ptree{};
        node.put("", value);
        return std::make_pair("", node);
    };

    // Read list of hostnames for PALS
    if (!opts.hosts.empty()) {
        auto hostsPtree = pt::ptree{};

        for (auto&& node : opts.hosts) {
            hostsPtree.push_back(make_array_elem(node));
        }

        // Insert hosts node into request
        launchPtree.add_child("hosts", std::move(hostsPtree));

    } else {
        throw std::runtime_error("no node list provided");
    }

    // Add launcher rank information
    if (opts.np > 0) {
        integerReplacements["%%nranks"] = opts.np;
        launchPtree.put("nranks", "%%nranks");
    }
    if (opts.ppn > 0) {
        integerReplacements["%%ppn"] = opts.ppn;
        launchPtree.put("ppn", "%%ppn");
    }

    // Add necessary environment variables
    { auto environmentPtree = pt::ptree{};

        // If inheriting environment is enabled, add all environment variables
        if (opts.envall) {
            for (auto env_var = environ; *env_var != nullptr; env_var++) {
                environmentPtree.push_back(make_array_elem(*env_var));
            }
        }

        // Add required inherited environment variables
        for (auto&& envVar : {"PATH", "USER", "LD_LIBRARY_PATH"}) {
            if (auto const envVal = ::getenv(envVar)) {
                environmentPtree.push_back(make_array_elem(envVar + std::string{"="} + envVal));
            }
        }

        // If barrier is enabled, add barrier environment variable
        if (launchBarrierMode == PALSFrontend::LaunchBarrierMode::Enabled) {
            environmentPtree.push_back(make_array_elem("PALS_STARTUP_BARRIER=1"));
        }

        // Add environment variables from options
        for (auto&& envVar : opts.env) {
            environmentPtree.push_back(make_array_elem(envVar));
        }

        // Add user-supplied environment variables
        if (env_list != nullptr) {
            for (char const* const* env_val = env_list; *env_val != nullptr; env_val++) {
                environmentPtree.push_back(make_array_elem(*env_val));
            }
        }

        launchPtree.add_child("environment", std::move(environmentPtree));
    }

    // Add user information
    if (opts.umask > 0) {
        integerReplacements["%%umask"] = opts.umask;
        launchPtree.put("umask", "%%umask");
    }

    // Add environment aliases
    { auto envaliasPtree = pt::ptree{};
        // Default environment alias
        envaliasPtree.put("APRUN_APP_ID", "PALS_APID");

        for (auto&& [var, val] : opts.envAliasMap) {
            envaliasPtree.put(var, val);
        }

        launchPtree.add_child("envalias", std::move(envaliasPtree));
    }

    // Add fanout and CPU bind information
    if (opts.fanout > 0) {
        integerReplacements["%%fanout"] = opts.fanout;
        launchPtree.put("fanout", "%%fanout");
    }
    if (!opts.cpuBind.empty()) {
        launchPtree.put("cpubind", opts.cpuBind);
    }
    if (!opts.memBind.empty()) {
        launchPtree.put("membind", opts.memBind);
    }
    if (!opts.pmi.empty()) {
        launchPtree.put("pmi", opts.pmi);
    }

    // Add job exclusivity and buffering settings
    booleanReplacements["%%exclusive"] = opts.exclusive;
    launchPtree.put("exclusive", "%%exclusive");
    booleanReplacements["%%line_buffered"] = opts.line_buffer;
    launchPtree.put("line_buffered", "%%line_buffered");
    booleanReplacements["%%abort_on_failure"] = opts.abort_on_failure;
    launchPtree.put("abort_on_failure", "%%abort_on_failure");

    // Add command arguments
    { auto cmdsPtree = pt::ptree{};

        // Add command for each index
        int cmd_idx = 0;
        for (auto&& cmd : opts.cmds) {

            auto cmdPtree = pt::ptree{};

            // Verify that binary exists
            (void)cti::findPath(cmd.argv[0]);

            // Add argument array
            { auto argvPtree = pt::ptree{};
                for (auto&& arg : cmd.argv) {
                    argvPtree.push_back(make_array_elem(arg));
                }

                cmdPtree.add_child("argv", std::move(argvPtree));
            }

            // Add working directory
            cmdPtree.put("wdir", cmd.wdir);

            // Add umask, nranks, depth
            auto const replacementPrefix = "%%cmd" + std::to_string(cmd_idx);
            integerReplacements[replacementPrefix + "umask"] = cmd.umask;
            cmdPtree.put("umask", replacementPrefix + "umask");
            integerReplacements[replacementPrefix + "nranks"] = cmd.np;
            cmdPtree.put("nranks", replacementPrefix + "nranks");
            integerReplacements[replacementPrefix + "depth"] = cmd.depth;
            cmdPtree.put("depth", replacementPrefix + "depth");

            // Add to cmd list
            cmdsPtree.push_back(std::make_pair("", std::move(cmdPtree)));

            cmd_idx++;
        }

        launchPtree.add_child("cmds", std::move(cmdsPtree));
    }

    // Add resource limits
    launchPtree.put("rlimits", opts.rlimits);

    // Encode as json string
    auto launchJsonStream = std::stringstream{};
    pt::json_parser::write_json(launchJsonStream, launchPtree);
    auto launchJson = launchJsonStream.str();

    // Replace placeholder values with integers
    for (auto const& keyValuePair : integerReplacements) {
        boost::replace_all(launchJson, "\"" + keyValuePair.first + "\"",
            std::to_string(keyValuePair.second));
    }
    for (auto const& keyValuePair : booleanReplacements) {
        boost::replace_all(launchJson, "\"" + keyValuePair.first + "\"",
            keyValuePair.second ? "true" : "false");
    }

    return launchJson;
}

PALSFrontend::PalsLaunchInfo
PALSFrontend::launchApp(const char * const launcher_argv[], int stdout_fd,
        int stderr_fd, const char *inputFile, const char *chdirPath, const char * const env_list[],
        LaunchBarrierMode const launchBarrierMode)
{
    // TODO: create variants of launchApp with different argument parsing styles.
    // For now, parse arguments as set by CTI_LAUNCHER_NAME, with default being mpiexec-style
    auto parseStyle = args::ParseStyle::Mpiexec;
    if (auto const launcher_name = ::getenv(CTI_LAUNCHER_NAME_ENV_VAR)) {
        if (strcmp(launcher_name, "aprun") == 0) {
            parseStyle = args::ParseStyle::Aprun;
        }
    }

    // Create launch JSON from launch arguments
    auto const launchJson = make_launch_json(parseStyle, launcher_argv, chdirPath, env_list, launchBarrierMode);
    writeLog("launch json: '%s'\n", launchJson.c_str());

    // Send launch JSON command
    auto const launchResult = cti::httpPostJsonReq(
        getApiInfo().hostname,
        getApiInfo().endpointBase + "v1/apps",
        getApiInfo().accessToken,
        launchJson);
    writeLog("launch result: '%s'\n", launchResult.c_str());

    // Extract launch result information
    auto [apId, hostsPlacement, binaryRankMap] = pals::response::parseLaunchInfo(launchResult);
    writeLog("apId: %s\n", apId.c_str());
    for (auto&& ctiHost : hostsPlacement) {
        writeLog("host %s has %lu ranks\n", ctiHost.hostname.c_str(), ctiHost.numPEs);
    }

    // Collect results
    return PalsLaunchInfo
        { .apId = std::move(apId)
        , .hostsPlacement = std::move(hostsPlacement)
        , .binaryRankMap = std::move(binaryRankMap)
        , .stdinFd  = ::open(inputFile ? inputFile : "/dev/null", O_RDONLY)
        , .stdoutFd = (stdout_fd < 0) ? dup(STDOUT_FILENO) : stdout_fd
        , .stderrFd = (stderr_fd < 0) ? dup(STDERR_FILENO) : stderr_fd
        , .started = false
        , .atBarrier = (launchBarrierMode == LaunchBarrierMode::Enabled)
    };
}

std::string
PALSFrontend::getApid(pid_t craycliPid) const
{
    // Get path to craycli binary for ptrace attach
    auto const procExePath = "/proc/" + std::to_string(craycliPid) + "/exe";
    auto const craycliPath = cti::cstr::readlink(procExePath);

    // Attach to craycli process
    auto craycliInferior = Inferior{craycliPath, craycliPid};

    // Get address of "totalview_jobid"
    auto jobidAddress = craycliInferior.readVariable<Inferior::Address>("totalview_jobid");

    // Read string variable value
    auto result = std::string{};
    while (auto c = craycliInferior.readMemory<char>(jobidAddress++)) {
        result.push_back(c);
    }

    // Process is detached upon destruction of craycliInferior

    return result;
}

PALSFrontend::PALSFrontend()
    : m_palsApiInfo{}
{
    // Read hostname and username from active Cray CLI configuration
    auto const activeConfig = craycli::readActiveConfig(
        cti::cstr::asprintf(craycli::defaultActiveConfigFilePattern, home_directory()));

    // If PALS debug mode is enabled, use local API server with root user
    if (::getenv(PALS_DEBUG)) {
        m_palsApiInfo.hostname = "127.0.0.1";
        m_palsApiInfo.username = "root";
        m_palsApiInfo.endpointBase = "/";
        m_palsApiInfo.accessToken = "";

    // Otherwise, use configured API gateway and user, Shasta endpoint base
    } else {
        std::tie(m_palsApiInfo.hostname, m_palsApiInfo.username) = craycli::readHostnameUsernamePair(
            cti::cstr::asprintf(craycli::defaultConfigFilePattern, home_directory(), activeConfig.c_str()));

        // Read access token from active Cray CLI configuration
        auto const tokenName = craycli::formatTokenName(getApiInfo().hostname, getApiInfo().username);
        m_palsApiInfo.accessToken = craycli::readAccessToken(
            cti::cstr::asprintf(craycli::defaultTokenFilePattern, home_directory(), tokenName.c_str()));

        // Default Shasta endpoint base using gateway server
        m_palsApiInfo.endpointBase = "/apis/pals/";
     }

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
        return !cti::httpGetReq(
            m_palsApiInfo.hostname,
            m_palsApiInfo.endpointBase + "v1/apps/" + m_apId,
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

std::map<std::string, std::vector<int>>
PALSApp::getBinaryRankMap() const
{
    return m_binaryRankMap;
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
        fprintf(stderr, "stdio input loop exception: %s\n", ex.what());
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
                fdWriteLoop(stdoutFd, stdoutData.content.c_str(), stdoutData.content.size() + 1);
                return WebsocketContinue;
            }

        , [stderrFd](pals::response::StderrData const& stderrData) {
                fdWriteLoop(stderrFd, stderrData.content.c_str(), stderrData.content.size() + 1);
                return WebsocketContinue;
            }

        , [](pals::response::ExitData const& exitData) {
                return WebsocketContinue;
            }

        , [](pals::response::Complete) {
                return WebsocketComplete;
            }

        , [](pals::response::AcknowledgementData const& /* unused */) {
                return WebsocketContinue;
            }

        }, stdioNotification);
    };

    // Wrap read relay loop to return success / failure code
    try {
        // Respond to output notifications from provided websocket
        cti::webSocketOutputTask(webSocketStream, stdioOutputCallback);

    } catch (std::exception const& ex) {
        fprintf(stderr, "stdio output loop exception: %s\n", ex.what());
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
    if (!m_atBarrier) {
        throw std::runtime_error("application is not at startup barrier");
    }

    // Send PALS the barrier release signal
    kill(SIGCONT);

    m_atBarrier = false;
}

static constexpr auto signalJsonPattern = "{\"signum\": %d}";
void
PALSApp::kill(int signal)
{
    try {

        cti::httpPostJsonReq(
            m_palsApiInfo.hostname,
            m_palsApiInfo.endpointBase + "v1/apps/" + m_apId + "/signal",
            m_palsApiInfo.accessToken,
            cti::cstr::asprintf(signalJsonPattern, signal));

    } catch (std::runtime_error const& ex) {
        // Ignore exception if application has already exited
        writeLog("warning: failed to kill application %s: %s\n", m_apId.c_str(), ex.what());
    }
}

void
PALSApp::shipPackage(std::string const& tarPath) const
{
    // Ship tarpath without changing its name on the backend
    shipPackage(tarPath, cti::cstr::basename(tarPath));
}

static constexpr auto filesJsonPattern = "{\"name\": \"%s\", \"path\": \"%s\"}";
void
PALSApp::shipPackage(std::string const& tarPath, std::string const& remoteName) const
{
    writeLog("shipPackage POST: %s -> %s\n", tarPath.c_str(), remoteName.c_str());
    auto const result = cti::httpPostFileReq(
        m_palsApiInfo.hostname,
        m_palsApiInfo.endpointBase + "v1/apps/" + m_apId + "/files?name=" + remoteName,
        m_palsApiInfo.accessToken,
        tarPath);

    writeLog("shipPackage result '%s'\n", result.c_str());
}

void
PALSApp::startDaemon(const char* const args[])
{
    // Prepare to start daemon binary on compute node
    auto const remoteBEDaemonPath = m_toolPath + "/" + getBEDaemonName();

    // Send daemon if not already shipped
    if (!m_beDaemonSent) {
        // Get the location of the backend daemon
        if (m_frontend.getBEDaemonPath().empty()) {
            throw std::runtime_error("Unable to locate backend daemon binary. Try setting " + std::string(CTI_BASE_DIR_ENV_VAR) + " environment varaible to the install location of CTI.");
        }

        // Ship the BE binary to its unique storage name
        shipPackage(m_frontend.getBEDaemonPath(), getBEDaemonName());

        // set transfer to true
        m_beDaemonSent = true;
    }

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
    auto const toolInfoJson = cti::httpPostJsonReq(
        m_palsApiInfo.hostname,
        m_palsApiInfo.endpointBase + "v1/apps/" + m_apId + "/tools",
        m_palsApiInfo.accessToken,
        toolLaunchJson);
    writeLog("startDaemon result '%s'\n", toolInfoJson.c_str());

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
    , m_binaryRankMap{std::move(palsLaunchInfo.binaryRankMap)}
    , m_palsApiInfo{fe.getApiInfo()}


    , m_apinfoPath{"/var/run/palsd/" + m_apId + "/apinfo"}
    , m_toolPath{"/var/run/palsd/" + m_apId + "/files"}
    , m_attribsPath{"/var/run/palsd/" + m_apId} // BE daemon looks for <m_attribsPath>/pmi_attribs
    , m_stagePath{cti::cstr::mkdtemp(std::string{m_frontend.getCfgDir() + "/palsXXXXXX"})}
    , m_extraFiles{}

    , m_stdioStream{std::make_unique<PALSFrontend::CtiWSSImpl>(
        m_palsApiInfo.hostname, "443", m_palsApiInfo.accessToken)}
    , m_queuedInFd{palsLaunchInfo.stdinFd}
    , m_queuedOutFd{palsLaunchInfo.stdoutFd}
    , m_queuedErrFd{palsLaunchInfo.stderrFd}
    , m_stdioInputFuture{}
    , m_stdioOutputFuture{}
    , m_atBarrier{palsLaunchInfo.atBarrier}

    , m_toolIds{}
{
    // Initialize websocket stream
    m_stdioStream->websocket.handshake(
         m_palsApiInfo.hostname,
         m_palsApiInfo.endpointBase + "v1/apps/" + m_apId + "/stdio");

    // Request application stream mode
    pals::rpc::writeStream(m_stdioStream->websocket, m_apId);

    // Request start application if not already started
    if (!palsLaunchInfo.started) {
        pals::rpc::writeStart(m_stdioStream->websocket, m_apId);
    }

    // Launch stdio output responder thread
    m_stdioOutputFuture = std::async(std::launch::async, stdioOutputTask,
        std::ref(m_stdioStream->websocket), m_queuedOutFd, m_queuedErrFd);

    // Launch stdio input generation thread
    m_stdioInputFuture = std::async(std::launch::async, stdioInputTask,
        std::ref(m_stdioStream->websocket), m_queuedInFd);
}

PALSApp::~PALSApp()
{
    // Delete application from PALS
    try {
        cti::httpDeleteReq(
            m_palsApiInfo.hostname,
            m_palsApiInfo.endpointBase + "v1/apps/" + m_apId,
            m_palsApiInfo.accessToken);
    } catch (std::exception const& ex) {
        writeLog("warning: PALS delete %s failed: %s\n", m_apId.c_str(), ex.what());
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
    m_stdioStream.reset();
}


// HPCM PALS specializations

std::string
HPCMPALSFrontend::getApid(pid_t launcher_pid)
{
    // MPIR attach to launcher
    auto const mpirData = Daemon().request_AttachMPIR(
        // Get path to launcher binary
        cti::take_pointer_ownership(
            _cti_pathFind(getLauncherName().c_str(), nullptr),
            std::free).get(),
        // Attach to existing launcher PID
        launcher_pid);

    // Extract apid string from launcher
    auto result = Daemon().request_ReadStringMPIR(mpirData.mpir_id, "totalview_jobid");

    // Release MPIR control
    Daemon().request_ReleaseMPIR(mpirData.mpir_id);

    return result;
}

HPCMPALSFrontend::PalsLaunchInfo
HPCMPALSFrontend::getPalsLaunchInfo(std::string const& apId)
{
    throw std::runtime_error("not implemented: HPCMPALSFrontend::getPalsLaunchInfo");
}

HPCMPALSFrontend::PalsLaunchInfo
HPCMPALSFrontend::launchApp(const char * const launcher_argv[],
        int stdoutFd, int stderrFd, const char *inputFile, const char *chdirPath, const char * const env_list[])
{
    // Get the launcher path from CTI environment variable / default.
    if (auto const launcher_path = cti::take_pointer_ownership(_cti_pathFind(getLauncherName().c_str(), nullptr), std::free)) {
        // set up arguments and FDs
        if (inputFile == nullptr) { inputFile = "/dev/null"; }
        if (stdoutFd < 0) { stdoutFd = STDOUT_FILENO; }
        if (stderrFd < 0) { stderrFd = STDERR_FILENO; }

        // construct argv array & instance
        cti::ManagedArgv launcherArgv
            { launcher_path.get()
        };

        // Copy provided launcher arguments
        launcherArgv.add(launcher_argv);

        // Launch program under MPIR control.
        auto mpirData = Daemon().request_LaunchMPIR(
            launcher_path.get(), launcherArgv.get(),
            ::open(inputFile, O_RDONLY), stdoutFd, stderrFd,
            env_list);

        // Get application ID from launcher
        auto apid = Daemon().request_ReadStringMPIR(mpirData.mpir_id,
            "totalview_jobid");

        // Construct launch info struct
        return PalsLaunchInfo
            { .daemonAppId = mpirData.mpir_id
            , .apId = std::move(apid)
            , .procTable = std::move(mpirData.proctable)
            , .binaryRankMap = std::move(mpirData.binaryRankMap)
            , .atBarrier = true
        };

    } else {
        throw std::runtime_error("Failed to find launcher in path: " + getLauncherName());
    }
}

// Add the launcher's timeout environment variable to provided environment list
// Set timeout to five minutes
static inline auto setTimeoutEnvironment(std::string const& launcherName, CArgArray env_list)
{
    // Determine the timeout environment variable for PALS `mpiexec` or PALS `aprun` command
    // https://connect.us.cray.com/jira/browse/PE-34329
    auto const timeout_env = (launcherName == "aprun")
        ? "APRUN_RPC_TIMEOUT=300"
        : "PALS_RPC_TIMEOUT=300";

    // Add the launcher's timeout disable environment variable to a new environment list
    auto fixedEnvVars = cti::ManagedArgv{};

    // Copy provided environment list
    if (env_list != nullptr) {
        fixedEnvVars.add(env_list);
    }

    // Append timeout disable environment variable
    fixedEnvVars.add(timeout_env);

    return fixedEnvVars;
}

std::weak_ptr<App>
HPCMPALSFrontend::launch(CArgArray launcher_argv, int stdout_fd, int stderr_fd,
    CStr inputFile, CStr chdirPath, CArgArray env_list)
{
    auto fixedEnvVars = setTimeoutEnvironment(getLauncherName(), env_list);

    auto appPtr = std::make_shared<HPCMPALSApp>(*this,
        launchApp(launcher_argv, stdout_fd, stderr_fd, inputFile, chdirPath, fixedEnvVars.get()));

    // Release barrier and continue launch
    appPtr->releaseBarrier();

    // Register with frontend application set
    auto resultInsertedPair = m_apps.emplace(std::move(appPtr));
    if (!resultInsertedPair.second) {
        throw std::runtime_error("Failed to insert new App object.");
    }

    return *resultInsertedPair.first;
}

std::weak_ptr<App>
HPCMPALSFrontend::launchBarrier(CArgArray launcher_argv, int stdout_fd, int stderr_fd,
        CStr inputFile, CStr chdirPath, CArgArray env_list)
{
    auto fixedEnvVars = setTimeoutEnvironment(getLauncherName(), env_list);

    auto ret = m_apps.emplace(std::make_shared<HPCMPALSApp>(*this,
        launchApp(launcher_argv, stdout_fd, stderr_fd, inputFile, chdirPath, fixedEnvVars.get())));
    if (!ret.second) {
        throw std::runtime_error("Failed to create new App object.");
    }

    return *ret.first;
}

std::weak_ptr<App>
HPCMPALSFrontend::registerJob(size_t numIds, ...)
{
    if (numIds != 1) {
        throw std::logic_error("expecting single apid argument to register app");
    }

    va_list idArgs;
    va_start(idArgs, numIds);

    char const* apid = va_arg(idArgs, char const*);

    va_end(idArgs);

    auto palsLaunchInfo = getPalsLaunchInfo(apid);

    auto ret = m_apps.emplace(std::make_shared<HPCMPALSApp>(*this,
        std::move(palsLaunchInfo)));
    if (!ret.second) {
        throw std::runtime_error("Failed to create new App object.");
    }

    return *ret.first;
}

// Current address can now be obtained using the `cminfo` tool.
std::string
HPCMPALSFrontend::getHostname() const
{
    static auto const nodeAddress = []() {

        // Run cminfo query
        auto const cminfo_query = [](char const* option) {
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

        // Get name of management network
        auto const managementNetwork = cminfo_query("--mgmt_net_name");
        if (!managementNetwork.empty()) {

            // Query management IP address
            auto const addressOption = "--" + managementNetwork + "_ip";
            auto const managementAddress = cminfo_query(addressOption.c_str());
            if (!managementAddress.empty()) {
                return managementAddress;
            }
        }

        // Fall back to `gethostname`
        return cti::cstr::gethostname();
    }();

    return nodeAddress;
}

std::string
HPCMPALSFrontend::getLauncherName()
{
    // Cache the launcher name result. Assume mpiexec by default.
    auto static launcherName = std::string{cti::getenvOrDefault(CTI_LAUNCHER_NAME_ENV_VAR, "mpiexec")};
    return launcherName;
}

HPCMPALSApp::HPCMPALSApp(HPCMPALSFrontend& fe, HPCMPALSFrontend::PalsLaunchInfo&& palsLaunchInfo)
    : App{fe}
    , m_daemonAppId{palsLaunchInfo.daemonAppId}
    , m_apId{std::move(palsLaunchInfo.apId)}

    , m_beDaemonSent{false}
    , m_procTable{std::move(palsLaunchInfo.procTable)}
    , m_binaryRankMap{std::move(palsLaunchInfo.binaryRankMap)}

    , m_apinfoPath{"/var/run/palsd/" + m_apId + "/apinfo"}
    , m_toolPath{"/tmp/cti-" + m_apId}
    , m_attribsPath{"/var/run/palsd/" + m_apId} // BE daemon looks for <m_attribsPath>/pmi_attribs
    , m_stagePath{cti::cstr::mkdtemp(std::string{m_frontend.getCfgDir() + "/palsXXXXXX"})}
    , m_extraFiles{}

    , m_atBarrier{palsLaunchInfo.atBarrier}
{
    // Get set of hosts for application
    for (auto&& [pid, hostname, executable] : m_procTable) {
        m_hosts.emplace(hostname);
    }

    // Create remote toolpath directory
    { auto palscmdArgv = cti::ManagedArgv { "palscmd", m_apId,
            "mkdir", "-p", m_toolPath };

        if (!m_frontend.Daemon().request_ForkExecvpUtil_Sync(
            m_daemonAppId, "palscmd", palscmdArgv.get(),
            -1, -1, -1,
            nullptr)) {
            throw std::runtime_error("failed to create remote toolpath directory for apid " + m_apId);
        }
    }
}

HPCMPALSApp::~HPCMPALSApp()
{
    // Remove remote toolpath directory
    { auto palscmdArgv = cti::ManagedArgv { "palscmd", m_apId,
            "rm", "-rf", m_toolPath };

        // Ignore failures in destructor
        m_frontend.Daemon().request_ForkExecvpUtil_Sync(
            m_daemonAppId, "palscmd", palscmdArgv.get(),
            -1, -1, -1,
            nullptr);
    }
}

std::string
HPCMPALSApp::getLauncherHostname() const
{
    throw std::runtime_error{"not supported for PALS: " + std::string{__func__}};
}

bool
HPCMPALSApp::isRunning() const
{
    auto palstatArgv = cti::ManagedArgv{"palstat", m_apId};
    return m_frontend.Daemon().request_ForkExecvpUtil_Sync(
        m_daemonAppId, "palstat", palstatArgv.get(),
        -1, -1, -1,
        nullptr);
}

size_t
HPCMPALSApp::getNumPEs() const
{
    return m_procTable.size();
}

size_t
HPCMPALSApp::getNumHosts() const
{
    return m_hosts.size();
}

std::vector<std::string>
HPCMPALSApp::getHostnameList() const
{
    // Make vector from set
    auto result = std::vector<std::string>{};
    result.reserve(m_hosts.size());
    for (auto&& hostname : m_hosts) {
        result.emplace_back(hostname);
    }

    return result;
}

std::vector<CTIHost>
HPCMPALSApp::getHostsPlacement() const
{
    // Count PEs for each host
    auto hostnameCountMap = std::map<std::string, size_t>{};
    for (auto&& [pid, hostname, executable] : m_procTable) {
        hostnameCountMap[hostname]++;
    }

    // Make vector from map
    auto result = std::vector<CTIHost>{};
    for (auto&& [hostname, count] : hostnameCountMap) {
        result.emplace_back(CTIHost{std::move(hostname), count});
    }

    return result;
}

std::map<std::string, std::vector<int>>
HPCMPALSApp::getBinaryRankMap() const
{
    return m_binaryRankMap;
}

void
HPCMPALSApp::releaseBarrier()
{
    if (!m_atBarrier) {
        throw std::runtime_error("application is not at startup barrier");
    }

    m_frontend.Daemon().request_ReleaseMPIR(m_daemonAppId);
    m_atBarrier = false;
}

void
HPCMPALSApp::shipPackage(std::string const& tarPath) const
{
    auto const destinationName = cti::cstr::basename(tarPath);

    auto palscpArgv = cti::ManagedArgv{"palscp", "-f", tarPath, "-d", destinationName, m_apId};

    if (!m_frontend.Daemon().request_ForkExecvpUtil_Sync(
        m_daemonAppId, "palscp", palscpArgv.get(),
        -1, -1, -1,
        nullptr)) {
        throw std::runtime_error("failed to ship " + tarPath + " using palscp");
    }

    // Move shipped file from noexec filesystem to toolpath directory
    auto const palscpDestination = "/var/run/palsd/" + m_apId + "/files/" + destinationName;
    auto palscmdArgv = cti::ManagedArgv { "palscmd", m_apId,
            "mv", palscpDestination, m_toolPath };

    if (!m_frontend.Daemon().request_ForkExecvpUtil_Sync(
        m_daemonAppId, "palscmd", palscmdArgv.get(),
        -1, -1, -1,
        nullptr)) {
        throw std::runtime_error("failed to move shipped package for apid " + m_apId);
    }
}

void
HPCMPALSApp::startDaemon(const char* const args[])
{
    // sanity check
    if (args == nullptr) {
        throw std::runtime_error("args array is empty!");
    }

    // Send daemon if not already shipped
    if (!m_beDaemonSent) {
        // Get the location of the backend daemon
        if (m_frontend.getBEDaemonPath().empty()) {
            throw std::runtime_error("Unable to locate backend daemon binary. Try setting " + std::string(CTI_BASE_DIR_ENV_VAR) + " environment variable to the install location of CTI.");
        }

        // Copy the BE binary to its unique storage name
        auto const sourcePath = m_frontend.getBEDaemonPath();
        auto const destinationPath = m_frontend.getCfgDir() + "/" + getBEDaemonName();

        // Create the args for copy
        auto copyArgv = cti::ManagedArgv {
            "cp", sourcePath.c_str(), destinationPath.c_str()
        };

        // Run copy command
        if (!m_frontend.Daemon().request_ForkExecvpUtil_Sync(
            m_daemonAppId, "cp", copyArgv.get(),
            -1, -1, -1,
            nullptr)) {
            throw std::runtime_error("failed to copy " + sourcePath + " to " + destinationPath);
        }

        // Ship the unique backend daemon
        shipPackage(destinationPath);
        // set transfer to true
        m_beDaemonSent = true;
    }

    // Create the arguments for palscmd
    auto palscmdArgv = cti::ManagedArgv { "palscmd", m_apId };

    // Use location of existing launcher binary on compute node
    auto const launcherPath = m_toolPath + "/" + getBEDaemonName();
    palscmdArgv.add(launcherPath);

    // Copy provided launcher arguments
    palscmdArgv.add(args);

    // tell frontend daemon to launch palscmd, wait for it to finish
    if (!m_frontend.Daemon().request_ForkExecvpUtil_Sync(
        m_daemonAppId, "palscmd", palscmdArgv.get(),
        -1, -1, -1,
        nullptr)) {
        throw std::runtime_error("failed to launch tool daemon for apid " + m_apId);
    }
}

void HPCMPALSApp::kill(int signum)
{
    // create the args for palsig
    auto palsigArgv = cti::ManagedArgv { "palsig", "-s", std::to_string(signum),
        m_apId };

    // tell frontend daemon to launch palsig, wait for it to finish
    if (!m_frontend.Daemon().request_ForkExecvpUtil_Sync(
        m_daemonAppId, "palsig", palsigArgv.get(),
        -1, -1, -1,
        nullptr)) {
        throw std::runtime_error("failed to send signal to apid " + m_apId);
    }
}
