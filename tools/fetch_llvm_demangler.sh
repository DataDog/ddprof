#!/bin/bash
# http://redsymbol.net/articles/unofficial-bash-strict-mode/
set -euo pipefail
IFS=$'\n\t'

### Set directory names
CURRENTDIR=$PWD
SCRIPTPATH=$(readlink -f "$0")
SCRIPTDIR=$(dirname $SCRIPTPATH)
cd $SCRIPTDIR/../
TOP_LVL_DIR=$PWD
cd $CURRENTDIR

### Nuke the old llvm dir
LLVM_ROOT=$TOP_LVL_DIR/vendor/llvm
rm -rf $LLVM_ROOT
mkdir -p $LLVM_ROOT/include/llvm/
mkdir -p $LLVM_ROOT/lib/

### Pull in just the parts of the llvm repo which matter for demangling.
# we make use of the fact that Github allows certain SVN operations, because
# doing this with a sparse git checkout is slightly painful
svn export https://github.com/llvm/llvm-project/trunk/llvm/include/llvm/Demangle $LLVM_ROOT/include/llvm/Demangle
svn export https://github.com/llvm/llvm-project/trunk/llvm/lib/Demangle $LLVM_ROOT/lib/Demangle
