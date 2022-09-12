#!/bin/bash
set -euo pipefail 

ONAME="event_parser"

flex -v -o${ONAME}.lex.c ${ONAME}.l
bison -v -o"${ONAME}.tab.c" -d ${ONAME}.y --header=event_parser.h
cc -DDEBUG -DEVENT_PARSER_MAIN ${ONAME}.lex.c ${ONAME}.tab.c ../src/perf_archmap.cc -I../include -I. -o event

# This can be used for debugging the parser
#./event 'ThisIsAnEventName'
