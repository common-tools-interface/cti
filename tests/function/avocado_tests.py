from avocado import Test
from avocado.utils import process

EXAMPLES_PATH = "../examples"
SUPPORT_PATH  = "../support"

'''
function_tests runs all of the Googletest-instrumented functional tests
'''
class FunctionTest(Test):
	def test(self):
		process.run("./function_tests")

'''
cti_barrier launches a binary, holds it at the startup barrier until
the user presses enter.
to automate: pipe from `yes`
'''
class CtiBarrierTest(Test):
	def test(self):
		process.run("yes | %s/cti_barrier %s/one_printer"
			% (EXAMPLES_PATH, SUPPORT_PATH), shell = True)

'''
cti_launch launches a binary and prints out various information about the job.
'''
class CtiLaunchTest(Test):
	def test(self):
		process.run("%s/cti_launch %s/one_printer"
			% (EXAMPLES_PATH, SUPPORT_PATH), shell = True)

'''
cti_callback launches a binary and holds it at startup. meanwhile, it launches
the tool daemon cti_callback_daemon from PATH and ensures it that it can
communicate over a socket.
to automate: pipe from `yes` and launch with custom PATH
'''
class CtiCallbackTest(Test):
	def test(self):
		process.run("yes | PATH=$PWD/%s:$PATH %s/cti_callback %s/one_printer"
			% (EXAMPLES_PATH, EXAMPLES_PATH, SUPPORT_PATH), shell = True)

'''
cti_link tests that programs can be linked against the FE/BE libraries.
'''
class CtiLinkTest(Test):
	def test(self):
		process.run("%s/cti_link"
			% (EXAMPLES_PATH), shell = True)

