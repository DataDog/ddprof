#!/bin/bash

### Set directory names
CURRENTDIR=$PWD
SCRIPTPATH=$(readlink -f "$0")
SCRIPTDIR=$(dirname $SCRIPTPATH)
cd $SCRIPTDIR/../../
TOP_LVL_DIR=$PWD
cd $CURRENTDIR

for i in {1..10}; do
  $TOP_LVL_DIR/release/collatz 1 1000000 A
  $TOP_LVL_DIR/release/collatz 2 1000000 B
  $TOP_LVL_DIR/release/collatz 3 1000000 C
done
