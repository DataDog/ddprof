#!/bin/bash
set -e

clean () {
  rm -f "$CFORMAT_TMP"
}

display_diff() {
  file="$1"
  CFORMAT_TMP=`mktemp`
  clang-format --style="{BasedOnStyle: Google, IndentWidth: 2, ColumnLimit: 120}" "$file" > "$CFORMAT_TMP"
  diff -u "$file" "$CFORMAT_TMP"
  rm -f "$CFORMAT_TMP"
}

trap clean EXIT

################################################################################
if [ 0 -eq $# ]; then
  for f in $(git ls-files '*.[ch]'); do display_diff $f; done
else
  display_diff $1
fi

