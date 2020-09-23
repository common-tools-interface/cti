/******************************************************************************\
 * libchecksum.cpp -
 *
 * Copyright 2019-2020 Hewlett Packard Enterprise Development LP.
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include "checksums.h"

#ifdef HAVE_CHECKSUM

#include <sstream>

#include "useful/cti_execvp.hpp"

static std::string hash_path(std::string const& path)
{
	char const* const sumArgv[] = { CHECKSUM_BINARY, path.c_str(), nullptr };
	auto sumOutput = cti::Execvp{CHECKSUM_BINARY, (char* const*)sumArgv};

	auto& sumStream = sumOutput.stream();
	std::string sumPathLine;

	if (sumOutput.getExitStatus() || !std::getline(sumStream, sumPathLine)) {
		throw std::runtime_error("failed to hash " + path);
	}

	return sumPathLine.substr(0, sumPathLine.find(" "));
}

bool has_same_hash(char const* path, char const* hash)
{
	return hash_path(path) == hash;
}

#else

bool has_same_hash(char const* path, char const* hash)
{
	return true;
}

#endif
