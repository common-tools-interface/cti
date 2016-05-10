#!/bin/bash
#1
module purge;
DATE=`date +%s`
DATE=`date -d @$DATE "+%Y-%m-%d"`
if [[ $1 == "-b" && ! -z $2 ]]
then
  BUILD_DIR=$2/$DATE
else
  BUILD_DIR=$PWD/install
fi
echo "Building to " $BUILD_DIR

if [ -d $BUILD_DIR ]
then
  rm -rf $BUILD_DIR
fi
mkdir $BUILD_DIR
chmod a+rwx $BUILD_DIR
mkdir $BUILD_DIR/install
chmod a+rwx $BUILD_DIR/install

./configure --prefix=$BUILD_DIR/install --with-gdb;
make;
make install
make tests;

mkdir $BUILD_DIR/install/docs
cp $PWD/extras/docs/ATTRIBUTIONS_cti.txt $BUILD_DIR/install/docs

rm -f $BUILD_DIR/install/lib/pkgconfig/*
cp $PWD/scripts/craytools_be.pc  $BUILD_DIR/install/lib/pkgconfig/craytools_be.pc
cp $PWD/scripts/craytools_fe.pc  $BUILD_DIR/install/lib/pkgconfig/craytools_fe.pc
chmod a+rwx $BUILD_DIR/install/lib/pkgconfig/craytools_be.pc
chmod a+rwx $BUILD_DIR/install/lib/pkgconfig/craytools_fe.pc
#cd $2
#ln -s $DATE latest

