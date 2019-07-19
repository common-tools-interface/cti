#!/bin/bash
PYTHON=python3


setup_gcovr() {
    if ! test -f ~/.local/bin/pip ; then
        echo "No pip detected. Installing..."
        if ! curl https://bootstrap.pypa.io/get-pip.py -o get-pip.py ; then
            echo "Failed to download pip setup script. Aborting..."
            return 1
        fi
        if ! $PYTHON get-pip.py --user ; then
            echo "Failed to run pip installer. Aborting..."
            return 1
        fi
        rm get-pip.py
    fi
    if ! test -f ~/.local/bin/gcovr ; then
        echo "gcovr not installed. Installing..."
        if ! ~/.local/bin/pip install gcovr --user ; then
            echo "Failed to install gcovr. Aborting..."
            return 1
        fi
    fi
    echo "Gcovr install is valid..."
    return 0
}

compiled_with_cc() {
    ls $SRC_DIR/frontend/.libs/*.gcno &> /dev/null
    return $?
}



# START MAIN CODE BLOCK

if [ "$#" -gt 2 ] ; then
    echo "Illegal number of arguments."
    echo "Expected none or path to coverage directory"
fi

# Change to coverage directory and set variable to get  back
cd ${1:-./}
START_DIR=$PWD

# Make sure in proper directory
if ! test -f ./build_coverage_report.sh ; then 
    echo "Invalid directory for execution."
    echo "Either launch in coverage directory or pass in path to it as a parameter"
    exit 1
fi

# Make sure gcov is installed properly
if ! setup_gcovr ; then
    exit 1
fi

# Get absolute path to src directory
cd ../../
SRC_DIR=$PWD/src/
cd $START_DIR

# Check that compiled with coverage checks in mind
if ! compiled_with_cc ; then
    echo "Failed to detect .gcno files. Please rebuild with --enable-code-coverage as a configure flag"
    exit 1
fi

# Setup modules appropriately
module purge
module load gcc/8.1.0

# Move all gcda and gcno files to where they need to be
GC_DIRS=$(find ${SRC_DIR}frontend/ -name "*.libs" -type d)
for libdir in $GC_DIRS; do
    cd $libdir
    cp *.gc* ../ &> /dev/null
done
cd $START_DIR

# Execute gcovr command
echo "Building coverage report..."
~/.local/bin/gcovr --root=$SRC_DIR -x > coverage.xml
echo "Coverage report created as 'coverage.xml'"
exit 0
