%{
#include <string.h>
#include "event_config.h"
#include "event_parser.h"

#define DISPATCH(type)                       \
{                                            \
	yylval.field = (ECF_##type);         \
	return KEY;                          \
}

%}

%x VALUE 

%%

<VALUE>{
[a-zA-Z_][a-zA-Z0-9_]*  {
	yylval.str = strdup(yytext);
	BEGIN 0;
	return WORD;
}
[+]?[[:digit:]]+            {
	yylval.num = strtol(yytext, NULL, 10);
	BEGIN 0;
	return NUMBER;
}
0x[[:digit:]]+          {
	
	yylval.num = strtol(yytext, NULL, 16);
	BEGIN 0;
	return HEXNUMBER;
}
[-+]?[[:digit:].]+[fFdD]?    {
	yylval.fpnum = strtod(yytext, NULL);
	BEGIN 0;
	return FLOAT;
}

.                       {
	BEGIN 0;
	return (int) yytext[0];
}
}

b|label                    DISPATCH(LABEL)
c|trace_arg_coeff|argcoeff DISPATCH(ARGCOEFF)
f|frequency|freq           DISPATCH(FREQUENCY)
e|event|eventname|ev       DISPATCH(EVENT)
g|group|groupname|gr       DISPATCH(GROUP)
i|trace_id|id              DISPATCH(ID)
l|location|loc             DISPATCH(LOCATION)
m|mode                     DISPATCH(MODE)
n|trace_arg_num|argno      DISPATCH(PARAMETER)
o|trace_arg_offset|argoff  DISPATCH(ARGOFFSET)
p|period|per               DISPATCH(PERIOD)
r|trace_reg_num|regno      DISPATCH(REGISTER)
z|trace_arg_size|argsz     DISPATCH(ARGSIZE)

=                       {
	BEGIN VALUE;
	return EQ;
}

[ ,]                    {
	BEGIN 0;
	return OPTSEP;
}

;                     {
	BEGIN 0;
	return OPTSEP;
}

[a-zA-Z_][a-zA-Z0-9_]*  {
	yylval.str = strdup(yytext);
	BEGIN 0;
	return WORD;
}

.                       {
	return (int) yytext[0];
}
