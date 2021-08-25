/******************************************************************************\
 * flux_api_parse_tests.cpp - Flux API response parsing tests
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

#include "cti_defs.h"

#include "src/frontend/frontend_impl/Flux/Frontend.hpp"
#include "src/frontend/frontend_impl/Flux/FluxAPI.hpp"

// include google testing files
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using ::testing::Return;
using ::testing::_;
using ::testing::Invoke;
using ::testing::WithoutArgs;
using ::testing::EndsWith;

TEST(parse_rangeList, Empty)
{
    auto const root = parse_json("[-1, -1]");
    auto base = int64_t{};
    auto const rangeList = flux::parse_rangeList(root, base);
    ASSERT_TRUE(std::holds_alternative<flux::Empty>(rangeList));
}

TEST(parse_rangeList, Single)
{
    auto const root = parse_json("3");
    auto base = int64_t{};
    auto const rangeList = flux::parse_rangeList(root, base);
    ASSERT_TRUE(std::holds_alternative<flux::RLE>(rangeList));
    auto const [value, count] = std::get<flux::RLE>(rangeList);
    EXPECT_TRUE(value == 3);
    EXPECT_TRUE(count == 1);
    EXPECT_TRUE(base == 3);
}

TEST(parse_rangeList, Range)
{
    auto const root = parse_json("[2,3]");
    auto base = int64_t{};
    auto const rangeList = flux::parse_rangeList(root, base);
    ASSERT_TRUE(std::holds_alternative<flux::Range>(rangeList));
    auto const [start, end] = std::get<flux::Range>(rangeList);
    EXPECT_TRUE(start == 2);
    EXPECT_TRUE(end == 5);
    EXPECT_TRUE(base == 5);
}

TEST(parse_rangeList, RLE)
{
    auto const root = parse_json("[2,-3]");
    auto base = int64_t{};
    auto const rangeList = flux::parse_rangeList(root, base);
    ASSERT_TRUE(std::holds_alternative<flux::RLE>(rangeList));
    auto const [value, count] = std::get<flux::RLE>(rangeList);
    EXPECT_TRUE(value == 2);
    EXPECT_TRUE(count == 4);
    EXPECT_TRUE(base == 2);
}

TEST(flatten_rangeList, Empty)
{
    auto const root = parse_json("[[-1, -1]]");
    auto const values = flux::flatten_rangeList(root);
    EXPECT_EQ(values.size(), 0);
}

TEST(flatten_rangeList, Single)
{
    auto const root = parse_json("[[2, 3]]");
    auto const values = flux::flatten_rangeList(root);
    auto const rhs = decltype(values){2, 3, 4, 5};
    EXPECT_EQ(values.size(), rhs.size());
    EXPECT_EQ(values, rhs);
}

TEST(flatten_rangeList, Multi)
{
    auto const root = parse_json("[[2, 3], [2, -2]]");
    auto const values = flux::flatten_rangeList(root);
    auto const rhs = decltype(values){2, 3, 4, 5, 7, 7, 7};
    EXPECT_EQ(values.size(), rhs.size());
    EXPECT_EQ(values, rhs);
}

TEST(for_each_prefixList, SingleEmpty)
{
    auto const root = parse_json("[[ \"prefix\", [[-1, -1]] ]]");
    auto values = std::unordered_set<std::string>{};
    flux::for_each_prefixList(root, [&values](std::string value) {
        values.emplace(std::move(value));
    });
    auto const rhs = decltype(values){"prefix"};
    EXPECT_EQ(values.size(), rhs.size());
    EXPECT_EQ(values, rhs);
}

TEST(for_each_prefixList, SingleRange)
{
    auto const root = parse_json("[[ \"prefix\", [[2, 3]] ]]");
    auto values = std::unordered_set<std::string>{};
    flux::for_each_prefixList(root, [&values](std::string value) {
        values.emplace(std::move(value));
    });
    auto const rhs = decltype(values){"prefix2", "prefix3", "prefix4", "prefix5"};
    EXPECT_EQ(values.size(), rhs.size());
    EXPECT_EQ(values, rhs);
}

TEST(for_each_prefixList, SingleRLE)
{
    auto const root = parse_json("[[ \"prefix\", [[2, -2]] ]]");
    auto values = std::unordered_set<std::string>{};
    flux::for_each_prefixList(root, [&values](std::string value) {
        values.emplace(std::move(value));
    });
    auto const rhs = decltype(values){"prefix2"};
    EXPECT_EQ(values.size(), rhs.size());
    EXPECT_EQ(values, rhs);
}

TEST(for_each_prefixList, SingleMulti)
{
    auto const root = parse_json("[[ \"prefix\", [[2, 3], [2, -2]] ]]");
    auto values = std::unordered_set<std::string>{};
    flux::for_each_prefixList(root, [&values](std::string value) {
        values.emplace(std::move(value));
    });
    auto const rhs = decltype(values){"prefix2", "prefix3", "prefix4", "prefix5", "prefix7", "prefix7"};
    EXPECT_EQ(values.size(), rhs.size());
    EXPECT_EQ(values, rhs);
}

TEST(for_each_prefixList, Multi)
{
    auto const root = parse_json("[ [ \"a\", [[2, 3], [2, -2]] ], [ \"b\", [[3, 2], [1, -1]] ] ]");
    auto values = std::unordered_set<std::string>{};
    flux::for_each_prefixList(root, [&values](std::string value) {
        values.emplace(std::move(value));
    });
    auto const rhs = decltype(values){"a2", "a3", "a4", "a5", "a7", "a7", "b3", "b4", "b5", "b6", "b6"};
    EXPECT_EQ(values.size(), rhs.size());
    EXPECT_EQ(values, rhs);
}

TEST(flatten_prefixList, SingleRLE)
{
    auto const root = parse_json("[[ \"prefix\", [[2, -2]] ]]");
    auto values = std::unordered_set<std::string>{};
    flux::for_each_prefixList(root, [&values](std::string value) {
        values.emplace(std::move(value));
    });
    auto const rhs = decltype(values){"prefix2", "prefix2", "prefix2"};
    EXPECT_EQ(values.size(), rhs.size());
    EXPECT_EQ(values, rhs);
}

TEST(make_hostsPlacement, SingleRank)
{
    auto const root = parse_json(
        "{ \"hosts\": [\"node15\"]"
        ", \"executables\": [\"/path/to/a.out\"]"
        ", \"ids\": [0]"
        ", \"pids\": [19797]"
        "}");
    auto const hostsPlacement = flux::make_hostsPlacement(root);
    ASSERT_EQ(hostsPlacement.size(), 1);

    EXPECT_EQ(hostsPlacement[0].hostname, "node15");
    EXPECT_EQ(hostsPlacement[0].numPEs, 1);
    { auto const rhs = decltype(hostsPlacement[0].rankPidPairs){{0, 19797}};
        EXPECT_EQ(hostsPlacement[0].rankPidPairs, rhs);
    }
}

TEST(make_hostsPlacement, MultiRank)
{
    auto const root = parse_json(
        "{ \"hosts\": [[ \"node\", [[15,-1]] ]]"
        ", \"executables\": [[ \"/path/to/a.out\", [[-1,-1]] ]]"
        ", \"ids\": [[0,1]]"
        ", \"pids\": [[7991,1]]"
        "}");
    auto const hostsPlacement = flux::make_hostsPlacement(root);
    ASSERT_EQ(hostsPlacement.size(), 1);

    EXPECT_EQ(hostsPlacement[0].hostname, "node15");
    EXPECT_EQ(hostsPlacement[0].numPEs, 2);
    { auto const rhs = decltype(hostsPlacement[0].rankPidPairs){{0, 7991}, {1, 7992}};
        EXPECT_EQ(hostsPlacement[0].rankPidPairs, rhs);
    }
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
