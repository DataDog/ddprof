#!/bin/bash
set -euo pipefail 
# This should only need to be run when the .y or .l files change

ONAME="event_parser"
INCLUDE_DIR=../include
SOURCE_DIR=../src/${ONAME}

lex -o${ONAME}.lex.c ${ONAME}.l
yacc -v "-o${ONAME}.tab.c" -d ${ONAME}.y
cc -O3 -DEVENT_PARSER_MAIN ${ONAME}.lex.c ${ONAME}.tab.c ../src/perf_archmap.cc -I${INCLUDE_DIR} -o event
mkdir -p ${SOURCE_DIR}
mv ${ONAME}.h ${INCLUDE_DIR}
mv *.c ${SOURCE_DIR}

./event 'event=value'
./event 'eventname=value'
./event 'e=value'
./event 'e=event,g=group'
./event 'e=event g=group'
./event 'e=event+g=group'
./event 'e=ev g=gr c=0.1'
./event 'e=ev g=gr c=.1'
./event 'e=ev g=gr c=1f'
./event 'e=ev c=-1'
./event 'e=ev c=+1'
./event 'shoe=foot'
./event 'zronk=foofoo'
