#!/bin/bash

module purge;
mkdir install;
./configure --prefix=$PWD/install --with-gdb;
make;
make install;
make tests;

mkdir install/docs
cp extras/docs/ATTRIBUTIONS_cti.txt install/docs

rm -f $PWD/install/lib/pkgconfig/*
cp $PWD/scripts/craytools_be.pc  $PWD/install/lib/pkgconfig/craytools_be.pc
cp $PWD/scripts/craytools_fe.pc  $PWD/install/lib/pkgconfig/craytools_fe.pc
chmod +rwx $PWD/install/lib/pkgconfig/craytools_be.pc
chmod +rwx $PWD/install/lib/pkgconfig/craytools_fe.pc
