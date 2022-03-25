#!/usr/bin/env python3

# forward to ./cdst-test/test_tool.py

import sys, os

if __name__ == "__main__":
    # load up cdst test library
    sys.path.insert(0, os.getcwd() + "/cdst-test")
    try:
        from test_tool import run_tests
    except ImportError:
        print("Failed to load testing library. Make sure you've initialized submodules.")
        print("Try \"git submodule update --init ./cdst-test\"")
        exit(-1)

    run_tests()
