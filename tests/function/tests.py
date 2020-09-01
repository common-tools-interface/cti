'''
 * Copyright 2019-2020 Hewlett Packard Enterprise Development LP
 *
 * Unpublished Proprietary Information.
 * This unpublished work is protected to trade secret, copyright and other laws.
 * Except as permitted by contract or express written permission of Hewlett
 * Packard Enterprise Development LP., no part of this work or its content may be
 * used, reproduced or disclosed in any form.
'''

import avocado
from avocado import Test
from avocado.utils import process

import subprocess
import os
import time
import logging

# these are set in readVariablesFromEnv during test setup
TESTS_BIN_PATH        = ""
TESTS_SRC_PATH        = ""
SUPPORT_PATH          = ""
CTI_INST_DIR          = ""
LIBEXEC_PATH          = ""
DAEMON_VER            = ""
LAUNCHER_ARGS         = ""
WLM                   = ""

def readVariablesFromEnv(test):
    global TESTS_BIN_PATH
    global TESTS_SRC_PATH
    global SUPPORT_PATH
    global CTI_INST_DIR
    global LIBEXEC_PATH
    global DAEMON_VER
    global LAUNCHER_ARGS

    TESTS_BIN_PATH  = "%s/bin" % os.path.dirname(os.path.realpath(__file__))
    TESTS_SRC_PATH  = "%s/src" % os.path.dirname(os.path.realpath(__file__))
    SUPPORT_PATH   = "%s/../test_support"  % os.path.dirname(os.path.realpath(__file__))

    try:
        CTI_INST_DIR = os.environ['CTI_INSTALL_DIR']
    except KeyError as e:
        test.error("Couldn't read %s from environment. Is the CTI module loaded?" % e)

    LIBEXEC_PATH   = "%s/libexec" % CTI_INST_DIR

    try:
        DAEMON_VER = os.environ['CTI_VERSION']
    except KeyError as e:
        test.error("Couldn't read %s from environment. Is the CTI module loaded?" % e)

    try:
        LAUNCHER_ARGS = os.environ["CDST_TESTS_LAUNCHER_ARGS"]
    except KeyError as e:
        test.error("Couldn't read %s from environment." % e)


# depends on TESTS_BIN_PATH being set first first
def detectWLM(test = None):
    global WLM

    if WLM != "":
        return WLM

    if test:
        test.assertTrue(TESTS_BIN_PATH != "", "No TESTS_BIN_PATH when detecting WLM. Did you call readVariablesFromEnv?")
    else:
        assert(TESTS_BIN_PATH != "", "No TESTS_BIN_PATH when detecting WLM. Did you call readVariablesFromEnv?")

    launch = ["stdbuf", "-oL", "%s/cti_wlm" % TESTS_BIN_PATH]
    print("Launching: " + " ".join(launch))
    proc = subprocess.Popen(launch,
        stdin = subprocess.PIPE, stdout = subprocess.PIPE, stderr = subprocess.STDOUT)
    proc_pid = proc.pid

    if test:
        test.assertTrue(proc_pid is not None)
    else:
        assert(proc_pid is not None)

    wlm = ""

    while (wlm == ""):
        line = proc.stdout.readline().decode('utf8')
        if not line:
            break
        line = line.rstrip()
        if line[:5] == "slurm":
            wlm = "slurm"
        elif line[:4] == "alps":
            wlm = "alps"
        elif line[:7] == "generic":
            wlm = "generic"

    if test:
        test.assertTrue(wlm != "", "Didn't dectect a WLM in detectWLM.")
    else:
        assert(wlm != "", "Didn't dectect a WLM in detectWLM.")

    WLM = wlm
    return WLM

#Note: if you want to skip a test but run the suite, add the folloing line at the top of the class definition:
#       @avocado.skip("<optional comment here>")

'''
function_tests runs all of the Googletest-instrumented functional tests
'''
class GTestFunctionTest(Test):
    def setUp(self):
        readVariablesFromEnv(self)
        # chdir needed for relative paths in the function test binary to be correct.
        # avocado runs each test in its own process so we don't need to change back.
        os.chdir("%s" % TESTS_BIN_PATH)

    def test_DaemonLibDir(self):
        testname = "DaemonLibDir"
        try:
            process.run("%s/function_tests \"%s\" --gtest_filter=CTIFEFunctionTest.%s" % (TESTS_BIN_PATH, LAUNCHER_ARGS, testname))
        except process.CmdError:
            self.fail("Google test %s failed." % testname)

    def test_HaveValidFrontend(self):
        testname = "HaveValidFrontend"
        try:
            process.run("%s/function_tests \"%s\" --gtest_filter=CTIFEFunctionTest.%s" % (TESTS_BIN_PATH, LAUNCHER_ARGS, testname))
        except process.CmdError:
            self.fail("Google test %s failed." % testname)

    def test_LdPreloadSet(self):
        testname = "LdPreloadSet"
        try:
            process.run("%s/function_tests \"%s\" --gtest_filter=CTIFEFunctionTest.%s" % (TESTS_BIN_PATH, LAUNCHER_ARGS, testname))
        except process.CmdError:
            self.fail("Google test %s failed." % testname)

    def test_Launch(self):
        testname = "Launch"
        try:
            process.run("%s/function_tests \"%s\" --gtest_filter=CTIFEFunctionTest.%s" % (TESTS_BIN_PATH, LAUNCHER_ARGS, testname))
        except process.CmdError:
            self.fail("Google test %s failed." % testname)

    def test_DoubleRelease(self):
        testname = "DoubleRelease"
        try:
            process.run("%s/function_tests \"%s\" --gtest_filter=CTIFEFunctionTest.%s" % (TESTS_BIN_PATH, LAUNCHER_ARGS, testname))
        except process.CmdError:
            self.fail("Google test %s failed." % testname)

    def test_StdoutPipe(self):
        testname = "StdoutPipe"
        try:
            process.run("%s/function_tests \"%s\" --gtest_filter=CTIFEFunctionTest.%s" % (TESTS_BIN_PATH, LAUNCHER_ARGS, testname))
        except process.CmdError:
            self.fail("Google test %s failed." % testname)

    def test_InputFile(self):
        testname = "InputFile"
        try:
            process.run("%s/function_tests \"%s\" --gtest_filter=CTIFEFunctionTest.%s" % (TESTS_BIN_PATH, LAUNCHER_ARGS, testname))
        except process.CmdError:
            self.fail("Google test %s failed." % testname)

    def test_EnvVars(self):
        testname = "EnvVars"
        try:
            process.run("%s/function_tests \"%s\" --gtest_filter=CTIFEFunctionTest.%s" % (TESTS_BIN_PATH, LAUNCHER_ARGS, testname))
        except process.CmdError:
            self.fail("Google test %s failed." % testname)

    def test_CreateSession(self):
        testname = "CreateSession"
        try:
            process.run("%s/function_tests \"%s\" --gtest_filter=CTIFEFunctionTest.%s" % (TESTS_BIN_PATH, LAUNCHER_ARGS, testname))
        except process.CmdError:
            self.fail("Google test %s failed." % testname)

    def test_CreateManifest(self):
        testname = "CreateManifest"
        try:
            process.run("%s/function_tests \"%s\" --gtest_filter=CTIFEFunctionTest.%s" % (TESTS_BIN_PATH, LAUNCHER_ARGS, testname))
        except process.CmdError:
            self.fail("Google test %s failed." % testname)

    def test_ExecToolDaemon(self):
        testname = "ExecToolDaemon"
        try:
            process.run("%s/function_tests \"%s\" --gtest_filter=CTIFEFunctionTest.%s" % (TESTS_BIN_PATH, LAUNCHER_ARGS, testname))
        except process.CmdError:
            self.fail("Google test %s failed." % testname)

    def test_Transfer(self):
        testname = "Transfer"
        try:
            process.run("%s/function_tests \"%s\" --gtest_filter=CTIFEFunctionTest.%s" % (TESTS_BIN_PATH, LAUNCHER_ARGS, testname))
        except process.CmdError:
            self.fail("Google test %s failed." % testname)

'''
cti_barrier launches a binary, holds it at the startup barrier until
the user presses enter.
to automate: pipe from `yes`
'''
class CtiBarrierTest(Test):
    def setUp(self):
        readVariablesFromEnv(self)

    def test(self):
        process.run("yes | %s/cti_barrier %s %s/hello_mpi"
            % (TESTS_BIN_PATH, LAUNCHER_ARGS, TESTS_SRC_PATH), shell = True)


'''
cti_launch launches a binary and prints out various information about the job.
'''
class CtiLaunchTest(Test):
    def setUp(self):
        readVariablesFromEnv(self)

    def test(self):
        process.run("%s/cti_launch %s %s/hello_mpi_wait"
            % (TESTS_BIN_PATH, LAUNCHER_ARGS, TESTS_SRC_PATH), shell = True)

'''
cti_kill launches a binary and then immediately sends a SIGTERM to it.
'''
class CtiKillTest(Test):
    def setUp(self):
        readVariablesFromEnv(self)

    def test(self):
        process.run("%s/cti_kill %s %s/hello_mpi_wait"
            % (TESTS_BIN_PATH, LAUNCHER_ARGS, TESTS_SRC_PATH), shell = True)

'''
cti_callback launches a binary and holds it at startup. meanwhile, it launches
the tool daemon cti_callback_daemon from PATH and ensures it that it can
communicate over a socket.
to automate: pipe from `yes` and launch with custom PATH
'''
class CtiCallbackTest(Test):
    def setUp(self):
        readVariablesFromEnv(self)

    def test(self):
        process.run("yes | PATH=%s:$PATH %s/cti_callback %s %s/hello_mpi"
            % (TESTS_BIN_PATH, TESTS_BIN_PATH, LAUNCHER_ARGS, TESTS_SRC_PATH), shell = True)

'''
cti_link tests that programs can be linked against the FE/BE libraries.
'''
class CtiLinkTest(Test):
    def setUp(self):
        readVariablesFromEnv(self)

    def test(self):
        if "LD_LIBRARY_PATH" in os.environ:
            print("LD_LIBRARY_PATH from os.environ %s" % os.path.expandvars('$LD_LIBRARY_PATH'))
        else:
            print("LD_LIBRARY_PATH not defined!")
        process.run("%s/cti_link"
            % (TESTS_BIN_PATH), shell = True)


'''
cti_wlm fetches the work load manager type about a running job.
'''
class CtiWLMTest(Test):
    def setUp(self):
        readVariablesFromEnv(self)

    def test(self):
        detectWLM(self)

'''
    cti_info fetches information about a running job. There are two versions
    of the test, one which uses the SLURM wlm, and launches a hello world test
    via the cti_barrier test; and another that uses the generic (SSH) wlm, and
    launches a hello world program that sleeps for 100 seconds or until the
    info test kills the mpi call.
'''
class CtiInfoTest(Test):
    def setUp(self):
        readVariablesFromEnv(self)

    def test(self):
        wlm = detectWLM(self)
        if wlm == "slurm":
            self.infoTestSLURM()
        elif wlm == "alps":
            self.infoTestALPS()
        elif wlm == "generic":
            self.infoTestSSH()
        else:
            self.error("Unsupported WLM!")

    def infoTestSLURM(self):
        proc = subprocess.Popen(["stdbuf", "-oL", "%s/cti_barrier" % TESTS_BIN_PATH,
            "%s/hello_mpi" % TESTS_SRC_PATH],
            stdin = subprocess.PIPE, stdout = subprocess.PIPE, stderr = subprocess.STDOUT)
        proc_pid = proc.pid
        self.assertTrue(proc_pid is not None)
        jobid = None
        stepid = None
        print("infoTestSLURM, cti_barrier proc_pid %d" % proc_pid)
        for line in iter(proc.stdout.readline, b''):
            print(line)
            line = line.decode('utf-8').rstrip()
            if line[:5] == 'jobid':
                jobid = line.split()[-1]
                print("jobid:", jobid)
            elif line[:6] == 'stepid':
                stepid = line.split()[-1]
                print("stepid:", stepid)
            if jobid is not None and stepid is not None:
                print("running cti_info...")
                # run cti_info
                process.run("%s/cti_info --jobid=%s --stepid=%s" %
                (TESTS_BIN_PATH, jobid, stepid), shell = True)
                # release barrier
                proc.stdin.write(b'\n')
                proc.stdin.flush()
                proc.stdin.close()
                proc.wait()
                break
        self.assertTrue(jobid is not None, "Couldn't determine jobid")
        self.assertTrue(stepid is not None, "Couldn't determine stepid")

    def infoTestALPS(self):
        proc = subprocess.Popen(["stdbuf", "-oL", "%s/cti_barrier" % TESTS_BIN_PATH,
            "%s/hello_mpi" % TESTS_SRC_PATH],
            stdin = subprocess.PIPE, stdout = subprocess.PIPE, stderr = subprocess.STDOUT)
        proc_pid = proc.pid
        self.assertTrue(proc_pid is not None)
        apid = None
        for line in iter(proc.stdout.readline, b''):
            print(line)
            line = line.decode('utf-8').rstrip()
            if line[:4] == 'apid':
                apid = line.split()[-1]
                print("apid:", apid)

            if apid is not None:
                print("running cti_info...")
                # run cti_info
                process.run("%s/cti_info --apid=%s" % (TESTS_BIN_PATH, apid), shell = True)
                # release barrier
                proc.stdin.write(b'\n')
                proc.stdin.flush()
                proc.stdin.close()
                proc.wait()
                break
        self.assertTrue(apid is not None, "Couldn't determine apid")

    def infoTestSSH(self):
        CTI_LNCHR_NAME = None
        if "CTI_LAUNCHER_NAME" in os.environ:
            CTI_LNCHR_NAME = os.path.expandvars('$CTI_LAUNCHER_NAME')
        self.assertTrue(CTI_LNCHR_NAME is not None)
        proc = subprocess.Popen(["stdbuf", "-oL", "%s" % CTI_LNCHR_NAME,
            "%s/hello_mpi_wait" % TESTS_SRC_PATH],
            stdin = subprocess.PIPE, stdout = subprocess.PIPE, stderr = subprocess.STDOUT)
        proc_pid = proc.pid
        self.assertTrue(proc_pid is not None)
        print("infoTestSSH, cti launcher is %s with a proc_pid of %d" % (CTI_LNCHR_NAME, proc_pid))
        if proc_pid is not None:
            print(proc_pid)
            # run cti_info
            process.run("%s/cti_info --pid=%s" % (TESTS_BIN_PATH, proc_pid), shell = True)

'''
    cti_mpmd is like cti_info, but it calls cti_getBinaryRankList to map binary
    names to rank IDs.
'''
class CtiMPMDTest(Test):
    def setUp(self):
        readVariablesFromEnv(self)

    def test(self):
        wlm = detectWLM(self)
        if wlm == "slurm":
            self.MPMDTestSLURM()
        elif wlm == "alps":
            self.MPMDTestALPS()
        else:
            self.error("Unsupported WLM!")

    def MPMDTestSLURM(self):
        answers = {
            "rank   0": " /usr/bin/echo",
            "rank   1": " /usr/bin/echo",
            "rank   2": " /usr/bin/echo",
            "rank   3": " /usr/bin/echo",
            "rank   4": " /usr/bin/echo",
            "rank   5": " /usr/bin/echo",
            "rank   6": " /usr/bin/echo",
            "rank   7": " /usr/bin/sleep"
        }

        proc_barrier = subprocess.Popen(["stdbuf", "-oL", "%s/cti_barrier" % TESTS_BIN_PATH,
            "-n8", "-l", "--multi-prog", "./src/mpmd.conf"],
            stdout = subprocess.PIPE, stdin = subprocess.PIPE, stderr = subprocess.STDOUT)
        proc_pid = proc_barrier.pid
        self.assertTrue(proc_pid is not None, "Couldn't start cti_barrier.")
        jobid = None
        stepid = None
        for line in iter(proc_barrier.stdout.readline, b''):
            line = line.decode('utf-8').rstrip()
            print(line)
            if line[:5] == 'jobid':
                jobid = line.split()[-1]
                print("jobid:", jobid)
            elif line[:6] == 'stepid':
                stepid = line.split()[-1]
                print("stepid:", stepid)
            if jobid is not None and stepid is not None:
                print("running cti_mpmd...")

                # test for exit code
                process.run("%s/cti_mpmd --jobid=%s --stepid=%s" % (TESTS_BIN_PATH, jobid, stepid), shell = True)

                # test for correctness
                proc_ctimpmd = subprocess.Popen(["stdbuf", "-oL", "%s/cti_mpmd" % TESTS_BIN_PATH,
                    "-j", jobid, "-s", stepid],
                    stdout = subprocess.PIPE, stderr = subprocess.STDOUT)

                for line_ctimpmd in iter(proc_ctimpmd.stdout.readline, b''):
                    line_ctimpmd = line_ctimpmd.decode("utf-8").rstrip()
                    print(line_ctimpmd)
                    if line_ctimpmd[:4] == "rank":
                        split = line_ctimpmd.split(":")
                        lhs = split[0]
                        rhs = split[1]
                        print("lhs:", lhs)
                        print("rhs:", rhs)
                        if lhs in answers:
                            self.assertTrue(answers[lhs] == rhs, "Incorrect output: %s, should be %s" % (line_ctimpmd, answers[lhs]))
                        else:
                            self.error("Got an unexpected result from cti_mpmd.")

                # release barrier
                proc_barrier.stdin.write(b'\n')
                proc_barrier.stdin.flush()
                proc_barrier.stdin.close()
                proc_barrier.wait()
                proc_ctimpmd.wait()
                break

        self.assertTrue(jobid is not None, "Couldn't determine jobid")
        self.assertTrue(stepid is not None, "Couldn't determine stepid")

    def MPMDTestALPS(self):
        answers = {
            "rank   0": " hello_mpi",
            "rank   1": " hello_mpi",
            "rank   2": " hello_mpi_wait",
            "rank   3": " hello_mpi_wait",
        }

        proc_barrier = subprocess.Popen(["stdbuf", "-oL", "%s/cti_barrier" % TESTS_BIN_PATH,
            "-n2", "%s/hello_mpi" % TESTS_SRC_PATH, ":", "-n2", "%s/hello_mpi_wait" % TESTS_SRC_PATH],
            stdout = subprocess.PIPE, stdin = subprocess.PIPE, stderr = subprocess.STDOUT)
        proc_pid = proc_barrier.pid
        self.assertTrue(proc_pid is not None, "Couldn't start cti_barrier.")
        apid = None
        for line in iter(proc_barrier.stdout.readline, b''):
            line = line.decode('utf-8').rstrip()
            print(line)
            if line[:4] == 'apid':
                apid = line.split()[-1]
                print("apid:", apid)

            if apid is not None:
                print("running cti_mpmd...")

                # test for exit code
                process.run("%s/cti_mpmd --apid=%s" % (TESTS_BIN_PATH, apid), shell = True)

                # test for correctness
                proc_ctimpmd = subprocess.Popen(["stdbuf", "-oL", "%s/cti_mpmd" % TESTS_BIN_PATH,
                    "--apid", apid],
                    stdout = subprocess.PIPE, stderr = subprocess.STDOUT)

                for line_ctimpmd in iter(proc_ctimpmd.stdout.readline, b''):
                    line_ctimpmd = line_ctimpmd.decode("utf-8").rstrip()
                    print(line_ctimpmd)
                    if line_ctimpmd[:4] == "rank":
                        split = line_ctimpmd.split(":")
                        lhs = split[0]
                        rhs = split[1]
                        print("lhs:", lhs)
                        print("rhs:", rhs)
                        if lhs in answers:
                            self.assertTrue(answers[lhs] == rhs, "Incorrect output: %s, should be %s" % (line_ctimpmd, answers[lhs]))
                        else:
                            self.error("Got an unexpected result from cti_mpmd.")

                # release barrier
                proc_barrier.stdin.write(b'\n')
                proc_barrier.stdin.flush()
                proc_barrier.stdin.close()
                proc_barrier.wait()
                proc_ctimpmd.wait()
                break

        self.assertTrue(apid is not None, "Couldn't determine apid")

class CTIEmptyLaunchTests(Test):
    def setUp(self):
        readVariablesFromEnv(self)

    def test_be(self):
        rdt = self.params.get('run_daemon_tests', None, 1)
        if rdt == 0:
            self.cancel('Cancelled due to param run_daemon_tests set to %d' %(rdt))

        try:
            process.run("%s/cti_be_daemon%s" % (LIBEXEC_PATH, DAEMON_VER))
            self.fail("Process didn't error as expected")
        except process.CmdError as details:
            return 0
    def test_fe(self):
        rdt = self.params.get('run_daemon_tests', None, 1)
        if rdt == 0:
            self.cancel('Cancelled due to param run_daemon_tests set to %d' %(rdt))

        try:
            process.run("%s/cti_fe_daemon%s" % (LIBEXEC_PATH, DAEMON_VER))
            self.fail("Process didn't error as expected")
        except process.CmdError as details:
            return 0

class CTIBEDaemonAPIDTest(Test):
    def setUp(self):
        readVariablesFromEnv(self)

    def test_ws(self):
        rdt = self.params.get('run_daemon_tests', None, 1)
        if rdt == 0:
            self.cancel('Cancelled due to param run_daemon_tests set to %d' %(rdt))
        try:
            process.run("%s/cti_be_daemon%s -a '    wspace'" % (LIBEXEC_PATH, DAEMON_VER))
            self.fail("Process didn't error as expected")
        except process.CmdError as details:
            return 0

class CTIBEDaemonBinaryTest(Test):
    def setUp(self):
        readVariablesFromEnv(self)

    def test_ws(self):
        rdt = self.params.get('run_daemon_tests', None, 1)
        if rdt == 0:
            self.cancel('Cancelled due to param run_daemon_tests set to %d' %(rdt))
        try:
            process.run("%s/cti_be_daemon%s -b '    ../test_support/one_print'" % (LIBEXEC_PATH, DAEMON_VER))
            self.fail("Process didn't error as expected")
        except process.CmdError as details:
            print(details)
            return 0

class CTIBEDaemonDirectoryTest(Test):
    def setUp(self):
        readVariablesFromEnv(self)

    def test_ws(self):
        rdt = self.params.get('run_daemon_tests', None, 1)
        if rdt == 0:
            self.cancel('Cancelled due to param run_daemon_tests set to %d' %(rdt))
        try:
            process.run("%s/cti_be_daemon%s -d '    %s'" % (LIBEXEC_PATH, DAEMON_VER, TESTS_BIN_PATH))
            self.fail("Process didn't error as expected")
        except process.CmdError as details:
            return 0

class CTIBEDaemonEnvTest(Test):
    def setUp(self):
        readVariablesFromEnv(self)

    def test_arg(self):
        rdt = self.params.get('run_daemon_tests', None, 1)
        if rdt == 0:
            self.cancel('Cancelled due to param run_daemon_tests set to %d' %(rdt))
        try:
            process.run("%s/cti_be_daemon%s --debug -e '    CTI_LOG_DIR=%s'" % (LIBEXEC_PATH, DAEMON_VER, TESTS_BIN_PATH))
            self.fail("Process didn't error as expected")
        except process.CmdError as details:
            result=os.path.isfile("%s/dbglog_NOAPID.-1.log" % TESTS_BIN_PATH)
            os.remove("%s/dbglog_NOAPID.-1.log" % TESTS_BIN_PATH)
            if result:
                return 0
            else:
                self.fail("Log file not created as expected")

class CTIBEDaemonHelpTest(Test):
    def setUp(self):
        readVariablesFromEnv(self)

    def test(self):
        rdt = self.params.get('run_daemon_tests', None, 1)
        if rdt == 0:
            self.cancel('Cancelled due to param run_daemon_tests set to %d' %(rdt))
        try:
            process.run("%s/cti_be_daemon%s --help" % (LIBEXEC_PATH, DAEMON_VER))
            self.fail("Process didn't error as expected")
        except process.CmdError as details:
            return 0

class CTIBEDaemonManifestTest(Test):
    def setUp(self):
        readVariablesFromEnv(self)

    def test_ws(self):
        rdt = self.params.get('run_daemon_tests', None, 1)
        if rdt == 0:
            self.cancel('Cancelled due to param run_daemon_tests set to %d' %(rdt))
        try:
            process.run("%s/cti_be_daemon%s -m '    ./'" % (LIBEXEC_PATH, DAEMON_VER))
            self.fail("Process didn't error as expected")
        except process.CmdError as details:
            return 0

class CTIBEDaemonPathTest(Test):
    def setUp(self):
        readVariablesFromEnv(self)

    def test_ws(self):
        rdt = self.params.get('run_daemon_tests', None, 1)
        if rdt == 0:
            self.cancel('Cancelled due to param run_daemon_tests set to %d' %(rdt))
        try:
            process.run("%s/cti_be_daemon%s -p '    ./'" % (LIBEXEC_PATH, DAEMON_VER))
            self.fail("Process didn't error as expected")
        except process.CmdError as details:
            return 0

class CTIBEDaemonAPATHTest(Test):
    def setUp(self):
        readVariablesFromEnv(self)

    def test_ws(self):
        rdt = self.params.get('run_daemon_tests', None, 1)
        if rdt == 0:
            self.cancel('Cancelled due to param run_daemon_tests set to %d' %(rdt))
        try:
            process.run("%s/cti_be_daemon%s -t '    ./'" % (LIBEXEC_PATH, DAEMON_VER))
            self.fail("Process didn't error as expected")
        except process.CmdError as details:
            return 0

class CTIBEDaemonLDPathTest(Test):
    def setUp(self):
        readVariablesFromEnv(self)

    def test_ws(self):
        rdt = self.params.get('run_daemon_tests', None, 1)
        if rdt == 0:
            self.cancel('Cancelled due to param run_daemon_tests set to %d' %(rdt))

        try:
            process.run("%s/cti_be_daemon%s -l '    ./'" % (LIBEXEC_PATH, DAEMON_VER))
            self.fail("Process didn't error as expected")
        except process.CmdError as details:
            return 0

class CTIBeDaemonWLMTest(Test):
    def setUp(self):
        readVariablesFromEnv(self)

    def test_WLM_NONE(self):
        rdt = self.params.get('run_daemon_tests', None, 1)
        if rdt == 0:
            self.cancel('Cancelled due to param run_daemon_tests set to %d' %(rdt))

        try:
            process.run("%s/cti_be_daemon%s -w CTI_WLM_NONE" % (LIBEXEC_PATH, DAEMON_VER))
            self.fail("Process didn't error as expected")
        except process.CmdError as details:
            return 0
    def test_WLM_INVALID(self):
        rdt = self.params.get('run_daemon_tests', None, 1)
        if rdt == 0:
            self.cancel('Cancelled due to param run_daemon_tests set to %d' %(rdt))

        try:
            process.run("%s/cti_be_daemon%s -w 12345" % (LIBEXEC_PATH, DAEMON_VER))
            self.fail("Process didn't error as expected")
        except process.CmdError as details:
            return 0
    def test_WLM_VALID(self):
        rdt = self.params.get('run_daemon_tests', None, 1)
        if rdt == 0:
            self.cancel('Cancelled due to param run_daemon_tests set to %d' %(rdt))

        try:
            process.run("%s/cti_be_daemon%s -w 3" % (LIBEXEC_PATH, DAEMON_VER))
            self.fail("Process didn't error as expected")
        except process.CmdError as details:
            return 0

class CTIBeDaemonInvalidArgTest(Test):
    def setUp(self):
        readVariablesFromEnv(self)

    def test_null(self):
        rdt = self.params.get('run_daemon_tests', None, 1)
        if rdt == 0:
            self.cancel('Cancelled due to param run_daemon_tests set to %d' %(rdt))

        try:
            process.run("%s/cti_be_daemon%s -Z" % (LIBEXEC_PATH, DAEMON_VER))
            self.fail("Process didn't error as expected")
        except process.CmdError as details:
            return 0

class CTIBeDaemonNoDirTool(Test):
    def setUp(self):
        readVariablesFromEnv(self)

    def test_no_dir(self):
        rdt = self.params.get('run_daemon_tests', None, 1)
        if rdt == 0:
            self.cancel('Cancelled due to param run_daemon_tests set to %d' %(rdt))

        try:
            process.run("%s/cti_be_daemon%s -w 3 -a 1" % (LIBEXEC_PATH, DAEMON_VER))
            self.fail("Process didn't error as expected")
        except process.CmdError as details:
            return 0
    def test_no_tool(self):
        rdt = self.params.get('run_daemon_tests', None, 1)
        if rdt == 0:
            self.cancel('Cancelled due to param run_daemon_tests set to %d' %(rdt))

        try:
            process.run("%s/cti_be_daemon%s -w 3 -a 1 -d ./test" % (LIBEXEC_PATH, DAEMON_VER))
            self.fail("Process didn't error as expected")
        except process.CmdError as details:
            return 0
