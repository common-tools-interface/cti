import avocado as ac
import os, sys

# get cdst_test from resources
if os.environ.get("TEST_BASE_DIR") == None:
    print("$TEST_BASE_DIR is required")
    exit(-1)
sys.path.append(os.environ.get("TEST_BASE_DIR") + "/opt/cray/tests/cdst/resources/cti/cdst-test")
import cdst_test.modules as mods

def require_modules(test):
    if mods.MODULES_HOME == None:
        test.cancel(f"precondition failed: module command not available (no modules home)")
    if mods.MODULES_FLAVOR == None:
        test.cancel(f"precondition failed: module command not available (couldn't detect modules flavor lua/tcl/etc)")

def require_module_not_loaded(test, module):
    require_modules(test)
    loaded_modules = mods.list()
    for fullname in loaded_modules:
        if (module + "/") in fullname:
            test.cancel(f"precondition failed: {module} is already loaded ({fullname})")

def require_module_available(test, module):
    require_modules(test)
    if not mods.dry_run("load", module):
        test.cancel(f"precondition failed: {module} is not available")

def require_zero_exit_code(test, command):
    try:
        result = ac.utils.process.run(command)
    except Exception as e:
        # an exception is raised here if the command doesn't exist or if it
        # exits with non-zero. we catch it because want that to show up as a
        # CANCEL, not an ERROR
        test.cancel(f"precondition failed: {command} failed {e})")

def require_pkg_config(test):
    require_zero_exit_code(test, "pkg-config --version")

def require_build_tools(test):
    require_zero_exit_code(test, "cc --version")
    require_zero_exit_code(test, "make --version")
    require_zero_exit_code(test, "install --version")

def require_file_exists(test, file):
    if not os.path.isfile(file):
        test.cancel(f"precondition failed: file doesn't exist: {file}")

class CtiModules(ac.Test):
    def test_module_load(self):
        """
        test that we can load the cray-cti module.

        preconditions:
        1. the cray-cti module is not loaded
        2. the cray-cti module is available
        """
        require_module_not_loaded(self, "cray-cti")
        require_module_available(self, "cray-cti")

        self.assertTrue(mods.invoke_successfully("load", "cray-cti"), "`module load cray-cti` failed")

        loaded_modules_no_versions = [m[:m.find('/')] for m in mods.list()]
        self.assertTrue("cray-cti" in loaded_modules_no_versions, "cray-cti missing from module list")

class CtiPrebuilt(ac.Test):
    def setUp(self):
        """
        all prebuilt tests share these preconditions:
        1. the cray-cti module is not loaded
           (prebuilt cti binaries should work with the system cti out of the box)
        """
        require_module_not_loaded(self, "cray-cti")

        resources_dir = os.environ.get("TEST_BASE_DIR") + "/opt/cray/tests/cdst/resources/cti/smoke"
        os.chdir(resources_dir)

    def test_cti_version(self):
        """
        test that the prebuilt cti_version binary provided by the testing package works
        """
        # assuming we are already in resource dir
        require_file_exists(self, "./prebuilt/test_cti_version")
        self.assertTrue(ac.utils.process.run("./prebuilt/test_cti_version", ignore_status=True).exit_status == 0, "`./prebuilt/test_cti_version` failed")

    def test_cti_current_wlm(self):
        """
        test that the prebuilt cti_current_wlm binary provided by the testing package works
        """
        # assuming we are already in resource dir
        require_file_exists(self, "./prebuilt/test_cti_current_wlm")
        self.assertTrue(ac.utils.process.run("./prebuilt/test_cti_current_wlm", ignore_status=True).exit_status == 0, "`./prebuilt/cti_current_wlm` failed")
