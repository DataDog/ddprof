#!/bin/bash
set -euo pipefail 

ONAME="event_parser"

lex -o${ONAME}.lex.c ${ONAME}.l
yacc -v "-o${ONAME}.tab.c" -d ${ONAME}.y
cc -g ${ONAME}.lex.c ${ONAME}.tab.c -I../../../include -o event

#./event "JustAnEvent"
#./event "OneGroup:OneEvent"
#./event 555
#./event 'EventWithRegister%2'
#./event 'EventWithOffset$24'
#./event 'EventWithOffsetAndSize$24.4'
#./event 'EventWithMetric|M'
#./event 'EventWithCallgraph|G'
#./event 'EventWithBoth|MG'
#./event 'Group:Event$2.1@5|MG'

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
