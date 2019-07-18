#!/bin/bash

# Check paramter count
if [ "$#" -gt 2 ] ; then
    echo "ERROR:"
    echo "Illegal number of arguments."
    echo "Expected none or path to function tests directory"
    exit 1
fi

# Change to tests/script directory and set variable to get back
cd ${1:-./}
START_DIR=$PWD

# Make sure script is executing in the right place
if ! test -f ./create_gcov_report.sh ; then
    echo "Invalid directory for execution."
    echo "Either launch the script from tests/script or provide it with the path to tests/script"
    exit 1
fi

# Build variables to make directory work easy
cti=$PWD/../../
gcov_report_path=$cti/tests/gcov_report/
frontend_path=$cti/src/frontend/
gcov_dirs=($frontend_path/ $frontend_path/cti_transfer/ $frontend_path/daemon/ $frontend_path/mpir_iface/ $frontend_path/frontend_impl/GenericSSH/ $frontend_path/frontend_impl/CraySLURM/)

# Configure modules for gcov usage
module purge
module load gcc/8.1.0

# Make sure code was actually compiled with --enable-code-coverage
if ! ls $frontend_path/cti_transfer/.libs/*.gcno &> /dev/null ; then
    echo "Failed to detect gcno files"
    echo "Build with --enable-code-coverage as a configure flag"
    exit 1
fi

# Make sure that directory for storing gcov data exists
if ! test -d $gcov_report_path ; then
    if ! mkdir $gcov_report_path ; then
        echo "Failed to create gcov_report directory."
        exit 1
    fi
fi

# Clean up old test results
rm -rf $gcov_report_path/*

# Create directory for .gcov dump
if ! mkdir $gcov_report_path/gcov_all ; then
    echo "Failed to create gcov_all directory."
    exit 1
fi

# Go to each directory and compile gcov data
# and place results into directories in gcov_report
for i in "${gcov_dirs[@]}"; do 
    cd $i
    dir_name=${PWD##*/}
    mkdir -p $gcov_report_path/$dir_name
    if ls .libs/*.gcno &> /dev/null ; then
        cp -r .libs/*.gc* ./
    fi
    for j in ./*.gcno ; do 
        filename="${j%.*}"
        mkdir -p $gcov_report_path/$dir_name/$filename
        touch $gcov_report_path/$dir_name/$filename/${filename}_report.txt
        gcov -rk $filename > $gcov_report_path/$dir_name/$filename/${filename}_report.txt
        cp *.gcov $gcov_report_path/$dir_name/$filename
        mv *.gcov $gcov_report_path/gcov_all
    done
    cd $START_DIR
done
