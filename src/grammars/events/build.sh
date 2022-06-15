#!/bin/bash
set -euo pipefail 

ONAME="event_parser"

lex -o${ONAME}.lex.c ${ONAME}.l
yacc "-o${ONAME}.tab.c" -d ${ONAME}.y
cc -g ${ONAME}.lex.c ${ONAME}.tab.c -I../../../include -o event

./event "JustAnEvent"
./event "OneGroup:OneEvent"
./event 555
./event 'EventWithRegister%2'
./event 'EventWithOffset$24'
./event 'EventWithOffsetAndSize$24.4'
./event 'EventWithMetric|M'
./event 'EventWithCallgraph|G'
./event 'EventWithBoth|MG'
./event 'Group:Event$2.1@5|MG'
