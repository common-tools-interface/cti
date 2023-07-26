/*
 * Copyright 2019-2021 Hewlett Packard Enterprise Development LP.
 * SPDX-License-Identifier: Linux-OpenIB
 */

#pragma once

#include <string>
#include <sstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <vector>
#include <functional>

#include <sys/socket.h>
#include <netinet/in.h>
#include <ifaddrs.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <netdb.h>
#include <signal.h>

#include <string.h>
#include <unistd.h>

#include "common_tools_fe.h"

static constexpr auto SUCCESS = int{0};
static constexpr auto FAILURE = int{1};

static constexpr auto APP_ERROR = cti_app_id_t{0};

// generate a temporary file and remove it on destruction
class temp_file_handle
{
private:
    std::unique_ptr<char, decltype(&::free)> m_path;

public:
    temp_file_handle(std::string const& templ)
        : m_path{strdup(templ.c_str()), ::free}
    {
        // use template to generate filename
        mktemp(m_path.get());
        if (m_path.get()[0] == '\0') {
            throw std::runtime_error("mktemp failed");
        }
    }

    temp_file_handle(temp_file_handle&& moved)
        : m_path{std::move(moved.m_path)}
    {
        moved.m_path.reset();
    }

    ~temp_file_handle()
    {
        // TODO: Log the warning if this fails.
        if( m_path ) {
            remove(m_path.get());
        }
    }

    char const* get() const { return m_path.get(); }
};

// The fixture for testing C interface function
class CTIFEFunctionTest
{
private:
    cti_app_id_t runningApp;

public:
    CTIFEFunctionTest()
        : runningApp{APP_ERROR}
    {}

    void stopApp() {
        if (runningApp != APP_ERROR) {
            // send sigkill to app
            if (cti_killApp(runningApp, SIGKILL) != SUCCESS) {
                std::cerr << "warning: failed to kill app on test cleanup" << std::endl;
            }

            // force deregister app
            cti_deregisterApp(runningApp);

            runningApp = APP_ERROR;
        }
    }

    ~CTIFEFunctionTest()
    {
        stopApp();
    }

    // note the running app ID so that we can clean it up later
    cti_app_id_t watchApp(cti_app_id_t appId)
    {
        if (runningApp == APP_ERROR) {
            runningApp = appId;
            return runningApp;
        } else {
            throw std::logic_error("assigned multiple apps to a test");
        }
    }

    cti_app_id_t replaceApp(cti_app_id_t appId)
    {
        stopApp();
        return watchApp(appId);
    }

};

void assert_true(bool condition, std::string error);

// take a vector of strings and prepend the system specific arguements to it
std::vector<std::string> createSystemArgv(int argc, char* mainArgv[], const std::vector<std::string>& appArgv);

// take a vector of strings, copy their c_str() pointers to a new vector,
// and add a nullptr at the end. the return value can then be used in
// ctiLaunchApp and similar via "return_value.data()"
std::vector<const char*> cstrVector(const std::vector<std::string> &v);

// Find my external IP
std::string getExternalAddress();

int bindAny(std::string const& address);

std::tuple<cti_app_id_t, int> launchSocketApp(char const* appPath,
    std::vector<char const*> extra_argv);
void testSocketApp(cti_app_id_t app_id, int test_socket,
    std::string const& expecting, int times);

void testSocketDaemon(cti_session_id_t sessionId, char const* daemonPath,
    std::vector<char const*> extra_argv, std::vector<char const*> extra_env,
    std::string const& expecting, int times=1);

// run `fn` and report how long it took to run. output will be tagged with `name`
void reportTime(std::string name, std::function<void()> fn);
