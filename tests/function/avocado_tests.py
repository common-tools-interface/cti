'''
 * Copyright 2019 Cray Inc. All Rights Reserved.
 *
 * Unpublished Proprietary Information.
 * This unpublished work is protected to trade secret, copyright and other laws.
 * Except as permitted by contract or express written permission of Cray Inc.,
 * no part of this work or its content may be used, reproduced or disclosed
 * in any form.
'''

import avocado
from avocado import Test
from avocado.utils import process

import subprocess
import os
from os import path, environ
import time

FUNCTIONAL_TESTS_PATH = path.dirname(path.realpath(__file__))
EXAMPLES_PATH  = "%s/../examples" % FUNCTIONAL_TESTS_PATH
SUPPORT_PATH   = "%s/../test_support"  % FUNCTIONAL_TESTS_PATH
CTI_INST_DIR   = os.path.expandvars('$CTI_INSTALL_DIR')
LIBEXEC_PATH   = "%s/libexec" % CTI_INST_DIR
DAEMON_VER     = "2.0.9999"

'''
cti_transfer launches a binary and holds it at startup. meanwhile, it transfers
over `testing.info` from PATH and prints a command to verify its existence on-node.
to automate: launch with custom PATH, extract and run the verification command
'''
class CtiTransferTest(Test):
    def test(self):
        proc = subprocess.Popen(["stdbuf", "-oL", "%s/cti_transfer" % EXAMPLES_PATH,
            "%s/basic_hello_mpi" % FUNCTIONAL_TESTS_PATH],
            env = dict(environ, PATH='%s:%s' % (EXAMPLES_PATH, environ['PATH'])),
            stdin = subprocess.PIPE, stdout = subprocess.PIPE, stderr = subprocess.STDOUT)
        for line in iter(proc.stdout.readline, ''):
            decoded_line = line.decode("utf-8").rstrip()
            if decoded_line[:6] == 'Verify':
                print("Sleeping...")
                # give tool daemon time to execute
                time.sleep(5)

                # run remote ls and verify testing.info is present
                process.run("%s | test -f " % (decoded_line[decoded_line.find('/'):] + "/testing.info"), shell = True)

                # end proc
                proc.stdin.write(b'\n')
                proc.stdin.flush()
                proc.stdin.close()
                proc.wait()
                break


'''
function_tests runs all of the Googletest-instrumented functional tests
'''
class FunctionTest(Test):
    def test(self):
        try :
            process.run("%s/function_tests" % FUNCTIONAL_TESTS_PATH)
        except process.CmdError:
            self.fail("Google tests failed. See log for more details")


'''
cti_barrier launches a binary, holds it at the startup barrier until
the user presses enter.
to automate: pipe from `yes`
'''
class CtiBarrierTest(Test):
    def test(self):
        process.run("yes | %s/cti_barrier %s/basic_hello_mpi"
            % (EXAMPLES_PATH, FUNCTIONAL_TESTS_PATH), shell = True)


'''
cti_launch launches a binary and prints out various information about the job.
'''
class CtiLaunchTest(Test):
    def test(self):
        process.run("%s/cti_launch %s/basic_hello_mpi"
            % (EXAMPLES_PATH, FUNCTIONAL_TESTS_PATH), shell = True)

'''
cti_callback launches a binary and holds it at startup. meanwhile, it launches
the tool daemon cti_callback_daemon from PATH and ensures it that it can
communicate over a socket.
to automate: pipe from `yes` and launch with custom PATH
'''
class CtiCallbackTest(Test):
    def test(self):
        process.run("yes | PATH=%s:$PATH %s/cti_callback %s/basic_hello_mpi"
            % (EXAMPLES_PATH, EXAMPLES_PATH, FUNCTIONAL_TESTS_PATH), shell = True)

'''
cti_link tests that programs can be linked against the FE/BE libraries.
'''
class CtiLinkTest(Test):
    def test(self):
        process.run("%s/cti_link"
            % (EXAMPLES_PATH), shell = True)


'''
cti_info fetches information about a running job.
to automate: hold a program at startup with cti_barrier, parse the job/stepid
'''
class CtiWLMTest(Test):
    def test(self):
        proc = subprocess.Popen(["stdbuf", "-oL", "%s/cti_wlm" % EXAMPLES_PATH],
            stdin = subprocess.PIPE, stdout = subprocess.PIPE, stderr = subprocess.STDOUT)
        proc_pid = proc.pid
        self.assertTrue(proc_pid is not None)
        generic = False
        slurm = False
        print("this is the CtiWLMTest, cti_wlm proc_pid %d" % proc_pid)
        while (slurm is not True and generic is not True):
            line = proc.stdout.readline().decode('utf8')
            if not line:
                break
            print(line)
            line = line.rstrip()
            if line[:7] == 'generic':
                generic = True
            elif line[:5] == 'slurm':
                slurm = True
        if slurm:
            print("CtiWLMTest, WLM type is slurm")
        elif generic:
            print("CtiWLMTest, WLM type is generic")
        else:
            print("CtiWLMTest, WLM type not detected!")
        self.assertTrue(slurm is not False or generic is not False)
        self.assertTrue(slurm is not True or generic is not True)

class CtiInfoTest(Test):
    def test(self):
        proc = subprocess.Popen(["stdbuf", "-oL", "%s/cti_wlm" % EXAMPLES_PATH],
            stdin = subprocess.PIPE, stdout = subprocess.PIPE, stderr = subprocess.STDOUT)
        proc_pid = proc.pid
        generic = False
        slurm = False
        self.assertTrue(proc_pid is not None)
        print("this is the CtiInfoTest, cti_wlm proc_pid %d" % proc_pid)
        while (slurm is not True and generic is not True):
            line = proc.stdout.readline().decode('utf8')
            if not line:
                break
            line = line.rstrip()
            if line[:5] == 'slurm':
                slurm = True
            elif line[:7] == 'generic':
                generic = True
        self.assertTrue(slurm is not False or generic is not False)
        self.assertTrue(slurm is not True or generic is not True)
        if slurm:
            print("CtiInfoTest, detected slurm WLM type, launching infoTestSLURM")
            self.infoTestSLURM()
        elif generic:
            print("CtiInfoTest, detected generic WLM type, launching infoTestSSH")
            self.infoTestSSH()

    def infoTestSLURM(self):
        proc = subprocess.Popen(["stdbuf", "-oL", "%s/cti_barrier" % EXAMPLES_PATH,
            "%s/basic_hello_mpi" % FUNCTIONAL_TESTS_PATH],
            # env = dict(environ, PATH='%s:%s' % (EXAMPLES_PATH, environ['PATH'])),
            stdin = subprocess.PIPE, stdout = subprocess.PIPE, stderr = subprocess.STDOUT)
        proc_pid = proc.pid
        self.assertTrue(proc_pid is not None)
        jobid = None
        stepid = None
        print("infoTestSLURM, cti_barrier proc_pid %d" % proc_pid)
        for line in iter(proc.stdout.readline, ''):
            print(line)
            line = line.rstrip()
            if line[:5] == 'jobid':
                jobid = line.split()[-1]
            elif line[:6] == 'stepid':
                stepid = line.split()[-1]
            if jobid is not None and stepid is not None:
                # run cti_info
                process.run("%s/cti_info --jobid=%s --stepid=%s" %
                (EXAMPLES_PATH, jobid, stepid), shell = True)
                # release barrier
                proc.stdin.write(b'\n')
                proc.stdin.flush()
                proc.stdin.close()
                proc.wait()
                break
        self.assertTrue(jobid is not None and stepid is not None)
        
    def infoTestSSH(self):
        CTI_LNCHR_NAME = None
        if "CTI_LAUNCHER_NAME" in os.environ:
            CTI_LNCHR_NAME = os.path.expandvars('$CTI_LAUNCHER_NAME')
        self.assertTrue(CTI_LNCHR_NAME is not None)
        proc = subprocess.Popen(["stdbuf", "-oL", "%s" % CTI_LNCHR_NAME,
            "%s/basic_hello_mpi_wait" % FUNCTIONAL_TESTS_PATH],
            stdin = subprocess.PIPE, stdout = subprocess.PIPE, stderr = subprocess.STDOUT)
        proc_pid = proc.pid
        self.assertTrue(proc_pid is not None)
        print("infoTestSSH, cti launcher is %s with a proc_pid of %d" % (CTI_LNCHR_NAME, proc_pid))
        if proc_pid is not None:
            print(proc_pid)
            # run cti_info
            process.run("%s/cti_info --pid=%s" % (EXAMPLES_PATH, proc_pid), shell = True)

class CTIEmptyLaunchTests(Test):
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
    def test_ws(self):
        rdt = self.params.get('run_daemon_tests', None, 1)
        if rdt == 0:
            self.cancel('Cancelled due to param run_daemon_tests set to %d' %(rdt))
        try:
            process.run("%s/cti_be_daemon%s -d '    %s'" % (LIBEXEC_PATH, DAEMON_VER, FUNCTIONAL_TESTS_PATH))
            self.fail("Process didn't error as expected")
        except process.CmdError as details:
            return 0

class CTIBEDaemonEnvTest(Test):
    def test_arg(self):
        rdt = self.params.get('run_daemon_tests', None, 1)
        if rdt == 0:
            self.cancel('Cancelled due to param run_daemon_tests set to %d' %(rdt))
        try:
            process.run("%s/cti_be_daemon%s --debug -e '    CTI_LOG_DIR=%s'" % (LIBEXEC_PATH, DAEMON_VER, FUNCTIONAL_TESTS_PATH))
            self.fail("Process didn't error as expected")
        except process.CmdError as details:
            result=os.path.isfile("./dbglog_NOAPID.-1.log")
            os.remove("./dbglog_NOAPID.-1.log")
            if result:
                return 0
            else:
                self.fail("Log file not created as expected")

class CTIBEDaemonHelpTest(Test):
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