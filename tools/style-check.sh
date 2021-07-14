#!/bin/bash
# http://redsymbol.net/articles/unofficial-bash-strict-mode/
set -euo pipefail
IFS=$'\n\t'

# Set directory names
BASEDIR=$(dirname "$0")
cd ${BASEDIR}
cd ../

# Find most recent clang-format, defaulting to an unqualified default
CLANG_FORMAT=$(command -v clang-format{-12,-11,-10,-9,} | head -n 1)
if ! command -v "${CLANG_FORMAT}" > /dev/null 2>&1; then
  echo "No suitable clang-format found"
  exit 1
fi

# Process arguments
RC=0
[[ -z "${APPLY:-}" ]] && APPLY="no"
[[ "${1:-,,}" == "apply" ]] && APPLY="yes"

# Setup a tmpfile 
tmpfile=$(mktemp /tmp/clang-format-diff.XXXXXX)
exec 5<>$tmpfile
rm -f $tmpfile
tmpfile="/dev/fd/5"

# Look for matching extensions and try to diff them.
for f in $(find . -regextype posix-egrep -iregex '.*\.(c|cc|cp|cpp|cxx|c++|h|hh|hp|hpp|hxx|h++)$' | grep -v '^\./vendor' | grep -v 'CMakeFiles'); do
  if [ ! -f $f ]; then
    continue
  fi
  if [ ${APPLY,,} == "yes" ]; then
    ${CLANG_FORMAT} -style=file -i $f
  else
    ${CLANG_FORMAT} -style=file $f > $tmpfile
    if ! cmp -s $f /dev/fd/5; then
      diff -u --color=always $f $tmpfile
      RC=1
    fi
  fi
done

exit $RC
