%{
#include <string.h>
#include "event_config.h"
#include "y.tab.h"
%}

%%

[a-zA-Z_][a-zA-Z0-9_]*    {
	yylval.str = strdup(yytext);
	return WORD;
}
[[:digit:]]+            {
	yylval.num = strtol(yytext, NULL, 10);
	return NUMBER;
}
0x[[:digit:]]+          {
	yylval.num = strtol(yytext, NULL, 16);
	return HEXNUMBER;
}

[\|]                    {return TYPESEP;}
[/:]                    {return EVENTSEP;}
[%]                     {return REGISTERSEP;}
[$]                     {return OFFSETSEP;}
[.]                     {return SIZESEP;}
[,]                     {return PERIODSEP;}
[@]                     {return FREQUENCYSEP;}
