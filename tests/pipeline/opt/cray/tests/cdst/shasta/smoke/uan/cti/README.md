# CTI Pipeline Smoke Tests

These smoke tests are made to be as lightweight as possible. They aim to answer
the question: "Is a version of cti and cti-devel installed and configured
correctly on this system?"

## Dependencies and Preconditions

To be as lightweight as possible, there is no special dependency/setup logic
built into these tests. The tests *check* for a valid environment but leave the
*setup* to the user/test runner/dockerfile/pipeline/etc.

These tests depend on the following to run:

- `avocado` in PATH
- `module` in PATH, valid `$MODULESHOME`
- `cray-cti` and `cray-cti-devel` modulefiles *available* to load, but not loaded yet
- `cc`, `CC`, `make`, `install`, and `pkg-config` in PATH
- `$TEST_BASE_DIR` set to the directory that contains the /opt in /opt/cray/tests/. This
  might be `/` when running tests actually installed at /opt/cray/tests, or some other
  path when running the tests from a repo checkout.

## Running the Tests

```
export TEST_BASE_DIR=<cti repo root>/tests/pipeline
avocado run ./tests.py
```

## Troubleshooting

### All Tests Immediately Fail With a Python Error Trace

If you run the tests and immediately see a wall of red error messages like this:

```
 (1/6) ./tests.py:CtiModules.test_module_load: ERROR: Traceback (most recent call last):\n  File "/home/users/chandlej/Code/cdst-cdst-test/tests/avocado-virtual-environment/avocado/lib64/python3.6/importlib/__init__.py", line 126, in import_module\n    return _bootstrap._gcd_import(name[level:], package, level... (0.11 s)
 ```

 You probably didn't set `$TEST_BASE_DIR`. `tests.py` relies on `$TEST_BASE_DIR` to find
 the supporting cdst testing library and import it. The error messages are import errors.

 To fix:

 ```
 export TEST_BASE_DIR=<CTI git repo root>/tests/pipeline
 ```
