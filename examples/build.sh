#!/bin/bash

#Do not want to build with CCE or any network specific modules
module purge;
module load cray-cti/1.0.1

cc $(pkg-config --libs --cflags craytools_fe) cti_transfer_example.c -o cti_transfer

