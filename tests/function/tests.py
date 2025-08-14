'''
 * Copyright 2019-2022 Hewlett Packard Enterprise Development LP
 * SPDX-License-Identifier: Linux-OpenIB
'''

import avocado
from avocado import Test
from avocado.utils import process as avproc

import subprocess
import os
import time
import logging
import signal
import fcntl
import getpass

# these are set in readVariablesFromEnv during test setup
CTI_INST_DIR          = ""
LIBEXEC_PATH          = ""
DAEMON_VER            = ""
LAUNCHER_ARGS         = ""

def readVariablesFromEnv(test):
    global CTI_INST_DIR
    global LIBEXEC_PATH
    global DAEMON_VER
    global LAUNCHER_ARGS

    try:
        CTI_INST_DIR = os.environ['CTI_INSTALL_DIR']
    except KeyError as e:
        test.fail("Couldn't read %s from environment. Is the CTI module loaded?" % e)

    LIBEXEC_PATH   = "%s/libexec" % CTI_INST_DIR

    try:
        DAEMON_VER = os.environ['CTI_VERSION']
    except KeyError as e:
        test.fail("Couldn't read %s from environment. Is the CTI module loaded?" % e)

    try:
        LAUNCHER_ARGS = os.environ["CDST_TESTS_LAUNCHER_ARGS"]
    except KeyError as e:
        test.fail("Couldn't read %s from environment." % e)

def detectWLM(test = None):
    launch = ["src/cti_wlm"]
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
        elif line[:4] == "pals":
            wlm = "pals"
        elif line[:4] == "flux":
            wlm = "flux"
        elif line[:7] == "generic":
            wlm = "generic"

    if test:
        test.assertTrue(wlm != "", "Didn't dectect a WLM in detectWLM.")
    else:
        assert wlm != "", "Didn't dectect a WLM in detectWLM."

    return wlm

def kill_popen(p):
    if p.poll() != None:
        return # already dead

    descs = avproc.kill_process_tree(p.pid, signal.SIGTERM)
    time.sleep(3)
    for pid in descs:
        avproc.safe_kill(pid, signal.SIGKILL)

def run_cti_test(test, testname, launch_argv, env = {}, timeout_seconds=90, stdin_bytes=None):
    for key, value in env.items():
        os.environ[key] = str(value)

    tmpout = f"{os.getcwd()}/tmp/{testname}.out"

    with open(tmpout, "wb") as outf:
        p = subprocess.Popen(launch_argv,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT
        )

        try:
            out, _ = p.communicate(input=stdin_bytes, timeout=timeout_seconds)
            outf.write(out)
        except subprocess.TimeoutExpired:
            kill_popen(p)
            out, _ = p.communicate()
            outf.write(out)

            if "Safe from launch timeout" in out.decode():
                # timed out after successfully launching
                test.fail("Timed out after sucessful job launch")
            else:
                # timed out without successfully launching
                test.cancel("Timed out while launching job")

        return p.poll()

class EndTestError(Exception):
    def __init__(self, cancel_reason=None, fail_reason=None):
        self.cancel_reason = cancel_reason
        self.fail_reason = fail_reason if fail_reason is not None else "Unspecified"

class CtiTest(Test):
    def setUp(self):
        readVariablesFromEnv(self)
        try:
            os.mkdir(os.getcwd() + "/tmp")
        except OSError:
            pass

    def test_DetectWLM(self):
        name = "DetectWLM"
        argv = ["./src/cti_wlm"]

        rc = run_cti_test(self, name, argv)
        self.assertTrue(rc == 0, f"Test binary exited with non-zero returncode ({rc})")

    '''
    cti_launch launches a binary and prints out various information about the job.
    '''
    def test_CtiLaunch(self):
        name = "CtiLaunch"
        argv = ["./src/cti_launch", *LAUNCHER_ARGS.split(), "./src/support/hello_mpi_wait"]

        rc = run_cti_test(self, name, argv)
        self.assertTrue(rc == 0, f"Test binary exited with non-zero returncode ({rc})")

    def test_CtiLaunchBadenv(self):
        name = "CtiLaunchBadenv"
        argv = ["./src/cti_launch_badenv", *LAUNCHER_ARGS.split()]

        rc = run_cti_test(self, name, argv)
        self.assertTrue(rc == 0, f"Test binary exited with non-zero returncode ({rc})")

    def test_CtiBarrier(self):
        name = "CtiBarrier"
        argv = ["./src/cti_barrier", *LAUNCHER_ARGS.split(), "./src/support/hello_mpi"]

        rc = run_cti_test(self, name, argv, stdin_bytes=b"\n")
        self.assertTrue(rc == 0, f"Test binary exited with non-zero returncode ({rc})")

    def test_CtiBarrierNonMpi(self):
        name = "CtiBarrierNonMpi"
        argv = ["./src/cti_barrier", *LAUNCHER_ARGS.split(), "/usr/bin/hostname"]
        env = {
            "CTI_PALS_BARRIER_NON_MPI": "1"
        }

        rc = run_cti_test(self, name, argv, env, stdin_bytes=b"\n")
        self.assertTrue(rc == 0, f"Test binary exited with non-zero returncode ({rc})")

    '''
    cti_callback launches a binary and holds it at startup. meanwhile, it launches
    the tool daemon cti_callback_daemon from PATH and ensures it that it can
    communicate over a socket.
    '''
    def test_CtiCallback(self):
        name = "CtiCallback"
        argv = ["./src/cti_callback", *LAUNCHER_ARGS.split(), "./src/support/hello_mpi"]
        env = {
            "PATH": f"./src/:{os.getenv('PATH', default='')}"
        }

        rc = run_cti_test(self, name, argv, env)
        self.assertTrue(rc == 0, f"Test binary exited with non-zero returncode ({rc})")

    def test_DoubleDaemon(self):
        name = "DoubleDaemon"
        argv = ["./src/cti_double_daemon", *LAUNCHER_ARGS.split()]

        rc = run_cti_test(self, name, argv)
        self.assertTrue(rc == 0, f"Test binary exited with non-zero returncode ({rc})")

    def test_Environment(self):
        name = "Environment"
        argv = ["./src/cti_environment", *LAUNCHER_ARGS.split()]

        rc = run_cti_test(self, name, argv)
        self.assertTrue(rc == 0, f"Test binary exited with non-zero returncode ({rc})")

    def test_FdIn(self):
        name = "FdIn"
        argv = ["./src/cti_fd_in", *LAUNCHER_ARGS.split()]

        rc = run_cti_test(self, name, argv)
        self.assertTrue(rc == 0, f"Test binary exited with non-zero returncode ({rc})")

    def test_FileIn(self):
        name = "FileIn"
        argv = ["./src/cti_file_in", *LAUNCHER_ARGS.split()]

        rc = run_cti_test(self, name, argv)
        self.assertTrue(rc == 0, f"Test binary exited with non-zero returncode ({rc})")

    def test_Transfer(self):
        name = "Transfer"
        argv = ["./src/cti_file_transfer", *LAUNCHER_ARGS.split()]

        rc = run_cti_test(self, name, argv)
        self.assertTrue(rc == 0, f"Test binary exited with non-zero returncode ({rc})")

    def getIds(self, popen, wlm, outf):
        # set the process' stdout to be non-blocking so that iter()
        # below will exit before the process itself exits
        current_flags = fcntl.fcntl(popen.stdout.fileno(), fcntl.F_GETFL)
        fcntl.fcntl(popen.stdout.fileno(), fcntl.F_SETFL, current_flags | os.O_NONBLOCK)

        # the usage of these can differ between wlm implementations:
        #   slurm: jobid, stepid
        #   alps:  apid,  unused
        #   TODO:  support other wlms
        #
        # on wlms that only need one number, set id_two to a dummy value other
        # than None so checks like "if id_two == None then cancel" don't trigger
        id_one = None
        id_two = None

        seconds_waited = 0

        with open(outf, "wb") as outfd:
            while True:
                time.sleep(1)
                seconds_waited += 1
                if popen.poll() != None or seconds_waited >= 45:
                    break

                for line in iter(popen.stdout.readline, b''):
                    outfd.write(line)
                    line = line.decode()

                    if wlm == "slurm":
                        if line[:5] == 'jobid':
                            id_one = line.split()[-1]
                        elif line[:6] == 'stepid':
                            id_two = line.split()[-1]
                    elif wlm == "alps":
                        if line[:4] == "apid":
                            id_one = line.split()[-1]
                            id_two = "unused"

                if id_one is not None and id_two is not None:
                    break

        if id_one is None or id_two is None:
            raise EndTestError(cancel_reason="Failed to extract ids")

        return id_one, id_two

    def test_CtiInfo(self):
        try:
            cti_barrier = None
            cti_info = None

            wlm = detectWLM(self)

            cti_barrier = subprocess.Popen(
                ["./src/cti_barrier", "./src/support/hello_mpi"],
                stdin = subprocess.PIPE,
                stdout = subprocess.PIPE, stderr = subprocess.STDOUT
            )

            # get job/step id numbers. some wlms only use the first one,
            # in which case the second one will be set to a not-None dummy value
            id_one, id_two = self.getIds(cti_barrier, wlm, "./tmp/CtiInfo_Barrier.out")

            # run cti_info
            if wlm == "slurm":
                cti_info = subprocess.Popen(
                    ["./src/cti_info", f"--jobid={id_one}", f"--stepid={id_two}"],
                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL
                )
            elif wlm == "alps":
                cti_info = subprocess.Popen(
                    ["./src/cti_info", f"--apid={id_one}"],
                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL
                )
            else:
                raise EndTestError(cancel_reason=f"Test not implemented for {wlm}")

            try:
                # cti_info doesn't launch jobs and should always be fast
                cti_info.wait(15)
                if cti_info.poll() != 0:
                    raise EndTestError(
                        fail_reason=f"cti_info exited with non-zero return code ({cti_info.poll()})"
                    )
            except subprocess.TimeoutExpired:
                raise EndTestError(fail_reason="cti_info timed out")
        except EndTestError as ete:
            if ete.cancel_reason is not None:
                self.cancel(ete.cancel_reason)
            if ete.fail_reason is not None:
                self.fail(ete.fail_reason)
            self.fail("No reason specified")
        finally:
            # release cti_barrier from barrier
            try:
                if cti_barrier is not None and cti_barrier.poll() is None:
                    cti_barrier.stdin.write(b"\n")
                    cti_barrier.stdin.flush()
                    cti_barrier.wait(15)
            except subprocess.TimeoutExpired:
                # let below code kill it
                pass

            # clean up all launched apps
            if cti_barrier is not None:
                kill_popen(cti_barrier)
            if cti_info is not None:
                kill_popen(cti_info)

            # log output
            if cti_barrier is not None:
                with open("./tmp/CtiInfo_Barrier.out", "ab") as outfd:
                    out, _ = cti_barrier.communicate()
                    outfd.write(out)
            if cti_info is not None:
                with open("./tmp/CtiInfo_Info.out", "wb") as outfd:
                    out, _ = cti_info.communicate()
                    outfd.write(out)

    # old versions of this test kept for reference for when PE-44946 and PE-44947 are worked on

    # def infoTestSSH(self):
    #     CTI_LNCHR_NAME = None
    #     if "CTI_LAUNCHER_NAME" in os.environ:
    #         CTI_LNCHR_NAME = os.path.expandvars('$CTI_LAUNCHER_NAME')
    #     self.assertTrue(CTI_LNCHR_NAME is not None)
    #     proc = subprocess.Popen(["stdbuf", "-oL", "%s" % CTI_LNCHR_NAME,
    #         "%s/hello_mpi_wait" % TESTS_SRC_PATH],
    #         stdin = subprocess.PIPE, stdout = subprocess.PIPE, stderr = subprocess.STDOUT)
    #     proc_pid = proc.pid
    #     self.assertTrue(proc_pid is not None)
    #     print("infoTestSSH, cti launcher is %s with a proc_pid of %d" % (CTI_LNCHR_NAME, proc_pid))
    #     if proc_pid is not None:
    #         print(proc_pid)
    #         # run cti_info
    #         process.run("%s/cti_info --pid=%s" % (TESTS_BIN_PATH, proc_pid), shell = True)

    # def infoTestPALS(self):
    #     proc = subprocess.Popen(["stdbuf", "-oL", "%s/cti_barrier" % TESTS_BIN_PATH,
    #         "%s/hello_mpi" % TESTS_SRC_PATH],
    #         stdin = subprocess.PIPE, stdout = subprocess.PIPE, stderr = subprocess.STDOUT)
    #     proc_pid = proc.pid
    #     self.assertTrue(proc_pid is not None)
    #     apid = None
    #     for line in iter(proc.stdout.readline, b''):
    #         print(line)
    #         line = line.decode('utf-8').rstrip()
    #         if line[:4] == 'apid':
    #             apid = line.split()[-1]
    #             print("apid:", apid)

    #         if apid is not None:
    #             print("running cti_info...")
    #             # run cti_info
    #             process.run("%s/cti_info --apid=%s" % (TESTS_BIN_PATH, apid), shell = True)
    #             # release barrier
    #             proc.stdin.write(b'\n')
    #             proc.stdin.flush()
    #             proc.stdin.close()
    #             proc.wait()
    #             break
    #     self.assertTrue(apid is not None, "Couldn't determine apid")

    # def infoTestFlux(self):
    #     proc = subprocess.Popen(["stdbuf", "-oL", "%s/cti_barrier" % TESTS_BIN_PATH,
    #         "%s/hello_mpi" % TESTS_SRC_PATH],
    #         stdin = subprocess.PIPE, stdout = subprocess.PIPE, stderr = subprocess.STDOUT)
    #     proc_pid = proc.pid
    #     self.assertTrue(proc_pid is not None)
    #     jobid = None
    #     for line in iter(proc.stdout.readline, b''):
    #         print(line)
    #         line = line.decode('utf-8').rstrip()
    #         if line[:5] == 'jobid':
    #             jobid = line.split()[-1]
    #             print("jobid:", jobid)

    #         if jobid is not None:
    #             print("running cti_info...")
    #             # run cti_info
    #             process.run("%s/cti_info --jobid=%s" % (TESTS_BIN_PATH, jobid), shell = True)
    #             # release barrier
    #             proc.stdin.write(b'\n')
    #             proc.stdin.flush()
    #             proc.stdin.close()
    #             proc.wait()
    #             break
    #     self.assertTrue(jobid is not None, "Couldn't determine jobid")

    '''
    cti_mpmd is like cti_info, but it calls cti_getBinaryRankList to map binary
    names to rank IDs.
    '''
    def test_MPMD(self):
        def mpmd_answers(wlm):
            if wlm == "slurm":
                return {
                    "rank   0": " /usr/bin/echo", "rank   1": " /usr/bin/echo",
                    "rank   2": " /usr/bin/echo", "rank   3": " /usr/bin/echo",
                    "rank   4": " /usr/bin/echo", "rank   5": " /usr/bin/echo",
                    "rank   6": " /usr/bin/echo", "rank   7": " /usr/bin/sleep"
                }
            elif wlm == "alps":
                return {
                    "rank   0": " hello_mpi",     "rank   1": " hello_mpi",
                    "rank   2": " hello_mpi_alt", "rank   3": " hello_mpi_alt",
                }
            raise EndTestError(cancel_reason=f"Test not implemented for {wlm}")

        try:
            cti_barrier = None
            cti_mpmd = None

            wlm = detectWLM(self)
            answers = mpmd_answers(wlm)

            # run cti_barrier
            if wlm == "slurm":
                cti_barrier = subprocess.Popen(
                    ["./src/cti_barrier", "-n8", "-l", "--multi-prog", "./src/static/mpmd.conf"],
                    stdin = subprocess.PIPE, stdout = subprocess.PIPE, stderr = subprocess.STDOUT
                )
            elif wlm == "alps":
                cti_barrier = subprocess.Popen(
                        ["./src/cti_barrier",
                         "-n2", "./src/support/hello_mpi", ":",
                         "-n2", "./src/support/hello_mpi_alt"],
                    stdin = subprocess.PIPE, stdout = subprocess.PIPE, stderr = subprocess.STDOUT
                )
            else:
                raise EndTestError(cancel_reason="Test not implemented for {wlm}")

            # get job/step id numbers. some wlms only use the first one,
            # in which case the second one will be set to a dummy value
            # (id_two == None indicates a critical failure in getIds)
            id_one, id_two = self.getIds(cti_barrier, wlm, "./tmp/CtiMPMD_Barrier.out")

            # run cti_mpmd
            if wlm == "slurm":
                cti_mpmd = subprocess.Popen(
                    ["./src/cti_mpmd", f"--jobid={id_one}", f"--stepid={id_two}"],
                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL
                )
            elif wlm == "alps":
                cti_mpmd = subprocess.Popen(
                    ["./src/cti_mpmd", f"--apid={id_one}"],
                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL
                )
            else:
                raise EndTestError(cancel_reason="Test not implemented for {wlm}")

            try:
                with open("./tmp/CtiMPMD_MPMD.out", "wb") as outfd:
                    # cti_mpmd doesn't launch any jobs and should always be fast
                    out, _ = cti_mpmd.communicate(timeout=15)
                    outfd.write(out)

                    # test return code
                    if cti_mpmd.returncode != 0:
                        raise EndTestError(fail_reason=
                            f"cti_mpmd exited with non-zero returncode ({cti_mpmd.returncode})"
                        )

                    # test answer correctness
                    for line_ctimpmd in out.decode().split("\n"):
                        if line_ctimpmd[:4] == "rank":
                            split = line_ctimpmd.split(":")
                            lhs = split[0]
                            rhs = split[1]
                            if lhs in answers and answers[lhs] != rhs:
                                raise EndTestError(fail_reason=
                                    f"Incorrect output: {line_ctimpmd} should be {answers[lhs]}"
                                )
                            elif lhs not in answers:
                                raise EndTestError(fail_reason=
                                    f"Got an unexpected result from cti_mpmd. ({line_ctimpmd})"
                                )
                            # else correct

            except subprocess.TimeoutExpired:
                cti_mpmd.kill()
                with open("./tmp/CtiMPMD_MPMD.out", "wb") as outfd:
                    out, _ = cti_mpmd.communicate()
                    outfd.write(out)

                raise EndTestError(fail_reason="cti_mpmd timed out")
        except EndTestError as ete:
            if ete.cancel_reason is not None:
                self.cancel(ete.cancel_reason)
            if ete.fail_reason is not None:
                self.fail(ete.fail_reason)
            self.fail("No reason specified")
        finally:
            # release cti_barrier app from barrier
            try:
                if cti_barrier is not None:
                    cti_barrier.stdin.write(b"\n")
                    cti_barrier.stdin.flush()
                    cti_barrier.wait(15)
            except subprocess.TimeoutExpired:
                # let below code kill it
                pass

            # kill all launched apps
            if cti_barrier is not None:
                kill_popen(cti_barrier)
            if cti_mpmd is not None:
                kill_popen(cti_mpmd)

            # log output
            if cti_barrier is not None:
                with open("./tmp/CtiMPMD_Barrier.out", "ab") as outfd:
                    out, _ = cti_barrier.communicate()
                    outfd.write(out)

    @avocado.skipIf(lambda t: detectWLM(t) != "slurm", "MPMDAttach is only supported on slurm")
    def test_MPMDAttach(self):
        try:
            cti_barrier = None
            cti_info = None

            wlm = detectWLM(self)

            cti_barrier = subprocess.Popen(
                ["./src/cti_barrier", "-n2", "./src/support/hello_mpi",
                    ":", "-n2", "./src/support/hello_mpi_wait"],
                stdin = subprocess.PIPE,
                stdout = subprocess.PIPE, stderr = subprocess.STDOUT
            )

            # get job/step id numbers
            id_one, id_two = self.getIds(cti_barrier, wlm, "./tmp/CtiInfo_Barrier.out")

            # run cti_info
            cti_info = subprocess.Popen(
                ["./src/cti_info", f"--jobid={id_one}", f"--stepid={id_two}"],
                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL
            )

            try:
                # cti_info doesn't launch jobs and should always be fast
                cti_info.wait(15)
                if cti_info.poll() != 0:
                    raise EndTestError(
                        fail_reason=f"cti_info exited with non-zero return code ({cti_info.poll()})"
                    )
            except subprocess.TimeoutExpired:
                raise EndTestError(fail_reason="cti_info timed out")
        except EndTestError as ete:
            if ete.cancel_reason is not None:
                self.cancel(ete.cancel_reason)
            if ete.fail_reason is not None:
                self.fail(ete.fail_reason)
            self.fail("No reason specified")
        finally:
            # release cti_barrier from barrier
            try:
                if cti_barrier is not None and cti_barrier.poll() is None:
                    cti_barrier.stdin.write(b"\n")
                    cti_barrier.stdin.flush()
                    cti_barrier.wait(15)
            except subprocess.TimeoutExpired:
                # let below code kill it
                pass

            # clean up all launched apps
            if cti_barrier is not None:
                kill_popen(cti_barrier)
            if cti_info is not None:
                kill_popen(cti_info)

            # log output
            if cti_barrier is not None:
                with open("./tmp/CtiInfo_Barrier.out", "ab") as outfd:
                    out, _ = cti_barrier.communicate()
                    outfd.write(out)
            if cti_info is not None:
                with open("./tmp/CtiInfo_Info.out", "wb") as outfd:
                    out, _ = cti_info.communicate()
                    outfd.write(out)

    # def MPMDTestPALS(self):
    #     answers = {
    #         "rank   0": " hello_mpi",
    #         "rank   1": " hello_mpi",
    #         "rank   2": " hello_mpi_wait",
    #         "rank   3": " hello_mpi_wait",
    #     }

    #     proc_barrier = subprocess.Popen(["stdbuf", "-oL", "%s/cti_barrier" % TESTS_BIN_PATH,
    #         "-n2", "%s/hello_mpi" % TESTS_SRC_PATH, ":", "-n2", "%s/hello_mpi_wait" % TESTS_SRC_PATH],
    #         stdout = subprocess.PIPE, stdin = subprocess.PIPE, stderr = subprocess.STDOUT)
    #     proc_pid = proc_barrier.pid
    #     self.assertTrue(proc_pid is not None, "Couldn't start cti_barrier.")
    #     apid = None
    #     for line in iter(proc_barrier.stdout.readline, b''):
    #         line = line.decode('utf-8').rstrip()
    #         print(line)
    #         if line[:4] == 'apid':
    #             apid = line.split()[-1]
    #             print("apid:", apid)

    #         if apid is not None:
    #             print("running cti_mpmd...")

    #             # test for exit code
    #             process.run("%s/cti_mpmd --apid=%s" % (TESTS_BIN_PATH, apid), shell = True)

    #             # test for correctness
    #             proc_ctimpmd = subprocess.Popen(["stdbuf", "-oL", "%s/cti_mpmd" % TESTS_BIN_PATH,
    #                 "--apid", apid],
    #                 stdout = subprocess.PIPE, stderr = subprocess.STDOUT)

    #             for line_ctimpmd in iter(proc_ctimpmd.stdout.readline, b''):
    #                 line_ctimpmd = line_ctimpmd.decode("utf-8").rstrip()
    #                 print(line_ctimpmd)
    #                 if line_ctimpmd[:4] == "rank":
    #                     split = line_ctimpmd.split(":")
    #                     lhs = split[0]
    #                     rhs = split[1]
    #                     print("lhs:", lhs)
    #                     print("rhs:", rhs)
    #                     if lhs in answers:
    #                         self.assertTrue(answers[lhs] == rhs, "Incorrect output: %s, should be %s" % (line_ctimpmd, answers[lhs]))
    #                     else:
    #                         self.fail("Got an unexpected result from cti_mpmd.")

    #             # release barrier
    #             proc_barrier.stdin.write(b'\n')
    #             proc_barrier.stdin.flush()
    #             proc_barrier.stdin.close()
    #             proc_barrier.wait()
    #             proc_ctimpmd.wait()
    #             break

    #     self.assertTrue(apid is not None, "Couldn't determine apid")

    @avocado.skipIf(lambda t: detectWLM(t) != "slurm", "MPMDDaemon is only supported on slurm")
    def test_MPMDDaemon(self):
        name = "MPMDDaemon"
        argv = ["./src/cti_mpmd_daemon", *LAUNCHER_ARGS.split()]

        # Ensure that CTI was able to launch MPMD job
        rc = run_cti_test(self, name, argv)
        if rc == 127:
            self.cancel("Required node configuration not available")
        self.assertTrue(rc == 0, f"Test binary exited with non-zero returncode ({rc})")

    def test_CtiKillSIGTERM(self):
        name = "CtiKillSIGTERM"
        argv = ["./src/cti_kill", *LAUNCHER_ARGS.split(), "./src/support/hello_mpi_wait", str(int(signal.SIGTERM))]

        rc = run_cti_test(self, name, argv)
        self.assertTrue(rc == 0, f"Test binary exited with non-zero returncode ({rc})")

    def test_CtiKillSIGKILL(self):
        name = "CtiKillSIGKILL"
        argv = ["./src/cti_kill", *LAUNCHER_ARGS.split(), "./src/support/hello_mpi_wait", str(int(signal.SIGKILL))]

        rc = run_cti_test(self, name, argv)
        self.assertTrue(rc == 0, f"Test binary exited with non-zero returncode ({rc})")

    def test_CtiKillSIGCONT(self):
        name = "CtiKillSIGCONT"
        argv = ["./src/cti_kill", *LAUNCHER_ARGS.split(), "./src/support/hello_mpi_wait", str(int(signal.SIGCONT))]

        rc = run_cti_test(self, name, argv)
        self.assertTrue(rc == 0, f"Test binary exited with non-zero returncode ({rc})")

    def test_CtiKillSIGZERO(self):
        name = "CtiKillSIGZERO"
        argv = ["./src/cti_kill", *LAUNCHER_ARGS.split(), "./src/support/hello_mpi_wait", "0"]

        rc = run_cti_test(self, name, argv)
        self.assertTrue(rc == 0, f"Test binary exited with non-zero returncode ({rc})")

    def test_LdPreload(self):
        name = "LdPreload"
        argv = ["./src/cti_ld_preload", *LAUNCHER_ARGS.split()]

        rc = run_cti_test(self, name, argv)
        self.assertTrue(rc == 0, f"Test binary exited with non-zero returncode ({rc})")

    '''
    cti_link tests that programs can be linked against the FE/BE libraries.
    '''
    def test_Link(self):
        name = "Link"
        argv = ["./src/cti_link"]

        rc = run_cti_test(self, name, argv)
        self.assertTrue(rc == 0, f"Test binary exited with non-zero returncode ({rc})")

    def test_Manifest(self):
        name = "Manifest"
        argv = ["./src/cti_manifest", *LAUNCHER_ARGS.split()]

        rc = run_cti_test(self, name, argv)
        self.assertTrue(rc == 0, f"Test binary exited with non-zero returncode ({rc})")

    @avocado.skipIf(lambda t: detectWLM(t) != "slurm", "MPIRShim is only supported on slurm")
    def test_MPIRShim(self):
        name = "MPIRShim"
        argv = ["./src/cti_mpir_shim", *LAUNCHER_ARGS.split()]
        shim_log_path = f"{os.getcwd()}/tmp/shim.out"
        env = {
            "CTI_DEBUG": "1",
            "CTI_MPIR_SHIM_LOG_PATH": shim_log_path
        }

        # Ensure that CTI was able to launch shimmed job
        rc = run_cti_test(self, name, argv, env)
        self.assertTrue(rc == 0, f"Test binary exited with non-zero returncode ({rc})")

        # Ensure that shim ran and exited correctly
        shim_started = False
        shim_child_exited_cleanly = False
        with open(shim_log_path) as shim_log:
            for line in shim_log:
                if "cti shim token detected" in line:
                    shim_started = True
                elif "child exited" in line:
                    shim_child_exited_cleanly = True
        self.assertTrue(shim_started, f"MPIR shim was not started correctly")
        self.assertTrue(shim_child_exited_cleanly, f"MPIR shim did not exit cleanly")

    def test_Redirect(self):
        name = "Redirect"
        argv = ["./src/cti_redirect", *LAUNCHER_ARGS.split()]

        rc = run_cti_test(self, name, argv)
        self.assertTrue(rc == 0, f"Test binary returned with nonzero returncode ({rc})")

    def test_ReleaseTwice(self):
        name = "ReleaseTwice"
        argv = ["./src/cti_release_twice", *LAUNCHER_ARGS.split()]

        rc = run_cti_test(self, name, argv)
        self.assertTrue(rc == 0, f"Test binary returned with nonzero returncode ({rc})")

    def test_Session(self):
        name = "Session"
        argv = ["./src/cti_session", *LAUNCHER_ARGS.split()]

        rc = run_cti_test(self, name, argv)
        self.assertTrue(rc == 0, f"Test binary returned with nonzero returncode ({rc})")

    def test_ToolDaemon(self):
        name = "ToolDaemon"
        argv = ["./src/cti_tool_daemon", *LAUNCHER_ARGS.split()]

        rc = run_cti_test(self, name, argv)
        self.assertTrue(rc == 0, f"Test binary returned with nonzero returncode ({rc})")

    def test_ToolDaemonArgv(self):
        name = "ToolDaemonArgv"
        argv = ["./src/cti_tool_daemon_argv", *LAUNCHER_ARGS.split()]

        rc = run_cti_test(self, name, argv)
        self.assertTrue(rc == 0, f"Test binary returned with nonzero returncode ({rc})")

    def test_ToolDaemonBadenv(self):
        name = "ToolDaemonBadenv"
        argv = ["./src/cti_tool_daemon_badenv", *LAUNCHER_ARGS.split()]

        rc = run_cti_test(self, name, argv)
        self.assertTrue(rc == 0, f"Test binary returned with nonzero returncode ({rc})")

    @avocado.skipIf(lambda t: detectWLM(t) != "slurm", "Not slurm")
    def test_Ops_Slurm_getSrunInfo(self):
        name = "Ops_Slurm_getSrunInfo"
        argv = ["./src/cti_ops", "test_name:getSrunInfo", *LAUNCHER_ARGS.split()]

        rc = run_cti_test(self, name, argv)
        self.assertTrue(rc == 0, f"Test binary returned with nonzero returncode ({rc})")

    @avocado.skipIf(lambda t: detectWLM(t) != "slurm", "Not slurm")
    def test_Ops_Slurm_getJobInfo_registerJobStep(self):
        name = "Ops_Slurm_getJobInfoRegisterJobStep"
        argv = ["./src/cti_ops", "test_name:getJobInfo, registerJobStep", *LAUNCHER_ARGS.split()]

        rc = run_cti_test(self, name, argv)
        self.assertTrue(rc == 0, f"Test binary returned with nonzero returncode ({rc})")

    @avocado.skipIf(lambda t: detectWLM(t) != "slurm", "Not slurm")
    def test_Ops_Slurm_submitBatchScript(self):
        name = "Ops_Slurm_submitBatchScript"
        argv = ["./src/cti_ops", "test_name:submitBatchScript", *LAUNCHER_ARGS.split()]

        rc = run_cti_test(self, name, argv)
        self.assertTrue(rc == 0, f"Test binary returned with nonzero returncode ({rc})")

    def test_CfgCleanup(self):
        # Test that CTI properly cleans up temporary files when it exits
        name = "CfgCleanup"
        argv = ["./src/cti_tool_daemon", *LAUNCHER_ARGS.split()]

        top_dir = os.getcwd() + "/tmp"
        try:
            os.mkdir(top_dir)
        except FileExistsError:
            pass

        base_dir = f"{top_dir}/cti-{getpass.getuser()}"
        try:
            os.mkdir(base_dir)
        except FileExistsError:
            pass
        # CTI requires 0700 permissions
        os.chmod(base_dir, 0o700)

        if len(os.listdir(base_dir)) != 0:
            self.cancel(f"{base_dir} not empty before starting test")

        # Create fake leftover directory
        with open("/proc/sys/kernel/pid_max", 'r') as f:
            max_pid = [int(x) for x in f.read().split()][0]
            old_cfg_dir = f"{base_dir}/{max_pid + 1}"
        try:
            os.mkdir(old_cfg_dir)
        except FileExistsError:
            pass
        os.chmod(old_cfg_dir, 0o700)
        # Make the directory older than 5 minutes
        os.utime(old_cfg_dir, (0, 0))

        rc = run_cti_test(self, name, argv, {"CTI_CFG_DIR": top_dir})
        if rc != 0:
            self.cancel(f"Test binary returned with nonzero returncode ({rc}), can't reliably test cleanup")

        self.assertTrue(
            len(os.listdir(base_dir)) == 0,
            f"{base_dir} not empty"
        )

    def test_Multithread(self):
        name = "Multithread"
        argv = ["./src/cti_multithread", *LAUNCHER_ARGS.split()]

        rc = run_cti_test(self, name, argv)
        self.assertTrue(rc == 0, f"Test binary returned with nonzero returncode ({rc})")
