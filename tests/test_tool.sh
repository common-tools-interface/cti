#!/bin/bash

SCRIPT=$(basename "$0")
BUILD=true
TEST_ALL=true
DIR=$(cd `dirname $0` && pwd)

run_tests() {
    # default action - run all tests

    if [ $BUILD = true ]; then
        if ! prebuild_checks ; then
            "Failed pre build checks."
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

pretest_checks() {
    # is the cray-cti module loaded?
    if [[ -z "$CTI_INSTALL_DIR" ]]; then
        echo "CTI_INSTALL_DIR not found. Trying to load cray-cti to fix it."
        if ! module load cray-cti &> /dev/null; then
            echo "Couldn't load cray-cti module."
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
    if ! eval "$CTI_TESTS_LAUNCHER $CTI_TESTS_LAUNCHER_ARGS ./function/tests/hello_mpi"; then
        echo "Can't run a basic mpi app. Do you need different launcher arguments?"
        return 1
    fi
    echo "Success."

    return 0
}

prebuild_checks() {
    if ! pkg-config --version 2>&1 > /dev/null; then
        echo "pkg-config not found."
        return 1
    fi

    if ! pkg-config common_tools_fe; then
        echo "Didn't find common tools headers, attempting to load cray-cti-devel module..."

        if ! module load cray-cti-devel; then
            echo "Couldn't load cray-cti-devel."
            return 1
        fi
        echo "Successfully loaded module."

        if ! pkg-config common_tools_fe; then
            echo "Loaded the cray-cti-devel module, but still couldn't find common tools headers..."
            return 1
        fi
    fi

    if ! pkg-config common_tools_be; then
        echo "Didn't find common tools headers, attempting to load cray-cti-devel module..."

        if ! module load cray-cti-devel; then
            echo "Couldn't load cray-cti-devel."
            return 1
        fi
        echo "Successfully loaded module."

        if ! pkg-config common_tools_be; then
            echo "Loaded the cray-cti-devel module, but still couldn't find common tools headers..."
            return 1
        fi
    fi

    if ! pkg-config cray-cdst-support; then
        echo "Didn't find cdst support headers, attempting to load cray-cdst-support module..."

        if ! module load cray-cdst-support; then
            echo "Couldn't load cray-cdst-support."
            return 1
        fi
        echo "Successfully loaded module."

        if ! pkg-config cray-cdst-support; then
            echo "Loaded the cray-cdst-support module, but still couldn't find cdst support headers..."
            return 1
        fi
    fi
}

build() {
    local source_files_glob="$DIR/function/tests/*.c \
                             $DIR/function/tests/*.cpp \
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
