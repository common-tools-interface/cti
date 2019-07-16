#!/bin/bash
gcov_report_path=$PWD/../gcov_report
frontend_path=../../src/frontend
gcov_dirs=($frontend_path/ $frontend_path/cti_transfer/ $frontend_path/daemon/ $frontend_path/mpir_iface/ $frontend_path/frontend_impl/GenericSSH/ $frontend_path/frontend_impl/CraySLURM/)

# Check paramter count

if [ "$#" -gt 2 ] ; then
    echo "Illegal number of arguments."
    echo "Expected none or path to function tests directory"
    exit 1
fi

# Configure modules for gcov usage
module purge
module load gcc/8.1.0

# Change to tests/script directory and set variable to get back
cd ${1:-./}
START_DIR=$PWD

# Make sure that directory for storing gcov data exists

if ! test -d $gcov_report_path ; then
    if ! mkdir $gcov_report_path ; then
        echo "Failed to create gcov_report directory."
        exit 1
    fi
fi

# Clean up old test results

rm -r $gcov_report_path/*

# Go to each directory and compile gcov data
# and place results into a directory in gcov_report

for i in "${gcov_dirs[@]}"; do 
    cd $i
    dir_name=${PWD##*/}
    mkdir -p $gcov_report_path/$dir_name
    cp -r .libs/*.gc* ./
    for j in ./*.gcno ; do 
        filename="${j%.*}"
        mkdir -p $gcov_report_path/$dir_name/$filename
        touch $gcov_report_path/$dir_name/${filename}_report.txt
        gcov $filename > $gcov_report_path/$dir_name/${filename}_report.txt
        mv *.gcov $gcov_report_path/$dir_name/$filename
    done
    rm ./*.gc*
    cd $START_DIR
done
