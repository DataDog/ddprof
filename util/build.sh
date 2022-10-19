#!/bin/bash
# Unless explicitly stated otherwise all files in this repository are licensed under the Apache License Version 2.0.
# This product includes software developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present Datadog, Inc.
set -euo pipefail 

ONAME="event_parser"

flex -v -o${ONAME}.lex.c ${ONAME}.l
bison -v -o"${ONAME}.tab.c" -d ${ONAME}.y --header=event_parser.h
cc -DDEBUG -DEVENT_PARSER_MAIN ${ONAME}.lex.c ${ONAME}.tab.c ../src/perf_archmap.cc -I../include -I. -o event

# This can be used for debugging the parser
#./event 'ThisIsAnEventName'
