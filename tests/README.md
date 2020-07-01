# Automated Tests

To run all functional tests, execute:
```
# run all tests
$ ./test_tool.sh
```
The `test_tool.sh` script is meant to be an easy way to run functional tests.
```
$ ./test_tool --help
    ./test_tool.sh [options]
    [-t|--test] <test name> : run a single test. if this option isn't used, all tests are run.
    [-c|--cp] <directory>   : copy tests to a directory.
    [-s|--skip-build]       : skip the build step.
    [-h|--help]             : show this help message.
```

To run a single test, execute:
```
# run a single test
$ ./test_tool.sh -t <test name>
# example:
$ ./test_tool.sh -t CtiBarrierTest
```

The actual test names can be found in `./function/avocado_tests.py`. Just use the
names of the python classes.

By default, the test script will rebuild all test programs upon each run. To skip
this, use the `--skip-build` or `-s` option:
```
# skip the build step
$ ./test_tool.sh --skip-build
# or
$ ./test_tool.sh -s
```

In order to write the results of the test to disk, `test_tool.sh` needs to be
in a directory where you have write permissions. To copy test data to a directory
where you have write permissions, execute:
```
# copy tests to another directory
$ ./test_tool.sh --cp <destination>
# or
$ ./test_tool.sh -c <destination>
```

Some of these options can be combined:
```
# skip the build step and run the CtiBarrierTest test
$ ./test_tool.sh -s -t CtiBarrierTest
```
