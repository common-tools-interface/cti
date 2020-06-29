# Functional Tests

Test source code is in `./tests`. They can be executed with the `build_run.sh`
script in this directory.

`build_run.sh` is smart enough to make sure you have avocado/python3/etc set up,
but it is not smart enough to build the tests for you. For a smarter script, use
`test_tool.sh` in the parent directory.

That being said...

To run all of the tests:
```
./build_run.sh
```

To run a single test:
```
./build_run.sh <test name>
# example
./build_run.sh CtiBarrierTest
```

`avocado_tests.py` expects some evironment variables to be set:
```
# CTI meta stuff; these should be set for you when you load the cray-cti (or cray-cti-devel) module
CTI_INSTALL_DIR
CTI_VERSION

# launcher args to pass to srun/aprun/etc. these are set via ../scripts/system_specific_setup by build_run.sh, but you can also set them yourself or just set the environment variable to an empty string
CTI_TESTS_LAUNCHER_ARGS
# some examples:
export CTI_TESTS_LAUNCHER_ARGS=""
export CTI_TESTS_LAUNCHER_ARGS="-n2 --ntasks-per-node=1" # slurm
export CTI_TESTS_LAUNCHER_ARGS="-n2 --ntasks-per-node=1 --mpi=cray_shasta" # slurm
export CTI_TESTS_LAUNCHER_ARGS="-n2 -N1" # alps
```

## Whitebox quirks

The `MPICH_SMP_SINGLE_COPY_OFF` environment variable, set in `build_run.sh` when running tests on whiteboxes, instructs our mpi implementation not to use SMP optimizations which are not supported on whiteboxes. you don't need to know the details, I know them because I spent three years working on programming model implementations. xpmem allows a process to arbitrarily attach to memory windows in another process. shared memory segments on steroids. arbitrary load/store access across process address spaces- very dangerous, requires kernel mods. Linux was like ffffff that we are not taking. so we need to apply them to our os'es. whiteboxes are not considered supported os'es for xpmem. same with login nodes, so they are missing patches. when you fail to set `MPICH_SMP_SINGLE_COPY_OFF`, its going to try preemptively mapping segments via xpmem and fail. its an abort type failure. setting it prevents that error from happening, but performance is negatively impacted but who cares, we are on a whitebox. basically the idea is we don't want Mr. Scientist to run their app in such a way that they would miss out on 20% or more performance improvements so we abort, users need to go out of their way to say "yes I know this is going to kill performance. Do it anyway.".

Whitebox related environment variables:

```
# instructs our mpi implementation not to use SMP optimizations which are not supported on whiteboxes
export MPICH_SMP_SINGLE_COPY_OFF=1
# set an environment variable for the launcher:
export CTI_LAUNCHER_NAME=/opt/cray/pe/snplauncher/default/bin/mpiexec
# ... or just load the module
module load cray-snplauncher
# and also set the workload manager type environment variable.
export CTI_WLM_IMPL=generic
```
