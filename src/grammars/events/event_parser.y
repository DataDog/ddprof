%{
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "event_config.h"

/*
#define YYSTYPE char *
*/

int yydebug = 0;
void yyerror(const char *str) {fprintf(stderr,"err: %s\n", str);}
int yywrap() { return 1;}

typedef struct yy_buffer_state * YY_BUFFER_STATE;
extern int yylex();
extern int yyparse();
extern YY_BUFFER_STATE yy_scan_string(char * str);
extern void yy_delete_buffer(YY_BUFFER_STATE buffer);

void ev_splice(EventConf *A, const EventConf *B) {
  // Accumulate into A only if A has undefined values
  if (!A->has_id && B->has_id) {
    A->has_id = B->has_id;
    A->id = B->id;
  }
  if (!A->eventname && B->eventname) {
    A->eventname = B->eventname;
    A->groupname = B->groupname;
  }

  if (A->type == EVENT_NONE && B->type != EVENT_NONE)
    A->type = B->type;

  if (A->loc_type == ECLOC_FREQ && B->loc_type != ECLOC_FREQ) {
    A->loc_type = B->loc_type;
    A->register_num = B->register_num;
    A->size = B->size;
    A->offset = B->offset;
  }

  if (A->cad_type == ECCAD_UNDEF && B->cad_type != ECCAD_UNDEF) {
    A->cad_type = B->cad_type;
    A->cadence = B->cadence;
  }
}

void ev_print(const EventConf *tp) {
  if (tp->has_id)
    printf("  id: %lu\n", tp->id);
  else if (tp->groupname)
    printf("  tracepoint: %s:%s\n", tp->groupname, tp->eventname);
  else
    printf("  event: %s\n", tp->eventname);
  
  const char *typenames[] = {"ILLEGAL", "callgraph", "metric", "metric and callgraph"};
  printf("  type: %s\n", typenames[tp->type]);


  if (tp->loc_type  == ECLOC_FREQ)
    printf("  location: count\n");
  else if (tp->loc_type == ECLOC_REG)
    printf("  location: register %d\n", tp->register_num);
  else if (tp->loc_type == ECLOC_RAW)
    printf("  location: raw event (offset %lu with size %d bytes)\n", tp->offset, tp->size);

  printf("\n");

}

int main(int c, char **v) { 
  if (c) {
    printf("%s\n", v[1]);
    YY_BUFFER_STATE buffer = yy_scan_string(v[1]);
    yyparse();
    yy_delete_buffer(buffer);
  } else {
    yyparse();
  }
  return 0;
}
%}

%name event_parse

%union {
	uint64_t num;
	char *str;
	char typ;
	EventConf event;
};

%token <num> NUMBER HEXNUMBER
%token <str> WORD
%token EVENTSEP WORD REGISTERSEP OFFSETSEP SIZESEP PERIODSEP FREQUENCYSEP TYPESEP

%type <num> register offset size event_id period frequency
%type <str> groupname eventname typespec
%type <event> event name location cadence type

%%

events:
	   |
	   events event{
		ev_print(&$2);
	   }
	   ;

event: name location cadence type {
		memset(&$$, 0, sizeof($$));
		ev_splice(&$$, &$1);
		ev_splice(&$$, &$2);
		ev_splice(&$$, &$3);
		ev_splice(&$$, &$4);
	}
	;

type:
	{
		memset(&$$, 0, sizeof($$));
		$$.type = EVENT_CALLGRAPH;
	} | TYPESEP typespec {
		memset(&$$, 0, sizeof($$));
		$$.type = EVENT_NONE;
		char *msg = $2;
		size_t sz = sizeof(msg);
		for (int i = 0; i < sz; i++) {
			if (msg[i] == 'M' || msg[i] == 'm')
				$$.type |= EVENT_METRIC;
			if (msg[i] == 'G' || msg[i] == 'g')
				$$.type |= EVENT_CALLGRAPH;
		}
	}

/* Don't forget to make events valid! */
name:
	groupname EVENTSEP eventname { 
		memset(&$$, 0, sizeof($$));
		$$.groupname = $1;
		$$.eventname = $3;
		$$.id = 0;
		$$.has_id = false;
	  } | eventname { 
		memset(&$$, 0, sizeof($$));
		$$.groupname = NULL;
		$$.eventname = $1;
		$$.id = 0;
		$$.has_id = false;
	  } | event_id {
		memset(&$$, 0, sizeof($$));
		$$.groupname = NULL;
		$$.eventname = NULL;
		$$.id = $1;
		$$.has_id = true;
	  }
	  ;

location:
        {
		memset(&$$, 0, sizeof($$));
	  	$$.loc_type = ECLOC_FREQ;
	} | REGISTERSEP register { 
		memset(&$$, 0, sizeof($$));
		$$.loc_type = ECLOC_REG;
		$$.register_num = $2;
	} | OFFSETSEP offset {
		memset(&$$, 0, sizeof($$));
		$$.loc_type = ECLOC_RAW;
		$$.offset = $2;
		$$.size = sizeof(uint64_t);
	} | OFFSETSEP offset SIZESEP size {
		memset(&$$, 0, sizeof($$));
		$$.loc_type = ECLOC_RAW;
		$$.offset = $2;
		$$.size = $4;
		$$.size = $$.size < 1 ? 1 : $$.size;
	}
	;

cadence: 
	{
		memset(&$$, 0, sizeof($$));
		$$.cad_type = ECCAD_UNDEF;
 	} | PERIODSEP period {
		memset(&$$, 0, sizeof($$));
		$$.cad_type = ECCAD_PERIOD;
		$$.cadence = $2;
	} | FREQUENCYSEP frequency {
		memset(&$$, 0, sizeof($$));
		$$.cad_type = ECCAD_FREQ;
		$$.cadence = $2;
	}
	;

typespec: WORD
groupname: WORD
eventname: WORD
register: NUMBER
offset: NUMBER | HEXNUMBER
size: NUMBER | HEXNUMBER
event_id: NUMBER
period: NUMBER
frequency: NUMBER
