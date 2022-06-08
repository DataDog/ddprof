#!/bin/bash
set -euo pipefail 

lex event.l
yacc -d event.y
cc -g lex.yy.c y.tab.c -I../../../include -o event

./event "JustAnEvent"
./event "OneGroup:OneEvent"
./event 555
./event 'EventWithRegister%2'
./event 'EventWithOffset$8'
./event 'EventWithOffsetAndSize$8.24'
./event 'EventWithMetric|M'
./event 'EventWithCallgraph|G'
./event 'EventWithBoth|MG'
./event 'Group:Event$2.1@5|MG'
