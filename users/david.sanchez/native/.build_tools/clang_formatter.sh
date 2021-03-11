#!/bin/bash
# http://redsymbol.net/articles/unofficial-bash-strict-mode/
set -euo pipefail
IFS=$'\n\t'

# Figure out latest clang-format command.  This might be different than the
# user's `clang-format` if they have one, but that's the point.
CLANG_FORMAT=""
if   command -v clang-format-12 >/dev/null 2>&1; then CLANG_FORMAT=clang-format-12
elif command -v clang-format-11 >/dev/null 2>&1; then CLANG_FORMAT=clang-format-11
elif command -v clang-format-10 >/dev/null 2>&1; then CLANG_FORMAT=clang-format-10
elif command -v clang-format-9  >/dev/null 2>&1; then CLANG_FORMAT=clang-format-9
else echo "No suitable clang-format found." && exit 1
fi

RC=0
APPLY="NO"
[[ "$#" -ge "1" && "$1" == "apply" ]] && APPLY="YES"

# Setup a tmpfile 
tmpfile=$(mktemp /tmp/clang-format-diff.XXXXXX)
exec 5<>$tmpfile
rm -f $tmpfile
tmpfile="/dev/fd/5"

# Look for matching extensions and try to diff them.
for f in $(git ls-files -m | grep -E '.*\.(c|cc|cp|cpp|cxx|c++|h|hh|hp|hpp|hxx|h++)$'); do
  echo CHECKING $f
  if [ ${APPLY} == "YES" ]; then
    ${CLANG_FORMAT} -style=file -i $f
  else
    ${CLANG_FORMAT} -style=file $f > $tmpfile
    if ! cmp -s $f /dev/fd/5; then
      diff --color=always $f $tmpfile
      RC=1
    fi
  fi
done

exit $RC
