import reframe as rfm
import reframe.utility.sanity as sn

from cpe_reframe import CpeReframeBase

import os

class CTIBuildMixin(rfm.RegressionMixin):
    # this mixin loads cray-cti-devel before the build step and unloads it after the build step.
    #
    # because of how CpeReframeBase works, when inheriting this class, it needs
    # to be inherited *before* CpeReframeBase.
    @run_before("compile")
    def load_build_modules(self):
        self.modules.append("cray-cti-devel")
        self.build_system.cflags = [
            '`pkg-config common_tools_fe --cflags --libs`',
            '`pkg-config common_tools_be --cflags --libs`',
        ]
        self.build_system.cxxflags = [
            '`pkg-config common_tools_fe --cflags --libs`',
            '`pkg-config common_tools_be --cflags --libs`',
        ]
        self.build_system.flags_from_environ = True

    @run_after("compile")
    def unload_build_modules(self):
        # XXX: does this also unload cray-cti? if so, what is the app actually
        #      linking against? the system default?
        self.modules.remove("cray-cti-devel")

class BuildHelloMpiWait(CTIBuildMixin, rfm.CompileOnlyRegressionTest):
    build_system = 'SingleSource'
    sourcepath = 'hello_mpi_wait.c'
    executable = './hello_mpi_wait.x'

class BuildCtiLaunch(CTIBuildMixin, rfm.CompileOnlyRegressionTest):
    build_system = 'SingleSource'
    sourcepath = 'cti_launch_test.c cti_fe_common.c'
    executable = './cti_launch.x'

class BuildCtiLaunchBarrier(CTIBuildMixin, rfm.CompileOnlyRegressionTest):
    build_system = 'SingleSource'
    sourcepath = 'cti_barrier_test.c cti_fe_common.c'
    executable = './cti_barrier.x'

class CtiLauncher(rfm.core.launchers.JobLauncher):
    def __init__(self, path):
        super().__init__()
        self.path = path

    def command(self, job):
        return [self.path]

@rfm.simple_test
class CtiLaunch(rfm.RunOnlyRegressionTest, CpeReframeBase):
    # use cti_launchApp to try to launch some applications on every partition on the system

    valid_systems = ['*']
    valid_prog_environs = ['cray']

    build_system = 'CustomBuild' # all building is done through fixtures

    hello_mpi_wait = fixture(BuildHelloMpiWait, scope='environment')
    cti_launch = fixture(BuildCtiLaunch, scope='environment')

    ranks = parameter([1, 16, 32])
    nodes = parameter([1, 2])

    @run_before("compile")
    def set_up_build(self):
        self.build_system.commands = []
        self.job.launcher = CtiLauncher(os.path.join(self.cti_launch.stagedir, self.cti_launch.executable))

    @run_before("run")
    def set_up_test(self):
        self.executable = os.path.join(self.hello_mpi_wait.stagedir, self.hello_mpi_wait.executable)
        self.num_tasks = self.ranks
        self.num_tasks_per_node = max(1, self.num_tasks // self.nodes)

    @sanity_function
    def validate(self):
        return sn.all([
            sn.assert_eq(self.job.exitcode, 0, "Binary had non zero exit code"),
        ])

@rfm.simple_test
class LaunchBarrier(rfm.RegressionTest, CTIBuildMixin, CpeReframeBase):
    # launch an application with cti_launchAppBarrier on every partition

    valid_systems = ['*']
    valid_prog_environs = ['cray']

    build_system = 'CustomBuild' # all building is done through fixtures

    hello_mpi_wait = fixture(BuildHelloMpiWait, scope='environment')
    cti_launch_barrier = fixture(BuildCtiLaunchBarrier, scope='environment')

    ranks = parameter([1, 16, 32])
    nodes = parameter([1, 2])

    @run_before("compile")
    def set_up_build(self):
        self.build_system.commands = []
        self.job.launcher = CtiLauncher(os.path.join(self.cti_launch_barrier.stagedir, self.cti_launch_barrier.executable))

    @run_before("run")
    def set_up_test(self):
        self.executable = os.path.join(self.hello_mpi_wait.stagedir, self.hello_mpi_wait.executable)
        self.executable_opts = ['<', '/usr/bin/yes']
        self.num_tasks = self.ranks
        self.num_tasks_per_node = max(1, self.num_tasks // self.nodes)

    @sanity_function
    def validate(self):
        return sn.all([
            sn.assert_eq(self.job.exitcode, 0, "Binary had non zero exit code"),
        ])

@rfm.simple_test
class DetectSlurm(rfm.RegressionTest, CTIBuildMixin, CpeReframeBase):
    # run cti_current_wlm and check that it returns slurm on slurm machines

    valid_systems = ['%scheduler=slurm']
    valid_prog_environs = ['cray']
    local = True

    build_system = 'SingleSource'
    sourcepath = 'cti_wlm_test.c'
    executable = './cti_wlm_test.x'

    @sanity_function
    def validate(self):
        return sn.all([
            sn.assert_found('slurm', self.stdout, "did not detect slurm"),
            sn.assert_eq(self.job.exitcode, 0, "Binary had non zero exit code"),
        ])

