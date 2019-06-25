'''
 * Copyright 2019 Cray Inc.  All Rights Reserved.
 *
 * Unpublished Proprietary Information.
 * This unpublished work is protected to trade secret, copyright and other laws.
 * Except as permitted by contract or express written permission of Cray Inc.,
 * no part of this work or its content may be used, reproduced or disclosed
 * in any form.
'''

from avocado import Test
from avocado.utils import process

import subprocess
from os import path, environ
import time

FUNCTIONAL_TESTS_PATH = path.dirname(path.realpath(__file__))
EXAMPLES_PATH = "%s/../examples" % FUNCTIONAL_TESTS_PATH
SUPPORT_PATH  = "%s/../test_support"  % FUNCTIONAL_TESTS_PATH

'''
function_tests runs all of the Googletest-instrumented functional tests

class FunctionTest(Test):
	def test(self):
		process.run("%s/function_tests" % FUNCTIONAL_TESTS_PATH)
'''

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
			print(line)
			if line[:4] == 'srun':
				# give tool daemon time to execute
				time.sleep(5)

				# run remote ls and verify testing.info is present
				process.run("%s | grep -q testing.info" % line.rstrip(), shell = True)

				# end proc
				proc.stdin.write(b'\n')
				proc.stdin.flush()
				proc.stdin.close()
				proc.wait()
				break
			elif line.decode("utf-8") == '':
				self.fail("CTi_Transfer: Incorrect STDOUT result")

'''
cti_info fetches information about a running job.
to automate: hold a program at startup with cti_barrier, parse the job/stepid
'''
class CtiInfoTest(Test):
	def test(self):
		proc = subprocess.Popen(["stdbuf", "-oL", "%s/cti_barrier" % EXAMPLES_PATH,
			"%s/basic_hello_mpi" % FUNCTIONAL_TESTS_PATH],
			# env = dict(environ, PATH='%s:%s' % (EXAMPLES_PATH, environ['PATH'])),
			stdin = subprocess.PIPE, stdout = subprocess.PIPE, stderr = subprocess.STDOUT)
		proc_pid = proc.pid
		if proc_pid is not None:
			print(proc_pid)
			time.sleep(4)
			# run cti_info
			process.run("%s/cti_info --pid %s" %
			(EXAMPLES_PATH, proc_pid), shell = True)

			# release barrier
			#proc.stdin.write(b'\n')
			#proc.stdin.flush()
			#proc.stdin.close()
			#proc.wait()
		self.assertTrue(proc_pid is not None)
		'''
		jobid = None
		stepid = None
		for line in iter(proc.stdout.readline, ''):
			print(line)
			line = line.rstrip()
			if line[:5] == 'jobid':
				jobid = line.split()[-1]
			elif line[:6] == 'stepid':
				stepid = line.split()[-1]

			if jobid is not None and stepid is not None:
				# run cti_info
				process.run("%s/cti_info --jobid %s --stepid %s" %
				(EXAMPLES_PATH, jobid, stepid), shell = True)

				# release barrier
				proc.stdin.write(b'\n')
				proc.stdin.flush()
				proc.stdin.close()
				proc.wait()
				break
		self.assertTrue(jobid is not None and stepid is not None)
		'''
