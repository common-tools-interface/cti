/******************************************************************************\
 * FluxAPI.hpp - Flux API response parsing functions
 *
 * Copyright 2021 Hewlett Packard Enterprise Development LP.
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

#include <memory>
#include <variant>
#include <type_traits>

// Boost JSON
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/algorithm/string/replace.hpp>

// Boost array stream
#include <boost/iostreams/device/array.hpp>
#include <boost/iostreams/stream.hpp>

namespace pt = boost::property_tree;

/* helper functions */

static inline auto parse_json(std::string const& json)
{
    // Create stream from string source
    auto jsonSource = boost::iostreams::array_source{json.c_str(), json.length()};
    auto jsonStream = boost::iostreams::stream<boost::iostreams::array_source>{jsonSource};

    auto root = pt::ptree{};
    try {
        pt::read_json(jsonStream, root);
    } catch (pt::json_parser::json_parser_error const& parse_ex) {
        throw std::runtime_error("failed to parse JSON response: " + json);
    }

    return root;
}

namespace flux
{

struct Empty {};
struct Range
    { int64_t start, end;
};
struct RLE
    { int64_t value, count;
};
using RangeList = std::variant<Empty, Range, RLE>;

// Read next rangelist object and return new Range / RLE state
// Updates `base` state by reference
static inline auto parse_rangeList(pt::ptree const& root, int64_t& base)
{
    /* The rangelists [ 1, 3 ], [ 5, -1 ] will be parsed as the following:
       - Range of ints 1 to 3 inclusive
       - RLE with value 3 + 5 = 8 of length -(-1) + 1 = 2
       - Values 1, 2, 3, 8, 8
       The prefix rangelist data [ "node", [ [1,3], [5,-1] ] ] will then be
       computed as node1, node2, node3, node8, node8.
       Finally, nodes 1 through 3 will have 1 PE each, and node 8 will have 2
    */

    // Single element will be interpreted as a range of size 1
    if (!root.data().empty()) {
        base = root.get_value<int64_t>();
        return RangeList { RLE
            { .value = base
            , .count = 1
        } };
    }

    // Multiple elements must be size 2 for range
    if (root.size() != 2) {
        throw std::runtime_error("Flux API rangelist must have size 2");
    }

    auto cursor = root.begin();

    // Add base offset to range start / RLE value
    auto const first = base + (cursor++)->second.get_value<int>();
    auto const second = (cursor++)->second.get_value<int>();

    // Negative first element indicates empty range
    if (first < 0) {
        return RangeList { Empty{} };
    }

    // Negative second element indicates run length encoding
    if (second < 0) {
        base = first;
        return RangeList { RLE
            { .value = base
            , .count = -second + 1
        } };

    // Otherwise, traditional range
    } else {
        base = first + second;
        return RangeList { Range
            { .start = first
            , .end = base
        } };
    }
}

static inline auto flatten_rangeList(pt::ptree const& root)
{
    auto result = std::vector<int64_t>{};

    auto base = int64_t{0};

    // { "": [ rangelist, ... ], ... }
    for (auto&& rangeListObjectPair : root) {

        // Parse inner rangelist object as either range or RLE
        // `base` is updated by `parse_rangeList`
        auto const rangeList = parse_rangeList(rangeListObjectPair.second, base);

        // Empty: no element
        if (std::holds_alternative<Empty>(rangeList)) {
            continue;

        // Range: add entire range to result
        } else if (std::holds_alternative<Range>(rangeList)) {

            auto const [start, end] = std::get<Range>(rangeList);
            result.reserve(result.size() + (end - start));
            for (auto i = start; i <= end; i++) {
                result.push_back(i);
            }

        // RLE: add run length to result
        } else if (std::holds_alternative<RLE>(rangeList)) {

            auto const [value, count] = std::get<RLE>(rangeList);
            std::fill_n(std::back_inserter(result), count, value);
        }
    }

    return result;
}

template <typename Func>
static void for_each_prefixList(pt::ptree const& root, Func&& func)
{
    /* "hosts" contains a prefix rangelist that expands to one instance of each
       hostname for every PE on that host.
       The rangelists [ 1, 3 ], [ 5, -1 ] will be parsed as the following:
       - Range of ints 1 to 3 inclusive
       - RLE with value 3 + 5 = 8 of length -(-1) + 1 = 2
       - Values 1, 2, 3, 8, 8
       The prefix rangelist data [ "node", [ [1,3], [5,-1] ] ] will then be
       computed as node1, node2, node3, node8, node8.
       Finally, nodes 1 through 3 will have 1 PE each, and node 8 will have 2
    */

    // { "": [ prefix_string, { "": [ rangelist, ... ], ... } ] }
    for (auto&& prefixListArrayPair : root) {

        // If element has data, it is a plain string instead of a prefix list
        if (!prefixListArrayPair.second.data().empty()) {
            func(prefixListArrayPair.second.data());
            continue;
        }

        // { "": [ prefix_string, { "": [ rangelist, ... ], ... } ] }
        auto cursor = prefixListArrayPair.second.begin();
        auto const prefix = (cursor++)->second.get_value<std::string>();
        auto const rangeListObjectArray = (cursor++)->second;

        auto hostnamePostfixes = flatten_rangeList(rangeListObjectArray);

        // Empty: there is a single string consisting solely of the prefix
        if (hostnamePostfixes.empty()) {
                func(prefix);

        // Run function on every generated string
        } else {
            for (auto&& postfix : hostnamePostfixes) {
                func(prefix + std::to_string(postfix));
            }
        }
    }
}

static inline auto const flatten_prefixList(pt::ptree const& root)
{
    auto result = std::vector<std::string>{};

    for_each_prefixList(root, [&](std::string hostname) {
        result.emplace_back(std::move(hostname));
    });

    return result;
}

static inline auto make_hostsPlacement(pt::ptree const& root)
{
    auto result = std::vector<FluxFrontend::HostPlacement>{};

    /* Flux proctable format:
      prefix_rangelist: [ prefix_string, [ rangelist, ... ] ]
      "hosts": [ prefix_rangelist, ... ]
      "executables": [ prefix_rangelist, ... ]
      "ids": [ rangelist, ... ]
      "pids": [ rangelist, ... ]

      Example: running 1 rank of a.out on node15
        { "hosts": ["node15"]
        , "executables": ["/path/to/a.out"]
        , "ids": [0]
        , "pids": [19797]
        }

      Example: running 2 ranks of a.out on node15, with PIDs 7991 and 7992
        { "hosts": [[ "node", [[15,-1]] ]]
        , "executables": [[ "/path/to/a.out", [[-1,-1]] ]]
        , "ids": [[0,1]]
        , "pids": [[7991,1]]
        }
    */

    auto hostPlacementMap = std::map<std::string, FluxFrontend::HostPlacement>{};
    auto hostname_entries = size_t{0};

    // Add count for every hostname
    for_each_prefixList(root.get_child("hosts"), [&](std::string const& hostname) {
        auto& hostPECountPair = hostPlacementMap[hostname];
        hostPECountPair.hostname = hostname;
        hostPECountPair.numPEs++;
        hostname_entries++;
    });

    // Get list of all ranks and PIDs
    auto const ranks = flatten_rangeList(root.get_child("ids"));
    auto const pids = flatten_rangeList(root.get_child("pids"));

    // Each hostname occurrence corresponds to a single rank and PID
    if (ranks.size() != hostname_entries) {
        throw std::runtime_error("mismatch between rank and hostname count from Flux API ("
            + std::to_string(ranks.size()) + " ranks and " + std::to_string(hostname_entries)
            + " hostname entries");
    }
    if (pids.size() != hostname_entries) {
        throw std::runtime_error("mismatch between PID and hostname count from Flux API ("
            + std::to_string(pids.size()) + " PIDs and " + std::to_string(hostname_entries)
            + " hostname entries");
    }

    // Host with N ranks will have the next N PIDs from rank list
    auto rank_cursor = ranks.begin();
    auto pid_cursor = pids.begin();
    for (auto&& [hostname, placement] : hostPlacementMap) {
        for (size_t i = 0; i < placement.numPEs; i++) {
            placement.rankPidPairs.emplace_back(*rank_cursor, *pid_cursor);
            rank_cursor++;
            pid_cursor++;
        }
    }

    // Construct placement vector from map
    result.reserve(hostPlacementMap.size());
    for (auto&& [hostname, placement] : hostPlacementMap) {
        result.emplace_back(std::move(placement));
    }

    return result;
}

} // namespace flux
