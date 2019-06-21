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
cti_barrier launches a binary, holds it at the startup barrier until
the user presses enter.
to automate: pipe from `yes`
'''
class SimpTest(Test):
    def test(self):
        process.run("./simp_test.sh", shell = True)

class CTILinkTestNoWLM(Test):
    def test(self):
        line_count = 0
        proc = subprocess.Popen("../examples/cti_link", stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        for line in iter(proc.stdout.readline, ''):
            if line.decode("utf-8") == "Current fe workload manager: No WLM detected\n" and line_count==0:
                line_count = line_count+1
                continue
            elif line.decode("utf-8") == "Current be workload manager: No WLM detected\n" and line_count==1:
                break
            else:
                self.fail("")

class CTILinkTestWLM(Test):
    def test(self):
        line_count = 0
        proc = subprocess.Popen(["./launch_functional_test.sh", "../examples/cti_link"], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        for line in iter(proc.stdout.readline, ''):
            if line.decode("utf-8") == "Current fe workload manager: Fallback (SSH based) workload manager\n" and line_count==0:
                line_count = line_count+1
                continue
            elif line.decode("utf-8") == "Current be workload manager: No WLM detected\n" and line_count==1:
                break
            else:
                self.fail("")
