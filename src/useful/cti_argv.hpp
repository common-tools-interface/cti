/*********************************************************************************\
 * cti_argv.hpp: Interface for handling argv.
 *
 * Copyright 2019-2023 Hewlett Packard Enterprise Development LP.
 * SPDX-License-Identifier: Linux-OpenIB
 ******************************************************************************/
#pragma once

#include <vector>
#include <string>
#include <sstream>
#include <stdexcept>

#include <getopt.h>
#include <string.h>

namespace cti {

// Check that the nullptr-terminated `env` list contains only valid strings for
// specifying environment variables with a function like putenv. (NAME=VALUE).
// If an invalid string is found, throws std::runtime_error.
inline void enforceValidEnvStrings(const char* const env[]) {
    for (const char* const* var = env; *var != nullptr; var++) {
        const auto eq = ::strchr(*var, '=');
        if (!eq || eq == *var)
            throw std::runtime_error(std::string("Bad environment variable string: \"") + *var + "\"");
    }
}

class ManagedArgv {
private:
    std::vector<char*> argv;

public:
    /* constructor creates NULL-terminator. */
    ManagedArgv() {
        argv.push_back(nullptr);
    }

    ManagedArgv(std::initializer_list<std::string const> str_list) {
        argv.reserve(str_list.size());
        for (auto&& str : str_list) {
            argv.push_back(strdup(str.c_str()));
        }
        argv.push_back(nullptr);
    }

    /* destructor frees the strdup'ed memory */
    ~ManagedArgv() {
        for (char* str : argv) {
            free(str);
        }
    }

    /* move constructor: destructive move the array */
    ManagedArgv(ManagedArgv&& moved)
        : argv{std::move(moved.argv)}
    {}
    ManagedArgv& operator= (ManagedArgv&& moved) {
        argv = std::move(moved.argv);
        return *this;
    }

    /* member methods */
    size_t size() const { return argv.size(); }
    char** get() { return argv.data(); }
    char const* const* get() const { return argv.data(); }

    ManagedArgv clone() const
    {
        auto result = ManagedArgv{};
        result.add(get());
        return result;
    }

    void add(std::string const& str) {
        argv.insert(argv.end() - 1, strdup(str.c_str()));
    }

    void add(const char* str) {
        if (str == nullptr) {
            throw std::logic_error("attempted to add nullptr pointer to managed argument array");
        }
        argv.insert(argv.end() - 1, strdup(str));
    }

    void add(const char* const* args) {
        if (args == nullptr) {
            throw std::logic_error("attempted to add null argument array");
        }
        for (auto arg = args; *arg != nullptr; arg++) {
            argv.insert(argv.end() - 1, strdup(*arg));
        }
    }

    void add(const ManagedArgv &other) {
        for (size_t i = 0; i < other.size() - 1; i++) {
            add(other.argv[i]);
        }
    }

    void add_front(std::string const& str) {
        argv.insert(argv.begin(), strdup(str.c_str()));
    }

    // free string at argv[index] and replace with str.
    void replace(std::size_t index, const std::string &str) {
        if (index >= argv.size() - 1) {
            throw std::out_of_range("attempted to replace managed argument out of bounds");
        }

        free(argv[index]);
        argv[index] = strdup(str.c_str());
    }

    std::string string() const {
        std::ostringstream r;

        if (argv.size() > 0)
            r << argv[0];

        // quote the arguments to ensure that anything with special characters
        // ("'&,...) is copy-pastable into a terminal
        for (size_t i = 1; i < argv.size() - 1; i++) {
            const std::string delim = " ";
            r << delim << '"' << argv[i] << '"';
        }

        return r.str();
    }
};

struct Argv {
    using GNUOption = struct option;
    static constexpr GNUOption long_options_done { nullptr, 0, nullptr, 0 };

    struct Option : public GNUOption {
        explicit constexpr Option(const char* longFlag, int shortFlag) :
            GNUOption { longFlag, no_argument, nullptr, shortFlag } {}
    };

    struct Parameter : public GNUOption {
        explicit constexpr Parameter(const char* longFlag, int shortFlag) :
            GNUOption { longFlag, required_argument, nullptr, shortFlag } {}
    };

    struct Argument : public std::string {
        Argument(std::string str) : std::string(str) {}
    };
};

template <class ArgvDef>
class OutgoingArgv : public ArgvDef {
private:
    ManagedArgv argv;

public:
    OutgoingArgv(std::string const& binary) {
        argv.add(binary);
    }

    char* const *get()    { return argv.get(); }
    ManagedArgv&& eject() { return std::move(argv); }

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
private:
    const int m_argc;
    char* const* m_argv;
    std::string m_flagSpec;
    int m_optind;

public:
    IncomingArgv(int argc, char* const* argv)
        : m_argc(argc)
        , m_argv(argv)
        , m_flagSpec{"+"} // Follow POSIX behavior, do not reorder
        , m_optind{0}
    {
        for (const Argv::GNUOption* opt_ptr = ArgvDef::long_options;
            opt_ptr->val != 0; opt_ptr++) {
            if (::isalpha(opt_ptr->val)) {
                m_flagSpec.push_back((char)(opt_ptr->val));
                if (opt_ptr->has_arg != no_argument) {
                    m_flagSpec.push_back(':');
                }
            }
        }
    }

    std::pair<int, std::string> get_next() {
        auto const old_optind = optind;
        optind = m_optind;
        int c = getopt_long(m_argc, m_argv, m_flagSpec.c_str(), ArgvDef::long_options, nullptr);
        m_optind = optind;
        optind = old_optind;
        if ((c < 0) || (optarg == nullptr)) {
            return std::make_pair(c, "");
        }
        return std::make_pair(c, optarg);
    }

    char* const* get_rest() {
        return m_argv + m_optind;
    }

    int get_rest_argc() {
        return m_argc - m_optind;
    }
};

} /* namespace cti */
