/*
  Unless explicitly stated otherwise all files in this repository are licensed
  under the Apache License Version 2.0. This product includes software
  developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
  Datadog, Inc.
*/

%option noyywrap nounput noinput
%option reentrant
%option bison-bridge

%{
#include <string.h>
#include "event_config.hpp"
#include "event_parser.h"

#define DISPATCH(type)                       \
{                                            \
	yylval->field = (EventConfField::k##type);         \
	return KEY;                          \
}

%}

%x VALUE 

%%

<VALUE>{
[[:alpha:]_][[:alnum:]_]*   {
	yylval->str = new std::string{yytext};
	BEGIN 0;
	return WORD;
}
[-+]?[[:digit:]]+           {
	yylval->num = strtol(yytext, NULL, 10);
	BEGIN 0;
	return NUMBER;
}
[-+]?0x[[:digit:]]+         {
	
	yylval->num = strtol(yytext, NULL, 16);
	BEGIN 0;
	return HEXNUMBER;
}
[-+]?[[:digit:].]+[fFdD]?   {
	yylval->fpnum = strtod(yytext, NULL);
	BEGIN 0;
	return FLOAT;
}

.                           {
	BEGIN 0;
	return (int) yytext[0];
}
}

s|value_scale|scale        DISPATCH(ValueScale)
f|frequency|freq           DISPATCH(Frequency)
e|event|eventname|ev       DISPATCH(Event)
g|group|groupname|gr       DISPATCH(Group)
i|id                       DISPATCH(Id)
l|label                    DISPATCH(Label)
m|mode                     DISPATCH(Mode)
n|arg_num|argno            DISPATCH(Parameter)
o|raw_offset|rawoff        DISPATCH(RawOffset)
p|period|per               DISPATCH(Period)
r|register|regno           DISPATCH(Register)
z|raw_size|rawsz           DISPATCH(RawSize)

=                           {
	BEGIN VALUE;
	return EQ;
}

[ \t]* {}

[ ,\t]                      {
	BEGIN 0;
	return OPTSEP;
}

;                           {
	BEGIN 0;
	return CONFSEP;
}

[[:alpha:]_][[:alnum:]_]*   {
	yylval->str = new std::string{yytext};
	BEGIN 0;
	return WORD;
}

.                           {
	return (int) yytext[0];
}
