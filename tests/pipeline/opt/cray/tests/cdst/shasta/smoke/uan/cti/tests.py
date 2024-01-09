import avocado as ac
import os, sys

# get cdst_test from resources
if os.environ.get("TEST_BASE_DIR") == None:
    print("$TEST_BASE_DIR is required")
    exit(-1)
sys.path.append(os.environ.get("TEST_BASE_DIR") + "/opt/cray/tests/cdst/resources/cti/cdst-test")
import cdst_test.modules as mods

def require_module_not_loaded(test, module):
    loaded_modules = mods.list()
    for fullname in loaded_modules:
        if (module + "/") in fullname:
            test.cancel(f"precondition failed: {module} is already loaded ({fullname})")

def require_module_available(test, module):
    if not mods.dry_run("load", module):
        test.cancel(f"precondition failed: {module} is not available")

def require_zero_exit_code(test, command):
    try:
        result = ac.utils.process.run(command)
    except Exception as e:
        # an exception is raised here if pkg-config doesn't exist or if it
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

    def test_module_load_devel(self):
        """
        test that we can load the cray-cti-devel module and that it correctly loads the cray-cti module.
        
        preconditions:
        1. the cray-cti-devel and cray-cti modules are not loaded
        2. the cray-cti-devel and cray-cti modules are available
        """
        require_module_not_loaded(self, "cray-cti")
        require_module_not_loaded(self, "cray-cti-devel")
        require_module_available(self, "cray-cti")
        require_module_available(self, "cray-cti-devel")

        self.assertTrue(mods.invoke_successfully("load", "cray-cti-devel"), "`module load cray-cti-devel` failed")

        loaded_modules_no_versions = [m[:m.find('/')] for m in mods.list()]
        self.assertTrue("cray-cti-devel" in loaded_modules_no_versions, "cray-cti-devel missing from module list")
        self.assertTrue("cray-cti" in loaded_modules_no_versions, "cray-cti missing from module list")

        loaded_cti_modules = mods.list("cray-cti")
        version = None
        for mod in loaded_cti_modules:
            if "cray-cti-devel" in mod:
                version = mod[mod.find("/"):]

        self.assertTrue(f"cray-cti-devel{version}" in mods.list(), f"cray-cti-devel{version} missing from module list")
        self.assertTrue(f"cray-cti{version}" in mods.list(), f"cray-cti{version} missing from module list")

    def test_devel_pkg_config(self):
        """
        test that loading cray-cti-devel exposes pkg-config functionality
        
        preconditions:
        1. pkg-config is present
        2. the cray-cti-devel module is not loaded
        3. the cray-cti-devel module is available
        4. pkg-config common_tools_fe returns nothing
        """
        require_pkg_config(self)
        require_module_not_loaded(self, "cray-cti-devel")
        require_module_available(self, "cray-cti-devel")
        if ac.utils.process.run("pkg-config common_tools_fe", ignore_status=True).exit_status == 0:
            self.cancel("pkg-config common_tools_fe worked before the test")

        self.assertTrue(mods.invoke_successfully("load", "cray-cti-devel"), "`module load cray-cti-devel` failed")
        self.assertTrue(ac.utils.process.run("pkg-config common_tools_fe", ignore_status=True).exit_status == 0, "`pkg-config common_tools_fe` failed after loading cray-cti-devel")
        self.assertTrue(ac.utils.process.run("pkg-config common_tools_be", ignore_status=True).exit_status == 0, "`pkg-config common_tools_be` failed after loading cray-cti-devel")

    def test_build(self):
        """
        test that loading cray-cti-devel lets us build a cti app

        preconditions:
        1. pkg-config is present
        2. cc and make are present
        3. the cray-cti-devel module is not loaded
        4. the cray-cti-devel module is available
        5. pkg-config common_tools_fe returns nothing
        """
        require_pkg_config(self)
        require_build_tools(self)
        require_module_not_loaded(self, "cray-cti-devel")
        require_module_available(self, "cray-cti-devel")
        if ac.utils.process.run("pkg-config common_tools_fe", ignore_status=True).exit_status == 0:
            self.cancel("pkg-config common_tools_fe worked before the test")

        resources_dir = os.environ.get("TEST_BASE_DIR") + "/opt/cray/tests/cdst/resources/cti/smoke"

        os.chdir(resources_dir)
        self.assertTrue(mods.invoke_successfully("load", "cray-cti-devel"), "`module load cray-cti-devel` failed")
        self.assertTrue(ac.utils.process.run("make test_build", ignore_status=True).exit_status == 0, "`make test_build` failed")
        self.assertTrue(ac.utils.process.run("./bin/test_build", ignore_status=True).exit_status == 0, "`./bin/test_build` failed")

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
