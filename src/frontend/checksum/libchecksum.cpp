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
