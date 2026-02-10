#include "cti_fe_function_test.hpp"
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/wait.h>

#include <fstream>
#include <filesystem>

#include "cti_fe_common.h"

// tests for functions available from cti_open_ops

// Use GDB to get job ID during prolog execution
static auto getSlurmJobId(pid_t pid)
{
    auto result = std::pair<int32_t, int32_t>{};
    int gdb_pipe[2];
    assert_true(::pipe(gdb_pipe) == 0, "Failed to create pipe");

    // Launch GDB subprocess
    if (auto gdb_pid = fork()) {
        ::close(gdb_pipe[1]);
        auto stdout = ::fdopen(gdb_pipe[0], "r");
        assert_true(stdout != nullptr, "Failed to open pipe for reading");

        char buf[512];
        ::memset(buf, '\0', sizeof(buf));

        // Extract variable result lines
        auto parse_val = [](std::string& line) {

            // Remove trailing newline
            if (line.back() == '\n') {
                line.pop_back();
            }
            fprintf(stderr, "GDB output: '%s'\n", line.c_str());

            // Unquoted string value
            auto id_start = line.rfind(" ");
            if ((id_start == std::string::npos) || (id_start + 2 >= line.length())) {
                return int32_t{};
            }
            auto id = line.substr(id_start + 2);
            if (id.empty() || (id.back() != '"')) {
                return int32_t{};
            }
            id.pop_back();

            // Parse as int
            try {
                return std::stoi(id);
            } catch (std::exception const& ex) {
                return int32_t{};
            }
        };

        // Get values for totalview_jobid and totalview_stepid
        while (::fgets(buf, sizeof(buf) - 1, stdout)) {
            auto line = std::string{buf};

            // totalview_jobid
            if (line.rfind("$1 = ", 0) == 0) {
                result.first = parse_val(line);

            // totalview_stepid
            } else if (line.rfind("$2 = ", 0) == 0) {
                result.second = parse_val(line);
            }
        }

        // Discard rest of GDB output
        while (::fgets(buf, sizeof(buf) - 1, stdout)) {}

        ::fclose(stdout);
        ::close(gdb_pipe[0]);

        int status = 0;
        while (true) {
            auto const rc = ::waitpid(gdb_pid, &status, 0);
            if (rc < 0) {
                if (errno == EINTR) {
                    continue;
                } else {
                    assert_true(false, "waitpid() on " + std::to_string(gdb_pid) + "failed");
                }
            } else {
                break;
            }
        }

    } else {
        // Create GDB arguments to print job and step IDs, then exit
        auto pidStr = std::to_string(pid);
        char const* gdb_argv[] = {
            "gdb", "-p", pidStr.c_str(),
            "-ex", "p totalview_jobid",
            "-ex", "p totalview_stepid",
            "-ex", "set confirm off",
            "-ex", "exit",
            nullptr
        };

        ::close(gdb_pipe[0]);
        ::dup2(gdb_pipe[1], STDOUT_FILENO);
        ::execvp("gdb", (char**)gdb_argv);
        ::perror("gdb");
        ::exit(-1);
    }

    return result;
}

void testSlurmAttachWait(int argc, char** argv)
{
    cti_slurm_ops_t* slurm_ops = nullptr;

    auto ops_wlm = cti_open_ops((void**)&slurm_ops);
    assert_true(ops_wlm == CTI_WLM_SLURM,
        "cti_open_ops returned other WLM than slurm");
    assert_true(slurm_ops != nullptr,
        "cti_open_ops did not set ops pointer");

    if (auto slurm_pid = ::fork()) {
        assert_true(slurm_pid > 0, "fork failed");

        struct Cleanup {
            Cleanup(pid_t pid) : m_pid{pid} {}
            ~Cleanup() {::kill(m_pid, SIGKILL);}
            pid_t m_pid;
        };
        auto cleanup = Cleanup(slurm_pid);

        std::cerr << "slurm_pid is " << slurm_pid << std::endl;

        // Extract job and step IDs using GDB
        // While the job prolog is running, the application is assigned a job ID,
        // but the proctable information is not yet filled out.
        // When the prolog finishes and the job starts, MPIR_Breakpoint will be
        // called and the proctable available for a full attach.
        // This will set a long-running prolog, get the job ID programmatically
        // via GDB, then test waiting attach.
        int32_t job_id, step_id;
        for (int tries_left = 5; tries_left > 0; tries_left--) {
            std::cerr << tries_left << " tries left " << std::endl;
            std::tie(job_id, step_id) = getSlurmJobId(slurm_pid);
            if (job_id == 0) {
                sleep(2);
                continue;
            }
            break;
        }

        assert_true(job_id != 0, "Could not extract job ID from srun process");

        std::cerr << "Safe from launch timeout.\n";

        auto appId = slurm_ops->registerJobStepWait(job_id, step_id, 30);
        assert_true(appId != 0, "registerJobStepWait returned 0");
        assert_true(cti_appIsValid(appId), "cti_appIsValid returned 0");
        cti_test_fe(appId);
        cti_deregisterApp(appId);

    } else {
        auto appArgv = createSystemArgv(argc, argv, {"./src/support/hello_mpi_wait"});
        appArgv.insert(appArgv.begin(), "--prolog=./src/support/sleeper.sh");
        appArgv.insert(appArgv.begin(), "srun");
        std::cerr << "execvp ";
        for (auto s : appArgv) std::cerr << s << ", ";
        std::cerr << std::endl;

        ::close(1);
        ::close(2);
        ::execvp("srun", const_cast<char**>(cstrVector(appArgv).data()));
        ::exit(1);
    }
}

int main(int argc, char** argv) {

    try {
        switch (cti_current_wlm()) {
        case CTI_WLM_SLURM: {
            testSlurmAttachWait(argc, argv);
            return 0;
        }
        case CTI_WLM_NONE: {
            assert_true(false, "failed to detect wlm");
            return -1;
        }
        default: {
            fprintf(stderr, "Unimplemented for %s\n", cti_wlm_type_toString(cti_current_wlm()));
            return 0;
        }
        }
    } catch (...) {
        // catch exceptions to ensure destructors run
        throw;
    }
}
