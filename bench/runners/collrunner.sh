#!/bin/bash

### Set directory names
CURRENTDIR=$PWD
SCRIPTPATH=$(readlink -f "$0")
SCRIPTDIR=$(dirname $SCRIPTPATH)
cd $SCRIPTDIR/../../
TOP_LVL_DIR=$PWD
cd $CURRENTDIR

for i in {1..10}; do
  $TOP_LVL_DIR/release/collatz 4 20000 5000 G
  $TOP_LVL_DIR/release/collatz 4 20000 5000 G
  $TOP_LVL_DIR/release/collatz 4 20000 5000 G
done
