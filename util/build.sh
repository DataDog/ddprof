#!/bin/bash
set -euo pipefail 

ONAME="event_parser"
INCLUDE_DIR=../include
SOURCE_DIR=../src/${ONAME}

flex -v -o${ONAME}.lex.c ${ONAME}.l
bison -v -o"${ONAME}.tab.c" -d ${ONAME}.y
cc -DDEBUG -DEVENT_PARSER_MAIN ${ONAME}.lex.c ${ONAME}.tab.c ../src/perf_archmap.cc -I${INCLUDE_DIR} -o event
#mkdir -p ${SOURCE_DIR}
#mv ${ONAME}.h ${INCLUDE_DIR}
#mv *.c ${SOURCE_DIR}

# This can be used for debugging
#./event 'ThisIsAnEventName'
