/******************************************************************************\
 * flux_api_parse_tests.cpp - Flux API response parsing tests
 *
 * Copyright 2021 Hewlett Packard Enterprise Development LP.
 * SPDX-License-Identifier: Linux-OpenIB
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
    EXPECT_EQ(value, 3);
    EXPECT_EQ(count, 1);
    EXPECT_EQ(base, 3);
}

TEST(parse_rangeList, Range)
{
    auto const root = parse_json("[2,3]");
    auto base = int64_t{};
    auto const rangeList = flux::parse_rangeList(root, base);
    ASSERT_TRUE(std::holds_alternative<flux::Range>(rangeList));
    auto const [start, end] = std::get<flux::Range>(rangeList);
    EXPECT_EQ(start, 2);
    EXPECT_EQ(end, 5);
    EXPECT_EQ(base, 5);
}

TEST(parse_rangeList, RLE)
{
    auto const root = parse_json("[2,-3]");
    auto base = int64_t{};
    auto const rangeList = flux::parse_rangeList(root, base);
    ASSERT_TRUE(std::holds_alternative<flux::RLE>(rangeList));
    auto const [value, count] = std::get<flux::RLE>(rangeList);
    EXPECT_EQ(value, 2);
    EXPECT_EQ(count, 4);
    EXPECT_EQ(base, 2);
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

TEST(flatten_rangeList, Mixed)
{
    auto const root = parse_json("[[1108964,31],[5,22],4,[6,1],2,3]");
    auto const values = flux::flatten_rangeList(root);
    auto const rhs = decltype(values){
        // [1108964,31] -> 1108964 - 1108995
        1108964, 1108965, 1108966, 1108967, 1108968, 1108969, 1108970, 1108971, 1108972,
        1108973, 1108974, 1108975, 1108976, 1108977, 1108978, 1108979, 1108980, 1108981,
        1108982, 1108983, 1108984, 1108985, 1108986, 1108987, 1108988, 1108989, 1108990,
        1108991, 1108992, 1108993, 1108994, 1108995,

        // 1108995 + [5,22] -> 1109000 - 11090022
        1109000, 1109001, 1109002, 1109003, 1109004, 1109005, 1109006, 1109007, 1109008,
        1109009, 1109010, 1109011, 1109012, 1109013, 1109014, 1109015, 1109016, 1109017,
        1109018, 1109019, 1109020, 1109021, 1109022,

        // 11090022 + 4 -> 110900026
        1109026,

        // 1109026 + [6,1] -> 1109032 - 1109033
        1109032, 1109033,

        // 1109033 + 2 -> 1109035
        1109035,

        // 1109035 + 3 -> 1109038
        1109038
    };
    EXPECT_EQ(values.size(), rhs.size());
    EXPECT_EQ(values, rhs);
}

TEST(for_each_prefixList, SingleEmpty)
{
    auto const root = parse_json("[[ \"prefix\", [[-1, -1]] ]]");
    auto values = std::unordered_set<std::string>{};
    flux::for_each_prefixList(root, [&values](std::string const& prefix, std::string const& suffix) {
        values.emplace(prefix + suffix);
    });
    auto const rhs = decltype(values){"prefix"};
    EXPECT_EQ(values.size(), rhs.size());
    EXPECT_EQ(values, rhs);
}

TEST(for_each_prefixList, SingleRange)
{
    auto const root = parse_json("[[ \"prefix\", [[2, 3]] ]]");
    auto values = std::unordered_set<std::string>{};
    flux::for_each_prefixList(root, [&values](std::string const& prefix, std::string const& suffix) {
        values.emplace(prefix + suffix);
    });
    auto const rhs = decltype(values){"prefix2", "prefix3", "prefix4", "prefix5"};
    EXPECT_EQ(values.size(), rhs.size());
    EXPECT_EQ(values, rhs);
}

TEST(for_each_prefixList, SingleRLE)
{
    auto const root = parse_json("[[ \"prefix\", [[2, -2]] ]]");
    auto values = std::unordered_set<std::string>{};
    flux::for_each_prefixList(root, [&values](std::string const& prefix, std::string const& suffix) {
        values.emplace(prefix + suffix);
    });
    auto const rhs = decltype(values){"prefix2"};
    EXPECT_EQ(values.size(), rhs.size());
    EXPECT_EQ(values, rhs);
}

TEST(for_each_prefixList, SingleMulti)
{
    auto const root = parse_json("[[ \"prefix\", [[2, 3], [2, -2]] ]]");
    auto values = std::unordered_set<std::string>{};
    flux::for_each_prefixList(root, [&values](std::string const& prefix, std::string const& suffix) {
        values.emplace(prefix + suffix);
    });
    auto const rhs = decltype(values){"prefix2", "prefix3", "prefix4", "prefix5", "prefix7", "prefix7"};
    EXPECT_EQ(values.size(), rhs.size());
    EXPECT_EQ(values, rhs);
}

TEST(for_each_prefixList, Multi)
{
    auto const root = parse_json("[ [ \"a\", [[2, 3], [2, -2]] ], [ \"b\", [[3, 2], [1, -1]] ] ]");
    auto values = std::unordered_set<std::string>{};
    flux::for_each_prefixList(root, [&values](std::string const& prefix, std::string const& suffix) {
        values.emplace(prefix + suffix);
    });
    auto const rhs = decltype(values){"a2", "a3", "a4", "a5", "a7", "a7", "b3", "b4", "b5", "b6"};
    EXPECT_EQ(values.size(), rhs.size());
    EXPECT_EQ(values, rhs);
}

TEST(flatten_prefixList, SingleRLE)
{
    auto const root = parse_json("[[ \"prefix\", [[2, -2]] ]]");
    auto values = std::unordered_set<std::string>{};
    flux::for_each_prefixList(root, [&values](std::string const& prefix, std::string const& suffix) {
        values.emplace(prefix + suffix);
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
        "{ \"hosts\": [[\"tioga\",[[29,-2],[3,-2]]]]"
        ", \"executables\": [[\"/g/g11/dangelo3/signals\",[[-1,-5]]]]"
        ", \"ids\":[[0,5]]"
        ", \"pids\":[[2736905,2],[1381418,2]]"
        "}");
    auto const hostsPlacement = flux::make_hostsPlacement(root);
    ASSERT_EQ(hostsPlacement.size(), 2);

    EXPECT_EQ(hostsPlacement[0].hostname, "tioga29");
    EXPECT_EQ(hostsPlacement[0].numPEs, 3);
    { auto const rhs = decltype(hostsPlacement[0].rankPidPairs){
            {0, 2736905}, {1, 2736906}, {2, 2736907}};
        EXPECT_EQ(hostsPlacement[0].rankPidPairs, rhs);
    }
    EXPECT_EQ(hostsPlacement[1].hostname, "tioga32");
    EXPECT_EQ(hostsPlacement[1].numPEs, 3);
    { auto const rhs = decltype(hostsPlacement[1].rankPidPairs){
            {3, 4118325}, {4, 4118326}, {5, 4118327}};
        EXPECT_EQ(hostsPlacement[1].rankPidPairs, rhs);
    }
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
