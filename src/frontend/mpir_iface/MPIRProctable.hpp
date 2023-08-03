/******************************************************************************\
 * MPIRProctable.hpp
 *
 * Copyright 2019-2020 Hewlett Packard Enterprise Development LP.
 * SPDX-License-Identifier: Linux-OpenIB
 ******************************************************************************/
#pragma once

#include <string>
#include <vector>
#include <map>

struct MPIRProctableElem {
    pid_t pid;
    std::string hostname;
    std::string executable;
};

using MPIRProctable = std::vector<MPIRProctableElem>;
using BinaryRankMap = std::map<std::string, std::vector<int>>;

static inline auto generateBinaryRankMap(MPIRProctable const& procTable)
{
	auto result = BinaryRankMap{};

	auto rank = int{0};
	for (auto&& elem : procTable) {
		result[elem.executable].push_back(rank++);
	}

	return result;
}
