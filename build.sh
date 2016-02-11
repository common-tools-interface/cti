#!/bin/bash

module purge;
mkdir install;
./configure --prefix=$PWD/install;
make;
make install;
make tests;
