/*********************************************************************************\
 * cti_argv.hpp: Interface for handling argv.
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
#pragma once

#include <vector>
#include <string>
#include <stdexcept>

#include <getopt.h>
#include <string.h>

namespace cti {

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
            if (str) { free(str); }
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

    void add(std::string const& str) {
        argv.insert(argv.end() - 1, strdup(str.c_str()));
    }

    void add(const char* str) {
        if (str == nullptr) {
            throw std::logic_error("attempted to add nullptr pointer to managed argument array");
        }
        argv.insert(argv.end() - 1, strdup(str));
    }

    void add(const char* const args[]) {
        if (args == nullptr) {
            throw std::logic_error("attempted to add null argument array");
        }
        for (auto arg = args; *arg != nullptr; arg++) {
            argv.insert(argv.end() - 1, strdup(*arg));
        }
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
            m_flagSpec.push_back((char)(opt_ptr->val));
            if (opt_ptr->has_arg != no_argument) {
                m_flagSpec.push_back(':');
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
};

} /* namespace cti */
