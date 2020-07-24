#!/bin/bash
# 
# Copyright 2019-2020 Cray Inc. All Rights Reserved.
# 
# Unpublished Proprietary Information.
# This unpublished work is protected to trade secret, copyright and other laws.
# Except as permitted by contract or express written permission of Cray Inc.,
# no part of this work or its content may be used, reproduced or disclosed
# in any form.
# 
# test_tool.sh
#
# This script is the "button" in "push button testing".
# running it with no arguments will build and run all functional tests.
# see ./test_tool.sh -h for other options.

SCRIPT=$(basename "$0")
BUILD=true
TEST_ALL=true
DIR=$(cd `dirname $0` && pwd)

# this is the default action of the script - perform checks and then build and run all tests
run_tests() {
    if [ $BUILD = true ]; then
        if ! prebuild_checks ; then
            echo "Failed pre build checks."
            exit 1
        fi

        if ! build ; then
            echo "Failed to build test program binaries."
            exit 1
        fi
    fi

    if ! pretest_checks ; then
        echo "Failed prerequisite checks."
        exit 1
    fi

    cd "$DIR/function"
    ./build_run.sh $@
    cd "$DIR"
}

# check performed after tests have been built but before they are run
pretest_checks() {
    # does CTI_INSTALL_DIR exist?
    if [[ -z "$CTI_INSTALL_DIR" ]]; then
        echo "CTI_INSTALL_DIR not found. Trying to load cray-cti to fix it."
        if ! require_module "cray-cti"; then
            return 1
        fi
        echo "Success."
    fi

    # are we in a writable directory?
    if ! [ -w `pwd` ]; then
        echo "Current directory is not writable."
        echo "Run ./$SCRIPT --cp <directory> to install the tests in a writable directory."
        return 1
    fi

    if ! source ./scripts/system_specific_setup.sh; then
        echo "Failed to get system specific setup."
        return 1
    fi

    # can i run jobs?
    echo "Trying to run a simple job..."
    if ! eval "$CTI_TESTS_LAUNCHER $CTI_TESTS_LAUNCHER_ARGS /usr/bin/hostname"; then
        echo "Can't run a basic job. Do you have enough nodes allocated?"
        return 1
    fi
    echo "Success."

    # can i run mpi apps?
    echo "Trying to run an mpi app..."
    if ! eval "$CTI_TESTS_LAUNCHER $CTI_TESTS_LAUNCHER_ARGS ./function/src/hello_mpi"; then
        echo "Can't run a basic mpi app. Do you need different launcher arguments?"
        return 1
    fi
    echo "Success."

    return 0
}

# checks performed before building tests
prebuild_checks() {
    if ! pkg-config --version 2>&1 > /dev/null; then
        echo "pkg-config not found."
        return 1
    fi

    if ! require_pkg_config "common_tools_fe" "cray-cti-devel"; then
        echo "Couldn't find required pkg-config files for common_tools_fe."
        return 1
    fi

    if ! require_pkg_config "common_tools_be" "cray-cti-devel"; then
        echo "Couldn't find required pkg-config files for common_tools_be."
        return 1
    fi

    return 0
}

# build tests with autotools
build() {
    local source_files_glob="$DIR/function/src/*.c \
                             $DIR/test_support/*.c"
    
    # touch source files to force rebuild
    # stat to make sure they're there; otherwise touch *.c would create a file
    # called '*.c'.
    if stat $source_files_glob --printf='' 2>/dev/null ; then
        touch $source_files_glob
    else
        echo "Failed to stat all test program source files. Are some missing?"
        return 1
    fi

    cd "$DIR"
    if ! (autoreconf -ifv && ./configure) ; then
        echo "Failed while running autotools."
        return 1
    fi
    
    cd "$DIR"
    if ! make ; then
        echo "Failed to compile test program binaries."
        return 1
    fi

    return 0
}

# copy tests to another directory - useful when cray-cti-tests is installed in a
# directory only writable by root
copy_to_usage() { 
    echo "Usage: ./$SCRIPT [-c|--cp] <destination>" 
}
copy_to() {
    if [ "$#" -lt 1 ]; then
        copy_to_usage
        return 1
    fi

    local dest="$1/cti-tests"

    echo "Copying tests to $dest."
    if [ -d $dest ]; then
        echo "Couldn't copy: $dest already exists."
        return 1
    fi

    if ! cp -r "$DIR" $dest; then
        echo "Failed to copy."
        return 1
    fi

    echo "Now cd $dest && ./test_tool.sh"

    return 0
}

# check that a module exists on the system
check_for_module() {
    if [ "$#" -lt 1 ]; then
        echo "Warning: No module passed to check_for_module"
        return 1
    fi

    # module show exits with 0 regardless of the result, so we have
    # to use grep to check for an error message.

    if module show "$1" 2>&1 | grep "ERROR" -q; then
        return 1
    else
        return 0
    fi
}

# check that a module exists and load it
require_module() {
    if [ "$#" -lt 1 ]; then
        echo "Warning: No module passed to require_module"
        return 1
    fi

    if ! check_for_module "$1"; then
        echo "Couldn't find $1 module."
        return 1
    fi

    # check for any errors that will happen when we load the module.
    # because of module load's behavior with stdout and pipes, this won't actually 
    # load anything, only get module to emit an error if there will be one.
    if module load "$1" |& grep "ERROR"; then
        echo "Error while loading $1 module."
        return 1
    fi
    
    # now we do the actual module load.
    if ! module load "$1"; then
        echo "Couldn't load $1 module."
        return 1
    fi

    # check to make sure we actually loaded the module - module load returns 0
    # even on failure so we need to read the list ourselves
    if ! module list |& grep "$1/" -q; then
        echo "Couldn't load $1 module"
        return 1
    fi

    echo "Successfully loaded $1 module."
}

# check that pkg-config for a given library exists, and load a module to try to
# fix it if it doesn't.
#
# usage: require_pkg_config <pkg-config name> <module to load>
require_pkg_config() {
    if [ "$#" -lt 2 ]; then
        echo "Warning: Not enough arguments for require_pkg_config."
        return 1
    fi

    if ! pkg-config "$1"; then
        echo "Didn't find pkg-config for $1, attempting to load $2 module..."

        if ! require_module "$2"; then
            return 1
        fi

        if ! pkg-config "$1"; then
            echo "Loaded the $2 module, but still couldn't find pkg-config for $1..."
            return 1
        fi
    fi
}

# process script arguments
#
# -t: run a single test
# -c: copy tests to another specified directory
# -s: skip the build step and go straight to testing
# -h: show help
process_arguments() {
    if [ "$#" -ne 0 ]; then
        # process arguments
        while [ "$#" -gt 0 ]; do
            case $1 in
                -c|--cp) 
                    if [ "$#" -gt 1 ]; then
                        TEST_ALL=false
                        copy_to $2
                    else
                        copy_to_usage
                        return 1
                    fi
                    shift ;;
                -t|--test)
                    if [ "$#" -gt 1 ]; then
                        run_tests $2
                        TEST_ALL=false
                    else
                        echo "Usage: ./$SCRIPT [-t|--test] <test name>"
                        return 1
                    fi
                    shift ;;
                -s|--skip-build)
                    echo "Skipping the build step."
                    BUILD=false
                    ;;
                -h|--help)
                    TEST_ALL=false
                    echo "    ./$SCRIPT [options]"
                    echo "    [-t|--test] <test name> : run a single test. if this option isn't used, all tests are run."
                    echo "    [-c|--cp] <directory>   : copy tests to a directory."
                    echo "    [-s|--skip-build]       : skip the build step."
                    echo "    [-h|--help]             : show this help message."
                    ;;
                *) 
                    echo "Unknown parameter passed: $1"
                    return 1 ;;
            esac
            shift
        done
    fi

    if [ $TEST_ALL = true ]; then
        run_tests
    fi

    return 0
}

process_arguments $@
