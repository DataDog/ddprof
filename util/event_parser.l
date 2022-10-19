/*
  Unless explicitly stated otherwise all files in this repository are licensed
  under the Apache License Version 2.0. This product includes software
  developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
  Datadog, Inc.
*/

%option noyywrap
%option nounput
%option noinput

%{
#include <string.h>
#include "event_config.hpp"
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
[[:alpha:]_][[:alnum:]_]*   {
	yylval.str = new std::string{yytext};
	BEGIN 0;
	return WORD;
}
[[:digit:]]+            {
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

c|arg_coeff|coeff          DISPATCH(ARGCOEFF)
f|frequency|freq           DISPATCH(FREQUENCY)
e|event|eventname|ev       DISPATCH(EVENT)
g|group|groupname|gr       DISPATCH(GROUP)
i|id                       DISPATCH(ID)
l|label                    DISPATCH(LABEL)
m|mode                     DISPATCH(MODE)
n|arg_num|argno            DISPATCH(PARAMETER)
o|arg_offset|argoff        DISPATCH(ARGOFFSET)
p|period|per               DISPATCH(PERIOD)
r|register|regno           DISPATCH(REGISTER)
z|arg_size|argsz           DISPATCH(ARGSIZE )

=                       {
	BEGIN VALUE;
	return EQ;
}

[ ,\t][ \t]*            {
	BEGIN 0;
	return OPTSEP;
}

;                       {
	BEGIN 0;
	return CONFSEP;
}

[[:alpha:]_][[:alnum:]_]*   {
	yylval.str = new std::string{yytext};
	BEGIN 0;
	return WORD;
}

.                       {
	return (int) yytext[0];
}
