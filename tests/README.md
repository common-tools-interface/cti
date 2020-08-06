# Automated Tests

## Running

To run all functional tests, execute:
```
# run all tests
$ ./test_tool_starter.py
```
The `test_tool_starter.py` script is meant to be an easy way to run functional tests.
```
usage: test_tool_starter.py [-h] [-t TEST] [-c LOCATION] [-s] [-f] [-r]
                            [-y FILE]

Run functional tests

optional arguments:
  -h, --help            show this help message and exit
  -t TEST, --test TEST  run a single test. if this option isn't used, all
                        tests are run. can be used multiple times to run
                        multiple tests.
  -c LOCATION, --cp LOCATION
                        copy tests to another location.
  -s, --skip-build      skip the build step.
  -f, --force           force tests to run, even if a check fails.
  -r, --remove, --clean
                        remove local files that get created when this tool is
                        run.
  -y FILE, --yaml FILE  specificy a yaml file with extra config. default is
                        test_tool_config.yaml.
```

To run a single test, execute:
```
# run a single test
$ ./test_tool_starter.py -t <test name>
# example:
$ ./test_tool_starter.py -t CtiBarrierTest
```

The actual test names can be found in `./function/tests.py`. Just use the
names of the python classes/methods.

By default, the test script will rebuild test programs upon each run. To skip
this, use the `--skip-build` or `-s` option:
```
# skip the build step
$ ./test_tool_starter.py --skip-build
# or
$ ./test_tool_starter.py -s
```

In order to write the results of the test to disk, `test_tool_starter.py` needs to be
in a directory where you have write permissions. To copy test data to a directory
where you have write permissions, execute:
```
# copy tests to another directory
$ ./test_tool_starter.py --cp <destination>
# or
$ ./test_tool_starter.py -c <destination>
```

Some of these options can be combined:
```
# skip the build step and run the CtiBarrierTest test
$ ./test_tool_starter.py -s -t CtiBarrierTest
```

## System Specific Configuration

`test_tool_config.yaml` defines how to build and run tests. In addition, it can define system-specific configuration. For example, arguments for WLMs are defined in the `launch` section:

```
# launcher config
launch:
  args:
    slurm: "-n4 --ntasks-per-node=2"
    alps: "-n4 -N2"
    ssh: "-n4 --ntasks-per-node=2"
```

On a system that needs additional or different launcher arguments, you can add a new section that is the specific system's hostname and override them. For a system called `cfa6k`, `test_tool_config.yaml` might look like this:

```
# launcher config
launch:
  args:
    slurm: "-n4 --ntasks-per-node=2"
    alps: "-n4 -N2"
    ssh: "-n4 --ntasks-per-node=2"

# system specific overides
cfa6k:
  launch:
    args:
      slurm: "-n4 --ntasks-per-node=2 --mpi=cray_shasta"
```

This will have the effect of using the new arguments if the test tool is run on a machine called `cfa6k`. Otherwise, the custom arguments are ignored and the defaults are used.

Only specified options will be changed. If an option isn't specified, the default is used.

## Relationship to cdst-support

The actual testing functionality and framework is provided by `cray-cdst-support-devel`. It needs to be loaded in some way for the test tool to be able to run tests. To find the testing library, `test_tool_starter.py` looks in the following places in the following order:

1. If defined, the location specified by the `CRAY_CDST_SUPPORT_INSTALL_DIR` 
environment variable
    * This can be set by loading the `cray-cdst-support` module or by manually setting it and pointing to a dev repository of `cdst-support`.
2. `/opt/cray/pe/cdst-support/default`