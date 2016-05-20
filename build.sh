#!/bin/bash
#1
module purge;
#DATE=`date +%s`
#DATE=`date -d @$DATE "+%Y-%m-%d"`
if [[ $1 == "-d" && ! -z $2 ]]
then
  BUILD_DIR=$2
else
  BUILD_DIR=$PWD/install
  if [ -d $BUILD_DIR ]
  then
    rm -rf $BUILD_DIR
  fi
  mkdir $BUILD_DIR
  chmod a+rwx $BUILD_DIR
  #mkdir $BUILD_DIR/install
  #chmod a+rwx $BUILD_DIR/install
fi
echo "Building to " $BUILD_DIR

./configure --prefix=$BUILD_DIR --with-gdb;
make;
make install
make tests;

mkdir $BUILD_DIR/docs
cp $PWD/extras/docs/ATTRIBUTIONS_cti.txt $BUILD_DIR/docs

rm -f $BUILD_DIR/lib/pkgconfig/*
cp $PWD/scripts/craytools_be.pc  $BUILD_DIR/lib/pkgconfig/craytools_be.pc
cp $PWD/scripts/craytools_fe.pc  $BUILD_DIR/lib/pkgconfig/craytools_fe.pc
chmod a+rwx $BUILD_DIR/lib/pkgconfig/craytools_be.pc
chmod a+rwx $BUILD_DIR/lib/pkgconfig/craytools_fe.pc

