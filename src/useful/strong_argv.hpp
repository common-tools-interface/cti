#ifndef _STRONG_ARGV_HPP
#define _STRONG_ARGV_HPP

#include <vector>
#include <string>

#include <getopt.h>
#include <string.h>

class ManagedArgv {
	std::vector<char*> argv;

public:
	/* constructor creates NULL-terminator. */
	ManagedArgv() {
		argv.push_back(NULL);
	}

	/* destructor frees the strdup'ed memory */
	~ManagedArgv() {
		for (char* str : argv) {
			if (str) { free(str); }
		}
	}

	/* move constructor: just move the array */
	ManagedArgv(ManagedArgv&& moved) : argv(std::move(moved.argv)) {}

	size_t size() const { return argv.size(); }
	char** get() { return argv.data(); }

	void add(std::string const& str) {
		argv.insert(argv.end() - 1, strdup(str.c_str()));
	}

	void add(const char* str) {
		argv.insert(argv.end() - 1, strdup(str));
	}
};

struct Argv {
	using GNUOption = struct option;

	static constexpr GNUOption long_options_done { nullptr, 0, nullptr, 0 };

	struct Option : public GNUOption {
		explicit constexpr Option(const char* longFlag, char shortFlag) : 
			GNUOption { longFlag, no_argument, nullptr, shortFlag } {}
	};

	struct Parameter : public GNUOption {
		explicit constexpr Parameter(const char* longFlag, char shortFlag) :
			GNUOption { longFlag, required_argument, nullptr, shortFlag } {}
	};

	struct Argument : public std::string {
		Argument(std::string str) : std::string(str) {}
	};
};

template <class ArgvDef>
class OutgoingArgv : public ArgvDef {
	ManagedArgv argv;

public:
	OutgoingArgv(std::string const& binary) {
		argv.add(binary);
	}

	char* const *get() { return argv.get(); }

	void add(Argv::Option const& opt) {
		if (opt.name == nullptr) {
			argv.add(std::string("-")  + (char)(opt.val));
		} else {
			argv.add(std::string("--") + opt.name);
		}
	}

	void add(Argv::Parameter const& param, std::string const& value) {
		if (param.name == nullptr) {
			argv.add(std::string("-")  + (char)(param.val));
			argv.add(value);
		} else {
			argv.add(std::string("--") + param.name + "=" + value);
		}
	}

	void add(Argv::Argument const& arg) {
		argv.add(arg);
	}
};

template <class ArgvDef>
class IncomingArgv : public ArgvDef {
	std::string flagSpec;
	const int argc;
	char* const* argv;

public:
	IncomingArgv(int argc_, char* const* argv_) : argc(argc_), argv(argv_) {
		for (const Argv::GNUOption* opt_ptr = ArgvDef::long_options;
			opt_ptr->val != 0; opt_ptr++) {

			flagSpec.push_back((char)(opt_ptr->val));
			if (opt_ptr->has_arg != no_argument) {
				flagSpec.push_back(':');
			}
		}
	}

	std::pair<int, std::string> get_next() {
		int c = getopt_long(argc, argv, flagSpec.c_str(), ArgvDef::long_options, nullptr);

		if ((c < 0) || (optarg == nullptr)) {
			return std::make_pair(c, "");
		}

		return std::make_pair(c, optarg);
	}

	char* const* get_rest() {
		return argv + optind;
	}
};

constexpr Argv::GNUOption Argv::long_options_done;

#endif
