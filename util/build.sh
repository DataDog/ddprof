#!/bin/bash
set -euo pipefail 
# This should only need to be run when the .y or .l files change

ONAME="event_parser"
INCLUDE_DIR=../include
SOURCE_DIR=../src/${ONAME}

lex -o${ONAME}.lex.c ${ONAME}.l
yacc -v "-o${ONAME}.tab.c" -d ${ONAME}.y --debug
cc -DDEBUG -DEVENT_PARSER_MAIN ${ONAME}.lex.c ${ONAME}.tab.c ../src/perf_archmap.cc -I${INCLUDE_DIR} -o event
mkdir -p ${SOURCE_DIR}
mv ${ONAME}.h ${INCLUDE_DIR}
mv *.c ${SOURCE_DIR}

# This can be used for debugging
#./event 'ThisIsAnEventName'
