#pragma once

#include "gtest/gtest.h"

#include "include/cray_tools_fe.h"

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
class CTIFEFunctionTest : public ::testing::Test
{
private:
    cti_app_id_t runningApp;

protected:
    CTIFEFunctionTest()
        : runningApp{APP_ERROR}
    {}

    ~CTIFEFunctionTest() override
    {
        if (runningApp != APP_ERROR) {
            // send sigkill to app
            if (cti_killApp(runningApp, SIGKILL) != SUCCESS) {
                std::cerr << "warning: failed to kill app on test cleanup" << std::endl;
            }

            // force deregister app
            cti_deregisterApp(runningApp);
        }
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
};
