#include "cti_fe_function_test.hpp"
#include <unistd.h>
#include <fstream>
#include <signal.h>
#include <filesystem>

// tests for functions available from cti_open_ops

void testSlurm_getSrunInfo(cti_slurm_ops_t* slurm_ops, int argc, char* argv[]) {
    auto appArgv = createSystemArgv(argc, argv, {"./src/support/hello_mpi"});
    auto app = CTIFEFunctionTest{};
    auto appId = app.watchApp(
        cti_launchAppBarrier(cstrVector(appArgv).data(), -1, -1, nullptr, nullptr, nullptr)
    );

    assert_true(appId > 0, cti_error_str());
    assert_true(cti_appIsValid(appId), cti_error_str());
    std::cerr << "Safe from launch timeout.\n";

    auto* srun_proc = slurm_ops->getSrunInfo(appId);
    assert_true(srun_proc != nullptr, cti_error_str());
    assert_true(srun_proc->jobid != 0, "jobid is 0");
    // a stepid of 0 is valid. there is no way to inherently check the correctness of
    // a stepid without matching it with the actual value output from squeue.

    free(srun_proc);
    srun_proc = nullptr;
}

void testSlurm_getJobInfoRegisterJobStep(cti_slurm_ops_t* slurm_ops, int argc, char* argv[]) {
    // launch a job with srun (not using cti), then use getJobInfo and registerJobStep
    // to attach to it and read info

    if (auto slurm_pid = ::fork()) {
        assert_true(slurm_pid > 0, "fork failed");

        struct Cleanup {
            Cleanup(pid_t pid) : m_pid{pid} {}
            ~Cleanup() {::kill(m_pid, SIGKILL);}
            pid_t m_pid;
        };
        auto cleanup = Cleanup(slurm_pid);

        std::cerr << "slurm_pid is " << slurm_pid << std::endl;

        cti_srunProc_t* srun_proc = nullptr;

        // try a few times, srun will take some time to launch the job
        for (int tries_left = 5; tries_left > 0; tries_left--) {
            std::cerr << tries_left << " tries left " << std::endl;
            srun_proc = slurm_ops->getJobInfo(slurm_pid);
            if (srun_proc == nullptr) {
                std::cerr << "getJobInfo returned nullptr" << std::endl;
                sleep(3);
                continue;
            }
            break;
        }

        assert_true(srun_proc != nullptr, "failed to get srun_proc");
        std::cerr << "Safe from launch timeout.\n";

        auto appId = slurm_ops->registerJobStep(srun_proc->jobid, srun_proc->stepid);
        assert_true(appId != 0, "registerJobStep returned 0");
        assert_true(cti_appIsValid(appId), "cti_appIsValid returned 0");
    } else {
        auto appArgv = createSystemArgv(argc, argv, {"./src/support/hello_mpi_wait"});
        appArgv.insert(appArgv.begin(), "srun");
        std::cerr << "execvp ";
        for (auto s : appArgv) std::cerr << s << ", ";
        std::cerr << std::endl;

        ::close(1); // to avoid polluting the output file
        ::close(2);
        ::execvp("srun", const_cast<char**>(cstrVector(appArgv).data()));
        exit(1);
    }
}

void testSlurm_submitBatchScript(cti_slurm_ops_t* slurm_ops, int argc, char* argv[]) {
    auto appArgv = createSystemArgv(argc, argv, {"./src/support/hello_mpi_wait"});
    appArgv.insert(appArgv.begin(), "srun");

    std::filesystem::create_directory("./tmp");

    auto batch = std::ofstream("./tmp/test_sbatch");
    assert_true(batch.is_open(), "failed to open sbatch file");

    // write batch script
    batch << "#!/bin/bash" << std::endl;

    // #SBATCH options skip "srun" and "./src/support/hello_mpi_wait"
    for (int i = 1; i < appArgv.size() - 1; i++)
        batch << "#SBATCH " << appArgv[i] << std::endl;

    // launch line
    for (auto str : appArgv) batch << str << " ";
    batch << std::endl;

    batch.close();

    cti_srunProc_t* srun_proc = slurm_ops->submitBatchScript("./tmp/test_sbatch", nullptr, nullptr);
    assert_true(srun_proc != nullptr, cti_error_str());

    auto appId = slurm_ops->registerJobStep(srun_proc->jobid, srun_proc->stepid);
    std::cerr << "Safe from launch timeout.\n";
    assert_true(appId != 0, "reigsterJobStep returned 0");
    assert_true(cti_appIsValid(appId), "cti_appIsValid returned 0");

    // try to clean up. don't check result of this, it's not part of what we're testing
    cti_killApp(appId, SIGKILL);
}

int testAlpsOps() {
    assert_true(false, "unimplemented");
    return -1; }

int testSshOps() {
    assert_true(false, "unimplemented");
    return -1;
}

int testPalsOps() {
    assert_true(false, "unimplemented");
    return -1;
}

int testFluxOps() {
    assert_true(false, "unimplemented");
    return -1;
}

int testLocalhostOps() {
    assert_true(false, "unimplemented");
    return -1;
}

int main(int argc, char* argv[]) {
    // pick a test to run. if "all", all tests for a given wlm are run
    const auto test_name_key = "test_name:";
    if (argc < 2 || strncmp(test_name_key, argv[1], strlen(test_name_key)) != 0) {
        std::cerr << "Pick a test by passing \"test_name:<name>\" as the first argument.\n";
        std::cerr << "Pass \"all\" to run all tests available for the system wlm.\n";
        return -1;
    }

    using namespace std::string_literals;

    const auto test_name = argv[1] + strlen(test_name_key);
    const auto run_all_tests = "all"s == test_name;

    std::cerr << "running test " << test_name << std::endl;

    try {
        switch (cti_current_wlm()) {
        case CTI_WLM_SLURM: {
            cti_slurm_ops_t* slurm_ops = nullptr;

            reportTime("cti_open_ops", [&](){
                auto ops_wlm = cti_open_ops((void**)&slurm_ops);
                assert_true(ops_wlm == CTI_WLM_SLURM,
                    "cti_open_ops returned other WLM than slurm");
                assert_true(slurm_ops != nullptr,
                    "cti_open_ops did not set ops pointer");
            });

            if ("getSrunInfo"s == test_name || run_all_tests)
                reportTime("getSrunInfo", [&](){ testSlurm_getSrunInfo(slurm_ops, argc - 1, &argv[1]); });

            if ("getJobInfo, registerJobStep"s == test_name || run_all_tests)
                reportTime("getJobInfo, registerJobStep", [&](){ testSlurm_getJobInfoRegisterJobStep(slurm_ops, argc - 1, &argv[1]); });

            if ("submitBatchScript"s == test_name || run_all_tests)
                reportTime("submitBatchScript", [&](){ testSlurm_submitBatchScript(slurm_ops, argc - 1, &argv[1]); });

            break;
        }
        case CTI_WLM_ALPS: return testAlpsOps();
        case CTI_WLM_SSH: return testSshOps();
        case CTI_WLM_PALS: return testPalsOps();
        case CTI_WLM_FLUX: return testFluxOps();
        case CTI_WLM_LOCALHOST: return testLocalhostOps();
        case CTI_WLM_NONE:
            assert_true(false, "failed to detect wlm");
            return -1;
        default:
            assert_true(false, "fell out of switch - no test was run.");
            return -1;
        }
    } catch (...) {
        // catch exceptions to ensure destructors run
        throw;
    }
}
