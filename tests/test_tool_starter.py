#!/usr/bin/env python3

import sys, os, subprocess

def find_cdst_support_prefix():
    prefix = None
    if "CRAY_CDST_SUPPORT_INSTALL_DIR" in os.environ:
        prefix = os.environ["CRAY_CDST_SUPPORT_INSTALL_DIR"]
    else:
        prefix = "/opt/cray/pe/cdst-support/default"
        print("CRAY_CDST_SUPPORT_INSTALL_DIR not found in environment, assuming " + prefix + ".")
        print("If this location is not correct, manually load cray-cdst-support and run test_tool_starter.py again.")

    return prefix

if __name__ == "__main__":
    # load up cdst test library
    prefix = find_cdst_support_prefix()
    sys.path.insert(0, prefix + "/testing_lib")
    try:
        import test_tool
    except ModuleNotFoundError:
        print("Failed to load testing library. Make sure you've installed cray-cdst-support-devel in " + prefix + ".")
        exit(-1)

    test_tool.run_tests()