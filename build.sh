#!/bin/bash

module purge;
mkdir install;
./configure --prefix=$PWD/install --with-gdb;
make;
make install;
make tests;

mkdir install/docs
cp extras/docs/ATTRIBUTIONS_cti.txt install/docs
