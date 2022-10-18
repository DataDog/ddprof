%{
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
//#include <string.h>
#include <string>

#include "event_config.hpp"
#include "perf_archmap.hpp"

/*
#define YYSTYPE char *
*/

#define YYDEBUG 0

typedef struct yy_buffer_state * YY_BUFFER_STATE;
extern int yylex(void);
extern int yyparse(void);
extern YY_BUFFER_STATE yy_scan_string(const char * str);
extern void yy_delete_buffer(YY_BUFFER_STATE buffer);

uint8_t mode_from_str(const std::string &str) {
  uint8_t mode = EVENT_NONE;
  if (str.empty())
    return mode;

  const std::string m_str{"Mm"};
  const std::string g_str{"Gg"};
  const std::string a_str{"Aa*"};

  for (const char &c : str) {
    if (m_str.find(c) != std::string::npos)
      mode |= EVENT_METRIC;
    if (g_str.find(c) != std::string::npos)
      mode |= EVENT_CALLGRAPH;
    if (a_str.find(c) != std::string::npos)
      mode |= EVENT_BOTH;
  }
  return mode;
}

void conf_finalize(EventConf *conf) {
  // Generate label if needed
  // * if both, "<eventname>:<groupname>"
  // * if only event, "<eventname>"
  // * if neither, but id, "id:<id>"
  // * if none, then this is an invalid event anyway
  if (conf->label.empty()) {
    if (!conf->eventname.empty() && !conf->groupname.empty()) {
      conf->label = conf->groupname + ":" + conf->eventname;
    } else if (!conf->eventname.empty() && conf->groupname.empty()) {
      conf->label = conf->eventname;
    } else if (conf->id) {
      conf->label = "id:" + std::to_string(conf->id);
    }
  }
}

void conf_print(const EventConf *tp) {
  if (tp->id)
    printf("  id: %lu\n", tp->id);
  else if (!tp->groupname.empty())
    printf("  tracepoint: %s:%s\n", tp->groupname.c_str(), tp->eventname.c_str());
  else
    printf("  event: %s\n", tp->eventname.c_str());

  if (!tp->label.empty())
    printf("  label: %s\n", tp->label.c_str());
  else
    printf("  label: <generated from event/groupname>\n");
  
  const char *modenames[] = {"ILLEGAL", "callgraph", "metric", "metric and callgraph"};
  printf("  type: %s\n", modenames[tp->mode]);


  if (tp->loc_type  == ECLOC_VAL)
    printf("  location: value\n");
  else if (tp->loc_type == ECLOC_REG)
    printf("  location: parameter%d\n", tp->register_num);
  else if (tp->loc_type == ECLOC_RAW)
    printf("  location: raw event (%lu with size %d bytes)\n", tp->arg_offset, tp->arg_size);

  if (tp->arg_coeff != 0)
    printf("  coefficient: %f\n", tp->arg_coeff);

  printf("\n");

}

EventConf g_accum_event_conf = {};

void yyerror(const char *str) {
#ifdef EVENT_PARSER_MAIN
  fprintf(stderr, "err: %s\n", str);
#endif 
}

#define VAL_ERROR() \
 do { \
   yyerror("Invalid value"); \
   YYABORT;  \
 } while(0)

EventConf *EventConf_parse(const char *msg) {
  g_accum_event_conf = EventConf{};
  int ret = -1;
  YY_BUFFER_STATE buffer = yy_scan_string(msg);
  ret = yyparse();
  yy_delete_buffer(buffer);
  return 0 == ret ? &g_accum_event_conf : NULL;
}

#ifdef EVENT_PARSER_MAIN
bool g_debugout_enable = false;
int main(int c, char **v) {
  g_debugout_enable = false;
  if (c) {
    printf(">\"%s\"\n", v[1]);
    YY_BUFFER_STATE buffer = yy_scan_string(v[1]);
    g_debugout_enable = true;
    if (!yyparse())
      conf_print(&g_accum_event_conf);
    else
      fprintf(stderr, "  ERROR\n");
    yy_delete_buffer(buffer);
  } else {
    yyparse();
  }
  return 0;
}
#endif
%}

%union {
	uint64_t num;
	std::string *str;
	char typ;
	double fpnum;
	EventConfField field;
};

%token EQ OPTSEP CONFSEP
%token <fpnum> FLOAT
%token <num> NUMBER HEXNUMBER
%token <str> WORD
%token <str> KEY

%type <num> uinteger
%type <field> conf
%type <field> opt

%%

// this only allows a single config to be processed at a time
// ... and has ugly whitespace stripping
confs:
      conf { conf_finalize(&g_accum_event_conf); }
      | conf CONFSEP { conf_finalize(&g_accum_event_conf); }
      ;

conf: // can be empty; nothing happens
    | opt
    | conf OPTSEP conf 
    ;

opt: // can be empty; nothing happens
   | WORD { g_accum_event_conf.eventname = *$1; }
   | KEY EQ WORD {
       switch($$) {
         case ECF_EVENT: g_accum_event_conf.eventname = *$3; break;
         case ECF_GROUP: g_accum_event_conf.groupname = *$3; break;
         case ECF_LABEL: g_accum_event_conf.label = *$3; break;
         case ECF_MODE:  g_accum_event_conf.mode |= mode_from_str(*$3); break;
         default: VAL_ERROR(); break;
       }
     }
     | KEY EQ WORD ':' WORD {
       if ($$ == ECF_EVENT || $$ == ECF_GROUP) {
         g_accum_event_conf.eventname = *$3;
         g_accum_event_conf.groupname = *$5;
       }
     }
     | KEY EQ uinteger {
       switch($$) {
         case ECF_ID: g_accum_event_conf.id = $3; break;
         case ECF_ARGSIZE: g_accum_event_conf.arg_size= $3; break;
         case ECF_ARGCOEFF: g_accum_event_conf.arg_coeff = 0.0 + $3; break;
         case ECF_MODE: g_accum_event_conf.mode = $3 & EVENT_BOTH; break;
           break;

         case ECF_PARAMETER:
         case ECF_REGISTER:
         case ECF_ARGOFFSET:
           // If the location type has already been set, then this is an error.
           if (g_accum_event_conf.loc_type) {
             VAL_ERROR();
             break;
           }
           if ($$ == ECF_PARAMETER) {
             g_accum_event_conf.loc_type = ECLOC_REG;
             unsigned int regno = param_to_regno_c($3);
             if (regno == -1u) {
               VAL_ERROR();
               break;
             }
             g_accum_event_conf.register_num = regno;
           }
           if ($$ == ECF_REGISTER) {
             if ($3 >= PERF_REGS_COUNT) {
               VAL_ERROR();
               break;
             }
             g_accum_event_conf.loc_type = ECLOC_REG;
             g_accum_event_conf.register_num = $3;
           }
           if ($$ == ECF_ARGOFFSET) {
             g_accum_event_conf.loc_type = ECLOC_RAW;
             g_accum_event_conf.arg_offset = $3;
           }
           break;

         case ECF_PERIOD:
         case ECF_FREQUENCY:
           // If the cadence has already been set, it's an error
           if (g_accum_event_conf.cad_type) {
             VAL_ERROR();
             break;
            }

           g_accum_event_conf.cadence = $3;
           if ($$ == ECF_PERIOD)
             g_accum_event_conf.cad_type = ECCAD_PERIOD;
           if ($$ == ECF_FREQUENCY)
             g_accum_event_conf.cad_type = ECCAD_FREQ;
           break;

         default: VAL_ERROR(); break;
       }
     }
     | KEY EQ FLOAT {
       if ($$ == ECF_ARGCOEFF)
         g_accum_event_conf.arg_coeff = $3;
       else
         VAL_ERROR();
     }
     ;

uinteger: NUMBER | HEXNUMBER
