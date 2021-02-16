#!/bin/bash
# http://redsymbol.net/articles/unofficial-bash-strict-mode/
set -euo pipefail
IFS=$'\n\t'

# Figure out latest clang-format command.
CLANG_FORMAT=""
if   command -v clang-format-11 >/dev/null 2>&1; then CLANG_FORMAT=clang-format-11
elif command -v clang-format-10 >/dev/null 2>&1; then CLANG_FORMAT=clang-format-10
elif command -v clang-format-10 >/dev/null 2>&1; then CLANG_FORMAT=clang-format-9
else echo "No suitable clang-format found." && exit -1
fi

for f in $(find . -name '*.h'   -o -name '*.cpp' -o -name '*.cxx' -o \
                  -name '*.c++' -o -name '*.cc'  -o -name '*.cp'  -o \
                  -name '*.c'   -o -name '*.i'   -o -name '*.ii'  -o \
                  -name '*.h'   -o -name '*.h++' -o -name '*.hpp' -o \
                  -name '*.hxx' -o -name '*.hh'  -o -name '*.inl' -o \
                  -name '*.inc' -o -name '*.ipp' -o -name '*.ixx' -o \
                  -name '*.txx' -o -name '*.tpp' -o -name '*.tcc' -o \
                  -name '*.tpl')
do
  ${CLANG_FORMAT} -style=file -i $f
done
