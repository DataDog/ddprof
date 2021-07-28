#!/bin/bash

### Set directory names
CURRENTDIR=$PWD
SCRIPTPATH=$(readlink -f "$0")
SCRIPTDIR=$(dirname $SCRIPTPATH)
cd $SCRIPTDIR/../../
TOP_LVL_DIR=$PWD
cd $CURRENTDIR

for i in {1..100}; do
  $TOP_LVL_DIR/deliverables/collatz 2 200000 500000 A
  $TOP_LVL_DIR/deliverables/collatz 2 200000 500000 A
done
