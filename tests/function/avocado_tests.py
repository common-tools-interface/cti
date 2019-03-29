from avocado import Test
from avocado.utils import process

class FunctionTest(Test):
	
	def test(self):
		process.run("./function_tests")
